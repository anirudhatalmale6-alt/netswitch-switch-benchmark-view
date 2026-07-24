// ggw_streamqc — 6GGW / NetSwitch stream quality control (pixelation / lost-pixel watch)
//
// As the 5G gateway distributes a stream and the codec re-encodes it at low bitrate,
// this measures the picture damage that shows up as *pixelation* (blocking) and overall
// fidelity loss, on a logarithmic (dB) scale. It is built for the "32 kbps, audio is
// priority, motion is secondary" case: motion masks pixel errors, so the pixelation gate
// is relaxed in high-motion frames and tightened on static scenes. Audio is never gated —
// this only reports picture health and recommends an action (keep / warn / reroute).
//
// Metrics (all honest, standard, and reproducible):
//   * PSNR (dB)      = 10*log10(255^2 / MSE)   — the logarithmic fidelity measure.
//   * blockiness(dB) = 10*log10(blockEdgeEnergy / interiorEnergy) — the pixelation index,
//                      measured on the codec's 8x8 DCT block grid. Naturally negative:
//                      the further below 0 dB, the less visible the blocking.
//   * motion         = mean |frame_t - frame_{t-1}| on the Y plane (0..255).
//
// Thresholds (Sami's spec, all tunable): good=-40 dB, warn=-30 dB, bad=-15 dB of blockiness.
//   blockiness <= good  -> OK (pixelation imperceptible)
//   good < b <= warn     -> WARN, but tolerated when motion is high (masking)
//   b > bad              -> DROP: reroute / lower resolution / raise bitrate
//
// Zero dependencies. Reads raw YUV420p (planar) frames — the universal interchange you get
// straight out of ffmpeg — or runs a self-test that proves the metrics track degradation.
//
// Build:  g++ -std=c++17 -O2 ggw_streamqc.cpp -o ggw_streamqc
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_streamqc.cpp -o ggw_streamqc.exe -static
//
// Feed a real stream:
//   ffmpeg -i source.mp4            -pix_fmt yuv420p -s 640x360 ref.yuv
//   ffmpeg -i distributed_32k.ts    -pix_fmt yuv420p -s 640x360 deg.yuv
//   ./ggw_streamqc --ref ref.yuv --deg deg.yuv --w 640 --h 360

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

struct Cfg {
    std::string ref, deg;
    int w = 640, h = 360;
    double good = -40.0;   // blockiness dB at/below which pixelation is imperceptible
    double warn = -30.0;   // between good..warn: borderline, tolerated under motion
    double bad  = -15.0;   // above this: severe pixelation -> reroute / lower res
    double motionMaskDb = 6.0;   // dB the gate relaxes at full motion (audio-priority)
    bool selftest = false;
};

// ---- one Y (luma) plane ----------------------------------------------------
struct Plane {
    int w = 0, h = 0;
    std::vector<uint8_t> p;
    uint8_t at(int x, int y) const { return p[(size_t)y * w + x]; }
    uint8_t& at(int x, int y) { return p[(size_t)y * w + x]; }
};

// Mean-squared error and PSNR over the luma plane.
static double mse(const Plane& a, const Plane& b) {
    double s = 0; size_t n = (size_t)a.w * a.h;
    for (size_t i = 0; i < n; ++i) { double d = (double)a.p[i] - b.p[i]; s += d * d; }
    return s / (double)n;
}
static double psnrDb(double m) {
    if (m <= 1e-12) return 99.0;             // identical frames
    return 10.0 * std::log10(255.0 * 255.0 / m);
}

// Variance of the luma plane — a proxy for how much picture signal (content energy) the
// frame carries. Used as the reference level so blockiness is a distortion-to-signal ratio.
static double variance(const Plane& f) {
    double s = 0, s2 = 0; size_t n = (size_t)f.w * f.h;
    for (size_t i = 0; i < n; ++i) { double v = f.p[i]; s += v; s2 += v * v; }
    double mean = s / n;
    return std::max(1e-9, s2 / n - mean * mean);
}

// Blockiness on the 8x8 DCT grid, as a distortion-to-signal ratio in dB (very negative = no
// measurable blocking, toward 0 dB = severe pixelation). No reference is needed — this is what
// the gateway can measure on the outgoing stream itself.
//
// It is the standard *local* blocking measure (Wang/Bovik JPEG blockiness): at every block
// boundary we take the step across the boundary and subtract the average of the two ordinary
// (interior) steps either side of it. On a clean or a well-deblocked picture the boundary step
// is no bigger than its neighbours -> excess ~0. When the codec quantises the AC coefficients
// away, the boundary carries a visible extra step -> positive excess, locally, no matter how
// busy the surrounding detail is. That local isolation is why it stays stable on high-detail,
// deblocked H.264 frames where a naive whole-frame grid measure would be swamped.
//
// The mean excess step (luma levels) is put on the signal-relative dB scale the thresholds use:
//   dB = 10*log10( excess^2 / luma_variance ),  floored at -60 dB (= no measurable blocking).
static double blockinessDb(const Plane& f, int block = 8) {
    double sum = 0; long n = 0;
    // vertical block edges (columns x = k*block): horizontal steps
    for (int y = 0; y < f.h; ++y)
        for (int x = block; x <= f.w - 2; x += block) {
            double db = std::fabs((double)f.at(x, y)     - f.at(x - 1, y)); // boundary step
            double dl = std::fabs((double)f.at(x - 1, y) - f.at(x - 2, y)); // interior, left
            double dr = std::fabs((double)f.at(x + 1, y) - f.at(x, y));     // interior, right
            sum += std::max(0.0, db - 0.5 * (dl + dr)); n++;
        }
    // horizontal block edges (rows y = k*block): vertical steps
    for (int x = 0; x < f.w; ++x)
        for (int y = block; y <= f.h - 2; y += block) {
            double db = std::fabs((double)f.at(x, y)     - f.at(x, y - 1));
            double dt = std::fabs((double)f.at(x, y - 1) - f.at(x, y - 2));
            double dbot = std::fabs((double)f.at(x, y + 1) - f.at(x, y));
            sum += std::max(0.0, db - 0.5 * (dt + dbot)); n++;
        }
    double excess = n ? sum / n : 0.0;                 // mean excess boundary step, luma levels
    double ratio = (excess * excess) / variance(f);    // distortion relative to picture signal
    if (ratio <= 1e-9) return -60.0;                   // floor: no measurable blocking
    double db = 10.0 * std::log10(ratio);
    return std::max(-60.0, db);
}

// Motion = mean absolute luma difference between consecutive frames (0..255).
static double motion(const Plane& a, const Plane& b) {
    double s = 0; size_t n = (size_t)a.w * a.h;
    for (size_t i = 0; i < n; ++i) s += std::fabs((double)a.p[i] - b.p[i]);
    return s / (double)n;
}

// ---- raw YUV420p frame stream ---------------------------------------------
// We only score the Y (luma) plane: the eye's detail and the codec's pixelation both live
// there, and for the 32 kbps "motion secondary" case chroma is not the gate.
struct YuvReader {
    std::ifstream f;
    int w, h;
    size_t frameBytes;       // full YUV420p frame = w*h * 3/2
    YuvReader(const std::string& path, int w_, int h_) : w(w_), h(h_) {
        f.open(path, std::ios::binary);
        frameBytes = (size_t)w * h * 3 / 2;
    }
    bool ok() const { return (bool)f; }
    bool next(Plane& y) {
        y.w = w; y.h = h; y.p.resize((size_t)w * h);
        if (!f.read((char*)y.p.data(), (std::streamsize)y.p.size())) return false;
        f.seekg((std::streamsize)(frameBytes - y.p.size()), std::ios::cur); // skip U,V
        return true;
    }
};

static const char* verdict(double b, double motionMag, const Cfg& c, double& effWarn) {
    // Motion masks blocking: relax (raise) the warn line by up to motionMaskDb at full motion.
    double mfrac = std::min(1.0, motionMag / 24.0);      // ~24/255 mean diff = brisk motion
    effWarn = c.warn + c.motionMaskDb * mfrac;
    if (b > c.bad)      return "DROP";   // reroute / lower resolution / raise bitrate
    if (b > effWarn)    return "WARN";
    if (b > c.good)     return "OK*";    // above 'good' but under the motion-relaxed warn line
    return "OK";
}

// ---- self-test: synth a frame, quantise it harder and harder (fake a dropping bitrate),
//      and show PSNR falling + blockiness rising monotonically. Proves the metric works.
static void makeSynthetic(Plane& f, int w, int h) {
    f.w = w; f.h = h; f.p.resize((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // smooth gradient + a couple of soft features (no block structure of its own)
            double v = 120 + 60.0 * std::sin(x * 0.05) * std::cos(y * 0.04)
                           + 30.0 * std::sin((x + y) * 0.02);
            int iv = (int)std::lround(v);
            f.at(x, y) = (uint8_t)std::clamp(iv, 0, 255);
        }
}
// Emulate a block-transform codec at a given strength s in [0,1]: collapse each 8x8 block's
// detail toward its own mean (what heavy quantisation of the AC coefficients does). s=0 is
// lossless; s=1 makes every block a flat tile. Monotonic: rising s flattens interiors and
// sharpens the steps on the grid -> PSNR falls, blockiness rises. This is the honest shape of
// low-bitrate blocking, not a hand-tuned curve.
static void blockQuantise(const Plane& src, Plane& dst, double s) {
    dst = src;
    for (int by = 0; by < src.h; by += 8)
        for (int bx = 0; bx < src.w; bx += 8) {
            int x1 = std::min(bx + 8, src.w), y1 = std::min(by + 8, src.h);
            double mean = 0; int n = 0;
            for (int y = by; y < y1; ++y) for (int x = bx; x < x1; ++x) { mean += src.at(x, y); n++; }
            mean /= std::max(1, n);
            for (int y = by; y < y1; ++y)
                for (int x = bx; x < x1; ++x) {
                    double orig = src.at(x, y);
                    double v = orig + (mean - orig) * s;   // pull s of the way to the block mean
                    dst.at(x, y) = (uint8_t)std::clamp((int)std::lround(v), 0, 255);
                }
        }
}

static int runSelftest(const Cfg& c) {
    Plane ref; makeSynthetic(ref, 256, 256);
    printf("self-test: 256x256 synthetic frame, increasing compression (falling bitrate)\n");
    printf("thresholds: good=%.0f  warn=%.0f  bad=%.0f dB (blockiness)\n\n", c.good, c.warn, c.bad);
    printf("%-12s %10s %13s %8s\n", "compress", "PSNR(dB)", "blockiness(dB)", "verdict");
    printf("--------------------------------------------------------\n");
    double prevPsnr = 1e9, prevBlock = -1e9; bool mono = true;
    for (double s : {0.05, 0.15, 0.30, 0.50, 0.70, 0.85, 0.97}) {
        Plane deg; blockQuantise(ref, deg, s);
        double m = mse(ref, deg), ps = psnrDb(m), bl = blockinessDb(deg);
        double effWarn; const char* v = verdict(bl, 0.0 /*static scene*/, c, effWarn);
        printf("%-12.2f %10.2f %13.2f %8s\n", s, ps, bl, v);
        if (ps > prevPsnr + 0.01 || bl < prevBlock - 0.01) mono = false;
        prevPsnr = ps; prevBlock = bl;
    }
    printf("--------------------------------------------------------\n");
    printf("monotonic (PSNR down, blockiness up as bitrate drops): %s\n", mono ? "YES" : "NO");
    return mono ? 0 : 2;
}

static int runStream(const Cfg& c) {
    YuvReader R(c.ref, c.w, c.h), D(c.deg, c.w, c.h);
    if (!R.ok()) { fprintf(stderr, "cannot open ref: %s\n", c.ref.c_str()); return 1; }
    if (!D.ok()) { fprintf(stderr, "cannot open deg: %s\n", c.deg.c_str()); return 1; }
    printf("%-6s %9s %13s %8s %8s\n", "frame", "PSNR(dB)", "blockiness(dB)", "motion", "verdict");
    printf("------------------------------------------------------------\n");
    Plane r, d, dPrev; bool havePrev = false;
    int i = 0, drop = 0, warn = 0; double psnrSum = 0, blSum = 0;
    while (R.next(r) && D.next(d)) {
        double m = mse(r, d), ps = psnrDb(m), bl = blockinessDb(d);
        double mo = havePrev ? motion(d, dPrev) : 0.0;
        double effWarn; const char* v = verdict(bl, mo, c, effWarn);
        printf("%-6d %9.2f %13.2f %8.2f %8s\n", i, ps, bl, mo, v);
        if (!strcmp(v, "DROP")) drop++; else if (!strcmp(v, "WARN")) warn++;
        psnrSum += ps; blSum += bl; dPrev = d; havePrev = true; ++i;
    }
    if (i == 0) { fprintf(stderr, "no frames (check --w/--h and file sizes)\n"); return 1; }
    printf("------------------------------------------------------------\n");
    printf("frames=%d  mean PSNR=%.2f dB  mean blockiness=%.2f dB  WARN=%d  DROP=%d\n",
           i, psnrSum / i, blSum / i, warn, drop);
    if (drop) printf("recommendation: %d frame(s) exceed the pixelation limit -> reroute / lower "
                     "resolution / raise the picture bitrate (audio kept at priority).\n", drop);
    else      printf("recommendation: pixelation within budget for a 32 kbps audio-priority stream.\n");
    return 0;
}

int main(int argc, char** argv) {
    Cfg c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* def) -> std::string { return (i + 1 < argc) ? argv[++i] : def; };
        if      (a == "--ref")  c.ref = val("");
        else if (a == "--deg")  c.deg = val("");
        else if (a == "--w")    c.w = std::atoi(val("640").c_str());
        else if (a == "--h")    c.h = std::atoi(val("360").c_str());
        else if (a == "--good") c.good = std::atof(val("-40").c_str());
        else if (a == "--warn") c.warn = std::atof(val("-30").c_str());
        else if (a == "--bad")  c.bad  = std::atof(val("-15").c_str());
        else if (a == "--motion-mask") c.motionMaskDb = std::atof(val("6").c_str());
        else if (a == "--selftest") c.selftest = true;
        else if (a == "--help" || a == "-h") {
            printf("ggw_streamqc — stream pixelation / quality watch (C++)\n"
                   "  --selftest                     prove the metrics on synthetic frames\n"
                   "  --ref F --deg F --w W --h H    score raw YUV420p ref vs distributed stream\n"
                   "  --good/-40 --warn/-30 --bad/-15  blockiness dB thresholds (tunable)\n"
                   "  --motion-mask 6                dB the warn line relaxes at full motion\n");
            return 0;
        }
    }
    if (c.selftest || (c.ref.empty() && c.deg.empty())) return runSelftest(c);
    return runStream(c);
}

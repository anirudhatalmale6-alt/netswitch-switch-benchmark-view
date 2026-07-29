// ggw_ddtm — 6GGW / NetSwitch DDTM measure
//
// Reads a 64-part (8x8) block from an SCCM measure and works out the best way to
// scale it, the way you described it:
//
//   * 64 parts, some POWERED (they carry energy — the known, determining parts)
//     and some UNPOWERED (near-zero — the unknown / negligible parts). We find
//     which are which after the transform.
//   * the block is UNQUANTED — no scaler applied yet. We REQUANT it at a range of
//     scaler numbers (dequantize/requantize) and see what each costs and keeps.
//   * we pick the BEST scaler by a log/ln criterion — quality in dB (log) against
//     rate in nats (ln) — the knee of the rate/quality curve.
//   * we band the result onto BANDWIDTH: throughput per MHz at that scaling
//     (spectral efficiency), the same bandwidth×MHz idea stream-ctl already uses.
//
// NOTE ON TERMS: SCCM and DDTM are your names. My reading, so you can correct it:
//   SCCM measure -> the 8x8 sample block that feeds this (screen/spectra measure)
//   DDTM measure -> this: the 64-coefficient transform + quant analysis
//   powered/unpowered -> transform coefficients above/below an energy threshold
//   unquant/requant   -> dequantize then requantize at a chosen scaler step
//   "best as log ln"  -> best scaler by dB-quality (log10) per nat-of-rate (ln)
// If any of those maps to something different in your data, tell me and I'll bend
// the tool to it — the maths below is real either way.
//
// Build:
//   Linux:   g++ -std=c++17 -O2 ggw_ddtm.cpp -o ggw_ddtm
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_ddtm.cpp -o ggw_ddtm.exe -static

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

static constexpr int N = 8;              // 8x8 = 64 parts
static constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// 8x8 DCT-II (forward) and DCT-III (inverse) — the standard JPEG/H.26x transform
// ---------------------------------------------------------------------------
static void dct2(const double in[N][N], double out[N][N]) {
    for (int u = 0; u < N; ++u) for (int v = 0; v < N; ++v) {
        double s = 0.0;
        for (int x = 0; x < N; ++x) for (int y = 0; y < N; ++y)
            s += in[x][y]
               * std::cos((2*x+1)*u*PI/(2*N))
               * std::cos((2*y+1)*v*PI/(2*N));
        double cu = (u==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
        double cv = (v==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
        out[u][v] = cu*cv*s;
    }
}
static void idct2(const double in[N][N], double out[N][N]) {
    for (int x = 0; x < N; ++x) for (int y = 0; y < N; ++y) {
        double s = 0.0;
        for (int u = 0; u < N; ++u) for (int v = 0; v < N; ++v) {
            double cu = (u==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
            double cv = (v==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
            s += cu*cv*in[u][v]
               * std::cos((2*x+1)*u*PI/(2*N))
               * std::cos((2*y+1)*v*PI/(2*N));
        }
        out[x][y] = s;
    }
}

// ---------------------------------------------------------------------------
// a representative SCCM block if none given: smooth gradient + a tone, so energy
// concentrates in a few coefficients (few powered, many unpowered — realistic).
// ---------------------------------------------------------------------------
static void default_block(double b[N][N]) {
    for (int x = 0; x < N; ++x) for (int y = 0; y < N; ++y)
        b[x][y] = 128.0 + 40.0*std::sin((x+1)*0.6) + 25.0*std::cos((y+1)*0.4) + 8.0*(x==y?1.0:0.0);
}

// parse up to 64 numbers from a file (whitespace/comma separated), row-major
static bool load_block(const std::string& path, double b[N][N]) {
    std::ifstream f(path); if (!f) return false;
    std::string all((std::istreambuf_iterator<char>(f)), {});
    for (char& c : all) if (c==',' || c==';') c=' ';
    std::stringstream ss(all); std::vector<double> v; double d;
    while (ss >> d) v.push_back(d);
    if (v.size() < 64) return false;
    for (int i = 0; i < 64; ++i) b[i/N][i%N] = v[i];
    return true;
}

struct QResult { double Q, mse, psnr_db, rate_bits, rate_nats, eff; int nonzero; };

// entropy of the quantized coefficient magnitudes — the real rate measure
static void entropy(const std::vector<int>& q, double& bits, double& nats, int& nonzero) {
    std::vector<int> mags; nonzero = 0;
    for (int v : q) { mags.push_back(std::abs(v)); if (v!=0) ++nonzero; }
    // histogram
    int mx = 0; for (int m : mags) mx = std::max(mx, m);
    std::vector<int> hist(mx+1, 0); for (int m : mags) hist[m]++;
    int n = (int)mags.size(); bits = 0.0; nats = 0.0;
    for (int h : hist) if (h>0) { double p = (double)h/n; bits -= p*std::log2(p); nats -= p*std::log(p); }
    // per-block total (bits per 64-coefficient block), + sign bits for nonzero
    bits = bits*n + nonzero; nats = nats*n;
}

int main(int argc, char** argv) {
    std::string infile; double srate_mhz = 8.0; // symbol/block rate scale for banding
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a=="--in" && i+1<argc) infile = argv[++i];
        else if (a=="--help") { std::printf("ggw_ddtm [--in block.txt]  (64 numbers = one 8x8 SCCM block)\n"); return 0; }
    }

    double blk[N][N];
    if (!infile.empty()) { if (!load_block(infile, blk)) { std::fprintf(stderr,"need 64 numbers in %s\n",infile.c_str()); return 2; } }
    else default_block(blk);

    // 1) transform: 64 parts -> 64 coefficients
    double C[N][N]; dct2(blk, C);
    std::vector<double> coef; for (int u=0;u<N;++u) for (int v=0;v<N;++v) coef.push_back(C[u][v]);

    // 2) powered vs unpowered: energy threshold = 1% of the total coefficient energy peak
    double peak = 0.0; for (double c : coef) peak = std::max(peak, std::fabs(c));
    double thr = peak * 0.01;
    int powered = 0; for (double c : coef) if (std::fabs(c) >= thr) ++powered;
    int unpowered = 64 - powered;

    std::printf("DDTM measure — 64 parts (8x8), from SCCM block\n");
    std::printf("  coefficient peak |C| = %.3f   powered threshold (1%%) = %.3f\n", peak, thr);
    std::printf("  POWERED (known, determining) : %d / 64\n", powered);
    std::printf("  UNPOWERED (unknown, near-zero): %d / 64\n\n", unpowered);

    // dynamic range for PSNR
    double bmin=1e18,bmax=-1e18; for(int x=0;x<N;++x)for(int y=0;y<N;++y){bmin=std::min(bmin,blk[x][y]);bmax=std::max(bmax,blk[x][y]);}
    double rng = (bmax-bmin>1e-9)?(bmax-bmin):1.0;

    // 3) requant sweep: for each scaler Q, dequant/requant and score
    std::vector<double> Qs = {1,2,3,4,6,8,12,16,24,32,48,64,96,128};
    std::vector<QResult> table;
    for (double Q : Qs) {
        std::vector<int> q(64);
        double deq[N][N];
        for (int i=0;i<64;++i) q[i] = (int)std::lround(coef[i]/Q);
        for (int i=0;i<64;++i) deq[i/N][i%N] = q[i]*Q;         // unquant->requant at this scaler
        double rec[N][N]; idct2(deq, rec);
        double se=0.0; for(int x=0;x<N;++x)for(int y=0;y<N;++y){double d=rec[x][y]-blk[x][y]; se+=d*d;}
        double mse = se/64.0;
        double psnr = (mse<1e-12)?120.0:10.0*std::log10(rng*rng/mse);
        double bits,nats; int nz; entropy(q,bits,nats,nz);
        double eff = (bits>1e-9)? psnr/(bits/64.0) : 0.0;      // dB (log) per bit/part  == "log ln" quality-per-rate
        table.push_back({Q,mse,psnr,bits,nats,eff,nz});
    }

    // 4) best choice by the log/ln criterion: max dB-quality per bit, above a quality floor
    const double PSNR_FLOOR = 30.0;
    int best = -1; double bestEff = -1;
    for (int i=0;i<(int)table.size();++i) if (table[i].psnr_db>=PSNR_FLOOR && table[i].eff>bestEff){bestEff=table[i].eff;best=i;}
    if (best<0){ best=0; for (int i=0;i<(int)table.size();++i) if (table[i].eff>table[best].eff) best=i; }

    std::printf("  Q(scaler)  nonzero  rate(bits/blk)  quality(PSNR dB)  entropy(nats)  dB-per-bit\n");
    for (int i=0;i<(int)table.size();++i) {
        std::printf("  %8.0f  %6d  %13.1f  %15.2f  %12.3f  %9.2f%s\n",
            table[i].Q, table[i].nonzero, table[i].rate_bits, table[i].psnr_db, table[i].rate_nats, table[i].eff,
            i==best?"  <-- best":"");
    }
    QResult B = table[best];
    std::printf("\n  BEST scaler = %.0f  (PSNR %.2f dB at %.1f bits/block, %.3f nats) — best dB per bit above %.0f dB floor\n",
        B.Q, B.psnr_db, B.rate_bits, B.rate_nats, PSNR_FLOOR);

    // 5) bandwidth banding: throughput per MHz at the chosen scaling (spectral eff)
    // bits/block -> bits/s given block rate; spectral efficiency = bits/s per Hz.
    double bits_per_block = B.rate_bits;
    double blocks_per_s   = srate_mhz * 1e6 / 64.0;            // one 64-part block per 64 samples at srate
    double throughput_bps = bits_per_block * blocks_per_s;
    double spectral_eff   = throughput_bps / (srate_mhz*1e6);  // bits/s/Hz at this scaling
    std::printf("\n  Bandwidth banding at best scaler (spectral efficiency %.3f bits/s/Hz):\n", spectral_eff);
    std::printf("     MHz band     throughput\n");
    for (double mhz : {1.0,2.0,5.0,10.0,20.0,40.0}) {
        double mbps = spectral_eff * mhz*1e6 / 1e6;
        std::printf("     %6.0f MHz   %9.2f Mbps\n", mhz, mbps);
    }
    std::printf("\n  (throughput = spectral_eff x bandwidth; higher scaler Q = fewer bits = fits a narrower MHz band.)\n");
    return 0;
}

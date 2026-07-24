// ggw_ipr — 6GGW / NetSwitch IPR compositor (image-in-picture regions), C++17, zero deps.
//
// IPR (from Sami's 6GGW 2026 roadmap): multiple images composited INSIDE a video frame — a
// picture-in-picture system. Each IPR inset (e.g. 320x240) is drawn inside the main video, at a
// corner or centre ("multicornered"), with:
//   * placement ratioing — a Far..Close depth that scales the inset (far = smaller, close = bigger);
//   * the inside-only rule — "outside placement is not possible, it is obligated": an inset that
//     would fall outside the video canvas is clamped in, or flagged if it cannot fit;
//   * a bezel — the "Gezel Bezel" frame drawn around each inset;
//   * a colour-variance budget — the roadmap's 2.3% "variance valeance" allowance.
//
// It composites to a real RGB frame (writes a .ppm you can view / convert with ffmpeg), and prints
// the layout + ratioing + the bandwidth-tier map (HIGH 3000 / MED 1200 / LOW 32 kbps) across the
// Rural->Suburb->City, Wifi8->5G->6G log curve from page 4.
//
// Build:  g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr.exe -static
// View :  ./ggw_ipr --out ipr.ppm --depth close  &&  ffmpeg -y -i ipr.ppm ipr.png

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

struct RGB { uint8_t r, g, b; };

struct Canvas {
    int w, h; std::vector<RGB> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_*h_, RGB{12,14,20}) {}
    RGB& at(int x, int y) { return px[(size_t)y*w + x]; }
    void fill(int x0, int y0, int rw, int rh, RGB c) {
        for (int y = std::max(0,y0); y < std::min(h, y0+rh); ++y)
            for (int x = std::max(0,x0); x < std::min(w, x0+rw); ++x) at(x,y) = c;
    }
    // vertical content gradient, so a region reads as "video" not a flat block
    void gradient(int x0, int y0, int rw, int rh, RGB a, RGB c) {
        for (int y = std::max(0,y0); y < std::min(h, y0+rh); ++y) {
            double t = rh > 1 ? (double)(y-y0)/(rh-1) : 0.0;
            RGB m{ (uint8_t)(a.r+(c.r-a.r)*t), (uint8_t)(a.g+(c.g-a.g)*t), (uint8_t)(a.b+(c.b-a.b)*t) };
            for (int x = std::max(0,x0); x < std::min(w, x0+rw); ++x) at(x,y) = m;
        }
    }
    void bezel(int x0, int y0, int rw, int rh, int t, RGB c) {   // the "Gezel Bezel" frame
        fill(x0, y0, rw, t, c); fill(x0, y0+rh-t, rw, t, c);
        fill(x0, y0, t, rh, c); fill(x0+rw-t, y0, t, rh, c);
    }
    void writePPM(const std::string& path) {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << w << " " << h << "\n255\n";
        f.write((const char*)px.data(), (std::streamsize)px.size()*3);
    }
};

// ---- Zero-dependency 5x7 bitmap font (uppercase + space) --------------------------------------
// Enough of the alphabet to spell the immaterial-rights notice. 1 = pixel on, 5 wide, 7 tall.
static const char* glyph(char c) {
    switch (std::toupper((unsigned char)c)) {
        case 'A': return "01110""10001""10001""11111""10001""10001""10001";
        case 'B': return "11110""10001""10001""11110""10001""10001""11110";
        case 'C': return "01110""10001""10000""10000""10000""10001""01110";
        case 'D': return "11100""10010""10001""10001""10001""10010""11100";
        case 'E': return "11111""10000""10000""11110""10000""10000""11111";
        case 'F': return "11111""10000""10000""11110""10000""10000""10000";
        case 'G': return "01110""10001""10000""10111""10001""10001""01110";
        case 'H': return "10001""10001""10001""11111""10001""10001""10001";
        case 'I': return "11111""00100""00100""00100""00100""00100""11111";
        case 'L': return "10000""10000""10000""10000""10000""10000""11111";
        case 'M': return "10001""11011""10101""10101""10001""10001""10001";
        case 'N': return "10001""11001""10101""10011""10001""10001""10001";
        case 'O': return "01110""10001""10001""10001""10001""10001""01110";
        case 'P': return "11110""10001""10001""11110""10000""10000""10000";
        case 'R': return "11110""10001""10001""11110""10100""10010""10001";
        case 'S': return "01111""10000""10000""01110""00001""00001""11110";
        case 'T': return "11111""00100""00100""00100""00100""00100""00100";
        case 'U': return "10001""10001""10001""10001""10001""10001""01110";
        case 'V': return "10001""10001""10001""10001""10001""01010""00100";
        case 'W': return "10001""10001""10001""10101""10101""11011""10001";
        case 'Y': return "10001""10001""01010""00100""00100""00100""00100";
        default:  return nullptr;                 // space / unknown -> blank advance
    }
}
static int drawText(Canvas& cv, int x, int y, int scale, const std::string& s, RGB fg) {
    int cx = x;
    for (char ch : s) {
        const char* g = glyph(ch);
        if (g) {
            for (int ry = 0; ry < 7; ++ry)
                for (int rx = 0; rx < 5; ++rx)
                    if (g[ry*5 + rx] == '1') cv.fill(cx + rx*scale, y + ry*scale, scale, scale, fg);
        }
        cx += 6 * scale;                          // 5 px glyph + 1 px gap
    }
    return cx - x;                                // pixel width drawn
}

// The immaterial-rights (intellectual property rights) violation notice, tiled/repeated across the
// frame in English — both when music plays with an image and when the stream is watched audio-only.
static void rightsOverlay(Canvas& cv, const std::string& notice) {
    int scale = std::max(1, cv.w / 320);          // readable at any canvas size
    int tw = (int)notice.size() * 6 * scale, th = 7 * scale;
    // a prominent centred banner...
    int bx = (cv.w - tw)/2, by = cv.h/2 - th/2;
    cv.fill(bx - 12, by - 10, tw + 24, th + 20, RGB{140,0,0});
    cv.bezel(bx - 12, by - 10, tw + 24, th + 20, 3, RGB{255,220,0});
    drawText(cv, bx, by, scale, notice, RGB{255,255,255});
    // ...plus the same notice repeated faintly on a grid, so it reads on any region of the frame.
    int sscale = std::max(1, scale/2), sw = (int)notice.size()*6*sscale, sh = 7*sscale;
    for (int ry = 30; ry < cv.h - sh; ry += sh + 46)
        for (int rx = 20; rx < cv.w - sw; rx += sw + 40)
            if (std::abs(ry - by) > th + 30)      // don't clutter the banner
                drawText(cv, rx, ry, sscale, notice, RGB{120,120,130});
}

// An IPR inset: base size, which corner/centre it anchors to, and a Far..Close depth in [0,1].
struct Inset {
    std::string name; int bw, bh; int anchor;   // 0 TL, 1 TR, 2 BL, 3 BR, 4 centre
    double close;                                 // 0 = far (small), 1 = close (big)
    RGB tint;
};
struct Placed { std::string name; int x,y,w,h; double scale; bool inside; };

// Depth ratioing: far = 0.5x, close = 1.0x. The inside-only rule clamps the inset into the canvas.
static Placed place(const Inset& in, int W, int H, int margin, bool& fits) {
    double scale = 0.5 + 0.5 * std::clamp(in.close, 0.0, 1.0);
    int w = (int)std::lround(in.bw * scale), h = (int)std::lround(in.bh * scale);
    int x, y;
    switch (in.anchor) {
        case 0: x = margin;            y = margin;            break; // top-left
        case 1: x = W - margin - w;    y = margin;            break; // top-right
        case 2: x = margin;            y = H - margin - h;    break; // bottom-left
        case 3: x = W - margin - w;    y = H - margin - h;    break; // bottom-right
        default:x = (W - w)/2;         y = (H - h)/2;         break; // centre
    }
    fits = (w + 2*margin <= W) && (h + 2*margin <= H);   // can it fit inside at all?
    x = std::clamp(x, margin, std::max(margin, W - margin - w));   // obligated inside
    y = std::clamp(y, margin, std::max(margin, H - margin - h));
    return { in.name, x, y, w, h, scale, fits };
}

static double colorVariancePct(Canvas& c) {   // rough colour spread, as the roadmap's "variance"
    double sr=0,sg=0,sb=0; size_t n=c.px.size();
    for (auto&p:c.px){sr+=p.r;sg+=p.g;sb+=p.b;}
    double mr=sr/n,mg=sg/n,mb=sb/n,vr=0,vg=0,vb=0;
    for (auto&p:c.px){vr+=(p.r-mr)*(p.r-mr);vg+=(p.g-mg)*(p.g-mg);vb+=(p.b-mb)*(p.b-mb);}
    double sd = std::sqrt((vr+vg+vb)/(3.0*n));
    return 100.0 * sd / 255.0;
}

static void tierMap() {
    printf("\nbandwidth tiers over the Rural->City / Wifi8->6G log curve (roadmap page 4):\n");
    printf("  %-8s %-10s %-8s %8s\n", "env", "radio", "tier", "kbps");
    printf("  ----------------------------------------\n");
    printf("  %-8s %-10s %-8s %8s\n", "Rural",  "Wifi8",  "LOW",    "32");
    printf("  %-8s %-10s %-8s %8s\n", "Suburb", "5G Wifi", "MEDIUM", "1200");
    printf("  %-8s %-10s %-8s %8s\n", "City",   "6G 2028", "HIGH",   "3000");
}

int main(int argc, char** argv) {
    int W = 1280, H = 720, margin = 24, bezel = 4;
    std::string out = "ipr.ppm", depth = "close", mode = "video";
    std::string notice = "IMMATERIAL RIGHTS VIOLATION";
    bool rights = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* d){ return (i+1<argc) ? argv[++i] : d; };
        if      (a == "--w") W = std::atoi(val("1280"));
        else if (a == "--h") H = std::atoi(val("720"));
        else if (a == "--out") out = val("ipr.ppm");
        else if (a == "--depth") depth = val("close");   // far | close
        else if (a == "--margin") margin = std::atoi(val("24"));
        else if (a == "--mode") mode = val("video");     // video | audio
        else if (a == "--rights") rights = true;         // overlay the IPR violation notice
        else if (a == "--notice") notice = val(notice.c_str());
        else if (a == "--help"||a=="-h") {
            printf("ggw_ipr — IPR (image-in-picture + immaterial-rights) compositor\n"
                   "  --w --h --out FILE.ppm --depth far|close --margin PX\n"
                   "  --mode video|audio   audio = audio-only stream frame\n"
                   "  --rights             overlay the immaterial-rights violation notice (repeated)\n"
                   "  --notice \"TEXT\"      notice wording (default: IMMATERIAL RIGHTS VIOLATION)\n"
                   "Composites 320x240 IPR insets inside the video (multicornered, inside-only,\n"
                   "depth ratioing, bezels). Writes a .ppm; convert with: ffmpeg -i FILE.ppm FILE.png\n");
            return 0;
        }
    }
    double closeVal = (depth == "far") ? 0.0 : 1.0;
    Canvas cv(W, H);

    // Audio-only stream: no video/insets — just the audio strip and the repeated rights notice.
    // "If stream is watched on audio it says same in english language and repeated."
    if (mode == "audio") {
        cv.fill(0, 0, W, H, RGB{10,10,14});
        for (int i=0;i<28;++i){ int bh=6+(int)(24*std::fabs(std::sin(i*0.6))); cv.fill(margin+8+i*((W-2*margin-16)/28), H-margin-4-bh, 6, bh, RGB{80,200,160}); }
        cv.fill(margin, H-margin-14, W-2*margin, 14, RGB{40,44,54});
        rightsOverlay(cv, notice);   // always shown for audio-only, per your note
        cv.writePPM(out);
        printf("IPR audio-only frame — canvas %dx%d, immaterial-rights notice overlaid (English, repeated)\n", W, H);
        printf("wrote %s  (convert: ffmpeg -y -i %s %s.png)\n", out.c_str(), out.c_str(), out.c_str());
        return 0;
    }

    // Main VIDEO region — centre, large, a content gradient (this is the "video" the roadmap centres).
    int vw = (int)(W*0.56), vh = vw*9/16, vx = (W-vw)/2, vy = (H-vh)/2;
    cv.gradient(vx, vy, vw, vh, RGB{30,90,160}, RGB{120,40,110});
    cv.bezel(vx, vy, vw, vh, bezel+2, RGB{210,210,220});

    // Four IPR insets at the corners ("multicornered"), 320x240 base, scaled by Far..Close depth.
    std::vector<Inset> insets = {
        {"IPR-1 image", 320, 240, 0, closeVal, {200,120,60}},
        {"IPR-2 image", 320, 240, 1, closeVal, {60,170,120}},
        {"IPR-3 image", 320, 240, 2, closeVal, {170,80,150}},
        {"IPR-4 text",  320, 240, 3, closeVal, {90,120,200}},
    };
    printf("IPR compositor — canvas %dx%d, depth=%s (far=0.5x, close=1.0x), inside-only enforced\n\n",
           W, H, depth.c_str());
    printf("%-13s %-8s %-16s %6s %8s\n", "region", "anchor", "placed x,y wxh", "scale", "inside");
    printf("--------------------------------------------------------------\n");
    printf("%-13s %-8s %6d,%-3d %4dx%-4d %6.2f %8s\n", "VIDEO (main)", "centre", vx, vy, vw, vh, 1.0, "yes");
    const char* anchName[] = {"TL","TR","BL","BR","centre"};
    for (auto& in : insets) {
        bool fits; Placed p = place(in, W, H, margin, fits);
        // draw inset: tinted panel + a lighter header strip + bezel
        cv.fill(p.x, p.y, p.w, p.h, in.tint);
        cv.fill(p.x, p.y, p.w, std::max(6, p.h/8), RGB{(uint8_t)(in.tint.r/2+90),(uint8_t)(in.tint.g/2+90),(uint8_t)(in.tint.b/2+90)});
        cv.bezel(p.x, p.y, p.w, p.h, bezel, RGB{235,235,240});
        printf("%-13s %-8s %6d,%-3d %4dx%-4d %6.2f %8s\n",
               in.name.c_str(), anchName[in.anchor], p.x, p.y, p.w, p.h, p.scale, fits?"yes":"NO-FIT");
    }
    // Audio indicator strip (bottom bar) — the roadmap shows Audio alongside Video/IPR.
    cv.fill(margin, H-margin-14, W-2*margin, 14, RGB{40,44,54});
    for (int i=0;i<28;++i){ int bh=6+(int)(24*std::fabs(std::sin(i*0.6))); cv.fill(margin+8+i*((W-2*margin-16)/28), H-margin-4-bh, 6, bh, RGB{80,200,160}); }

    // Immaterial-rights notice over the music+image frame: "if you listen to music with image it
    // says Immaterial rights violation" — English, repeated across the frame.
    if (rights) rightsOverlay(cv, notice);

    cv.writePPM(out);
    printf("--------------------------------------------------------------\n");
    if (rights) printf("immaterial-rights notice overlaid (English, repeated): \"%s\"\n", notice.c_str());
    printf("frame colour spread: %.1f%% (informational; the roadmap's 2.3%% is a per-region\n"
           "  tolerance to apply when the AI redraws a bezel/facet, not a whole-frame target)\n",
           colorVariancePct(cv));
    printf("wrote %s  (convert: ffmpeg -y -i %s %s.png)\n", out.c_str(), out.c_str(), out.c_str());
    tierMap();
    return 0;
}

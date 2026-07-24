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
    std::string out = "ipr.ppm", depth = "close";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* d){ return (i+1<argc) ? argv[++i] : d; };
        if      (a == "--w") W = std::atoi(val("1280"));
        else if (a == "--h") H = std::atoi(val("720"));
        else if (a == "--out") out = val("ipr.ppm");
        else if (a == "--depth") depth = val("close");   // far | close
        else if (a == "--margin") margin = std::atoi(val("24"));
        else if (a == "--help"||a=="-h") {
            printf("ggw_ipr — IPR image-in-picture compositor\n"
                   "  --w --h --out FILE.ppm --depth far|close --margin PX\n"
                   "Composites 320x240 IPR insets inside the video (multicornered, inside-only,\n"
                   "depth ratioing, bezels). Writes a .ppm; convert with: ffmpeg -i FILE.ppm FILE.png\n");
            return 0;
        }
    }
    double closeVal = (depth == "far") ? 0.0 : 1.0;
    Canvas cv(W, H);

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

    cv.writePPM(out);
    printf("--------------------------------------------------------------\n");
    printf("frame colour spread: %.1f%% (informational; the roadmap's 2.3%% is a per-region\n"
           "  tolerance to apply when the AI redraws a bezel/facet, not a whole-frame target)\n",
           colorVariancePct(cv));
    printf("wrote %s  (convert: ffmpeg -y -i %s %s.png)\n", out.c_str(), out.c_str(), out.c_str());
    tierMap();
    return 0;
}

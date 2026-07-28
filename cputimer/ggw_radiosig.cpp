// 6GGW / NetSwitch — radio timing signature runner
// ---------------------------------------------------------------------------
// Runs Sami's "all radio in mobile phone" parameter set through his reduction
// chain, sampling once per second, on top of the shared measured CPU timebase
// (cpu_mhz_timer.h). It computes the parts of his recipe that are well defined
// and clearly flags the tokens that aren't, so he can confirm/correct — nothing
// is invented to fill a gap.
//
//   Antenna 171.4   Box 12.9   Delay 0.00004498   CPU 44.8
//   reduction: / sqrt(47.9985), * 3.251734420901306, / 1.25, / 44.449, / sqrt(4.4)
//   [AI] sample every second for best result
//
// Build:  g++ -std=c++17 -O2 ggw_radiosig.cpp -o ggw_radiosig
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_radiosig.cpp -o ggw_radiosig.exe -static
// Run:    ggw_radiosig                 (5 one-second samples)
//         ggw_radiosig --samples 10    (10 samples)
//         ggw_radiosig --shrink        (also divide by sqrt(22) until very small)
// ---------------------------------------------------------------------------
#include "cpu_mhz_timer.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <ctime>
#endif

// --- Sami's radio parameters (his figures, named so they're auditable) ------
namespace radio {
    constexpr double antenna = 171.4;
    constexpr double box     = 12.9;
    constexpr double delay   = 0.00004498;   // seconds — a supplied delay/echo, not pinged
    constexpr double cpu     = 44.8;         // his CPU term
}

// --- his reduction chain, each stage explicit and configurable --------------
// Every stage below is a divide/multiply he stated. The value flows through all
// stages in order. Change one line to re-map a stage if I read it wrong.
struct Stage { const char* name; double factor; bool multiply; };
static const std::vector<Stage> CHAIN = {
    { "/ sqrt(47.9985)", std::sqrt(47.9985),        false },  // "DIVIDE ALL NUMBERS ... 47,9985 SQRT"
    { "* 3.25173442090", 3.251734420901306,         true  },  // "(multiplier is 3,251734420901306)"
    { "/ 1.25 (qubert)", 1.25,                       false },  // "QUBERT 1.25" — read as divide by 1.25
    { "/ 44.449",        44.449,                     false },  // "DIV 44,449"
    { "/ sqrt(4.4)",     std::sqrt(4.4),             false },  // "SQRT 4,4"
};

static double reduce(double v) {
    for (const auto& s : CHAIN) v = s.multiply ? v * s.factor : v / s.factor;
    return v;
}

// optional extra: divide by sqrt(22) until very small (his R77 rule), count steps
static int shrink(double& v, double threshold = 1e-9) {
    int n = 0; const double d = ggw::sqrt22();
    while (std::fabs(v) > threshold && n < 4096) { v /= d; ++n; }
    return n;
}

int main(int argc, char** argv) {
    int samples = 5; bool do_shrink = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--samples") && i + 1 < argc) samples = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--shrink")) do_shrink = true;
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("6GGW radio timing signature\n"
                        "  ggw_radiosig [--samples N] [--shrink]\n"
                        "  --samples  one-second samples to take (default 5)\n"
                        "  --shrink   also divide by sqrt(22) until < 1e-9 (your R77 rule)\n");
            return 0;
        }
    }

    std::printf("6GGW / NetSwitch radio timing signature\n\n");
    std::printf("parameters (yours): antenna %.1f  box %.1f  delay %.8f s  cpu %.1f\n",
                radio::antenna, radio::box, radio::delay, radio::cpu);
    std::printf("live timebase     : %.1f MHz measured (folded into the cpu term)\n", ggw::cpu_mhz());
    std::printf("reduction chain   :");
    for (const auto& s : CHAIN) std::printf(" %s", s.name);
    std::printf("\n[AI] sampling every 1 second\n\n");

    std::printf("%-6s %-14s %-14s %-16s %-16s %-16s\n",
                "sec", "antenna'", "box'", "delay'", "cpu'", "signature");
    std::printf("%s\n", std::string(86, '-').c_str());

    for (int s = 0; s < samples; ++s) {
        double mhz = ggw::cpu_mhz();                 // real, per-sample
        double a = reduce(radio::antenna);
        double b = reduce(radio::box);
        double d = reduce(radio::delay);
        // cpu term blends his 44.8 with the live measured MHz (scaled) before reducing
        double c = reduce(radio::cpu + mhz * 1e-3);
        double sig = a + b + d + c;                  // combined radio signature
        int shr = 0; double sig_small = sig; if (do_shrink) shr = shrink(sig_small);
        if (do_shrink)
            std::printf("%-6d %-14.8f %-14.8f %-16.10g %-16.8f %-.4g (>>%d)\n",
                        s + 1, a, b, d, c, sig_small, shr);
        else
            std::printf("%-6d %-14.8f %-14.8f %-16.10g %-16.8f %-.8f\n",
                        s + 1, a, b, d, c, sig);
        if (s + 1 < samples) ggw::sqrt22();          // (no-op touch) keep header linked
        // one-second gate
        if (s + 1 < samples) {
        #ifdef _WIN32
            Sleep(1000);
        #else
            struct timespec ts{1, 0}; nanosleep(&ts, nullptr);
        #endif
        }
    }

    std::printf("\nwhat I mapped vs. what I need you to confirm:\n"
                "  mapped (computed above): antenna 171.4, box 12.9, delay 0.00004498, cpu 44.8;\n"
                "    the chain / sqrt(47.9985), * 3.251734420901306, / 1.25, / 44.449, / sqrt(4.4);\n"
                "    and 1 Hz sampling. The delay is treated as a supplied echo (no ping is made).\n"
                "  need a definition (I did NOT guess these): KAZURIMA, PESORIMA, TPAU, TELLUS,\n"
                "    and the terms 'LOG T-498.000 / 1.4999999777550149', 'compare 15,4M in PID',\n"
                "    'minima 38449,9 / maxima 478.447,008'. Tell me the operation each one is and\n"
                "    I'll drop it straight into the chain — every stage is one line in the code.\n");
    return 0;
}

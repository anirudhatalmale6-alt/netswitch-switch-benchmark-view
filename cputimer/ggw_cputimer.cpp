// 6GGW / NetSwitch — CPU MHz timer demo / CLI
// ---------------------------------------------------------------------------
// Shows the shared timebase from cpu_mhz_timer.h working on real hardware:
//   * measures the CPU timebase (MHz) by calibrating the cycle counter,
//   * measures loop-step time (ns),
//   * runs Sami's timing signature (base +cpu -loop +echo, then /sqrt(22)
//     until very small), and
//   * echoes the thermal-expansion parameters from his message and computes
//     the CPU-radius expansion he specified.
//
// Build:  g++ -std=c++17 -O2 ggw_cputimer.cpp -o ggw_cputimer
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_cputimer.cpp -o ggw_cputimer.exe -static
// Run:    ggw_cputimer                 (measure + signature, echo = 0)
//         ggw_cputimer --echo 0.0000024   (fold in a passively-measured echo, seconds)
//         ggw_cputimer --base 0.5467567 --repeat 5
// ---------------------------------------------------------------------------
#include "cpu_mhz_timer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Sami's thermal-expansion parameters (message: "Relativity of CPU in
// motherboard..."). Kept as named constants so the numbers are auditable, not
// buried. Only the ones with a clear physical use are computed below; the rest
// are recorded so the model layer can pick them up.
namespace thermal_params {
    constexpr double poisson_ratio     = 0.22889;      // P'(k'), (i 0.24)
    constexpr double thermal_cond      = 32.0;         // W/m-K
    constexpr double heat_capacity     = 0.0089945;    // his units
    constexpr double youngs_modulus    = 0.00031999989;
    constexpr double dielectric_const  = 0.000009999918;
    constexpr double mhz_ref           = 250.0;        // MHz reference in his note
    constexpr double loss_factor       = 55.0;
    constexpr double r_cpu_mm          = 30.0;         // CPU radius, cold
    constexpr double r_cpu_hot_mm      = 30.00998;     // CPU radius, expanded (his figure)
}

static void print_expansion() {
    using namespace thermal_params;
    double dr = r_cpu_hot_mm - r_cpu_mm;               // radial expansion, mm
    double strain = dr / r_cpu_mm;                     // dimensionless
    // sphere surface 4*pi*r^2 (he wrote 3/4 pi r^2 VAR r) — geometry that shifts:
    const double PI = 3.14159265358979323846;
    double a_cold = 4.0 * PI * r_cpu_mm * r_cpu_mm;
    double a_hot  = 4.0 * PI * r_cpu_hot_mm * r_cpu_hot_mm;
    std::printf("thermal-expansion parameters (from your message):\n");
    std::printf("  Poisson's ratio      %.5f\n", poisson_ratio);
    std::printf("  thermal conductivity %.1f W/m-K\n", thermal_cond);
    std::printf("  heat capacity        %.7f\n", heat_capacity);
    std::printf("  Young's modulus      %.11f\n", youngs_modulus);
    std::printf("  dielectric constant  %.9f\n", dielectric_const);
    std::printf("  loss factor          %.1f    MHz ref %.0f\n", loss_factor, mhz_ref);
    std::printf("  CPU radius expansion %.5f mm -> %.5f mm  (dr = %.5f mm, strain %.6f%%)\n",
                r_cpu_mm, r_cpu_hot_mm, dr, strain * 100.0);
    std::printf("  surface 4*pi*r^2     %.5f -> %.5f mm^2  (+%.5f mm^2 of geometry shift)\n\n",
                a_cold, a_hot, a_hot - a_cold);
}

int main(int argc, char** argv) {
    double base = 0.5467567, echo_s = 0.0;
    int repeat = 3;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--echo")   && i + 1 < argc) echo_s = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--base") && i + 1 < argc) base = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("6GGW CPU MHz timer\n"
                        "  ggw_cputimer [--echo SECONDS] [--base V] [--repeat N]\n"
                        "  --echo   passively-measured line/radio echo (seconds); no ping is made\n"
                        "  --base   starting value for the signature (default 0.5467567)\n"
                        "  --repeat how many measurement rounds (default 3)\n");
            return 0;
        }
    }

    std::printf("6GGW / NetSwitch CPU MHz timer\n\n");

    double mhz = ggw::cpu_mhz();
    if (mhz > 0.0)
        std::printf("measured CPU timebase : %.1f MHz  (%.3f GHz), calibrated cycle counter vs steady clock\n",
                    mhz, mhz / 1000.0);
    else
        std::printf("measured CPU timebase : not available in userspace on this arch (no cycle counter)\n");
    double lns = ggw::loop_time_ns();
    std::printf("measured loop-step    : %.4f ns/iteration\n", lns);
    if (echo_s > 0.0)
        std::printf("network echo (folded) : %.16g s  (supplied — measured passively, no IP activity)\n", echo_s);
    else
        std::printf("network echo (folded) : 0  (pass --echo SECONDS from the link layer to fold it in)\n");
    std::printf("\n");

    std::printf("timing signature  (base +cpu -loop +echo, then / sqrt(22)=%.8f until < 1e-9):\n",
                ggw::sqrt22());
    std::printf("  %-6s %-14s %-14s %-16s %-14s %-4s\n",
                "round", "combined", "cpu term", "loop term", "reduced", "divs");
    for (int r = 0; r < repeat; ++r) {
        ggw::Signature s = ggw::timing_signature(base, echo_s);
        std::printf("  %-6d %-14.10f %-14.10g %-16.10g %-14.4g %-4d\n",
                    r + 1, s.combined, s.cpu_term, s.loop_term, s.reduced, s.divisions);
    }
    std::printf("\n");

    print_expansion();

    std::printf("The header cpu_mhz_timer.h is the drop-in: #include it in any module or AI\n"
                "calculation to share this one measured timebase. Echo stays an input so the\n"
                "timer never opens a socket (your fixed-line / radio echo rule).\n");
    return 0;
}

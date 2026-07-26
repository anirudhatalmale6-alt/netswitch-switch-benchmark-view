// ggw_dramm — 6GGW / NetSwitch tight compute kernel + honest micro-benchmark (C++17).
//
// This is the "DRAM" hot path Sami keeps describing: a very small, allocation-free inner loop
// that grinds many tiny normalised-math values (natural log, square-root reductions, reciprocals,
// small powers) with as little overhead per line as possible. Nothing here touches the heap, the
// OS, or a file in the hot loop — every result stays in registers, which is exactly the point:
// "every line here is a waste of CPU time", so there are as few as possible.
//
// It ships two kernels doing the SAME maths so the comparison is real and defensible:
//   * hot_kernel   — the tight version (registers only, fixed scratch, no per-iter allocation)
//   * naive_kernel — the wasteful version (per-iteration heap allocation + std::pow), i.e. how the
//                    same computation looks before it's tuned.
// The speedup between them is an honest, same-machine measurement — no invented numbers.
//
// The identical kernel exists as a CUDA-C twin in ggw_dramm.cu, so you can run the C++17 CPU path
// here and the CUDA path on an NVIDIA box and compare like-for-like.
//
// Build : g++ -std=c++17 -O2 ggw_dramm.cpp -o ggw_dramm
// Run   : ./ggw_dramm --iters 50000000
//         ./ggw_dramm --iters 50000000 --json

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

// ---- the maths, defined once so both kernels (and the .cu) stay identical -------------------
// One step folds a value through log / sqrt / reciprocal / a small power. TERMS square-root
// reductions per step are the "square roots all the way to 40 pieces" idea, kept honest and finite.
static constexpr int TERMS = 40;

// Approximate FLOPs per outer step, counted straight from the source below (for a labelled,
// not invented, throughput figure). log≈1, each sqrt≈1, the adds/mults/div counted individually.
static constexpr double FLOPS_PER_STEP = 1.0 /*log*/ + 2.0 /*+1, *0.5 seed*/
                                       + (double)TERMS * 2.0 /*sqrt + accumulate*/
                                       + 3.0 /*reciprocal + normalise*/;

// One tight step. Pure register math; the compiler keeps `x` in an FP register across the call.
static inline double step(double x, int i) noexcept {
    double v = std::log(x + 1.0) + 1.0;          // natural log, kept > 0
    double acc = 0.0;
    // "square roots to 40 pieces": a finite reduction of successive roots, normalised small.
    for (int k = 1; k <= TERMS; ++k)
        acc += std::sqrt(v / (double)k);
    double norm = acc / (double)TERMS;           // minimise: bring it back to O(1)
    return 1.0 / (norm + (double)((i & 7) + 1)); // reciprocal + a tiny data-dependent shift
}

// ---- tight kernel: no heap, no allocation, everything in registers --------------------------
static double hot_kernel(std::uint64_t iters) noexcept {
    double x = 0.5;
    for (std::uint64_t i = 0; i < iters; ++i)
        x = step(x, (int)i);
    return x;   // returned so the optimiser can't delete the loop
}

// ---- naive kernel: identical maths, but written the wasteful way ----------------------------
// A fresh heap buffer every iteration and std::pow instead of the folded reciprocal — this is the
// "before" the tight kernel is the "after". Same result to floating-point tolerance.
static double naive_kernel(std::uint64_t iters) {
    double x = 0.5;
    for (std::uint64_t i = 0; i < iters; ++i) {
        std::vector<double> scratch(TERMS);            // heap traffic every single step
        double v = std::log(x + 1.0) + 1.0;
        double acc = 0.0;
        for (int k = 1; k <= TERMS; ++k) {
            scratch[k - 1] = std::pow(v / (double)(k), 0.5);  // pow(.,0.5) instead of sqrt
            acc += scratch[k - 1];
        }
        double norm = acc / (double)TERMS;
        x = std::pow(norm + (double)((i & 7) + 1), -1.0);     // pow(.,-1) instead of 1.0/.
    }
    return x;
}

struct Timing { double secs; double steps_per_sec; double ns_per_step; double gflops; };

template <class F>
static Timing time_kernel(F&& f, std::uint64_t iters, double& sink) {
    auto t0 = std::chrono::steady_clock::now();
    sink += f(iters);
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    Timing t;
    t.secs = secs;
    t.steps_per_sec = (double)iters / secs;
    t.ns_per_step = secs / (double)iters * 1e9;
    t.gflops = (double)iters * FLOPS_PER_STEP / secs / 1e9;
    return t;
}

int main(int argc, char** argv) {
    std::uint64_t iters = 5'000'000;   // ~2-3 s tight on a typical laptop core
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--iters" && i + 1 < argc) iters = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--json") json = true;
        else if (a == "--help" || a == "-h") {
            printf("ggw_dramm — tight C++17 compute kernel + honest micro-benchmark\n"
                   "  --iters N   outer steps (default 5000000)\n"
                   "  --json      machine-readable output\n"
                   "Each step = 1 log + %d sqrt-reductions + reciprocal (~%.0f FLOPs).\n"
                   "The CUDA-C twin of the same kernel is in ggw_dramm.cu.\n",
                   TERMS, FLOPS_PER_STEP);
            return 0;
        }
    }
    if (iters == 0) { fprintf(stderr, "iters must be > 0\n"); return 2; }

    double sink = 0.0;
    Timing hot   = time_kernel(hot_kernel,   iters, sink);
    Timing naive = time_kernel(naive_kernel, iters, sink);
    double speedup = naive.secs / hot.secs;

    if (json) {
        printf("{\"iters\":%llu,\"terms\":%d,\"flops_per_step\":%.0f,"
               "\"hot\":{\"secs\":%.6f,\"steps_per_sec\":%.0f,\"ns_per_step\":%.2f,\"gflops\":%.3f},"
               "\"naive\":{\"secs\":%.6f,\"steps_per_sec\":%.0f,\"ns_per_step\":%.2f,\"gflops\":%.3f},"
               "\"speedup\":%.2f,\"sink\":%.6g}\n",
               (unsigned long long)iters, TERMS, FLOPS_PER_STEP,
               hot.secs, hot.steps_per_sec, hot.ns_per_step, hot.gflops,
               naive.secs, naive.steps_per_sec, naive.ns_per_step, naive.gflops,
               speedup, sink);
        return 0;
    }

    printf("\n6GGW dramm — tight compute kernel benchmark (C++17, single core)\n");
    printf("  outer steps      : %llu\n", (unsigned long long)iters);
    printf("  work per step    : 1 log + %d sqrt-reductions + reciprocal  (~%.0f FLOPs)\n",
           TERMS, FLOPS_PER_STEP);
    printf("  ------------------------------------------------------------\n");
    printf("  %-14s %12s %14s %12s\n", "kernel", "time (s)", "steps/sec", "GFLOP/s");
    printf("  %-14s %12.4f %14.3e %12.3f\n", "tight (hot)",  hot.secs,   hot.steps_per_sec,   hot.gflops);
    printf("  %-14s %12.4f %14.3e %12.3f\n", "naive (heap)", naive.secs, naive.steps_per_sec, naive.gflops);
    printf("  ------------------------------------------------------------\n");
    printf("  tight is %.1fx faster than the naive version (same maths, same machine)\n", speedup);
    printf("  ns per step (tight): %.2f\n\n", hot.ns_per_step);
    printf("Note: single-thread CPU figures. The CUDA-C twin (ggw_dramm.cu) runs the identical\n");
    printf("kernel on an NVIDIA GPU for a like-for-like comparison — see README.md.\n\n");
    return 0;
}

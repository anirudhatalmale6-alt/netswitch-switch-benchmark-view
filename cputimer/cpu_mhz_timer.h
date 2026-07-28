// 6GGW / NetSwitch — CPU MHz timer  (drop-in header, include anywhere)
// ===========================================================================
// A single shared timebase for every module and every AI calculation, exactly
// as Sami asked: "CPU timer for MHz timer, include this in all source code."
//
//   #include "cpu_mhz_timer.h"
//   double mhz  = ggw::cpu_mhz();          // measured CPU timebase, MHz
//   double lns  = ggw::loop_time_ns();     // measured time of one loop step
//   auto   sig  = ggw::timing_signature(); // his combine-and-reduce formula
//
// What is REAL vs supplied:
//   * cpu_mhz()      — measured on THIS core by calibrating the CPU cycle
//                      counter (RDTSC on x86) against a steady wall clock.
//                      On invariant-TSC CPUs this is the stable base rate the
//                      timer runs on. No /proc, no gu; a live measurement.
//   * loop_time_ns() — measured time of one iteration of a fixed unit of work.
//   * echo seconds   — the network echo is an INPUT. Per Sami's rule ("do NOT
//                      make IP activity, listen to the echo from a fixed line
//                      or radio line") this header NEVER opens a socket or
//                      pings. You pass in the echo the link layer already
//                      measured passively; default 0 means "no echo folded in".
//
// The reduction: base (+cpu) (-loop) (+echo), then divide by sqrt(22) until the
// value is very small, counting the divisions — "DIVIDE ALL RESULTS WITH SQUARE
// ROOT 22 UNTIL VERY SMALL NUMBERS". Deterministic; same inputs → same result.
//
// Header-only, no dependencies beyond <chrono>/<cmath>. C++11+. Windows + Linux.
// ===========================================================================
#ifndef GGW_CPU_MHZ_TIMER_H
#define GGW_CPU_MHZ_TIMER_H

#include <chrono>
#include <cmath>
#include <cstdint>

#if defined(_MSC_VER)
  #include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
  #include <x86intrin.h>
#endif

namespace ggw {

// Sami's constant: reduce by sqrt(22) each step.
inline double sqrt22() { return std::sqrt(22.0); }   // ~4.69041575982343

// --- raw cycle counter -----------------------------------------------------
// Returns the CPU cycle counter (RDTSC) where available, else 0 (caller then
// falls back to the wall clock for the rate).
inline uint64_t cpu_cycles() {
#if defined(_MSC_VER)
    return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return 0;   // non-x86: no userspace cycle counter; cpu_mhz() falls back
#endif
}

// --- measured CPU timebase in MHz ------------------------------------------
// Calibrates the cycle counter against steady_clock over `gate_ms`. Result is
// cycles-per-second / 1e6. On a CPU with no userspace counter it returns 0.0
// (honest "not measurable here" rather than a fabricated number).
inline double cpu_mhz(int gate_ms = 120) {
    using clk = std::chrono::steady_clock;
    uint64_t c0 = cpu_cycles();
    if (c0 == 0) {
        // spin once more to confirm the counter really is dead, not just zero
        for (volatile int i = 0; i < 1000; ++i) {}
        if (cpu_cycles() == 0) return 0.0;
    }
    auto t0 = clk::now();
    uint64_t c1 = cpu_cycles();
    // busy-wait the gate so we measure cycles actually advancing under load
    volatile double sink = 0.0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count() < gate_ms)
        sink += 1.0;
    (void)sink;
    uint64_t c2 = cpu_cycles();
    auto t1 = clk::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    if (secs <= 0.0) return 0.0;
    double cyc = double(c2 - c1);
    return (cyc / secs) / 1.0e6;   // MHz
}

// --- measured loop-step time in nanoseconds --------------------------------
// Times `iters` iterations of a small fixed workload and returns ns/iteration.
inline double loop_time_ns(uint64_t iters = 2000000ull) {
    using clk = std::chrono::steady_clock;
    volatile uint64_t acc = 1469598103934665603ull;   // FNV offset, just work
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; ++i) { acc ^= i; acc *= 1099511628211ull; }
    auto t1 = clk::now();
    (void)acc;
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return iters ? ns / double(iters) : 0.0;
}

// --- the timing signature (Sami's formula) ---------------------------------
struct Signature {
    double base;        // starting value (his 0.5467567 default)
    double cpu_term;    // added CPU-derived value
    double loop_term;   // subtracted loop-time value
    double echo_s;      // added network echo (seconds), passed in — never pinged
    double combined;    // base + cpu - loop + echo, before reduction
    double reduced;     // after dividing by sqrt(22) until < threshold
    int    divisions;   // how many sqrt(22) divisions it took
};

// cpu_term / loop_term default to a small normalisation of the live
// measurements (MHz scaled down, loop-ns scaled down) so the formula runs on
// real numbers off this machine; pass explicit values to override.
inline Signature timing_signature(double base = 0.5467567,
                                  double echo_s = 0.0,
                                  double threshold = 1e-9,
                                  double cpu_term = NAN,
                                  double loop_term = NAN) {
    if (std::isnan(cpu_term))  cpu_term  = cpu_mhz()      * 1e-6;   // MHz  -> ~GHz*1e-3 scale
    if (std::isnan(loop_term)) loop_term = loop_time_ns() * 1e-6;   // ns   -> tiny down-term
    Signature s;
    s.base = base; s.cpu_term = cpu_term; s.loop_term = loop_term; s.echo_s = echo_s;
    s.combined = base + cpu_term - loop_term + echo_s;
    double v = s.combined; int n = 0;
    const double d = sqrt22();
    // divide by sqrt(22) until "very small"; guard against non-finite / zero
    while (std::fabs(v) > threshold && n < 4096) { v /= d; ++n; }
    s.reduced = v; s.divisions = n;
    return s;
}

} // namespace ggw
#endif // GGW_CPU_MHZ_TIMER_H

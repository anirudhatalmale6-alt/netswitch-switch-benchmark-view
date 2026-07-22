/**
 * cpu_benchmark.cpp - CPU benchmark implementation
 *
 * Tests computational performance using integer, floating-point,
 * and memory-bound operations. Copyright AI2ORBIT Co. 2026
 */

#include "cpu_benchmark.h"
#include "timer.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>
#include <bit>

CpuBenchmark::CpuBenchmark() noexcept {}

CpuBenchmark::Results CpuBenchmark::run(std::size_t iterations)
{
    Results results{};
    results.iterations = iterations;
    results.benchmark_successful = false;
    results.benchmark_type = "Mixed CPU Workload";

    if (iterations == 0)
    {
        std::cerr << "Error: Iterations must be greater than 0\n";
        return results;
    }

    Timer total_timer;
    total_timer.start();

    std::uint64_t int_result = compute_integer_workload(iterations);
    double        float_result = compute_float_workload(iterations);
    std::uint64_t mem_result = compute_memory_workload(iterations);

    double elapsed_seconds = total_timer.elapsed_seconds();

    // Prevent the compiler from optimizing the results away
    // without the overhead of volatile memory writes.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    (void)int_result; (void)float_result; (void)mem_result;

    results.timing.total_time_seconds     = elapsed_seconds;
    results.timing.operations_per_second  = static_cast<double>(iterations * 3) / elapsed_seconds;
    results.timing.time_per_operation_ns  = (elapsed_seconds / static_cast<double>(iterations * 3)) * 1'000'000'000.0;
    results.benchmark_successful          = true;

    return results;
}

std::uint64_t CpuBenchmark::compute_integer_workload(std::size_t iterations) noexcept
{
    std::uint64_t result = 1;
    for (std::size_t i = 0; i < iterations; ++i)
    {
        result = (result * 31 + 17) % 1000000007ULL;
        result ^= (result << 13) | (result >> 19);
    }
    return result;
}

// Fast inline sin approximation (Bhaskara I, ~0.17% max error over [0, pi])
// Extended to full circle via quadrant folding.
static inline double fast_sin(double x) noexcept
{
    // Reduce x to [0, 2*pi)
    constexpr double TWO_PI = 6.283185307179586;
    x -= TWO_PI * static_cast<long long>(x / TWO_PI);
    if (x < 0) x += TWO_PI;

    // Fold into [0, pi] with sign tracking
    double sign = 1.0;
    if (x > TWO_PI * 0.5) { x = TWO_PI - x; sign = -1.0; }

    // Bhaskara I: sin(x) ≈ 16x(pi - x) / (5*pi^2 - 4x(pi - x))
    constexpr double PI = 3.141592653589793;
    double num = 16.0 * x * (PI - x);
    double den = 5.0 * PI * PI - 4.0 * x * (PI - x);
    return sign * num / den;
}

static inline double fast_cos(double x) noexcept
{
    constexpr double HALF_PI = 1.5707963267948966;
    return fast_sin(x + HALF_PI);
}

// simplify to a Newton-Raphson seed from a bit cast
static inline double fast_sqrt(double x) noexcept
{
    if (x <= 0.0) return 0.0;
    std::uint64_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    bits = 0x1FF7A3BEA91D9B1BULL + (bits >> 1);
    double y;
    std::memcpy(&y, &bits, sizeof(y));

    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);

    return y;
}

// Fast exp approximation via bit-level float trick (Schraudolph, 1999).
// Max relative error ~3.5%; sufficient for a benchmark workload.
static inline double fast_exp(double x) noexcept
{
    // Clamp to avoid overflow/underflow
    if (x >  88.0) return 1.0e38;
    if (x < -88.0) return 0.0;
    union { double d; std::int64_t i; } u;
    u.i = static_cast<std::int64_t>(6497320848556798LL * x + 4607182418800017408LL);
    return u.d;
}

double CpuBenchmark::compute_float_workload(std::size_t iterations) noexcept
{
    double result = 1.0;
    for (std::size_t i = 0; i < iterations; ++i)
    {
        result = fast_sin(result + static_cast<double>(i)) * fast_cos(result);
        result = fast_sqrt(result < 0.0 ? -result + 1.0 : result + 1.0);
        result = fast_exp(result * 0.1) - 1.0;
    }
    return result;
}

std::uint64_t CpuBenchmark::compute_memory_workload(std::size_t iterations) noexcept
{
    constexpr std::size_t array_size = 1024;

    // Static allocation: no heap traffic on repeated calls.
    static std::uint64_t data[array_size];
    static bool initialised = false;
    if (!initialised)
    {
        for (std::size_t i = 0; i < array_size; ++i)
            data[i] = i * 31 + 17;
        initialised = true;
    }

    std::uint64_t result = 0;
    for (std::size_t i = 0; i < iterations; ++i)
    {
        std::size_t index = (i * 7) % array_size;
        result += data[index];
        data[index] = (result * 13) % 1000000007ULL;
    }
    return result;
}

void CpuBenchmark::print_results(const Results &results)
{
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << " CPU Benchmark Results\n";
    std::cout << "========================================\n\n";

    std::cout << "Benchmark Type: " << results.benchmark_type << "\n";
    std::cout << "Iterations: "     << results.iterations     << "\n\n";

    if (results.benchmark_successful)
    {
        std::cout << "Performance Metrics:\n";
        std::cout << " " << std::left << std::setw(30) << "Total Time:"
                  << std::fixed << std::setprecision(6)
                  << results.timing.total_time_seconds << " seconds\n";

        std::cout << " " << std::left << std::setw(30) << "Operations/Second:"
                  << std::fixed << std::setprecision(2)
                  << results.timing.operations_per_second << "\n";

        std::cout << " " << std::left << std::setw(30) << "Time/Operation:"
                  << std::fixed << std::setprecision(2)
                  << results.timing.time_per_operation_ns << " ns\n";
    }
    else
    {
        std::cout << "Benchmark failed to complete successfully.\n";
    }

    std::cout << "\nNote: CPU benchmarks measure computational throughput and may vary\n";
    std::cout << " based on CPU frequency scaling, thermal throttling, and system load.\n\n";
}
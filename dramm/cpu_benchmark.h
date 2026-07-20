#ifndef CPU_BENCHMARK_H
#define CPU_BENCHMARK_H

#include <cstdint>
#include <string>

class CpuBenchmark {
public:
    /**
     * Structure to hold performance timing and throughput metrics
     */
    struct Timing {
        double total_time_seconds;
        double operations_per_second;
        double time_per_operation_ns;
    };

    /**
     * Structure to hold the final results of the CPU benchmark
     */
    struct Results {
        std::size_t iterations;
        std::string benchmark_type;
        bool benchmark_successful;
        Timing timing;
    };

    CpuBenchmark() noexcept;

    /**
     * Runs the mixed CPU workload (Integer, Float, and Memory-bound)
     */
    Results run(std::size_t iterations);

    /**
     * Helper to print formatted CPU results to the console
     */
    static void print_results(const Results& results);

private:
    /**
     * Performs modular arithmetic and bitwise operations
     */
    std::uint64_t compute_integer_workload(std::size_t iterations) noexcept;

    /**
     * Performs transcendental math (sin, cos, exp, sqrt)
     */
    double compute_float_workload(std::size_t iterations) noexcept;

    /**
     * Performs small-scale rapid memory access and modification
     */
    std::uint64_t compute_memory_workload(std::size_t iterations) noexcept;
};

#endif // CPU_BENCHMARK_H
// ggw_dramm.cu — CUDA-C twin of the 6GGW tight compute kernel.
//
// This runs the IDENTICAL maths as ggw_dramm.cpp's hot_kernel (1 log + 40 sqrt-reductions +
// reciprocal per step), one CUDA thread per element, so you get a like-for-like C++17-CPU vs
// CUDA-C comparison. Timed with CUDA events (GPU-side wall clock), device->host transfer excluded
// from the compute figure and reported separately, because for a kernel this tiny the launch +
// transfer overhead is the whole story — which is exactly what we want the number to show.
//
// Needs an NVIDIA GPU + the CUDA toolkit (nvcc). It is NOT built on the delivery box (no GPU there);
// build and run it on your machine:
//   nvcc -O3 -arch=native ggw_dramm.cu -o ggw_dramm_cuda        # CUDA 11.5+ understands -arch=native
//   ./ggw_dramm_cuda --elems 2000000 --steps 40
// If -arch=native isn't supported by your CUDA version, use your card's arch, e.g. -arch=sm_86 (Ampere).

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cuda_runtime.h>

static constexpr int TERMS = 40;   // must match ggw_dramm.cpp
static constexpr double FLOPS_PER_STEP = 1.0 + 2.0 + (double)TERMS * 2.0 + 3.0;  // ~86, matches CPU

#define CUDA_CHECK(call) do { cudaError_t _e = (call); \
    if (_e != cudaSuccess) { fprintf(stderr, "CUDA error %s at %s:%d\n", \
        cudaGetErrorString(_e), __FILE__, __LINE__); std::exit(1); } } while (0)

// One tight step — byte-for-byte the same expression as step() in ggw_dramm.cpp.
__device__ __forceinline__ double step(double x, int i) {
    double v = log(x + 1.0) + 1.0;
    double acc = 0.0;
    for (int k = 1; k <= TERMS; ++k)
        acc += sqrt(v / (double)k);
    double norm = acc / (double)TERMS;
    return 1.0 / (norm + (double)((i & 7) + 1));
}

// Each thread grinds `steps` iterations of the kernel from its own seed and writes the final value.
__global__ void dramm_kernel(double* out, std::uint64_t elems, int steps) {
    std::uint64_t tid = blockIdx.x * (std::uint64_t)blockDim.x + threadIdx.x;
    if (tid >= elems) return;
    double x = 0.5 + (double)(tid & 1023) * 1e-6;   // per-thread seed
    for (int s = 0; s < steps; ++s)
        x = step(x, s);
    out[tid] = x;
}

int main(int argc, char** argv) {
    std::uint64_t elems = 2'000'000;
    int steps = TERMS;   // steps per thread; keep small — this is a latency-bound tiny kernel
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--elems") && i + 1 < argc) elems = strtoull(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--help")) {
            printf("ggw_dramm_cuda — CUDA-C twin of the tight kernel\n"
                   "  --elems N   threads / elements (default 2000000)\n"
                   "  --steps M   kernel steps per thread (default %d)\n", TERMS);
            return 0;
        }
    }

    int dev = 0; cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDevice(&dev));
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev));

    double* d_out = nullptr;
    CUDA_CHECK(cudaMalloc(&d_out, elems * sizeof(double)));

    int block = 256;
    int grid  = (int)((elems + block - 1) / block);

    // warm up (first launch pays context/JIT cost — never time that)
    dramm_kernel<<<grid, block>>>(d_out, elems, steps);
    CUDA_CHECK(cudaDeviceSynchronize());

    cudaEvent_t a, b; CUDA_CHECK(cudaEventCreate(&a)); CUDA_CHECK(cudaEventCreate(&b));
    CUDA_CHECK(cudaEventRecord(a));
    dramm_kernel<<<grid, block>>>(d_out, elems, steps);
    CUDA_CHECK(cudaEventRecord(b));
    CUDA_CHECK(cudaEventSynchronize(b));
    float ms = 0.0f; CUDA_CHECK(cudaEventElapsedTime(&ms, a, b));
    double secs = ms / 1000.0;

    // copy back + anti-DCE reduction on the host
    double* h = (double*)malloc(elems * sizeof(double));
    auto t0 = clock();
    CUDA_CHECK(cudaMemcpy(h, d_out, elems * sizeof(double), cudaMemcpyDeviceToHost));
    double copy_secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    double sink = 0.0; for (std::uint64_t i = 0; i < elems; ++i) sink += h[i];

    double total_steps = (double)elems * (double)steps;
    printf("\n6GGW dramm — CUDA-C twin  (GPU: %s, CC %d.%d)\n", prop.name, prop.major, prop.minor);
    printf("  elements (threads): %llu   steps/thread: %d\n", (unsigned long long)elems, steps);
    printf("  kernel time       : %.4f s   (device events, transfer excluded)\n", secs);
    printf("  d->h copy         : %.4f s\n", copy_secs);
    printf("  throughput        : %.3e steps/s   %.3f GFLOP/s\n",
           total_steps / secs, total_steps * FLOPS_PER_STEP / secs / 1e9);
    printf("  sink (anti-DCE)   : %.6g\n\n", sink);
    printf("Compare against the C++17 CPU path: ./ggw_dramm --iters %llu (single core).\n\n",
           (unsigned long long)elems);

    free(h); cudaFree(d_out);
    cudaEventDestroy(a); cudaEventDestroy(b);
    return 0;
}

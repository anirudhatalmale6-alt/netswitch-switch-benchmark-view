# 6GGW / NetSwitch — dramm: tight compute kernel + honest CUDA-C vs C++17 comparison

The "DRAM" hot path: a very small, allocation-free inner loop that grinds many tiny normalised-math
values (natural log, a 40-piece square-root reduction, reciprocals). The whole design goal is *no
overhead per line* — nothing in the hot loop touches the heap, the OS, or a file. Same kernel, two
runtimes: a C++17 CPU version you can run anywhere, and a CUDA-C twin for an NVIDIA GPU.

## Files
| file | what it is |
|------|-----------|
| `ggw_dramm.cpp` | the tight C++17 kernel + a naive baseline, self-benchmarking |
| `ggw_dramm.cu`  | the **identical** kernel as CUDA-C, for an NVIDIA box |
| `CMakeLists.txt`| builds the CPU tool always; builds the CUDA tool only if `nvcc` is present |

## Build & run

CPU (builds anywhere with a C++17 compiler):
```
g++ -std=c++17 -O2 ggw_dramm.cpp -o ggw_dramm
./ggw_dramm --iters 50000000        # add --json for machine-readable output
```
or via CMake (builds CPU, and CUDA too if a GPU toolkit is found):
```
cmake -S . -B build && cmake --build build
```

CUDA (needs an NVIDIA GPU + toolkit — **not built on the delivery box, which has no GPU**):
```
nvcc -O3 -arch=native ggw_dramm.cu -o ggw_dramm_cuda     # or -arch=sm_86 for Ampere, etc.
./ggw_dramm_cuda --elems 2000000 --steps 40
```

## The comparison, done honestly

Two kernels run the **same maths** so the number means something:
- **tight (hot)** — registers only, fixed scratch, no per-iteration allocation.
- **naive (heap)** — a fresh heap buffer every step + `std::pow` instead of `sqrt`/reciprocal.

Measured here, single core (Intel-class laptop, `-O2`, 2M steps):

```
kernel             time (s)      steps/sec      GFLOP/s
tight (hot)          0.3585      5.58e+06         0.480
naive (heap)         1.1902      1.68e+06         0.145
tight is 3.3x faster than the naive version (same maths, same machine)
```

That 3.3x is real and repeatable — it's the value of the tuning, not a slogan.

## What to expect from CUDA here (and why)

For a kernel this small, the honest expectation is that CUDA **does not automatically win** — and that
matches what you already said about the CUDA API. The kernel is latency/overhead-bound: each launch
pays kernel-launch + (if you transfer) PCIe copy cost, and there's very little arithmetic per element
to hide it behind. A GPU pulls ahead only when you give it enough independent work (millions of
elements, many steps each) so the thousands of cores stay saturated. The `.cu` reports the kernel
time and the device→host copy **separately** so you can see exactly where the time goes rather than
being sold a single flattering figure. Run it on your box and we'll read the real numbers together.

## Honest notes (so nothing bites us later)

- **CMake version:** C++17 needs **CMake ≥ 3.8** (that's when `CMAKE_CXX_STANDARD 17` landed); the
  CUDA half wants **3.18+**. There is no usable "CMake 1.4 / 1.9" for this — those are ~year-2000
  releases that predate C++17 by a decade. I used the real floor (3.8) in `CMakeLists.txt`.
- **C++ standard:** the kernel is plain C++17 and will also compile as C++20/23. It does **not** need
  C++26. Going to an older standard (C++11/14) is possible but buys nothing here.
- **Cadence / Synopsys:** those are chip-design (EDA) simulators — a different category of tool from a
  network gateway. I can benchmark our kernel and give numbers we can defend (like the tight-vs-naive
  figure above), but I won't print "beats Cadence/Synopsys": it isn't an apples-to-apples claim, and a
  buyer who checks would catch it. If you want a comparison against them, tell me the exact axis
  (throughput? cost? a specific simulation task) and I'll build one that holds up.
- **AMD vs Intel:** the numbers above are single-core on whatever CPU runs it; they don't identify a
  vendor. If you want per-CPU figures, run the tool on each machine and we'll tabulate the real
  results — no guessing.

This is a demo/benchmark component, **not production-ready** as-is. It plugs into `server-cpp` as the
compute core (the existing `cpu_benchmark` unit) once we lock the workload you actually want measured.

# ggw_benchrun — one-pass tester-runner (CPU + RAM + thermal → names the bottleneck)

Your ask: run everything once and tell me what the limit is. The separate tools
each measure one axis (`intbench` = CPU int loopset, `drammtune` = DRAMM RAM
search, `thermal` = kernel sensors). This runner measures the two axes that race
for wall-time on a real job — **compute** and **memory** — live on the machine,
reads the real thermal headroom, and reports which one is the limiter.

## How the verdict is made (not a guess)

- **COMPUTE** `C` = timed integer multiply-accumulate loop → ops/s
- **MEMORY** `Mbw` = timed streaming write + read-verify → bytes/s
- A job has arithmetic intensity `I = ops / bytes touched` (ops per byte).
- The machine's balance point is `B = C / Mbw` (ops per byte it can feed):
  - `I > B` → **compute-bound** (the ALU is the wall)
  - `I < B` → **memory-bound** (bandwidth is the wall)
- Equivalent view, also printed: predicted `t_compute = ops/C`, `t_mem = bytes/Mbw`;
  the larger wall-time is the bottleneck. The two views agree by construction.
- **THERMAL** is read from `/sys/class/thermal` (real milli-°C) and reported as
  headroom to the trip point — an advisory throttle flag, since once headroom
  runs out the device throttles both `C` and `Mbw`.
- **GPU** stays an honest hook returning `-1` ("no device") — same pattern as
  `intbench` / `drammtune`. When your GPU C-code lands, `gpu_ops()` returns real
  ops/s and the GPU axis joins the race automatically.

Nothing is faked: both throughputs are measured on the box that runs it, the
memory pass verifies every byte it wrote, and the compute kernel is deterministic
(fixed seed, no `rand`) so a fixed count always reproduces the same checksum.

## Build & verify

```
g++ -std=c++17 -O2 ggw_benchrun.cpp -o ggw_benchrun
./ggw_benchrun selftest          # 12 checks: determinism, memory verify, crossover — all PASS
```

## Run

```
./ggw_benchrun run                          # auto job profile, live measure + verdict
./ggw_benchrun run --ops 2e10 --bytes 1e9   # your own job mix (ops, bytes touched)
./ggw_benchrun run --root ./sample_sys      # read thermal from a test sysfs tree
```

Example on a dev box: `C ≈ 578 Mops/s`, `Mbw ≈ 26.7 GB/s` → balance `B ≈ 0.022
ops/byte`. A compute-heavy job (`I = 74 ops/byte ≫ B`) reports **COMPUTE-bound**;
a bandwidth-heavy job (`I = 0.003 ops/byte ≪ B`) flips to **MEMORY-bound**. The
verdict tracks your job, it is not hardcoded.

## When your GPU C-code lands

`gpu_ops()` is the one function to fill in — same contract as the hook in
`intbench` and `drammtune`. Return measured GPU ops/s and the GPU axis enters the
same crossover math, so the runner can then say "GPU-bound" too when that's the
truth. On a real phone the thermal section also fills in (CPU/GPU/battery zones)
instead of the "no sensor" line you see on a headless build box.

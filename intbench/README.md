# ggw_intbench — GPU / CPU / HPU loopset timer (INT2 / INT4 / INT8 / INT16)

From your HW-testing notes: *"CPU and/or GPU … int4/int2 … three bars on top to time GPU
vs CPU loopset calculation."* This is that tool — the CPU-measured side, with an honest
GPU/HPU hook so the other two bars go live the moment a backend is linked.

## What it does
Runs a quantized multiply-accumulate **loopset** at four integer precisions (INT2, INT4,
INT8, INT16), each packed to its real bit-width, times it, and reports **GOPS**
(giga-ops/sec). It draws **three bars — GPU / CPU / HPU** — so the same loopset is
comparable across every compute unit present.

```
[INT4]  cpu-checksum 5430f53b70e83d4c
  GPU  |                          no device (awaiting backend)
  CPU  | ############             3.75 GOPS
  HPU  |                          no device (awaiting backend)
```

## Honest by construction
- **GPU and HPU report "no device"** until a real backend is linked — nothing is faked.
- **Deterministic**: same `--size`/`--iters` → same checksum on any machine
  (Linux + Windows verified identical: INT4 selftest checksum `eb1b7b1375f67a8f`).
- **Scalar-CPU truth**: on a plain CPU the four precisions run at *similar* GOPS, because a
  scalar loop does an int64 MAC regardless of width. The real INT4/INT2 speed-up only
  appears on **SIMD / GPU tensor units** — which is exactly why the GPU comparison matters.
  This tool makes that gap visible instead of hiding it.

## Linking your GPU code (the third bar)
Implement one function and the GPU bar goes live:
```cpp
double gpu_loopset(int bits, int size, int iters, uint64_t* chk);  // return GOPS>=0, fill *chk
```
(CUDA/OpenCL/compute-shader — same signature for HPU via `hpu_loopset`). Return `-1` for no
device. When you send your GPU code I wire it straight into this hook and the three bars time
the identical loopset side by side.

## Run
```
ggw_intbench selftest                       # 5/5 checks
ggw_intbench run --size 5000 --iters 20000
ggw_intbench run --size 700  --iters 100000
ggw_intbench run --size 6    --iters 2000000
```
(Your sizes 6 / 700 / 5000 map straight onto `--size`.)

## Selftest (5 checks)
1. Quantized MAC reference is reproducible
2. All precisions clamp to their signed bit-width range
3. 4× iters takes materially longer (the timer is real, not a constant)
4. Deterministic checksum across two identical runs
5. GPU/HPU hooks honestly report absence

## Build
```
g++ -std=c++17 -O2 ggw_intbench.cpp -o ggw_intbench
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_intbench.cpp -o ggw_intbench.exe -static
```

## Still to lock (from your notes, once the full set lands)
The Newton / inductive-power (Ampere·Newton·Meter) relation, the Richter (log) ladder
1–9, and the log-T1/T2/T3/T4 temperature screen are part of the thermal/STAC engine — I'll
lock those formulas when you send the complete values (I won't guess half-defined ones).

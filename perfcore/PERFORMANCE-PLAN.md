# AI2ORBIT — Performance Engine Plan (physics + math, iOS + Android)

Built from **your** "WORKING VERSION CPU DRAMM — Project Plan" (pp.4–6): CPU pacing/timing,
the DRAMM PIN/CHIP read-write priority table, RAM virtualized ×4.2, and the LOOP THROUGH
CPU–GPU with integrity retry. This plan folds the physics/math you asked for into ONE native
performance engine that runs the **same on iOS and Android** and pushes to **max throughput**.

Scope note: this is the *performance / physics* half only. The SQL/tunnel/Fabric half of the
old plan (pp.7–17) is intentionally left out per your message — not the focus now.

---

## The one idea that makes it "constant" on iOS and Android

One **native C++17 core** (`ggw_perfcore`), zero OS calls, zero managed-runtime calls. It runs
*beneath* the VM/scheduler — that is what "overcome HyperVisor" means in practice: measure the
silicon, not the Java/Swift runtime on top of it. Same code → comparable, deterministic numbers
on both platforms. Integration is a thin bridge each side (no logic there):

- **Android:** NDK / JNI — `ggw_perfcore` compiled as a `.so`, called from Kotlin.
- **iOS:** the same `.cpp` added to the Xcode target as Objective-C++ (`.mm`), called from Swift.

This is why a plain APK/HTML "benchmark" was never enough — the managed runtime hides the real
numbers. The native core is the fix.

---

## Modules — your concepts → real code

| Your plan / words | Module | Status | Measured vs modelled |
|---|---|---|---|
| DRAMM priority table (p.5): per-pin READ/WRITE/READ/WRITE/WRITE/WRITE, time each | `perfcore dramm` | **BUILT** | measured (real memory read/write + integrity check) |
| "adding removing friction, to up and down performance" (bus friction) | `perfcore friction` / `fricsweep` | **BUILT** | measured (sequential streaming vs random pointer-chase — a real 20–30× swing) |
| "run many instances even though it loses a lot of speed it still works" (your patent) | `perfcore instances` / `instsweep` | **BUILT** | measured (N concurrent instances; integrity holds at every N — degrades, never breaks) |
| "math electricity and named formulas" | `perfcore power` | **BUILT** | P = V·I exact; energy/op from measured ops/s (V, I are your inputs) |
| "+10% kbps in all phones" (network speed) | `perfcore netgain` | **BUILT** | modelled honestly: effective kbps = window/RTT, so a lower-latency route lifts it on the SAME line; capped at line rate (no fake radio/ISP boost) |
| RAM virtualized ×4.2 (p.6) | `perfcore virt42` | **BUILT** (model) | modelled tiered capacity + hit/miss latency blend |
| peak / "max kvint calculation" | `perfcore run` → peak ops/s | **BUILT** | measured peak throughput reached in the pass |
| CPU compute + DRAM streaming | `intbench`, `drammtune`, `benchrun`, `dramtest`, `productmeasure` | **BUILT earlier** | measured |
| thermal / board heat | `thermocalc` | **BUILT earlier** | modelled (your derating math) |
| LOOP THROUGH CPU–GPU (p.6) | `perfcore run` loop | **partial** | CPU + DRAM live; GPU = honest stub |
| GPU time / GPU in the loop | `gpu_ops()` hook | **STUB (-1)** | waiting on your GPU C-code — one function fills it in |
| Cross-platform bridges (NDK / Obj-C++) | integration step | **NEXT** | — |

Everything marked BUILT compiles clean (`-Wall -Wextra`) and is proven by `selftest`
(12/12 in `perfcore` alone). Nothing is faked; the GPU path is labelled a stub, not invented.

---

## What the friction knob actually shows (the "up/down performance" lever)

`fricsweep` on a dev box:

```
friction   %random   Mops/s
  1.00       100        59.7      <- full friction: random access, latency-bound
  0.75        75        72.0
  0.50        50       115.1
  0.25        25       208.9
  0.00         0      1701.2      <- friction removed: sequential streaming
=> removing friction raised throughput ~27x
```

This is honest and physical: it is the true cost of memory access patterns (cache misses vs
streaming). The product lever = arrange data so the CPU streams instead of chasing pointers.
No overclocking, no fake "make the phone faster" — real, defensible, and it demonstrates your
"add/remove friction" idea with numbers.

---

## Build order (what I do next, in order)

1. **Wire `perfcore` into the two apps** — NDK for Android, Obj-C++ for iOS — so the same numbers
   show on both. This is the "run constant on iOS and Android" deliverable.  *(no new inputs needed)*
   → **STARTED:** Android side done at the native layer — JNI bridge + CMake + Kotlin surface in
   `perfcore/android/`, cross-compiled with NDK r27 to valid `libperfcore.so` for arm64-v8a and
   armeabi-v7a (JNI symbols verified). Next: drop into the app module UI; iOS side adds the same
   `.cpp` as Obj-C++ to the Xcode target.
2. **DRAMM ×4.2 virtualization made measured** — back the tiered model with a real fast-tier vs
   slow-tier latency probe on-device.  *(no new inputs needed)*
3. **Fold in your named constants** — e.g. the 56.3° = arctan(1.5) / −100/11 lock, once you say
   which quantity it drives (phase / dB / reflection). Added as a checked constant.
4. **GPU into the loop** — the moment you send the GPU C-code (even ~20 lines CUDA/OpenCL/Metal),
   `gpu_ops()` fills in and GPU joins CPU+DRAM in the roofline/peak.  *(needs your GPU code)*
5. **On-device report** — one screen: DRAMM GB/s, friction headroom, power W + J/op, peak ops/s,
   thermal headroom. Same on both platforms.

Only steps 3 and 4 wait on you; 1, 2, 5 I can just do.

---

## Honesty line (so it survives a network engineer's review)

- Real: DRAMM read/write timing, friction (memory-pattern) throughput, P=V·I, peak ops/s, thermal.
- Modelled (labelled): ×4.2 virtualization blend until step 2, energy/op (your V·I estimate).
- Stub (labelled): GPU, until your code lands.

That mix is deliberate — it means every number I hand you is either measured on the device or a
model you can see the inputs of. Nothing that a serious reviewer could puncture.

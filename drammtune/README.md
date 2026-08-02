# ggw_drammtune — DRAMM CPU/GPU combination-search tuner (+ battery savings)

From your note: *"AI needs to find right turns of the CPU and right rotation stops and starts of
CPU and GPU. Dramm needs to run fast as it can with these combinations"* — *"with battery savings."*

**The "AI" is a search.** It drives the **real DRAMM kernel** (the exact M1 `step(x,i)` from
`dramm/`) across a grid of compute combinations and reports the best ones for two goals at once:
speed and battery. Two knobs model your words:

| your words | knob | meaning |
|---|---|---|
| "right turns of the CPU" | `threads` (T) | how many cores are turned on: 1, 2, 4, 8, … |
| "rotation stops and starts" | `duty` (D) | cores run a burst then stop; 1.0 = always on, 0.25 = on a quarter of the time |
| "CPU **and** GPU" | GPU unit | a GPU candidate, live once `gpu_throughput()` is wired (stub today) |

For each combination it measures DRAMM throughput and computes:
- **delivered** = throughput × duty (work per wall-second at that duty)
- **energy (model)** = `T·D + 0.15·T·(1−D)` core-seconds
- **eff** = delivered ÷ energy — work per unit of battery

Then it names three winners: **FASTEST** (max delivered), **BEST-BATTERY** (max eff), and — if you
pass `--target` — **MEET-TARGET**, the lowest-energy combination that still hits your throughput
target. That last one is the practical battery-saver: the least power that still does the job.

## Run
```
ggw_drammtune selftest
ggw_drammtune search --iters 4000000 --maxthreads 16
ggw_drammtune search --iters 4000000 --target 20    # cheapest combo delivering >= 20 Msteps/s
```

## Honest by construction
- **Throughput is measured** live on the real DRAMM kernel — no invented numbers. The kernel is
  deterministic: it reproduces the canonical DRAMM constant `x = 0.120493199688742` (same value as
  the `approve` result M3), which is checked in selftest — proof this is the same maths as `dramm/`.
- **Energy is a stated MODEL, not a watt reading.** It is labelled "(model)" everywhere. Swap
  `energy_hook()` for a real sensor (Intel RAPL on Linux, the Windows battery API) and the battery
  axis becomes measured — one function, same as the GPU hook.
- **GPU is an honest stub** returning "no device" until a backend is linked. When your GPU code
  lands, `gpu_throughput()` makes the GPU a real candidate in the same search — then the tuner finds
  the best CPU-vs-GPU-vs-mixed combination, exactly as you asked.

## Build
```
g++ -std=c++17 -O2 ggw_drammtune.cpp -o ggw_drammtune -pthread
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_drammtune.cpp -o ggw_drammtune.exe -static
```
Verified: Linux g++ + Windows MinGW static (wine), selftest 5/5 on both.

## Roadmap (your Mon→Fri cadence)
- **Today** — this working version (measured CPU search, modelled battery, GPU hook).
- **Tomorrow** — fixes + a real energy sensor (RAPL) so battery is measured, not modelled.
- **Tuesday** — implementation-ready: the picked combination emitted as an apply-able config
  (thread count + duty schedule) the product can consume.
- **Wed→Fri** — fold in the GPU unit when your GPU code arrives; CPU+GPU mixed combinations.

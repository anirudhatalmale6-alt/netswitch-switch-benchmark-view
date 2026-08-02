# HANDOFF — for the devs picking this up

This is a clean onboarding map so work can be split. Everything is plain C++17
(single file per tool, zero external deps), plus one HTML screensaver and the
SQL data plane. No hidden build steps.

## 1. Build + verify everything with one command

```
sh build-all.sh
```

It compiles every module and runs the self-tests. Expected tail:

```
selftests: 6 passed, 0 failed
built 19, skipped 1, failed 0
```

(The one "skip" is a toolkit-dependent build — Qt / OpenSSL / mingw — not a
failure. Each tool also builds on its own: `g++ -std=c++17 -O2 <tool>.cpp -o <tool>`.)

Every measurement tool answers `selftest` and prints PASS/FAIL per check, so a new
dev can trust a tool before touching it:

```
benchrun/ggw_benchrun   selftest      # 12 checks
dramtest/ggw_dramtest   selftest      # 10 checks (incl. injected stuck-cell)
intbench/ggw_intbench   selftest
drammtune/ggw_drammtune selftest
thermocalc/ggw_thermocalc selftest
siso/ggw_siso           selftest
```

Full tree + one-line descriptions: `SOURCE-MANIFEST.md`.

## 2. The ONE thing to implement: the GPU hook

Every benchmark tool measures CPU/RAM for real and leaves the GPU as a single
honest function that currently returns "no device" (`-1`). This is deliberate —
nothing is faked. Fill in these four spots with real GPU code (CUDA / OpenCL /
Vulkan / Metal — whatever the target uses) and the GPU axis goes live everywhere:

| File | Function | Contract |
|------|----------|----------|
| `benchrun/ggw_benchrun.cpp` | `gpu_ops()` | return measured GPU ops/s (>0) → joins the roofline race |
| `intbench/ggw_intbench.cpp` | GPU path (search `gpu`) | return GOPS per INT2/4/8/16 precision → third bar goes live |
| `drammtune/ggw_drammtune.cpp` | `gpu_throughput()` | return GPU steps/s → GPU becomes a candidate in the tuner search |
| `gpuscreensaver/index.html` | GPU line | swap the WebGL probe for device counters (currently real WebGL, labelled) |

Each returns a single number given a size/iters input. Even ~20 lines of real,
compiling GPU code per target is enough — wire it in, re-run `selftest`, done.

## 3. What is measured vs modelled vs stubbed (so nobody over-claims)

- **Measured live** — CPU throughput, RAM bandwidth + cell integrity/timing, disk
  IOPS, thermal (real `/sys/class/thermal` sensors), the SISO poles/zeros, the
  thermodynamics closed forms (each cross-checked against numeric integration).
- **Model, clearly labelled** — drammtune's battery-energy figure (swaps to a real
  RAPL / battery reading when a power sensor is wired to `energy_hook()`).
- **Stub, returns −1** — every GPU path above, until the GPU hook lands.

## 4. Platform targets (given by Sami)

- **iOS** — iPhone 17 Max, iOS 26.6 (Tahoe/Sonoma line). On-device app: `ios-sysbar/`.
- **Android** — Android 17. On-device app: `android-sysbar/`; the NDK on-device
  bench build points the same tools at the phone's mapped test region.
- The C++ tools are portable as-is (Linux/Android sysfs + Windows WMI in `thermal`);
  the on-device wrappers are the two `*-sysbar` app projects.

## 5. Suggested split

- **Dev A — GPU:** implement the four GPU hooks in §2 for the target silicon.
- **Dev B — on-device:** wire `ios-sysbar` / `android-sysbar` to run `benchrun` +
  `dramtest` + `thermal` on the phone and surface the numbers.
- **Dev C — data plane / networking:** `server-cpp`, `replicate` (phone-to-phone),
  `stream-ctl`/`stream-qc`, `netswitch-sql` (MS SQL Server 2025/26).

Anything unclear in a tool, its `README.md` states the exact formula/protocol and
what's real vs pending. Nothing here needs me to explain it verbally to build it.

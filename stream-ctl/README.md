# 6GGW / NetSwitch — stream delivery control (C++)

Decides **how the gateway fills the pipe and holds the stream up**, on the bitrate tiers you set:

| tier | bitrate |
|------|---------|
| **HIGH** | 3000 kbps |
| **MEDIUM** | 1200 kbps |
| **LOW** | 32 kbps |

Single C++17 file, **zero dependencies**, cross-compiles to a static Windows `.exe`. Four commands:

## `battery` — accu-battery saturation (basic R), network vs no-network

A first-order **RC analogue** of battery charge saturation:

```
dSoC/dt = (S_inf − SoC) / tau  −  I_load        tau = R·C
```

`R` is the "basic R" internal resistance, `C` the pack capacity analogue, `I_load` the drain — a
small idle current with **no network**, a bigger one **on network** (the radio is working). Because
it's linear with constant coefficients it has an exact **Laplace** (first-order) solution:

```
SoC_eq = S_inf − tau·I_load
SoC(t) = SoC_eq + (SoC0 − SoC_eq)·e^(−t/tau)
```

The tool integrates the ODE with **RKF45** (Runge-Kutta-Fehlberg 4/5, adaptive step) and prints the
numeric result **side by side with that Laplace closed form** — they agree to machine precision
(~1e-15), which is the proof the integrator is correct.

```
./ggw_streamctl battery --t 120 --R 2 --C 15 --inet 0.02 --iidle 0.0006 --soc0 0.10
```

## `pipe` — fill the waterpipe on a −3% floor, then pick a tier

Drives the throughput up to the link capacity and **holds it at 97%** — a 3% headroom so the link is
never fully saturated (which would spike latency and loss). That is the **−3% flooring**. The fill
dynamics `dF/dt = gain·(target − F)` are integrated with RKF45.

Capacity comes from a link preset, a raw number, or **bandwidth over MHz** (throughput = spectral
efficiency × bandwidth): `capacity = SE[bit/s/Hz] × BW[MHz]`.

```
./ggw_streamctl pipe --link lightcable        # fibre  100 Mbps -> HIGH
./ggw_streamctl pipe --link adsl              # ADSL     8 Mbps -> HIGH
./ggw_streamctl pipe --link lowend            # 32 kbps         -> LOW
./ggw_streamctl pipe --mhz 0.2 --se 2         # 0.2 MHz × 2 b/s/Hz = 400 kbps -> LOW
./ggw_streamctl pipe --link adsl --battery-soc 0.12   # battery low -> steps HIGH down to MEDIUM
```

Tier rule: the **highest tier that fits under the −3% floor**, stepped **down one** when the battery
is under 20% (save power). This is the hook the pixelation watch (`../stream-qc`) also feeds — a DROP
verdict there is another reason to step the tier down or reroute.

## `split` — MPEG-4 uncontainerized cache split

Cuts a raw (uncontainerized) MPEG-4 elementary stream into cache segments at the split points you
gave — **1 2 7 15 40 59 80 %** — so edges can cache and re-serve the pieces (ties into the
split-for-cache model in the estimator).

```
./ggw_streamctl split --n 1000000                 # print the byte offsets
./ggw_streamctl split --in clip.m4v --out part     # actually cut -> part.seg0.m4v … part.seg7.m4v
```

The segments rejoin **byte-for-byte** into the original (verified).

## Build

```
g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl                                   # Linux/macOS
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl.exe -static    # Windows
cl /std:c++17 /EHsc ggw_streamctl.cpp                                                   # Windows/MSVC
```
Or `./build.sh`. A prebuilt `ggw_streamctl.exe` ships in the delivery zip.

## What I verified here (see `streamctl-verification.txt`)

- **RKF45 == Laplace** on the battery model to ~1e-15 (max |numeric − analytic| = 3.4e-15).
- **Pipe** settles onto exactly 97% of capacity on every link (the −3% floor).
- **Tier selection**: fibre→HIGH, ADSL→HIGH, 32k→LOW; battery 12% on fibre → HIGH steps down to MEDIUM.
- **Split**: a 1 MB file cut at the 7 points → 8 segments that rejoin **byte-identical** to the source.

## Honesty notes

- The battery model is a **first-order RC approximation** of charge saturation (the standard,
  well-understood engineering model), not a full electrochemical simulation. `R`/`C`/drains are
  named, tunable inputs — set them to your device's numbers.
- RKF45 and the Laplace solution are **genuinely implemented**; the self-test is a real numeric
  agreement check, not a hard-coded pass.
- Spectral efficiency (`--se`) is an input you supply for your radio/modulation; the tool does the
  `SE × MHz` arithmetic, it does not guess your link's efficiency.

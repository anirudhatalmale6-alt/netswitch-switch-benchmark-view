# ggw_rfsim — RF / microwave circuit engine

Single file, C++17, zero dependencies. This is the **circuit-level** computational core
that tools like **AWR (Microwave Office)** and **ADS** run on: S-parameters, ABCD two-port
cascade, transmission lines, microstrip synthesis/analysis, matching networks.

```
g++ -std=c++17 -O2 ggw_rfsim.cpp -o ggw_rfsim
./ggw_rfsim selftest      # 38 checks, PASS/FAIL each
./ggw_rfsim report        # showcase
```

Capability On/Off is in `settings.conf` (`./ggw_rfsim settings settings.conf`); the
field-solver roadmap (FEM / 3D FDTD, still TODO) is in `ROADMAP.md`.

## What it computes (all verified in selftest against known values)

| Command | Does | Verified against |
|---------|------|------------------|
| `micro syn <Z0> <er>` | microstrip: target impedance → track width/height ratio | 50Ω/FR4 → w/h≈1.91 |
| `micro ana <w/h> <er>` | microstrip: geometry → effective εr and Z0 | round-trips back to 50Ω ±1.5 |
| `qwt <Zs> <Zl>` | quarter-wave transformer impedance | √(50·100)=70.71, and it truly matches |
| `match <RS> <RL>` | L-network (real→real): Q, series/shunt reactances | Zin verified = 50+0j numerically |
| `line <ZLre> <ZLim> <Z0> <f> <len> <er>` | Zin, |Γ|, VSWR, return-loss of a terminated line | 100Ω on 50Ω → VSWR 2.000 |
| `sweep <ZLre> <ZLim> <Zsys> <Zline> <er> <len> <f0MHz> <f1MHz> <npts>` | VSWR/RL vs frequency (S11 sweep, the core RF deliverable) | quarter-wave 100→50 dips to VSWR 1.000 at design freq, degrades at band edges |
| `filter <butter\|cheby> <n> <fc_MHz> <Z0> [ripple_dB]` | lowpass LC synthesis: prototype g-values → real L/C ladder | Butterworth n=3 g={1,2,1}; Cheby 0.5dB n=3 g={1.596,1.097,1.596} |
| `bpf <butter\|cheby> <n> <f0_MHz> <BW_MHz> <Z0> [ripple_dB]` | bandpass LC synthesis (lowpass prototype → bandpass transform) | gives real pF/nH per element |
| `s2p <file.s2p>` | read Touchstone data (RI/MA/dB, any freq unit): per-freq VSWR/RL/insertion-loss + Rollett stability K, |Δ| | 3dB attenuator → IL 3.0dB, VSWR 1, K>1 stable |
| `fft` | Fourier: FFT / inverse FFT (radix-2). Demo resolves a 2-tone signal | tone→single bin, delta→flat, Parseval, round-trip |
| `settings [file.conf]` | show capability On/Off + implemented/planned (FEM, FDTD, Fourier…) | file toggles; planned-but-enabled flagged |

`filter`/`bpf` cover the AWR/ADS "filter synthesis" workflow; `s2p` reads the universal RF
data format so measured or simulated S-parameters (from a VNA, or exported by ADS/CST/HFSS)
drop straight in. LC element values are exact closed forms; g-values match published tables.
| `fdtd <Z1> <Z2>` | **1D field solver**: reflection off an impedance step | matches |(Z2−Z1)/(Z2+Z1)| to ~0.3% |

## Honest scope — what this is and is NOT

- **AWR / ADS class (circuit-level):** this engine covers it — two-port S-parameters,
  ABCD cascade, transmission-line and microstrip math, matching. That is real and here.
- **CST / HFSS class (3D full-wave):** those solve Maxwell's equations on a 3D mesh
  (FEM / FDTD). That is a *separate, much larger* engine. The `fdtd` command is a
  **minimal 1D representative** of that solver class — a real Yee-grid FDTD with Mur
  absorbing boundaries that reproduces the analytic step reflection. It proves the
  method works; it does **not** replicate a 3D solver. Labelled as such everywhere.
- **FEM / 3D FDTD (TODO):** the full 3D field solvers are planned and tracked in
  `ROADMAP.md` (not built yet — `settings.conf` lists them as planned). The `fourier`
  module is the time-domain→spectrum step a 3D FDTD reuses. The client has existing
  FEM/FDTD code to fold in, which shortcuts the from-scratch effort.

## Method notes

- Microstrip: Hammerstad analysis, Wheeler synthesis closed forms.
- Two-ports: ABCD matrices, cascade = matrix multiply; S-parameters from ABCD referenced
  to Z0. Lossless lines: A=cosβl, B=jZ0 sinβl, C=j sinβl/Z0, D=cosβl.
- L-match: Q=√(Rhigh/Rlow−1); |Xshunt|=Rhigh/Q across the high-R side, |Xseries|=Q·Rlow;
  the lowpass solution (shunt C, series L) is verified numerically to present the source R.
- FDTD: 1D Yee update, eps∝1/Z, mu∝Z so wave impedance = Z and speed is constant; 1st-order
  Mur ABC on both ends; incident peak from a homogeneous run, reflected peak from the
  stepped run, ratio = |Γ|.

Everything measured/derived is a closed form or a simulated field — nothing is stubbed here.
No GPU hook in this tool; it is CPU-exact by design.

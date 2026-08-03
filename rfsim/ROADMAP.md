# rfsim ROADMAP — FEM / FDTD and beyond (TODO)

This file tracks what is built vs planned, so nothing is over-claimed. It is the
authoritative TODO list for the field-solver side of the RF suite. Capability
On/Off state is in `settings.conf` (`./ggw_rfsim settings settings.conf`).

## Status legend
- DONE — implemented and covered by `selftest`.
- TODO — planned, not built yet. Enabling it in `settings.conf` only records intent;
  the tool flags it "enabled but NOT yet built".

## Field solvers

| Capability | Class | Status | Notes |
|-----------|-------|--------|-------|
| 1D FDTD | full-wave (1D) | **DONE** | Yee grid + Mur absorbing boundaries; reproduces the closed-form reflection off an impedance step to ~0.3%. This is the working proof of the FDTD method. |
| 3D FDTD | full-wave (3D) — CST class | **TODO** | Full Yee lattice in 3D, CPML boundaries, plane-wave / port excitation, S-parameter extraction via time-domain → FFT (the `fourier` module already exists for this step). Large build; scope separately. |
| FEM | full-wave (3D) — HFSS class | **TODO** | Frequency-domain finite-element solver: tetrahedral mesh, edge (Nédélec) elements, adaptive refinement, wave ports. Largest build. **Client has existing FEM/FDTD calculations + code to fold in** — that shortcuts a from-scratch effort. Awaiting his files + a list of settings to expose. |

## Why 1D first, then FFT

The 1D FDTD already returns a time-domain field. Fourier (FFT) — now implemented —
turns a single broadband time-domain run into a full frequency response in one shot.
That same pipeline (time-domain solve → FFT → S-parameters vs frequency) is exactly how
the 3D FDTD will produce wideband results, so the pieces are being built in the right order:

1. 1D FDTD (DONE) → validates the time-stepping and boundary handling.
2. Fourier / FFT (DONE) → time-domain to spectrum, verified (tone, delta, Parseval, round-trip).
3. 3D FDTD (TODO) → same method, 3 dimensions, port excitation.
4. FEM (TODO) → frequency-domain alternative for resonant / high-Q structures; fold in client's code.

## Immediate TODO (once the client's list + code arrive)
- [ ] Receive client's FEM/FDTD calculations + code (language TBD) and integrate.
- [ ] Receive the settings list (which On/Off toggles he wants exposed) and extend `settings.conf`.
- [ ] Wire 3D FDTD time-domain output through the existing `fourier` FFT to S-parameters.
- [ ] Thermal automation hook (`thermal` capability) — connect to the existing `thermal/` and
      `thermocalc/` tools for phone/laptop temperature control at scale.

## Testing policy ("test well")
Every implemented capability ships checks in `./ggw_rfsim selftest` (currently 38, all
PASS) and each closed form is verified against a published value or an independent method
(numeric integration, round-trip, or the analytic result). No capability is marked DONE
here until its selftest checks are in place.

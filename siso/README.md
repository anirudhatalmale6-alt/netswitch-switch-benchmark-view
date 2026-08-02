# ggw_siso — SISO transfer-function analyser (the "PID of phone" math)

Analyses the "FROM NOKIA MATERIAL 2022" transfer function:

```
G(s) = K * (s^2 + 3.2 s + 7.2)(s^2 - 8 s + 400)
       -----------------------------------------------
       (s + 7)(s^2 - 1.2 s + 0.8)(s^2 + 33 s + 700)
```

It multiplies the factors into num/den polynomials, finds all poles and zeros
(Durand-Kerner), and reports the two facts that decide PID design:

- **Stability** — every pole in the left half-plane? For this plant **no**:
  `s^2 - 1.2 s + 0.8` → poles at `0.6 ± 0.66 j` (Re > 0) → open loop is
  **UNSTABLE**, so it needs the feedback loop / PID to be usable.
- **Minimum phase** — every zero in the left half-plane? Here **no**:
  `s^2 - 8 s + 400` → zeros at `4 ± 19.6 j` (Re > 0) → **non-minimum-phase**.
  A right-half-plane zero puts a hard ceiling on how fast any controller can
  drive it — the honest, math-backed reason this system "can't calculate fast"
  until the loop is tuned.

Both facts are exact properties of *your* coefficients, not assumptions.

## Build & verify

```
g++ -std=c++17 -O2 ggw_siso.cpp -o ggw_siso
./ggw_siso selftest     # roots vs known factors + trace/product identities — all PASS
./ggw_siso              # analyse the Nokia plant (poles, zeros, DC gain, verdict)
```

Pass your own plant with `--num "1,3.2,7.2;1,-8,400"` and
`--den "1,7;1,-1.2,0.8;1,33,700"` (each factor = coefficients highest-power-first).

## What I need to turn this into a PID

Your target **settling time** and **max overshoot** (and whether the sample
period is the `TIME 1/KPBS` unit in the material). With those I tune a PID
against this exact plant and hand back the gains + a step-response check.

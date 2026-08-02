# ggw_thermocalc — thermodynamic calculator (thermal MODEL side)

The **formula** half of the thermal work. `ggw_thermal` reads real device
sensors; this tool computes the closed-form thermodynamics from last week's
material, and self-checks every result against numerical integration.

Formulas implemented (exactly as written in the material):

- Heat capacity `Cp(T) = a + b*T - c/T^2`
- Entropy `dS = INT Cp/T dT` between two temperatures
- Enthalpy `dH = INT Cp dT`
- Ideal gas isothermal `pV = nRT` → `W = Q = nRT ln(V2/V1)`, `dS = nR ln(V2/V1)`
- Van der Waals `(p + a(N/V)^2)(V - Nb) = N kB T` → isothermal
  `W = N kB T ln((V2-Nb)/(V1-Nb)) + a N^2 (1/V2 - 1/V1)`
- Stirling cycle (ideal, regenerated) `eta = Wnet/Qin` = Carnot bound `1 - Tc/Th`

## Build & verify

```
g++ -std=c++17 -O2 ggw_thermocalc.cpp -o ggw_thermocalc
./ggw_thermocalc selftest          # every closed form vs numeric — all PASS
```

## Use

```
./ggw_thermocalc entropy  --a 20 --b 0.01 --c 1e5 --T1 300 --T2 600
./ggw_thermocalc enthalpy --a 20 --b 0.01 --c 1e5 --T1 300 --T2 600
./ggw_thermocalc ideal    --n 1 --T 300 --V1 1e-3 --V2 2e-3
./ggw_thermocalc vdw      --N 6.022e23 --a 0.1364 --b 3.913e-29 --T 300 --V1 2e-2 --V2 5e-2
./ggw_thermocalc stirling --Th 600 --Tc 300 --n 1 --V1 1e-3 --V2 2e-3
```

## Reconciling with the quoted numbers

The material quotes `dS/n ~ 9.69 J/(K.mol)`, `W = 1.855 MJ` and small residuals
(`0.59%`, `1.69%`). Those come from a specific `a,b,c` and specific leg
temperatures/volumes. Give me those constants and legs and this reproduces the
figures exactly — that's how we confirm the model matches your dataset rather
than guessing a formula.

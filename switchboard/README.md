# ggw_switchboard — marine / industrial main electricity board monitor

Single file, C++17, zero dependencies. Built for the kind of readings on a large ship's
main switchboard, which span an enormous dynamic range — from tens of nanovolts (sensor
floor) up to mega-volt scale, milliamps to kilo-amps, and energy from Wh to TWh.

```
g++ -std=c++17 -O2 ggw_switchboard.cpp -o ggw_switchboard
./ggw_switchboard selftest    # 15 checks
./ggw_switchboard board       # demo on the ship-board values
```

## What it does (all verified in selftest)

| Command | Does | Check |
|---------|------|-------|
| `scale <value> <unit>` | auto-format any reading to the right SI prefix (n u m k M G T) | 990000 W → 990 kW; 40e-9 V → 40 nV; 2.7e12 Wh → 2.7 TWh |
| `power <V> <I> <pf> [3ph]` | single- or three-phase real power | 1100V·900A = 990 kW; 3ph 400V/100A/0.8 = 55.43 kW |
| `energy <Ah> <V>` | stored energy in Wh, auto-scaled | 30000 Ah @ 750 V = 22.5 MWh |
| `check <value> <lo> <hi>` | NORMAL / WARN (near a limit) / ALARM (out of range) | 1090 in [0,1100] → WARN, 1200 → ALARM |
| `board` | dashboard on the listed ship values | bus V/I status, 3-phase power, stored energy, full-range auto-scale |

## Why auto-scaling matters here

The board values given span from **40 nV** to **700 MV** — a dynamic range of ~1.75×10¹⁶,
about 16 orders of magnitude. A monitor that prints raw numbers is unreadable across that
range; this one always shows the human-readable engineering form (e.g. `700 MV`, `15 kA`,
`2.7 TWh`) so an operator reads the board at a glance and alarms are unambiguous.

## Scope / honesty

All formulas are exact closed forms (P = V·I, three-phase P = √3·V_LL·I_L·pf, Wh = Ah·V,
SI decade scaling) and each is checked against a hand value in `selftest`. This is the
measurement/monitoring core — it does not yet talk to real switchboard hardware (Modbus /
NMEA / analog front-end); that acquisition layer is the next step once the interface is known.

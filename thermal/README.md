# 6GGW / NetSwitch — thermal telemetry

The industry-facing thermals piece from the November schedule, as a real, runnable
engine. It reads the device as a **set of parts** — CPU clusters, GPU, battery,
modem/RF power-amp, board zones, each screen's own controller — and reports:

- the **live temperature** of every sensor the platform exposes,
- the **nearest thermal trip point** (passive / hot / critical) and the **headroom** to it,
- and, over time, the device's **heat pattern**: per-part min / mean / max / swing, and
  which parts move the most under load.

That last part is the point Sami raised: as parts heat, they expand, and the parts that
expand most are the ones that shift the board/antenna geometry and add timing jitter on the
antenna↔CPU↔RF path. This tool finds those parts from the device's own sensors — the same
physical picture as a heat-expansion simulation, read live off the hardware instead of a
CAD model.

## Honest scope (what's measured vs. not)

- **Every temperature printed is a real kernel sensor reading** — milli-°C from
  `/sys/class/thermal` and `/sys/class/hwmon`. Nothing is invented or placeholder.
- **What a device exposes is up to its kernel.** Modern phones expose CPU cluster, GPU,
  battery, PMIC and usually modem/RF zones (`mtktscpu`, `gpu`, `battery`, `pa-therm`,
  `modem` …). Per-antenna or per-screen temps show up **only where the vendor wired a
  sensor** — parts with none are listed as `no sensor`, never guessed.
- **Trip points are the device's own throttle thresholds** read from the same sysfs, so
  "headroom" and "THROTTLE" are the hardware's real limits, not a made-up ceiling.
- This build box (a container) exposes **no** sensors — so here it's verified against a
  synthetic sensor tree with known temps (`sample_sys/`) and prints the correct headroom,
  status and heat-pattern ranking. On a real phone/gateway it lists the live zones.

## Build & run

```
g++ -std=c++17 -O2 ggw_thermal.cpp -o ggw_thermal

./ggw_thermal                         # one live snapshot of every sensor
./ggw_thermal --watch 20              # learn the heat pattern over 20 samples
./ggw_thermal --watch 20 --interval 500   # sample every 500 ms
./ggw_thermal --root ./sample_sys     # read a test / captured sensor tree
```

## What it looks like (verified against sample_sys/)

```
PART (sensor)              kind           temp C next trip       headroom  status
----------------------------------------------------------------------------------------
battery                    thermal-zone     34.5 hot 45             +10.5  warm
gpu                        thermal-zone     68.9 passive 90         +21.1  ok
cpu-big-cluster            thermal-zone     72.4 passive 85         +12.6  warm
modem-pa-rf                thermal-zone     81.7 passive 85          +3.3  hot
soc_thermal:package        hwmon            88.3 crit 100           +11.7  warm

hottest part: soc_thermal:package at 88.3 C
```

Heat-pattern learn (same run, driving the big cluster + modem up under load):

```
PART (sensor)                   min     mean      max    swing    drift
------------------------------------------------------------------------
cpu-big-cluster                72.4     80.7     88.2     15.8    +15.8
modem-pa-rf                    81.7     85.8     89.6      7.9     +7.9
...
most thermally active parts (biggest swing = the ones that expand/contract most):
  1. cpu-big-cluster  (15.8 C swing)
  2. modem-pa-rf  (7.9 C swing)
```

## Where it fits

This is the engine behind the **thermal** section of the performance panel and the
November "staying inside the thermal envelope under load" demo. On the stage it becomes a
live gauge + logged history; the data source is this reader.

## Platforms

- **Linux / Android** — `/sys/class/thermal` + `/sys/class/hwmon` (this file).
- **Windows** — the equivalent source is WMI `MSAcpi_ThermalZoneTemperature` (ACPI zones)
  or a vendor sensor driver (LibreHardwareMonitor-style) for per-core/GPU. That's a
  separate backend behind the same table; noted here rather than shipped as a misleading
  `/sys`-reading .exe that would find nothing on Windows.

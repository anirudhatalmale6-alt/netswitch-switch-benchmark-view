# 6GGW / NetSwitch — CPU MHz timer

The shared timebase for every module and every AI calculation, as you asked:
"CPU timer for MHz timer, please include this in all source code."

It's a **drop-in header** (`cpu_mhz_timer.h`) plus a small demo CLI
(`ggw_cputimer`). Include the header anywhere:

```cpp
#include "cpu_mhz_timer.h"
double mhz = ggw::cpu_mhz();            // measured CPU timebase, MHz
double lns = ggw::loop_time_ns();       // measured loop-step time, ns
auto   s   = ggw::timing_signature();   // your combine-and-reduce formula
```

## What it measures (real, on this hardware)

- **CPU timebase (MHz)** — measured by reading the CPU cycle counter (RDTSC on
  x86) and calibrating it against a steady wall clock. No `/proc`, no guessing —
  a live measurement. On this AMD box it reads **2400.0 MHz**, matching the
  advertised 2399.998. On a CPU with no userspace counter it returns 0 and says
  so, rather than inventing a number.
- **loop-step time (ns)** — the measured time of one iteration of a fixed unit
  of work (~1.1–1.2 ns/iter here).

## The timing signature (your formula)

`value = base (+ cpu term) (- loop term) (+ echo)`, then **divide by √22 until
the value is very small**, counting the divisions:

```
base   0.5467567
+cpu   from the measured MHz
-loop  from the measured loop time
+echo  network echo, SECONDS — supplied, never pinged (see below)
-> divide by sqrt(22) = 4.69041576 ... until < 1e-9
```

Example run (echo = 0.0000024 s): combined `0.5491580`, reduced `2.202e-10`
after **14** divisions — deterministic, same inputs give the same result every
time.

## The network echo rule

You were explicit: *"do NOT make IP activity, listen to the echo from a fixed
line or radio line."* So the timer **never opens a socket and never pings**. The
echo is an **input** — you pass in the value the link layer already measured
passively (`--echo SECONDS`, or `ggw::timing_signature(base, echo_s)`), or 0 to
fold nothing in. The header has no network code at all.

## Thermal-expansion parameters

The demo also records the CPU thermal-expansion constants from your message
(Poisson's ratio 0.22889, thermal conductivity 32, heat capacity 0.0089945,
Young's modulus, dielectric constant, loss factor 55, MHz ref 250) as named,
auditable constants, and computes the CPU-radius expansion you gave:
**30.00000 mm → 30.00998 mm** (dr = 0.00998 mm, strain 0.033%), which shifts the
`4·pi·r²` surface by **+7.526 mm²** — the geometry change the heat-pattern model
tracks. These feed the thermal model; nothing here is fabricated beyond your own
figures.

## Build & run

```
# Linux
g++ -std=c++17 -O2 ggw_cputimer.cpp -o ggw_cputimer

# Windows (mingw; MSVC works too — header-only, no libs)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_cputimer.cpp -o ggw_cputimer.exe -static

./ggw_cputimer                      # measure + signature
./ggw_cputimer --echo 0.0000024     # fold in a passively-measured echo (seconds)
./ggw_cputimer --base 0.5467567 --repeat 5
```

Verified on Linux (native) and Windows (`.exe` under a Windows runtime): both
read 2400.0 MHz on this box.

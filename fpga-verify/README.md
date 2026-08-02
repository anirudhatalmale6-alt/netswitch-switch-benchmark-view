# ggw_fpga_verify — FPGA-Engine verification calculator

Built from your two paired screenshots ("these two work together — one is the calc, one is the math
calc"): the vector/geometry side (cross product, triangle area, angles, hypotenuse) and the
power/data side (P=V·I). Your note is headed *"Verification, Testing, and Maintenance"* — so this
is a **checker**: it computes the rigorous answer for each worked example and reports
computed-vs-stated, flagging anything that doesn't reconcile. It never silently adopts a number that
doesn't hold, and it doesn't guess undefined tokens.

## What it found in the current notes (all 5 rigorous checks pass; 3 reconciliations to confirm)

1. **Cross product** `a=i-j+k × b=j-2k = (1,2,1)` — matches your `(.,2,1)`. Good.
2. **Triangle area** for A(0,1,2) B(-2,5,1) C(2,0,-2): the exact cross-product area is
   **10.3078 m²**. Your note's `base·height/2 = 2.2·4.133/2 = 4.55 m²` is ~2× lower — the two
   methods disagree, so one of the base/height figures needs a second look.
3. **Angle**: your note reads `sin(alpha)=0.3434 ⇒ alpha=51.8442°`, but `asin(0.3434)=20.08°`
   and `sin(51.8442°)=0.786`. The 0.3434 and the 51.8442 can't both be the same angle — tell me
   which is the source of truth.
4. **Sides 895, 5000**: `hypot=5079.47`, angles `10.15° / 79.85°` (sum 90). The `51.84/38.16` pair
   in the note is complementary but isn't the angles of these two sides.
5. **Power** `P=V·I`: 200 W / 4 A = 50 V. Fine.

## Run
```
ggw_fpga_verify selftest
ggw_fpga_verify area  0 1 2  -2 5 1  2 0 -2
ggw_fpga_verify cross 4 -1 5  1 -3 -4
ggw_fpga_verify power --volts 230 --amps 4
```

## Build
```
g++ -std=c++17 -O2 ggw_fpga_verify.cpp -o ggw_fpga_verify
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_fpga_verify.cpp -o ggw_fpga_verify.exe -static
```
Verified Linux + Windows (wine), selftest 5/5.

## Open — not guessed
The free label `c` in the cross-product notes, `yfii`, and `Throughput S … include
resistance/capacitance` are undefined in the screenshots. Send me a definition or one worked
input→output for each and I'll add them as real checks — same rule as the two thermals.

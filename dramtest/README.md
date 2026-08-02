# ggw_dramtest — DRAM cell tester (your DRAMM TESTAAJA / DRAM READER WRITER)

Implements your memory-test protocol exactly, from the chat and `filesaiorbit.pdf`:

> "WRITE 45x each byte then rewrite 0 on top of each byte … First 0 then 0 and 00
> — this means this is well written to test the spot. Because of the warming issue
> we never touch this spot again. … LOOP THIS 35 times and run a log of the results
> in seconds … Map each speed and log RADIAN for each speed."

## What it does (and verifies)

For each cell (byte position) in the test region:

1. Write a deterministic non-zero test pattern **W times** (default 45).
2. Overwrite with 0, then 0 again ("0 then 00"), and **read-verify it reads 0**.
3. Time that whole write+verify — the cell's own speed.
4. Mark the cell **warmed** and never write it again (your warming rule).

Then it repeats the sweep up to **LOOPS** times (default 35), bounded by a
wall-time budget (`--secs`; your production cap is "4 Gi digital seconds"), and
for every cell maps its speed to a **radian** angle in `[0, 2π]` (slowest → 0,
fastest → 2π) and logs the distribution — the per-cell speed dial you asked for.

## It has teeth (not a no-op)

The selftest injects a **stuck cell** and confirms the tester raises an integrity
fail — so a real bad cell would be caught, not silently passed. 10 checks total:
pattern determinism, non-zero pattern, clean-run integrity, the warming rule
(a cell is never written twice), fault detection, and the radian mapping bounds.

## Build & verify

```
g++ -std=c++17 -O2 ggw_dramtest.cpp -o ggw_dramtest
./ggw_dramtest selftest          # 10 checks — all PASS
./ggw_dramtest run               # default: 65536 cells, 45 writes, 35 loops, 2 s
./ggw_dramtest run --cells 262144 --writes 45 --loops 35 --secs 3
```

## Honest scope

This tests the process's own RAM buffer: real writes, real read-back verification,
real per-cell timing on this machine. It is not a kernel-level bit-flip / row-hammer
rig (that needs EDAC / mapped physical pages) — the **protocol and the pass/fail +
timing + radian logic are yours**, ready to point at a real mapped region on the
on-device NDK phone build once I have the phone model + Android version.

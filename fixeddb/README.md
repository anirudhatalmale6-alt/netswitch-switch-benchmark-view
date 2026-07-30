# ggw_fixeddb — fixed-size in-CPU database + Black/Grey/White boxes

Your ask: *a fast db in a fixed-size binary, compiled and run in the CPU — the db stays
fixed size and runs fast; when it becomes larger it stays the same binary size.* Plus:
*have the QTY measure visible — three boxes, the txt db (Black / Grey / White).*

## What it does

A normal database grows in memory as rows go in. This one **does not**. The whole store
lives in inline arrays — no heap, no realloc — so its footprint is a compile-time constant
(`sizeof` the struct), whether you insert 10 rows or 1,000,000:

```
10 rows = 24.0 KB    10,000 rows = 24.0 KB    1,000,000 rows = 24.0 KB
```

Rows past capacity overwrite the oldest (a ring), running totals stay in fixed-width
counters, and lookups are O(1) open-addressing hits. The core store is **24 KB — under the
30 KB DRAMM target** — so it sits in CPU cache and stays fast.

## The three boxes

Every row carries a quality reading in dB, classified on the same scale as the optimizer:

```
  White >= -6 dB   (Good)      Grey  -14..-6 dB  (Fair)      Black < -14 dB  (Poor)
```

The QTY measure is the live count in each band, drawn as three text boxes:

```
  +--- BLACK ---+  +--- GREY ----+  +--- WHITE ---+
  |       1112  |  |        529  |  |        407  |
  | ########### |  | #####       |  | ####        |
  +-------------+  +-------------+  +-------------+
  | Poor        |  | Fair        |  | Good        |
  +-------------+  +-------------+  +-------------+
```

A fixed-width FNV checksum over the window gives the "chksum..." validation you sketched —
same rows in, same checksum, on every machine (Linux + Windows: `eec3a6f9f7461565`).

## Run (command line, no browser)

```
ggw_fixeddb.exe selftest
ggw_fixeddb.exe demo --n 1000000        # a million rows, footprint unchanged
ggw_fixeddb.exe demo --n 100 --seed 7
```

## Selftest (5 checks)

1. Footprint constant across 10 / 10k / 1,000,000 inserts, and under 30 KB
2. Ring eviction — the live window caps at capacity, never grows
3. Black + Grey + White always equals the live row count
4. insert → get round-trips (O(1) lookup)
5. Deterministic checksum — two independent fills match

## Build

```
g++ -std=c++17 -O2 ggw_fixeddb.cpp -o ggw_fixeddb
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_fixeddb.cpp -o ggw_fixeddb.exe -static
```

To change the capacity, edit `CAP` (must stay a power of two). At `CAP = 2048` the store is
24 KB; e.g. `CAP = 1024` → 12 KB, `CAP = 4096` → 48 KB. Footprint is always `CAP × 12 B`
plus a few counters — never grows with the number of rows inserted.

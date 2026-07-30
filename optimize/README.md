# ggw_optimize — speed optimizer (auto + manual)

August "Now" backlog item: *"Functions; optimize automatically speed, optimize manually
between upload/download 1300 kbps down and 250 kbps up"* — with item #1's **Priority call
management** built in.

The switch lives in an asymmetric envelope: lots of room **down** (listen), very little
**up** (talk).

    DOWN ceiling = 1300 kbps    UP ceiling = 250 kbps    FLOOR = 32 kbps (audio-only)

## Two ways to drive it

**AUTO** — give it the live available link and it allocates codec bitrate to every active
call to maximise quality within the envelope. Priority calls get their 32 kbps voice floor
reserved first; leftover down-bandwidth is split 2:1 in favour of priority, so a normal
video degrades before a priority one. The envelope is never exceeded.

```
./ggw_optimize auto --down 1300 --up 250 --calls 4 --priority 1
```

**MANUAL** — force a target bitrate on one call. It clamps to `[FLOOR, ceiling]`, reports
the resulting tier / Black-Grey-White band / MOS, and tells you whether the call must be
rerouted to hold quality.

```
./ggw_optimize manual --target 640 --dir down
./ggw_optimize manual --target 90  --dir up      # too low -> REROUTE
```

## Quality mapping (shared with ggw_streamqc and the Week-2 panel)

    High   >= 900 kbps        White  >= -6 dB of ceiling
    Medium >= 180 kbps        Grey   >= -14 dB
    Low    <  180 kbps        Black  <  -14 dB

MOS is a bounded ITU-style logistic on effective bitrate — an honest model, not a typed-in
row. Same input → same number on every machine.

## Selftest (5 checks)

```
./ggw_optimize selftest
```

1. MOS/tier monotonic across the whole 32→1300 sweep
2. Envelope respected (down ≤ 1300, up ≤ 250) for 1..8 calls
3. Priority call keeps more bitrate than a normal call
4. Overload seats priority first, reroutes the excess, never starves priority
5. Deterministic — byte-identical allocation checksum over two runs

Verified on Linux (g++) and Windows (MinGW static, run under wine): identical checksum
`8c3bc1444ed524b9`.

## Build

```
g++ -std=c++17 -O2 ggw_optimize.cpp -o ggw_optimize
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_optimize.cpp -o ggw_optimize.exe -static
```

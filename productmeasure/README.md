# productmeasure — device MEM + AI measure (product management)

One CLI that emits a single product-readiness report from real measurements on
the device it runs on. This is the "mem and AI measures in the product, in the
CLI" line — macOS CLI, on-device, and remote all run the same binary.

## What it measures (all real)

**MEM**
- `bandwidth` — timed streaming write + read-verify over a DRAM-sized working set
  (bytes/s, write+read counted). Verifies every byte.
- `integrity` — fraction of cells that write a non-zero pattern, read it back, then
  zero-overwrite and read that back, both exact (0..1). Same idea as `dramtest`,
  condensed to one pass.

**AI**
- `gemm` — a fixed-size dense fp32 matmul (N×N), timed → FLOP/s. Matmul is the core
  kernel of on-device inference, so this is a real capability floor, not a synthetic
  score. Deterministic: same N → same checksum.
- `infer floor` — tokens/s a dense model of P params can do at ~2·P FLOP/token.
  This is a **CPU single-thread lower bound**, clearly labelled — the real device
  runs wider, threaded, and on GPU, so the live number is higher.
- `gpu_ai()` — honest **−1 stub** ("no device") until the GPU C-code links it. Same
  contract as `benchrun`/`intbench`/`drammtune`.

## Readiness verdict

`FAIL` if any cell is bad (integrity < 1). Otherwise `OK` at ≥2 GB/s & ≥1 GFLOP/s,
`GOOD` at ≥8 GB/s & ≥5 GFLOP/s. Thresholds are pure logic and unit-tested — a
product team can gate a release on the one word.

## Run

```
g++ -std=c++17 -O2 ggw_productmeasure.cpp -o ggw_productmeasure
./ggw_productmeasure selftest                 # 15 checks
./ggw_productmeasure run                       # human report
./ggw_productmeasure json                      # one machine-readable line (CI/dashboard)
./ggw_productmeasure run --params 3.0e9 --gemm 384 --membuf 67108864 --secs 0.3
```

`json` mode is the one to pipe into a product dashboard or a CI gate. Nothing is
faked: measured numbers are measured, the infer floor is a labelled lower bound,
and the GPU axis is −1 until the device hook is wired.

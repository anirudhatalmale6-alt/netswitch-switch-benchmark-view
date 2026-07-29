# 6GGW / NetSwitch — DDTM measure (`ggw_ddtm`)

Takes a 64-part (8×8) block from an SCCM measure and works out the best way to
**scale** it — which parts carry the signal, how hard to quantize, and what
bandwidth that buys — exactly the chain you described.

## Your description → what the tool does

| Your words | In the tool |
|------------|-------------|
| 64 / common, known and unknown parts | the 64 coefficients of an 8×8 DCT of the block |
| determinants of **powered playback** | the **powered** coefficients — energy above threshold (the ones that matter) |
| we discover the **unpowered playback** | the **unpowered** coefficients — near-zero (the negligible / unknown ones) |
| it is **unquanted**, needs unquant + requant with a different scaler | dequantize → requantize the coefficients at a chosen **scaler Q** |
| finds best choice as **log ln** to best scaling | pick the scaler by **dB quality (log10) per nat of rate (ln)** — the knee |
| bandwidth per SCCM, **banding of throughput over MHz** | map the chosen scaling to **throughput per MHz** (spectral efficiency) |

## Run

```
ggw_ddtm                 # uses a representative SCCM block
ggw_ddtm --in block.txt  # your own block: 64 numbers (whitespace/comma), row-major
```

## What it reports (real numbers, verified)

```
POWERED (known, determining) : 6 / 64
UNPOWERED (unknown, near-zero): 58 / 64

Q(scaler)  nonzero  rate(bits/blk)  quality(PSNR dB)  entropy(nats)  dB-per-bit
       1      20          133.0            58.32        78.347      28.06
       8      15           95.3            42.00        55.694      28.19
      24       6           50.2            30.24        30.663      38.52  <-- best
      64       5           39.9            25.60        24.208      41.04

BEST scaler = 24  (PSNR 30.24 dB at 50.2 bits/block, 30.663 nats) — best dB per bit above 30 dB floor

Bandwidth banding at best scaler (spectral efficiency 0.785 bits/s/Hz):
    1 MHz   0.78 Mbps   /   10 MHz   7.85 Mbps   /   40 MHz  31.40 Mbps
```

- **PSNR (dB)** is the log quality measure; **entropy in nats** is the ln rate
  measure — so "best as log ln" is literally the column `dB-per-bit`, maximised
  above a 30 dB quality floor.
- **higher scaler Q → fewer bits → fits a narrower MHz band.** That's the
  bandwidth trade the banding table shows.

## Why you can trust it

- The transform is the standard 8×8 DCT-II / inverse DCT-III (JPEG / H.26x). At
  scaler Q=1 the round-trip is near-lossless (**58 dB**), which is the correctness
  check: the maths reconstructs the block, then quantization is the only loss.
- A flat block reports **1 powered / 63 unpowered** — all energy in the DC term,
  exactly as it should be.
- Identical output on Linux and Windows (`.exe`).

## Notes on the terms

SCCM and DDTM are your names — the table above is my reading of each one. The
maths (DCT, quant/requant, rate–distortion, spectral efficiency) is standard and
correct regardless, but if "powered/unpowered" or "best scaling" means something
specific in your SCCM data (a particular threshold, a fixed set of scalers, a
target dB or a target MHz band), tell me the number and I'll pin the tool to it.
It also plugs straight into `stream-ctl/` (which already does bandwidth × MHz)
and `stream-qc/` (which already scores quality in dB).

## Build

```
# Linux
g++ -std=c++17 -O2 ggw_ddtm.cpp -o ggw_ddtm
# Windows (mingw; MSVC works too)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_ddtm.cpp -o ggw_ddtm.exe -static
```

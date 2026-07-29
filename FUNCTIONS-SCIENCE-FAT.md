# 6GGW / NetSwitch — functions, scientific names, F.A.T. & safety grouping

For each computing function in the product: its **official / scientific name**, the
**approach** (what established method it is), the **Factory Acceptance Test (F.A.T.)**
that proves it works, and a **safety / ethics group**. This is the pack you hand to a
reviewer or an acceptance auditor. Numbering matches `MATH-INDEX.md` (M1–M40).

## A note on "147 versions of AI" and STAC

I want to be straight with you rather than pad this out: the product does **not**
contain 147 AI models, and there isn't a real, citable list of "147 versions of AI"
I can put my name to. What the system *does* contain is a set of **deterministic,
auditable numerical and signal-processing functions** plus **one deterministic
system-approval check** (the `approve` binary — your "the server's AI is the same as
the thin client's"): same binary, same input, same answer everywhere, no black-box
model. That is a genuine strength for F.A.T. and ethics — every output is
reproducible and explainable.

- **STAC** (Securities Technology Analysis Center) and **MLPerf** are benchmark
  *bodies/suites*, not AI versions. If the "147" comes from a catalogue like that,
  send me the source and I'll map our functions to whichever of its tests apply.
- If instead you want the classical-AI *methods* the system leans on named properly,
  the honest list is short and real: DCT/DSP transforms, entropy coding, numerical
  ODE integration, hashing, companding — all below. I'd rather give you 40 real,
  testable functions than 147 invented labels.

## Safety / ethics groups

| Group | Meaning | Care level |
|-------|---------|-----------|
| G1 Deterministic-compute | pure maths, reproducible, no learned model, fully auditable | low risk |
| G2 Signal/media | DSP on media frames; lossy but measured (PSNR/entropy) | low risk |
| G3 Measurement/telemetry | reads local hardware counters; no personal data | low risk |
| G4 Identity/security | handles secrets/keys; single-use, salted, never logged plaintext | **high care** |
| G5 Data plane | stores device/session rows in SQL; network params only | medium care |

Ethics emphasis: no user tracking, no biometric/behavioural profiling, no covert
data collection. Telemetry is machine-local; identity is one-time codes and TLS.

## Functions — name · approach · F.A.T. · group

| # | Function | Official / scientific name | Approach | F.A.T. (how proven) | Group |
|---|----------|---------------------------|----------|---------------------|-------|
| M1–M3 | DRAMM kernel / workload | iterated fixed-point map (closed form) | precomputed Σ1/√k, deterministic iterate | result `0.120493199688742`, hash `1b8ab116…` identical on Linux+Windows | G1 |
| M4 | result_hash | MurmurHash3 finalizer (fmix64) | avalanche bit-mix of IEEE-754 bits | same bits ⇒ same hash, verified cross-platform | G1 |
| M5 | approval_ratio | symmetric mean ratio √((a/b+b/a)/2) | quasi-arithmetic mean, =1 iff equal | 1.000 on match, >1 on skew; 3× skew rejected | G1 |
| M6/M7 | DCT-II / IDCT-III | Discrete Cosine Transform (Ahmed 1974) | JPEG/H.26x separable 8×8 transform | Q=1 round-trip ≥ 58 dB PSNR (near-lossless) | G2 |
| M8 | powered/unpowered split | coefficient energy thresholding | |C| ≥ 1% peak ⇒ significant | flat block → 1 powered / 63 unpowered | G2 |
| M9 | entropy | Shannon entropy (1948), bits & nats | −Σ p·log p over quantised magnitudes | matches hand calc on known histogram | G2 |
| M10 | PSNR / requant | Peak Signal-to-Noise Ratio | 10·log10(range²/MSE) | monotone with Q; 58 dB at Q=1 | G2 |
| M11 | best-scaler | rate–distortion knee | argmax dB per bit above floor | picks the elbow of the R/D curve | G2 |
| M12/M26 | spectral efficiency | Shannon–Hartley banding | bits/s per Hz × bandwidth | throughput scales linearly with MHz | G2 |
| M13/M14 | ddtm_constant(_calc) | log-domain closed form | LOG10(SQRT(…)/120W) | reproduces your −0.090908889 once T1/T2/LLR given | G1 |
| M15 | hyper_downscale | linear scaling | c/f | exact ratio, unit-tested in SQL | G1 |
| M16 | thrice_sinwave | 3-term Fourier sum | |k|·Σ aᵢ·sin(2π fᵢ t) | amplitude/phase checked at t=0,¼T | G1 |
| M17/M18 | lnlog / binary_lnlog | iterated logarithm ln(ln x) | double-log domain | lnlog(e^e)=1 exactly | G1 |
| M20/M21 | µ-law / RTP | G.711 companding + RFC 3550 | 8-band logarithmic PCM, RTP packing | 25/25 packets relayed intact both OS | G2 |
| M22/M23 | PSNR / blockiness | Wang–Bovik blockiness index | no-reference 8×8 grid pixelation (dB) | rises with real block artefacts | G2 |
| M24/M25 | battery ODE | Runge–Kutta–Fehlberg 4(5) + Laplace | numerical + analytic ODE, cross-checked | RKF45 vs analytic agree ~1e-15 | G1 |
| M27 | split/rejoin | byte-exact partition | cut at fixed %s, checksum rejoin | SHA of rejoin == original | G1 |
| M29/M30 | ops/s, FPS, MFLOPS | LINPACK/DGEMM + FMA throughput | FLOP-counted wall-clock timing | checksummed so work isn't elided | G3 |
| M31/M32 | RDTSC MHz / radio reduce | calibrated time-stamp counter | cycle count ÷ calibrated interval | MHz stable across runs | G3 |
| M33 | IOPS/MB-s/latency | fsync-honest disk benchmark | seq/random with real fsync | percentiles reproduce on same disk | G3 |
| M34 | thermal headroom | trip-margin monitor | per-part temp vs trip point | flags throttle swing | G3 |
| M35/M36 | rerouting / distance | least-cost path + RTT geodistance | cost=RTT(1+load), km from handshake | least-cost POP chosen; km ∝ RTT | G3 |
| M37 | SHA-256 | FIPS 180-4 | standard secure hash | empty-string digest = `e3b0c442…b855` | G4 |
| M38 | backup-code gen | Crockford base32, CSPRNG | 50-bit entropy, unbiased byte%32 | 500 unique codes, no I/L/O/U | G4 |
| M39 | prefix_to_mask | CIDR → dotted mask | bit arithmetic | /24 → 255.255.255.0 | G5 |
| M40 | TLS handshake | TLS 1.3 (EC-256/RSA) | standard key exchange + cert verify | negotiates + verifies cert | G4 |

## F.A.T. procedure (how to run acceptance)

1. Build every module on the target OS (`build-all.sh` / MinGW).
2. Run `ggw_report` — capture MFLOPS + compatibility verdict (attach the HTML/PDF).
3. Run each module's self-test / known-answer check in the table above.
4. Confirm the cross-platform hashes match (approve `1b8ab116…`, SHA-256 empty digest).
5. File the report + this table as the acceptance record.

Every function here is deterministic or measured — there is no hidden model whose
output you can't reproduce. That is deliberate, and it is what makes the system pass
a real Factory Acceptance Test.

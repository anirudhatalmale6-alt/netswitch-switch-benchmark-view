# 6GGW / NetSwitch — numbered math index

Every math function in the suite, numbered `M1…Mn`, with its file, signature, and
what it computes. This index is the authoritative numbering; the newest source
files also carry matching `// [Mxx]` tags at the function (see `ddtm`, `approve`,
`netswitch-sql/scaling`). Ask and I'll stamp the tags into any other file too.

## Compute kernel — `dramm/`, `loopbench/`, `approve/`

| # | File · function | Computes |
|---|-----------------|----------|
| M1 | `dramm/cpu_benchmark.cpp · step(x,i)` | one DRAMM step: `v=ln(x+1)+1; acc=Σ_{k=1..40} sqrt(v/k); return 1/(acc/40 + ((i&7)+1))` |
| M2 | `loopbench/ggw_loopbench.cpp · step_opt / dramm_sum_const` | closed form: `Σ sqrt(v/k)=sqrt(v)·C`, `C=Σ1/√k=11.267648377839` (precompute once) |
| M3 | `approve/ggw_approve.cpp · workload(iters)` | deterministic iterate of M2's `step_opt` → the reproducible result `0.120493199688742` |
| M4 | `approve/ggw_approve.cpp · result_hash(r)` | 64-bit fmix of the IEEE-754 bits (murmur finalizer) — same binary ⇒ same hash |
| M5 | `approve/ggw_approve.cpp · approval_ratio(a,b)` | `SQRT((a/b + b/a)/2)` — 1.0 iff timings equal, grows on divergence |

## Transform / scaling — `ddtm/`, `netswitch-sql/scaling/`

| # | File · function | Computes |
|---|-----------------|----------|
| M6 | `ddtm/ggw_ddtm.cpp · dct2` | forward 8×8 DCT-II with `C(0)=√(1/N)`, else `√(2/N)` |
| M7 | `ddtm/ggw_ddtm.cpp · idct2` | inverse 8×8 DCT-III (reconstruct the block) |
| M8 | `ddtm/ggw_ddtm.cpp · (powered split)` | `|C[u,v]| ≥ 0.01·peak` → powered; else unpowered |
| M9 | `ddtm/ggw_ddtm.cpp · entropy(q)` | Shannon entropy of quantized magnitudes, in **bits** and **nats (ln)** |
| M10 | `ddtm/ggw_ddtm.cpp · (requant + PSNR)` | `q=round(C/Q)`, `C'=q·Q`, `PSNR=10·log10(range²/MSE)` |
| M11 | `ddtm/ggw_ddtm.cpp · (best scaler)` | argmax of `PSNR / (bits/64)` above a 30 dB floor |
| M12 | `ddtm/ggw_ddtm.cpp · (MHz banding)` | spectral efficiency `bits·blocks_per_s / bandwidth` → Mbps per MHz |
| M13 | `scaling/ggw_scaling.sql · dbo.ddtm_constant()` | precompute-once constant `-0.090908889` (LLOG) |
| M14 | `scaling/ggw_scaling.sql · dbo.ddtm_constant_calc(...)` | `LOG10( SQRT((T1·MG(7/9)·LogLogRad)/POWER(POWER(T2,T1),MG(1/6))) / 120W )` |
| M15 | `scaling/ggw_scaling.sql · dbo.hyper_downscale(c,f)` | `c/f` — the hypervisor downscaler |
| M16 | `scaling/ggw_scaling.sql · dbo.thrice_sinwave(t,k,…)` | `|k|·Σ_{i=1..3} aᵢ·sin(2π·fᵢ·t)` |
| M17 | `scaling/ggw_scaling.sql · dbo.lnlog(x)` | `LOG(LOG(x))` — ln of ln; `lnlog(e^e)=1` |
| M18 | `scaling/ggw_scaling.sql · dbo.binary_lnlog(w)` | hex bits of `w` plus its lnlog |
| M19 | `scaling/ggw_scaling.sql · dbo.choose_best_scaler` | DB-side max `PSNR/(bits/64)` above floor |

## Media — `voice-edge/`, `stream-qc/`, `stream-ctl/`, `ipr/`

| # | File · function | Computes |
|---|-----------------|----------|
| M20 | `voice-edge/ggw_voice_edge.cpp · ulaw(pcm)` | G.711 µ-law encode (sign, 8 exponent bands, mantissa) |
| M21 | `voice-edge/ggw_voice_edge.cpp · make_rtp(...)` | 440 Hz sine → PCMU, RFC 3550 RTP header pack |
| M22 | `stream-qc/ggw_streamqc.cpp · PSNR` | `10·log10(255²/MSE)` fidelity in dB |
| M23 | `stream-qc/ggw_streamqc.cpp · blockiness` | Wang/Bovik 8×8-grid pixelation index (dB), no reference |
| M24 | `stream-ctl/ggw_streamctl.cpp · battery (RKF45)` | RC saturation ODE by Runge–Kutta–Fehlberg 4(5) |
| M25 | `stream-ctl/ggw_streamctl.cpp · battery (Laplace)` | analytic first-order check; agrees with M24 to ~1e-15 |
| M26 | `stream-ctl/ggw_streamctl.cpp · pipe capacity` | `bandwidth × MHz` spectral efficiency, −3% floor tier pick |
| M27 | `stream-ctl/ggw_streamctl.cpp · split` | cut points at 1 2 7 15 40 59 80 %, byte-exact rejoin |
| M28 | `ipr/ggw_ipr.cpp · depth ratioing` | far 0.5× / close 1.0× inset scale, inside-only clamp |

## Measure — `sysmon/`, `cputimer/`, `diskbench/`, `thermal/`

| # | File · function | Computes |
|---|-----------------|----------|
| M29 | `sysmon/ggw_sysmon.cpp · measure_cpu_ops_bln` | per-core FMA ops ÷ wall time → billions ops/s |
| M30 | `sysmon/ggw_sysmon.cpp · measure_fps` | offscreen frames rendered ÷ time → FPS |
| M31 | `cputimer/cpu_mhz_timer.h · calibrated RDTSC` | CPU timebase MHz from calibrated cycle count |
| M32 | `cputimer/ggw_radiosig.cpp · reduction chain` | antenna/box/delay/cpu √47.9985 reduction at 1 Hz |
| M33 | `diskbench/ggw_diskbench.cpp · IOPS/MB-s/latency` | fsync-honest seq/random throughput + percentiles |
| M34 | `thermal/ggw_thermal.cpp · trip headroom` | per-part temp vs trip, throttle-swing learn |

## Control — `server-cpp/`, `registrar/`, `backup-codes/`, `secure/`

| # | File · function | Computes |
|---|-----------------|----------|
| M35 | `server-cpp/ggw_server.cpp · rerouting cost` | `cost = RTT × (1 + load/100)`, least-cost reachable POP |
| M36 | `server-cpp/ggw_server.cpp · distance` | `d = 0.0001607 m/ps × TCP-handshake RTT` → signal-path km |
| M37 | `backup-codes/ggw_backupcodes.cpp · sha256` | FIPS 180-4 SHA-256 (also in registrar, voice-edge) |
| M38 | `backup-codes/ggw_backupcodes.cpp · code gen` | Crockford base32, `byte%32` unbiased, 50 bits/code |
| M39 | `registrar/ggw_registrar.cpp · prefix_to_mask` | CIDR prefix → dotted netmask |
| M40 | `secure/ggw_secure.cpp · EC-256 / RSA handshake` | TLS 1.3 key exchange + cert verify (OpenSSL) |

---

Total: **40 math functions** across 20 source files. The three newest modules
(`ddtm`, `approve`, `scaling`) carry inline `// [Mxx]` tags matching this table;
say the word and I'll stamp the rest.

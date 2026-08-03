# 6GGW / NetSwitch — complete source manifest

Every module I've written for this project, in one tree. Each folder is a
self-contained program with its own `README.md`, its source (`*.cpp`, one
`*.sql` for the data plane), and where relevant a `build.sh`. No hidden
dependencies — plain C++17 (and T-SQL for the database), builds on Linux with
`g++` and on Windows with MinGW / the VS cross-tools prompt.

Build everything at once with `build-all.sh` (Linux) — it compiles each module
and runs its self-test.

## Networking / switch core

| Folder | Program | What it is |
|--------|---------|-----------|
| `server-cpp/` | ggw_server | The NetSwitch server: session registry, least-cost rerouting, region failover, the HTTP/socket control surface. The big one (~39 KB). |
| `replicate/` | ggw_replicate | Phone-to-phone multi-stream replication transport — STATE (reliable delta), FILE/attachment (reliable, SHA-256 verified), AUDIO + VIDEO (real-time). Roadmap #1. |
| `stream-ctl/` | ggw_streamctl | Stream control / rerouting: least-cost path, RTT geodistance, POP selection, `.switchc` routing. |
| `stream-qc/` | ggw_streamqc | Call-quality metering — Black/Grey/White throughput classes tied to reroute decisions (Low/Med/High). |
| `voice-edge/` | ggw_voice_edge | G.711 µ-law voice edge + RTP packing, the Walkie-Talkie relay core. |
| `registrar/` | ggw_registrar | Device/session registration + one-time code issuance. |
| `secure/` | ggw_secure | TLS 1.3 tunnel + cert generation (`gen_certs.sh`, `tunnel.sh`). |
| `client-cli/` | ggw_cli | Thin-client command-line switch client. |
| `client-qt/` | (Qt app) | Desktop thin-client GUI (CMake + Qt). |

## Benchmark / measurement suite

| Folder | Program | What it is |
|--------|---------|-----------|
| `report-engine/` | ggw_report | The report engine you ran — MFLOPS (matmul + FMA), CPU/RAM/disk inventory, OS compatibility verdict, writes phone-friendly HTML. |
| `dramm/` | ggw_dramm | DRAMM CPU workload / physics micro-engine (deterministic fixed-point kernel). |
| `loopbench/` | ggw_loopbench | Sustained-loop throughput (ops/s, FPS, MFLOPS) with reproducibility checksums. |
| `cputimer/` | ggw_cputimer + ggw_radiosig | RDTSC-calibrated MHz timer and radio-signal reduce. |
| `diskbench/` | ggw_diskbench | fsync-honest disk IOPS / MB-s / latency percentiles. |
| `thermal/` | ggw_thermal | Per-part thermal headroom / throttle-margin monitor. |
| `sysmon/` | ggw_sysmon | Live host telemetry (cores, mem, load) — no personal data. |
| `intbench/` | ggw_intbench | INT2/4/8/16 loopset timer (GOPS), three bars GPU/CPU/HPU. GPU/HPU = honest hook until backend linked. |
| `drammtune/` | ggw_drammtune | DRAMM combination-search tuner (threads×duty) with a battery-energy model. GPU = hook. |
| `benchrun/` | ggw_benchrun | One-pass CPU+RAM+thermal runner; names the bottleneck via roofline crossover. GPU = hook. |
| `dramtest/` | ggw_dramtest | DRAM cell tester (write ×N → zero-overwrite → verify → warming rule → per-cell radian log). Detects stuck cells. |
| `productmeasure/` | ggw_productmeasure | Device MEM + AI measure for product management (bandwidth + cell integrity + dense-GEMM inference floor → one readiness verdict; `json` mode for CI). GPU = hook. |
| `thermocalc/` | ggw_thermocalc | Thermodynamics (Cp/entropy/enthalpy/ideal/van-der-Waals/Stirling), each closed form self-checked vs numeric integration. |
| `siso/` | ggw_siso | SISO transfer-function analyser (poles/zeros via Durand-Kerner) — the Nokia-2022 plant: UNSTABLE + non-minimum-phase. |
| `gpuscreensaver/` | index.html | Three-line GPU/CPU/cooperative screensaver, driven by real WebGL + CPU measurements. GPU line → device counters when the GPU hook lands. |

## Numeric / signal functions

| Folder | Program | What it is |
|--------|---------|-----------|
| `ddtm/` | ggw_ddtm | DDTM log-domain scaling constant (locks to your −0.090908889 once T1/T2/LogLogRad are given). |
| `ipr/` | ggw_ipr | DCT-II/IDCT image-block transform, entropy, PSNR, powered/unpowered split (6ggw-ipr). |
| `approve/` | ggw_approve | Deterministic system-approval check — "server AI == thin-client AI": same binary, same input, same answer everywhere. |
| `backup-codes/` | ggw_backupcodes | Crockford base32 CSPRNG backup-code generator (50-bit entropy, unbiased). |

## Data plane (MS SQL)

| Folder | File | What it is |
|--------|------|-----------|
| `netswitch-sql/` | netswitch_sql.cpp | ODBC data-plane connector (device_session, sccm_block, scaler_result). |
| `netswitch-sql/scaling/` | ggw_scaling.sql + verify_math.py | Scaling math in T-SQL with a Python cross-check. |

## Docs (root)

`ARCHITECTURE.md`, `CLIENTS.md`, `MATH-INDEX.md` (M1–M40), `DB-CONNECTIONS.md`,
`INSTALL-COMPATIBILITY.md`, `FUNCTIONS-SCIENCE-FAT.md`, and
`switch-config/SCHEDULE-NOV.md` (the November gamehouse plan).

## Build

```bash
./build-all.sh                     # Linux: build + self-test every module
# per-module Windows static exe, e.g.:
x86_64-w64-mingw32-g++ -std=c++17 -O2 server-cpp/ggw_server.cpp -o ggw_server.exe -static -lws2_32
```

Not included in this archive: `node_modules/`, CMake/Gradle `build/` output, and
compiled binaries — those regenerate from the source here. The mobile app
packaging lives under `packaging/ggw6-app/` (Capacitor project; its `BUILD-APK.md`
has the steps).

# 6GGW server component — native C++ build

This is the **server half** of the 6GGW product, rewritten from `server.js` into a single
C++17 program. Same wire API, same real measurements, same AES-256-GCM per-device database —
but it compiles to one native binary with no interpreter to install. Drop it on the box at
each of your two sites and run it.

The client PWA does not need to change: it talks to the exact same endpoints.

---

## Build

You need a C++17 compiler plus OpenSSL and zlib dev headers (present on every mainstream Linux;
on Ubuntu/Debian: `sudo apt install g++ libssl-dev zlib1g-dev`, or with cmake `cmake`).

**One-liner (no cmake):**
```
g++ -std=c++17 -O2 ggw_server.cpp -o ggw_server -lssl -lcrypto -lz -lpthread
```

**Or with cmake:**
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Either way you get a `./ggw_server` binary (~170 KB).

## Run

```
./ggw_server                 # listens on :8090, channel=dev
PORT=80 ./ggw_server         # any port
CHANNEL=rc PORT=8091 ./ggw_server
DB_KEY=your-real-secret CHANNEL=prod PORT=443 ./ggw_server
```

Environment variables (all optional):

| var           | default                 | meaning                                             |
|---------------|-------------------------|-----------------------------------------------------|
| `PORT`        | `8090`                  | TCP port to listen on                               |
| `CHANNEL`     | `dev`                   | `dev` \| `rc` \| `prod` — release channel, reported in health |
| `DB_KEY`      | `6ggw-dev-key-change-me`| passphrase for the per-device DB (hashed to AES-256 key). **Set a real one for rc/prod.** |
| `SEC_SHORT_MS`| `15000`                 | security watch short arm (fast tick)                |
| `SEC_LONG_MS` | `300000`                | security watch long arm (firewall self-test + roll-up) |
| `SEC_FW_HOST` | `one.one.one.one`       | egress reachability probe target                    |
| `SEC_FW_PORT` | `443`                   | egress probe port                                   |

## Endpoints (identical to the Node build)

| method | path                                        | what it does                                                        |
|--------|---------------------------------------------|---------------------------------------------------------------------|
| GET    | `/api/health`                               | this server's own live health (load, memory, uptime, cores) — all real |
| GET    | `/api/ping`                                 | tiny echo; the **client** times it for the real server↔phone line length |
| GET    | `/api/distance?host=bbc.co.uk[&port=443]`   | DNS resolve + **real TCP-handshake RTT** (best of 3) → signal-path km |
| GET    | `/api/estimate?users=&bitrate=&server=`     | delivery capacity for N post addresses (streams, coverage, servers needed) |
| POST   | `/api/device/health`                        | store a device snapshot in its OWN AES-256-GCM DB. Body: `{"id":"phone-01", ...stats}` |
| GET    | `/api/device/history?id=phone-01`           | read that device's decrypted history                                |
| GET    | `/api/devices`                              | list known device IDs                                               |
| POST   | `/api/backup`                               | bundle every (still-encrypted) device DB → `data/backups/*.6ggwbak.gz` for tape/LTO |
| GET    | `/api/backups`                              | list backup archives                                                |
| GET    | `/api/security`                             | round-the-clock watch status + firewall/egress self-test            |
| GET    | `/api/security/log?n=50`                    | tail the security log                                               |
| GET    | `/`                                         | serves the bundled 6GGW client (the PWA one level up), so one box ships both halves |

### What is really measured (nothing modelled on this side)

* **`/api/distance`** opens a real TCP socket to the target and times the SYN/SYN-ACK round trip
  (best of 3 samples, take-closest), then turns that latency into the signal-path cable length using
  your constant **d = 0.0001607 m per picosecond** (~160,700 km/s in cable). It is the honest,
  portable stand-in for a raw ICMP traceroute (which needs root and is blocked on most hosting),
  and it is genuinely measured every call.
* **`/api/health`** and the security roll-up read the real OS: load average, free/total memory,
  core count, uptime.
* **`/api/security`** runs a two-hand "clock" watchdog: a fast arm samples request/error rates,
  a slow arm rolls the window up and runs a live outbound-firewall reachability self-test, raising
  `ALERT`s to `data/security/security-<date>.log`.

## Data on disk

```
server-cpp/
  data/
    devices/<id>.db        # one file per device, AES-256-GCM encrypted at rest (iv|tag|ciphertext)
    backups/*.6ggwbak.gz   # gzip bundle of the encrypted blobs + a checksummed manifest (safe for tape)
    security/security-*.log
```

**Encryption at rest:** each device DB is AES-256-GCM with the key = SHA-256(`DB_KEY`). Verified:
the raw `.db` file contains no plaintext field names, only ciphertext. Transport security is TLS —
put this behind nginx/Caddy, or terminate TLS in front of it, for `https://`.

**Envelope compatibility with the Node build:** the DB envelope (`iv(12) | tag(16) | ciphertext`)
and the key derivation (SHA-256 of `DB_KEY`) are byte-for-byte the same as `server.js`. A DB or
backup written by one build decrypts on the other (verified). The only internal difference is the
plaintext record layout: this build stores history as NDJSON; the API responses are unchanged.

## Running 24/7 at your two sites (systemd)

Copy `ggw-server.service` to `/etc/systemd/system/`, edit the path/port/channel, then:

```
sudo systemctl daemon-reload
sudo systemctl enable --now ggw-server
journalctl -u ggw-server -f
```

Run one instance per site (e.g. `CHANNEL=rc` at site-A, `CHANNEL=prod` at site-B, or the same
channel on both) and point the client at each. The `/api/distance` numbers from the two sites are
what let you compare the two routes.

## Parity with the Node build (verified)

`/api/estimate` output is byte-identical to `server.js` (aside from Node pretty-printing vs this
build's compact JSON — same keys, same values, same order). `/api/ping`, `/api/devices`,
`/api/backups`, `/api/security` all match in shape and values. The AES envelope cross-decrypts.

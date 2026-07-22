# 6GGW — Architecture

A network & hardware maintenance platform in two halves that ship together.

See `docs/architecture.html` for the diagram (open it in any browser).

```
   CLIENT (6GGW PWA)                         SERVER COMPONENT (Node, zero-dep)
   Android/iOS/Win/Mac/Linux                 HCL / Fujitsu / any Linux box
   ─────────────────────────                 ─────────────────────────────────
   search (IP/phone/router/domain)           GET  /api/health      (real OS health)
   RAM·CPU·heap live jam guard      HTTPS     GET  /api/ping        (client times line length)
   Health Snapshot (share)   ◀───────────▶   GET  /api/distance    (real DNS+TCP RTT → km)
   Stream delivery estimate       TLS        GET  /api/estimate    (delivery capacity)
   Domain route + MAC + reroute              POST /api/device/health   ─┐
   Storage read/write throughput             GET  /api/device/history   │ per-device
   Switch control (.switchc/.xml)            GET  /api/devices          │ encrypted DB
   Line length (signal-path km)                                        ─┘ AES-256-GCM
                                             CHANNEL = dev | rc | prod
```

## 1 · Client — the 6GGW PWA (`/index.html`, `/sw.js`, `/manifest.webmanifest`)

One codebase installs on all platforms (add-to-home-screen, offline via service worker).
Every value that a browser *can* read is read live (StorageManager, IndexedDB, Battery API,
`navigator.connection`, WebGL, Web Workers). Anything a browser physically cannot do —
raw traceroute, controlling the radio, reading raw flash, locating a phone number — is either
handed to the server (where it's done for real) or labelled on screen as a model. **Nothing is faked.**

## 2 · Server component (`/server/server.js`)

Zero external dependencies — pure Node (`http`, `net`, `dns`, `os`, `crypto`). Runs with
`node server.js`. It serves the bundled client too, so **one box ships both halves**.

- **`/api/distance`** — the real measurement a phone can't make: resolves DNS, opens a real TCP
  socket to the host and times the handshake (best of 3 samples), then the signal-path cable
  length from that latency using `d = 0.0001607 m/ps`. This is the honest, portable stand-in for
  a raw ICMP traceroute (which needs root and is blocked on most hosts).
- **`/api/health`** — the server's own CPU load, cores, memory, uptime, from the OS.
- **`/api/ping`** — tiny echo; the client times it to get the real server↔phone line length.
- **`/api/estimate`** — MPEG-4 / broadband delivery capacity for N post addresses, plus the
  fixed-line / mobile-Wi-Fi / 5G comparison.

## Release channels — dev / rc / prod

Set `CHANNEL=dev|rc|prod` (default `dev`). Same code, different config:

- **dev** — your working build.
- **rc** — the release candidate you run and test before promoting (this is what you'll run
  from two locations tomorrow).
- **prod** — the production build.

The active channel is reported in `/api/health` and stamped on every stored record.

## Per-device DB & encryption

Each hardware device gets its **own** database file: `server/data/devices/<id>.db`. Records are
the Health Snapshots the device POSTs. Files are **encrypted at rest with AES-256-GCM** (Node's
built-in `crypto`); the key comes from `DB_KEY` (set a real one for rc/prod). Last 500 snapshots
per device are kept. The `data/` directory is git-ignored so device data never leaves the box.

### On the "SSL 1467-bit" note — the honest picture

Your spec said *SSL 1467-bit*. Two separate layers here, and I want to be straight about the numbers:

- **In transit (TLS):** the server sits behind TLS (run it behind nginx/Caddy, or Node's `https`
  with a real certificate). TLS key sizes are **fixed by the standard** — RSA **2048** or **4096**
  bits, or elliptic-curve (P-256 / P-384). A non-standard size like 1467 isn't accepted by any TLS
  library or browser (RSA moduli are chosen at standard lengths; browsers reject odd sizes). So the
  honest, interoperable choice is **RSA-4096 or ECC P-384** — stronger than 1467 and actually valid.
- **At rest (the DB):** encryption is **AES-256** (symmetric) — 256-bit key, which is the standard
  strong choice and not tied to the 1467 figure.

If the 1467 came from a specific requirement (a customer, a certifier like DNV), tell me the source
and I'll map it to the correct standard parameters — I won't put a number in that a real TLS stack
would reject at the fair.

## Deploy

```bash
cd server
CHANNEL=rc DB_KEY="a-strong-secret" node server.js     # or PORT=80
```

Front it with nginx/Caddy for TLS in rc/prod. No database server, no secrets in the repo, no
outbound calls except the latency probe you request.

© AI2ORBIT Co. 2026

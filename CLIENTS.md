# 6GGW / NetSwitch — the product, end to end

One product, four interchangeable faces over a single HTTP API. Everything below the API is
**C++** (with the original Node build kept as a reference); the phone app is the installable web
app; the desktop/CLI faces are the new Qt and command-line clients.

```
                         ┌───────────────────────────────────────────────┐
                         │              6GGW server component             │
                         │   server-cpp/  (native C++, primary)           │
                         │   server/      (Node.js, reference build)      │
                         │                                                │
                         │   /api/health  /api/ping  /api/distance        │
                         │   /api/estimate  /api/device/*  /api/backup    │
                         │   /api/security …   (one wire API for all)     │
                         └───────────────▲───────────────▲────────────────┘
                                         │               │
             ┌───────────────┬───────────┴───┬───────────┴──────────┐
             │               │               │                      │
     ┌───────┴──────┐ ┌──────┴───────┐ ┌─────┴────────┐   ┌─────────┴─────────┐
     │  PWA client  │ │  Qt client   │ │  CLI client  │   │  native iOS /     │
     │  index.html  │ │  client-qt/  │ │ client-cli/  │   │  Android          │
     │  (phone,     │ │  Win/Lin/mac │ │  Win/Lin/mac │   │  (same Qt source) │
     │   installable)│ │  + mobile    │ │              │   │                   │
     └──────────────┘ └──────────────┘ └──────────────┘   └───────────────────┘
```

## The pieces

| Folder | What it is | Build | Verified here |
|--------|------------|-------|---------------|
| `index.html`, `sw.js`, `manifest.webmanifest` | the installable PWA (phone app base) | none — it's web | ✓ (Playwright) |
| `server-cpp/` | **native C++** server — the primary server build | `g++`/CMake (OpenSSL+zlib) | ✓ 12/12 endpoints |
| `server/` | Node.js server — reference build, same API | `node server.js` | ✓ parity-checked |
| `client-qt/` | **Qt** desktop client (Win/Linux/macOS + iOS/Android) | Qt Creator / CMake / qmake | ✓ all 4 tabs, live |
| `client-cli/` | **CLI** client (Win/Linux/macOS) | `g++` / mingw / MSVC | ✓ all commands, live |

## The one API they all speak

| endpoint | meaning |
|----------|---------|
| `GET /api/health` | server's own live health (load, memory, cores, uptime, channel) |
| `GET /api/ping` | tiny echo; the client times it for the real server↔device line length |
| `GET /api/distance?host=` | DNS + real TCP-handshake RTT (best of 3) → signal-path km (d = 0.0001607 m/ps) |
| `GET /api/estimate?users&bitrate&server` | delivery capacity (streams, coverage, servers needed) |
| `POST /api/device/health`, `GET /api/device/history`, `GET /api/devices` | per-device AES-256-GCM database |
| `POST /api/backup`, `GET /api/backups` | gzip backup bundles for tape/LTO |
| `GET /api/security`, `GET /api/security/log` | round-the-clock watch + firewall self-test |

## Rerouting (the focus)

Both new clients implement the rerouting view the same way:

1. Measure a **real TCP-handshake RTT** to each POP (the around-the-world list: London, Helsinki,
   Tallinn, Stockholm, Oslo, Warsaw, Amsterdam, Frankfurt, New York, Los Angeles, Moscow, Greenwich).
2. Weight it by a per-POP link-utilisation indicator into a least-cost score: `cost = RTT × (1 + load/100)`.
3. Pick the lowest-cost reachable POP as the **AI best path**, and log every POP's number.

RTT, ping and distance are hard measurements. `load%` is a clearly-labelled utilisation indicator
used only to weight the score — no invented "measurements".

## What is and isn't buildable on my box vs yours

- **Built and verified here:** the C++ server, the Qt desktop client (compiled against system Qt5,
  run headless, all four tabs exercised against a live server — see `docs/`), and the CLI client for
  both Linux **and** Windows (`ggw_cli.exe`, cross-compiled with mingw).
- **Built on your side (needs the mobile SDKs):** the native iOS `.ipa` and Android `.apk`/`.aab`.
  They come from the *same* `client-qt/` source in Qt Creator with the iOS/Android kits — no code
  changes. Steps are in `client-qt/README.md`.

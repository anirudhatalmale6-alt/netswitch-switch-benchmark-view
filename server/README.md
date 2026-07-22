# 6GGW Server Component

The **server** half of the client/server product. The 6GGW app (the PWA) is the **client**;
this is the small server piece that runs on a real box (HCL / Fujitsu / any Linux, or a laptop)
and does the measurements a browser physically cannot — a real traceroute-style latency probe,
and the delivery-capacity maths for many users from one server.

**Zero dependencies.** Just Node 16+.

## Run

```bash
cd server
node server.js            # listens on http://localhost:8090
# or choose a port:
PORT=80 node server.js
```

Open `http://<server-ip>:8090/` — it serves the full 6GGW client, so **one box ships both halves**.

## API (all real, measured server-side)

| Endpoint | What it does |
|---|---|
| `GET /api/health` | This server's live health: CPU cores, load average (1/5/15 min), total/free memory, uptime — read straight from the OS. This is the "server" side of the maintenance view. |
| `GET /api/ping` | Tiny echo. The **client (phone in your hand) times this request** to get the real server↔phone **line length** (ping to the phone, exactly as you described with bbc.co.uk). |
| `GET /api/distance?host=bbc.co.uk` | Resolves DNS, then does a **real TCP-handshake RTT** to the host (3 samples, takes the closest), and computes the signal-path cable length from that latency using the constant `d = 0.0001607 m/ps`. This is a genuine network measurement — the honest, portable stand-in for a raw ICMP traceroute (which needs root and is blocked on most hosts). |
| `GET /api/estimate?users=30000&bitrate=1.5&server=10` | MPEG-4 / broadband **delivery capacity** for N post addresses from one server, plus the fixed-line / mobile-Wi-Fi / 5G comparison. `bitrate` in Mbps/stream, `server` uplink in Gbps. |
| `POST /api/device/health` | Store a device's health snapshot in its **own encrypted DB** (`data/devices/<id>.db`, AES-256-GCM at rest). Body: `{"id":"site-A", ...stats}`. |
| `GET /api/device/history?id=site-A` | Read that device's stored history (decrypted server-side). |
| `GET /api/devices` | List known device IDs. |
| `POST /api/backup` | Bundle **all** device DBs into one gzipped, checksummed archive under `data/backups/*.6ggwbak.gz` — ready to copy onto tape (IBM/LTO) or offsite. Blobs stay encrypted inside the archive. |
| `GET /api/backups` | List archives already made. |
| `GET /api/security` | Round-the-clock **security watch** status: uptime, request/error counters, the firewall/egress self-test result (`fw_ok`), and recent alerts. |
| `GET /api/security/log?n=50` | Tail the security event log (JSON lines). |

## Security watch (round-the-clock)

A watchdog runs on a two-hand clock the whole time the server is up, and writes to
`data/security/security-<date>.log`:

- **short arm** (default every 15 s) — samples request/error rate and checks the listener is still bound; raises an ALERT on an error-rate spike or a downed listener.
- **long arm** (default every 5 min) — rolls the window into a summary line and runs the **firewall / egress self-test**: a real TCP connect to an outbound target (default `one.one.one.one:443`). If it can't get out, it logs `ALERT egress_blocked` with the hint that an outbound firewall may be blocking that port — exactly the "make sure no fw issues" check.

Every 4xx/5xx and every write (POST) is logged with the client IP, path and status. Tune with
`SEC_SHORT_MS`, `SEC_LONG_MS`, `SEC_FW_HOST`, `SEC_FW_PORT`. Check it at a glance from the client
(CPU·GPU tab → SECURITY WATCH), or:

```bash
curl http://localhost:8090/api/security        # fw_ok, counters, recent alerts
curl "http://localhost:8090/api/security/log?n=50"
```

Note: this watches the server process and its egress reachability (the honest, portable check). For
OS-level packet-filter rules, keep running your `ufw`/`firewalld`/`iptables` audit alongside it.

## Release channels

`CHANNEL=dev|rc|prod node server.js` — same code, different config. `dev` = your working build, `rc` = the release candidate you test before promoting, `prod` = production. Reported in `/api/health` and stamped on every stored record. Set a real `DB_KEY=...` for rc/prod.

## Backup of network data (for tape / LTO / offsite)

The per-device DBs are already **AES-256-GCM encrypted at rest**, so backups carry the encrypted
blobs byte-for-byte — the DB key never has to touch the backup process. Two ways:

```bash
# 1) via the running server
curl -X POST http://localhost:8090/api/backup      # -> data/backups/backup-<channel>-<ts>.6ggwbak.gz (+ .sha256)

# 2) standalone (no server, no key) — ideal for cron -> tape
node backup.js                                     # writes the same archive

# verify + restore an archive (checks the .sha256 and every per-device hash first)
node restore.js data/backups/backup-....6ggwbak.gz --verify   # verify only
node restore.js data/backups/backup-....6ggwbak.gz            # restore into data/devices/ (keeps *.db.bak)
```

Point your IBM tape / LTO job (or `rsync`, or any offsite store) at `data/backups/`. A nightly cron:

```cron
0 2 * * *  cd /path/to/server && node backup.js >> data/backups/backup.log 2>&1
```

### Example

```bash
curl "http://localhost:8090/api/distance?host=bbc.co.uk"
# { "host":"bbc.co.uk", "ip":"...", "rtt_ms":5.23, "samples":[8.93,5.23,5.36],
#   "signal_path_km":420.1, "method":"TCP-handshake RTT, best of 3, ..." }

curl "http://localhost:8090/api/estimate?users=30000&bitrate=1.5&server=10"
# 45 Gbps demand, 6,666 streams/server, 22.2% coverage of 30,000 addresses, 5 servers for all
```

## Why this matters

The client app labels its route/distance numbers as **modelled**, because a phone browser
can't run a raw traceroute. Point the client at this server and those same numbers become
**measured** — real RTT, real distance, real capacity. Nothing here is faked; the TCP handshake
actually opens a socket to the target and times the round trip.

## Deploy notes

- Runs anywhere Node runs. For a public box, put it behind nginx/Caddy for TLS, or run `PORT=80`.
- Per-device data lives in `data/devices/` (encrypted at rest); backups in `data/backups/`. Both are git-ignored.
- The only outbound calls are the latency probes you ask it to make. Set `DB_KEY` for rc/prod.
- Android/Windows/Mac/Linux clients all just open the served page or call the API.

© AI2ORBIT Co. 2026

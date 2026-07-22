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
- No database, no secrets, no external calls except the latency probe you ask it to make.
- Android/Windows/Mac/Linux clients all just open the served page or call the API.

© AI2ORBIT Co. 2026

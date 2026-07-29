# 6GGW / NetSwitch — user + device registrar (`ggw_registrar`)

The "server component that keeps track of users" — the security gate in front of
the switch ↔ server ↔ serverless path. A user proves who they are with a
**one-time backup code** from the list (see `backup-codes/`); on success the
registrar records that device's network parameters as a session.

## What it logs (the spec's IP/GW/MASK/DNS/DNS2/DNS-peripheral/DHCP)

```
enroll : verify a backup code (single-use, burned) -> record the device
list   : show enrolled device sessions
```

```
# with real params read off the device (Linux best-effort):
ggw_registrar enroll --config codes.switchc.xml --code ERRKF-CH7X4 --user sami --auto

# or pass them explicitly (any OS):
ggw_registrar enroll --config codes.switchc.xml --code ERRKF-CH7X4 --user sami \
  --ip 10.0.0.42 --gw 10.0.0.1 --mask 255.255.255.0 \
  --dns 1.1.1.1 --dns2 8.8.8.8 --dns-periph 9.9.9.9 --dhcp on

ggw_registrar list
```

A session record (one JSON line in `registry.log`):

```json
{"seq":1,"time":"2026-07-29 07:34:35","user":"sami","code":"#1",
 "ip":"167.235.196.123","gw":"172.31.1.1","mask":"255.255.255.255",
 "dns":"127.0.0.53","dns2":"n/a","dns_periph":"n/a","dhcp":"on"}
```

`--auto` (Linux) fills IP, gateway, netmask (prefix→dotted), the DNS servers
from `resolv.conf`, and reads **DHCP vs static** off the route/address flags —
all real reads. On Windows pass the fields as flags.

## Security is the gate, not an afterthought

- No valid code → **no session**. Replay a used code → **denied**. Garbage →
  **denied**. Only an unused backup code enrolls a device.
- The backup-code check is byte-identical to `backup-codes/`: salted SHA-256, the
  config holds only hashes, each code burns on use. Verified on Linux and
  Windows (`.exe`), including replay-denial and the burn persisting.

## How this answers the RTP / media-edge architecture question

Your voice/walkie-talkie rerouting maps onto standard, proven pieces — here's
the honest layout of what's ours vs. what's an off-the-shelf carrier component:

```
  phone (thin client)            control plane                media plane
  ─────────────────              ─────────────                ───────────
  SIP UA + sysmon      ──SIP──▶  Kamailio (SIP proxy/     ┌▶ rtpengine  = RTP HANDLER
  (registers, monitors)          registrar, signalling)   │  + RTP SCALER (relay,
        │                              │                   │    transcode, rate-adapt)
        │  enrol (backup code)         │ picks edge via    │
        ▼                              ▼ NetSwitch route   │  = MEDIA EDGE HANDLER
  ggw_registrar  ◀──────── 6GGW rerouting engine ─────────┘   (relay at the lowest-
  (this tool: authz +      (measures RTT to every POP,          cost reachable POP)
   device/session log)      cost = RTT·(1+load/100),
        │                    least-cost path = the edge)
        ▼
  MS SQL (netswitch-sql/) — device/session store, scaler state
```

- **RTP HANDLER / RTP SCALER** = `rtpengine` (open-source, carrier-grade): it
  relays the RTP, and scales by rate-adapting / transcoding per link. We don't
  reinvent it — we *place* and *steer* it.
- **MEDIA EDGE HANDLER** = an rtpengine instance at each edge POP. The
  **NetSwitch rerouting engine already does the hard part**: it measures real
  RTT to every POP and picks the least-cost reachable one — that same decision
  chooses which media edge a call's RTP flows through, which is exactly your
  "reroute for non-congestion" goal.
- **Kamailio** handles SIP signalling (who's calling whom, registration); it
  asks the rerouting engine which edge to pin the media to.
- **Switch ↔ server ↔ serverless, phone as thin-client, MS SQL** — yes, this
  shape works: the phone is a thin SIP client + monitor; this registrar is the
  authz/control function (stateless, so it scales out serverless behind a load
  balancer — "scaler multiples"); the `netswitch-sql/` component is the MS SQL
  store for device/session/scaler state.

What I'd build next for the working demo: a tiny SIP-register + RTP-echo test
between two thin clients through one rtpengine edge, with the registrar gating
enrolment and the rerouting engine choosing the edge. That's a real, runnable
slice of the whole picture — say the word.

## Build

```
# Linux
g++ -std=c++17 -O2 ggw_registrar.cpp -o ggw_registrar

# Windows (mingw; MSVC works too)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_registrar.cpp -o ggw_registrar.exe -static
```

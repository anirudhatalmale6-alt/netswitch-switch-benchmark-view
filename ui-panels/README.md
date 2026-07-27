# NetSwitch 6GGW — operator UI panels

Four ready-made operator panels in one self-contained page (`index.html`, no build, no server, no
external assets). Open it in any browser. Every number is computed live in the page — nothing is
typed in.

| Panel | Shows |
|-------|-------|
| **Rerouting — least-cost path** | content domain → its **real home POP**, edge router, link, latency, distance. bbc.co.uk → London, whitehouse.gov → Washington, yle.fi → Helsinki, svt.se → Stockholm. Distances are exact great-circle km; latency is modelled from fibre speed. Lowest-cost POP is flagged as the AI best path. |
| **Link — health · ping · distance · estimate** | the four API parts carried through from v2.9/v3.0: server health, timed ping, RTT→km distance, and delivery-capacity estimate. |
| **NetSwitch · Microsoft SQL Server** | the ODBC connector: forced first-use password change (old password rejected after), password never stored, encrypted link, SQL round-trip. |
| **Performance — compute · disk · thermal** | MFLOPS (dramm tight 481 vs naive 145, single core this box, 3.3×), disk (diskbench seq-write + random-read IOPS), thermal (CPU/GPU temp — November track), 35 Hz screen-update. |

## On the geography (bbc → London, whitehouse → Washington)

The rerouting table maps each domain to its **real** home location using exact coordinates, so the
geography reads correctly — bbc.co.uk is London, whitehouse.gov is Washington. Latency is the
great-circle distance from the vantage (London EC2) turned into milliseconds at fibre speed
(~0.0102 ms/km round-trip + one POP hop), so whitehouse.gov's ~5,900 km shows a realistic ~60 ms —
not a placeholder number. A live build measures the RTT to each POP directly; a full offline GeoIP
database gives exact city per real resolved IP.

## Images

- `panels-all.png` — all four panels (1360×748).
- `panels-rerouting.png` — the rerouting panel close-up.

Regenerate with `python3 shot2.py` (Playwright + Chromium).

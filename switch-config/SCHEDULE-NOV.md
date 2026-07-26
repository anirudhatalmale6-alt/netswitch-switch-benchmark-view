# 6GGW / NetSwitch — module map + November plan

## All modules, one picture

```
                        ┌──────────────────────────────────────────────┐
                        │            6GGW / NetSwitch server            │
                        │   server-cpp/ (C++, primary) · server/ (Node) │
                        │   one HTTP API: health ping distance estimate │
                        │   device backup security                      │
                        └───▲──────▲──────▲──────▲──────▲──────▲─────────┘
                            │      │      │      │      │      │
        ┌───────────────────┘      │      │      │      │      └───────────────┐
        │            ┌─────────────┘      │      └──────────┐                  │
   ┌────┴─────┐ ┌────┴─────┐ ┌────────────┴───┐ ┌───────────┴──┐ ┌────────────┴───┐
   │  CLIENTS │ │  ROUTE   │ │    STREAM      │ │     IPR      │ │    SECURE      │
   │ pwa/qt/  │ │ rerouting│ │ stream-ctl +   │ │ image-in-pic │ │ TLS EC-256 /   │
   │ cli      │ │ POP RTT  │ │ stream-qc      │ │ compositor   │ │ RSA · DTLS ·   │
   │ desktop+ │ │ least-   │ │ tiers·pipe·    │ │ insets·depth │ │ SSH tunnel ·   │
   │ mobile   │ │ cost     │ │ split·QC(dB)   │ │ ·bezel       │ │ signed config  │
   └──────────┘ └──────────┘ └────────────────┘ └──────────────┘ └────────────────┘
                                     │
                              ┌──────┴──────┐
                              │    DRAMM    │  tight compute kernel (CPU) + CUDA-C twin
                              │ cpu/gpu/    │  the CPU/GPU/thermal/memory/screen-update track
                              │ thermal/mem │
                              └─────────────┘

   CONFIG LAYER (new):  example.switchc  —  application/vnd.6ggw-switch
   a Junos-XML-style file that sets host, interfaces (v4+v6), the POP list & weights,
   stream tiers/QC, IPR, and security. Applied at boot and re-appliable live — but only
   if signed and inside capacity (that's what blocks a hostile config from hijacking routing).
```

## What is real today (built + verified in the repo)

- Server (12/12 endpoints), PWA + Qt (desktop **and** mobile from one source) + CLI clients.
- Rerouting (measured RTT least-cost), stream-ctl (tiers/pipe/split), stream-qc (dB pixelation gate).
- IPR compositor, secure transport (TLS 1.3 EC-256/RSA), dramm compute kernel (CPU tight vs naive = 3.3×; CUDA-C twin ready for your GPU box).

## November plan (London gamehouse demo)

| When | Piece | Deliverable |
|------|-------|-------------|
| **Now → early Oct** | `.switchc` config layer | file the media type; server reads/validates a signed .switchc at boot; live re-apply |
| **Oct** | Reroute-by-config + guardrails | config sets POP list/weights; signature + load-gate block a rogue config; tests |
| **Oct** | IPv4/IPv6 dual-stack + rebind | run through an IP change without dropping; broadcast-marking; IPv6 path |
| **Oct → Nov** | Performance/telemetry | dramm on the box; CPU/GPU/thermal/memory readout; 35 Hz screen-update piece + short video cast |
| **Nov** | Demo polish | one-command build of every module; the config drives a live reroute + stream on screen at the fair |
| **Winter (post-Nov)** | Protocol breadth | GPS / Bluetooth / Wi-Fi variants (NetSwitch Wi-Fi), "all modern protocols" — roadmap, not Nov-blocking |

Honest split: everything in the "Now/Oct/Nov" rows is buildable and demoable by November on top of
what already exists. The winter row (all-protocols, GPS/BT breadth) is a stretch track — real, but
not something I'd promise for the November stage.

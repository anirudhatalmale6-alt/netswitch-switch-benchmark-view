# Week 2 panels — trace/waterfall · board heat-map · call quality

Roadmap items #2 and #8 (with the #5 throughput↔quality engine folded in), in one
self-contained page: `week2.html`. No build, no server, no external assets — open it
in any browser or on the phone. Every number is computed live in the page.

## The three panels

**1. Trace — call + replication waterfall** (item #2)
A devtools-style waterfall of one phone-to-phone session. The setup chain runs first
(DNS → TCP handshake → TLS 1.3), then the four transport streams open in parallel:
AUDIO and VIDEO (real-time) come up first so the call is live within a couple of RTTs,
while STATE (reliable delta) and FILE (reliable 32 KB) run alongside — FILE is the
longest bar because it's chunked and ACK'd. Every offset and duration is derived from
one handshake RTT, not typed in. A live build stamps each segment straight from the
replication transport's own packet log.

**2. Board heat-map — POP load** (item #2)
A region × node grid (6 regions × 8 edge nodes). Each cell is a node's utilisation,
coloured green (headroom) → amber → red (hot). The hottest cell is boxed. This is what
the rerouting engine reads: new sessions are steered to the coolest reachable node, off
the red ones.

**3. Call quality — Black / Grey / White** (items #8 + #5)
Three running signal levels in dB — Black (noise floor), Grey (mid), White (clean) —
each with a live bar. The selector is exactly your notation: **[X] Low [0] Medium
[ ] High** — `[X]` active tier, `[0]` reachable, `[ ]` off. Below it, the
throughput↔quality engine: as available throughput falls (**1500 → 32 kbps**) the switch
lowers the codec bitrate but *protects the call* — when White quality dips toward the
target it reroutes to a cleaner POP rather than let the call degrade. That "lower
throughput, better call" trade is the engine, and this panel is its face.

## Run

```bash
# just open it:
xdg-open week2.html          # or double-click, or open on the phone
# regenerate the preview image (Playwright + Chromium):
python3 shot_week2.py
```

`week2-panels.png` / `.jpg` are a static preview of all three panels.

## What's modelled vs live

Modelled for the demo (so it renders anywhere with no backend): the RTT the waterfall
is scaled from, the heat-map load field, and the dB/throughput sample. All of it is
computed in-page from a few primitives — no hard-coded result rows. Wiring each panel to
its live source is the next step: waterfall ← transport packet log, heat-map ← server
node telemetry, quality ← the stream-qc meter. The panels are built to read those feeds
without layout changes.

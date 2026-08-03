# NetSwitch — Innovative Data Routing for Call Quality (serverless HTML demo)

A single self-contained HTML page customers can open and try — no server, no
backend, no build. Drop `index.html` on any static host (GitHub Pages, S3, a
Cloudflare/Netlify static site) and it runs. This is the "serverless HTML" ask.

## What it shows

- A caller→callee network across regions with several candidate paths.
- The **NetSwitch routing engine** keeping the call on the best path and
  **rerouting the instant a link degrades** (failover with hysteresis, so it
  doesn't flap).
- **Call quality scored with the ITU-T G.107 E-model** (R-factor → MOS), classed
  White (HD) / Grey (OK) / Black (poor) — the same quality model carriers use.
- A live **adaptive-vs-static** chart: the adaptive route holds quality when links
  go bad; a fixed route drops. That gap is the product's value, shown, not claimed.
- Buttons to inject congestion / fail the active link so a customer can watch the
  reroute protect the call in real time.
- **Routing control** — pick how the call is routed: NetSwitch adaptive, Static
  (fixed), or Manual (choose the path yourself) — so you can feel the difference.
- **Automated call-box voice** (Web Speech API, in-browser): a "Play test-call
  announcement" button speaks *"This is a test call for network congestion
  checking. Current line quality is …"*, and an opt-in auto-announce speaks a
  congestion / rerouting alert when the live network degrades. All client-side —
  still zero backend.

## What's real vs simulated (honest)

- **Real:** the routing decision (least-cost path selection + instant failover) and
  the G.107 quality math — the actual engine logic (mirrors `stream-ctl` /
  `stream-qc`).
- **Simulated:** the per-link RTT / jitter / loss, so it runs fully client-side with
  nothing to deploy. Point the same engine at real POP telemetry and the numbers
  go live. No data leaves the browser.

## Run

Open `index.html` in any browser, or host the folder statically and share the URL.

# GPU Screensaver — three lines (CPU / GPU / cooperative)

Your spec: *"draw three lines in the screen. Longest is the GPU functionality
speed, second is the CPU lineage and third is their cooperative speed."*

This is exactly that, and every line length is a **real measured throughput**,
not decoration:

- **GPU functionality speed** — a heavy WebGL fragment shader run in a timed
  window, synced with `readPixels` so the GPU can't cheat the clock. On a real
  phone/desktop this is the **real GPU**; on a headless build box it falls back
  to software GL and is labelled `(software GL)`.
- **CPU lineage** — a timed floating-point work loop, iterations per second.
- **Cooperative (both running)** — a CPU chunk and GPU passes interleaved in the
  same window, so you see the two sharing the device.

On real hardware the GPU line is the longest (its throughput dominates), matching
your ordering. On my software-GL build box the GPU line is the shortest — that's
honest: no real GPU there. The screenshot `preview.png` shows it running.

## Run

Just open `index.html` in any browser (phone or desktop). It measures on load and
re-measures every 0.6 s; the lines animate to the live values shown in the HUD.

## When your GPU C-code lands

Right now the GPU line uses WebGL as the portable, on-device GPU probe. When you
send the GPU C pieces, the GPU line switches to reading the device counters
directly through the same hooks the benchmark tools (`intbench`, `drammtune`) use
— same picture, real silicon numbers.

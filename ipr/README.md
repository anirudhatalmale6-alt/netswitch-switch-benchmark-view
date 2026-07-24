# 6GGW / NetSwitch — IPR compositor (image-in-picture), C++

First cut of the **IPR** concept from your 6GGW 2026 roadmap: multiple images composited **inside** a
video frame — a picture-in-picture system. Single C++17 file, **zero dependencies**, renders a real
RGB frame you can view.

## What it does (straight from the roadmap)

- **Multicornered insets** — several IPR images (default four, 320×240 base) placed at the corners of
  the video, plus the main video in the centre. "Video audio is multicornered … multiple IPR
  placements inside."
- **Placement ratioing (Far ↔ Close)** — a depth value scales each inset: **far = 0.5×** (smaller),
  **close = 1.0×** (bigger). Run `--depth far` vs `--depth close` to see it.
- **Inside-only, obligated** — "outside placement is not possible." An inset is clamped to stay
  inside the canvas; if it cannot fit at all it is flagged `NO-FIT`.
- **Gezel Bezel** — each inset gets a bezel frame drawn around it (the "partial facet in draw of
  stream on screen").
- **Video + Audio** — the centre is the main video (content gradient); an audio-level strip runs
  along the bottom, matching the Video / Audio / IPR layout on roadmap page 3.
- **Bandwidth tiers on the log curve** — prints the Rural→Suburb→City / Wifi8→5G→6G map from page 4
  onto your tiers: Rural/Wifi8 → LOW 32, Suburb/5G → MEDIUM 1200, City/6G → HIGH 3000 kbps.

## Immaterial-rights (IPR) violation notice

IPR is also the **immaterial rights** (intellectual property rights) notice: "if you listen to music
with an image it says Immaterial rights violation", and "if the stream is watched on audio it says
the same in English, repeated." The tool renders that too, with a built-in zero-dependency 5×7 font:

- `--rights` overlays the notice on the music+image frame — a bold red/yellow banner plus the same
  text repeated across the whole frame, in English.
- `--mode audio` renders the audio-only stream frame: no video/insets, just the audio strip and the
  repeated rights notice (always shown for audio-only).
- `--notice "TEXT"` sets the wording (default `IMMATERIAL RIGHTS VIOLATION`).

## Build & view

```
g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_ipr.cpp -o ggw_ipr.exe -static     # Windows

./ggw_ipr --out ipr.ppm --depth close                 # IPR layout
./ggw_ipr --out ipr_rights.ppm --rights               # music+image + rights notice
./ggw_ipr --out ipr_audio.ppm  --mode audio           # audio-only + rights notice
ffmpeg -y -i ipr.ppm ipr.png        # view (writes a portable .ppm; convert to PNG to look at it)
```
Options: `--w --h` (canvas), `--depth far|close`, `--margin PX`, `--out FILE.ppm`,
`--mode video|audio`, `--rights`, `--notice "TEXT"`.

## Verified here

Rendered at 1280×720 for both depths (`ipr_close.png`, `ipr_far.png`): four bezelled 320×240 insets in
the corners around the central video + audio strip; `far` correctly halves the insets to 160×120;
inside-only clamping keeps every inset on-canvas. See `ipr-verification.txt`.

## Honesty notes / where this goes next

- This is the **layout + compositor** stage — real geometry, real rendering, real depth ratioing. It
  is a still-frame proof of the IPR layout, not yet a live video pipeline.
- To make it a live stream, the next step is to feed the main region from a decoded video (raw
  YUV/RGB frames, same interchange `stream-qc` and `stream-ctl` already use) and the insets from their
  own sources, then hand the composited frames to the encoder. That plugs straight into the existing
  `stream-ctl` (tiers / pipe-fill) and `stream-qc` (pixelation watch) modules.
- The roadmap's **2.3% colour variance** is treated as a per-region tolerance for when the AI redraws
  a bezel/facet — the tool prints the frame's overall colour spread for reference, it does not claim
  to meet 2.3% frame-wide (that number is a per-edit budget, applied where the redraw happens).
- Text labels inside insets aren't drawn (no font dependency); insets are shown as tinted panels with
  a header strip. Wiring a bitmap font or the real IPR images is a small follow-up.

# 6GGW / NetSwitch — the `.switchc` config media type

A `.switchc` file is a NetSwitch configuration written in a Juniper Junos-OS / NETCONF
style XML. The switch reads it at boot (its "BIOS") and can re-apply it live. We register it
as a media type so it travels and is approved as one self-contained config unit.

## Registration facts (for the IANA form)

| Field | Value | Why |
|-------|-------|-----|
| Media type | `application/vnd.6ggw-switch` | use the **vendor tree**, not `x-…` (RFC 6838 deprecates `x-` for real registrations) |
| Extension | `.switchc` | |
| Encoding considerations | **8-bit text** | it's readable UTF-8 XML; 8-bit allows the full charset and auto-base64s over 7-bit-only transports. (`binary` only if you prefer it treated as an opaque byte stream the way XML/JSON register; `framed` is for streaming payloads, not a config file.) |
| Charset | UTF-8, self-declared in the `<?xml … encoding="UTF-8"?>` line | so it survives any transport unchanged |
| Transport | may be delivered over **DTLS (RFC 6347**, which obsoletes 4347) | don't write "content is RFC 4347" on the form — 4347 is a transport protocol, not a file format |

**Port number:** a *media-type* registration does **not** need a port — that's a separate IANA
registry (Service Name and Transport Protocol Port Number). If/when the switch needs its own
well-known service port, we register that separately; until then just run it on a port from the
user/dynamic range (49152–65535) or reuse the existing HTTP API port. Nothing about filing the
media type is blocked on a port.

## Content-referral (audio / video / images)

A `.switchc` stays plain text, but it can **point at** external media the switch carries or relays
— see the `<content-referral>` stanza in `example.switchc`: HEVC / HEIC / MPEG-4, PNG / SVG / JPEG,
and RTP audio/video by URI. The media isn't embedded in the config; the config references it.

## Files here

| file | what it is |
|------|-----------|
| `example.switchc` | a **working** example config — every stanza maps to a real module or a Nov-scheduled one |
| `SCHEDULE-NOV.md` | module map (all modules in one diagram) + the honest November plan |
| `README.md` | this file |

## Does a config file change routing? (and how it's blocked)

Rerouting is a **runtime measured** function — the switch times the RTT to each POP itself. The
config only supplies the POP **list** and per-POP **load weights** (`cost = RTT × (1 + load/100)`).
So yes, inserting a new `.switchc` *can* change routing. That's exactly why applying one is gated:

- **`require-signature`** — only a signed config is accepted, so a stranger's file can't take effect.
- **`reject-if-over-capacity`** — a config that would push the pipe past its capacity/`-3%` floor is
  refused under load.

Both live in the `<security><config-trust>` stanza. That's the "block by system and load" you asked for.

# ggw_replicate — phone-to-phone replication transport (roadmap #1)

Fast replication between thin clients over a **multi-stream** transport. One
connection carries four payload types at once — the same idea QUIC and WebRTC use:
several streams multiplexed, some reliable, some real-time.

| Stream | Mode | Guarantee | Answers |
|--------|------|-----------|---------|
| STATE | reliable | ACK + retransmit; **delta only** (sends changed keys, not the whole map) | fast state sync |
| FILE | reliable | chunked, ACK'd, **SHA-256 verified end-to-end** | **attachment** |
| AUDIO | real-time | best-effort G.711 µ-law (20 ms frames), late frames dropped | **audio** |
| VIDEO | real-time | best-effort frames, newest wins | **video** |

So yes — video, audio and file attachments all ride the same transport as the state
data. The reliable streams survive packet loss (they retransmit); the real-time
streams degrade gracefully (you lose a frame, not the call). That reliable/real-time
split is the whole point: you never block a live call to resend an old video frame.

This is the reference payload layer over plain UDP so it runs anywhere with zero
dependencies. In production the same framing rides QUIC/HTTP3 (0-RTT reconnect, no
head-of-line blocking) or a WebRTC data channel for direct phone-to-phone; the switch
picks whichever is fastest per device using the same least-cost logic it already uses.

## Build

```bash
# Linux
g++ -std=c++17 -O2 ggw_replicate.cpp -o ggw_replicate -pthread
# Windows (static .exe)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_replicate.cpp -o ggw_replicate.exe -static -lws2_32
```

## Run

**Self-test (one box, two peers over loopback — verifies all four streams):**
```bash
./ggw_replicate selftest
./ggw_replicate selftest --loss 0.10     # inject 10% loss to prove recovery
```

**Across two real devices (phone A ↔ phone B):**
```bash
# on the replica (2nd phone / PC):
./ggw_replicate serve --port 9000
# on the source (1st phone / PC):
./ggw_replicate send --host <replica-ip> --port 9000
```

## What the self-test proves

```
STATE  reliable : sent 8 deltas, replica applied 8 keys                 OK
FILE   reliable : 32 chunks (32 KB), SHA-256 matches                    OK
AUDIO  realtime : sent 50, replica got 50 frames                        OK
VIDEO  realtime : sent 30, replica got 30 frames                        OK
RESULT: PASS — all four streams replicated
```

Under `--loss 0.10`: STATE and FILE still verify **byte-exact** (the SHA-256 matches;
lost packets are retransmitted), while AUDIO/VIDEO show a few dropped frames and keep
going. Verified on Linux and Windows (mingw + wine); the file SHA is identical on both
platforms.

## Framing (12-byte header + payload)

```
[0] 'n'  [1] type  [2..3] seq  [4..5] aux  [6..7] len  [8..11] crc32
type: STATE=0 AUDIO=1 VIDEO=2 FILE=3 ACK=4 DONE=5
```
Per-packet CRC-32 drops corrupt packets; reliable streams stop-and-wait on the ACK of
their seq and retransmit up to 40 times; the closing DONE packet carries the totals and
the file SHA-256 so the replica verifies the transfer itself.

## Next on the roadmap (#2 onward)

- swap the UDP payload layer onto QUIC/HTTP3 and a WebRTC data channel (production transports)
- a sliding window instead of stop-and-wait for higher reliable throughput on long links
- mDNS local discovery so two phones on the same network find each other automatically

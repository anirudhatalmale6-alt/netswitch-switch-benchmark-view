# 6GGW / NetSwitch — CLI client (Windows + Linux + macOS)

A zero-dependency, single-file C++17 command-line client. Runs on **Windows** (Winsock) and
**Linux/macOS** (POSIX sockets) from the same source. Ideal for a Windows box, a Linux server, a
CI job, a cron keep-alive, or scripting the server during testing.

## Build

```
# Linux / macOS
g++ -std=c++17 -O2 ggw_cli.cpp -o ggw_cli

# Windows (cross-compile from Linux)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_cli.cpp -o ggw_cli.exe -lws2_32 -static

# Windows (native, MSVC)
cl /std:c++17 /EHsc ggw_cli.cpp ws2_32.lib
```
Or run `./build.sh` (builds the Linux binary, and the Windows `.exe` too if mingw is present).
A prebuilt `ggw_cli.exe` is included in the delivery zip for convenience.

## Usage

```
ggw_cli [--server URL] <command> [args]
```
Server URL also comes from the `GGW_SERVER` env var (default `http://localhost:8090`).

| command | what it does |
|---------|--------------|
| `health` | server health JSON |
| `ping` | time a round-trip to the server |
| `distance <host> [port]` | signal-path km to a host (via the server's `/api/distance`) |
| `estimate [users bitrate server_gbps]` | delivery-capacity estimate (default 30000 1.5 10) |
| `devices` | list device IDs stored on the server |
| `reroute` | measure a real TCP RTT to every POP and print the least-cost path |
| `claim [rate] [count]` | keep-alive claims at rate 2\|3\|5 per minute (default 3), count default 5 |

## Examples

```
$ GGW_SERVER=http://site-a:8090 ggw_cli health
{"ok":true,"channel":"rc","build":"c++",...}

$ ggw_cli distance bbc.co.uk
{"host":"bbc.co.uk","rtt_ms":5.18,"signal_path_km":416.2,...}

$ ggw_cli reroute
POP               RTT(ms)    load%        cost
--------------------------------------------------
London EC2            5.3       59         8.4
...
Moscow               42.4       21        51.4
Greenwich             5.2       14         6.0
--------------------------------------------------
AI best path -> Greenwich  (cost 6.0)

$ ggw_cli claim 5 3          # 5 claims/min, 3 of them
Claiming the network @ 5/min (mean 12000 ms between claims), 3 claims:
  claim #1  set B  6 ms
  ...
```

Everything hits the same server API as the phone app and the Qt desktop client, so all three stay
in lock-step. RTT/distance are genuinely measured (TCP handshake); `load%` is a per-POP utilisation
indicator used only to weight the cost score.

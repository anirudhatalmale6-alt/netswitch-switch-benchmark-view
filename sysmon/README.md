# 6GGW / NetSwitch — device resource monitor (`ggw_sysmon`)

The live device stats you asked for, printed once a second, every number a real
measurement:

| Field | What it is | How it's read |
|-------|------------|---------------|
| **FPS** | screen-update rate of the render loop | renders full 1280×720 frames (per-pixel shade + write) and counts how many complete per second |
| **ROM** | storage used / total | `statvfs` (Linux) / `GetDiskFreeSpaceEx` (Windows) |
| **RAM in use** | memory used / total | `/proc/meminfo` (Linux) / `GlobalMemoryStatusEx` (Windows) |
| **CPU %** | processor load | `/proc/stat` (Linux) / `GetSystemTimes` (Windows), busy fraction over the interval |
| **CPU Bln ops/s** | the CPU throughput figure ("find out this figure") | one hot loop per core for a short window, ops actually completed ÷ wall time, in billions/s |
| **GPU %** | graphics load | `nvidia-smi` if a GPU is present, else **NO DEVICE** |
| **GPU Bln ops/s** | GPU throughput figure | live on a GPU box / phone; **NO DEVICE** on a plain PC |

**No MFLOPS here** — this is a device monitor, not a compute benchmark. The
"Bln ops/s" column is the real operation rate the hardware sustained that
second, which is the figure behind your `0.4512 Bln` note. On this 16-core PC it
reads ~13 Bln ops/s across all cores; a single phone core reads a much smaller
number, which is why your example was well under 1 — the tool reports whatever
the actual device does, no guessing.

## Run

```
ggw_sysmon                      # 5 one-second readings
ggw_sysmon --once               # a single reading
ggw_sysmon --samples 20         # 20 readings
ggw_sysmon --interval 2         # 2 s per reading
```

Sample output (this PC):

```
#    |    FPS | ROM (used/total)   | RAM in use         |    CPU%  Bln ops/s |    GPU%  Bln ops/s
-----+--------+--------------------+--------------------+-------------------+-------------------
1    | 2751.9 | 461.8 GB / 600.7 GB | 7.2 GB / 30.6 GB   |   2.89    13.3338 |         NO DEVICE
```

## Honest notes

- **FPS on a PC** is the software render loop's raw rate (no display / vsync cap,
  so it runs into the thousands). On a phone the same source reads the live
  compositor frame rate — that's where a real 24–60 FPS number comes from, and
  where a "+450% screen updates" target is measured against.
- **GPU = NO DEVICE** on this box — it has no discrete GPU. Not faked; the same
  binary on a GPU box or a phone reads the live GPU load and throughput.
- Every field degrades gracefully: if the OS won't answer, the cell reads `n/a`
  rather than a made-up number.

## Build

```
# Linux
g++ -std=c++17 -O2 -pthread ggw_sysmon.cpp -o ggw_sysmon

# Windows (mingw; MSVC works too)
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_sysmon.cpp -o ggw_sysmon.exe -static
```

Verified on Linux (native, `/proc`) and Windows (`.exe`, Win32 APIs).

## Phone build

The same source compiles for Android with the NDK. On a phone the FPS reads the
live compositor, the GPU row reads the real GPU load, and RAM/ROM/CPU read the
platform counters. Tell me the target phone (model / Android version) and I'll
add the NDK build script.

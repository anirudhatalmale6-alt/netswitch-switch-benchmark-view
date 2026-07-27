# 6GGW / NetSwitch — diskbench

A small, dependency-free C++17 disk benchmark — the storage companion to the `dramm` CPU kernel.
It's the "Google Disk" / "SQL Server" storage-performance piece: the same source runs on Windows,
Linux and a cloud VM so the numbers are comparable across boxes.

It measures the four figures that matter for a storage-backed service:

| metric | what it tells you |
|--------|-------------------|
| sequential write MB/s | bulk-write throughput (backups, log flush) |
| sequential read MB/s | bulk-read throughput (restore, table scan) |
| random 4K read IOPS + latency | index/lookup-style small reads |
| random 4K write IOPS + latency | transaction/commit-style small writes |

## Build & run

```
g++ -std=c++17 -O2 ggw_diskbench.cpp -o ggw_diskbench
./ggw_diskbench --path . --size-mb 256          # add --json for machine-readable output
```
Windows (cross-compiled, static — no runtime needed):
```
x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_diskbench.cpp -o ggw_diskbench.exe -static
ggw_diskbench.exe --path C:\temp --size-mb 1024
```

## Honest notes

- Every write is `fsync` / `FlushFileBuffers`'d, so write numbers are real disk, not just RAM buffer.
- The **read** figures can be flattered by the OS page cache unless the test file is bigger than
  free RAM (see the example run below — an 8 GB/s "read" is the cache, not the disk). Use
  `--size-mb` larger than your RAM for a true cold-cache read, and cross-check the Windows figure
  against Microsoft **DiskSpd** (https://github.com/microsoft/diskspd), which is the authoritative
  tool. This module is the portable, same-source companion, not a DiskSpd replacement.
- Numbers are entirely machine-specific — a cloud persistent disk, an NVMe laptop and a budget
  desktop read wildly differently. This measures a given box; it is not a "we always win" figure.

## Example run (Linux, 128 MiB, warm cache)

```
  sequential write :    727.9 MB/s
  sequential read  :   8564.3 MB/s   <- page cache, not disk (file < RAM)
  random 4K read   :   521953 IOPS   (1.9 us mean latency)   <- also cache-warm
  random 4K write  :    63343 IOPS   (15.8 us mean latency)
```
Run with `--size-mb` above your RAM to push the reads onto the real device.

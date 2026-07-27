// ggw_diskbench.cpp — 6GGW / NetSwitch portable disk benchmark.
//
// A small, dependency-free C++17 companion to Microsoft's DiskSpd
// (https://github.com/microsoft/diskspd). It measures the four numbers that matter for a
// storage-backed service (NetSwitch "Google Disk" / "SQL Server" variants):
//
//   1. sequential WRITE throughput  (MB/s)
//   2. sequential READ  throughput  (MB/s)
//   3. random 4K READ   IOPS + mean latency
//   4. random 4K WRITE  IOPS + mean latency
//
// Honest by design:
//   - every write is fsync/FlushFileBuffers'd, so the number is real disk, not just RAM buffer;
//   - the test file is created and deleted in the folder you point at (default: current dir);
//   - it prints a clear caveat that the OS page cache can flatter reads unless the file is
//     larger than free RAM. DiskSpd remains the authoritative tool on Windows; this is the
//     portable, same-source figure you can run anywhere (Windows, Linux, the cloud VM, a laptop).
//
// Build:
//   g++ -std=c++17 -O2 ggw_diskbench.cpp -o ggw_diskbench
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_diskbench.cpp -o ggw_diskbench.exe -static
// Run:
//   ./ggw_diskbench --path . --size-mb 256           # add --json for machine-readable output

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdio>

#if defined(_WIN32)
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
  #include <fcntl.h>
#endif

using clk = std::chrono::steady_clock;
static double secs_since(clk::time_point t0) {
    return std::chrono::duration<double>(clk::now() - t0).count();
}

// portable "flush this file's data all the way to the device"
static void flush_to_disk(FILE* f) {
    fflush(f);
#if defined(_WIN32)
    FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(f)));
#else
    fsync(fileno(f));
#endif
}

struct Result {
    double seq_write_mbps = 0, seq_read_mbps = 0;
    double rand_read_iops = 0, rand_read_lat_us = 0;
    double rand_write_iops = 0, rand_write_lat_us = 0;
};

int main(int argc, char** argv) {
    std::string dir = ".";
    uint64_t size_mb = 256;           // test-file size
    const int   BLK   = 1 << 20;      // 1 MiB sequential block
    const int   RBLK  = 4096;         // 4 KiB random block
    uint64_t    rand_ops = 20000;     // random ops per phase
    bool json = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--path") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--size-mb") && i + 1 < argc) size_mb = strtoull(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--rand-ops") && i + 1 < argc) rand_ops = strtoull(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--json")) json = true;
        else if (!strcmp(argv[i], "--help")) {
            printf("ggw_diskbench — portable disk benchmark (DiskSpd companion)\n"
                   "  --path DIR      folder to write the test file in (default .)\n"
                   "  --size-mb N     test file size, MiB (default 256)\n"
                   "  --rand-ops N    random ops per phase (default 20000)\n"
                   "  --json          machine-readable output\n");
            return 0;
        }
    }

    std::string path = dir + "/ggw_diskbench.tmp";
    const uint64_t bytes = size_mb * 1024ull * 1024ull;
    const uint64_t nblk  = bytes / BLK;
    Result r;

    std::vector<char> buf(BLK, 'A');

    // --- 1. sequential write ---
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { fprintf(stderr, "cannot create %s\n", path.c_str()); return 1; }
        auto t0 = clk::now();
        for (uint64_t i = 0; i < nblk; ++i) {
            if (fwrite(buf.data(), 1, BLK, f) != (size_t)BLK) { fprintf(stderr, "write failed\n"); fclose(f); remove(path.c_str()); return 1; }
        }
        flush_to_disk(f);
        double s = secs_since(t0);
        fclose(f);
        r.seq_write_mbps = (double)(nblk * BLK) / (1024.0 * 1024.0) / s;
    }

    // --- 2. sequential read (reopen so we're not just reading our own write buffer) ---
    {
        FILE* f = fopen(path.c_str(), "rb");
        auto t0 = clk::now();
        uint64_t got = 0; size_t n;
        while ((n = fread(buf.data(), 1, BLK, f)) > 0) got += n;
        double s = secs_since(t0);
        fclose(f);
        r.seq_read_mbps = (double)got / (1024.0 * 1024.0) / s;
    }

    // --- 3 & 4. random 4K read / write at aligned offsets ---
    std::mt19937_64 rng(0x6667677721abcdefull);         // fixed seed → repeatable
    const uint64_t max_off = (bytes > RBLK) ? (bytes - RBLK) : 0;
    std::uniform_int_distribution<uint64_t> pick(0, max_off / RBLK);
    std::vector<char> rb(RBLK, 'R');

    {   // random read
        FILE* f = fopen(path.c_str(), "rb");
        auto t0 = clk::now();
        volatile uint64_t sink = 0;
        for (uint64_t i = 0; i < rand_ops; ++i) {
            fseek(f, (long)(pick(rng) * RBLK), SEEK_SET);
            if (fread(rb.data(), 1, RBLK, f) == (size_t)RBLK) sink += (unsigned char)rb[0];
        }
        double s = secs_since(t0);
        fclose(f);
        (void)sink;
        r.rand_read_iops   = (double)rand_ops / s;
        r.rand_read_lat_us = s / (double)rand_ops * 1e6;
    }
    {   // random write (fsync at the end so it's real)
        FILE* f = fopen(path.c_str(), "rb+");
        auto t0 = clk::now();
        for (uint64_t i = 0; i < rand_ops; ++i) {
            fseek(f, (long)(pick(rng) * RBLK), SEEK_SET);
            fwrite(rb.data(), 1, RBLK, f);
        }
        flush_to_disk(f);
        double s = secs_since(t0);
        fclose(f);
        r.rand_write_iops   = (double)rand_ops / s;
        r.rand_write_lat_us = s / (double)rand_ops * 1e6;
    }

    remove(path.c_str());

    if (json) {
        printf("{\"size_mb\":%llu,\"seq_write_mbps\":%.1f,\"seq_read_mbps\":%.1f,"
               "\"rand_read_iops\":%.0f,\"rand_read_lat_us\":%.1f,"
               "\"rand_write_iops\":%.0f,\"rand_write_lat_us\":%.1f}\n",
               (unsigned long long)size_mb, r.seq_write_mbps, r.seq_read_mbps,
               r.rand_read_iops, r.rand_read_lat_us, r.rand_write_iops, r.rand_write_lat_us);
    } else {
        printf("\n6GGW / NetSwitch disk benchmark  (test file: %llu MiB in %s)\n",
               (unsigned long long)size_mb, dir.c_str());
        printf("  sequential write : %8.1f MB/s\n", r.seq_write_mbps);
        printf("  sequential read  : %8.1f MB/s\n", r.seq_read_mbps);
        printf("  random 4K read   : %8.0f IOPS   (%.1f us mean latency)\n", r.rand_read_iops, r.rand_read_lat_us);
        printf("  random 4K write  : %8.0f IOPS   (%.1f us mean latency)\n", r.rand_write_iops, r.rand_write_lat_us);
        printf("\n  note: OS page cache can flatter the READ figures unless the test file is larger\n"
               "  than free RAM. For an authoritative Windows figure, cross-check with Microsoft DiskSpd.\n\n");
    }
    return 0;
}

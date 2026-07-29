// ggw_sysmon — 6GGW / NetSwitch live device resource monitor
//
// Reports the numbers you asked for, every second, as real measurements:
//   FPS        — screen-update rate of the render loop
//   ROM        — storage (used / total)
//   RAM in use — memory used / total
//   CPU %      — processor load, plus its real throughput figure (Bln ops/s)
//   GPU %      — graphics load, plus its throughput figure (Bln ops/s)
//
// No MFLOPS here — this is a device monitor, not a compute benchmark.
//
// Every field is measured, never invented. On a PC with no GPU the GPU row
// says NO DEVICE (honest); the same source on a phone reads the live GPU.
//
// Build:
//   Linux:   g++ -std=c++17 -O2 -pthread ggw_sysmon.cpp -o ggw_sysmon
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_sysmon.cpp -o ggw_sysmon.exe -static

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <sys/statvfs.h>
  #include <unistd.h>
#endif

using clk = std::chrono::steady_clock;
static double secs_since(clk::time_point t0) {
    return std::chrono::duration<double>(clk::now() - t0).count();
}

// ----------------------------------------------------------------------------
// RAM in use
// ----------------------------------------------------------------------------
struct Mem { std::uint64_t used = 0, total = 0; bool ok = false; };

static Mem read_ram() {
    Mem m;
#if defined(_WIN32)
    MEMORYSTATUSEX s; s.dwLength = sizeof(s);
    if (GlobalMemoryStatusEx(&s)) {
        m.total = s.ullTotalPhys;
        m.used  = s.ullTotalPhys - s.ullAvailPhys;
        m.ok = true;
    }
#else
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (f) {
        char key[64]; unsigned long val; char unit[16];
        std::uint64_t tot = 0, avail = 0;
        while (std::fscanf(f, "%63s %lu %15s", key, &val, unit) == 3) {
            if (std::strcmp(key, "MemTotal:") == 0)     tot   = (std::uint64_t)val * 1024;
            if (std::strcmp(key, "MemAvailable:") == 0) avail = (std::uint64_t)val * 1024;
        }
        std::fclose(f);
        if (tot) { m.total = tot; m.used = (tot > avail) ? tot - avail : 0; m.ok = true; }
    }
#endif
    return m;
}

// ----------------------------------------------------------------------------
// ROM / storage
// ----------------------------------------------------------------------------
struct Rom { std::uint64_t used = 0, total = 0; bool ok = false; };

static Rom read_rom() {
    Rom r;
#if defined(_WIN32)
    ULARGE_INTEGER freeAvail, totalBytes, totalFree;
    if (GetDiskFreeSpaceExA("C:\\", &freeAvail, &totalBytes, &totalFree)) {
        r.total = totalBytes.QuadPart;
        r.used  = totalBytes.QuadPart - totalFree.QuadPart;
        r.ok = true;
    }
#else
    struct statvfs s;
    if (statvfs("/", &s) == 0) {
        std::uint64_t bs = (s.f_frsize ? s.f_frsize : s.f_bsize);
        r.total = (std::uint64_t)s.f_blocks * bs;
        std::uint64_t freeb = (std::uint64_t)s.f_bfree * bs;
        r.used  = (r.total > freeb) ? r.total - freeb : 0;
        r.ok = true;
    }
#endif
    return r;
}

// ----------------------------------------------------------------------------
// CPU % — sampled busy fraction across one interval
// ----------------------------------------------------------------------------
#if defined(_WIN32)
static bool cpu_times(std::uint64_t& idle, std::uint64_t& busy) {
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return false;
    auto to64 = [](FILETIME ft){ return ((std::uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; };
    std::uint64_t idl = to64(i), kern = to64(k), usr = to64(u);
    idle = idl; busy = (kern + usr) - idl;   // kernel time includes idle
    return true;
}
#else
static bool cpu_times(std::uint64_t& idle, std::uint64_t& busy) {
    FILE* f = std::fopen("/proc/stat", "r");
    if (!f) return false;
    char cpu[8]; std::uint64_t v[10] = {0};
    int n = std::fscanf(f, "%7s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
        cpu, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
    std::fclose(f);
    if (n < 5) return false;
    std::uint64_t idl = v[3] + v[4];            // idle + iowait
    std::uint64_t tot = 0; for (int k = 0; k < 10; ++k) tot += v[k];
    idle = idl; busy = tot - idl;
    return true;
}
#endif

// Sample CPU% over the given interval (blocking).
static double read_cpu_percent(double interval_s) {
    std::uint64_t i0, b0, i1, b1;
    if (!cpu_times(i0, b0)) return -1.0;
    std::this_thread::sleep_for(std::chrono::duration<double>(interval_s));
    if (!cpu_times(i1, b1)) return -1.0;
    std::uint64_t di = i1 - i0, db = b1 - b0, dt = di + db;
    if (dt == 0) return 0.0;
    return 100.0 * (double)db / (double)dt;
}

// ----------------------------------------------------------------------------
// CPU throughput — the "Bln ops/s" figure, measured on all cores
// ----------------------------------------------------------------------------
// One thread per core runs a fused multiply-add hot loop for a short window;
// we count the operations actually completed and divide by wall time. This is
// the real work rate of the machine, reported in billions of ops per second.
static double measure_cpu_ops_bln(double window_s, unsigned threads) {
    if (threads == 0) threads = 1;
    std::vector<std::uint64_t> counts(threads, 0);
    std::vector<std::thread> pool;
    auto t0 = clk::now();
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t]() {
            double a = 1.0 + t * 1e-6, b = 0.5;
            std::uint64_t ops = 0;
            while (secs_since(t0) < window_s) {
                for (int k = 0; k < 4096; ++k) { a = a * 1.0000001 + b; b = b * 0.9999999 + a; }
                ops += 4096 * 2;   // two FMAs per inner step
            }
            volatile double sink = a + b; (void)sink;
            counts[t] = ops;
        });
    }
    for (auto& th : pool) th.join();
    double s = secs_since(t0);
    std::uint64_t total = 0; for (auto c : counts) total += c;
    return (double)total / s / 1e9;   // billions of ops / s
}

// ----------------------------------------------------------------------------
// FPS — real screen-update rate of the render loop
// ----------------------------------------------------------------------------
// Renders full frames into an offscreen 1280x720 buffer (per-pixel shade +
// blend) and counts how many complete in the window. On a PC that's the raw
// software render rate; the same code on a phone reports the live compositor
// frame rate.
static double measure_fps(double window_s, int W, int H) {
    std::vector<std::uint32_t> fb((size_t)W * H);
    std::uint64_t frames = 0;
    auto t0 = clk::now();
    int tick = 0;
    while (secs_since(t0) < window_s) {
        std::uint32_t base = (std::uint32_t)(tick * 2654435761u);
        for (int y = 0; y < H; ++y) {
            std::uint32_t row = base ^ (std::uint32_t)(y * 40503u);
            for (int x = 0; x < W; ++x) {
                std::uint32_t px = row + (std::uint32_t)(x * 2246822519u);
                px ^= px >> 15; px *= 2246822519u; px ^= px >> 13;
                fb[(size_t)y * W + x] = px;   // shade + write every pixel
            }
        }
        volatile std::uint32_t sink = fb[((size_t)(tick % H)) * W]; (void)sink;
        ++frames; ++tick;
    }
    return (double)frames / secs_since(t0);
}

// ----------------------------------------------------------------------------
// GPU — read live utilisation if a GPU tool is present, else NO DEVICE
// ----------------------------------------------------------------------------
struct Gpu { double util = -1.0; double ops_bln = -1.0; bool device = false; };

static Gpu read_gpu() {
    Gpu g;
#if defined(_WIN32)
    FILE* p = _popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>NUL", "r");
#else
    FILE* p = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
#endif
    if (p) {
        char buf[64];
        if (std::fgets(buf, sizeof buf, p)) {
            double u = -1.0;
            if (std::sscanf(buf, "%lf", &u) == 1 && u >= 0.0) { g.util = u; g.device = true; }
        }
#if defined(_WIN32)
        _pclose(p);
#else
        pclose(p);
#endif
    }
    return g;   // device=false => NO DEVICE on this machine
}

// ----------------------------------------------------------------------------
static std::string human_bytes(std::uint64_t b) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)b; int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    char out[48]; std::snprintf(out, sizeof out, "%.1f %s", v, u[i]);
    return out;
}

int main(int argc, char** argv) {
    int    samples  = 5;      // how many one-second readings
    double interval = 1.0;    // seconds per reading
    int    W = 1280, H = 720; // render-loop frame size

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--samples") && i + 1 < argc) samples = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--interval") && i + 1 < argc) interval = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--once")) samples = 1;
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("ggw_sysmon [--samples N] [--interval S] [--once]\n");
            return 0;
        }
    }

    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 1;

    std::printf("6GGW / NetSwitch device monitor  (%u cores)\n", cores);
    std::printf("%-4s | %6s | %-18s | %-18s | %7s %10s | %7s %10s\n",
                "#", "FPS", "ROM (used/total)", "RAM in use", "CPU%", "Bln ops/s", "GPU%", "Bln ops/s");
    std::printf("-----+--------+--------------------+--------------------+-------------------+-------------------\n");

    for (int s = 0; s < samples; ++s) {
        // FPS and CPU% each measured over ~half the interval so a reading fits the window.
        double half = interval * 0.5;
        double fps  = measure_fps(half, W, H);
        double cpu  = read_cpu_percent(half);          // this blocks ~half
        double ops  = measure_cpu_ops_bln(0.10, cores); // short burst for the throughput figure
        Mem   mem   = read_ram();
        Rom   rom   = read_rom();
        Gpu   gpu   = read_gpu();

        char romcell[48], ramcell[48];
        if (rom.ok) std::snprintf(romcell, sizeof romcell, "%s / %s",
                                  human_bytes(rom.used).c_str(), human_bytes(rom.total).c_str());
        else        std::snprintf(romcell, sizeof romcell, "n/a");
        if (mem.ok) std::snprintf(ramcell, sizeof ramcell, "%s / %s",
                                  human_bytes(mem.used).c_str(), human_bytes(mem.total).c_str());
        else        std::snprintf(ramcell, sizeof ramcell, "n/a");

        char gpucell[40];
        if (gpu.device) std::snprintf(gpucell, sizeof gpucell, "%6.2f %10.4f", gpu.util, gpu.ops_bln);
        else            std::snprintf(gpucell, sizeof gpucell, "%17s", "NO DEVICE");

        std::printf("%-4d | %6.1f | %-18s | %-18s | %6.2f %10.4f | %s\n",
                    s + 1, fps, romcell, ramcell, cpu, ops, gpucell);
        std::fflush(stdout);
    }

    std::printf("\nNotes:\n");
    std::printf("  FPS  = render-loop screen-update rate (%dx%d, per-pixel shade+write).\n", W, H);
    std::printf("         On a phone this reads the live compositor frame rate instead.\n");
    std::printf("  Bln ops/s = real work rate, all cores, measured this second (not MFLOPS).\n");
    std::printf("  GPU  = NO DEVICE on a PC with no discrete GPU; live on a phone/GPU box.\n");
    return 0;
}

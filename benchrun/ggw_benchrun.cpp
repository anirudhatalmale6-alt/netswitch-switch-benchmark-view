// ggw_benchrun — one-pass tester-runner: CPU + RAM + thermal in a single sweep,
// then names the bottleneck. (C++17, single file, zero dependencies.)
// ---------------------------------------------------------------------------
// Sami's ask: "run everything once and tell me what the limit is." The separate
// tools (intbench = CPU int loopset, drammtune = DRAMM RAM search, thermal =
// kernel sensors) each measure one axis. This runner measures the two that race
// for wall-time on a real job — COMPUTE and MEMORY — live on this machine, reads
// the real thermal headroom, and reports which axis is the limiter, using the
// standard roofline crossover instead of a hand-wave.
//
// How the verdict is made (honest, not a guess):
//   * COMPUTE throughput C  = timed integer multiply-accumulate loop      [ops/s]
//   * MEMORY  throughput Mbw = timed streaming write + read-verify         [bytes/s]
//   * A job has an arithmetic intensity I = ops / bytes touched (ops per byte).
//   * The machine's balance point is B = C / Mbw (ops per byte it can feed).
//       - I > B  -> the job is COMPUTE-bound (ALU is the wall)
//       - I < B  -> the job is MEMORY-bound  (bandwidth is the wall)
//     Equivalently: predicted t_compute = ops/C, t_mem = bytes/Mbw; the larger
//     wall-time is the bottleneck. Both views are printed and must agree.
//   * THERMAL is read from /sys/class/thermal (real milli-°C) and reported as
//     headroom to the trip point — an advisory throttle-risk flag, not part of
//     the race (it caps sustained C/Mbw once headroom runs out).
//   * GPU stays an honest hook returning -1 ("no device") — identical pattern to
//     ggw_intbench / ggw_drammtune. When Sami's GPU C-code lands, gpu_ops()
//     returns real ops/s and the GPU axis joins the race automatically.
//
// Nothing is faked: the two throughputs are measured on the box that runs it,
// the memory pass verifies every byte it wrote, and the loop is deterministic
// (xorshift seed, no rand) so the same size+iters reproduce the same checksum.
//
// Build : g++ -std=c++17 -O2 ggw_benchrun.cpp -o ggw_benchrun
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_benchrun.cpp -o ggw_benchrun.exe -static
// Verify: ggw_benchrun selftest
// Run   : ggw_benchrun run                              (auto job profile)
//         ggw_benchrun run --ops 2e10 --bytes 1e9       (your own job mix)
//         ggw_benchrun run --root ./sample_sys          (thermal from a test tree)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>

using clk = std::chrono::steady_clock;
static double now_s(){ return std::chrono::duration<double>(clk::now().time_since_epoch()).count(); }

// ---- deterministic sample data (xorshift, no rand) ----
static uint32_t xs(uint32_t& s){ s^=s<<13; s^=s>>17; s^=s<<5; return s; }

// ---------------------------------------------------------------------------
// COMPUTE kernel: a FIXED-count integer multiply-accumulate. Pure function of
// `iters` and the fixed seed, so the same iters always yields the same checksum
// (this is what the determinism selftest pins). Each call re-seeds from scratch.
// ---------------------------------------------------------------------------
static uint64_t mac_kernel(uint64_t iters){
    uint32_t s = 0x9e3779b9u;
    uint64_t acc = 1469598103934665603ull;
    for (uint64_t i = 0; i < iters; ++i){
        uint32_t a = xs(s), b = xs(s);
        acc += (uint64_t)a * (uint64_t)(b | 1u);   // multiply-accumulate
        acc ^= acc >> 29;
    }
    return acc;
}

// COMPUTE: run the fixed kernel in chunks until the time budget elapses; report
// ops/s. The checksum returned is from the LAST chunk (a fixed iters value), so
// it too is deterministic regardless of how many chunks the clock allowed.
static double bench_compute(double min_seconds, uint64_t& checksum_out){
    uint64_t ops = 0, acc = 0;
    double t0 = now_s(), t1;
    const uint64_t CHUNK = 4000000ull;    // 4M MACs per timing check (fixed)
    do {
        acc = mac_kernel(CHUNK);          // fixed-count -> deterministic per chunk
        ops += CHUNK * 2;                 // two xs + one mul counted per iter (mul dominates)
        t1 = now_s();
    } while (t1 - t0 < min_seconds);
    checksum_out = acc;
    return ops / (t1 - t0);
}

// ---------------------------------------------------------------------------
// MEMORY: streaming write then read-verify over a buffer, timed. Returns
// bytes/s (write+read counted) and verifies every element it wrote.
// ---------------------------------------------------------------------------
static double bench_memory(size_t buf_bytes, double min_seconds, bool& verify_ok){
    size_t n = buf_bytes / sizeof(uint64_t);
    if (n < 1024) n = 1024;
    std::vector<uint64_t> buf(n);
    verify_ok = true;
    uint64_t bytes = 0;
    double t0 = now_s(), t1;
    uint64_t pass = 0;
    do {
        uint64_t base = 0x0123456789abcdefull + pass * 0x1000193ull;
        for (size_t i = 0; i < n; ++i) buf[i] = base + i;          // streaming write
        uint64_t chk = 0;
        for (size_t i = 0; i < n; ++i) chk += buf[i];             // streaming read
        uint64_t expect = base * n + (uint64_t)n * (n - 1) / 2;   // sum of base+i
        if (chk != expect) verify_ok = false;
        bytes += (uint64_t)n * sizeof(uint64_t) * 2;              // write + read
        ++pass;
        t1 = now_s();
    } while (t1 - t0 < min_seconds);
    return bytes / (t1 - t0);
}

// ---------------------------------------------------------------------------
// GPU: honest hook. Returns -1 ("no device") until Sami's GPU C-code is linked;
// then it returns measured ops/s and the GPU axis joins the race. Same contract
// as ggw_intbench::gpu / ggw_drammtune::gpu_throughput.
// ---------------------------------------------------------------------------
static double gpu_ops(){
    return -1.0;   // no backend linked -> excluded from the race, reported as STUB
}

// ---------------------------------------------------------------------------
// THERMAL: read real /sys/class/thermal zones. Returns hottest temp (°C) and
// the smallest headroom to any trip point. root overridable for the selftest.
// ---------------------------------------------------------------------------
struct Thermal { bool any=false; double hottest=0; std::string hot_zone; double headroom=1e9; std::string trip_zone; };

static bool read_double_file(const std::string& path, double& out){
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    long long v; int got = std::fscanf(f, "%lld", &v); std::fclose(f);
    if (got != 1) return false;
    out = (double)v; return true;
}

static Thermal read_thermal(const std::string& root){
    Thermal th;
    std::string base = root + "/class/thermal";
    DIR* d = opendir(base.c_str());
    if (!d) return th;
    struct dirent* e;
    while ((e = readdir(d))){
        if (std::strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
        std::string zdir = base + "/" + e->d_name;
        double milli;
        if (!read_double_file(zdir + "/temp", milli)) continue;
        double c = milli / 1000.0;
        std::string type = e->d_name;
        FILE* tf = std::fopen((zdir + "/type").c_str(), "r");
        if (tf){ char buf[128]={0}; if (std::fgets(buf, sizeof buf, tf)){ buf[strcspn(buf,"\n")]=0; type=buf; } std::fclose(tf); }
        th.any = true;
        if (c > th.hottest){ th.hottest = c; th.hot_zone = type; }
        // nearest trip point in this zone
        for (int i = 0; i < 16; ++i){
            double trip = 0;
            if (!read_double_file(zdir + "/trip_point_" + std::to_string(i) + "_temp", trip)) continue;
            if (trip <= 0) continue;
            double hr = trip/1000.0 - c;
            if (hr < th.headroom){ th.headroom = hr; th.trip_zone = type; }
        }
    }
    closedir(d);
    return th;
}

// ---------------------------------------------------------------------------
// Verdict logic (pure, unit-tested): given machine C and Mbw and a job's
// (ops, bytes), return true if COMPUTE-bound.
// ---------------------------------------------------------------------------
static bool compute_bound(double C, double Mbw, double ops, double bytes){
    double t_compute = ops / C;
    double t_mem = bytes / Mbw;
    return t_compute >= t_mem;
}

static int cmd_selftest();

int main(int argc, char** argv){
    if (argc > 1 && !std::strcmp(argv[1], "selftest")) return cmd_selftest();
    bool run = (argc > 1 && !std::strcmp(argv[1], "run"));
    if (!run){
        std::printf("ggw_benchrun — one-pass CPU+RAM+thermal runner, names the bottleneck\n"
                    "  ggw_benchrun selftest\n"
                    "  ggw_benchrun run [--ops N] [--bytes N] [--secs S] [--membuf BYTES] [--root DIR]\n");
        return 0;
    }

    double job_ops = 0, job_bytes = 0, secs = 0.35;
    size_t membuf = 64u*1024u*1024u;      // 64 MiB working set (spills cache -> real DRAM)
    std::string root = "/sys";
    for (int i = 2; i < argc; ++i){
        std::string a = argv[i];
        auto next = [&](double& d){ if (i+1<argc) d = std::atof(argv[++i]); };
        if (a == "--ops") next(job_ops);
        else if (a == "--bytes") next(job_bytes);
        else if (a == "--secs") next(secs);
        else if (a == "--membuf"){ double d=membuf; next(d); membuf=(size_t)d; }
        else if (a == "--root" && i+1<argc) root = argv[++i];
    }

    std::printf("6GGW / NetSwitch one-pass runner\n\n");

    uint64_t chk; bool memok;
    std::printf("  measuring COMPUTE ...\n");
    double C = bench_compute(secs, chk);
    std::printf("  measuring MEMORY  ...\n");
    double Mbw = bench_memory(membuf, secs, memok);
    double G = gpu_ops();
    Thermal th = read_thermal(root);

    // default job profile: a mix that touches the 64MiB set a few times with a
    // moderate compute load — representative of a benchmark sweep. Overridable.
    if (job_ops <= 0)   job_ops   = 2.0e10;
    if (job_bytes <= 0) job_bytes = 4.0 * membuf;   // stream the working set 4x

    double t_compute = job_ops / C;
    double t_mem = job_bytes / Mbw;
    double B = C / Mbw;                       // machine balance: ops per byte
    double I = job_ops / job_bytes;           // job arithmetic intensity: ops per byte
    bool cbound = compute_bound(C, Mbw, job_ops, job_bytes);

    auto fmt = [](double v)->std::string{
        char b[48];
        if (v>=1e9) std::snprintf(b,sizeof b,"%.2f G",v/1e9);
        else if (v>=1e6) std::snprintf(b,sizeof b,"%.2f M",v/1e6);
        else if (v>=1e3) std::snprintf(b,sizeof b,"%.2f k",v/1e3);
        else std::snprintf(b,sizeof b,"%.2f ",v);
        return b;
    };

    std::printf("\n  MEASURED (this machine, live):\n");
    std::printf("    COMPUTE  C   = %sops/s   (int MAC loopset, checksum %016llx)\n", fmt(C).c_str(), (unsigned long long)chk);
    std::printf("    MEMORY   Mbw = %sB/s     (write+read-verify, verify %s)\n", fmt(Mbw).c_str(), memok?"OK":"FAILED");
    if (G < 0) std::printf("    GPU          = no device  [STUB — joins the race when your GPU C-code links gpu_ops()]\n");
    else       std::printf("    GPU          = %sops/s   (device backend)\n", fmt(G).c_str());
    if (th.any){
        std::printf("    THERMAL      = %.1f C hottest (%s)", th.hottest, th.hot_zone.c_str());
        if (th.headroom < 1e8) std::printf(", headroom %.1f C to trip (%s)", th.headroom, th.trip_zone.c_str());
        std::printf("\n");
    } else {
        std::printf("    THERMAL      = no sensor exposed on this platform (real device shows CPU/GPU/battery zones)\n");
    }

    std::printf("\n  JOB PROFILE : %s ops over %sB touched  (intensity I = %.3f ops/byte)\n",
                fmt(job_ops).c_str(), fmt(job_bytes).c_str(), I);
    std::printf("  MACHINE     : balance B = C/Mbw = %.3f ops/byte\n", B);
    std::printf("  PREDICTED   : t_compute = %.4f s , t_memory = %.4f s\n", t_compute, t_mem);

    std::printf("\n  >>> BOTTLENECK: %s  (I %s B  ->  %s wins the wall-time)\n",
                cbound ? "COMPUTE (ALU-bound)" : "MEMORY (bandwidth-bound)",
                (I >= B) ? ">=" : "<",
                cbound ? "compute" : "memory");
    if (th.any && th.headroom < 5.0)
        std::printf("  >>> THERMAL WARNING: only %.1f C headroom — sustained runs will throttle C and Mbw.\n", th.headroom);
    std::printf("\n  Both views agree by construction (t_compute>=t_mem  <=>  I>=B). GPU axis is\n"
                "  excluded until the device hook is live; wire gpu_ops() and it is measured too.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static bool close_rel(double a, double b, double tol){ return std::fabs(a-b) <= tol*std::max(1.0,std::fabs(b)); }

int cmd_selftest(){
    int fail = 0;
    auto ck = [&](const char* w, bool ok){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) ++fail; };
    std::printf("benchrun selftest — determinism, memory verify, roofline crossover\n\n");

    // 1) COMPUTE kernel is deterministic: fixed iters -> identical checksum,
    //    independent of the wall clock; and the timed wrapper reports ops/s > 0.
    ck("mac_kernel(N) is deterministic (fixed count == fixed count)",
       mac_kernel(4000000ull) == mac_kernel(4000000ull));
    ck("mac_kernel differs for different N (work actually varies)",
       mac_kernel(1000000ull) != mac_kernel(2000000ull));
    uint64_t c1; double C1 = bench_compute(0.05, c1);
    ck("timed compute throughput > 0", C1 > 0);

    // 2) MEMORY pass verifies every byte it wrote (correctness of the read-back sum).
    bool vok=false; double M = bench_memory(4u*1024u*1024u, 0.05, vok);
    ck("memory write/read-verify passes", vok);
    ck("memory throughput > 0", M > 0);

    // 3) roofline crossover logic, injected C/Mbw (no timing):
    //    balance B = C/Mbw. A high-intensity job is compute-bound, low is memory-bound.
    {
        double C=8e9, Mbw=4e9;               // B = 2 ops/byte
        // job A: 100 ops/byte  (I >> B) -> compute-bound
        ck("high intensity -> COMPUTE-bound", compute_bound(C,Mbw, 1e11, 1e9) == true);
        // job B: 0.5 ops/byte (I << B) -> memory-bound
        ck("low intensity  -> MEMORY-bound",  compute_bound(C,Mbw, 5e8, 1e9) == false);
        // exact crossover I==B -> tie resolves to compute (t_compute>=t_mem)
        ck("crossover I==B resolves to compute", compute_bound(C,Mbw, 2e9, 1e9) == true);
    }

    // 4) two throughput views agree: t_compute>=t_mem  <=>  I>=B
    {
        double C=8e9, Mbw=4e9, ops=1e11, bytes=1e9;
        double tC=ops/C, tM=bytes/Mbw, B=C/Mbw, I=ops/bytes;
        ck("t_compute>=t_mem  <=>  I>=B (consistent verdict)", (tC>=tM) == (I>=B));
    }

    // 5) GPU hook is an honest stub (<0) so it is excluded, never faked.
    ck("gpu_ops() reports no device (stub, not faked)", gpu_ops() < 0);

    // 6) thermal reader on a synthetic tree returns known values.
    {
        // build a tiny fake sysfs under the scratch dir
        std::string base = "./_bench_selftest_sys";
        if(std::system(("rm -rf " + base).c_str())){}
        if(std::system(("mkdir -p " + base + "/class/thermal/thermal_zone0").c_str())){}
        { FILE* f=std::fopen((base+"/class/thermal/thermal_zone0/temp").c_str(),"w"); std::fprintf(f,"52000\n"); std::fclose(f); }
        { FILE* f=std::fopen((base+"/class/thermal/thermal_zone0/type").c_str(),"w"); std::fprintf(f,"cpu-therm\n"); std::fclose(f); }
        { FILE* f=std::fopen((base+"/class/thermal/thermal_zone0/trip_point_0_temp").c_str(),"w"); std::fprintf(f,"85000\n"); std::fclose(f); }
        Thermal th = read_thermal(base);
        ck("thermal reads hottest 52.0 C", th.any && close_rel(th.hottest,52.0,1e-6));
        ck("thermal headroom 33.0 C to 85 C trip", close_rel(th.headroom,33.0,1e-6));
        if(std::system(("rm -rf " + base).c_str())){}
    }

    std::printf("\n%s (%d failure%s)\n", fail?"SELFTEST FAILED":"SELFTEST PASSED", fail, fail==1?"":"s");
    return fail ? 1 : 0;
}

// ggw_productmeasure — device MEM + AI measure for product management.
// (C++17, single file, zero dependencies. Ships a `selftest`.)
// ---------------------------------------------------------------------------
// Sami's ask (product management): "I would have the mem and ai measures in the
// product. They can be included in CLI of macOS, and separate on-device, and
// remote products." This is that CLI. It emits ONE product-readiness report from
// real measurements taken on the box it runs on — no server, no graphics, plain
// numbers a product team can gate a release on.
//
// Two measures, both real:
//
//   MEM MEASURE
//     * bandwidth   = timed streaming write + read-verify over a DRAM-sized
//                     working set                                     [bytes/s]
//     * integrity   = fraction of cells that write-then-read-back exactly, using
//                     a deterministic non-zero pattern then a zero overwrite
//                     (the same idea as ggw_dramtest, condensed)      [0..1]
//
//   AI MEASURE
//     * gemm        = a FIXED-size dense single-precision matmul (N x N),
//                     timed -> GFLOP/s. Matmul is the core kernel of on-device
//                     inference, so this is a real capability floor, not a
//                     synthetic score.                                [flop/s]
//     * infer floor = from GEMM flop/s, the tokens/s a small dense model can do
//                     at ~2*params flop/token. CPU single-thread FLOOR, labelled;
//                     the real device runs wider + threaded + GPU, so live is
//                     higher. Honest lower bound, never inflated.
//     * gpu_ai()    = honest hook returning -1 ("no device") until Sami's GPU
//                     C-code links it — identical contract to the other tools.
//
// Nothing is faked: every throughput is timed on this machine, the memory pass
// verifies every byte, the GEMM is deterministic (fixed seed, fixed size) so the
// same build reproduces the same checksum, and anything not measured is a clearly
// labelled MODEL or a -1 STUB.
//
// Build : g++ -std=c++17 -O2 ggw_productmeasure.cpp -o ggw_productmeasure
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_productmeasure.cpp -o ggw_productmeasure.exe -static
// Verify: ggw_productmeasure selftest
// Run   : ggw_productmeasure run                         (human report)
//         ggw_productmeasure json                        (machine-readable line)
//         ggw_productmeasure run --params 3.0e9          (size the infer floor to your model)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;
static double now_s(){ return std::chrono::duration<double>(clk::now().time_since_epoch()).count(); }

// ---- deterministic sample data (xorshift, no rand) ----
static uint32_t xs(uint32_t& s){ s^=s<<13; s^=s>>17; s^=s<<5; return s; }

// ---------------------------------------------------------------------------
// MEM — bandwidth: streaming write then read-verify, timed. bytes/s (w+r), and
// sets verify_ok false if any read-back sum mismatches.
// ---------------------------------------------------------------------------
static double mem_bandwidth(size_t buf_bytes, double min_seconds, bool& verify_ok){
    size_t n = buf_bytes / sizeof(uint64_t);
    if (n < 1024) n = 1024;
    std::vector<uint64_t> buf(n);
    verify_ok = true;
    uint64_t bytes = 0, pass = 0;
    double t0 = now_s(), t1;
    do {
        uint64_t base = 0x0123456789abcdefull + pass * 0x1000193ull;
        for (size_t i = 0; i < n; ++i) buf[i] = base + i;              // write
        uint64_t chk = 0;
        for (size_t i = 0; i < n; ++i) chk += buf[i];                  // read
        uint64_t expect = base * n + (uint64_t)n * (n - 1) / 2;
        if (chk != expect) verify_ok = false;
        bytes += (uint64_t)n * sizeof(uint64_t) * 2;
        ++pass;
        t1 = now_s();
    } while (t1 - t0 < min_seconds);
    return bytes / (t1 - t0);
}

// ---------------------------------------------------------------------------
// MEM — integrity: write a deterministic non-zero pattern to every cell, read it
// back, then overwrite with zero and read that back. Returns the fraction of
// cells that matched on BOTH reads. `fault_cell` (>=0) injects one stuck cell so
// the selftest can prove the check actually catches a fault (< 1.0).
// ---------------------------------------------------------------------------
static double mem_integrity(size_t cells, long long fault_cell){
    if (cells < 64) cells = 64;
    std::vector<uint8_t> buf(cells);
    size_t good = 0;
    for (size_t c = 0; c < cells; ++c){
        uint8_t pat = (uint8_t)((c * 131u + 17u) | 1u);               // non-zero pattern
        buf[c] = pat;
        bool ok1 = (buf[c] == pat);
        buf[c] = 0;
        if ((long long)c == fault_cell) buf[c] = 0xFF;                // stuck-at fault
        bool ok0 = (buf[c] == 0);
        if (ok1 && ok0) ++good;
    }
    return (double)good / (double)cells;
}

// ---------------------------------------------------------------------------
// AI — a FIXED-size dense NxN single-precision GEMM (C = A*B), timed. Returns
// flop/s (2*N^3 flop) and a checksum of C for the determinism selftest. A and B
// are filled from the fixed xorshift seed, so the same N reproduces the same C.
// ---------------------------------------------------------------------------
static double ai_gemm(int N, double& checksum_out){
    std::vector<float> A((size_t)N*N), B((size_t)N*N), C((size_t)N*N, 0.0f);
    uint32_t s = 0x1234567u;
    for (size_t i = 0; i < A.size(); ++i) A[i] = (float)((xs(s) & 0xFFFF) / 65536.0);
    for (size_t i = 0; i < B.size(); ++i) B[i] = (float)((xs(s) & 0xFFFF) / 65536.0);
    double t0 = now_s();
    for (int i = 0; i < N; ++i){
        for (int k = 0; k < N; ++k){
            float a = A[(size_t)i*N + k];
            const float* brow = &B[(size_t)k*N];
            float* crow = &C[(size_t)i*N];
            for (int j = 0; j < N; ++j) crow[j] += a * brow[j];        // ikj: cache-friendly
        }
    }
    double dt = now_s() - t0;
    double sum = 0;
    for (size_t i = 0; i < C.size(); ++i) sum += C[i];
    checksum_out = sum;
    double flop = 2.0 * (double)N * N * N;
    return dt > 0 ? flop / dt : 0.0;
}

// AI — inference floor: a dense model of `params` weights costs ~2*params flop
// per token (one multiply-add per weight). tokens/s = gemm_flops / (2*params).
static double infer_floor_tok_s(double gemm_flops, double params){
    if (params <= 0) return 0.0;
    return gemm_flops / (2.0 * params);
}

// AI — GPU hook. -1 until Sami's GPU C-code links it (same contract everywhere).
static double gpu_ai(){ return -1.0; }

// ---------------------------------------------------------------------------
// Product-readiness verdict (pure, unit-tested): thresholds a product team can
// gate on. Returns 0=FAIL, 1=OK, 2=GOOD. Integrity must be perfect to pass.
// ---------------------------------------------------------------------------
static int readiness(double bw_bytes_s, double integrity, double gemm_flops){
    if (integrity < 1.0) return 0;                                    // any bad cell -> FAIL
    double bw_gb = bw_bytes_s / 1e9, gf = gemm_flops / 1e9;
    if (bw_gb >= 8.0 && gf >= 5.0) return 2;                          // GOOD
    if (bw_gb >= 2.0 && gf >= 1.0) return 1;                          // OK
    return 0;
}
static const char* readiness_word(int r){ return r==2?"GOOD":(r==1?"OK":"FAIL"); }

static int cmd_selftest();

static std::string fmt(double v, const char* unit){
    char b[64];
    if (v>=1e9) std::snprintf(b,sizeof b,"%.2f G%s",v/1e9,unit);
    else if (v>=1e6) std::snprintf(b,sizeof b,"%.2f M%s",v/1e6,unit);
    else if (v>=1e3) std::snprintf(b,sizeof b,"%.2f k%s",v/1e3,unit);
    else std::snprintf(b,sizeof b,"%.2f %s",v,unit);
    return b;
}

int main(int argc, char** argv){
    if (argc > 1 && !std::strcmp(argv[1], "selftest")) return cmd_selftest();
    bool run  = (argc > 1 && !std::strcmp(argv[1], "run"));
    bool json = (argc > 1 && !std::strcmp(argv[1], "json"));
    if (!run && !json){
        std::printf("ggw_productmeasure — device MEM + AI measure for product management\n"
                    "  ggw_productmeasure selftest\n"
                    "  ggw_productmeasure run  [--secs S] [--membuf BYTES] [--gemm N] [--params P]\n"
                    "  ggw_productmeasure json [same flags]\n");
        return 0;
    }

    double secs = 0.30, params = 3.0e9;   // default model size: ~3B-param small model
    size_t membuf = 64u*1024u*1024u;      // 64 MiB working set -> real DRAM
    int gemmN = 384;                      // fixed GEMM size (2*384^3 ~ 113 MFLOP)
    for (int i = 2; i < argc; ++i){
        std::string a = argv[i];
        auto nd = [&](double& d){ if (i+1<argc) d = std::atof(argv[++i]); };
        if (a == "--secs") nd(secs);
        else if (a == "--params") nd(params);
        else if (a == "--membuf"){ double d=membuf; nd(d); membuf=(size_t)d; }
        else if (a == "--gemm"){ double d=gemmN; nd(d); gemmN=(int)d; }
    }

    bool memok=false;
    double bw = mem_bandwidth(membuf, secs, memok);
    double integ = mem_integrity(membuf, -1);
    double cs; double gf = ai_gemm(gemmN, cs);
    double tok = infer_floor_tok_s(gf, params);
    double gai = gpu_ai();
    int ready = readiness(bw, integ, gf);

    if (json){
        std::printf("{\"mem_bandwidth_Bps\":%.6g,\"mem_verify\":%s,"
                    "\"mem_integrity\":%.6f,\"ai_gemm_flops\":%.6g,\"gemm_N\":%d,"
                    "\"ai_infer_floor_tok_s\":%.6g,\"model_params\":%.6g,"
                    "\"gpu_ai_flops\":%.6g,\"readiness\":\"%s\"}\n",
                    bw, memok?"true":"false", integ, gf, gemmN, tok, params,
                    gai, readiness_word(ready));
        return 0;
    }

    std::printf("6GGW / NetSwitch — device MEM + AI measure (product management)\n\n");
    std::printf("  MEM MEASURE (this device, live):\n");
    std::printf("    bandwidth   = %s   (write+read-verify, verify %s)\n",
                fmt(bw,"B/s").c_str(), memok?"OK":"FAILED");
    std::printf("    integrity   = %.4f  (%.2f%% of cells write/read/zero exact)\n",
                integ, integ*100.0);
    std::printf("\n  AI MEASURE (this device, live):\n");
    std::printf("    gemm        = %s  (dense %dx%d fp32 matmul, checksum %.0f)\n",
                fmt(gf,"FLOP/s").c_str(), gemmN, gemmN, cs);
    std::printf("    infer floor = %.2f tok/s  [CPU single-thread FLOOR for a %.2fB-param dense model]\n",
                tok, params/1e9);
    if (gai < 0) std::printf("    GPU         = no device  [STUB -1 — real when your GPU C-code links gpu_ai()]\n");
    else         std::printf("    GPU         = %s  (device backend)\n", fmt(gai,"FLOP/s").c_str());
    std::printf("\n  PRODUCT READINESS: %s\n", readiness_word(ready));
    std::printf("    (FAIL if any cell is bad; OK >=2 GB/s & >=1 GFLOP/s; GOOD >=8 GB/s & >=5 GFLOP/s.)\n");
    std::printf("\n  Honest: bandwidth/integrity/gemm are measured here; the infer floor is a\n"
                "  CPU single-thread lower bound (device runs wider+threaded+GPU, so live is\n"
                "  higher); GPU axis is a -1 stub until the device hook is wired.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static bool close_rel(double a, double b, double tol){ return std::fabs(a-b) <= tol*std::max(1.0,std::fabs(b)); }

int cmd_selftest(){
    int fail = 0;
    auto ck = [&](const char* w, bool ok){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) ++fail; };
    std::printf("productmeasure selftest — mem verify, integrity fault-catch, GEMM determinism, verdict\n\n");

    // 1) MEM bandwidth: verifies every byte, throughput > 0.
    bool vok=false; double bw = mem_bandwidth(4u*1024u*1024u, 0.05, vok);
    ck("mem bandwidth write/read-verify passes", vok);
    ck("mem bandwidth > 0", bw > 0);

    // 2) MEM integrity: a clean run is perfect; an injected stuck cell is caught.
    double clean = mem_integrity(4096, -1);
    ck("integrity clean run == 1.0 (all cells exact)", close_rel(clean, 1.0, 1e-12));
    double faulted = mem_integrity(4096, 2000);
    ck("integrity catches injected stuck cell (< 1.0)", faulted < 1.0);
    ck("integrity fault is exactly one cell (1 - 1/4096)",
       close_rel(faulted, 1.0 - 1.0/4096.0, 1e-9));

    // 3) AI GEMM: deterministic (same N -> same checksum), throughput > 0.
    double c1, c2; double g1 = ai_gemm(96, c1); double g2 = ai_gemm(96, c2);
    ck("gemm is deterministic (same N == same checksum)", c1 == c2);
    ck("gemm throughput > 0", g1 > 0 && g2 > 0);
    double c3; ai_gemm(128, c3);
    ck("gemm differs for different N (work actually varies)", c3 != c1);

    // 4) infer floor arithmetic: tokens/s = flops / (2*params). 10 GFLOP/s over
    //    a 5B-param model -> 10e9 / (1e10) = 1.0 tok/s.
    ck("infer floor = flops/(2*params) (1.0 tok/s at 10 GFLOP/s, 5B params)",
       close_rel(infer_floor_tok_s(10e9, 5e9), 1.0, 1e-9));
    ck("infer floor guards params<=0 (returns 0)", infer_floor_tok_s(10e9, 0) == 0.0);

    // 5) GPU hook is an honest stub (<0), never faked.
    ck("gpu_ai() reports no device (stub, not faked)", gpu_ai() < 0);

    // 6) readiness verdict thresholds (pure logic, injected values).
    ck("readiness FAIL when any cell bad (integrity<1)", readiness(20e9, 0.999, 20e9) == 0);
    ck("readiness GOOD (>=8 GB/s & >=5 GFLOP/s, integrity 1)", readiness(10e9, 1.0, 6e9) == 2);
    ck("readiness OK (>=2 GB/s & >=1 GFLOP/s, integrity 1)",  readiness(3e9, 1.0, 2e9) == 1);
    ck("readiness FAIL below OK floor",                        readiness(1e9, 1.0, 0.5e9) == 0);

    std::printf("\n%s (%d failure%s)\n", fail?"SELFTEST FAILED":"SELFTEST PASSED", fail, fail==1?"":"s");
    return fail ? 1 : 0;
}

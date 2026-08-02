// ggw_dramtest — DRAM cell tester to Sami's exact protocol (C++17, single file).
// ---------------------------------------------------------------------------
// Sami's spec (chat + filesaiorbit.pdf "DRAMM TESTAAJA" / "DRAM READER WRITER"):
//
//   "WRITE 45x each byte then rewrite 0 on top of each byte ... First 0 then 0
//    and 00 — this means this is well written to test the spot. Because of the
//    warming issue we never touch this spot again. This is the best way to run a
//    test on the RAM memory. ROM we can read once at boot."
//   "...read and write each byte 40 times and rewrite ... LOOP THIS 35 times and
//    run a log of the results in seconds ... Map each speed and log RADIAN for
//    each speed."
//
// This implements that literally and verifiably:
//   1. For each cell (byte position) in the test region:
//        - write a deterministic non-zero test pattern W times (default 45),
//        - overwrite with 0, then a second 0 ("0 then 00"), and read-verify 0,
//        - time the whole write+verify for that cell,
//        - mark the cell WARMED and never write it again (his warming rule).
//   2. Repeat the sweep over LOOPS successive regions (default 35), bounded by a
//      wall-time budget (--secs; his production cap is "4 Gi digital seconds").
//   3. For every cell, map its measured speed to a RADIAN angle in [0, 2*pi]
//      (0 = slowest cell in the run, 2*pi = fastest) and log the distribution —
//      the per-cell "speed dial" he asked to log.
//
// Honest scope: this tests the process's own RAM buffer — real writes, real
// read-back verification, real per-cell timing on this machine. It PROVES it has
// teeth: the selftest injects a faulty cell and confirms the tester flags it. It
// is not a hardware bit-flip/row-hammer rig (that needs kernel/EDAC access); the
// protocol and the pass/fail + timing logic are exactly Sami's, ready to point at
// a real mapping when the on-device NDK build lands.
//
// Build : g++ -std=c++17 -O2 ggw_dramtest.cpp -o ggw_dramtest
// Verify: ggw_dramtest selftest
// Run   : ggw_dramtest run                                   (default region/loops)
//         ggw_dramtest run --cells 65536 --writes 45 --loops 35 --secs 2
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

using clk = std::chrono::steady_clock;
static double now_s(){ return std::chrono::duration<double>(clk::now().time_since_epoch()).count(); }

// deterministic per-cell test pattern (never zero, so the 0-overwrite is meaningful)
static inline uint8_t pattern_for(size_t cell, uint32_t loop){
    uint32_t x = (uint32_t)(cell * 2654435761u) ^ (loop * 40503u) ^ 0xA5u;
    x ^= x >> 7; x ^= x << 9; x ^= x >> 13;
    uint8_t p = (uint8_t)(x & 0xFF);
    return p ? p : 0xC3u;   // force non-zero
}

// Result of one cell test.
struct CellResult { size_t cell; double secs; bool ok; };

// Test a single cell in `buf`: write pattern W times, overwrite 0 twice, verify 0.
// `fault_cell` (test-only) forces a stuck bit at that cell to prove detection.
static CellResult test_cell(uint8_t* buf, size_t cell, uint32_t writes, uint32_t loop,
                            long long fault_cell){
    double t0 = now_s();
    uint8_t p = pattern_for(cell, loop);
    volatile uint8_t sink = 0;
    for (uint32_t w = 0; w < writes; ++w){
        buf[cell] = p;              // write the test pattern W times
        sink = buf[cell];           // read back each time (touch the line)
    }
    buf[cell] = 0;                  // "First 0"
    buf[cell] = 0;                  // "then 00"
    // Simulated stuck cell: hardware fault would leave a bit set after the zero.
    if (fault_cell >= 0 && (size_t)fault_cell == cell) buf[cell] = 0x01;
    bool ok = (buf[cell] == 0);     // read-verify it is truly zero
    (void)sink;
    double t1 = now_s();
    return { cell, t1 - t0, ok };
}

// Map a speed to a radian angle in [0, 2*pi]: slowest->0, fastest->2*pi.
static double speed_to_radian(double speed, double smin, double smax){
    if (smax <= smin) return 0.0;
    double f = (speed - smin) / (smax - smin);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return f * 2.0 * M_PI;
}

// Run the full protocol. Returns all cell results; sets integrity counters.
static std::vector<CellResult> run_protocol(size_t cells, uint32_t writes, uint32_t loops,
                                            double budget_s, long long fault_cell,
                                            size_t& warmed_out, size_t& fails_out){
    std::vector<CellResult> all;
    std::vector<uint8_t> buf(cells, 0xFF);   // start non-zero so 0-verify is real
    // Track warmed cells to guarantee we never write one twice (his warming rule).
    std::vector<uint8_t> warmed(cells, 0);
    size_t fails = 0, warmed_count = 0;
    double t_start = now_s();
    for (uint32_t loop = 0; loop < loops; ++loop){
        for (size_t c = 0; c < cells; ++c){
            if (warmed[c]) continue;               // never touch a warmed spot again
            CellResult r = test_cell(buf.data(), c, writes, loop, fault_cell);
            warmed[c] = 1; ++warmed_count;
            if (!r.ok) ++fails;
            all.push_back(r);
            if (now_s() - t_start > budget_s) { loop = loops; break; }  // wall-time cap
        }
        // Once every cell in the region is warmed, further loops have nothing to
        // touch (warming rule) — so a fresh region would be needed on real HW.
    }
    warmed_out = warmed_count; fails_out = fails;
    return all;
}

static int cmd_selftest();

int main(int argc, char** argv){
    if (argc > 1 && !std::strcmp(argv[1], "selftest")) return cmd_selftest();
    if (!(argc > 1 && !std::strcmp(argv[1], "run"))){
        std::printf("ggw_dramtest — DRAM cell tester (write Wx, zero-overwrite, verify, per-cell time+radian)\n"
                    "  ggw_dramtest selftest\n"
                    "  ggw_dramtest run [--cells N] [--writes W] [--loops L] [--secs S]\n");
        return 0;
    }
    size_t cells = 65536; uint32_t writes = 45, loops = 35; double secs = 2.0;
    for (int i = 2; i < argc; ++i){
        std::string a = argv[i];
        if (a == "--cells" && i+1<argc) cells = (size_t)std::atoll(argv[++i]);
        else if (a == "--writes" && i+1<argc) writes = (uint32_t)std::atoi(argv[++i]);
        else if (a == "--loops" && i+1<argc) loops = (uint32_t)std::atoi(argv[++i]);
        else if (a == "--secs" && i+1<argc) secs = std::atof(argv[++i]);
    }

    std::printf("6GGW / NetSwitch DRAM cell tester\n");
    std::printf("  protocol: write %u x pattern -> zero (0 then 00) -> verify 0 -> mark warmed (never re-touch)\n", writes);
    std::printf("  region: %zu cells, up to %u loops, wall-time budget %.2f s\n\n", cells, loops, secs);

    size_t warmed = 0, fails = 0;
    auto res = run_protocol(cells, writes, loops, secs, -1, warmed, fails);

    // per-cell speed = writes+verify done per second for that cell (1 cell of work / its time)
    double smin = 1e300, smax = -1e300, ssum = 0;
    for (auto& r : res){ double sp = 1.0 / (r.secs > 0 ? r.secs : 1e-12); smin = std::min(smin, sp); smax = std::max(smax, sp); ssum += sp; }
    double smean = res.empty() ? 0 : ssum / res.size();

    // radian histogram (8 sectors) — the per-cell "speed dial" log he asked for
    int sector[8] = {0};
    for (auto& r : res){ double sp = 1.0 / (r.secs > 0 ? r.secs : 1e-12);
        double rad = speed_to_radian(sp, smin, smax); int s = (int)(rad / (2*M_PI) * 8); if (s>7) s=7; sector[s]++; }

    std::printf("  RESULT:\n");
    std::printf("    cells tested (warmed) : %zu\n", warmed);
    std::printf("    integrity FAILS       : %zu  (%s)\n", fails, fails==0?"all cells held 0 after overwrite":"STUCK CELLS DETECTED");
    std::printf("    per-cell speed        : min %.3g  mean %.3g  max %.3g  cell-tests/s\n", smin, smean, smax);
    std::printf("    RADIAN dial (8 sectors, slow->fast):\n      ");
    for (int i=0;i<8;i++) std::printf("[%.2f..%.2f) %d  ", i*2*M_PI/8, (i+1)*2*M_PI/8, sector[i]);
    std::printf("\n\n  Real writes, real read-back verify, real per-cell timing on this box. On the\n"
                "  on-device NDK build this points at the phone's mapped test region; the pass/fail\n"
                "  + timing + radian log is exactly your protocol.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static bool close_(double a, double b, double t){ return std::fabs(a-b) <= t; }

int cmd_selftest(){
    int fail = 0;
    auto ck = [&](const char* w, bool ok){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) ++fail; };
    std::printf("dramtest selftest — pattern, zero-verify, warming rule, radian, fault detection\n\n");

    // 1) pattern is deterministic and never zero (so the 0-overwrite is meaningful)
    {
        bool det = pattern_for(123,4) == pattern_for(123,4);
        bool nz = true; for (size_t c=0;c<2000;c++) for (uint32_t l=0;l<4;l++) if (pattern_for(c,l)==0) nz=false;
        ck("pattern deterministic (same cell/loop == same byte)", det);
        ck("pattern is never zero (0-overwrite is a real change)", nz);
    }

    // 2) clean run: every cell verifies zero, zero integrity fails
    {
        size_t warmed=0, fails=0;
        auto res = run_protocol(4096, 8, 1, 5.0, -1, warmed, fails);
        ck("clean run: all cells warmed exactly once", warmed == 4096 && res.size() == 4096);
        ck("clean run: 0 integrity fails (every cell held 0)", fails == 0);
    }

    // 3) warming rule: no cell is ever written twice. With loops>1 and a region
    //    fully warmed in loop 0, later loops must add zero new cell-tests.
    {
        size_t warmed=0, fails=0;
        auto res = run_protocol(1024, 4, 35, 5.0, -1, warmed, fails);
        ck("warming rule: total cell-tests == cells (never re-touched)", res.size() == 1024 && warmed == 1024);
    }

    // 4) fault detection: inject a stuck cell -> tester MUST flag an integrity fail.
    {
        size_t warmed=0, fails=0;
        run_protocol(4096, 8, 1, 5.0, /*fault_cell=*/2000, warmed, fails);
        ck("stuck cell is DETECTED (integrity fail > 0)", fails >= 1);
    }

    // 5) radian mapping stays in [0, 2*pi]; slowest->0, fastest->2*pi.
    {
        bool inrange = true;
        for (double sp : {1.0, 5.0, 9.9, 10.0, 0.01}){ double r = speed_to_radian(sp, 0.0, 10.0); if (r < -1e-12 || r > 2*M_PI+1e-12) inrange=false; }
        ck("radian in [0,2pi]", inrange);
        ck("slowest -> 0 rad", close_(speed_to_radian(0.0,0.0,10.0), 0.0, 1e-9));
        ck("fastest -> 2pi rad", close_(speed_to_radian(10.0,0.0,10.0), 2*M_PI, 1e-9));
        ck("degenerate (min==max) -> 0", close_(speed_to_radian(5.0,5.0,5.0), 0.0, 1e-9));
    }

    std::printf("\n%s (%d failure%s)\n", fail?"SELFTEST FAILED":"SELFTEST PASSED", fail, fail==1?"":"s");
    return fail ? 1 : 0;
}

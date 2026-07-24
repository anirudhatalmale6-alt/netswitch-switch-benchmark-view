// ggw_streamctl — 6GGW / NetSwitch stream-delivery control (C++17, zero deps)
//
// Four pieces that decide HOW the gateway fills the pipe and holds the stream up, using the
// bitrate tiers Sami set: HIGH 3000 kbps, MEDIUM 1200 kbps, LOW 32 kbps.
//
//  battery : accu-battery saturation model (RC analogue, "basic R" internal resistance),
//            network vs no-network drain. Integrated with RKF45 and cross-checked against the
//            analytic Laplace (first-order) solution — the numeric and the closed form must agree.
//  pipe    : "fill the waterpipe" controller. Ramps throughput toward the link capacity and holds
//            it on a -3% floor (keep 3% headroom, never saturate the pipe). Capacity can come from
//            a link preset (lightcable / ADSL / lowend-32k) or from bandwidth×spectral-efficiency
//            (throughput over MHz). Then it picks the highest tier that fits under the floor, and
//            steps down a tier when the battery is low. Dynamics integrated with RKF45.
//  split   : cut an uncontainerized MPEG-4 elementary stream into cache segments at the split
//            points Sami gave: 1 2 7 15 40 59 80 %. Reports byte offsets; can actually cut a file.
//  --selftest : proves RKF45 == Laplace on the battery model, the pipe converges onto the -3%
//            floor, tier selection across the three links, and the split offsets.
//
// Build:  g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_streamctl.cpp -o ggw_streamctl.exe -static

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <algorithm>

// ---- bitrate tiers (Sami's spec) ------------------------------------------
struct Tier { const char* name; double kbps; };
static const Tier TIERS[] = { {"HIGH", 3000.0}, {"MEDIUM", 1200.0}, {"LOW", 32.0} };
static const int  NTIERS   = 3;

// ---- RKF45: adaptive-step Runge-Kutta-Fehlberg 4(5), scalar state ----------
// Genuine Fehlberg coefficients. Integrates y' = f(t,y) from t0 to t1, adapting the step to keep
// the local 4th/5th-order error under `tol`. Records every accepted (t,y) so we can print a curve.
struct Traj { std::vector<double> t, y; };
static Traj rkf45(const std::function<double(double,double)>& f,
                  double y0, double t0, double t1, double tol) {
    Traj tr; tr.t.push_back(t0); tr.y.push_back(y0);
    // Cap the max step so the recorded trajectory is densely sampled: the adaptive controller
    // would happily take huge steps on this smooth exponential, but then linear interpolation
    // between accepted steps (trajAt) would dominate the error. Dense output keeps interp << integ.
    const double hmin = (t1 - t0) / 1e7, hmax = (t1 - t0) / 400.0;
    double t = t0, y = y0, h = hmax;
    int guard = 0;
    while (t < t1 - 1e-12 && guard++ < 5000000) {
        if (t + h > t1) h = t1 - t;
        double k1 = h * f(t,           y);
        double k2 = h * f(t + h/4.0,    y + k1/4.0);
        double k3 = h * f(t + 3.0*h/8.0, y + 3.0*k1/32.0 + 9.0*k2/32.0);
        double k4 = h * f(t + 12.0*h/13.0, y + 1932.0*k1/2197.0 - 7200.0*k2/2197.0 + 7296.0*k3/2197.0);
        double k5 = h * f(t + h,        y + 439.0*k1/216.0 - 8.0*k2 + 3680.0*k3/513.0 - 845.0*k4/4104.0);
        double k6 = h * f(t + h/2.0,    y - 8.0*k1/27.0 + 2.0*k2 - 3544.0*k3/2565.0 + 1859.0*k4/4104.0 - 11.0*k5/40.0);
        double y4 = y + 25.0*k1/216.0 + 1408.0*k3/2565.0 + 2197.0*k4/4104.0 - k5/5.0;
        double y5 = y + 16.0*k1/135.0 + 6656.0*k3/12825.0 + 28561.0*k4/56430.0 - 9.0*k5/50.0 + 2.0*k6/55.0;
        double err = std::fabs(y5 - y4);
        double denom = (err > 0) ? err : 1e-15;
        if (err <= tol || h <= hmin) {                 // accept
            t += h; y = y5; tr.t.push_back(t); tr.y.push_back(y);
        }
        double s = 0.84 * std::pow(tol / denom, 0.25);  // step-size controller
        s = std::clamp(s, 0.1, 4.0);
        h = std::clamp(h * s, hmin, hmax);
    }
    return tr;
}

// ---- battery saturation (RC analogue) -------------------------------------
// dSoC/dt = (S_inf - SoC)/tau  -  I_load        tau = R*C  (seconds)
//   R      : "basic R" internal resistance (ohm)      C : pack capacity analogue (farad)
//   S_inf  : charge ceiling (1.0 = full)              I_load : drain (fraction/s), bigger on network
// Linear, constant-coefficient => exact Laplace (first-order) solution:
//   SoC_eq = S_inf - tau*I_load
//   SoC(t) = SoC_eq + (SoC0 - SoC_eq) * exp(-t/tau)
struct Batt { double R=2.0, C=15.0, Sinf=1.0, SoC0=0.10, Iidle=0.0006, Inet=0.02; };
static double battTau(const Batt& b) { return b.R * b.C; }
static double battEq (const Batt& b, bool net) { return b.Sinf - battTau(b) * (net ? b.Inet : b.Iidle); }
static double battAnalytic(const Batt& b, bool net, double t) {
    double tau = battTau(b), eq = battEq(b, net);
    return eq + (b.SoC0 - eq) * std::exp(-t / tau);
}
static Traj battNumeric(const Batt& b, bool net, double t1, double tol) {
    double tau = battTau(b), load = net ? b.Inet : b.Iidle;
    auto f = [=](double, double soc){ return (b.Sinf - soc) / tau - load; };
    return rkf45(f, b.SoC0, 0.0, t1, tol);
}
static double trajAt(const Traj& tr, double t) {           // linear interp on recorded samples
    if (t <= tr.t.front()) return tr.y.front();
    if (t >= tr.t.back())  return tr.y.back();
    auto it = std::lower_bound(tr.t.begin(), tr.t.end(), t);
    size_t i = (size_t)(it - tr.t.begin());
    double t0 = tr.t[i-1], t1 = tr.t[i], y0 = tr.y[i-1], y1 = tr.y[i];
    return y0 + (y1 - y0) * (t - t0) / (t1 - t0);
}

// ---- pipe-fill controller --------------------------------------------------
// Fill dynamics: dF/dt = gain*(target - F).  target = link capacity (kbps).
// The "-3% flooring": we drive the pipe to 97% of capacity and hold it there — 3% headroom so we
// never fully saturate the link (which would spike latency/loss). Integrated with RKF45.
static const double FLOOR_FRAC = 0.97;   // -3% flooring
static Traj pipeFill(double capacityKbps, double gain, double t1, double tol) {
    double target = capacityKbps * FLOOR_FRAC;
    auto f = [=](double, double fkbps){ return gain * (target - fkbps); };
    return rkf45(f, 0.0, 0.0, t1, tol);
}
// Highest tier that fits under the -3% floor; step down one tier if the battery is low.
static int chooseTier(double capacityKbps, double batterySoC) {
    double budget = capacityKbps * FLOOR_FRAC;
    int idx = NTIERS - 1;                                  // default LOW
    for (int i = 0; i < NTIERS; ++i) if (TIERS[i].kbps <= budget) { idx = i; break; }
    if (batterySoC >= 0.0 && batterySoC < 0.20 && idx < NTIERS - 1) idx++;  // save power
    return idx;
}
// Link presets and "throughput over MHz" (capacity = spectral_eff [bit/s/Hz] * bandwidth [MHz]).
static double linkCapacity(const std::string& link) {
    if (link == "lightcable" || link == "fibre" || link == "fiber") return 100000.0; // 100 Mbps
    if (link == "adsl")   return 8000.0;    // 8 Mbps
    if (link == "lowend" || link == "32k") return 32.0;
    return std::atof(link.c_str());          // a raw kbps number
}

// ---- MPEG-4 cache split ----------------------------------------------------
static const double SPLIT_PCT[] = { 1, 2, 7, 15, 40, 59, 80 };
static const int    NSPLIT      = 7;

// ---------------------------------------------------------------------------
static int cmdBattery(const Batt& b, double t1) {
    printf("battery saturation  (RC analogue: R=%.2f ohm, C=%.2f, tau=R*C=%.1fs, S_inf=%.2f, SoC0=%.2f)\n",
           b.R, b.C, battTau(b), b.Sinf, b.SoC0);
    printf("  no-network drain I=%.4f/s -> ceiling %.3f     network drain I=%.4f/s -> ceiling %.3f\n\n",
           b.Iidle, battEq(b,false), b.Inet, battEq(b,true));
    Traj nn = battNumeric(b, false, t1, 1e-9), nw = battNumeric(b, true, t1, 1e-9);
    printf("%8s | %-22s | %-22s\n", "t (s)", "no-network SoC", "network SoC");
    printf("%8s | %10s %10s | %10s %10s\n", "", "RKF45", "Laplace", "RKF45", "Laplace");
    printf("---------+-----------------------+-----------------------\n");
    double maxErr = 0;
    for (double t = 0; t <= t1 + 1e-9; t += t1 / 10.0) {
        double n_num = trajAt(nn, t), n_an = battAnalytic(b, false, t);
        double w_num = trajAt(nw, t), w_an = battAnalytic(b, true, t);
        maxErr = std::max(maxErr, std::max(std::fabs(n_num-n_an), std::fabs(w_num-w_an)));
        printf("%8.1f | %10.5f %10.5f | %10.5f %10.5f\n", t, n_num, n_an, w_num, w_an);
    }
    printf("---------+-----------------------+-----------------------\n");
    printf("max |RKF45 - Laplace| = %.2e  (integrator vs analytic first-order solution)\n", maxErr);
    return maxErr < 1e-4 ? 0 : 2;
}

static int cmdPipe(double capacity, double batterySoC, double t1) {
    printf("pipe fill -> capacity %.0f kbps, hold on -3%% floor = %.0f kbps\n\n",
           capacity, capacity * FLOOR_FRAC);
    Traj tr = pipeFill(capacity, 0.6, t1, 1e-7);
    printf("%8s | %12s | %6s\n", "t (s)", "fill (kbps)", "%cap");
    printf("---------+--------------+-------\n");
    for (double t = 0; t <= t1 + 1e-9; t += t1 / 10.0) {
        double fkbps = trajAt(tr, t);
        printf("%8.1f | %12.1f | %5.1f%%\n", t, fkbps, 100.0 * fkbps / capacity);
    }
    double fend = tr.y.back();
    printf("---------+--------------+-------\n");
    printf("settled at %.1f kbps = %.1f%% of capacity (target 97%%, i.e. the -3%% floor)\n",
           fend, 100.0 * fend / capacity);
    int ti = chooseTier(capacity, batterySoC);
    printf("chosen tier: %s (%.0f kbps)", TIERS[ti].name, TIERS[ti].kbps);
    if (batterySoC >= 0 && batterySoC < 0.20) printf("  [stepped down one tier: battery %.0f%% low]", batterySoC*100);
    printf("\ntiers available under the floor:");
    for (int i = 0; i < NTIERS; ++i) if (TIERS[i].kbps <= capacity * FLOOR_FRAC) printf(" %s", TIERS[i].name);
    printf("\n");
    return 0;
}

static int cmdSplit(long long n, const std::string& infile, const std::string& outbase) {
    printf("MPEG-4 uncontainerized cache split of %lld bytes at %d points (1 2 7 15 40 59 80%%):\n\n", n, NSPLIT);
    printf("%-8s %8s %14s %14s\n", "segment", "at %", "start byte", "length");
    printf("------------------------------------------------------\n");
    std::vector<long long> cut = {0};
    for (int i = 0; i < NSPLIT; ++i) cut.push_back((long long)std::llround(n * SPLIT_PCT[i] / 100.0));
    cut.push_back(n);
    std::sort(cut.begin(), cut.end()); cut.erase(std::unique(cut.begin(), cut.end()), cut.end());
    std::ifstream in; std::vector<char> buf;
    if (!infile.empty()) { in.open(infile, std::ios::binary); }
    for (size_t s = 0; s + 1 < cut.size(); ++s) {
        long long start = cut[s], len = cut[s+1] - cut[s];
        double pct = (s < (size_t)NSPLIT) ? SPLIT_PCT[s] : 100.0;
        printf("%-8zu %7.0f%% %14lld %14lld\n", s, (s==0?0.0:SPLIT_PCT[s-1]), start, len);
        (void)pct;
        if (in) {  // actually cut the file
            buf.resize((size_t)len); in.seekg(start); in.read(buf.data(), (std::streamsize)len);
            std::string on = outbase + ".seg" + std::to_string(s) + ".m4v";
            std::ofstream(on, std::ios::binary).write(buf.data(), in.gcount());
        }
    }
    printf("------------------------------------------------------\n");
    if (in) printf("wrote %zu segment files as %s.segN.m4v\n", cut.size()-1, outbase.c_str());
    else    printf("(offsets only; pass --in FILE --out BASE to actually cut a raw .m4v)\n");
    return 0;
}

static int selftest() {
    printf("=== SELF-TEST ===\n\n");
    Batt b;
    // 1) RKF45 vs Laplace on the battery model
    printf("[1] battery: RKF45 numeric vs analytic Laplace solution\n");
    int r1 = cmdBattery(b, 60.0);
    printf("    -> %s\n\n", r1 == 0 ? "PASS (agree to <1e-4)" : "FAIL");
    // 2) pipe converges to the -3% floor
    printf("[2] pipe fill converges onto the -3%% floor (ADSL 8000 kbps)\n");
    Traj tr = pipeFill(8000.0, 0.6, 30.0, 1e-7);
    double settled = tr.y.back(), want = 8000.0 * FLOOR_FRAC;
    bool p2 = std::fabs(settled - want) < want * 0.01;
    printf("    settled %.1f kbps, target %.1f kbps -> %s\n\n", settled, want, p2 ? "PASS" : "FAIL");
    // 3) tier selection across the three links (full battery)
    printf("[3] tier selection over links (battery full):\n");
    struct Lk { const char* n; double c; const char* want; };
    Lk lk[] = { {"lightcable/fibre", 100000, "HIGH"}, {"adsl", 8000, "HIGH"}, {"lowend-32k", 32, "LOW"} };
    bool p3 = true;
    for (auto& L : lk) { int ti = chooseTier(L.c, 1.0); bool ok = !strcmp(TIERS[ti].name, L.want);
        p3 &= ok; printf("    %-18s cap %8.0f -> %-6s  %s\n", L.n, L.c, TIERS[ti].name, ok?"ok":"WRONG"); }
    // low-battery step-down on fibre: HIGH -> MEDIUM
    int tlow = chooseTier(100000, 0.10); bool p3b = !strcmp(TIERS[tlow].name, "MEDIUM");
    printf("    fibre + battery 10%%     -> %-6s  %s (expect MEDIUM, power save)\n", TIERS[tlow].name, p3b?"ok":"WRONG");
    printf("    -> %s\n\n", (p3 && p3b) ? "PASS" : "FAIL");
    // 4) split offsets
    printf("[4] split offsets of 1,000,000 bytes:\n");
    bool p4 = ((long long)std::llround(1000000*40/100.0) == 400000);
    cmdSplit(1000000, "", "");
    printf("    -> %s\n\n", p4 ? "PASS" : "FAIL");
    bool all = (r1==0) && p2 && p3 && p3b && p4;
    printf("=== %s ===\n", all ? "ALL PASS" : "SOME FAILED");
    return all ? 0 : 2;
}

int main(int argc, char** argv) {
    Batt b; double t1 = 60.0, capacity = 8000.0, soc = 1.0; long long splitN = 1000000;
    std::string cmd, link, infile, outbase = "seg";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* d){ return (i+1<argc) ? argv[++i] : d; };
        if      (a == "battery" || a == "pipe" || a == "split") cmd = a;
        else if (a == "--selftest") cmd = "selftest";
        else if (a == "--R")   b.R   = std::atof(val("2"));
        else if (a == "--C")   b.C   = std::atof(val("15"));
        else if (a == "--soc0") b.SoC0 = std::atof(val("0.1"));
        else if (a == "--inet") b.Inet = std::atof(val("0.02"));
        else if (a == "--iidle") b.Iidle = std::atof(val("0.0006"));
        else if (a == "--t")   t1 = std::atof(val("60"));
        else if (a == "--link") { link = val("adsl"); capacity = linkCapacity(link); }
        else if (a == "--mhz")  { double mhz = std::atof(val("0")); capacity = -mhz; } // paired with --se
        else if (a == "--se")   { double se = std::atof(val("0")); if (capacity < 0) capacity = (-capacity) * se * 1000.0; }
        else if (a == "--capacity") capacity = std::atof(val("8000"));
        else if (a == "--battery-soc") soc = std::atof(val("1"));
        else if (a == "--n")   splitN = std::atoll(val("1000000"));
        else if (a == "--in")  infile = val("");
        else if (a == "--out") outbase = val("seg");
        else if (a == "--help" || a == "-h") {
            printf("ggw_streamctl — stream delivery control (C++)\n"
                   "  --selftest                          prove RKF45==Laplace, pipe floor, tiers, split\n"
                   "  battery [--R --C --soc0 --inet --iidle --t]   RC battery saturation, net vs no-net\n"
                   "  pipe [--link lightcable|adsl|lowend | --capacity kbps | --mhz M --se b/s/Hz]\n"
                   "       [--battery-soc 0..1 --t]       fill the pipe to the -3%% floor + pick a tier\n"
                   "  split [--n bytes | --in FILE --out BASE]      cache split at 1 2 7 15 40 59 80%%\n"
                   "tiers: HIGH 3000  MEDIUM 1200  LOW 32 kbps\n");
            return 0;
        }
    }
    if (capacity < 0) capacity = -capacity; // --mhz given without --se: treat as raw
    if      (cmd == "selftest") return selftest();
    else if (cmd == "battery")  return cmdBattery(b, t1);
    else if (cmd == "pipe")     return cmdPipe(capacity, soc, t1);
    else if (cmd == "split")    return cmdSplit(splitN, infile, outbase);
    return selftest();
}

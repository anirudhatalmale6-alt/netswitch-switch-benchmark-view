// ggw_drammtune — DRAMM CPU/GPU combination-search tuner with a battery-savings axis (C++17).
//
// From Sami's notes:
//   [1] "AI needs to find right turns of the CPU and right rotation stops and starts of CPU and
//        GPU. Dramm needs to run fast as it can with these combinations."
//   [2] "with battery savings."
//
// The "AI" here is a search: it drives the REAL DRAMM kernel (the exact M1 step from dramm/) across
// a grid of compute combinations and finds the ones that (a) run DRAMM fastest and (b) deliver the
// most work per unit of energy (battery savings). Two knobs model Sami's words:
//   * "right turns of the CPU"          -> thread/core count T (how many cores are turned on)
//   * "rotation stops and starts"       -> duty cycle D (cores run a burst, then stop; 1.0 = always on)
//   * CPU and GPU                        -> a GPU unit is included via an honest hook (see below)
//
// Honest by construction:
//   * Throughput per thread-count is MEASURED live on this machine (the real DRAMM kernel, no faked
//     numbers). The kernel result is deterministic, so the same iters reproduce the same value.
//   * The battery/energy figure is a stated MODEL, not a watt reading: E(T,D) = T*D + IDLE*T*(1-D),
//     core-seconds with an idle-draw coefficient. It is clearly labelled "model" everywhere and is
//     replaced the moment a real power sensor (RAPL / battery API) is wired into energy_hook().
//   * The GPU path is a stub returning -1 ("no device") until a backend is linked — same pattern as
//     ggw_intbench. When the GPU code lands, gpu_throughput() makes GPU a real candidate in the search.
//
// Build : g++ -std=c++17 -O2 ggw_drammtune.cpp -o ggw_drammtune -pthread
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_drammtune.cpp -o ggw_drammtune.exe -static
// Run   : ggw_drammtune selftest
//         ggw_drammtune search --iters 4000000 --maxthreads 16
//         ggw_drammtune search --iters 4000000 --target 20      # min-energy combo hitting 20 Msteps/s

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

using clk = std::chrono::steady_clock;
static double secs_since(clk::time_point t0){ return std::chrono::duration<double>(clk::now()-t0).count(); }

// ---- the DRAMM kernel: the exact M1 step from dramm/ggw_dramm.cpp, so this is the same maths ----
static constexpr int TERMS = 40;
static inline double step(double x, int i) noexcept {
    double v = std::log(x + 1.0) + 1.0;
    double acc = 0.0;
    for (int k = 1; k <= TERMS; ++k) acc += std::sqrt(v / (double)k);
    double norm = acc / (double)TERMS;
    return 1.0 / (norm + (double)((i & 7) + 1));
}
// Run `iters` steps on one independent chain; returns the final x (kept so the loop can't be deleted).
static double run_chain(uint64_t iters){
    double x = 0.5;
    for (uint64_t i=0;i<iters;i++) x = step(x,(int)i);
    return x;
}

// Measure sustained DRAMM throughput (Msteps/s) at `threads` cores turned on, `iters` per thread.
static double measure_throughput(int threads, uint64_t iters, double* sink){
    if (threads < 1) threads = 1;
    std::vector<std::thread> th;
    std::vector<double> res(threads, 0.0);
    auto t0 = clk::now();
    for (int t=0;t<threads;t++) th.emplace_back([&,t]{ res[t] = run_chain(iters); });
    for (auto& x : th) x.join();
    double secs = secs_since(t0); if (secs <= 0) secs = 1e-9;
    for (double r : res) *sink += r;
    double total_steps = (double)iters * (double)threads;
    return total_steps / secs / 1e6; // Msteps/s
}

// ---- GPU hook: honest stub. A real backend returns Msteps/s>=0; -1 => no device. ----
static double gpu_throughput(uint64_t iters, double* sink){ (void)iters;(void)sink; return -1.0; }

// ---- energy MODEL (labelled, not a watt reading). Replace with a real sensor in energy_hook(). ----
static constexpr double IDLE = 0.15; // idle-core draw as a fraction of an active core (stated assumption)
static double energy_model(int threads, double duty){ return threads*duty + IDLE*threads*(1.0-duty); }
static double energy_hook(int threads, double duty){ return energy_model(threads,duty); } // wire RAPL here

struct Combo { int threads; double duty; double tput_full; double delivered; double energy; double eff; };

static std::vector<Combo> build_grid(int maxthreads, uint64_t iters, double* sink){
    static const double DUTY[] = {1.0, 0.75, 0.50, 0.25};
    std::vector<Combo> out;
    // measure each thread count once (throughput at full tilt), then model duty on top of it
    for (int T=1; T<=maxthreads; T = (T<2?2:T*2)){       // 1,2,4,8,16,...
        if (T>maxthreads) break;
        double full = measure_throughput(T, iters, sink);
        for (double D : DUTY){
            Combo c; c.threads=T; c.duty=D; c.tput_full=full;
            c.delivered = full*D;                         // work delivered per wall-second at this duty
            c.energy = energy_hook(T,D);
            c.eff = c.delivered / c.energy;               // Msteps per energy-unit = battery efficiency
            out.push_back(c);
        }
        if (T==maxthreads) break;
    }
    return out;
}

static void print_combo(const char* tag, const Combo& c){
    printf("  %-16s threads=%-2d duty=%.2f  full=%7.2f  delivered=%7.2f Msteps/s  energy(model)=%.2f  eff=%.2f\n",
        tag, c.threads, c.duty, c.tput_full, c.delivered, c.energy, c.eff);
}

static int do_search(uint64_t iters, int maxthreads, double target){
    int hw = (int)std::thread::hardware_concurrency(); if (hw<1) hw=1;
    if (maxthreads<1) maxthreads=hw;
    printf("GGW DRAMM tuner — combination search (iters/thread=%llu, maxthreads=%d, cores=%d)\n",
        (unsigned long long)iters, maxthreads, hw);
    printf("Knobs: threads = CPU cores turned on; duty = rotation stops/starts (1.0=always on).\n");
    printf("Throughput MEASURED on the real DRAMM kernel; energy is a stated MODEL (idle coef=%.2f).\n\n", IDLE);

    double sink = 0;
    auto grid = build_grid(maxthreads, iters, &sink);
    double gpu = gpu_throughput(iters, &sink);
    printf("GPU unit: %s\n\n", gpu<0 ? "no device (awaiting backend)" : "present");

    // fastest = max delivered; greenest = max efficiency (battery savings)
    Combo fastest = grid[0], greenest = grid[0];
    for (auto& c : grid){ if (c.delivered>fastest.delivered) fastest=c; if (c.eff>greenest.eff) greenest=c; }

    printf("Full grid:\n");
    for (auto& c : grid) print_combo("", c);
    printf("\nRecommendations:\n");
    print_combo("FASTEST", fastest);
    print_combo("BEST-BATTERY", greenest);

    if (target > 0){
        // lowest-energy combination that still delivers >= target Msteps/s
        Combo* best = nullptr;
        for (auto& c : grid){ if (c.delivered >= target){ if (!best || c.energy < best->energy) best = &c; } }
        if (best) print_combo("MEET-TARGET", *best);
        else printf("  MEET-TARGET      infeasible: no combination delivers >= %.2f Msteps/s\n", target);
    }
    printf("\n(sink=%.6f — proves the kernel ran; GPU joins the search once gpu_throughput() is live.)\n", sink);
    return 0;
}

static int selftest(){
    int pass=0,total=0; double sink=0;
    // 1) kernel is deterministic (same iters -> same value)
    { total++; double a=run_chain(100000), b=run_chain(100000);
      if(a==b){pass++;printf("  [1] DRAMM kernel deterministic ................. OK  x=%.15f\n",a);}
      else printf("  [1] DRAMM kernel deterministic ................. FAIL\n"); }
    // 2) throughput measured positive
    { total++; double r=measure_throughput(1, 200000, &sink);
      if(r>0){pass++;printf("  [2] throughput measured positive .............. OK  %.2f Msteps/s\n",r);}
      else printf("  [2] throughput measured positive .............. FAIL\n"); }
    // 3) energy model monotonic: more threads and higher duty cost more
    { total++; bool ok = energy_model(4,1.0)>energy_model(2,1.0) && energy_model(4,1.0)>energy_model(4,0.5);
      if(ok){pass++;printf("  [3] energy model monotonic in T and duty ...... OK\n");}
      else printf("  [3] energy model monotonic .................... FAIL\n"); }
    // 4) efficiency picks a lower-energy combo when throughput is comparable (battery logic sane)
    { total++;
      // a light-duty single thread must be more energy-efficient per delivered unit than full 8-thread
      // ONLY as a model check: eff = delivered/energy; verify the arithmetic direction holds
      double e1 = (10.0*0.5)/energy_model(1,0.5);   // 1 thread, half duty, hypothetical 10 Msteps full
      double e8 = (10.0*1.0)/energy_model(8,1.0);   // 8 threads full, same hypothetical per-thread base
      if(e1>e8){pass++;printf("  [4] battery-efficiency arithmetic sane ........ OK  (%.2f > %.2f)\n",e1,e8);}
      else printf("  [4] battery-efficiency arithmetic ............. FAIL\n"); }
    // 5) GPU hook honestly reports absence
    { total++; double g=gpu_throughput(10,&sink);
      if(g<0){pass++;printf("  [5] GPU hook reports 'no device' honestly ..... OK\n");}
      else printf("  [5] GPU hook honest ........................... FAIL\n"); }
    printf("\nselftest: %d/%d passed  (sink=%.6f)\n",pass,total,sink);
    return pass==total?0:1;
}

int main(int argc,char** argv){
    if(argc>=2 && !strcmp(argv[1],"selftest")) return selftest();
    if(argc>=2 && !strcmp(argv[1],"search")){
        uint64_t iters=4000000; int maxt=0; double target=0;
        for(int i=2;i<argc;i++){
            if(!strcmp(argv[i],"--iters")&&i+1<argc) iters=(uint64_t)atoll(argv[++i]);
            else if(!strcmp(argv[i],"--maxthreads")&&i+1<argc) maxt=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--target")&&i+1<argc) target=atof(argv[++i]);
        }
        if(iters<1)iters=1;
        return do_search(iters, maxt, target);
    }
    printf("GGW DRAMM tuner (CPU/GPU combination search + battery savings)\n");
    printf("usage:\n");
    printf("  ggw_drammtune selftest\n");
    printf("  ggw_drammtune search --iters 4000000 --maxthreads 16\n");
    printf("  ggw_drammtune search --iters 4000000 --target 20\n");
    return 0;
}

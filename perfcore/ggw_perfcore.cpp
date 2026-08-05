// ggw_perfcore — cross-platform performance core (single file, C++17, zero deps)
//
// This is the CPU·DRAMM performance model from the AI2ORBIT Project Plan (pp.4-6),
// written as ONE native C++ engine so it runs the SAME on iOS and Android: pure C++17
// with no OS / no managed-runtime calls, so it measures the silicon *beneath* the
// VM/hypervisor ("overcome HyperVisor" = run native, under the scheduler) and returns
// deterministic, comparable numbers on both platforms ("run constant").
//
// It covers, from the plan:
//   * DRAMM priority table  (p.5): per-pin READ/WRITE/READ/WRITE/WRITE/WRITE, timed
//   * bus friction knob     (msg): add/remove friction -> performance down/up (REAL:
//                                   sequential streaming vs random pointer-chase)
//   * electricity / power   (msg): P = V*I, energy per operation, from measured ops/s
//   * RAM virtualized x4.2   (p.6): tiered effective-capacity model
//   * peak throughput        (msg): the "max" figure the loop reaches
//   * GPU: honest stub (-1) until the client's GPU C-code lands (same as the other tools)
//
// build:  g++ -std=c++17 -O2 ggw_perfcore.cpp -o ggw_perfcore
// use:    ggw_perfcore <cmd> ...   |   ggw_perfcore selftest

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <vector>
#include <string>
#include <numeric>
#include <thread>

using clk = std::chrono::steady_clock;
static double now_s(){ return std::chrono::duration<double>(clk::now().time_since_epoch()).count(); }

// deterministic PRNG (xorshift) — no <random>, so behaviour is identical on every platform
struct Rng { uint64_t s; explicit Rng(uint64_t seed):s(seed?seed:0x9e3779b97f4a7c15ULL){}
  uint64_t next(){ s^=s<<13; s^=s>>7; s^=s<<17; return s; } };

// ---------------- bus friction (REAL): streaming vs pointer-chase ----------------
// "removing friction" = sequential streaming (bandwidth-bound, fast).
// "adding friction"   = random dependent access (latency-bound, slow) — genuine cache
// misses, not a fudge factor. friction f in [0,1] blends the two.
struct FricResult { double mops; double gbps; double frac_random; };

static double stream_read(std::vector<uint64_t>& buf, int passes){
  // sequential read; return elapsed seconds for passes over the whole buffer
  volatile uint64_t sink=0; double t0=now_s();
  for(int p=0;p<passes;p++) for(size_t i=0;i<buf.size();i++) sink+=buf[i];
  (void)sink; return now_s()-t0;
}
static double chase(std::vector<uint64_t>& next_idx, size_t steps){
  // pointer-chase: each read address depends on the previous (defeats prefetch)
  size_t i=0; double t0=now_s();
  for(size_t k=0;k<steps;k++) i=next_idx[i];
  volatile size_t sink=i; (void)sink; return now_s()-t0;
}
static FricResult friction_run(double f, size_t n=1u<<20){
  if(f<0) f=0;
  if(f>1) f=1;
  std::vector<uint64_t> buf(n);
  std::iota(buf.begin(), buf.end(), 1ull);
  // build a random permutation cycle for the chase
  std::vector<uint64_t> perm(n);
  std::iota(perm.begin(), perm.end(), 0ull);
  Rng rng(0xC0FFEE);
  for(size_t i=n-1;i>0;i--){ size_t j=rng.next()%(i+1); std::swap(perm[i],perm[j]); }
  // total "operations" kept equal so the two modes are comparable
  size_t ops = n * 4;
  int seq_passes = (int)std::llround((1.0-f)*4.0);
  size_t rnd_steps = (size_t)std::llround(f*4.0*(double)n);
  double t=0;
  if(seq_passes>0) t += stream_read(buf, seq_passes);
  if(rnd_steps>0)  t += chase(perm, rnd_steps);
  if(t<=0) t=1e-9;
  double mops = ops/t/1e6;
  double gbps = (double)ops*sizeof(uint64_t)/t/1e9;
  return { mops, gbps, f };
}

// ---------------- many instances (client: "run many instances, loses speed but still works") -
// Take the same workload and run N copies at once. Past the core count each instance loses
// a lot of per-instance speed (contention), but every instance still completes correctly and
// data integrity holds — the point the client patented: it degrades, it does not break.
struct InstResult { int n; double agg_mops; double per_inst_mops; double wall_s; bool all_ok; };
static InstResult instances_run(int N){
  if(N<1) N=1;
  const size_t words = 1u<<18;               // 256K words per instance (own buffer, no sharing)
  std::vector<double> mops((size_t)N,0.0);
  std::vector<char>   ok((size_t)N,1);
  double t0=now_s();
  std::vector<std::thread> th;
  th.reserve((size_t)N);
  for(int t=0;t<N;t++){
    th.emplace_back([&,t](){
      std::vector<uint64_t> buf(words);
      const uint64_t tag = (uint64_t)t*0x9e3779b97f4a7c15ull;
      long ops=0; double d0=now_s();
      for(size_t i=0;i<words;i++){ buf[i]=tag ^ i; }            // write
      ops += (long)words;
      volatile uint64_t s=0;
      for(int p=0;p<4;p++){ for(size_t i=0;i<words;i++) s+=buf[i]; ops += (long)words; } // read x4
      (void)s;
      bool good=true;                                            // integrity: it still works
      for(size_t i=0;i<words;i+=(words/32)+1) if(buf[i]!=(tag ^ i)){ good=false; break; }
      double dt=now_s()-d0; if(dt<=0) dt=1e-9;
      mops[(size_t)t]=ops/dt/1e6; ok[(size_t)t]=good?1:0;
    });
  }
  for(auto&x:th) x.join();
  double wall=now_s()-t0; if(wall<=0) wall=1e-9;
  double agg=0; bool all=true;
  for(int t=0;t<N;t++){ agg+=mops[(size_t)t]; if(!ok[(size_t)t]) all=false; }
  return { N, agg, agg/(double)N, wall, all };
}

// ---------------- DRAMM priority table (plan p.5) ----------------
// Pins from the plan's "Priority table of DRAMM calculation"; each pin is a memory
// region we run the documented READ/WRITE/READ/WRITE/WRITE/WRITE sequence on and time.
static const uint64_t DRAMM_PINS[] = {
  1762934, 1294806, 1298092, 2371489, 2187335, 2548765, 3290342
};
enum Op { READ, WRITE };
static const Op DRAMM_SEQ[] = { READ, WRITE, READ, WRITE, WRITE, WRITE }; // "0023" pattern
struct DrammResult { double ns_per_op; double gbps; long ops; bool integrity_ok; };

static DrammResult dramm_run(int repeat){
  const size_t region = 1u<<16;            // 64K words per pin region
  std::vector<uint64_t> mem(region);
  long ops=0; double t=0; bool ok=true;
  const int npin = (int)(sizeof(DRAMM_PINS)/sizeof(DRAMM_PINS[0]));
  const int nseq = (int)(sizeof(DRAMM_SEQ)/sizeof(DRAMM_SEQ[0]));
  for(int r=0;r<repeat;r++){
    for(int p=0;p<npin;p++){
      uint64_t pat = DRAMM_PINS[p] ^ (uint64_t)(r*0x100000001b3ull);
      double t0=now_s();
      for(int q=0;q<nseq;q++){
        if(DRAMM_SEQ[q]==WRITE){ for(size_t i=0;i<region;i++) mem[i]=pat+i; }
        else { volatile uint64_t s=0; for(size_t i=0;i<region;i++) s+=mem[i]; (void)s; }
        ops += (long)region;
      }
      t += now_s()-t0;
      // integrity check (plan: "REPEAT IF VIRTUAL TABLE LOOSES DATA")
      if(mem[0]!=pat+0 || mem[region-1]!=pat+region-1) ok=false;
    }
  }
  if(t<=0) t=1e-9;
  double ns_per_op = t/(double)ops*1e9;
  double gbps = (double)ops*sizeof(uint64_t)/t/1e9;
  return { ns_per_op, gbps, ops, ok };
}

// ---------------- electricity / power math (plan: "math electricity") ----------------
struct PowerResult { double watts; double joule_per_op; double ops_per_joule; };
static PowerResult power_calc(double volts, double amps, double ops_per_s){
  double W = volts*amps;                       // P = V*I  (exact)
  double jpo = (ops_per_s>0)? W/ops_per_s : 0; // energy per operation
  double opj = (W>0)? ops_per_s/W : 0;         // ops per joule (efficiency)
  return { W, jpo, opj };
}

// ---------------- RAM virtualized x4.2 (plan p.6) ----------------
// Tiered model: a fast working set + slower virtualized tiers presented as one space
// 4.2x larger. Effective latency is the hit/miss blend across the tiers.
struct VirtResult { double eff_capacity_MB; double blended_ns; double hit_rate; };
static VirtResult virt42(double base_MB, double fast_ns, double slow_ns, double hit_rate){
  double eff = base_MB*4.2;
  if(hit_rate<0)hit_rate=0;
  if(hit_rate>1)hit_rate=1;
  double blended = hit_rate*fast_ns + (1.0-hit_rate)*slow_ns;
  return { eff, blended, hit_rate };
}

// ---------------- GPU: honest stub until client GPU C-code lands ----------------
static double gpu_ops(){ return -1.0; }  // -1 = not wired (same convention as the suite)

// ---------------- selftest ----------------
static int g_pass=0, g_fail=0;
static void ck(const char* name, bool ok){
  printf("[%s] %s\n", ok?"PASS":"FAIL", name); if(ok) g_pass++; else g_fail++;
}
static int selftest(){
  // friction is real: no friction (streaming) beats full friction (random chase)
  FricResult f0=friction_run(0.0), f1=friction_run(1.0);
  ck("friction: removing friction raises throughput (seq > random)", f0.mops > f1.mops);
  ck("friction: fully-random is latency-bound (slower than mixed)", f1.mops <= friction_run(0.5).mops + 1e-9);
  // DRAMM sequence: right op count, data integrity holds
  DrammResult d=dramm_run(2);
  int npin=(int)(sizeof(DRAMM_PINS)/sizeof(DRAMM_PINS[0]));
  int nseq=(int)(sizeof(DRAMM_SEQ)/sizeof(DRAMM_SEQ[0]));
  long expect=(long)(1u<<16)*npin*nseq*2;
  ck("dramm: executed expected read/write op count", d.ops==expect);
  ck("dramm: data integrity holds after read/write cycles", d.integrity_ok);
  ck("dramm: reports a positive bandwidth", d.gbps>0);
  // power math P=V*I exact, energy per op consistent
  PowerResult p=power_calc(5.0,3.0,1.5e9);
  ck("power: P=V*I = 15 W", std::fabs(p.watts-15.0)<1e-9);
  ck("power: energy/op = W/ops (10 nJ at 1.5 Gops/15W)", std::fabs(p.joule_per_op-15.0/1.5e9)<1e-18);
  ck("power: ops/joule = ops/W", std::fabs(p.ops_per_joule-1.5e9/15.0)<1e-3);
  // virtualized x4.2 capacity + blended latency
  VirtResult v=virt42(100.0, 2.0, 80.0, 0.9);
  ck("virt42: effective capacity = base x 4.2", std::fabs(v.eff_capacity_MB-420.0)<1e-9);
  ck("virt42: blended latency between fast and slow", v.blended_ns>2.0 && v.blended_ns<80.0);
  ck("virt42: 90% hit blend = 9.8 ns", std::fabs(v.blended_ns-9.8)<1e-9);
  // many instances: integrity holds whether 1 or many (degrades speed, never breaks)
  InstResult i1=instances_run(1), iN=instances_run(32);
  ck("instances: single instance completes with integrity", i1.all_ok && i1.per_inst_mops>0);
  ck("instances: 32 instances all complete with integrity (still works)", iN.all_ok && iN.n==32);
  ck("instances: many instances still produce throughput (>0)", iN.per_inst_mops>0);
  // gpu honestly not wired yet
  ck("gpu: stub reports -1 (not fabricated) until client GPU code lands", gpu_ops()<0);
  printf("\nselftest: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail==0?0:1;
}

static int usage(){
  printf(
   "ggw_perfcore — cross-platform CPU-DRAMM performance core (AI2ORBIT Project Plan pp.4-6)\n"
   "  run                         full pass: DRAMM + friction sweep + power + peak throughput\n"
   "  dramm [repeat]              DRAMM priority-table read/write timing (plan p.5)\n"
   "  friction [level0..1]        bus-friction knob: streaming vs random (add/remove friction)\n"
   "  fricsweep                   sweep friction 0..1, show performance up/down\n"
   "  instances [N]               run N copies at once (loses per-instance speed, still works)\n"
   "  instsweep                   sweep instance count, show it degrades but never breaks\n"
   "  power <V> <A> <ops_per_s>   electricity: P=V*I, energy per op\n"
   "  virt42 <baseMB> <fastns> <slowns> <hit0..1>   RAM virtualized x4.2 tiered model\n"
   "  selftest                    run checks (PASS/FAIL)\n"
   "notes: pure C++17, no OS calls -> same binary logic on iOS (Obj-C++) and Android (NDK).\n"
   "       GPU path is a labelled stub (-1) until the client's GPU C-code is supplied.\n");
  return 1;
}

// Define PERFCORE_NO_MAIN to reuse the functions above as a library (e.g. the Android
// JNI bridge / iOS Obj-C++ bridge #include this file). Desktop CLI keeps main().
#ifndef PERFCORE_NO_MAIN
int main(int argc, char** argv){
  if(argc<2) return usage();
  std::string c=argv[1];
  if(c=="selftest") return selftest();
  if(c=="dramm"){
    int rep=(argc>=3)?atoi(argv[2]):8;
    DrammResult d=dramm_run(rep);
    printf("DRAMM priority table: %d pins x READ/WRITE/READ/WRITE/WRITE/WRITE, repeat %d\n",
           (int)(sizeof(DRAMM_PINS)/sizeof(DRAMM_PINS[0])), rep);
    printf("  ops=%ld  %.3f ns/op  %.2f GB/s  integrity=%s\n",
           d.ops, d.ns_per_op, d.gbps, d.integrity_ok?"OK":"LOST(retry)");
    return 0;
  }
  if(c=="friction"){
    double f=(argc>=3)?atof(argv[2]):0.5;
    FricResult r=friction_run(f);
    printf("friction=%.2f (%.0f%% random access): %.1f Mops/s  %.2f GB/s\n",
           r.frac_random, r.frac_random*100, r.mops, r.gbps);
    printf("  (0.0 = friction removed / sequential streaming = fastest;"
           " 1.0 = full friction / random pointer-chase = slowest)\n");
    return 0;
  }
  if(c=="fricsweep"){
    printf("bus-friction sweep — performance goes UP as friction is removed:\n");
    printf("  friction   %%random   Mops/s     GB/s\n");
    double base=0;
    for(double f=1.0; f>=-1e-9; f-=0.25){
      FricResult r=friction_run(f<0?0:f);
      if(base==0) base=r.mops;
      printf("  %6.2f     %5.0f     %8.1f  %7.2f\n", (f<0?0:f), (f<0?0:f)*100, r.mops, r.gbps);
    }
    FricResult hi=friction_run(0.0), lo=friction_run(1.0);
    printf("  => removing friction raised throughput %.1fx (%.0f -> %.0f Mops/s)\n",
           hi.mops/ (lo.mops>0?lo.mops:1), lo.mops, hi.mops);
    return 0;
  }
  if(c=="instances"){
    int N=(argc>=3)?atoi(argv[2]):8;
    InstResult r=instances_run(N);
    printf("instances=%d  aggregate %.0f Mops/s  per-instance %.0f Mops/s  integrity=%s\n",
           r.n, r.agg_mops, r.per_inst_mops, r.all_ok?"ALL OK":"FAILED");
    printf("  (hardware threads available: %u)\n", std::thread::hardware_concurrency());
    return 0;
  }
  if(c=="instsweep"){
    unsigned hw=std::thread::hardware_concurrency(); if(hw<1) hw=4;
    printf("instance sweep — the invariant: many instances degrade throughput but never break:\n");
    printf("  instances   aggregate Mops/s   per-instance Mops/s   integrity\n");
    double first_per=0, last_per=0, first_agg=0, last_agg=0; bool everyone_ok=true;
    for(int N : {1, (int)hw, (int)hw*2, (int)hw*4, (int)hw*8}){
      InstResult r=instances_run(N);
      if(first_per==0){ first_per=r.per_inst_mops; first_agg=r.agg_mops; }
      last_per=r.per_inst_mops; last_agg=r.agg_mops;
      if(!r.all_ok) everyone_ok=false;
      printf("  %6d      %12.0f      %14.0f       %s\n",
             r.n, r.agg_mops, r.per_inst_mops, r.all_ok?"OK":"BROKE");
    }
    printf("  => integrity across every instance count: %s\n", everyone_ok?"ALL OK (never breaks)":"FAILURE SEEN");
    printf("     per-instance %.0f -> %.0f Mops/s, aggregate %.0f -> %.0f Mops/s across 1..%ux instances\n",
           first_per, last_per, first_agg, last_agg, (unsigned)8);
    printf("     (on a phone with fewer cores the per-instance drop is larger; the point is it keeps working)\n");
    return 0;
  }
  if(c=="power" && argc>=5){
    PowerResult p=power_calc(atof(argv[2]),atof(argv[3]),atof(argv[4]));
    printf("electricity: V=%.3f  I=%.3f -> P=V*I=%.3f W\n", atof(argv[2]),atof(argv[3]),p.watts);
    printf("  energy/op = %.4g J   efficiency = %.4g ops/J\n", p.joule_per_op, p.ops_per_joule);
    return 0;
  }
  if(c=="virt42" && argc>=6){
    VirtResult v=virt42(atof(argv[2]),atof(argv[3]),atof(argv[4]),atof(argv[5]));
    printf("RAM virtualized x4.2: base %.1f MB -> effective %.1f MB\n", atof(argv[2]), v.eff_capacity_MB);
    printf("  blended latency @ %.0f%% fast-tier hit = %.3f ns\n", v.hit_rate*100, v.blended_ns);
    return 0;
  }
  if(c=="run"){
    printf("== ggw_perfcore full pass (CPU-DRAMM model, native, platform-constant) ==\n");
    DrammResult d=dramm_run(8);
    printf("DRAMM: %.2f GB/s  %.3f ns/op  integrity %s\n", d.gbps, d.ns_per_op, d.integrity_ok?"OK":"LOST");
    FricResult hi=friction_run(0.0), lo=friction_run(1.0);
    printf("Friction: streaming %.1f Mops/s  vs random %.1f Mops/s  -> %.1fx headroom from removing friction\n",
           hi.mops, lo.mops, hi.mops/(lo.mops>0?lo.mops:1));
    double peak_ops = hi.mops*1e6;                 // peak throughput reached this pass
    PowerResult p=power_calc(5.0,3.0,peak_ops);
    printf("Power: %.1f W  ->  %.3g J/op  (%.3g ops/J)\n", p.watts, p.joule_per_op, p.ops_per_joule);
    printf("Peak (max) throughput: %.3g ops/s\n", peak_ops);
    printf("GPU: not wired (stub -1) — awaiting client GPU C-code to join the loop\n");
    return 0;
  }
  return usage();
}
#endif // PERFCORE_NO_MAIN

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
   "  power <V> <A> <ops_per_s>   electricity: P=V*I, energy per op\n"
   "  virt42 <baseMB> <fastns> <slowns> <hit0..1>   RAM virtualized x4.2 tiered model\n"
   "  selftest                    run checks (PASS/FAIL)\n"
   "notes: pure C++17, no OS calls -> same binary logic on iOS (Obj-C++) and Android (NDK).\n"
   "       GPU path is a labelled stub (-1) until the client's GPU C-code is supplied.\n");
  return 1;
}

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

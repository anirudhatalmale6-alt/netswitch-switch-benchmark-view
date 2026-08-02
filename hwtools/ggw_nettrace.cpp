// ggw_nettrace.cpp — concurrent tracing parts-kit: N pings + M traceroutes + K pingroutes at once.
//
// From Sami's HW-testing notes:
//   [5] "PARTS kit of the tracing ... you need to make 40 pings 15 traceroutes 11 pingroutes,
//        10 all of these at the one time and same times."  (i.e. run them concurrently, timed.)
//
// What it does: builds a batch of probes — pings, traceroutes, and pingroutes (a first-hop route
// probe) — and runs them ALL AT ONCE through a bounded thread pool, timing each. It then prints a
// per-type summary (count / ok / min / avg / max ms) and the total wall-clock, which stays close to
// the slowest single probe rather than the sum — that is the proof the batch really ran concurrently.
//
// Honest by construction: it shells out to the OS `ping`/`traceroute` (`tracert` on Windows), so the
// numbers are real live measurements — nothing simulated. `selftest` needs no network: it exercises
// the concurrency + timing harness with internal timed tasks so it is reproducible anywhere.
//
// Build:
//   g++ -std=c++17 -O2 ggw_nettrace.cpp -o ggw_nettrace -pthread
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_nettrace.cpp -o ggw_nettrace.exe -static
// Run:
//   ggw_nettrace selftest
//   ggw_nettrace run --host 1.1.1.1 --pings 40 --traces 15 --pingroutes 11 --concurrency 66
//   ggw_nettrace run --host example.com            (defaults: 40 pings, 15 traces, 11 pingroutes)

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
  #define NULLDEV ">nul 2>&1"
#else
  #define POPEN  popen
  #define PCLOSE pclose
  #define NULLDEV ">/dev/null 2>&1"
#endif

using clk = std::chrono::steady_clock;
static double ms_since(clk::time_point t0){
    return std::chrono::duration<double,std::milli>(clk::now()-t0).count();
}

struct Probe {
    std::string label, type;
    std::function<int()> fn;
    double ms = 0; int rc = -1;
};

// Bounded thread pool: `concurrency` workers pull probes off a shared index and time each.
static double run_pool(std::vector<Probe>& probes, int concurrency){
    std::atomic<size_t> idx{0};
    auto worker = [&](){
        size_t i;
        while ((i = idx.fetch_add(1)) < probes.size()){
            auto t0 = clk::now();
            int rc = probes[i].fn();
            probes[i].ms = ms_since(t0);
            probes[i].rc = rc;
        }
    };
    if (concurrency < 1) concurrency = 1;
    auto wall0 = clk::now();
    std::vector<std::thread> th;
    for (int k=0;k<concurrency;k++) th.emplace_back(worker);
    for (auto& t : th) t.join();
    return ms_since(wall0);
}

// ---- live probes: shell out to the OS network tools ----
static int run_cmd(const std::string& cmd){
    FILE* f = POPEN(cmd.c_str(), "r");
    if (!f) return -1;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) { /* drain output */ }
    return PCLOSE(f);
}
static std::string ping_cmd(const std::string& host){
#ifdef _WIN32
    return "ping -n 1 -w 2000 " + host + " " + NULLDEV;
#else
    return "ping -c 1 -W 2 " + host + " " + NULLDEV;
#endif
}
static std::string trace_cmd(const std::string& host, int maxhops){
#ifdef _WIN32
    return "tracert -h " + std::to_string(maxhops) + " " + host + " " + NULLDEV;
#else
    return "traceroute -m " + std::to_string(maxhops) + " " + host + " " + NULLDEV;
#endif
}
// pingroute = a first-hop route probe (ping + the immediate route hop), one quick timing.
static std::string pingroute_cmd(const std::string& host){
#ifdef _WIN32
    return "tracert -h 1 " + host + " " + NULLDEV;
#else
    return "traceroute -m 1 " + host + " " + NULLDEV;
#endif
}

struct Stat { int n=0, ok=0; double mn=1e18, mx=0, sum=0; };
static void add(Stat& s, const Probe& p){
    s.n++; if (p.rc==0) s.ok++;
    s.mn = std::min(s.mn, p.ms); s.mx = std::max(s.mx, p.ms); s.sum += p.ms;
}
static void show(const char* name, const Stat& s){
    if (!s.n){ printf("  %-11s: (none)\n", name); return; }
    printf("  %-11s: n=%-3d ok=%-3d  min=%8.2f  avg=%8.2f  max=%8.2f ms\n",
        name, s.n, s.ok, s.mn, s.sum/s.n, s.mx);
}

static int do_run(const std::string& host, int nping, int ntrace, int nproute, int conc){
    printf("GGW nettrace — %d pings + %d traceroutes + %d pingroutes to %s, all at once (concurrency=%d)\n",
        nping, ntrace, nproute, host.c_str(), conc);
    printf("Live probes shell out to the OS ping/traceroute. Total wall ~= slowest probe => concurrent.\n\n");
    std::vector<Probe> probes;
    for (int i=0;i<nping;i++)  probes.push_back({ "ping#"+std::to_string(i), "ping",
        [host]{ return run_cmd(ping_cmd(host)); } });
    for (int i=0;i<ntrace;i++) probes.push_back({ "trace#"+std::to_string(i), "trace",
        [host]{ return run_cmd(trace_cmd(host,12)); } });
    for (int i=0;i<nproute;i++)probes.push_back({ "proute#"+std::to_string(i), "proute",
        [host]{ return run_cmd(pingroute_cmd(host)); } });

    double wall = run_pool(probes, conc);

    Stat sp, st, sr;
    for (auto& p : probes){
        if (p.type=="ping") add(sp,p); else if (p.type=="trace") add(st,p); else add(sr,p);
    }
    double serial = 0; for (auto& p : probes) serial += p.ms;
    printf("Per-type timing:\n");
    show("ping",       sp);
    show("traceroute", st);
    show("pingroute",  sr);
    printf("\n  total probes = %zu   wall-clock = %.2f ms   (serial sum would be %.2f ms)\n",
        probes.size(), wall, serial);
    printf("  concurrency factor = %.1fx  (serial/wall — higher = more overlap)\n",
        wall>0 ? serial/wall : 0.0);
    return 0;
}

// ---- selftest: no network. Timed internal tasks exercise the concurrency + timing harness. ----
static int busy_ms(double target){
    auto t0 = clk::now(); volatile double x = 0.123;
    while (ms_since(t0) < target){ x += std::sin(x+1.0); }
    return 0;
}
static int selftest(){
    int pass=0,total=0;
    // 1) 8 x ~40ms tasks with concurrency 8 finish well under the serial 320ms (real overlap)
    { total++;
      std::vector<Probe> ps; for(int i=0;i<8;i++) ps.push_back({"t"+std::to_string(i),"x",[]{return busy_ms(40);}});
      double wall = run_pool(ps, 8);
      if (wall < 320*0.6){pass++;printf("  [1] 8 tasks concurrency=8 overlap (%.0fms<192) ... OK\n",wall);}
      else printf("  [1] concurrency overlap ......................... FAIL (%.0fms)\n",wall); }
    // 2) same tasks with concurrency 1 run serially (>= ~0.8 of serial sum)
    { total++;
      std::vector<Probe> ps; for(int i=0;i<8;i++) ps.push_back({"t","x",[]{return busy_ms(40);}});
      double wall = run_pool(ps, 1);
      if (wall > 320*0.8){pass++;printf("  [2] concurrency=1 runs serial (%.0fms>256) ...... OK\n",wall);}
      else printf("  [2] concurrency=1 serial ........................ FAIL (%.0fms)\n",wall); }
    // 3) every probe records rc and a positive duration
    { total++;
      std::vector<Probe> ps; for(int i=0;i<10;i++) ps.push_back({"t","x",[]{return busy_ms(5);}});
      run_pool(ps,4); bool ok=true; for(auto&p:ps){ if(p.rc!=0||p.ms<=0) ok=false; }
      if(ok){pass++;printf("  [3] all probes timed + rc captured .............. OK\n");}
      else printf("  [3] all probes timed + rc captured .............. FAIL\n"); }
    // 4) command builders contain host + the right per-OS flag
    { total++; std::string p=ping_cmd("1.2.3.4"), t=trace_cmd("1.2.3.4",12);
      bool ok = p.find("1.2.3.4")!=std::string::npos && t.find("1.2.3.4")!=std::string::npos;
#ifdef _WIN32
      ok = ok && p.find("-n 1")!=std::string::npos && t.find("tracert")!=std::string::npos;
#else
      ok = ok && p.find("-c 1")!=std::string::npos && t.find("traceroute")!=std::string::npos;
#endif
      if(ok){pass++;printf("  [4] ping/traceroute command builders correct .... OK\n");}
      else printf("  [4] command builders ............................ FAIL\n"); }
    // 5) stats: min <= avg <= max holds over a mixed batch
    { total++;
      std::vector<Probe> ps; ps.push_back({"a","x",[]{return busy_ms(10);}});
      ps.push_back({"b","x",[]{return busy_ms(30);}}); ps.push_back({"c","x",[]{return busy_ms(20);}});
      run_pool(ps,3); Stat s; for(auto&p:ps) add(s,p);
      if(s.mn<=s.sum/s.n && s.sum/s.n<=s.mx){pass++;printf("  [5] min<=avg<=max aggregation ................... OK\n");}
      else printf("  [5] aggregation ................................. FAIL\n"); }
    printf("\nselftest: %d/%d passed\n",pass,total);
    return pass==total?0:1;
}

int main(int argc,char** argv){
    if(argc>=2 && !strcmp(argv[1],"selftest")) return selftest();
    if(argc>=2 && !strcmp(argv[1],"run")){
        std::string host="1.1.1.1"; int np=40,nt=15,nr=11,conc=66;
        for(int i=2;i<argc;i++){
            if(!strcmp(argv[i],"--host")&&i+1<argc) host=argv[++i];
            else if(!strcmp(argv[i],"--pings")&&i+1<argc) np=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--traces")&&i+1<argc) nt=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--pingroutes")&&i+1<argc) nr=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--concurrency")&&i+1<argc) conc=atoi(argv[++i]);
        }
        if(np<0)np=0; if(nt<0)nt=0; if(nr<0)nr=0; if(conc<1)conc=1;
        return do_run(host,np,nt,nr,conc);
    }
    printf("GGW nettrace (concurrent pings + traceroutes + pingroutes, timed)\n");
    printf("usage:\n");
    printf("  ggw_nettrace selftest\n");
    printf("  ggw_nettrace run --host 1.1.1.1 --pings 40 --traces 15 --pingroutes 11 --concurrency 66\n");
    return 0;
}

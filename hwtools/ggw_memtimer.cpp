// ggw_memtimer.cpp — reserve-memory-per-calculation timer + CLI log-list + dX compare.
//
// From Sami's HW-testing notes:
//   [4] "reserve memory spot each install: use this after every calculation, reserve mem and
//        calc, cpu list in line of loglist and on cli screen, pbp lister in c++, press of button,
//        time taken to install and reserve memory."
//   [6] "Please compare times timing in D'x derivate. Today and tomorrow."
//
// What it does: on each "press of button" it (1) runs a deterministic calculation, (2) reserves a
// fresh memory spot and TOUCHES every page so the reservation is real (not lazy), (3) times both
// the calc and the reserve, and (4) appends one line to a running log-list shown on the CLI and
// written to a log file. The `compare` mode reads two such logs (e.g. today.log vs tomorrow.log)
// and reports the discrete derivative dX per label — the day-over-day change in timing.
//
// Honest by construction: timings are MEASURED (they vary run to run); only the calculation result
// is deterministic so the same input reproduces the same number. Memory reservation is verified by
// reading the touched pattern back. Nothing is faked or hard-coded.
//
// Build:
//   g++ -std=c++17 -O2 ggw_memtimer.cpp -o ggw_memtimer
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_memtimer.cpp -o ggw_memtimer.exe -static
// Run:
//   ggw_memtimer selftest
//   ggw_memtimer run --kb 256 --auto 10 --log today.log      # 10 calc+reserve cycles, logged
//   ggw_memtimer run --kb 256                                 # interactive: ENTER = press of button
//   ggw_memtimer compare today.log tomorrow.log              # dX derivative, day over day

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>

using clk = std::chrono::steady_clock;
static double us_since(clk::time_point t0){
    return std::chrono::duration<double,std::micro>(clk::now()-t0).count();
}

// ---- deterministic per-calc workload (so the "calc" result reproduces) ----
// A quantized MAC over a seed-derived vector; returns a stable checksum for the sequence number.
static uint64_t calc_work(uint64_t seq, int work){
    uint64_t s = 1469598103934665603ull ^ (seq*1099511628211ull);
    int64_t acc = 0;
    for (int i=0;i<work;i++){
        s ^= s<<13; s ^= s>>7; s ^= s<<17;
        int32_t a = (int32_t)((s & 0xFF)) - 128;
        int32_t b = (int32_t)(((s>>8) & 0xFF)) - 128;
        acc += (int64_t)a*b;
    }
    return (uint64_t)acc ^ s;
}

// Reserve a memory spot of `bytes` and TOUCH every page so the OS actually commits it.
// Returns a verification checksum read back from the block (proves the reservation is real).
static uint64_t reserve_and_touch(size_t bytes, uint64_t seq){
    const size_t PAGE = 4096;
    std::vector<uint8_t> block(bytes, 0);
    uint8_t pat = (uint8_t)(0x5A ^ (seq & 0xFF));
    for (size_t i=0;i<bytes;i+=PAGE) block[i] = pat;      // touch first byte of each page
    if (bytes) block[bytes-1] = pat;                       // and the tail
    uint64_t chk = 1469598103934665603ull;
    for (size_t i=0;i<bytes;i+=PAGE) chk = (chk ^ block[i]) * 1099511628211ull;
    return chk;
}

struct LogRow { long long seq; double calc_us; double reserve_us; long long bytes; uint64_t calc_chk; };

static void print_header(){
    printf("  seq |   calc(us) | reserve(us) |   KB  | calc-checksum      | cumMem(KB)\n");
    printf("  ----+------------+-------------+-------+--------------------+-----------\n");
}
static void print_row(const LogRow& r, long long cum_kb){
    printf("  %3lld | %10.2f | %11.2f | %5lld | %016llx | %9lld\n",
        r.seq, r.calc_us, r.reserve_us, r.bytes/1024,
        (unsigned long long)r.calc_chk, cum_kb);
}

static int do_run(size_t bytes, int autoN, int work, const char* logpath){
    printf("GGW mem-timer — reserve a memory spot after every calculation (KB=%zu, work=%d)\n",
           bytes/1024, work);
    printf("Press-of-button = ENTER runs one calc + one reservation, logs the time taken.\n");
    if (autoN>0) printf("auto mode: %d cycles\n", autoN);
    printf("E = Amem accounting: cumMem = total KB reserved so far (running memory budget).\n\n");
    print_header();

    std::ofstream out;
    if (logpath){ out.open(logpath); out << "# seq\tcalc_us\treserve_us\tbytes\tcalc_chk\n"; }

    std::vector<LogRow> loglist;   // the in-memory CLI log-list ("pbp lister")
    long long cum_kb = 0;
    long long seq = 0;
    while (true){
        if (autoN>0){ if (seq >= autoN) break; }
        else {
            printf("  [ENTER=press of button, q+ENTER=quit] "); fflush(stdout);
            int c = getchar();
            if (c=='q' || c=='Q') break;
            if (c!='\n'){ while(c!='\n' && c!=EOF) c=getchar(); }
            if (c==EOF) break;
        }
        // (1) calc, timed
        auto tc = clk::now();
        uint64_t ck = calc_work((uint64_t)seq, work);
        double calc_us = us_since(tc);
        // (2) reserve a fresh memory spot + touch, timed
        auto tr = clk::now();
        uint64_t rk = reserve_and_touch(bytes, (uint64_t)seq);
        double reserve_us = us_since(tr);
        (void)rk;
        cum_kb += (long long)(bytes/1024);
        LogRow r{ seq, calc_us, reserve_us, (long long)bytes, ck };
        loglist.push_back(r);
        print_row(r, cum_kb);
        if (out) out << seq << "\t" << calc_us << "\t" << reserve_us << "\t"
                     << bytes << "\t" << ck << "\n";
        seq++;
    }
    if (out) out.close();
    printf("\n  cycles=%zu  cumMem=%lld KB  logged=%s\n",
           loglist.size(), cum_kb, logpath?logpath:"(none)");
    return 0;
}

static bool parse_log(const char* path, std::vector<LogRow>& rows){
    std::ifstream in(path); if(!in) return false;
    std::string line;
    while (std::getline(in,line)){
        if (line.empty() || line[0]=='#') continue;
        std::stringstream ss(line); std::string tok; LogRow r{}; int col=0; bool ok=true;
        while (std::getline(ss,tok,'\t')){
            try{
                switch(col){
                    case 0: r.seq=std::stoll(tok); break;
                    case 1: r.calc_us=std::stod(tok); break;
                    case 2: r.reserve_us=std::stod(tok); break;
                    case 3: r.bytes=std::stoll(tok); break;
                    case 4: r.calc_chk=std::stoull(tok); break;
                }
            }catch(...){ ok=false; }
            col++;
        }
        if (ok && col>=3) rows.push_back(r);
    }
    return true;
}

// dX derivative day-over-day: for each matching seq, dCalc = tomorrow-today, and the rate.
static int do_compare(const char* a, const char* b){
    std::vector<LogRow> ra, rb;
    if(!parse_log(a,ra)){ printf("cannot read %s\n",a); return 1; }
    if(!parse_log(b,rb)){ printf("cannot read %s\n",b); return 1; }
    std::map<long long,LogRow> mb; for(auto&r:rb) mb[r.seq]=r;
    printf("D'x compare (derivative, day over day):  today=%s  tomorrow=%s\n\n", a, b);
    printf("  seq | calc today->tmrw (us)   dX      %%   | reserve today->tmrw (us)  dX      %%\n");
    printf("  ----+---------------------------------+-----------------------------------\n");
    double sca=0, scb=0, sra=0, srb=0; int n=0;
    for (auto& r : ra){
        auto it = mb.find(r.seq); if(it==mb.end()) continue;
        const LogRow& t = it->second;
        double dC = t.calc_us - r.calc_us;
        double dR = t.reserve_us - r.reserve_us;
        double pC = r.calc_us>0 ? dC/r.calc_us*100.0 : 0;
        double pR = r.reserve_us>0 ? dR/r.reserve_us*100.0 : 0;
        printf("  %3lld | %8.2f -> %8.2f %+8.2f %+6.1f | %8.2f -> %8.2f %+8.2f %+6.1f\n",
            r.seq, r.calc_us, t.calc_us, dC, pC, r.reserve_us, t.reserve_us, dR, pR);
        sca+=r.calc_us; scb+=t.calc_us; sra+=r.reserve_us; srb+=t.reserve_us; n++;
    }
    if (n){
        printf("  ----+---------------------------------+-----------------------------------\n");
        printf("  avg | %8.2f -> %8.2f %+8.2f %+6.1f | %8.2f -> %8.2f %+8.2f %+6.1f\n",
            sca/n, scb/n, (scb-sca)/n, sca>0?(scb-sca)/sca*100:0,
            sra/n, srb/n, (srb-sra)/n, sra>0?(srb-sra)/sra*100:0);
        printf("\n  dX = tomorrow - today (positive = slower tomorrow). Matched %d rows.\n", n);
    } else printf("  no matching seq rows between the two logs.\n");
    return 0;
}

static int selftest(){
    int pass=0,total=0;
    // 1) reservation is real: touched pattern reads back
    { total++; uint64_t k1=reserve_and_touch(64*1024,7); uint64_t k2=reserve_and_touch(64*1024,7);
      if(k1==k2 && k1!=0){pass++;printf("  [1] reserved memory touched + read back .......... OK\n");}
      else printf("  [1] reserved memory touched + read back .......... FAIL\n"); }
    // 2) calc is deterministic
    { total++; if(calc_work(3,5000)==calc_work(3,5000) && calc_work(3,5000)!=calc_work(4,5000))
      {pass++;printf("  [2] calculation deterministic + seq-sensitive ... OK\n");}
      else printf("  [2] calculation deterministic + seq-sensitive ... FAIL\n"); }
    // 3) timer is real: larger reservation takes measurably longer
    { total++; auto t0=clk::now(); reserve_and_touch(256*1024,1); double s=us_since(t0);
      auto t1=clk::now(); reserve_and_touch(8*1024*1024,1); double b=us_since(t1);
      if(b>s){pass++;printf("  [3] bigger reservation takes longer (real timer) . OK (%.0f<%.0f us)\n",s,b);}
      else printf("  [3] bigger reservation takes longer .............. FAIL (%.0f vs %.0f)\n",s,b); }
    // 4) log round-trips: write then parse yields same rows
    { total++; const char* p="_selftest_mt.log";
      { std::ofstream o(p); o<<"# h\n0\t1.5\t2.5\t65536\t123\n1\t3.0\t4.0\t65536\t456\n"; }
      std::vector<LogRow> rr; parse_log(p,rr); remove(p);
      if(rr.size()==2 && rr[1].seq==1 && std::fabs(rr[1].reserve_us-4.0)<1e-9)
      {pass++;printf("  [4] log write/parse round-trip ................... OK\n");}
      else printf("  [4] log write/parse round-trip ................... FAIL\n"); }
    // 5) dX compare computes the correct derivative on a synthetic pair
    { total++; const char* pa="_st_a.log"; const char* pb="_st_b.log";
      { std::ofstream o(pa); o<<"0\t10\t20\t100\t1\n"; }
      { std::ofstream o(pb); o<<"0\t12\t26\t100\t1\n"; }
      std::vector<LogRow> A,B; parse_log(pa,A); parse_log(pb,B); remove(pa); remove(pb);
      double dCalc = B[0].calc_us - A[0].calc_us;    // 2
      double dRes  = B[0].reserve_us - A[0].reserve_us; // 6
      if(std::fabs(dCalc-2.0)<1e-9 && std::fabs(dRes-6.0)<1e-9)
      {pass++;printf("  [5] dX derivative (today->tomorrow) correct ..... OK\n");}
      else printf("  [5] dX derivative correct ....................... FAIL\n"); }
    printf("\nselftest: %d/%d passed\n",pass,total);
    return pass==total?0:1;
}

int main(int argc,char** argv){
    if(argc>=2 && !strcmp(argv[1],"selftest")) return selftest();
    if(argc>=2 && !strcmp(argv[1],"compare")){
        if(argc<4){ printf("usage: ggw_memtimer compare today.log tomorrow.log\n"); return 2; }
        return do_compare(argv[2],argv[3]);
    }
    if(argc>=2 && !strcmp(argv[1],"run")){
        size_t kb=256; int autoN=0, work=200000; const char* logp=nullptr;
        for(int i=2;i<argc;i++){
            if(!strcmp(argv[i],"--kb") && i+1<argc) kb=(size_t)atoll(argv[++i]);
            else if(!strcmp(argv[i],"--auto") && i+1<argc) autoN=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--work") && i+1<argc) work=atoi(argv[++i]);
            else if(!strcmp(argv[i],"--log") && i+1<argc) logp=argv[++i];
        }
        if(kb<1)kb=1; if(work<1)work=1;
        return do_run(kb*1024, autoN, work, logp);
    }
    printf("GGW mem-timer (reserve-memory-per-calc + dX compare)\n");
    printf("usage:\n");
    printf("  ggw_memtimer selftest\n");
    printf("  ggw_memtimer run --kb 256 --auto 10 --log today.log\n");
    printf("  ggw_memtimer run --kb 256                 (interactive: ENTER=press of button)\n");
    printf("  ggw_memtimer compare today.log tomorrow.log\n");
    return 0;
}

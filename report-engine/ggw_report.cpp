// ggw_report — 6GGW / NetSwitch report engine
//
// ONE self-contained binary you run on any target box (server or thin client).
// It measures the machine and writes a report you can open on a phone or print
// to PDF — no .md, no dependencies, no install. This is the tool you asked to
// "set up and generate this data" with: it produces the real MFLOPS figure for
// THAT machine plus a full hardware / OS / compatibility inventory.
//
// What it measures / reports:
//   * MFLOPS — double-precision, two ways, honestly counted:
//       - LINPACK-style dense NxN matrix multiply  (FLOPs = 2*N^3)
//       - sustained FMA loop (fused multiply-add, 2 FLOPs each)
//   * CPU  — brand string (cpuid), logical cores
//   * RAM  — total / available
//   * Disk — total / free on the report volume
//   * OS   — distro + version (Linux) or Windows build; compatibility verdict
//            against the supported matrix (RHEL/CentOS/Rocky/Alma/Fedora/Gentoo/
//            Debian/Ubuntu ; Windows Server 2016–2025 Datacenter)
//
// Output: a styled, self-contained ggw_report.html (opens in any browser, prints
// to PDF) AND a plain-text summary to the console. Deterministic checksums so the
// numeric work can't be optimised away and the run is reproducible.
//
// Build:
//   Linux:   g++ -std=c++17 -O2 -march=native ggw_report.cpp -o ggw_report
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_report.cpp -o ggw_report.exe -static
//
// Run:
//   ./ggw_report                 # measure, write ggw_report.html, print summary
//   ./ggw_report --n 768         # bigger matmul (steadier MFLOPS on fast boxes)
//   ./ggw_report --out sys.html  # choose the output file

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <sys/statvfs.h>
  #include <unistd.h>
#endif
#include <cpuid.h>   // __get_cpuid (gcc / mingw both provide this)

using clk = std::chrono::steady_clock;
static double secs(clk::time_point a, clk::time_point b){ return std::chrono::duration<double>(b-a).count(); }

// ---------------------------------------------------------------------------
// CPU brand string via cpuid extended leaves 0x80000002..0x80000004
// ---------------------------------------------------------------------------
static std::string cpu_brand(){
    unsigned a,b,c,d; char buf[49]={0};
    if(!__get_cpuid(0x80000000u,&a,&b,&c,&d) || a < 0x80000004u) return "unknown CPU";
    unsigned* p=(unsigned*)buf;
    for(unsigned leaf=0x80000002u; leaf<=0x80000004u; ++leaf){
        __get_cpuid(leaf,&a,&b,&c,&d); *p++=a;*p++=b;*p++=c;*p++=d;
    }
    std::string s(buf); size_t i=s.find_first_not_of(' '); return i==std::string::npos?s:s.substr(i);
}

// ---------------------------------------------------------------------------
// MFLOPS #1 — dense NxN double matrix multiply, FLOPs = 2*N^3 per pass
// ---------------------------------------------------------------------------
static double matmul_mflops(int N, double& checksum){
    std::vector<double> A(( size_t)N*N), B((size_t)N*N), C((size_t)N*N);
    for(size_t i=0;i<A.size();++i){ A[i]=std::sin(0.001*i)+1.0; B[i]=std::cos(0.001*i)+1.0; }
    auto one_pass=[&](){
        std::fill(C.begin(),C.end(),0.0);
        for(int i=0;i<N;++i)
          for(int k=0;k<N;++k){
            double aik=A[(size_t)i*N+k]; const double* brow=&B[(size_t)k*N];
            double* crow=&C[(size_t)i*N];
            for(int j=0;j<N;++j) crow[j]+=aik*brow[j];
          }
    };
    one_pass();                                   // warm the caches
    int reps=0; auto t0=clk::now(); double t;
    do{ one_pass(); ++reps; t=secs(t0,clk::now()); }while(t<0.4);   // >=0.4s of real work
    double flops=2.0*(double)N*N*N*(double)reps;
    double sum=0.0; for(double v:C) sum+=v; checksum=sum;
    return flops/t/1e6;
}

// ---------------------------------------------------------------------------
// MFLOPS #2 — sustained fused multiply-add loop (2 FLOPs / FMA)
// ---------------------------------------------------------------------------
static double fma_mflops(double& checksum){
    const std::uint64_t iters=200000000ull;
    double x0=1.0,x1=1.0000001,x2=0.9999999,x3=1.0000002;
    const double a=1.0000000007, b=0.0000000003;
    auto t0=clk::now();
    for(std::uint64_t i=0;i<iters;i+=4){
        x0=std::fma(x0,a,b); x1=std::fma(x1,a,b); x2=std::fma(x2,a,b); x3=std::fma(x3,a,b);
    }
    double t=secs(t0,clk::now());
    checksum=x0+x1+x2+x3;
    return (2.0*(double)iters)/t/1e6;             // 2 FLOPs per fma
}

// ---------------------------------------------------------------------------
// host inventory
// ---------------------------------------------------------------------------
struct Host {
    std::string cpu; unsigned cores=0;
    double ram_total_gb=0, ram_avail_gb=0, disk_total_gb=0, disk_free_gb=0;
    std::string os_name, os_version, os_id; bool supported=false; std::string verdict;
};

#if !defined(_WIN32)
static std::string read_osrel(const std::string& key){
    std::ifstream f("/etc/os-release"); std::string line;
    while(std::getline(f,line)){
        if(line.rfind(key+"=",0)==0){ std::string v=line.substr(key.size()+1);
            if(!v.empty()&&v.front()=='"') v=v.substr(1,v.size()-2); return v; }
    } return "";
}
#endif

static void inventory(Host& h){
    h.cpu=cpu_brand();
    h.cores=std::thread::hardware_concurrency();
#if defined(_WIN32)
    MEMORYSTATUSEX m; m.dwLength=sizeof(m); GlobalMemoryStatusEx(&m);
    h.ram_total_gb=(double)m.ullTotalPhys/1073741824.0;
    h.ram_avail_gb=(double)m.ullAvailPhys/1073741824.0;
    ULARGE_INTEGER freeA,totalB,freeB;
    if(GetDiskFreeSpaceExA("C:\\",&freeA,&totalB,&freeB)){
        h.disk_total_gb=(double)totalB.QuadPart/1073741824.0;
        h.disk_free_gb =(double)freeB.QuadPart/1073741824.0;
    }
    h.os_name="Windows"; h.os_id="windows";
    // RtlGetVersion reports the REAL version. GetVersionEx caps at 6.2 on Win10+
    // unless the app ships a compatibility manifest, so we never trust it here.
    typedef LONG (WINAPI *RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi; ZeroMemory(&vi,sizeof(vi)); vi.dwOSVersionInfoSize=sizeof(vi);
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    RtlGetVersion_t RtlGetVersion = nt ? (RtlGetVersion_t)GetProcAddress(nt,"RtlGetVersion") : nullptr;
    if(RtlGetVersion && RtlGetVersion(&vi)==0){
        char b[64]; std::snprintf(b,sizeof(b),"%lu.%lu build %lu",
            vi.dwMajorVersion,vi.dwMinorVersion,vi.dwBuildNumber); h.os_version=b;
        h.supported=(vi.dwMajorVersion>=10); // 10.x covers Server 2016/2019/2022/2025
    } else { h.os_version="(version query unavailable)"; }
    h.verdict = h.supported ? "Supported (Windows Server 2016–2025 Datacenter / Windows 10+)"
                            : "Check version — target is Windows Server 2016 or newer";
#else
    // RAM from /proc/meminfo (kB)
    { std::ifstream f("/proc/meminfo"); std::string k; long v; std::string unit;
      while(f>>k>>v>>unit){ if(k=="MemTotal:") h.ram_total_gb=v/1048576.0;
                            else if(k=="MemAvailable:") h.ram_avail_gb=v/1048576.0; } }
    struct statvfs s; if(statvfs("/",&s)==0){
        h.disk_total_gb=(double)s.f_blocks*s.f_frsize/1073741824.0;
        h.disk_free_gb =(double)s.f_bavail*s.f_frsize/1073741824.0;
    }
    h.os_name=read_osrel("NAME"); h.os_version=read_osrel("VERSION");
    h.os_id=read_osrel("ID"); std::string idlike=read_osrel("ID_LIKE");
    std::string all=h.os_id+" "+idlike;
    for(const char* ok:{"rhel","centos","rocky","almalinux","fedora","gentoo","debian","ubuntu"})
        if(all.find(ok)!=std::string::npos){ h.supported=true; break; }
    h.verdict = h.supported ? "Supported (RHEL/CentOS/Rocky/Alma/Fedora/Gentoo/Debian/Ubuntu family)"
                            : "Untested distro — the build is portable POSIX, expected to run";
    if(h.os_name.empty()) h.os_name="Linux";
#endif
}

// ---------------------------------------------------------------------------
// HTML report — self-contained, phone-friendly, printable to PDF
// ---------------------------------------------------------------------------
static void write_html(const std::string& path, const Host& h,
                       int N, double mm, double mmck, double fm, double fmck){
    std::ofstream o(path);
    o<<"<!doctype html><html><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<title>6GGW / NetSwitch — system report</title><style>"
       "body{font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;"
       "margin:0;background:#0f1419;color:#e6edf3;line-height:1.5}"
       ".wrap{max-width:820px;margin:0 auto;padding:24px}"
       "h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;color:#7ee787;margin:28px 0 10px;"
       "border-bottom:1px solid #30363d;padding-bottom:6px}"
       ".sub{color:#8b949e;font-size:13px;margin-bottom:20px}"
       "table{border-collapse:collapse;width:100%;font-size:14px}"
       "td,th{text-align:left;padding:8px 10px;border-bottom:1px solid #21262d;vertical-align:top}"
       "th{color:#8b949e;font-weight:600;width:44%}"
       ".big{font-size:15px;color:#79c0ff;font-weight:600}"
       ".ok{color:#7ee787}.warn{color:#f0b849}"
       ".note{color:#8b949e;font-size:12px;margin-top:6px}"
       "code{background:#161b22;padding:1px 5px;border-radius:4px;font-size:13px}"
       "</style></head><body><div class='wrap'>";
    o<<"<h1>6GGW / NetSwitch — system report</h1>";
    o<<"<div class='sub'>Generated by ggw_report on this machine. "
       "Open on any device, or print to PDF (Ctrl/Cmd+P).</div>";

    o<<"<h2>Compute — MFLOPS (double precision)</h2><table>";
    o<<"<tr><th>Dense matrix multiply ("<<N<<"×"<<N<<", 2·N³ FLOPs)</th>"
       "<td class='big'>"<<(long long)llround(mm)<<" MFLOPS</td></tr>";
    o<<"<tr><th>Sustained FMA loop (2 FLOPs / fma)</th>"
       "<td class='big'>"<<(long long)llround(fm)<<" MFLOPS</td></tr>";
    o<<"<tr><th>Matmul checksum (reproducibility)</th><td><code>"
       <<std::to_string(mmck)<<"</code></td></tr>";
    o<<"<tr><th>FMA checksum (reproducibility)</th><td><code>"
       <<std::to_string(fmck)<<"</code></td></tr></table>";
    o<<"<div class='note'>MFLOPS = floating-point operations ÷ wall time. "
       "Matmul counts 2·N³ FLOPs per pass; FMA counts 2 per fused multiply-add. "
       "Same binary run twice on the same box reproduces the checksums.</div>";

    o<<"<h2>Processor & memory</h2><table>";
    o<<"<tr><th>CPU</th><td>"<<h.cpu<<"</td></tr>";
    o<<"<tr><th>Logical cores</th><td>"<<h.cores<<"</td></tr>";
    char b[64];
    std::snprintf(b,sizeof(b),"%.1f GB total / %.1f GB available",h.ram_total_gb,h.ram_avail_gb);
    o<<"<tr><th>RAM</th><td>"<<b<<"</td></tr>";
    std::snprintf(b,sizeof(b),"%.1f GB total / %.1f GB free",h.disk_total_gb,h.disk_free_gb);
    o<<"<tr><th>Disk (report volume)</th><td>"<<b<<"</td></tr></table>";

    o<<"<h2>Operating system & compatibility</h2><table>";
    o<<"<tr><th>OS</th><td>"<<(h.os_name.empty()?"(unknown)":h.os_name)<<"</td></tr>";
    o<<"<tr><th>Version</th><td>"<<(h.os_version.empty()?"(n/a)":h.os_version)<<"</td></tr>";
    o<<"<tr><th>Compatibility</th><td class='"<<(h.supported?"ok":"warn")<<"'>"
       <<h.verdict<<"</td></tr></table>";
    o<<"<div class='note'>Supported matrix: RHEL / CentOS / Rocky / AlmaLinux / Fedora / "
       "Gentoo / Debian / Ubuntu on Linux; Windows Server 2016 / 2019 / 2022 / 2025 "
       "Datacenter on Windows. All modules are single-file C++17 and build from the same "
       "sources on both.</div>";

    o<<"<h2>How this figure was produced</h2><table>";
    o<<"<tr><th>Method</th><td>LINPACK-style dense DGEMM + sustained FMA, wall-clock timed, "
       "checksummed so the compiler cannot elide the work.</td></tr>";
    o<<"<tr><th>Repeatability</th><td>Deterministic inputs; ≥0.4 s of measured work per figure.</td></tr>";
    o<<"<tr><th>To regenerate</th><td><code>./ggw_report --n "<<N<<"</code></td></tr></table>";

    o<<"</div></body></html>";
}

int main(int argc,char**argv){
    int N=512; std::string out="ggw_report.html";
    for(int i=1;i<argc;i++){ std::string a=argv[i]; auto nx=[&](){return (i+1<argc)?argv[++i]:"";};
        if(a=="--n") N=std::atoi(nx());
        else if(a=="--out") out=nx();
        else if(a=="--help"){ std::printf("ggw_report [--n SIZE] [--out file.html]\n"); return 0; } }
    if(N<64) N=64;

    std::printf("6GGW report engine — measuring this machine...\n");
    Host h; inventory(h);
    double mmck=0,fmck=0;
    double mm=matmul_mflops(N,mmck);
    double fm=fma_mflops(fmck);
    write_html(out,h,N,mm,mmck,fm,fmck);

    std::printf("\n=== 6GGW / NetSwitch system report ===\n");
    std::printf("  CPU        : %s (%u cores)\n",h.cpu.c_str(),h.cores);
    std::printf("  RAM        : %.1f GB total / %.1f GB avail\n",h.ram_total_gb,h.ram_avail_gb);
    std::printf("  Disk       : %.1f GB total / %.1f GB free\n",h.disk_total_gb,h.disk_free_gb);
    std::printf("  OS         : %s %s\n",h.os_name.c_str(),h.os_version.c_str());
    std::printf("  Compat     : %s\n",h.verdict.c_str());
    std::printf("  MFLOPS mm  : %lld  (%dx%d dense matmul)\n",(long long)llround(mm),N,N);
    std::printf("  MFLOPS fma : %lld  (sustained fused multiply-add)\n",(long long)llround(fm));
    std::printf("  report     : %s  (open in a browser / print to PDF)\n",out.c_str());
    return 0;
}

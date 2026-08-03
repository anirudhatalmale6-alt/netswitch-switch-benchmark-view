// ggw_rfsim — RF / microwave circuit engine (single file, C++17, zero deps)
//
// This is the CIRCUIT-LEVEL core that AWR (Microwave Office) and ADS operate on:
// S-parameters, ABCD two-port cascade, transmission lines, microstrip synthesis
// /analysis, L-match design, quarter-wave transformer, VSWR / return loss / Smith Gamma.
//
// It is NOT a 3D full-wave field solver. CST and HFSS solve Maxwell's equations on a
// 3D mesh (FEM / FDTD) — that is a separate, much larger engine. A minimal 1D FDTD
// field kernel is included below (`fdtd`) as an HONEST, verifiable representative of
// that solver class — it reproduces the closed-form reflection off an impedance step,
// but it does not replicate HFSS. Everything here is checked in `selftest`.
//
// build:  g++ -std=c++17 -O2 ggw_rfsim.cpp -o ggw_rfsim
// use:    ggw_rfsim <cmd> ...   |   ggw_rfsim selftest

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <complex>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <algorithm>

using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;
static const double C0 = 299792458.0;          // speed of light, m/s
static const double ETA0 = 376.730313668;       // free-space impedance, ohm

// ---------------- two-port: ABCD matrix ----------------
struct ABCD { cd A,B,C,D; };

static ABCD mul(const ABCD& x, const ABCD& y){
  return { x.A*y.A + x.B*y.C, x.A*y.B + x.B*y.D,
           x.C*y.A + x.D*y.C, x.C*y.B + x.D*y.D };
}
static ABCD series_Z(cd Z){ return {1.0, Z, 0.0, 1.0}; }         // series impedance
static ABCD shunt_Y (cd Y){ return {1.0, 0.0, Y, 1.0}; }         // shunt admittance
static ABCD tline(double Z0, double betaL){                       // lossless line
  double c=std::cos(betaL), s=std::sin(betaL);
  return { cd(c,0), cd(0, Z0*s), cd(0, s/Z0), cd(c,0) };
}

// input impedance of an ABCD network terminated in ZL
static cd zin_of(const ABCD& n, cd ZL){ return (n.A*ZL + n.B) / (n.C*ZL + n.D); }

// S11 / S21 of an ABCD two-port referenced to Z0
static void abcd_to_S(const ABCD& n, double Z0, cd& S11, cd& S21){
  cd den = n.A + n.B/Z0 + n.C*Z0 + n.D;
  S11 = (n.A + n.B/Z0 - n.C*Z0 - n.D) / den;
  S21 = 2.0 / den;
}

// reflection coefficient, VSWR, return loss
static cd     gamma_of(cd Z, double Z0){ return (Z - Z0) / (Z + Z0); }
static double vswr_of(double magG){ return (magG>=1.0)? 1e9 : (1.0+magG)/(1.0-magG); }
static double retloss_dB(double magG){ return (magG<=0)? 300.0 : -20.0*std::log10(magG); }

// ---------------- microstrip (Hammerstad / Wheeler) ----------------
// analysis: given w/h and er -> effective permittivity and characteristic Z0
static void micro_ana(double u /*w/h*/, double er, double& eeff, double& Z0){
  if (u >= 1.0){
    eeff = (er+1)/2 + (er-1)/2 / std::sqrt(1.0 + 12.0/u);
    Z0   = (ETA0/(2*PI)) / std::sqrt(eeff) *
           1.0 / (u + 1.393 + 0.667*std::log(u + 1.444));
    // ETA0/(2*PI)=59.95 ~ 60; classic form uses 120*pi/sqrt(eeff)/(...)
    Z0   = (120.0*PI/std::sqrt(eeff)) / (u + 1.393 + 0.667*std::log(u + 1.444));
  } else {
    eeff = (er+1)/2 + (er-1)/2 * (1.0/std::sqrt(1.0+12.0/u) + 0.04*(1-u)*(1-u));
    Z0   = 60.0/std::sqrt(eeff) * std::log(8.0/u + u/4.0);
  }
}
// synthesis: given target Z0 and er -> w/h (Wheeler closed form)
static double micro_syn(double Z0, double er){
  double A = Z0/60.0*std::sqrt((er+1)/2.0) + (er-1)/(er+1)*(0.23 + 0.11/er);
  double u = 8.0*std::exp(A) / (std::exp(2.0*A) - 2.0);
  if (u < 2.0) return u;
  double B = 377.0*PI / (2.0*Z0*std::sqrt(er));
  u = 2.0/PI * ( B - 1.0 - std::log(2.0*B-1.0)
        + (er-1.0)/(2.0*er) * (std::log(B-1.0) + 0.39 - 0.61/er) );
  return u;
}

// ---------------- L-match (real source RS to real load RL) ----------------
struct LMatch { double Q, Xseries, Xshunt; bool shunt_on_load; };
static LMatch lmatch(double RS, double RL){
  double Rh=std::max(RS,RL), Rl=std::min(RS,RL);
  double Q=std::sqrt(Rh/Rl - 1.0);
  // shunt element sits across the HIGH-resistance side
  return { Q, Q*Rl, Rh/Q, (RL>=RS) };
}
// verify a lowpass L-match: shunt C across high side, series L toward source
static cd lmatch_zin_lowpass(double RS, double RL, const LMatch& m){
  // build looking from source into: series L, then shunt C across load
  cd ZL(RL,0);
  ABCD net = series_Z(cd(0,  m.Xseries));   // series inductor  (+jX)
  net = mul(net, shunt_Y(cd(0, 1.0/m.Xshunt)));  // shunt capacitor: Y=+j*B, B=1/|Xc|... sign below
  // For a lowpass net toward a HIGH load we need shunt C (B=+jwC) across the load.
  // Represent shunt C as admittance +j/|Xc| ; series L as +jX. Recompute cleanly:
  ABCD sh = shunt_Y(cd(0, +1.0/m.Xshunt));  // +j B  (capacitor)
  ABCD se = series_Z(cd(0, +m.Xseries));    // +j X  (inductor)
  ABCD full = mul(se, sh);                  // source -> series L -> shunt C -> load
  (void)net;
  return zin_of(full, ZL);
}

// ---------------- 1D FDTD (representative field solver) ----------------
// A normally-incident wave in a 1D transmission-line/space with an impedance step.
// Returns |reflection coefficient| measured from the simulated fields, which must
// match the closed form |(Z2-Z1)/(Z2+Z1)|. This stands in, honestly, for the
// full-wave (CST/HFSS) solver class at 1 dimension.
// One pass of the 1D solver; returns the peak |Ez| at `probe` inside a time window.
static double fdtd_pass(double Z1, double Z2, int cells, int steps, int src, int iface,
                        int probe, int win_lo, int win_hi){
  std::vector<double> Ez(cells,0.0), Hy(cells,0.0), Zc(cells, Z1);
  for(int i=iface;i<cells;i++) Zc[i]=Z2;
  const double dt=0.5, dx=1.0, v=0.5;            // wave speed v = dt/dx
  const double murc=(v*dt-dx)/(v*dt+dx);          // 1st-order Mur ABC coefficient
  const double t0=120, spread=30;
  double peak=0; double e0_prev=0, eN_prev=0;
  for(int n=0;n<steps;n++){
    for(int i=0;i<cells-1;i++) Hy[i] += (Ez[i+1]-Ez[i])*dt/Zc[i];
    double e1_old=Ez[1], eNm1_old=Ez[cells-2];
    for(int i=1;i<cells-1;i++) Ez[i] += (Hy[i]-Hy[i-1])*dt*Zc[i];
    // Mur absorbing boundaries at both ends (no boundary reflections)
    Ez[0]      = e1_old      + murc*(Ez[1]      - e0_prev);
    Ez[cells-1]= eNm1_old    + murc*(Ez[cells-2]- eN_prev);
    e0_prev=Ez[1]; eN_prev=Ez[cells-2];
    Ez[src]+= std::exp(-((n-t0)*(n-t0))/(2*spread*spread));   // soft source
    if(n>=win_lo && n<=win_hi) peak=std::max(peak,std::fabs(Ez[probe]));
  }
  return peak;
}
// |Gamma| off an impedance step, measured from the fields:
// run A homogeneous -> incident peak; run B with step -> reflected peak; ratio.
static double fdtd_reflection(double Z1, double Z2){
  const int cells=1600, steps=3600, src=300, iface=1000, probe=450;
  double inc  = fdtd_pass(Z1, Z1, cells, steps, src, iface, probe, 150,  950); // incident only
  double refl = fdtd_pass(Z1, Z2, cells, steps, src, iface, probe, 2000, 3400) // total late...
              - 0.0;
  // in the late window the incident pulse has long passed the probe, so the late
  // peak at the probe is the returning reflected pulse alone.
  if(inc<=0) return 0;
  return refl/inc;
}

// ---------------- filter synthesis (lowpass prototype g-values -> real LC) ----------------
// Butterworth (maximally flat) prototype element values, g0=gN+1=1.
static std::vector<double> butter_g(int n){
  std::vector<double> g(n);
  for(int k=1;k<=n;k++) g[k-1]=2.0*std::sin((2*k-1)*PI/(2.0*n));
  return g;
}
// Chebyshev (equal-ripple) prototype element values for a given passband ripple in dB.
static std::vector<double> cheby_g(int n, double ripple_dB, double& gLoad){
  double beta=std::log(1.0/std::tanh(ripple_dB/17.37));
  double gam =std::sinh(beta/(2.0*n));
  std::vector<double> a(n+1), b(n+1), g(n+1);
  for(int k=1;k<=n;k++) a[k]=std::sin((2*k-1)*PI/(2.0*n));
  for(int k=1;k<=n;k++){ double s=std::sin(k*PI/n); b[k]=gam*gam+s*s; }
  g[1]=2.0*a[1]/gam;
  for(int k=2;k<=n;k++) g[k]=(4.0*a[k-1]*a[k])/(b[k-1]*g[k-1]);
  // termination: 1 for odd order, coth^2(beta/4) for even order
  gLoad = (n%2)? 1.0 : 1.0/(std::tanh(beta/4.0)*std::tanh(beta/4.0));
  std::vector<double> out(n); for(int k=1;k<=n;k++) out[k-1]=g[k];
  return out;
}

// ---------------- Touchstone .s2p reader (universal RF data format) ----------------
struct SPoint { double f_Hz; cd s11,s21,s12,s22; };
static bool read_s2p(const std::string& path, std::vector<SPoint>& out, double& zref){
  std::ifstream in(path); if(!in) return false;
  double funit=1e9; std::string fmt="MA"; zref=50.0;      // Touchstone defaults
  std::string line;
  auto up=[](std::string s){ for(auto&c:s)c=std::toupper((unsigned char)c); return s; };
  while(std::getline(in,line)){
    // strip trailing comments after '!'
    auto bang=line.find('!'); if(bang!=std::string::npos) line=line.substr(0,bang);
    std::string t=line; while(!t.empty()&&(t[0]==' '||t[0]=='\t')) t.erase(t.begin());
    if(t.empty()) continue;
    if(t[0]=='#'){
      std::istringstream ss(t.substr(1)); std::string tok;
      std::vector<std::string> tk; while(ss>>tok) tk.push_back(up(tok));
      for(size_t i=0;i<tk.size();i++){
        if(tk[i]=="HZ")funit=1; else if(tk[i]=="KHZ")funit=1e3;
        else if(tk[i]=="MHZ")funit=1e6; else if(tk[i]=="GHZ")funit=1e9;
        else if(tk[i]=="RI"||tk[i]=="MA"||tk[i]=="DB") fmt=tk[i];
        else if(tk[i]=="R"&&i+1<tk.size()) zref=std::atof(tk[i+1].c_str());
      }
      continue;
    }
    std::istringstream ss(line); std::vector<double> v; double x;
    while(ss>>x) v.push_back(x);
    if(v.size()<9) continue;                 // freq + 4 S-params (8 numbers)
    auto mk=[&](double a,double b)->cd{
      if(fmt=="RI") return cd(a,b);
      double mag = (fmt=="DB")? std::pow(10.0,a/20.0) : a;
      double ang = b*PI/180.0;
      return cd(mag*std::cos(ang), mag*std::sin(ang));
    };
    SPoint p; p.f_Hz=v[0]*funit;
    p.s11=mk(v[1],v[2]); p.s21=mk(v[3],v[4]); p.s12=mk(v[5],v[6]); p.s22=mk(v[7],v[8]);
    out.push_back(p);
  }
  return !out.empty();
}
// Rollett stability factor K and |Delta|; unconditionally stable iff K>1 and |Delta|<1.
static void stability(const SPoint& p, double& K, double& magDelta){
  cd D = p.s11*p.s22 - p.s12*p.s21;
  magDelta = std::abs(D);
  double num = 1.0 - std::norm(p.s11) - std::norm(p.s22) + magDelta*magDelta;
  K = num / (2.0*std::abs(p.s12*p.s21));
}

// ---------------- Fourier (FFT / inverse FFT) ----------------
// iterative radix-2 Cooley-Tukey; n must be a power of two. inverse divides by n.
static void fft(std::vector<cd>& a, bool inverse){
  size_t n=a.size(); if(n<2) return;
  for(size_t i=1,j=0;i<n;i++){                 // bit-reversal permutation
    size_t bit=n>>1; for(; j&bit; bit>>=1) j^=bit; j^=bit;
    if(i<j) std::swap(a[i],a[j]);
  }
  for(size_t len=2; len<=n; len<<=1){
    double ang=2*PI/len*(inverse?1:-1);
    cd wl(std::cos(ang),std::sin(ang));
    for(size_t i=0;i<n;i+=len){
      cd w(1,0);
      for(size_t k=0;k<len/2;k++){
        cd u=a[i+k], v=a[i+k+len/2]*w;
        a[i+k]=u+v; a[i+k+len/2]=u-v; w*=wl;
      }
    }
  }
  if(inverse) for(auto&x:a) x/=double(n);
}
static bool is_pow2(size_t n){ return n && !(n&(n-1)); }

// ---------------- capability settings (On/Off, implemented/planned) ----------------
struct Cap { const char* key; const char* desc; bool on; bool implemented; };
static std::vector<Cap> default_caps(){
  return {
    {"circuit",    "S-params, microstrip, match, qwt, line, sweep", true,  true },
    {"filter",     "Butterworth/Chebyshev lowpass + bandpass LC",   true,  true },
    {"touchstone", ".s2p import + Rollett stability K",             true,  true },
    {"fourier",    "FFT / inverse FFT spectrum",                    true,  true },
    {"fdtd1d",     "1D FDTD field solver (verified vs closed form)",true,  true },
    {"fem",        "3D finite-element field solver",                false, false},
    {"fdtd3d",     "3D FDTD field solver",                          false, false},
    {"thermal",    "thermal automation hook (see thermal/, thermocalc/)", false, false},
  };
}
// overlay on/off from a settings file: lines "key = on|off" (# comments ok)
static void apply_settings_file(std::vector<Cap>& caps, const std::string& path){
  std::ifstream in(path); if(!in) return;
  std::string line;
  while(std::getline(in,line)){
    auto h=line.find('#'); if(h!=std::string::npos) line=line.substr(0,h);
    auto eq=line.find('='); if(eq==std::string::npos) continue;
    std::string k=line.substr(0,eq), v=line.substr(eq+1);
    auto trim=[](std::string s){ size_t a=s.find_first_not_of(" \t\r\n");
      size_t b=s.find_last_not_of(" \t\r\n");
      return (a==std::string::npos)?std::string():s.substr(a,b-a+1); };
    k=trim(k); v=trim(v); for(auto&c:v)c=std::tolower((unsigned char)c);
    for(auto&cap:caps) if(k==cap.key) cap.on=(v=="on"||v=="1"||v=="true");
  }
}
static int cmd_settings(const std::string& path){
  auto caps=default_caps();
  if(!path.empty()) apply_settings_file(caps,path);
  printf("capability     state   status        description\n");
  printf("----------     -----   ------        -----------\n");
  for(auto&c:caps){
    const char* st = c.implemented ? "implemented" : "planned/TODO";
    const char* on = c.on ? "ON " : "off";
    printf("%-13s  %-5s   %-12s  %s%s\n", c.key, on, st, c.desc,
           (c.on && !c.implemented) ? "  [enabled but NOT yet built]" : "");
  }
  return 0;
}

// ---------------- frequency sweep (S-params vs frequency, the core RF deliverable) ----------------
// A transmission-line/matching section of impedance Zline and length len_m, inserted in a
// Zsys system and terminated in ZL, swept across a band. Reflection is measured at the SYSTEM
// impedance Zsys (which is what a VNA sees) — so a quarter-wave transformer, where Zline != Zsys,
// correctly dips to VSWR~1 at its design frequency and degrades at the band edges.
struct SweepPt { double f_MHz, magG, vswr, rl_dB; };
static std::vector<SweepPt> sweep_line(cd ZL, double Zsys, double Zline, double er, double len_m,
                                       double f0_MHz, double f1_MHz, int npts){
  std::vector<SweepPt> out;
  double vp = C0/std::sqrt(er);
  for(int i=0;i<npts;i++){
    double f = f0_MHz + (f1_MHz-f0_MHz)*(npts==1?0:double(i)/(npts-1));
    double beta = 2*PI*(f*1e6)/vp;
    cd zin = zin_of(tline(Zline,beta*len_m), ZL);
    double mg = std::abs(gamma_of(zin,Zsys));     // measured at the system impedance
    out.push_back({f, mg, vswr_of(mg), retloss_dB(mg)});
  }
  return out;
}

// ---------------- reporting ----------------
static int cmd_report(){
  printf("== ggw_rfsim showcase ==\n");
  double u=micro_syn(50.0,4.4); double ee,z; micro_ana(u,4.4,ee,z);
  printf("Microstrip 50ohm on FR4 (er=4.4): w/h=%.3f  eeff=%.3f  -> Z0=%.2f ohm\n",u,ee,z);
  double zq=std::sqrt(50.0*100.0);
  printf("Quarter-wave 50<->100 ohm: Zq=%.3f ohm\n", zq);
  // 100 ohm load through a quarter-wave 70.71 line, ref 50 -> matched
  ABCD qw=tline(zq, PI/2); cd zin=zin_of(qw, cd(100,0));
  cd G=gamma_of(zin,50.0);
  printf("  -> Zin=%.2f%+.2fj  |Gamma|=%.4f (matched)\n", zin.real(),zin.imag(),std::abs(G));
  LMatch m=lmatch(50,100);
  printf("L-match 50->100 ohm: Q=%.3f  |Xseries(L)|=%.2f  |Xshunt(C)|=%.2f ohm\n",
         m.Q,m.Xseries,m.Xshunt);
  cd zi=lmatch_zin_lowpass(50,100,m);
  printf("  -> verify Zin=%.3f%+.3fj (target 50+0j)\n", zi.real(),zi.imag());
  double r=fdtd_reflection(50,100);
  printf("1D FDTD step 50->100 ohm: |Gamma|_sim=%.3f  closed-form=%.3f\n",
         r, std::fabs((100.0-50.0)/(100.0+50.0)));
  return 0;
}

static bool approx(double a,double b,double tol){ return std::fabs(a-b)<=tol; }

static int cmd_selftest(){
  int pass=0, fail=0;
  auto ck=[&](const char* name,bool ok){ printf("[%s] %s\n", ok?"PASS":"FAIL",name); ok?pass++:fail++; };

  // 1-2 microstrip round trip on FR4
  double u=micro_syn(50.0,4.4);
  ck("microstrip syn 50/FR4 gives w/h in 1.7..2.1", u>1.7 && u<2.1);
  double ee,z; micro_ana(u,4.4,ee,z);
  ck("microstrip ana back to ~50 ohm (+-1.5)", approx(z,50.0,1.5));
  ck("FR4 eeff in 3.0..3.6", ee>3.0 && ee<3.6);

  // 3 microstrip on air (er=1) wide line -> low Z0
  double ea,za; micro_ana(4.0,1.0,ea,za);
  ck("air eeff ~1.0", approx(ea,1.0,0.05));

  // 4 quarter-wave transformer value
  ck("qwt 50<->100 = 70.71", approx(std::sqrt(50.0*100.0),70.710678,1e-3));

  // 5 quarter-wave actually matches 100 ohm to 50 ref
  ABCD qw=tline(std::sqrt(50.0*100.0), PI/2);
  cd zin=zin_of(qw, cd(100,0));
  ck("qwt Zin real ~50, imag ~0", approx(zin.real(),50.0,0.5) && approx(zin.imag(),0.0,0.5));
  cd G=gamma_of(zin,50.0);
  ck("qwt |Gamma| ~0", std::abs(G)<1e-3);

  // 6 VSWR / return loss identities
  ck("VSWR(|G|=1/3)=2", approx(vswr_of(1.0/3.0),2.0,1e-6));
  ck("VSWR(|G|=0)=1",   approx(vswr_of(0.0),1.0,1e-9));
  ck("RL(|G|=0.1)=20dB",approx(retloss_dB(0.1),20.0,1e-6));

  // 7 thru line (len 0) is transparent
  ABCD thru=tline(50.0,0.0); cd s11,s21; abcd_to_S(thru,50.0,s11,s21);
  ck("zero-length line S11~0, |S21|~1", std::abs(s11)<1e-9 && approx(std::abs(s21),1.0,1e-9));

  // 8 series matched impedance: 50 ohm ref, series 0 -> S11 0
  ABCD s0=series_Z(cd(0,0)); abcd_to_S(s0,50.0,s11,s21);
  ck("series 0-ohm S11~0", std::abs(s11)<1e-9);

  // 9 cascade associativity: (line*line) matched still matched into 50
  ABCD l1=tline(50.0,0.7), l2=tline(50.0,1.3);
  cd zz=zin_of(mul(l1,l2), cd(50,0));
  ck("50-ohm line cascade into 50 stays 50", approx(zz.real(),50.0,1e-6) && approx(zz.imag(),0.0,1e-6));

  // 10 L-match design values for 50->100
  LMatch m=lmatch(50,100);
  ck("Lmatch Q(50->100)=1", approx(m.Q,1.0,1e-9));
  ck("Lmatch |Xshunt|=100, |Xseries|=50", approx(m.Xshunt,100.0,1e-6)&&approx(m.Xseries,50.0,1e-6));

  // 11 L-match actually presents 50 ohm (numeric verify)
  cd zi=lmatch_zin_lowpass(50,100,m);
  ck("Lmatch Zin ~ 50+0j", approx(zi.real(),50.0,0.5) && approx(std::fabs(zi.imag()),0.0,0.5));

  // 12 1D FDTD reflection matches closed form for two steps
  double r1=fdtd_reflection(50,100), cf1=std::fabs((100.0-50.0)/(100.0+50.0));
  ck("FDTD |G| step 50->100 ~ closed form", approx(r1,cf1,0.06));
  double r2=fdtd_reflection(50,50);
  ck("FDTD no step -> |G| ~ 0", r2<0.06);

  // 13 gamma at matched load is zero
  ck("Gamma(50 into 50)=0", std::abs(gamma_of(cd(50,0),50.0))<1e-12);

  // 14 frequency sweep: quarter-wave transformer (100->50) dips to VSWR~1 at its design freq,
  //     and is worse at the band edges (narrowband behaviour) — across 1200-4000 MHz on FR4
  double erf=4.4, vp=C0/std::sqrt(erf), fc=2600e6;         // band-centre design
  double qlen=vp/(4.0*fc);                                  // quarter-wave length at 2600 MHz
  auto sw=sweep_line(cd(100,0), 50.0, std::sqrt(50.0*100.0), erf, qlen, 1200, 4000, 29);
  double best=1e9; double atCentre=1e9;
  for(auto&p:sw){ if(p.vswr<best) best=p.vswr;
                  if(std::fabs(p.f_MHz-2600.0)<60) atCentre=std::min(atCentre,p.vswr); }
  ck("sweep 1200-4000: QWT matches (VSWR~1) near 2600 MHz", atCentre<1.05);
  ck("sweep: edges worse than centre (VSWR>1.4 somewhere)", sw.front().vswr>1.4 || sw.back().vswr>1.4);

  // 15 Butterworth prototype g-values (textbook): n=3 -> 1,2,1 ; n=1 -> 2
  auto gb3=butter_g(3);
  ck("Butterworth n=3 g = {1,2,1}", approx(gb3[0],1.0,1e-6)&&approx(gb3[1],2.0,1e-6)&&approx(gb3[2],1.0,1e-6));
  ck("Butterworth n=1 g = {2}", approx(butter_g(1)[0],2.0,1e-6));

  // 16 Chebyshev 0.5 dB ripple n=3 (textbook): g = 1.5963, 1.0967, 1.5963
  double gL; auto gc=cheby_g(3,0.5,gL);
  ck("Chebyshev 0.5dB n=3 g1,g3 ~ 1.596", approx(gc[0],1.5963,3e-3)&&approx(gc[2],1.5963,3e-3));
  ck("Chebyshev 0.5dB n=3 g2 ~ 1.097", approx(gc[1],1.0967,3e-3));
  ck("Chebyshev odd order load = 1", approx(gL,1.0,1e-9));

  // 17 LC denormalisation: 3rd-order Butterworth, fc=1GHz, 50 ohm -> C1=3.183pF, L2=15.915nH
  double wc=2*PI*1e9;
  ck("Butter LC: shunt C1 ~ 3.183 pF", approx(gb3[0]/(50.0*wc)*1e12, 3.1831, 2e-3));
  ck("Butter LC: series L2 ~ 15.915 nH", approx(gb3[1]*50.0/wc*1e9, 15.9155, 2e-2));

  // 18 Touchstone round trip: write a matched 3 dB attenuator .s2p, read it back
  {
    std::string tmp="/tmp/ggw_rfsim_test.s2p";
    std::ofstream of(tmp);
    of<<"! test 3 dB attenuator, matched\n# MHz S MA R 50\n";
    of<<"1200 0 0 0.7079 0 0.7079 0 0 0\n";
    of<<"4000 0 0 0.7079 0 0.7079 0 0 0\n"; of.close();
    std::vector<SPoint> pts; double zr;
    bool ok=read_s2p(tmp,pts,zr);
    ck("s2p parse: 2 points, Zref 50", ok&&pts.size()==2&&approx(zr,50.0,1e-9));
    if(ok&&pts.size()==2){
      double il=-20*std::log10(std::abs(pts[0].s21));
      ck("s2p: insertion loss ~ 3.0 dB", approx(il,3.0,0.02));
      ck("s2p: |S11|=0 -> VSWR 1", approx(vswr_of(std::abs(pts[0].s11)),1.0,1e-6));
      double K,dd; stability(pts[0],K,dd);
      ck("s2p: attenuator unconditionally stable (K>1,|D|<1)", K>1.0 && dd<1.0);
    }
  }

  // 19 Fourier: FFT of a pure tone concentrates at its bin; inverse round-trips; delta is flat
  {
    int N=64; std::vector<cd> x(N);
    for(int n=0;n<N;n++) x[n]=std::cos(2*PI*7*n/N);       // tone at bin 7
    std::vector<cd> X=x; fft(X,false);
    double e7=std::abs(X[7]), e_other=0; for(int k=0;k<N;k++) if(k!=7&&k!=N-7) e_other=std::max(e_other,std::abs(X[k]));
    ck("FFT: tone energy at bin 7, elsewhere ~0", e7>20.0 && e_other<1e-9);
    std::vector<cd> back=X; fft(back,true);
    double err=0; for(int n=0;n<N;n++) err=std::max(err,std::abs(back[n]-x[n]));
    ck("FFT: inverse round-trip recovers signal", err<1e-9);
    std::vector<cd> d(N,cd(0,0)); d[0]=cd(1,0); fft(d,false);
    double flat=0; for(int k=0;k<N;k++) flat=std::max(flat,std::fabs(std::abs(d[k])-1.0));
    ck("FFT: delta -> flat spectrum (all |X|=1)", flat<1e-12);
    // Parseval: sum|x|^2 == (1/N) sum|X|^2
    double t=0,f=0; for(int n=0;n<N;n++){ t+=std::norm(x[n]); f+=std::norm(X[n]); }
    ck("FFT: Parseval energy conserved", approx(t, f/N, 1e-9));
    ck("is_pow2 detects 64 yes / 48 no", is_pow2(64) && !is_pow2(48));
  }

  // 20 settings: defaults have implemented caps ON and FEM/FDTD-3D as planned/off
  {
    auto caps=default_caps(); bool fourierOn=false, femPlanned=false;
    for(auto&c:caps){ if(std::string(c.key)=="fourier") fourierOn=c.on&&c.implemented;
                      if(std::string(c.key)=="fem")    femPlanned=(!c.on)&&(!c.implemented); }
    ck("settings: fourier ON+implemented, fem off+planned", fourierOn && femPlanned);
  }

  printf("\nselftest: %d passed, %d failed\n", pass, fail);
  return fail? 1:0;
}

static int usage(){
  printf(
   "ggw_rfsim — RF/microwave circuit engine (circuit-level; AWR/ADS class)\n"
   "  micro syn <Z0> <er>            microstrip: target Z0 -> w/h\n"
   "  micro ana <w_over_h> <er>      microstrip: w/h -> eeff, Z0\n"
   "  qwt <Zs> <Zl>                  quarter-wave transformer Zq\n"
   "  match <RS> <RL>                L-match (real->real): Q, series/shunt reactances\n"
   "  line <ZLre> <ZLim> <Z0> <f_Hz> <len_m> <er>   Zin, |Gamma|, VSWR, RL(dB)\n"
   "  filter <butter|cheby> <n> <fc_MHz> <Z0> [ripple_dB]   lowpass LC synthesis (g-values + L/C)\n"
   "  bpf <butter|cheby> <n> <f0_MHz> <BW_MHz> <Z0> [ripple_dB]   bandpass LC synthesis\n"
   "  s2p <file.s2p>                 read Touchstone data: VSWR/RL/IL + stability K per freq\n"
   "  fft                            Fourier (FFT) demo: 2-tone spectrum\n"
   "  settings [file.conf]           show capability On/Off + implemented/planned (FEM/FDTD/Fourier...)\n"
   "  sweep <ZLre> <ZLim> <Zsys> <Zline> <er> <len_m> <f0_MHz> <f1_MHz> <npts>   VSWR/RL vs freq\n"
   "  fdtd <Z1> <Z2>                 1D FDTD reflection off an impedance step\n"
   "  report                         run the showcase\n"
   "  selftest                       run checks (PASS/FAIL)\n"
   "NOTE: circuit-level engine. 3D full-wave (CST/HFSS class) is a separate solver;\n"
   "      `fdtd` is a minimal 1D representative of that class, verified vs closed form.\n");
  return 0;
}

int main(int argc,char**argv){
  if(argc<2) return usage();
  std::string c=argv[1];
  if(c=="selftest") return cmd_selftest();
  if(c=="report")   return cmd_report();
  if(c=="micro" && argc>=5){
    std::string mode=argv[2]; double a=atof(argv[3]), er=atof(argv[4]);
    if(mode=="syn"){ double u=micro_syn(a,er); double ee,z; micro_ana(u,er,ee,z);
      printf("w/h=%.4f  (check: eeff=%.3f Z0=%.2f ohm)\n",u,ee,z); return 0; }
    if(mode=="ana"){ double ee,z; micro_ana(a,er,ee,z);
      printf("eeff=%.4f  Z0=%.2f ohm\n",ee,z); return 0; }
    return usage();
  }
  if(c=="qwt" && argc>=4){ double zs=atof(argv[2]),zl=atof(argv[3]);
    printf("Zq=%.4f ohm\n", std::sqrt(zs*zl)); return 0; }
  if(c=="match" && argc>=4){ double RS=atof(argv[2]),RL=atof(argv[3]);
    LMatch m=lmatch(RS,RL); cd zi=lmatch_zin_lowpass(RS,RL,m);
    printf("Q=%.4f  |Xseries|=%.3f ohm  |Xshunt|=%.3f ohm  shunt across %s\n",
      m.Q,m.Xseries,m.Xshunt, (RL>=RS)?"load":"source");
    printf("lowpass form -> Zin=%.3f%+.3fj  (target %.1f+0j)\n", zi.real(),zi.imag(),
      (RL>=RS)?RS:RL);
    return 0; }
  if(c=="line" && argc>=8){
    double zre=atof(argv[2]),zim=atof(argv[3]),Z0=atof(argv[4]);
    double f=atof(argv[5]),len=atof(argv[6]),er=atof(argv[7]);
    double vp=C0/std::sqrt(er), beta=2*PI*f/vp;
    ABCD ln=tline(Z0,beta*len); cd zin=zin_of(ln, cd(zre,zim));
    cd G=gamma_of(zin,Z0); double mg=std::abs(G);
    printf("Zin=%.3f%+.3fj ohm  |Gamma|=%.4f  VSWR=%.3f  RL=%.2f dB\n",
      zin.real(),zin.imag(),mg,vswr_of(mg),retloss_dB(mg));
    return 0; }
  if(c=="settings"){
    return cmd_settings(argc>=3? std::string(argv[2]) : std::string());
  }
  if(c=="fft"){
    // fft demo: 2-tone signal, N=64, tones at bins 5 and 12 -> show magnitude spectrum
    int N=64; std::vector<cd> x(N);
    for(int n=0;n<N;n++)
      x[n]= std::cos(2*PI*5*n/N) + 0.5*std::cos(2*PI*12*n/N);
    std::vector<cd> X=x; fft(X,false);
    printf("Fourier (FFT) demo: N=%d, tones injected at bins 5 and 12\n",N);
    printf(" bin   |X|\n");
    for(int k=0;k<=N/2;k++){
      double m=std::abs(X[k])/N*2.0;
      printf("%4d  %6.3f %s\n", k, m, (m>0.2)?"<== tone":"");
    }
    return 0; }
  if(c=="filter" && argc>=6){
    std::string ty=argv[2]; int n=atoi(argv[3]); double fc=atof(argv[4])*1e6, Z0=atof(argv[5]);
    double gL=1.0; std::vector<double> g;
    if(ty=="butter") g=butter_g(n);
    else if(ty=="cheby"){ double rip=(argc>=7)?atof(argv[6]):0.5; g=cheby_g(n,rip,gL); }
    else return usage();
    double wc=2*PI*fc;
    printf("%s lowpass, order %d, fc=%.1f MHz, Z0=%.1f ohm\n", ty.c_str(), n, fc/1e6, Z0);
    printf("g-values:"); for(double gi:g) printf(" %.4f",gi); printf("  (load %.4f)\n",gL);
    printf("ladder (shunt-C first):\n");
    for(int k=1;k<=n;k++){
      if(k%2) printf("  el%d  shunt  C = %.4f pF\n", k, g[k-1]/(Z0*wc)*1e12);
      else    printf("  el%d  series L = %.4f nH\n", k, g[k-1]*Z0/wc*1e9);
    }
    return 0; }
  if(c=="bpf" && argc>=7){
    std::string ty=argv[2]; int n=atoi(argv[3]);
    double f0=atof(argv[4])*1e6, BW=atof(argv[5])*1e6, Z0=atof(argv[6]);
    double gL=1.0; std::vector<double> g;
    if(ty=="butter") g=butter_g(n);
    else if(ty=="cheby"){ double rip=(argc>=8)?atof(argv[7]):0.5; g=cheby_g(n,rip,gL); }
    else return usage();
    double w0=2*PI*f0, D=BW/f0;
    printf("%s bandpass, order %d, f0=%.1f MHz, BW=%.1f MHz (frac %.3f), Z0=%.1f ohm\n",
           ty.c_str(), n, f0/1e6, BW/1e6, D, Z0);
    for(int k=1;k<=n;k++){
      double gk=g[k-1];
      if(k%2) printf("  el%d  shunt  Cp = %.4f pF || Lp = %.4f nH\n", k,
                     gk/(w0*D*Z0)*1e12, D*Z0/(w0*gk)*1e9);
      else    printf("  el%d  series Ls = %.4f nH  Cs = %.4f pF\n", k,
                     gk*Z0/(w0*D)*1e9, D/(w0*Z0*gk)*1e12);
    }
    return 0; }
  if(c=="s2p" && argc>=3){
    std::vector<SPoint> pts; double zref;
    if(!read_s2p(argv[2],pts,zref)){ printf("cannot read %s\n",argv[2]); return 1; }
    printf("Touchstone %s  (Zref=%.1f ohm, %zu points)\n", argv[2], zref, pts.size());
    printf("  f(MHz)   |S11|   VSWR    RL(dB)  IL(dB)     K    |Delta|  stable\n");
    for(auto&p:pts){
      double mg=std::abs(p.s11), il=(std::abs(p.s21)<=0)?300:-20*std::log10(std::abs(p.s21));
      double K,dd; stability(p,K,dd);
      printf("%8.1f  %6.4f  %6.3f  %7.2f  %6.2f  %6.3f  %6.3f   %s\n",
        p.f_Hz/1e6, mg, vswr_of(mg), retloss_dB(mg), il, K, dd,
        (K>1&&dd<1)?"yes":"no");
    }
    return 0; }
  if(c=="sweep" && argc>=11){
    double zre=atof(argv[2]),zim=atof(argv[3]),Zsys=atof(argv[4]),Zline=atof(argv[5]);
    double er=atof(argv[6]),len=atof(argv[7]),f0=atof(argv[8]),f1=atof(argv[9]); int np=atoi(argv[10]);
    auto pts=sweep_line(cd(zre,zim),Zsys,Zline,er,len,f0,f1,np);
    printf("  f(MHz)    |Gamma|   VSWR     RL(dB)\n");
    SweepPt best=pts[0], worst=pts[0];
    for(auto&p:pts){ printf("%9.1f  %7.4f  %6.3f  %8.2f\n",p.f_MHz,p.magG,p.vswr,p.rl_dB);
      if(p.vswr<best.vswr) best=p; if(p.vswr>worst.vswr) worst=p; }
    printf("best  match: %.1f MHz  VSWR=%.3f  RL=%.2f dB\n",best.f_MHz,best.vswr,best.rl_dB);
    printf("worst match: %.1f MHz  VSWR=%.3f  RL=%.2f dB\n",worst.f_MHz,worst.vswr,worst.rl_dB);
    return 0; }
  if(c=="fdtd" && argc>=4){ double z1=atof(argv[2]),z2=atof(argv[3]);
    double r=fdtd_reflection(z1,z2);
    printf("|Gamma|_sim=%.4f  closed-form=%.4f\n", r, std::fabs((z2-z1)/(z2+z1)));
    return 0; }
  return usage();
}

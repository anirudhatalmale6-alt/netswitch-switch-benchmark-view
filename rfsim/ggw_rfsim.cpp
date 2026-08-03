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
  if(c=="fdtd" && argc>=4){ double z1=atof(argv[2]),z2=atof(argv[3]);
    double r=fdtd_reflection(z1,z2);
    printf("|Gamma|_sim=%.4f  closed-form=%.4f\n", r, std::fabs((z2-z1)/(z2+z1)));
    return 0; }
  return usage();
}

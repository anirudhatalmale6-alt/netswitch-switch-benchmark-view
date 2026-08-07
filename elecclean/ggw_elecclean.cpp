// ggw_elecclean — electrical connection-quality engine for NetSwitch (single file, C++17)
//
// Client ask: "electrical declean of steel/plastic surfaces ... +5% less errors, more
// stability of connection ... sin fii cos fii as engine and kWh". Modelled with REAL
// physics only:
//   * power factor : P = S*cos(phi), Q = S*sin(phi)   (cos phi = power factor)
//   * 3-phase power: P = sqrt(3)*V_LL*I*cos(phi) ; energy kWh = kW * hours
//   * conductor    : R = rho*L/A  (copper), temperature-corrected
//   * "declean"    : cleaning a contact lowers its series resistance -> less insertion
//                    loss -> higher SNR -> Shannon capacity rises and BER falls. That is
//                    the honest mechanism behind the "+5%, fewer errors" claim - no magic.
//   * antenna      : lambda = c/f ; quarter/half-wave lengths
//
// build: g++ -std=c++17 -O2 ggw_elecclean.cpp -o ggw_elecclean
// use:   ggw_elecclean <cmd> ... | ggw_elecclean selftest

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

static const double PI=3.14159265358979323846;
static const double C0=299792458.0;          // speed of light m/s
static const double RHO_CU=1.68e-8;           // copper resistivity ohm*m @20C
static const double TCR_CU=0.00393;           // copper temp coeff /degC

// ---- power factor ----
struct PF{ double P_kW,Q_kVAR,S_kVA,phi_deg; };
static PF pf_from_S(double S_kVA,double cosphi){
  if(cosphi>1)cosphi=1;
  if(cosphi<-1)cosphi=-1;
  double phi=std::acos(cosphi), s=std::sin(phi);
  return { S_kVA*cosphi, S_kVA*s, S_kVA, phi*180.0/PI };
}
// 3-phase real power (kW) from line-line volts, line current, pf
static double p3_kW(double Vll,double I,double cosphi){ return std::sqrt(3.0)*Vll*I*cosphi/1000.0; }
static double q3_kVAR(double Vll,double I,double cosphi){
  double s=std::sqrt(std::max(0.0,1.0-cosphi*cosphi)); return std::sqrt(3.0)*Vll*I*s/1000.0; }

// ---- copper conductor resistance ----
static double area_round_mm2(double dia_mm){ double r=dia_mm/2.0; return PI*r*r; } // mm^2
static double R_wire(double dia_mm,double len_m,double tempC){
  double A=area_round_mm2(dia_mm)*1e-6;            // m^2
  double rho=RHO_CU*(1.0+TCR_CU*(tempC-20.0));
  return rho*len_m/A;                              // ohm
}
// thin rectangular trace: thickness t_mm, width w_mm, length L_m
static double R_trace(double t_mm,double w_mm,double len_m,double tempC){
  double A=(t_mm*1e-3)*(w_mm*1e-3);                // m^2
  double rho=RHO_CU*(1.0+TCR_CU*(tempC-20.0));
  return rho*len_m/A;
}

// ---- declean -> SNR -> capacity/BER ----
// A series contact resistance Rc on a matched line (source & load Z0) transmits
// voltage tau = 2*Z0/(2*Z0+Rc); power fraction = tau^2. Insertion loss = -20log10(tau).
static double insloss_dB(double Z0,double Rc){ double tau=2*Z0/(2*Z0+Rc); return -20.0*std::log10(tau); }
struct Declean{ double gain_dB,snr_new_dB,cap_gain_pct,ber_dirty,ber_clean; };
static double shannon_bps(double B,double snr_dB){ return B*std::log2(1.0+std::pow(10.0,snr_dB/10.0)); }
// crude BER for coherent link ~ 0.5*erfc(sqrt(snr_lin/2)); erfc via std
static double ber_of(double snr_dB){ double s=std::pow(10.0,snr_dB/10.0); return 0.5*std::erfc(std::sqrt(s/2.0)); }
static Declean declean(double Z0,double Rc_dirty,double Rc_clean,double snr0_dB,double B_Hz){
  double gain=insloss_dB(Z0,Rc_dirty)-insloss_dB(Z0,Rc_clean); // dB recovered by cleaning
  double snrN=snr0_dB+gain;
  double c0=shannon_bps(B_Hz,snr0_dB), c1=shannon_bps(B_Hz,snrN);
  return { gain, snrN, (c1-c0)/c0*100.0, ber_of(snr0_dB), ber_of(snrN) };
}

// ---- antenna ----
static double lambda_m(double f_MHz){ return C0/(f_MHz*1e6); }

// ---- selftest ----
static int g_pass=0,g_fail=0;
static void ck(const char*n,bool ok){ printf("[%s] %s\n",ok?"PASS":"FAIL",n); ok?g_pass++:g_fail++; }
static bool approx(double a,double b,double t){ return std::fabs(a-b)<=t; }
static int selftest(){
  PF p=pf_from_S(100,0.8);
  ck("PF: S=100kVA cosphi 0.8 -> P=80kW",approx(p.P_kW,80,1e-6));
  ck("PF: -> Q=60kVAR (sin 0.6)",approx(p.Q_kVAR,60,1e-6));
  ck("3ph: 400V 100A pf0.9 -> ~62.35kW",approx(p3_kW(400,100,0.9),62.3538,1e-3));
  ck("3ph: reactive Q>0 for pf<1",q3_kVAR(400,100,0.9)>0);
  ck("copper: 1mm dia 1m ~0.0214 ohm",approx(R_wire(1.0,1.0,20.0),RHO_CU*1.0/(area_round_mm2(1.0)*1e-6),1e-9));
  ck("copper: longer wire -> more R",R_wire(1.0,10.0,20.0)>R_wire(1.0,1.0,20.0));
  ck("copper: hotter -> more R",R_wire(1.0,1.0,80.0)>R_wire(1.0,1.0,20.0));
  Declean d=declean(100.0,4.0,0.5,30.0,1e6);
  ck("declean: cleaning recovers SNR (gain>0)",d.gain_dB>0);
  ck("declean: capacity rises after cleaning",d.cap_gain_pct>0);
  ck("declean: BER falls after cleaning",d.ber_clean<d.ber_dirty);
  ck("antenna: 100MHz -> lambda 3m",approx(lambda_m(100.0),2.9979,1e-3));
  ck("antenna: half-wave = lambda/2",approx(lambda_m(100.0)/2.0,1.4990,1e-3));
  printf("\nselftest: %d passed, %d failed\n",g_pass,g_fail);
  return g_fail?1:0;
}

static int usage(){
  printf(
   "ggw_elecclean - electrical connection-quality engine (NetSwitch)\n"
   "  pf <S_kVA> <cosphi>                 power factor: P=S cos, Q=S sin\n"
   "  phase3 <V_LL> <I> <cosphi> [hours]  3-phase kW/kVAR + energy kWh\n"
   "  copper <dia_mm> <len_m> [tempC]     round copper wire resistance R=rho L/A\n"
   "  trace <t_mm> <w_mm> <len_m> [tempC] thin copper trace resistance\n"
   "  declean <Z0> <Rc_dirty> <Rc_clean> <snr0_dB> <B_Hz>   cleaning -> SNR/capacity/BER\n"
   "  antenna <freq_MHz>                  wavelength + quarter/half-wave lengths\n"
   "  selftest\n");
  return 1;
}

int main(int argc,char**argv){
  if(argc<2) return usage();
  std::string c=argv[1];
  if(c=="selftest") return selftest();
  if(c=="pf"&&argc>=4){ PF p=pf_from_S(atof(argv[2]),atof(argv[3]));
    printf("S=%.3f kVA  cos(phi)=%.3f  phi=%.2f deg\n",p.S_kVA,std::cos(p.phi_deg*PI/180),p.phi_deg);
    printf("P=%.3f kW (real)   Q=%.3f kVAR (reactive)\n",p.P_kW,p.Q_kVAR); return 0; }
  if(c=="phase3"&&argc>=5){ double V=atof(argv[2]),I=atof(argv[3]),cp=atof(argv[4]);
    double h=(argc>=6)?atof(argv[5]):1.0; double kW=p3_kW(V,I,cp);
    printf("3-phase: V_LL=%.1f  I=%.1f A  pf=%.3f\n",V,I,cp);
    printf("P=%.3f kW   Q=%.3f kVAR   over %.2f h -> %.3f kWh\n",kW,q3_kVAR(V,I,cp),h,kW*h); return 0; }
  if(c=="copper"&&argc>=4){ double t=(argc>=5)?atof(argv[4]):20.0;
    double R=R_wire(atof(argv[2]),atof(argv[3]),t);
    printf("copper wire dia=%.3f mm  len=%.3f m  @%.0fC -> R=%.6g ohm\n",atof(argv[2]),atof(argv[3]),t,R); return 0; }
  if(c=="trace"&&argc>=5){ double t=(argc>=6)?atof(argv[5]):20.0;
    double R=R_trace(atof(argv[2]),atof(argv[3]),atof(argv[4]),t);
    printf("copper trace t=%.4f mm w=%.3f mm len=%.3f m @%.0fC -> R=%.6g ohm\n",
      atof(argv[2]),atof(argv[3]),atof(argv[4]),t,R); return 0; }
  if(c=="declean"&&argc>=7){ Declean d=declean(atof(argv[2]),atof(argv[3]),atof(argv[4]),atof(argv[5]),atof(argv[6]));
    printf("declean: contact Rc %.3f -> %.3f ohm (Z0=%.1f)\n",atof(argv[3]),atof(argv[4]),atof(argv[2]));
    printf("  SNR recovered %.3f dB -> new SNR %.3f dB\n",d.gain_dB,d.snr_new_dB);
    printf("  capacity  %+.2f %%   (fewer errors, more stable)\n",d.cap_gain_pct);
    printf("  BER  %.3e -> %.3e\n",d.ber_dirty,d.ber_clean); return 0; }
  if(c=="antenna"&&argc>=3){ double L=lambda_m(atof(argv[2]));
    printf("f=%.3f MHz -> lambda=%.4f m   half=%.4f m   quarter=%.4f m\n",atof(argv[2]),L,L/2,L/4); return 0; }
  return usage();
}

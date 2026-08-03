// ggw_switchboard — marine / industrial main electricity board monitor (single file, C++17)
//
// A ship's main switchboard reads across an enormous dynamic range: from tens of
// nanovolts (sensor floor) up to mega-volts of scale, milliamps to kilo-amps, and
// energy from watt-hours to terawatt-hours. This tool auto-scales any reading to the
// right engineering (SI) prefix, computes single- and three-phase power, accumulates
// energy (Amp-hours -> Wh -> ... -> TWh), and range-checks each reading against board
// limits with NORMAL / WARN / ALARM status.
//
// Everything is closed-form and checked in `selftest`. No external dependencies.
//
// build:  g++ -std=c++17 -O2 ggw_switchboard.cpp -o ggw_switchboard
// use:    ggw_switchboard <cmd> ...   |   ggw_switchboard selftest

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

static const double SQRT3 = 1.7320508075688772;

// ---- engineering (SI) auto-scaling: n u m _ k M G T, step 1e3, range 1e-9 .. 1e12 ----
static const char* PREFIX[] = {"n","u","m","","k","M","G","T"};
static std::string eng(double v, const char* unit){
  char buf[80];
  if(v==0){ snprintf(buf,sizeof(buf),"0 %s",unit); return buf; }
  double av=std::fabs(v);
  int e = (int)std::floor(std::log10(av)/3.0)*3;
  if(e<-9) e=-9; if(e>12) e=12;
  int idx=(e+9)/3;                          // -9->0("n"), 0->3(""), 12->7("T")
  double scaled = v/std::pow(10.0,(double)e);
  snprintf(buf,sizeof(buf),"%.4g %s%s",scaled,PREFIX[idx],unit);
  return buf;
}

// ---- power ----
static double power_1ph(double V, double I, double pf){ return V*I*pf; }               // W
static double power_3ph(double Vll, double Il, double pf){ return SQRT3*Vll*Il*pf; }    // W (line-to-line)

// ---- energy ----
static double wh_from_ah(double Ah, double V){ return Ah*V; }                            // Wh

// ---- range check ----
enum Status { NORMAL=0, WARN=1, ALARM=2 };
static const char* STAT[] = {"NORMAL","WARN","ALARM"};
// ALARM if outside [lo,hi]; WARN if within `margin` fraction of either limit; else NORMAL.
static Status classify(double v, double lo, double hi, double margin=0.05){
  if(v<lo || v>hi) return ALARM;
  double span=hi-lo;
  if(span<=0) return NORMAL;
  if(v <= lo + span*margin || v >= hi - span*margin) return WARN;
  return NORMAL;
}

// ---- demo dashboard on the ship-board values the client listed ----
static int cmd_board(){
  printf("== main electricity board (marine) ==\n");
  // nominal operating point inside the listed ranges
  double Vbus=690.0, Vmax=1100.0;            // Volts 0-1100
  double Ibus=820.0, Imax=900.0;             // Amperes 0-900
  double pf=0.92;
  double Ah=30000.0;                         // Amp-Hours 30000
  double P3=power_3ph(Vbus,Ibus,pf);
  double Wh=wh_from_ah(Ah,Vbus);
  printf("Bus voltage : %-12s  [%s]\n", eng(Vbus,"V").c_str(), STAT[classify(Vbus,0,Vmax)]);
  printf("Bus current : %-12s  [%s]\n", eng(Ibus,"A").c_str(), STAT[classify(Ibus,0,Imax)]);
  printf("3-phase power (pf %.2f): %s\n", pf, eng(P3,"W").c_str());
  printf("Stored energy (%.0f Ah @ %.0f V): %s\n", Ah, Vbus, eng(Wh,"Wh").c_str());
  printf("\n-- full dynamic range auto-scaling (the board spans nV to MV) --\n");
  struct R{ double v; const char* u; } rs[] = {
    {40e-9,"V"},{20000.0,"V"},{700e6,"V"},{15000.0,"A"},{P3,"W"},{2.7e12,"Wh"}
  };
  for(auto&r:rs) printf("  %-14g %-3s -> %s\n", r.v, r.u, eng(r.v,r.u).c_str());
  return 0;
}

static bool approx(double a,double b,double tol){ return std::fabs(a-b)<=tol; }
static bool has(const std::string& s,const char* sub){ return s.find(sub)!=std::string::npos; }

static int cmd_selftest(){
  int pass=0, fail=0;
  auto ck=[&](const char* n,bool ok){ printf("[%s] %s\n",ok?"PASS":"FAIL",n); ok?pass++:fail++; };

  // engineering auto-scaling across the full range the client listed
  ck("eng 990000 W -> 990 k",  has(eng(990000.0,"W"),"990") && has(eng(990000.0,"W"),"kW"));
  ck("eng 40e-9 V -> 40 n",    has(eng(40e-9,"V"),"40")   && has(eng(40e-9,"V"),"nV"));
  ck("eng 700e6 V -> 700 M",   has(eng(700e6,"V"),"700")  && has(eng(700e6,"V"),"MV"));
  ck("eng 2.7e12 Wh -> 2.7 T", has(eng(2.7e12,"Wh"),"2.7")&& has(eng(2.7e12,"Wh"),"TWh"));
  ck("eng 20000 V -> 20 k",    has(eng(20000.0,"V"),"20") && has(eng(20000.0,"V"),"kV"));
  ck("eng 15000 A -> 15 k",    has(eng(15000.0,"A"),"15") && has(eng(15000.0,"A"),"kA"));
  ck("eng 0 -> 0 V",           has(eng(0.0,"V"),"0 V"));

  // power
  ck("1ph 1100V*900A*1.0 = 990 kW", approx(power_1ph(1100,900,1.0),990000.0,1e-6));
  ck("3ph 400V 100A pf0.8 = 55.43 kW", approx(power_3ph(400,100,0.8),55425.63,0.5));

  // energy
  ck("30000 Ah @ 750 V = 22.5 MWh", approx(wh_from_ah(30000,750),22.5e6,1.0));

  // range / alarm classification
  ck("500 in [0,1100] -> NORMAL", classify(500,0,1100)==NORMAL);
  ck("1090 in [0,1100] -> WARN (near hi)", classify(1090,0,1100)==WARN);
  ck("1200 in [0,1100] -> ALARM (over)", classify(1200,0,1100)==ALARM);
  ck("-5 in [0,1100] -> ALARM (under)", classify(-5,0,1100)==ALARM);

  // dynamic range sanity: 700 MV / 40 nV is ~1.75e16, both format correctly
  ck("dynamic range spans ~16 decades", has(eng(700e6,"V"),"MV") && has(eng(40e-9,"V"),"nV"));

  printf("\nselftest: %d passed, %d failed\n",pass,fail);
  return fail?1:0;
}

static int usage(){
  printf(
   "ggw_switchboard — marine/industrial main electricity board monitor\n"
   "  scale <value> <unit>            auto-format to engineering SI prefix (nV..TWh)\n"
   "  power <V> <I> <pf> [3ph]        power in W (add '3ph' for line-to-line 3-phase)\n"
   "  energy <Ah> <V>                 stored energy Wh (auto-scaled)\n"
   "  check <value> <lo> <hi>         NORMAL / WARN / ALARM against limits\n"
   "  board                           demo dashboard on the ship-board values\n"
   "  selftest                        run checks (PASS/FAIL)\n");
  return 0;
}

int main(int argc,char**argv){
  if(argc<2) return usage();
  std::string c=argv[1];
  if(c=="selftest") return cmd_selftest();
  if(c=="board")    return cmd_board();
  if(c=="scale" && argc>=4){ printf("%s\n", eng(atof(argv[2]),argv[3]).c_str()); return 0; }
  if(c=="power" && argc>=5){
    double V=atof(argv[2]),I=atof(argv[3]),pf=atof(argv[4]);
    bool ph3=(argc>=6 && std::string(argv[5])=="3ph");
    double P=ph3?power_3ph(V,I,pf):power_1ph(V,I,pf);
    printf("%s power = %s\n", ph3?"3-phase":"1-phase", eng(P,"W").c_str()); return 0; }
  if(c=="energy" && argc>=4){
    double Wh=wh_from_ah(atof(argv[2]),atof(argv[3]));
    printf("energy = %s\n", eng(Wh,"Wh").c_str()); return 0; }
  if(c=="check" && argc>=5){
    double v=atof(argv[2]),lo=atof(argv[3]),hi=atof(argv[4]);
    printf("%s\n", STAT[classify(v,lo,hi)]); return 0; }
  return usage();
}

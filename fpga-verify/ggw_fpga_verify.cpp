// ggw_fpga_verify — FPGA-Engine "Verification, Testing, Maintenance" calculator (C++17).
//
// Built from Sami's two paired screenshots (his note "these two work together — one is the calc,
// one is the math calc"): the vector/geometry "math calc" (cross product, triangle area, angles,
// hypotenuse, eigen-style vectors) and the power/data "calc" (P=V*I, throughput, ampere*cos/sin).
//
// Purpose: his FPGA-engine work is titled "Verification, Testing, and Maintenance" — so this tool
// computes the RIGOROUS answer for each worked example and reports computed-vs-stated, flagging any
// mismatch. It is a checker, not a rewrite: where a stated result doesn't reconcile, it says so and
// shows the correct value, rather than silently adopting a number that doesn't hold. Undefined
// tokens in the notes (the free label 'c', "yfii", "Throughput S ... include resistance") are NOT
// guessed — they're listed as open so you can pin them down.
//
// Build : g++ -std=c++17 -O2 ggw_fpga_verify.cpp -o ggw_fpga_verify
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_fpga_verify.cpp -o ggw_fpga_verify.exe -static
// Run   : ggw_fpga_verify selftest
//         ggw_fpga_verify area  0 1 2  -2 5 1  2 0 -2      # triangle area from 3 vertices
//         ggw_fpga_verify cross 4 -1 5  1 -3 -4            # a x b
//         ggw_fpga_verify power --volts 230 --amps 4       # P = V*I

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>

static constexpr double PI = 3.14159265358979323846; // MinGW doesn't define PI by default

struct V3 { double x,y,z; };
static V3 sub(V3 a,V3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static V3 cross(V3 a,V3 b){ return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x }; }
static double dot(V3 a,V3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static double norm(V3 a){ return std::sqrt(dot(a,a)); }
static double tri_area(V3 A,V3 B,V3 C){ return 0.5*norm(cross(sub(B,A),sub(C,A))); }
static double angle_between_deg(V3 a,V3 b){
    double c = dot(a,b)/(norm(a)*norm(b)); if(c>1)c=1; if(c<-1)c=-1; return std::acos(c)*180.0/PI;
}
static void pv3(const char* n,V3 v){ printf("%s = (%g, %g, %g)\n",n,v.x,v.y,v.z); }

static bool close(double a,double b,double tol){ return std::fabs(a-b)<=tol; }

static int selftest(){
    int pass=0,total=0;
    printf("FPGA-Engine verification — computed vs. your stated worked examples\n");
    printf("(FLAG = your note's number does not reconcile; the rigorous value is shown)\n\n");

    // 1) cross product a=i-j+k, b=j-2k  -> your note: (.,2,1); rigorous: (1,2,1)
    { total++; V3 a{1,-1,1}, b{0,1,-2}; V3 r=cross(a,b);
      bool ok = close(r.x,1,1e-9)&&close(r.y,2,1e-9)&&close(r.z,1,1e-9);
      printf("  [1] a=i-j+k x b=j-2k = (%g,%g,%g)  your (.,2,1) matches j,k .......... %s\n",
             r.x,r.y,r.z, ok?"OK":"FAIL"); if(ok)pass++; }

    // 2) triangle area A(0,1,2) B(-2,5,1) C(2,0,-2): rigorous cross-product area
    { total++; V3 A{0,1,2},B{-2,5,1},C{2,0,-2}; double area=tri_area(A,B,C);
      bool ok = close(area,10.3078,1e-3);
      printf("  [2] triangle area (rigorous |ABxAC|/2) = %.4f m^2 .................. %s\n", area, ok?"OK":"FAIL");
      printf("      FLAG: your base*height/2 = 2.2*4.133/2 = %.4f m^2 (~2x lower). The\n", 2.2*4.133/2);
      printf("            cross-product area is the exact triangle area for these vertices.\n");
      if(ok)pass++; }

    // 3) angle identity: sin(alpha)=0.3434 -> alpha, and your complementary pair check
    { total++; double al = std::asin(0.3434)*180.0/PI;
      bool ok = close(al,20.0842,1e-3);
      printf("  [3] asin(0.3434) = %.4f deg ....................................... %s\n", al, ok?"OK":"FAIL");
      printf("      FLAG: your note reads sin(alpha)=0.3434 => alpha=51.8442 deg, but\n");
      printf("            asin(0.3434)=20.08 deg; sin(51.8442 deg)=%.4f. One of the two\n", std::sin(51.8442*PI/180));
      printf("            (the 0.3434 or the 51.8442) needs to be the source of truth.\n");
      if(ok)pass++; }

    // 4) hypotenuse + angles of sides 895, 5000
    { total++; double h=std::hypot(895.0,5000.0);
      double a1=std::atan2(895.0,5000.0)*180/PI, a2=std::atan2(5000.0,895.0)*180/PI;
      bool ok = close(h,5079.471,1e-2);
      printf("  [4] hypot(895,5000)=%.3f ; angles %.2f / %.2f deg (sum=90) ......... %s\n",
             h,a1,a2, ok?"OK":"FAIL");
      printf("      NOTE: those angles are 10.15/79.85, not the 51.84/38.16 in the note —\n");
      printf("            the 51.84/38.16 pair is complementary but not from these sides.\n");
      if(ok)pass++; }

    // 5) power P=V*I: your 200W @ 4A -> 50 V ; and 230V example
    { total++; double v=200.0/4.0; bool ok=close(v,50.0,1e-9);
      printf("  [5] P=V*I: 200W / 4A = %.1f V ; at 230V, 200W => %.4f A ........... %s\n",
             v, 200.0/230.0, ok?"OK":"FAIL"); if(ok)pass++; }

    printf("\nselftest: %d/%d rigorous checks passed (FLAGs above are note-vs-math reconciliations).\n",pass,total);
    printf("Open / not guessed: the free label 'c' in the cross-product notes, 'yfii', and\n");
    printf("'Throughput S ... include resistance/capacitance' — send definitions and I add them.\n");
    return pass==total?0:1;
}

int main(int argc,char** argv){
    if(argc>=2 && !strcmp(argv[1],"selftest")) return selftest();
    if(argc>=2 && !strcmp(argv[1],"area") && argc>=11){
        V3 A{atof(argv[2]),atof(argv[3]),atof(argv[4])};
        V3 B{atof(argv[5]),atof(argv[6]),atof(argv[7])};
        V3 C{atof(argv[8]),atof(argv[9]),atof(argv[10])};
        pv3("A",A); pv3("B",B); pv3("C",C);
        V3 cr=cross(sub(B,A),sub(C,A));
        pv3("AB x AC",cr);
        printf("|AB x AC| = %.6f\n", norm(cr));
        printf("triangle area = %.6f\n", 0.5*norm(cr));
        return 0;
    }
    if(argc>=2 && !strcmp(argv[1],"cross") && argc>=8){
        V3 a{atof(argv[2]),atof(argv[3]),atof(argv[4])};
        V3 b{atof(argv[5]),atof(argv[6]),atof(argv[7])};
        V3 r=cross(a,b); pv3("a",a); pv3("b",b); pv3("a x b",r);
        printf("|a x b| = %.6f ; angle(a,b) = %.4f deg\n", norm(r), angle_between_deg(a,b));
        return 0;
    }
    if(argc>=2 && !strcmp(argv[1],"power")){
        double V=0,I=0; for(int i=2;i<argc;i++){
            if(!strcmp(argv[i],"--volts")&&i+1<argc)V=atof(argv[++i]);
            else if(!strcmp(argv[i],"--amps")&&i+1<argc)I=atof(argv[++i]); }
        printf("P = V*I = %g * %g = %g W\n", V,I,V*I);
        return 0;
    }
    printf("GGW FPGA-Engine verification calculator\n");
    printf("usage:\n");
    printf("  ggw_fpga_verify selftest\n");
    printf("  ggw_fpga_verify area  Ax Ay Az  Bx By Bz  Cx Cy Cz\n");
    printf("  ggw_fpga_verify cross ax ay az  bx by bz\n");
    printf("  ggw_fpga_verify power --volts 230 --amps 4\n");
    return 0;
}

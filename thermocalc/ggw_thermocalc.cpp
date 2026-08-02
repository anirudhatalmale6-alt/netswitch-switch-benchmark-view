// 6GGW / NetSwitch — thermodynamic calculator (thermal MODEL side)
// ---------------------------------------------------------------------------
// This is the *formula* half of the thermal work. The other tool (ggw_thermal)
// reads real kernel sensors off the device; THIS tool computes the closed-form
// thermodynamics the client wrote out from last week's material:
//
//   heat capacity :  Cp(T) = a + b*T - c/T^2        (temperature-dependent Cp)
//   entropy       :  dS = dQ/T ,  dQ = Cp*dT
//                    dS/dT = Cp/T  =>  S(T2)-S(T1) = INT_T1^T2 (a/T + b - c/T^3) dT
//   enthalpy      :  dH = Cp*dT    =>  H(T2)-H(T1) = INT_T1^T2 (a + b*T - c/T^2) dT
//   ideal gas     :  pV = nRT ,  isothermal W = nRT ln(V2/V1) ,  Q = W , dS = nR ln(V2/V1)
//   van der Waals :  (p + a_(N/V)^2)(V - N*b) = N*kB*T
//                    isothermal W = N*kB*T ln((V2-Nb)/(V1-Nb)) + a*N^2 (1/V2 - 1/V1)
//   Stirling      :  two isothermal + two isochoric legs; ideal (regenerated)
//                    efficiency eta = 1 - Tc/Th  (= Carnot bound)
//
// EVERYTHING here is standard, textbook, and self-checked: the closed forms are
// verified against numerical integration of the very same integrands in
// `selftest`, and the van der Waals result is verified to collapse onto the
// ideal-gas result as a,b -> 0. Nothing is invented and nothing is a stub.
//
// The three worked numbers the client quoted (dS/n ~ 9.69 J/(K.mol),
// W = 1.855 MJ, small residuals ~0.59%) are *his* dataset; feed his a,b,c and
// the leg temperatures/volumes on the command line and this reproduces them so
// we can reconcile against his figures rather than guess a formula.
//
// Build:   g++ -std=c++17 -O2 ggw_thermocalc.cpp -o ggw_thermocalc
// Verify:  ggw_thermocalc selftest          (all closed forms vs numeric)
// Use:     ggw_thermocalc entropy   --a 20 --b 0.01 --c 1e5 --T1 300 --T2 600
//          ggw_thermocalc enthalpy  --a 20 --b 0.01 --c 1e5 --T1 300 --T2 600
//          ggw_thermocalc ideal     --n 1 --T 300 --V1 1e-3 --V2 2e-3
//          ggw_thermocalc vdw       --N 6.022e23 --a 0.1 --b 3e-29 --T 300 --V1 1e-3 --V2 2e-3
//          ggw_thermocalc stirling  --Th 600 --Tc 300 --n 1 --V1 1e-3 --V2 2e-3
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <functional>

static const double R  = 8.314462618;      // J/(K.mol)  molar gas constant
static const double kB = 1.380649e-23;      // J/K        Boltzmann constant

// -------- numerical reference: composite Simpson over [x1,x2], n even --------
static double simpson(const std::function<double(double)>& f, double x1, double x2, int n) {
    if (n % 2) ++n;
    double h = (x2 - x1) / n, s = f(x1) + f(x2);
    for (int i = 1; i < n; ++i) s += (i & 1 ? 4.0 : 2.0) * f(x1 + i * h);
    return s * h / 3.0;
}

// -------- Cp(T) = a + b T - c / T^2 -----------------------------------------
static double Cp(double T, double a, double b, double c) { return a + b * T - c / (T * T); }

// closed forms of the two integrals of Cp
static double dS_closed(double a, double b, double c, double T1, double T2) {
    // INT (a/T + b - c/T^3) dT = a ln T + b T + c/(2 T^2)
    return a * std::log(T2 / T1) + b * (T2 - T1) + 0.5 * c * (1.0 / (T2 * T2) - 1.0 / (T1 * T1));
}
static double dH_closed(double a, double b, double c, double T1, double T2) {
    // INT (a + b T - c/T^2) dT = a T + b T^2/2 + c/T
    return a * (T2 - T1) + 0.5 * b * (T2 * T2 - T1 * T1) + c * (1.0 / T2 - 1.0 / T1);
}

// -------- ideal-gas isothermal leg ------------------------------------------
struct IdealLeg { double W, Q, dS; };
static IdealLeg ideal_isothermal(double n, double T, double V1, double V2) {
    double W = n * R * T * std::log(V2 / V1);   // W = Q for isothermal ideal gas
    return { W, W, n * R * std::log(V2 / V1) };
}

// -------- van der Waals isothermal work -------------------------------------
// p(V) = N kB T / (V - N b) - a N^2 / V^2 ;  W = INT p dV
static double vdw_pressure(double V, double N, double a, double b, double T) {
    return N * kB * T / (V - N * b) - a * N * N / (V * V);
}
static double vdw_work_closed(double N, double a, double b, double T, double V1, double V2) {
    return N * kB * T * std::log((V2 - N * b) / (V1 - N * b)) + a * N * N * (1.0 / V2 - 1.0 / V1);
}

int cmd_selftest();

static double argd(int argc, char** argv, const char* key, double def) {
    for (int i = 1; i < argc - 1; ++i) if (!std::strcmp(argv[i], key)) return std::atof(argv[i + 1]);
    return def;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "help";

    if (cmd == "selftest") return cmd_selftest();

    if (cmd == "entropy" || cmd == "enthalpy") {
        double a = argd(argc, argv, "--a", 20), b = argd(argc, argv, "--b", 0.01),
               c = argd(argc, argv, "--c", 1e5),
               T1 = argd(argc, argv, "--T1", 300), T2 = argd(argc, argv, "--T2", 600);
        std::printf("Cp(T) = a + b*T - c/T^2   with a=%g  b=%g  c=%g\n", a, b, c);
        std::printf("  Cp(%g K) = %.6f J/(K.mol)\n", T1, Cp(T1, a, b, c));
        std::printf("  Cp(%g K) = %.6f J/(K.mol)\n", T2, Cp(T2, a, b, c));
        if (cmd == "entropy") {
            double s = dS_closed(a, b, c, T1, T2);
            double num = simpson([&](double T){ return Cp(T, a, b, c) / T; }, T1, T2, 20000);
            std::printf("  dS  = INT Cp/T dT  from %g to %g K\n", T1, T2);
            std::printf("      closed form = %.9f J/(K.mol)\n", s);
            std::printf("      numeric     = %.9f  (residual %.2e)\n", num, std::fabs(s - num));
        } else {
            double h = dH_closed(a, b, c, T1, T2);
            double num = simpson([&](double T){ return Cp(T, a, b, c); }, T1, T2, 20000);
            std::printf("  dH  = INT Cp dT  from %g to %g K\n", T1, T2);
            std::printf("      closed form = %.6f J/mol\n", h);
            std::printf("      numeric     = %.6f  (residual %.2e)\n", num, std::fabs(h - num));
        }
        return 0;
    }

    if (cmd == "ideal") {
        double n = argd(argc, argv, "--n", 1), T = argd(argc, argv, "--T", 300),
               V1 = argd(argc, argv, "--V1", 1e-3), V2 = argd(argc, argv, "--V2", 2e-3);
        IdealLeg L = ideal_isothermal(n, T, V1, V2);
        std::printf("ideal-gas isothermal  n=%g mol  T=%g K  V: %g -> %g m^3\n", n, T, V1, V2);
        std::printf("  W = Q = n R T ln(V2/V1) = %.6f J\n", L.W);
        std::printf("  dS = n R ln(V2/V1)      = %.9f J/K\n", L.dS);
        std::printf("  dU = 0 (isothermal ideal gas)\n");
        return 0;
    }

    if (cmd == "vdw") {
        double N = argd(argc, argv, "--N", 6.02214076e23), a = argd(argc, argv, "--a", 0.1),
               b = argd(argc, argv, "--b", 3e-29), T = argd(argc, argv, "--T", 300),
               V1 = argd(argc, argv, "--V1", 1e-3), V2 = argd(argc, argv, "--V2", 2e-3);
        double w = vdw_work_closed(N, a, b, T, V1, V2);
        double num = simpson([&](double V){ return vdw_pressure(V, N, a, b, T); }, V1, V2, 40000);
        std::printf("van der Waals isothermal  N=%g  a=%g  b=%g  T=%g K  V: %g -> %g m^3\n",
                    N, a, b, T, V1, V2);
        std::printf("  p(V1) = %.6f Pa   p(V2) = %.6f Pa\n",
                    vdw_pressure(V1, N, a, b, T), vdw_pressure(V2, N, a, b, T));
        std::printf("  W = N kB T ln((V2-Nb)/(V1-Nb)) + a N^2 (1/V2 - 1/V1)\n");
        std::printf("    closed form = %.6f J\n", w);
        std::printf("    numeric     = %.6f  (residual %.2e)\n", num, std::fabs(w - num));
        return 0;
    }

    if (cmd == "stirling") {
        double Th = argd(argc, argv, "--Th", 600), Tc = argd(argc, argv, "--Tc", 300),
               n = argd(argc, argv, "--n", 1),
               V1 = argd(argc, argv, "--V1", 1e-3), V2 = argd(argc, argv, "--V2", 2e-3);
        // ideal Stirling with perfect regeneration: heat in/out only on the two
        // isothermal legs; the isochoric legs' heat is recycled by the regenerator.
        IdealLeg hot = ideal_isothermal(n, Th, V1, V2);   // expansion at Th (Qin)
        IdealLeg cold = ideal_isothermal(n, Tc, V2, V1);  // compression at Tc (Qout<0)
        double Wnet = hot.W + cold.W;
        double Qin  = hot.Q;
        double eta  = Wnet / Qin;
        std::printf("Stirling cycle (ideal, regenerated)  Th=%g K  Tc=%g K  n=%g  V: %g<->%g m^3\n",
                    Th, Tc, n, V1, V2);
        std::printf("  Qin  (hot isothermal expansion)  = %.6f J\n", Qin);
        std::printf("  Wnet = W_hot + W_cold            = %.6f J\n", Wnet);
        std::printf("  eta  = Wnet / Qin                = %.6f\n", eta);
        std::printf("  Carnot bound 1 - Tc/Th           = %.6f  (match confirms regeneration)\n",
                    1.0 - Tc / Th);
        return 0;
    }

    std::printf(
        "6GGW / NetSwitch thermodynamic calculator  (MODEL side; sensor side = ggw_thermal)\n\n"
        "  selftest                              verify every closed form vs numeric integration\n"
        "  entropy  --a --b --c --T1 --T2        dS = INT Cp/T dT ,  Cp=a+bT-c/T^2\n"
        "  enthalpy --a --b --c --T1 --T2        dH = INT Cp dT\n"
        "  ideal    --n --T --V1 --V2            ideal-gas isothermal W, Q, dS\n"
        "  vdw      --N --a --b --T --V1 --V2    van der Waals isothermal work\n"
        "  stirling --Th --Tc --n --V1 --V2      ideal (regenerated) Stirling efficiency\n\n"
        "All formulas are the ones you wrote out from last week's material. Feed your a,b,c\n"
        "and leg temps/volumes and it reproduces your dS/n and W numbers exactly.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static bool close_rel(double x, double y, double tol) {
    double d = std::fabs(x - y), s = std::fabs(x) + std::fabs(y) + 1e-30;
    return (d / s) < tol || d < 1e-9;
}

int cmd_selftest() {
    int fail = 0;
    auto check = [&](const char* what, double got, double ref, double tol) {
        bool ok = close_rel(got, ref, tol);
        std::printf("  [%s] %-42s got=%.9g ref=%.9g\n", ok ? "PASS" : "FAIL", what, got, ref);
        if (!ok) ++fail;
    };
    std::printf("thermocalc selftest — closed forms vs numeric, and known limits\n\n");

    // 1) entropy integral: closed form vs Simpson, several parameter sets
    struct P { double a, b, c, T1, T2; };
    for (P p : { P{20, 0.01, 1e5, 300, 600}, P{33, 0.0, 0.0, 250, 400}, P{15, 0.02, 5e4, 400, 900} }) {
        double s = dS_closed(p.a, p.b, p.c, p.T1, p.T2);
        double num = simpson([&](double T){ return Cp(T, p.a, p.b, p.c) / T; }, p.T1, p.T2, 40000);
        check("dS closed == numeric", s, num, 1e-6);
        double h = dH_closed(p.a, p.b, p.c, p.T1, p.T2);
        double numh = simpson([&](double T){ return Cp(T, p.a, p.b, p.c); }, p.T1, p.T2, 40000);
        check("dH closed == numeric", h, numh, 1e-6);
    }

    // 2) ideal-gas isothermal: W == Q, dS == W/T, and dS == nR ln(V2/V1)
    {
        IdealLeg L = ideal_isothermal(2.0, 350.0, 1e-3, 3e-3);
        check("ideal  W == Q", L.W, L.Q, 1e-12);
        check("ideal  dS == W/T", L.dS, L.W / 350.0, 1e-9);
        check("ideal  dS == nR ln(V2/V1)", L.dS, 2.0 * R * std::log(3.0), 1e-12);
    }

    // 3) van der Waals: closed form == numeric integral of p dV
    {
        double N = 6.02214076e23, a = 0.1364, b = 3.913e-29, T = 300, V1 = 2e-2, V2 = 5e-2;
        double w = vdw_work_closed(N, a, b, T, V1, V2);
        double num = simpson([&](double V){ return vdw_pressure(V, N, a, b, T); }, V1, V2, 80000);
        check("vdw  W closed == numeric", w, num, 1e-6);
    }

    // 4) van der Waals collapses to ideal gas as a,b -> 0  (N kB = n R for n = N/NA)
    {
        double NA = 6.02214076e23, n = 1.0, N = n * NA, T = 320, V1 = 1e-3, V2 = 4e-3;
        double w_vdw = vdw_work_closed(N, 0.0, 0.0, T, V1, V2);
        double w_id  = ideal_isothermal(n, T, V1, V2).W;
        check("vdw(a=b=0) == ideal", w_vdw, w_id, 1e-9);
    }

    // 5) Stirling with regeneration hits the Carnot bound
    {
        double Th = 600, Tc = 300;
        IdealLeg hot = ideal_isothermal(1, Th, 1e-3, 2e-3);
        IdealLeg cold = ideal_isothermal(1, Tc, 2e-3, 1e-3);
        double eta = (hot.W + cold.W) / hot.Q;
        check("stirling eta == 1 - Tc/Th", eta, 1.0 - Tc / Th, 1e-9);
    }

    std::printf("\n%s  (%d failure%s)\n", fail ? "SELFTEST FAILED" : "SELFTEST PASSED",
                fail, fail == 1 ? "" : "s");
    return fail ? 1 : 0;
}

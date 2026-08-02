// 6GGW / NetSwitch — SISO transfer-function analyser
// ---------------------------------------------------------------------------
// The client's "FROM NOKIA MATERIAL year 2022" block is a single-input /
// single-output linear transfer function written as a product of quadratics:
//
//   G(s) = K * (s^2 + 3.2 s + 7.2)(s^2 - 8 s + 400)
//          -----------------------------------------------------
//          (s + 7)(s^2 - 1.2 s + 0.8)(s^2 + 33 s + 700)
//
// This tool multiplies the factors out into numerator/denominator polynomials,
// finds every root (poles and zeros) by Durand-Kerner, and reports the two
// facts that actually matter for "CALCULATE PID OF PHONE":
//
//   * STABILITY   — is every pole in the left half-plane (Re < 0)?  Here it is
//                   NOT: the factor (s^2 - 1.2 s + 0.8) has roots at
//                   0.6 +/- 0.7 j, i.e. Re > 0, so the open loop is UNSTABLE
//                   and needs feedback (the PID) to be usable.
//   * MINIMUM PHASE — are all zeros in the left half-plane?  Here NO: the factor
//                   (s^2 - 8 s + 400) puts zeros at 4 +/- 19.6 j (Re > 0), so the
//                   plant is NON-MINIMUM-PHASE — a right-half-plane zero, which
//                   hard-limits how fast any controller can push it. This is the
//                   honest reason a device like this "can't calculate fast" until
//                   the loop is tuned; it is a real property of his own numbers.
//
// The default plant IS the client's Nokia function. Pass your own factors with
// --num / --den (comma-separated quadratic/linear factors) to analyse another.
// The math is exact/standard: roots are cross-checked against the polynomial's
// trace (sum of roots == -a_{n-1}/a_n) and product (== (-1)^n a_0/a_n).
//
// Build:   g++ -std=c++17 -O2 ggw_siso.cpp -o ggw_siso
// Verify:  ggw_siso selftest
// Use:     ggw_siso                      (analyse the Nokia plant)
//          ggw_siso --K 20               (set the loop gain used for the DC gain)
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <complex>
#include <vector>
#include <string>
#include <algorithm>

using cd = std::complex<double>;

// polynomial as coefficients, highest power first:  p[0] s^n + ... + p[n]
using Poly = std::vector<double>;

static Poly poly_mul(const Poly& a, const Poly& b) {
    Poly r(a.size() + b.size() - 1, 0.0);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j)
            r[i + j] += a[i] * b[j];
    return r;
}

static cd poly_eval(const Poly& p, cd x) {
    cd r = 0;
    for (double c : p) r = r * x + c;   // Horner, highest-first
    return r;
}

// Durand-Kerner: all roots (real or complex) of a polynomial at once.
static std::vector<cd> roots(Poly p) {
    while (p.size() > 1 && std::fabs(p.front()) < 1e-15) p.erase(p.begin());   // drop leading zeros
    int n = (int)p.size() - 1;
    std::vector<cd> z(n);
    if (n <= 0) return z;
    // monic copy
    Poly m(p); for (double& c : m) c /= p[0];
    cd seed(0.4, 0.9);
    for (int i = 0; i < n; ++i) z[i] = std::pow(seed, i);
    for (int it = 0; it < 2000; ++it) {
        double move = 0;
        for (int i = 0; i < n; ++i) {
            cd denom = 1;
            for (int j = 0; j < n; ++j) if (j != i) denom *= (z[i] - z[j]);
            cd d = poly_eval(m, z[i]) / denom;
            z[i] -= d;
            move = std::max(move, std::abs(d));
        }
        if (move < 1e-14) break;
    }
    return z;
}

static Poly parse_factors(const std::string& spec) {
    // factors separated by ';' ; each factor coefficients separated by ',' highest-first
    Poly acc = {1.0};
    size_t i = 0;
    while (i < spec.size()) {
        size_t e = spec.find(';', i);
        std::string f = spec.substr(i, e == std::string::npos ? std::string::npos : e - i);
        Poly fac; size_t j = 0;
        while (j < f.size()) {
            size_t c = f.find(',', j);
            fac.push_back(std::atof(f.substr(j, c == std::string::npos ? std::string::npos : c - j).c_str()));
            if (c == std::string::npos) break; j = c + 1;
        }
        if (!fac.empty()) acc = poly_mul(acc, fac);
        if (e == std::string::npos) break; i = e + 1;
    }
    return acc;
}

static void print_poly(const char* label, const Poly& p) {
    std::printf("  %s (highest power first): ", label);
    for (size_t i = 0; i < p.size(); ++i) std::printf("%s%.4g", i ? ", " : "", p[i]);
    std::printf("\n");
}

static void report_roots(const char* what, const std::vector<cd>& r, bool& all_lhp) {
    all_lhp = true;
    std::printf("  %s (%zu):\n", what, r.size());
    for (auto z : r) {
        double re = z.real(), im = z.imag();
        if (std::fabs(im) < 1e-9) im = 0;
        bool lhp = re < -1e-9;
        if (!lhp) all_lhp = false;
        std::printf("     %8.4f %s %7.4f j   Re %s 0  [%s]\n",
                    re, im < 0 ? "-" : "+", std::fabs(im),
                    re < -1e-9 ? "<" : (re > 1e-9 ? ">" : "="),
                    lhp ? "stable half" : "right/imag half");
    }
}

int cmd_selftest();

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "selftest")) return cmd_selftest();

    double K = 1.0;
    std::string numspec, denspec;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--K" && i + 1 < argc) K = std::atof(argv[++i]);
        else if (a == "--num" && i + 1 < argc) numspec = argv[++i];
        else if (a == "--den" && i + 1 < argc) denspec = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::printf("ggw_siso [--K gain] [--num f1;f2;..] [--den f1;f2;..]\n"
                        "  factors are comma-separated coefficients, highest power first\n"
                        "  e.g. a quadratic s^2-8s+400 is \"1,-8,400\"\n"
                        "  default plant is the client's Nokia 2022 transfer function\n");
            return 0;
        }
    }

    // default = the client's Nokia function
    Poly num = numspec.empty()
        ? poly_mul(parse_factors("1,3.2,7.2"), parse_factors("1,-8,400"))
        : parse_factors(numspec);
    Poly den = denspec.empty()
        ? poly_mul(poly_mul(parse_factors("1,7"), parse_factors("1,-1.2,0.8")), parse_factors("1,33,700"))
        : parse_factors(denspec);

    std::printf("6GGW / NetSwitch SISO analyser   (loop gain K = %g)\n\n", K);
    print_poly("numerator  N(s)", num);
    print_poly("denominator D(s)", den);
    std::printf("\n");

    auto z = roots(num);
    auto p = roots(den);
    bool zeros_lhp, poles_lhp;
    report_roots("ZEROS  (roots of N)", z, zeros_lhp);
    std::printf("\n");
    report_roots("POLES  (roots of D)", p, poles_lhp);

    // DC gain G(0) = K * N(0)/D(0)
    double dc = (std::fabs(den.back()) > 1e-30) ? K * num.back() / den.back() : NAN;
    std::printf("\n  DC gain G(0) = K * N(0)/D(0) = %.6g\n", dc);
    std::printf("\n  STABILITY     : %s (all poles Re<0? %s)\n",
                poles_lhp ? "STABLE open loop" : "UNSTABLE open loop -> needs feedback/PID",
                poles_lhp ? "yes" : "NO");
    std::printf("  MINIMUM PHASE : %s (all zeros Re<0? %s)\n",
                zeros_lhp ? "minimum-phase" : "NON-minimum-phase -> right-half-plane zero caps loop speed",
                zeros_lhp ? "yes" : "NO");
    std::printf("\nThese two facts are exact properties of your own coefficients — this is why\n"
                "the plant needs the PID and why there is a hard ceiling on how fast it settles.\n"
                "Give me your target settling time / overshoot and I'll tune a PID against it.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static bool close_(double x, double y, double t) { return std::fabs(x - y) < t; }

int cmd_selftest() {
    int fail = 0;
    auto ck = [&](const char* w, bool ok) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fail;
    };
    std::printf("siso selftest — roots vs known factors, trace/product identities\n\n");

    // 1) roots of (s-1)(s-2)(s-3) = s^3 -6s^2 +11s -6 are 1,2,3
    {
        Poly p = poly_mul(poly_mul(Poly{1,-1}, Poly{1,-2}), Poly{1,-3});
        auto r = roots(p);
        std::vector<double> re; for (auto z : r) re.push_back(z.real());
        std::sort(re.begin(), re.end());
        ck("cubic roots == 1,2,3",
           close_(re[0],1,1e-6) && close_(re[1],2,1e-6) && close_(re[2],3,1e-6));
        double sum = 0, prod = 1; for (double x : re) { sum += x; prod *= x; }
        ck("trace == -a1/a0 (=6)", close_(sum, 6, 1e-6));
        ck("product == (-1)^n a_n/a0 (=6)", close_(prod, 6, 1e-6));   // n=3: -(-6)=6
    }

    // 2) complex roots: s^2 - 8s + 400 -> 4 +/- j*sqrt(384)
    {
        auto r = roots(Poly{1,-8,400});
        double im = std::sqrt(384.0);
        bool ok = close_(r[0].real(),4,1e-6) && close_(r[1].real(),4,1e-6)
               && close_(std::fabs(r[0].imag()), im, 1e-4);
        ck("s^2-8s+400 -> 4 +/- j*sqrt(384) (RHP zero)", ok);
    }

    // 3) the client's plant: denominator has a right-half-plane pole (unstable),
    //    numerator has a right-half-plane zero (non-minimum-phase)
    {
        Poly num = poly_mul(Poly{1,3.2,7.2}, Poly{1,-8,400});
        Poly den = poly_mul(poly_mul(Poly{1,7}, Poly{1,-1.2,0.8}), Poly{1,33,700});
        auto z = roots(num); auto p = roots(den);
        bool zl=true, pl=true;
        for (auto v : z) if (v.real() >= -1e-9) zl = false;
        for (auto v : p) if (v.real() >= -1e-9) pl = false;
        ck("Nokia plant is UNSTABLE (a pole Re>0)", !pl);
        ck("Nokia plant is NON-minimum-phase (a zero Re>0)", !zl);
    }

    std::printf("\n%s (%d failure%s)\n", fail ? "SELFTEST FAILED" : "SELFTEST PASSED",
                fail, fail == 1 ? "" : "s");
    return fail ? 1 : 0;
}

// 6GGW / NetSwitch — thermal telemetry reader
// ---------------------------------------------------------------------------
// Enumerates every temperature sensor the platform exposes, reports the live
// per-part temperature, the nearest thermal trip point and the headroom to it,
// and (with --watch) learns the device's *heat pattern* over time: per-part
// min / max / mean / drift, and which parts move the most under load.
//
// This is the "impress the industry" telemetry piece: it treats the phone /
// gateway as a set of parts (CPU cores, GPU, battery, modem/RF, board zones,
// each screen's own controller) and shows the system staying inside its
// thermal envelope — the same physical picture as a heat-expansion sim, read
// from the device's real kernel sensors instead of a CAD model.
//
// Honest scope:
//   * Every temperature printed is a REAL kernel sensor reading (milli-°C from
//     /sys/class/thermal and /sys/class/hwmon). Nothing is invented.
//   * What a given device exposes is up to its kernel. Modern phones expose CPU
//     cluster, GPU, battery, PMIC and often modem/RF ("mtktscpu", "sdm-therm",
//     "modem", "pa-therm" …) zones. Per-antenna or per-screen sensors appear
//     ONLY where the vendor wired them; parts with no sensor are listed as
//     "no sensor" rather than guessed.
//   * Trip points (passive / hot / critical) are read from the same sysfs, so
//     "headroom" is the device's own throttle threshold, not a made-up limit.
//
// Portable to Linux and Android (both use /sys/class/thermal). On Windows the
// equivalent source is WMI MSAcpi_ThermalZoneTemperature / a vendor driver —
// see README; this file targets the Linux/Android sysfs path.
//
// Build:   g++ -std=c++17 -O2 ggw_thermal.cpp -o ggw_thermal
// Verify:  ggw_thermal --root ./sample_sys        (synthetic tree, known temps)
// Live:    ggw_thermal                              (reads the real device)
//          ggw_thermal --watch 20 --interval 500    (heat-pattern learn, 20 samples)
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <ctime>
#include <cerrno>

static std::string slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string s; char buf[512]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
static bool exists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false; std::fclose(f); return true;
}
static bool is_int(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0; if (i >= s.size()) return false;
    for (; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
// milli-°C (or milli- for hwmon) -> °C. Kernel thermal/hwmon report in milli-degrees.
static double milli_to_c(const std::string& raw) {
    if (!is_int(raw)) return NAN;
    return std::strtoll(raw.c_str(), nullptr, 10) / 1000.0;
}

struct Trip { double temp; std::string type; };   // passive / hot / critical …
struct Part {
    std::string source;   // sysfs path
    std::string kind;     // "thermal-zone" | "hwmon"
    std::string name;     // human name: zone "type" or hwmon name[:label]
    double temp = NAN;    // °C, live
    std::vector<Trip> trips;
    // heat-pattern accumulators (filled by --watch)
    double tmin = NAN, tmax = NAN, tsum = 0, tlast = NAN; long samples = 0;
};

// nearest trip point ABOVE the current temp (the one we'd hit next)
static const Trip* next_trip(const Part& p) {
    const Trip* best = nullptr;
    for (const auto& t : p.trips) {
        if (!std::isfinite(t.temp) || t.temp <= 0) continue;
        if (std::isfinite(p.temp) && t.temp < p.temp) continue;   // already past it
        if (!best || t.temp < best->temp) best = &t;
    }
    if (!best) {  // all trips already passed (or none above) -> pick highest defined
        for (const auto& t : p.trips)
            if (std::isfinite(t.temp) && t.temp > 0 && (!best || t.temp > best->temp)) best = &t;
    }
    return best;
}

static void read_zone(const std::string& dir, const std::string& base, std::vector<Part>& out) {
    Part p; p.kind = "thermal-zone"; p.source = dir;
    p.name = slurp(dir + "/type"); if (p.name.empty()) p.name = base;
    p.temp = milli_to_c(slurp(dir + "/temp"));
    for (int i = 0; i < 32; ++i) {
        std::string tp = dir + "/trip_point_" + std::to_string(i) + "_temp";
        if (!exists(tp)) continue;
        Trip t; t.temp = milli_to_c(slurp(tp));
        t.type = slurp(dir + "/trip_point_" + std::to_string(i) + "_type");
        if (std::isfinite(t.temp) && t.temp > 0) p.trips.push_back(t);
    }
    out.push_back(std::move(p));
}

static void read_hwmon(const std::string& dir, std::vector<Part>& out) {
    std::string chip = slurp(dir + "/name"); if (chip.empty()) chip = "hwmon";
    for (int i = 1; i <= 64; ++i) {
        std::string in = dir + "/temp" + std::to_string(i) + "_input";
        if (!exists(in)) continue;
        Part p; p.kind = "hwmon"; p.source = in;
        std::string lbl = slurp(dir + "/temp" + std::to_string(i) + "_label");
        p.name = lbl.empty() ? (chip + ":temp" + std::to_string(i)) : (chip + ":" + lbl);
        p.temp = milli_to_c(slurp(in));
        for (const char* k : {"crit", "max", "emergency"}) {
            std::string tp = dir + "/temp" + std::to_string(i) + "_" + k;
            if (exists(tp)) { double v = milli_to_c(slurp(tp));
                if (std::isfinite(v) && v > 0) p.trips.push_back({v, k}); }
        }
        out.push_back(std::move(p));
    }
}

static std::vector<Part> enumerate(const std::string& root) {
    std::vector<Part> parts;
    // /sys/class/thermal/thermal_zone*
    std::string tdir = root + "/class/thermal";
    // allow --root to point straight at a folder holding thermal_zone*/hwmon* too
    std::vector<std::string> tbases = { tdir, root };
    for (const auto& base : tbases) {
        DIR* d = opendir(base.c_str()); if (!d) continue;
        dirent* e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n.rfind("thermal_zone", 0) == 0) read_zone(base + "/" + n, n, parts);
        }
        closedir(d);
    }
    // /sys/class/hwmon/hwmon*
    std::vector<std::string> hbases = { root + "/class/hwmon", root };
    for (const auto& base : hbases) {
        DIR* d = opendir(base.c_str()); if (!d) continue;
        dirent* e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n.rfind("hwmon", 0) == 0) read_hwmon(base + "/" + n, parts);
        }
        closedir(d);
    }
    return parts;
}

static const char* status_for(const Part& p, double headroom) {
    if (!std::isfinite(p.temp)) return "no sensor";
    if (std::isfinite(headroom)) {
        if (headroom <= 0)  return "THROTTLE";
        if (headroom <= 5)  return "hot";
        if (headroom <= 15) return "warm";
    }
    return "ok";
}

static void snapshot(std::vector<Part>& parts) {
    std::printf("\n%-26s %-12s %8s %-14s %9s  %s\n",
                "PART (sensor)", "kind", "temp C", "next trip", "headroom", "status");
    std::printf("%s\n", std::string(88, '-').c_str());
    double hottest = -1e9; std::string hottest_name;
    for (auto& p : parts) {
        const Trip* nt = next_trip(p);
        double head = (nt && std::isfinite(p.temp)) ? (nt->temp - p.temp) : NAN;
        char tbuf[24], trbuf[24], hbuf[24];
        if (std::isfinite(p.temp)) std::snprintf(tbuf, sizeof tbuf, "%7.1f", p.temp);
        else                       std::snprintf(tbuf, sizeof tbuf, "%7s", "-");
        if (nt) std::snprintf(trbuf, sizeof trbuf, "%s %.0f", nt->type.c_str(), nt->temp);
        else    std::snprintf(trbuf, sizeof trbuf, "%s", "-");
        if (std::isfinite(head)) std::snprintf(hbuf, sizeof hbuf, "%+7.1f", head);
        else                     std::snprintf(hbuf, sizeof hbuf, "%7s", "-");
        std::printf("%-26.26s %-12.12s %8s %-14.14s %9s  %s\n",
                    p.name.c_str(), p.kind.c_str(), tbuf, trbuf, hbuf, status_for(p, head));
        if (std::isfinite(p.temp) && p.temp > hottest) { hottest = p.temp; hottest_name = p.name; }
    }
    if (hottest > -1e8)
        std::printf("\nhottest part: %s at %.1f C\n", hottest_name.c_str(), hottest);
}

// heat-pattern learn: sample every part N times, then report drift/movement.
static void watch(const std::string& root, int n, int interval_ms) {
    std::vector<Part> acc = enumerate(root);
    if (acc.empty()) { std::printf("no sensors to watch.\n"); return; }
    for (int s = 0; s < n; ++s) {
        std::vector<Part> cur = enumerate(root);
        for (size_t i = 0; i < acc.size() && i < cur.size(); ++i) {
            double t = cur[i].temp; if (!std::isfinite(t)) continue;
            Part& a = acc[i];
            a.tmin = std::isfinite(a.tmin) ? std::min(a.tmin, t) : t;
            a.tmax = std::isfinite(a.tmax) ? std::max(a.tmax, t) : t;
            a.tsum += t; a.tlast = t; a.samples++;
        }
        if (s + 1 < n) {
            struct timespec ts{ interval_ms / 1000, (long)(interval_ms % 1000) * 1000000L };
            nanosleep(&ts, nullptr);
        }
    }
    std::printf("\nheat pattern over %d samples (%d ms apart):\n\n", n, interval_ms);
    std::printf("%-26s %8s %8s %8s %8s %8s\n",
                "PART (sensor)", "min", "mean", "max", "swing", "drift");
    std::printf("%s\n", std::string(72, '-').c_str());
    // "drift" = last - first-seen mean is noisy; use last - min as rise indicator.
    std::vector<std::pair<double,std::string>> movers;
    for (auto& a : acc) {
        if (a.samples == 0) {
            std::printf("%-26.26s %8s %8s %8s %8s %8s   (no sensor)\n",
                        a.name.c_str(), "-", "-", "-", "-", "-");
            continue;
        }
        double mean = a.tsum / a.samples;
        double swing = a.tmax - a.tmin;
        double drift = a.tlast - a.tmin;
        std::printf("%-26.26s %8.1f %8.1f %8.1f %8.1f %+8.1f\n",
                    a.name.c_str(), a.tmin, mean, a.tmax, swing, drift);
        movers.push_back({swing, a.name});
    }
    std::sort(movers.begin(), movers.end(),
              [](auto& x, auto& y){ return x.first > y.first; });
    if (!movers.empty()) {
        std::printf("\nmost thermally active parts (biggest swing = the ones that expand/contract most):\n");
        for (size_t i = 0; i < movers.size() && i < 3; ++i)
            std::printf("  %zu. %s  (%.1f C swing)\n", i + 1, movers[i].second.c_str(), movers[i].first);
    }
    std::printf("\nnote: swing is how far a part moved across the run; under sustained load the\n"
                "parts with the largest swing are the ones whose expansion most shifts the\n"
                "board/antenna geometry — the ones the heat-pattern model should weight.\n");
}

int main(int argc, char** argv) {
    std::string root = "/sys";
    int watch_n = 0, interval_ms = 1000;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) root = argv[++i];
        else if (a == "--watch" && i + 1 < argc) watch_n = std::atoi(argv[++i]);
        else if (a == "--interval" && i + 1 < argc) interval_ms = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "6GGW / NetSwitch thermal telemetry\n\n"
                "  ggw_thermal                       one live snapshot of every sensor\n"
                "  ggw_thermal --watch N             learn the heat pattern over N samples\n"
                "  ggw_thermal --watch N --interval MS   sample every MS ms (default 1000)\n"
                "  ggw_thermal --root PATH           read from PATH instead of /sys (for testing)\n\n"
                "Reads real kernel sensors from /sys/class/thermal and /sys/class/hwmon.\n"
                "Parts with no sensor on this device are shown as 'no sensor', never guessed.\n");
            return 0;
        }
    }
    std::printf("6GGW / NetSwitch thermal telemetry  (source: %s)\n", root.c_str());
    std::vector<Part> parts = enumerate(root);
    if (parts.empty()) {
        std::printf("\nno temperature sensors exposed at %s.\n"
                    "  - on a real phone/gateway this lists CPU/GPU/battery/modem/board zones.\n"
                    "  - on a stripped VM/container the kernel exposes none (that's this build box).\n"
                    "  - point --root at a device's /sys, or at a captured sensor tree, to read them.\n",
                    root.c_str());
        return 0;
    }
    snapshot(parts);
    if (watch_n > 1) watch(root, watch_n, interval_ms);
    return 0;
}

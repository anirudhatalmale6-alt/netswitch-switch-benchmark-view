// 6GGW SERVER COMPONENT  —  native C++ port of server.js (the "2. server" half).
//
// Same product, same wire API, no runtime interpreter. Runs on any Linux box
// (HCL / Fujitsu / a laptop) as one static-ish binary:
//
//     ./ggw_server                 # listens on :8090
//     PORT=80 ./ggw_server
//     CHANNEL=rc PORT=8091 ./ggw_server
//
// What it REALLY does (all measurements are real; nothing modelled on this side):
//   GET  /api/health            — this server's own live health (load, memory, uptime, cores)
//   GET  /api/ping              — tiny echo; the CLIENT times it for the real server<->phone line length
//   GET  /api/distance?host=..  — DNS resolve + real TCP-handshake RTT (best of 3) -> signal-path km
//                                 using the client's cable constant d = 0.0001607 m per picosecond
//   GET  /api/estimate?users&bitrate&server — MPEG-4/broadband delivery capacity for N post addresses
//   POST /api/device/health     — store a device's health snapshot in its OWN AES-256-GCM DB
//   GET  /api/device/history?id=..  — read that device's stored history (decrypted)
//   GET  /api/devices           — list known device IDs
//   POST /api/backup            — bundle every (still-encrypted) device DB into one gzip archive
//   GET  /api/backups           — list backup archives
//   GET  /api/security          — round-the-clock watch status + firewall/egress self-test
//   GET  /api/security/log?n=50 — tail the security log
//   GET  /                      — serves the bundled 6GGW client (the PWA) so one box ships both halves
//
// Envelope compatibility: the per-device DB uses exactly the same AES-256-GCM envelope as the Node
// build — iv(12) | tag(16) | ciphertext — with the key = SHA-256(DB_KEY). So a DB written by either
// build decrypts on the other; only the plaintext record layout differs (this build uses NDJSON).
//
// Build:  cmake -B build && cmake --build build      (or:  ./build.sh)
// Deps:   OpenSSL (AES-256-GCM + SHA-256) and zlib (gzip) — both ship on every mainstream Linux.

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ----------------------------------------------------------------------------- config
static const char*  VERSION      = "0.4.0";           // matches the Node build's API version
static const char*  BUILD_TAG    = "c++";
static const double SIGNAL_CONST = 0.0001607;         // metres per picosecond (client's cable constant)

static int    PORT       = 8090;
static std::string CHANNEL = "dev";                   // dev | rc | prod
static int    SHORT_MS   = 15000;                     // security short arm (minute hand)
static int    LONG_MS    = 300000;                    // security long arm  (hour hand)
static std::string FW_HOST = "one.one.one.one";       // egress reachability probe
static int    FW_PORT    = 443;

static std::string BASE_DIR;                          // dir of the binary
static std::string CLIENT_DIR;                        // the PWA lives one level up
static std::string DB_DIR;                            // data/devices
static std::string BACKUP_DIR;                        // data/backups
static std::string SEC_DIR;                           // data/security
static unsigned char DB_KEY[32];                      // SHA-256(DB_KEY env)

static std::chrono::steady_clock::time_point START_TP;

// ----------------------------------------------------------------------------- small utils
static std::string envOr(const char* k, const std::string& d) {
  const char* v = getenv(k);
  return (v && *v) ? std::string(v) : d;
}
static long nowMs() {
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}
static std::string isoNow() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}
static std::string isoDate() { return isoNow().substr(0, 10); }
static double roundN(double v, int n) {
  double m = 1; for (int i = 0; i < n; i++) m *= 10;
  return std::round(v * m) / m;
}
static std::string num(double v, int n = -1) {   // trim trailing zeros for clean JSON numbers
  std::ostringstream o;
  if (n >= 0) { o.precision(n); o << std::fixed << v; }
  else        { o.precision(10); o << v; }
  std::string s = o.str();
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s;
}
static std::string jstr(const std::string& s) {   // JSON-escape a string, with quotes
  std::string o = "\"";
  for (char c : s) {
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  o += "\"";
  return o;
}
static void mkdirp(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) {
    cur += p[i];
    if (p[i] == '/' || i + 1 == p.size()) {
      if (cur != "/" ) mkdir(cur.c_str(), 0755);
    }
  }
}
static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
  return true;
}
static bool writeFile(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
}
static std::string dirOfSelf() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  std::string p = (n > 0) ? std::string(buf, n) : std::string(".");
  size_t s = p.find_last_of('/');
  return (s == std::string::npos) ? "." : p.substr(0, s);
}

// ----------------------------------------------------------------------------- crypto (OpenSSL)
static std::string sha256hex(const unsigned char* data, size_t len) {
  unsigned char h[SHA256_DIGEST_LENGTH];
  SHA256(data, len, h);
  static const char* hx = "0123456789abcdef";
  std::string o;
  for (unsigned char b : h) { o += hx[b >> 4]; o += hx[b & 15]; }
  return o;
}
[[maybe_unused]] static std::string sha256hex(const std::string& s) {
  return sha256hex((const unsigned char*)s.data(), s.size());
}
// AES-256-GCM: output = iv(12) | tag(16) | ciphertext  (byte-identical layout to the Node build)
static std::string encrypt(const std::string& plain) {
  unsigned char iv[12]; RAND_bytes(iv, sizeof iv);
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
  EVP_EncryptInit_ex(ctx, nullptr, nullptr, DB_KEY, iv);
  std::string ct(plain.size() + 16, '\0');
  int outl = 0, tot = 0;
  EVP_EncryptUpdate(ctx, (unsigned char*)&ct[0], &outl,
                    (const unsigned char*)plain.data(), (int)plain.size());
  tot = outl;
  EVP_EncryptFinal_ex(ctx, (unsigned char*)&ct[0] + tot, &outl);
  tot += outl;
  ct.resize(tot);
  unsigned char tag[16];
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
  EVP_CIPHER_CTX_free(ctx);
  std::string out;
  out.append((char*)iv, 12);
  out.append((char*)tag, 16);
  out.append(ct);
  return out;
}
static bool decrypt(const std::string& buf, std::string& out) {
  if (buf.size() < 28) return false;
  const unsigned char* iv  = (const unsigned char*)buf.data();
  const unsigned char* tag = (const unsigned char*)buf.data() + 12;
  const unsigned char* data = (const unsigned char*)buf.data() + 28;
  int dlen = (int)buf.size() - 28;
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
  EVP_DecryptInit_ex(ctx, nullptr, nullptr, DB_KEY, iv);
  std::string pt(dlen > 0 ? dlen : 0, '\0');
  int outl = 0, tot = 0;
  if (dlen > 0) { EVP_DecryptUpdate(ctx, (unsigned char*)&pt[0], &outl, data, dlen); tot = outl; }
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
  int ok = EVP_DecryptFinal_ex(ctx, (unsigned char*)&pt[0] + tot, &outl);
  tot += outl;
  EVP_CIPHER_CTX_free(ctx);
  if (ok <= 0) return false;
  pt.resize(tot);
  out.swap(pt);
  return true;
}
static std::string gzipBytes(const std::string& in) {
  z_stream zs{};
  deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  zs.next_in = (Bytef*)in.data();
  zs.avail_in = (uInt)in.size();
  std::string out;
  char buf[16384];
  int ret;
  do {
    zs.next_out = (Bytef*)buf;
    zs.avail_out = sizeof buf;
    ret = deflate(&zs, Z_FINISH);
    out.append(buf, sizeof buf - zs.avail_out);
  } while (ret != Z_STREAM_END);
  deflateEnd(&zs);
  return out;
}

// ----------------------------------------------------------------------------- device DB (NDJSON in an AES-GCM envelope)
static std::mutex g_dbMx;
static std::string safeId(const std::string& id) {
  std::string o;
  for (char c : id)
    if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') o += c;
  if (o.size() > 64) o.resize(64);
  return o.empty() ? "unknown" : o;
}
static std::string deviceFile(const std::string& id) { return DB_DIR + "/" + safeId(id) + ".db"; }
static std::vector<std::string> readHistory(const std::string& id) {  // each element = one record's JSON text
  std::vector<std::string> rows;
  std::string enc;
  if (!readFile(deviceFile(id), enc)) return rows;
  std::string plain;
  if (!decrypt(enc, plain)) return rows;
  std::istringstream ss(plain);
  std::string line;
  while (std::getline(ss, line)) if (!line.empty()) rows.push_back(line);
  return rows;
}
// insert received_at + channel at the front of the posted JSON object (mirrors Node's Object.assign order)
static std::string decorate(const std::string& body) {
  std::string head = "\"received_at\":" + jstr(isoNow()) + ",\"channel\":" + jstr(CHANNEL);
  std::string b = body;
  size_t o = b.find('{');
  if (o == std::string::npos) return "{" + head + ",\"record\":" + jstr(body) + "}";
  size_t after = o + 1;
  // if the object already has fields, add a comma
  size_t firstNonWs = b.find_first_not_of(" \t\r\n", after);
  bool hasFields = (firstNonWs != std::string::npos && b[firstNonWs] != '}');
  std::string ins = head + (hasFields ? "," : "");
  // strip newlines so the record stays a single NDJSON line
  std::string one;
  for (char c : b) if (c != '\n' && c != '\r') one += c;
  size_t oo = one.find('{');
  return one.substr(0, oo + 1) + ins + one.substr(oo + 1);
}
static std::string saveHealth(const std::string& id, const std::string& body) {
  std::lock_guard<std::mutex> lk(g_dbMx);
  mkdirp(DB_DIR);
  auto rows = readHistory(id);
  std::string row = decorate(body);
  rows.push_back(row);
  while (rows.size() > 500) rows.erase(rows.begin());
  std::string joined;
  for (auto& r : rows) { joined += r; joined += "\n"; }
  writeFile(deviceFile(id), encrypt(joined));
  return "{\"stored\":true,\"id\":" + jstr(safeId(id)) +
         ",\"count\":" + std::to_string(rows.size()) + ",\"record\":" + row + "}";
}
static std::vector<std::string> listDevices() {
  std::vector<std::string> out;
  DIR* d = opendir(DB_DIR.c_str());
  if (!d) return out;
  struct dirent* e;
  while ((e = readdir(d))) {
    std::string n = e->d_name;
    if (n.size() > 3 && n.substr(n.size() - 3) == ".db") out.push_back(n.substr(0, n.size() - 3));
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

// ----------------------------------------------------------------------------- real TCP-handshake RTT
// non-blocking connect timed with steady_clock; returns ms, or -1 on failure/timeout.
static double tcpRtt(const std::string& host, int port, int timeoutMs) {
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  std::string ports = std::to_string(port);
  if (getaddrinfo(host.c_str(), ports.c_str(), &hints, &res) != 0 || !res) return -1;
  double best = -1;
  for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    auto t0 = std::chrono::steady_clock::now();
    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    bool ok = false;
    if (r == 0) ok = true;
    else if (errno == EINPROGRESS) {
      fd_set ws; FD_ZERO(&ws); FD_SET(fd, &ws);
      struct timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
      if (select(fd + 1, nullptr, &ws, nullptr, &tv) > 0) {
        int err = 0; socklen_t l = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
        ok = (err == 0);
      }
    }
    if (ok) {
      double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                      std::chrono::steady_clock::now() - t0).count();
      best = ms;
    }
    close(fd);
    if (ok) break;
  }
  freeaddrinfo(res);
  return best;
}
// resolve host -> its IP as text (first A/AAAA), for the /distance report
static std::string resolveIp(const std::string& host) {
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return "";
  char ip[INET6_ADDRSTRLEN] = {0};
  if (res->ai_family == AF_INET)
    inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ip, sizeof ip);
  else
    inet_ntop(AF_INET6, &((sockaddr_in6*)res->ai_addr)->sin6_addr, ip, sizeof ip);
  freeaddrinfo(res);
  return ip;
}

// ----------------------------------------------------------------------------- security watch
struct SecState {
  long started = 0;
  std::atomic<long> reqTotal{0}, errTotal{0};
  long reqWindow = 0, errWindow = 0;
  std::map<std::string, long> byIp;
  std::string lastShort, lastLong;
  int egressOk = -1;            // -1 unknown, 0 false, 1 true
  double egressMs = -1;
  std::string egressChecked;
  std::vector<std::string> alerts;   // JSON objects
  std::mutex mx;
} SEC;

static std::string secLogFile() { return SEC_DIR + "/security-" + isoDate() + ".log"; }
static void secLog(const std::string& level, const std::string& event, const std::string& detailJson) {
  mkdirp(SEC_DIR);
  std::string line = "{\"ts\":" + jstr(isoNow()) + ",\"level\":" + jstr(level) +
                     ",\"event\":" + jstr(event) + ",\"channel\":" + jstr(CHANNEL);
  if (!detailJson.empty() && detailJson != "{}") line += "," + detailJson.substr(1, detailJson.size() - 2);
  line += "}";
  std::ofstream f(secLogFile(), std::ios::app);
  if (f) f << line << "\n";
  if (level == "ALERT") {
    std::lock_guard<std::mutex> lk(SEC.mx);
    SEC.alerts.push_back(line);
    while (SEC.alerts.size() > 50) SEC.alerts.erase(SEC.alerts.begin());
  }
}
static void secObserve(const std::string& method, const std::string& path, int code, long ms, const std::string& ip) {
  SEC.reqTotal++;
  {
    std::lock_guard<std::mutex> lk(SEC.mx);
    SEC.reqWindow++;
    if (code >= 400) { SEC.errTotal++; SEC.errWindow++; }
    SEC.byIp[ip]++;
  }
  std::string d = "{\"ip\":" + jstr(ip) + ",\"method\":" + jstr(method) +
                  ",\"path\":" + jstr(path) + ",\"code\":" + std::to_string(code) +
                  ",\"ms\":" + std::to_string(ms) + "}";
  if (code >= 400) secLog("WARN", "http_error", d);
  else if (method != "GET") secLog("INFO", "http_write", d);
}
static void secEgressCheck() {
  double ms = tcpRtt(FW_HOST, FW_PORT, 4000);
  std::lock_guard<std::mutex> lk(SEC.mx);
  if (ms >= 0) { SEC.egressOk = 1; SEC.egressMs = roundN(ms, 1); }
  else {
    SEC.egressOk = 0; SEC.egressMs = -1;
    secLog("ALERT", "egress_blocked",
           "{\"target\":" + jstr(FW_HOST + ":" + std::to_string(FW_PORT)) +
           ",\"hint\":\"outbound firewall may be blocking this port\"}");
  }
  SEC.egressChecked = isoNow();
}
static double loadavg1() { double la[3] = {0,0,0}; getloadavg(la, 3); return la[0]; }
static void secShortTick() {
  SEC.lastShort = isoNow();
  std::lock_guard<std::mutex> lk(SEC.mx);
  if (SEC.reqWindow >= 20 && (double)SEC.errWindow / SEC.reqWindow > 0.5)
    secLog("ALERT", "error_rate_high",
           "{\"window_requests\":" + std::to_string(SEC.reqWindow) +
           ",\"window_errors\":" + std::to_string(SEC.errWindow) + "}");
}
static void secLongTick() {
  SEC.lastLong = isoNow();
  secEgressCheck();
  std::lock_guard<std::mutex> lk(SEC.mx);
  long topN = 0; std::string topIp;
  for (auto& kv : SEC.byIp) if (kv.second > topN) { topN = kv.second; topIp = kv.first; }
  if (topN > 1000)
    secLog("ALERT", "ip_spike", "{\"ip\":" + jstr(topIp) + ",\"requests\":" + std::to_string(topN) + "}");
  struct sysinfo si{}; sysinfo(&si);
  long memFreeMb = (long)((double)si.freeram * si.mem_unit / 1048576.0);
  secLog("INFO", "window_summary",
         "{\"uptime_s\":" + std::to_string((nowMs() - SEC.started) / 1000) +
         ",\"requests\":" + std::to_string(SEC.reqWindow) +
         ",\"errors\":" + std::to_string(SEC.errWindow) +
         ",\"unique_ips\":" + std::to_string(SEC.byIp.size()) +
         ",\"egress_ok\":" + (SEC.egressOk == 1 ? "true" : SEC.egressOk == 0 ? "false" : "null") +
         ",\"load_avg\":" + num(roundN(loadavg1(), 2)) +
         ",\"mem_free_mb\":" + std::to_string(memFreeMb) + "}");
  SEC.reqWindow = 0; SEC.errWindow = 0; SEC.byIp.clear();
}
static std::string secStatusJson() {
  std::lock_guard<std::mutex> lk(SEC.mx);
  std::string alerts = "[";
  size_t start = SEC.alerts.size() > 10 ? SEC.alerts.size() - 10 : 0;
  for (size_t i = start; i < SEC.alerts.size(); i++) { if (i > start) alerts += ","; alerts += SEC.alerts[i]; }
  alerts += "]";
  std::string egOk = SEC.egressOk == 1 ? "true" : SEC.egressOk == 0 ? "false" : "null";
  return "{\"watch\":\"running\",\"channel\":" + jstr(CHANNEL) +
         ",\"started\":" + jstr(isoNow()) +   // report-time; started tracked internally
         ",\"uptime_s\":" + std::to_string((nowMs() - SEC.started) / 1000) +
         ",\"short_arm_ms\":" + std::to_string(SHORT_MS) +
         ",\"long_arm_ms\":" + std::to_string(LONG_MS) +
         ",\"last_short_tick\":" + (SEC.lastShort.empty() ? "null" : jstr(SEC.lastShort)) +
         ",\"last_long_tick\":" + (SEC.lastLong.empty() ? "null" : jstr(SEC.lastLong)) +
         ",\"requests_total\":" + std::to_string(SEC.reqTotal.load()) +
         ",\"errors_total\":" + std::to_string(SEC.errTotal.load()) +
         ",\"firewall\":{\"egress_target\":" + jstr(FW_HOST + ":" + std::to_string(FW_PORT)) +
             ",\"egress_ok\":" + egOk +
             ",\"egress_ms\":" + (SEC.egressMs >= 0 ? num(SEC.egressMs) : "null") +
             ",\"checked\":" + (SEC.egressChecked.empty() ? "null" : jstr(SEC.egressChecked)) + "}" +
         ",\"fw_ok\":" + (SEC.egressOk != 0 ? "true" : "false") +
         ",\"recent_alerts\":" + alerts +
         ",\"log_file\":" + jstr("security-" + isoDate() + ".log") + "}";
}
static std::string secTailJson(int n) {
  std::string content;
  if (!readFile(secLogFile(), content)) return "[]";
  std::vector<std::string> lines;
  std::istringstream ss(content); std::string l;
  while (std::getline(ss, l)) if (!l.empty()) lines.push_back(l);
  int take = std::max(1, std::min(500, n));
  size_t start = lines.size() > (size_t)take ? lines.size() - take : 0;
  std::string out = "[";
  for (size_t i = start; i < lines.size(); i++) { if (i > start) out += ","; out += lines[i]; }
  out += "]";
  return out;
}

// ----------------------------------------------------------------------------- endpoints producing JSON
static std::string healthJson() {
  struct sysinfo si{}; sysinfo(&si);
  double unit = (double)si.mem_unit;
  long totalMb = (long)((double)si.totalram * unit / 1048576.0);
  long freeMb  = (long)((double)si.freeram  * unit / 1048576.0);
  char host[256] = {0}; gethostname(host, sizeof host - 1);
  double la[3] = {0,0,0}; getloadavg(la, 3);
  long uptimeS = (long)std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - START_TP).count();
  size_t alertCount; int egOk;
  { std::lock_guard<std::mutex> lk(SEC.mx); alertCount = SEC.alerts.size(); egOk = SEC.egressOk; }
  return "{\"ok\":true,\"channel\":" + jstr(CHANNEL) +
         ",\"version\":" + jstr(VERSION) + ",\"build\":" + jstr(BUILD_TAG) +
         ",\"host\":" + jstr(host) +
         ",\"platform\":" + jstr("linux") +
         ",\"cpu_cores\":" + std::to_string((long)sysconf(_SC_NPROCESSORS_ONLN)) +
         ",\"load_avg\":[" + num(roundN(la[0],2)) + "," + num(roundN(la[1],2)) + "," + num(roundN(la[2],2)) + "]" +
         ",\"mem_total_mb\":" + std::to_string(totalMb) +
         ",\"mem_free_mb\":" + std::to_string(freeMb) +
         ",\"uptime_s\":" + std::to_string(uptimeS) +
         ",\"security\":{\"watch\":\"running\",\"fw_ok\":" + (egOk != 0 ? "true" : "false") +
             ",\"alerts\":" + std::to_string(alertCount) + "}" +
         ",\"time\":" + jstr(isoNow()) + "}";
}
static std::string qget(const std::map<std::string, std::string>& q, const std::string& k, const std::string& d = "") {
  auto it = q.find(k); return it == q.end() ? d : it->second;
}
static std::string estimateJson(const std::map<std::string, std::string>& q) {
  double bitrate    = std::max(0.05, atof(qget(q, "bitrate", "1.5").c_str()));
  long   users      = std::max(1L, atol(qget(q, "users", "30000").c_str()));
  double serverGbps = std::max(0.1, atof(qget(q, "server", "10").c_str()));
  double serverMbps = serverGbps * 1000;
  double demandMbps = bitrate * users;
  long   cap        = (long)std::floor(serverMbps / bitrate);
  double coverage   = std::min(100.0, (double)cap / users * 100.0);
  long   serversNeeded = (long)std::ceil(demandMbps / serverMbps);
  struct A { const char* name; double dl; };
  A access[3] = { {"Fixed line (fibre)", 500}, {"Mobile Wi-Fi (4G)", 40}, {"5G", 300} };
  std::string acc = "[";
  const char* note[3] = {"typical","typical","typical"};
  for (int i = 0; i < 3; i++) {
    if (i) acc += ",";
    long streams = (long)std::floor(access[i].dl / bitrate);
    acc += "{\"name\":" + jstr(access[i].name) + ",\"downlink_mbps\":" + num(access[i].dl) +
           ",\"note\":" + jstr(note[i]) + ",\"streams_sustained\":" + std::to_string(streams) +
           ",\"fits_stream\":" + (access[i].dl >= bitrate ? "true" : "false") + "}";
  }
  acc += "]";
  return "{\"inputs\":{\"bitrate_mbps\":" + num(bitrate) + ",\"users\":" + std::to_string(users) +
         ",\"server_gbps\":" + num(serverGbps) + "}" +
         ",\"total_demand_mbps\":" + num(roundN(demandMbps,1)) +
         ",\"total_demand_gbps\":" + num(roundN(demandMbps/1000,2)) +
         ",\"streams_one_server_serves\":" + std::to_string(cap) +
         ",\"coverage_percent\":" + num(roundN(coverage,1)) +
         ",\"servers_needed_for_all\":" + std::to_string(serversNeeded) +
         ",\"access_comparison\":" + acc + "}";
}
static std::string distanceJson(const std::string& host, int port) {
  std::string ip = resolveIp(host);
  if (ip.empty()) return "";
  std::vector<double> samples;
  for (int i = 0; i < 3; i++) { double r = tcpRtt(ip, port, 4000); if (r >= 0) samples.push_back(r); }
  if (samples.empty()) return "\x01no TCP response from " + host + ":" + std::to_string(port);
  double rtt = *std::min_element(samples.begin(), samples.end());
  double oneWayKm = SIGNAL_CONST * (rtt * 1e9) / 1000 / 2;
  std::string sj = "[";
  for (size_t i = 0; i < samples.size(); i++) { if (i) sj += ","; sj += num(roundN(samples[i], 2)); }
  sj += "]";
  return "{\"host\":" + jstr(host) + ",\"ip\":" + jstr(ip) + ",\"port\":" + std::to_string(port) +
         ",\"rtt_ms\":" + num(roundN(rtt, 2)) + ",\"samples\":" + sj +
         ",\"signal_path_km\":" + num(roundN(oneWayKm, 1)) +
         ",\"method\":\"TCP-handshake RTT, best of 3, cable constant d=0.0001607 m/ps\"" +
         ",\"measured_at\":" + jstr(isoNow()) + "}";
}
static std::string makeBackupJson() {
  std::lock_guard<std::mutex> lk(g_dbMx);
  mkdirp(BACKUP_DIR);
  auto ids = listDevices();
  std::string files = "[", blobsJson = "{";
  std::string bundleBlobs = "{";
  for (size_t i = 0; i < ids.size(); i++) {
    std::string raw; readFile(deviceFile(ids[i]), raw);
    std::string sh = sha256hex((const unsigned char*)raw.data(), raw.size());
    if (i) files += ",";
    files += "{\"id\":" + jstr(ids[i]) + ",\"bytes\":" + std::to_string(raw.size()) +
             ",\"sha256\":" + jstr(sh) + "}";
  }
  files += "]";
  std::string stamp = isoNow();
  for (auto& c : stamp) if (c == ':' || c == '.') c = '-';
  std::string manifest = "{\"created_at\":" + jstr(isoNow()) + ",\"channel\":" + jstr(CHANNEL) +
                         ",\"version\":" + jstr(VERSION) + ",\"device_count\":" + std::to_string(ids.size()) +
                         ",\"files\":" + files +
                         ",\"encryption\":\"AES-256-GCM at rest (blobs unchanged)\"}";
  // gzip payload = the manifest + base64 blobs (kept AES-encrypted; safe for tape/LTO)
  std::string payload = "{\"manifest\":" + manifest + ",\"blobs\":{";
  for (size_t i = 0; i < ids.size(); i++) {
    std::string raw; readFile(deviceFile(ids[i]), raw);
    // base64 of the encrypted bytes
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc; int val = 0, bits = -6;
    for (unsigned char c : raw) { val = (val << 8) + c; bits += 8;
      while (bits >= 0) { enc += b64[(val >> bits) & 0x3F]; bits -= 6; } }
    if (bits > -6) enc += b64[((val << 8) >> (bits + 8)) & 0x3F];
    while (enc.size() % 4) enc += '=';
    if (i) payload += ",";
    payload += jstr(ids[i]) + ":" + jstr(enc);
  }
  payload += "}}";
  std::string gz = gzipBytes(payload);
  std::string name = "backup-" + CHANNEL + "-" + stamp + ".6ggwbak.gz";
  std::string outPath = BACKUP_DIR + "/" + name;
  writeFile(outPath, gz);
  std::string bundleSha = sha256hex((const unsigned char*)gz.data(), gz.size());
  writeFile(outPath + ".sha256", bundleSha + "  " + name + "\n");
  return "{\"file\":" + jstr(name) + ",\"path\":" + jstr(outPath) +
         ",\"devices\":" + std::to_string(ids.size()) + ",\"bytes\":" + std::to_string(gz.size()) +
         ",\"bundle_sha256\":" + jstr(bundleSha) + ",\"manifest\":" + manifest + "}";
}
static std::string listBackupsJson() {
  std::string out = "[";
  DIR* d = opendir(BACKUP_DIR.c_str());
  if (d) {
    struct dirent* e; bool first = true;
    while ((e = readdir(d))) {
      std::string n = e->d_name;
      if (n.size() > 11 && n.substr(n.size() - 11) == ".6ggwbak.gz") {
        struct stat st{}; stat((BACKUP_DIR + "/" + n).c_str(), &st);
        if (!first) out += ",";
        first = false;
        out += "{\"file\":" + jstr(n) + ",\"bytes\":" + std::to_string((long)st.st_size) + "}";
      }
    }
    closedir(d);
  }
  out += "]";
  return "{\"channel\":" + jstr(CHANNEL) + ",\"dir\":" + jstr(BACKUP_DIR) + ",\"backups\":" + out + "}";
}

// ----------------------------------------------------------------------------- HTTP server
static const std::map<std::string, std::string> MIME = {
  {".html","text/html"}, {".js","application/javascript"}, {".css","text/css"},
  {".json","application/json"}, {".webmanifest","application/manifest+json"}, {".png","image/png"},
  {".bin","application/octet-stream"}, {".switchc","application/x-6ggw-switch"} };

static std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2;
    } else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}
static std::map<std::string, std::string> parseQuery(const std::string& qs) {
  std::map<std::string, std::string> q;
  std::istringstream ss(qs); std::string pair;
  while (std::getline(ss, pair, '&')) {
    size_t eq = pair.find('=');
    if (eq == std::string::npos) q[urlDecode(pair)] = "";
    else q[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
  }
  return q;
}
static void sendAll(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
    if (n <= 0) break;
    off += n;
  }
}
static void sendJson(int fd, int code, const std::string& body) {
  const char* status = code == 200 ? "OK" : code == 400 ? "Bad Request" :
                       code == 404 ? "Not Found" : code == 502 ? "Bad Gateway" : "OK";
  std::ostringstream h;
  h << "HTTP/1.1 " << code << " " << status << "\r\n"
    << "Content-Type: application/json\r\n"
    << "Access-Control-Allow-Origin: *\r\n"
    << "Content-Length: " << body.size() << "\r\n"
    << "Connection: close\r\n\r\n";
  sendAll(fd, h.str() + body);
}
static void sendRaw(int fd, int code, const std::string& ctype, const std::string& body) {
  std::ostringstream h;
  h << "HTTP/1.1 " << code << " " << (code == 200 ? "OK" : code == 404 ? "Not Found" : "Forbidden") << "\r\n"
    << "Content-Type: " << ctype << "\r\n"
    << "Content-Length: " << body.size() << "\r\n"
    << "Connection: close\r\n\r\n";
  sendAll(fd, h.str() + body);
}
static void serveStatic(int fd, const std::string& pathname) {
  std::string rel = (pathname == "/") ? "/index.html" : pathname;
  std::string file = CLIENT_DIR + rel;
  // block traversal
  if (file.find("..") != std::string::npos || file.compare(0, CLIENT_DIR.size(), CLIENT_DIR) != 0) {
    sendRaw(fd, 403, "text/plain", "forbidden"); return;
  }
  std::string data;
  if (!readFile(file, data)) { sendRaw(fd, 404, "text/plain", "not found"); return; }
  std::string ext; size_t dot = file.find_last_of('.');
  if (dot != std::string::npos) ext = file.substr(dot);
  auto it = MIME.find(ext);
  sendRaw(fd, 200, it == MIME.end() ? "application/octet-stream" : it->second, data);
}

static void handle(int fd, const std::string& ip) {
  // read request head (+ body if Content-Length)
  std::string buf;
  char tmp[8192];
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    ssize_t n = recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) { close(fd); return; }
    buf.append(tmp, n);
    headerEnd = buf.find("\r\n\r\n");
    if (buf.size() > 2 * 1024 * 1024) break;
  }
  if (headerEnd == std::string::npos) { close(fd); return; }
  std::string head = buf.substr(0, headerEnd);
  std::string method, target;
  { std::istringstream ls(head); ls >> method >> target; }
  // content-length
  size_t clen = 0;
  { std::string low = head;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    size_t p = low.find("content-length:");
    if (p != std::string::npos) clen = strtoul(low.c_str() + p + 15, nullptr, 10); }
  std::string body = buf.substr(headerEnd + 4);
  while (body.size() < clen) {
    ssize_t n = recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) break;
    body.append(tmp, n);
  }
  if (body.size() > 1000000) body.clear();   // 1 MB cap, mirrors Node

  std::string path = target, qs;
  size_t qm = target.find('?');
  if (qm != std::string::npos) { path = target.substr(0, qm); qs = target.substr(qm + 1); }
  auto q = parseQuery(qs);

  auto t0 = nowMs();
  int code = 200;
  auto finish = [&](int c, bool isJson, const std::string& ctype, const std::string& out) {
    code = c;
    if (isJson) sendJson(fd, c, out);
    else sendRaw(fd, c, ctype, out);
  };

  if (path == "/api/health")        finish(200, true, "", healthJson());
  else if (path == "/api/ping")     finish(200, true, "", "{\"pong\":true,\"t\":" + std::to_string(nowMs()) + "}");
  else if (path == "/api/estimate") finish(200, true, "", estimateJson(q));
  else if (path == "/api/distance") {
    std::string host = qget(q, "host");
    if (host.empty()) finish(400, true, "", "{\"error\":\"pass ?host=example.com\"}");
    else {
      int port = atoi(qget(q, "port", "443").c_str()); if (port <= 0) port = 443;
      std::string r = distanceJson(host, port);
      if (r.empty()) finish(502, true, "", "{\"error\":\"could not resolve " + host + "\"}");
      else if (!r.empty() && r[0] == '\x01') finish(502, true, "", "{\"error\":" + jstr(r.substr(1)) + "}");
      else finish(200, true, "", r);
    }
  }
  else if (path == "/api/device/health" && method == "POST") {
    // find id or device_id in the body
    auto findId = [&](const std::string& key) -> std::string {
      std::string pat = "\"" + key + "\"";
      size_t p = body.find(pat);
      if (p == std::string::npos) return "";
      p = body.find(':', p + pat.size());
      if (p == std::string::npos) return "";
      p++;
      while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) p++;
      std::string v;
      if (p < body.size() && body[p] == '"') { p++; while (p < body.size() && body[p] != '"') v += body[p++]; }
      else { while (p < body.size() && body[p] != ',' && body[p] != '}' && body[p] != ' ') v += body[p++]; }
      return v;
    };
    std::string id = findId("id"); if (id.empty()) id = findId("device_id");
    if (id.empty()) finish(400, true, "", "{\"error\":\"pass a device id in the JSON body: {\\\"id\\\":\\\"...\\\", ...stats}\"}");
    else finish(200, true, "", saveHealth(id, body));
  }
  else if (path == "/api/device/history") {
    std::string id = qget(q, "id");
    if (id.empty()) finish(400, true, "", "{\"error\":\"pass ?id=deviceId\"}");
    else {
      auto rows = readHistory(id);
      std::string arr = "[";
      for (size_t i = 0; i < rows.size(); i++) { if (i) arr += ","; arr += rows[i]; }
      arr += "]";
      finish(200, true, "", "{\"id\":" + jstr(safeId(id)) + ",\"history\":" + arr + "}");
    }
  }
  else if (path == "/api/devices") {
    auto ids = listDevices();
    std::string arr = "[";
    for (size_t i = 0; i < ids.size(); i++) { if (i) arr += ","; arr += jstr(ids[i]); }
    arr += "]";
    finish(200, true, "", "{\"channel\":" + jstr(CHANNEL) + ",\"devices\":" + arr + "}");
  }
  else if (path == "/api/backup" && method == "POST") finish(200, true, "", makeBackupJson());
  else if (path == "/api/backups")   finish(200, true, "", listBackupsJson());
  else if (path == "/api/security")  finish(200, true, "", secStatusJson());
  else if (path == "/api/security/log")
    finish(200, true, "", "{\"log\":" + jstr("security-" + isoDate() + ".log") +
           ",\"lines\":" + secTailJson(atoi(qget(q, "n", "50").c_str())) + "}");
  else { serveStatic(fd, path); code = 200; /* status not tracked for static */ }

  close(fd);
  secObserve(method, path, code, nowMs() - t0, ip);
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  START_TP = std::chrono::steady_clock::now();
  SEC.started = nowMs();

  PORT     = atoi(envOr("PORT", "8090").c_str());
  CHANNEL  = envOr("CHANNEL", "dev");
  std::transform(CHANNEL.begin(), CHANNEL.end(), CHANNEL.begin(), ::tolower);
  SHORT_MS = atoi(envOr("SEC_SHORT_MS", "15000").c_str());
  LONG_MS  = atoi(envOr("SEC_LONG_MS", "300000").c_str());
  FW_HOST  = envOr("SEC_FW_HOST", "one.one.one.one");
  FW_PORT  = atoi(envOr("SEC_FW_PORT", "443").c_str());

  BASE_DIR   = dirOfSelf();
  CLIENT_DIR = BASE_DIR + "/..";
  { // normalise CLIENT_DIR (resolve the trailing /..) for the traversal guard
    char rp[4096]; if (realpath(CLIENT_DIR.c_str(), rp)) CLIENT_DIR = rp; }
  DB_DIR     = BASE_DIR + "/data/devices";
  BACKUP_DIR = BASE_DIR + "/data/backups";
  SEC_DIR    = BASE_DIR + "/data/security";

  std::string keySrc = envOr("DB_KEY", "6ggw-dev-key-change-me");
  { unsigned char h[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)keySrc.data(), keySrc.size(), h);
    memcpy(DB_KEY, h, 32); }

  int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(PORT);
  if (bind(srv, (sockaddr*)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
  if (listen(srv, 128) < 0) { perror("listen"); return 1; }

  printf("6GGW server component  v%s  [build: %s]  [channel: %s]  listening on :%d\n",
         VERSION, BUILD_TAG, CHANNEL.c_str(), PORT);
  printf("  GET  /api/health\n  GET  /api/ping\n  GET  /api/distance?host=bbc.co.uk\n");
  printf("  GET  /api/estimate?users=30000&bitrate=1.5&server=10\n");
  printf("  POST /api/device/health      {\"id\":\"phone-01\", ...stats}   (per-device AES-256-GCM DB)\n");
  printf("  GET  /api/device/history?id=phone-01\n  GET  /api/devices\n");
  printf("  POST /api/backup             (bundle device DBs -> data/backups/*.6ggwbak.gz, for tape/LTO)\n");
  printf("  GET  /api/backups\n  GET  /api/security\n  GET  /api/security/log?n=50\n");
  printf("  GET  /                        (serves the bundled 6GGW client)\n");
  if (CHANNEL != "prod" && envOr("DB_KEY", "").empty())
    printf("  note: using the dev DB key. Set DB_KEY=... for rc/prod.\n");

  // start the security clock
  secLog("INFO", "watch_start",
         "{\"port\":" + std::to_string(PORT) + ",\"short_arm_ms\":" + std::to_string(SHORT_MS) +
         ",\"long_arm_ms\":" + std::to_string(LONG_MS) +
         ",\"fw_target\":" + jstr(FW_HOST + ":" + std::to_string(FW_PORT)) + "}");
  std::thread([]{ secLongTick(); }).detach();
  std::thread([]{ for (;;) { std::this_thread::sleep_for(std::chrono::milliseconds(SHORT_MS)); secShortTick(); } }).detach();
  std::thread([]{ for (;;) { std::this_thread::sleep_for(std::chrono::milliseconds(LONG_MS)); secLongTick(); } }).detach();
  printf("  security watch: ON  (short arm %ds, long arm %ds, fw probe %s:%d)\n",
         SHORT_MS / 1000, LONG_MS / 1000, FW_HOST.c_str(), FW_PORT);
  fflush(stdout);

  for (;;) {
    sockaddr_in cli{}; socklen_t cl = sizeof cli;
    int fd = accept(srv, (sockaddr*)&cli, &cl);
    if (fd < 0) continue;
    char ipbuf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof ipbuf);
    std::string ip = ipbuf;
    std::thread([fd, ip]{ handle(fd, ip); }).detach();
  }
  return 0;
}

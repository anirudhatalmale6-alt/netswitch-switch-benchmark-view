// ggw_secure — 6GGW / NetSwitch secure transport (SSL/TLS + RSA/ECDSA), C++17 over OpenSSL.
//
// The switch and the mobile terminal talk over a real TLS channel. This is the code that folds into
// server-cpp (SSL_CTX on the listener) and the client (verify + connect). It proves, end to end:
//   * a TLS 1.2/1.3 handshake using the short-term self-signed cert (EC-256 or RSA);
//   * full peer + hostname verification against that cert as its own root (self-signed);
//   * an encrypted request/response round-trip.
// The same channel runs unchanged over Wifi6 or 5G — TLS is transport-agnostic; it only needs an IP
// route, which the SSH tunnel (tunnel.sh) provides when the switch sits behind NAT/CGNAT on mobile.
//
// Build: g++ -std=c++17 -O2 ggw_secure.cpp -o ggw_secure -lssl -lcrypto
// Test : ./gen_certs.sh && ./ggw_secure --selftest --cert certs/ec256.crt --key certs/ec256.key
//        ./ggw_secure --server --port 8443 --cert certs/ec256.crt --key certs/ec256.key   (one term)
//        ./ggw_secure --client --host 127.0.0.1 --port 8443 --ca certs/ec256.crt           (another)

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

static void die(const char* m){ fprintf(stderr, "ERROR: %s\n", m); ERR_print_errors_fp(stderr); exit(1); }

static int tcp_listen(int port, int& boundPort){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
    if (bind(fd, (sockaddr*)&a, sizeof a) < 0) die("bind");
    if (listen(fd, 1) < 0) die("listen");
    socklen_t l = sizeof a; getsockname(fd, (sockaddr*)&a, &l); boundPort = ntohs(a.sin_port);
    return fd;
}
static int tcp_connect(const char* host, int port){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) die("bad host");
    if (connect(fd, (sockaddr*)&a, sizeof a) < 0) die("connect");
    return fd;
}

static int g_seclevel = -1;   // -1 = OpenSSL default (2). Set >=0 to override (0/1 allow small keys).

struct HS { std::string proto, cipher, peerReq; bool ok = false; };
static HS run_server(int listenFd, const std::string& cert, const std::string& key){
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (g_seclevel >= 0) SSL_CTX_set_security_level(ctx, g_seclevel);
    if (SSL_CTX_use_certificate_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) <= 0) die("load cert");
    if (SSL_CTX_use_PrivateKey_file (ctx, key.c_str(),  SSL_FILETYPE_PEM) <= 0) die("load key");
    if (!SSL_CTX_check_private_key(ctx)) die("cert/key mismatch");

    int c = accept(listenFd, nullptr, nullptr);
    SSL* ssl = SSL_new(ctx); SSL_set_fd(ssl, c);
    HS hs;
    if (SSL_accept(ssl) <= 0) { ERR_print_errors_fp(stderr); }
    else {
        char buf[256] = {0}; int n = SSL_read(ssl, buf, sizeof buf - 1);
        if (n > 0) hs.peerReq.assign(buf, n);
        const char* reply = "6GGW-SWITCH: secure channel up\n";
        SSL_write(ssl, reply, (int)strlen(reply));
        hs.proto = SSL_get_version(ssl); hs.cipher = SSL_get_cipher(ssl); hs.ok = true;
    }
    SSL_shutdown(ssl); SSL_free(ssl); close(c); close(listenFd); SSL_CTX_free(ctx);
    return hs;
}

struct CR { std::string proto, cipher, reply; long verify = -1; bool ok = false; };
static CR run_client(const char* host, int port, const std::string& ca, const std::string& sni){
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (g_seclevel >= 0) SSL_CTX_set_security_level(ctx, g_seclevel);
    if (!ca.empty()) {
        if (SSL_CTX_load_verify_locations(ctx, ca.c_str(), nullptr) <= 0) die("load CA");
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);   // require + verify the switch's cert
    }
    int fd = tcp_connect(host, port);
    SSL* ssl = SSL_new(ctx); SSL_set_fd(ssl, fd);
    if (!sni.empty()) { SSL_set_tlsext_host_name(ssl, sni.c_str()); SSL_set1_host(ssl, sni.c_str()); }
    CR cr;
    if (SSL_connect(ssl) <= 0) { ERR_print_errors_fp(stderr); }
    else {
        const char* req = "6GGW-CLIENT: hello over TLS\n";
        SSL_write(ssl, req, (int)strlen(req));
        char buf[256] = {0}; int n = SSL_read(ssl, buf, sizeof buf - 1);
        if (n > 0) cr.reply.assign(buf, n);
        cr.proto = SSL_get_version(ssl); cr.cipher = SSL_get_cipher(ssl);
        cr.verify = SSL_get_verify_result(ssl); cr.ok = true;
    }
    SSL_shutdown(ssl); SSL_free(ssl); close(fd); SSL_CTX_free(ctx);
    return cr;
}

static int selftest(const std::string& cert, const std::string& key){
    int boundPort = 0; int lf = tcp_listen(0, boundPort);
    HS hs; std::thread t([&]{ hs = run_server(lf, cert, key); });
    std::this_thread::sleep_for(std::chrono::milliseconds(120));   // let the server reach accept()
    CR cr = run_client("127.0.0.1", boundPort, cert, "localhost");
    t.join();

    printf("TLS self-test on 127.0.0.1:%d  (cert=%s)\n", boundPort, cert.c_str());
    printf("  handshake     : %s\n", (hs.ok && cr.ok) ? "OK" : "FAILED");
    printf("  protocol      : %s\n", cr.proto.c_str());
    printf("  cipher        : %s\n", cr.cipher.c_str());
    printf("  peer verify   : %s (%ld)\n", cr.verify == X509_V_OK ? "VALID" : X509_verify_cert_error_string(cr.verify), cr.verify);
    printf("  server saw    : %s", hs.peerReq.c_str());
    printf("  client got    : %s", cr.reply.c_str());
    bool pass = hs.ok && cr.ok && cr.verify == X509_V_OK
             && cr.reply.find("secure channel up") != std::string::npos;
    printf("  => %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(int argc, char** argv){
    signal(SIGPIPE, SIG_IGN);
    std::string mode, cert = "certs/ec256.crt", key = "certs/ec256.key", ca, host = "127.0.0.1", sni = "localhost";
    int port = 8443;
    for (int i = 1; i < argc; ++i){
        std::string a = argv[i];
        auto val = [&](const char* d){ return std::string(i+1 < argc ? argv[++i] : d); };
        if      (a == "--selftest") mode = "selftest";
        else if (a == "--server")   mode = "server";
        else if (a == "--client")   mode = "client";
        else if (a == "--cert") cert = val(cert.c_str());
        else if (a == "--key")  key  = val(key.c_str());
        else if (a == "--ca")   ca   = val("");
        else if (a == "--host") host = val(host.c_str());
        else if (a == "--sni")  sni  = val(sni.c_str());
        else if (a == "--port") port = std::atoi(val("8443").c_str());
        else if (a == "--seclevel") g_seclevel = std::atoi(val("2").c_str());
        else if (a == "--help" || a == "-h") {
            printf("ggw_secure — 6GGW secure transport (TLS over OpenSSL)\n"
                   "  --selftest --cert F --key F         one-process handshake + verify\n"
                   "  --server --port N --cert F --key F   run the switch's TLS listener\n"
                   "  --client --host H --port N --ca F    connect as the mobile terminal\n");
            return 0;
        }
    }
    if (mode == "selftest") return selftest(cert, key);
    if (mode == "server") {
        int bp = 0; int lf = tcp_listen(port, bp);
        printf("6GGW switch: TLS listener on 127.0.0.1:%d (cert=%s)\n", bp, cert.c_str());
        HS hs = run_server(lf, cert, key);
        printf("  handshake %s  proto=%s cipher=%s\n  request: %s",
               hs.ok ? "OK" : "FAILED", hs.proto.c_str(), hs.cipher.c_str(), hs.peerReq.c_str());
        return hs.ok ? 0 : 1;
    }
    if (mode == "client") {
        CR cr = run_client(host.c_str(), port, ca, sni);
        printf("6GGW terminal: connected %s:%d\n  proto=%s cipher=%s verify=%s\n  reply: %s",
               host.c_str(), port, cr.proto.c_str(), cr.cipher.c_str(),
               cr.verify == X509_V_OK ? "VALID" : X509_verify_cert_error_string(cr.verify), cr.reply.c_str());
        return cr.ok ? 0 : 1;
    }
    fprintf(stderr, "pick a mode: --selftest | --server | --client  (see --help)\n");
    return 2;
}

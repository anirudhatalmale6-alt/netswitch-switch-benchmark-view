// netswitch_sql.cpp — NetSwitch (Microsoft SQL Server) Windows client.
//
// A small, dependency-free Windows console client that connects to a Microsoft SQL Server
// over ODBC. It is the "NetSwitch Microsoft SQL Server" variant's connect tool:
//
//   * on FIRST USE it forces the operator to set a NEW password (the shipped/temporary
//     credential must be changed before the software will connect — the password is changed
//     on the server with ALTER LOGIN, so the old one stops working);
//   * it never stores the password on disk — only the server + username profile is saved;
//   * after connecting it runs a timed health query (@@VERSION / DB_NAME / GETDATE) and reports
//     the SQL round-trip in milliseconds — the same "measure the real line, then route" idea the
//     rest of NetSwitch uses, applied to the database link.
//
// Windows-only by nature (uses the Windows ODBC subsystem + the Microsoft SQL Server ODBC driver).
//
// Build (cross-compiled from Linux, static, no runtime needed):
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 netswitch_sql.cpp -o netswitch_sql.exe -static -lodbc32
//
// Run:
//   netswitch_sql.exe --server tcp:HOST,1433 --user sa
//   (add --db MyDatabase  --driver "ODBC Driver 18 for SQL Server"  --reset to force a change again)

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>

using clk = std::chrono::steady_clock;

// ---- config file: %APPDATA%\NetSwitch\sql.cfg  (server + user + change-done flag; NEVER the password) ----
static std::string cfg_dir() {
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", buf, sizeof(buf));
    std::string base = (n > 0 && n < sizeof(buf)) ? std::string(buf) : ".";
    return base + "\\NetSwitch";
}
static std::string cfg_path() { return cfg_dir() + "\\sql.cfg"; }

struct Profile {
    std::string server, user, db, driver;
    bool changed = false;   // has the first-use password change been done?
    bool loaded  = false;
};

static Profile load_profile() {
    Profile p;
    FILE* f = fopen(cfg_path().c_str(), "rb");
    if (!f) return p;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if      (k == "server")  p.server = v;
        else if (k == "user")    p.user   = v;
        else if (k == "db")      p.db     = v;
        else if (k == "driver")  p.driver = v;
        else if (k == "changed") p.changed = (v == "1");
    }
    fclose(f);
    p.loaded = true;
    return p;
}

static void save_profile(const Profile& p) {
    CreateDirectoryA(cfg_dir().c_str(), nullptr);   // ok if it already exists
    FILE* f = fopen(cfg_path().c_str(), "wb");
    if (!f) { fprintf(stderr, "warning: cannot write %s\n", cfg_path().c_str()); return; }
    fprintf(f, "server=%s\n", p.server.c_str());
    fprintf(f, "user=%s\n",   p.user.c_str());
    fprintf(f, "db=%s\n",     p.db.c_str());
    fprintf(f, "driver=%s\n", p.driver.c_str());
    fprintf(f, "changed=%d\n", p.changed ? 1 : 0);
    fclose(f);
}

// ---- hidden password entry (no echo) ----
static std::string read_hidden(const char* prompt) {
    printf("%s", prompt); fflush(stdout);
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0; GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
    std::string s; int c;
    while ((c = getchar()) != EOF && c != '\n' && c != '\r') s.push_back((char)c);
    SetConsoleMode(h, mode);
    printf("\n");
    return s;
}
static std::string read_line(const char* prompt) {
    printf("%s", prompt); fflush(stdout);
    std::string s; int c;
    while ((c = getchar()) != EOF && c != '\n' && c != '\r') s.push_back((char)c);
    return s;
}

// ---- ODBC helpers ----
static std::string diag(SQLSMALLINT type, SQLHANDLE h) {
    std::string out;
    SQLCHAR state[7], msg[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER native; SQLSMALLINT len;
    SQLSMALLINT rec = 1;
    while (SQLGetDiagRecA(type, h, rec++, state, &native, msg, sizeof(msg), &len) == SQL_SUCCESS) {
        char buf[SQL_MAX_MESSAGE_LENGTH + 64];
        snprintf(buf, sizeof(buf), "  [%s] (%ld) %s\n", state, (long)native, msg);
        out += buf;
    }
    return out;
}
static bool ok(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }

// escape a single-quoted SQL string literal (double any ')
static std::string sql_lit(const std::string& s) {
    std::string o; for (char c : s) { if (c == '\'') o += "''"; o += c; } return o;
}

struct Conn {
    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    ~Conn() {
        if (dbc) { SQLDisconnect(dbc); SQLFreeHandle(SQL_HANDLE_DBC, dbc); }
        if (env) SQLFreeHandle(SQL_HANDLE_ENV, env);
    }
};

static std::string build_connstr(const Profile& p, const std::string& pw) {
    std::string s = "Driver={" + p.driver + "};Server=" + p.server + ";";
    if (!p.db.empty()) s += "Database=" + p.db + ";";
    s += "UID=" + p.user + ";PWD=" + pw + ";";
    s += "Encrypt=yes;TrustServerCertificate=yes;";   // encrypt the link; self-signed dev certs allowed
    return s;
}

// connect; on success cn.dbc is live. returns true/false, fills err.
static bool connect(Conn& cn, const std::string& connstr, std::string& err) {
    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &cn.env))) { err = "cannot alloc ODBC env"; return false; }
    SQLSetEnvAttr(cn.env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, cn.env, &cn.dbc))) { err = "cannot alloc ODBC dbc"; return false; }
    SQLCHAR outstr[1024]; SQLSMALLINT outlen = 0;
    SQLRETURN r = SQLDriverConnectA(cn.dbc, nullptr,
                                    (SQLCHAR*)connstr.c_str(), SQL_NTS,
                                    outstr, sizeof(outstr), &outlen, SQL_DRIVER_NOPROMPT);
    if (!ok(r)) { err = diag(SQL_HANDLE_DBC, cn.dbc); return false; }
    return true;
}

// run a statement with no result set (e.g. ALTER LOGIN). returns true/false + err.
static bool exec_noresult(SQLHDBC dbc, const std::string& sql, std::string& err) {
    SQLHSTMT st = SQL_NULL_HANDLE;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st);
    SQLRETURN r = SQLExecDirectA(st, (SQLCHAR*)sql.c_str(), SQL_NTS);
    bool good = ok(r) || r == SQL_NO_DATA;
    if (!good) err = diag(SQL_HANDLE_STMT, st);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return good;
}

// health query: version, db, server time — timed. prints a short report.
static bool health(SQLHDBC dbc, std::string& err) {
    SQLHSTMT st = SQL_NULL_HANDLE;
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st);
    const char* q =
        "SELECT LEFT(CAST(SERVERPROPERTY('ProductVersion') AS varchar(64)),64),"
        " DB_NAME(), CONVERT(varchar(30), SYSDATETIME(), 121)";
    auto t0 = clk::now();
    SQLRETURN r = SQLExecDirectA(st, (SQLCHAR*)q, SQL_NTS);
    if (!ok(r)) { err = diag(SQL_HANDLE_STMT, st); SQLFreeHandle(SQL_HANDLE_STMT, st); return false; }
    char ver[128] = {0}, db[128] = {0}, now[64] = {0};
    SQLLEN l1, l2, l3;
    if (ok(SQLFetch(st))) {
        SQLGetData(st, 1, SQL_C_CHAR, ver, sizeof(ver), &l1);
        SQLGetData(st, 2, SQL_C_CHAR, db,  sizeof(db),  &l2);
        SQLGetData(st, 3, SQL_C_CHAR, now, sizeof(now), &l3);
    }
    double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    SQLFreeHandle(SQL_HANDLE_STMT, st);

    printf("\n  connected OK\n");
    printf("  SQL Server version : %s\n", ver[0] ? ver : "(unknown)");
    printf("  database           : %s\n", db[0]  ? db  : "(default)");
    printf("  server time        : %s\n", now[0] ? now : "(?)");
    printf("  SQL round-trip     : %.1f ms   (NetSwitch uses this to rank/route the DB link)\n\n", ms);
    return true;
}

static void usage() {
    printf("NetSwitch (Microsoft SQL Server) client\n"
           "  --server HOST[,PORT]   e.g. tcp:127.0.0.1,1433   (required first run)\n"
           "  --user NAME            SQL login                 (required first run)\n"
           "  --db NAME              default database          (optional)\n"
           "  --driver NAME          ODBC driver (default: ODBC Driver 17 for SQL Server)\n"
           "  --reset                force the first-use password change again\n"
           "  --help\n"
           "The password is asked at the prompt and is never written to disk.\n");
}

int main(int argc, char** argv) {
    Profile p = load_profile();
    if (p.driver.empty()) p.driver = "ODBC Driver 17 for SQL Server";
    bool reset = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* d){ return (i + 1 < argc) ? std::string(argv[++i]) : std::string(d); };
        if      (a == "--server") p.server = next("");
        else if (a == "--user")   p.user   = next("");
        else if (a == "--db")     p.db     = next("");
        else if (a == "--driver") p.driver = next(p.driver.c_str());
        else if (a == "--reset")  reset = true;
        else if (a == "--help")   { usage(); return 0; }
    }

    printf("== NetSwitch  -  Microsoft SQL Server connector ==\n");

    if (p.server.empty()) p.server = read_line("SQL Server (host[,port]): ");
    if (p.user.empty())   p.user   = read_line("SQL login username     : ");
    if (p.server.empty() || p.user.empty()) { fprintf(stderr, "server and user are required.\n"); return 2; }

    bool must_change = reset || !p.changed;   // first use (or --reset) forces a new password

    if (must_change) {
        printf("\nFirst use: the temporary password must be changed before the software will connect.\n");
        std::string oldpw = read_hidden("Current (temporary) password : ");
        std::string new1, new2;
        for (;;) {
            new1 = read_hidden("New password                 : ");
            new2 = read_hidden("New password (again)         : ");
            if (new1.size() < 8)      { printf("  -> at least 8 characters, please.\n"); continue; }
            if (new1 != new2)         { printf("  -> the two entries did not match.\n"); continue; }
            if (new1 == oldpw)        { printf("  -> the new password must differ from the old one.\n"); continue; }
            break;
        }
        // connect with the OLD password, then change it on the server.
        Conn cn; std::string err;
        if (!connect(cn, build_connstr(p, oldpw), err)) {
            fprintf(stderr, "\nconnect with the current password failed:\n%s\n", err.c_str());
            fprintf(stderr, "check the server/user/driver, then run again.\n");
            return 3;
        }
        std::string alter =
            "ALTER LOGIN [" + p.user + "] WITH PASSWORD = N'" + sql_lit(new1) +
            "' OLD_PASSWORD = N'" + sql_lit(oldpw) + "'";
        if (!exec_noresult(cn.dbc, alter, err)) {
            fprintf(stderr, "\npassword change (ALTER LOGIN) failed:\n%s\n", err.c_str());
            fprintf(stderr, "the login needs permission to change its own password.\n");
            return 4;
        }
        printf("\n  password changed on the server. the old password no longer works.\n");
        p.changed = true;
        save_profile(p);
        // reconnect with the NEW password and show health.
        Conn cn2;
        if (!connect(cn2, build_connstr(p, new1), err)) { fprintf(stderr, "reconnect failed:\n%s\n", err.c_str()); return 5; }
        std::string herr; if (!health(cn2.dbc, herr)) { fprintf(stderr, "%s\n", herr.c_str()); return 6; }
        return 0;
    }

    // normal run: ask for the password, connect, health.
    std::string pw = read_hidden("Password: ");
    Conn cn; std::string err;
    if (!connect(cn, build_connstr(p, pw), err)) {
        fprintf(stderr, "\nconnect failed:\n%s\n", err.c_str());
        fprintf(stderr, "(run with --reset if the password must be changed again.)\n");
        return 3;
    }
    save_profile(p);   // persist any server/user/driver passed on the command line
    std::string herr; if (!health(cn.dbc, herr)) { fprintf(stderr, "%s\n", herr.c_str()); return 6; }
    return 0;
}

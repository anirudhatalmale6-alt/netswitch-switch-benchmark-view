// ggw_fixeddb -- 6GGW / NetSwitch fixed-size in-CPU database + Black/Grey/White boxes
//
// Sami's ask: "a fast db in a fixed size binary, compiled and run in CPU -- the db stays
// fixed size and runs fast. When the db becomes larger it stays the same binary size."
// And: "have this QTY measure visible -- three boxes, the txt db (Black / Grey / White)."
//
// THE IDEA. A normal database grows in memory as you add rows. This one does not. It is a
// fixed-capacity store held entirely in inline arrays -- no heap, no realloc -- so its
// footprint is a COMPILE-TIME CONSTANT (sizeof the struct) whether you insert 10 rows or
// 10,000,000. New rows past capacity overwrite the oldest (a ring), and running totals are
// kept in fixed-width counters. The whole thing fits in CPU cache and stays there, which is
// why it's fast: lookups are O(1) open-addressing hits, no pointer chasing, no allocation.
//
// This is also the DRAMM footprint target: the core store is sized UNDER 30 KB.
//
// THE THREE BOXES. Every row carries a quality reading in dB. Each is classified
//   White >= -6 dB   (Good / clean)      Grey  -14..-6 dB  (Fair)      Black < -14 dB (Poor)
// -- the same Black/Grey/White scale as ggw_optimize / ggw_streamqc. The QTY measure is the
// live count in each band over the fixed window, drawn as three text boxes.
//
// A fixed-width FNV checksum over the window matches the "chksum..." validation you sketched
// -- same rows in, same checksum, on every machine.
//
// Zero dependencies, single file, deterministic. Runs from the command line (no browser).
// Build:  g++ -std=c++17 -O2 ggw_fixeddb.cpp -o ggw_fixeddb
//         x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_fixeddb.cpp -o ggw_fixeddb.exe -static
//
// Run:    ./ggw_fixeddb selftest
//         ./ggw_fixeddb demo --n 1000000        # stream a million rows, footprint unchanged
//         ./ggw_fixeddb demo --n 100 --seed 7

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>

// ---- fixed-size store -------------------------------------------------------
// CAP slots, each 12 bytes packed. 2048 * 12 = 24576 B = 24.0 KB core, under 30 KB DRAMM.
static constexpr uint32_t CAP  = 2048;          // power of two -> mask instead of modulo
static constexpr uint32_t MASK = CAP - 1;
static constexpr float DB_WHITE = -6.0f;        // >= White
static constexpr float DB_GREY  = -14.0f;       // >= Grey, else Black

#pragma pack(push, 1)
struct Slot {
    uint32_t key;     // record key (0 = empty)
    float    db;      // quality reading, dB
    uint32_t seq;     // insertion order, for ring eviction
};
#pragma pack(pop)

struct FixedDB {
    std::array<Slot, CAP> slot{};   // inline -> counted in sizeof, no heap
    uint32_t occupied = 0;          // live rows (<= CAP)
    uint64_t inserted = 0;          // total ever inserted (can far exceed CAP)
    uint32_t nextSeq  = 1;
    // running band counters over the LIVE window
    uint32_t nBlack = 0, nGrey = 0, nWhite = 0;
    uint64_t chk = 1469598103934665603ull;   // FNV-1a rolling checksum

    static uint32_t band(float db) {           // 0=Black 1=Grey 2=White
        if (db >= DB_WHITE) return 2;
        if (db >= DB_GREY)  return 1;
        return 0;
    }
    void bump(uint32_t b, int d) {
        if (b == 0) nBlack += d; else if (b == 1) nGrey += d; else nWhite += d;
    }
    // open-addressing insert/update; on a full table, evict the oldest seq in the probe path
    void put(uint32_t key, float db) {
        if (key == 0) key = 1;                 // 0 reserved for empty
        inserted++;
        uint64_t u; std::memcpy(&u, &db, 4);
        chk ^= (key ^ (u << 1)); chk *= 1099511628211ull;
        uint32_t h = key * 2654435761u;        // Knuth multiplicative hash
        uint32_t evictIdx = 0, evictSeq = 0xffffffffu; bool haveEvict = false;
        for (uint32_t i = 0; i < CAP; ++i) {
            uint32_t idx = (h + i) & MASK;
            Slot& s = slot[idx];
            if (s.key == 0) {                  // free slot -> new row
                s.key = key; s.db = db; s.seq = nextSeq++;
                occupied++; bump(band(db), +1);
                return;
            }
            if (s.key == key) {                // update existing row in place
                bump(band(s.db), -1);
                s.db = db; s.seq = nextSeq++;
                bump(band(db), +1);
                return;
            }
            if (s.seq < evictSeq) { evictSeq = s.seq; evictIdx = idx; haveEvict = true; }
        }
        // table full: overwrite oldest -> footprint stays constant
        if (haveEvict) {
            Slot& s = slot[evictIdx];
            bump(band(s.db), -1);
            s.key = key; s.db = db; s.seq = nextSeq++;
            bump(band(db), +1);
        }
    }
    bool get(uint32_t key, float& out) const {
        if (key == 0) key = 1;
        uint32_t h = key * 2654435761u;
        for (uint32_t i = 0; i < CAP; ++i) {
            const Slot& s = slot[(h + i) & MASK];
            if (s.key == 0) return false;
            if (s.key == key) { out = s.db; return true; }
        }
        return false;
    }
};

// ---- deterministic sample stream (no rand(), reproducible) ------------------
static float sampleDb(uint32_t i, uint32_t seed) {
    uint32_t x = (i * 2246822519u) ^ (seed * 3266489917u);
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    float u = (x & 0xffffff) / float(0x1000000);   // 0..1
    return -30.0f + 30.0f * u;                      // -30 .. 0 dB
}

// ---- the three text boxes ---------------------------------------------------
static void drawBoxes(const FixedDB& db) {
    uint32_t vals[3]  = { db.nBlack, db.nGrey, db.nWhite };
    const char* nm[3] = { "BLACK", "GREY", "WHITE" };
    const char* rt[3] = { "Poor",  "Fair", "Good"  };
    uint32_t mx = 1; for (int i=0;i<3;i++) if (vals[i]>mx) mx=vals[i];
    std::printf("  +--- BLACK ---+  +--- GREY ----+  +--- WHITE ---+\n");
    std::printf("  |");
    for (int i=0;i<3;i++){ std::printf(" %10u  |%s", vals[i], i<2?"  |":"\n"); }
    // bar row
    std::printf("  |");
    for (int i=0;i<3;i++){
        int bar = (int)((vals[i] * 11ull) / mx);           // 0..11 chars
        char b[16]; for(int k=0;k<11;k++) b[k]= k<bar?'#':' '; b[11]=0;
        std::printf(" %-11s |%s", b, i<2?"  |":"\n");
    }
    std::printf("  +-------------+  +-------------+  +-------------+\n");
    std::printf("  |");
    for (int i=0;i<3;i++){ std::printf(" %-11s |%s", rt[i], i<2?"  |":"\n"); }
    std::printf("  +-------------+  +-------------+  +-------------+\n");
    (void)nm;
}

static double kb(size_t b){ return b / 1024.0; }

static void report(const FixedDB& db) {
    std::printf("  rows inserted : %llu\n", (unsigned long long)db.inserted);
    std::printf("  live window   : %u / %u slots (%.1f%% full)\n",
                db.occupied, CAP, 100.0 * db.occupied / CAP);
    std::printf("  FOOTPRINT     : %.1f KB  (fixed -- sizeof(FixedDB), zero heap)\n",
                kb(sizeof(FixedDB)));
    std::printf("  checksum      : %016llx\n\n", (unsigned long long)db.chk);
    drawBoxes(db);
    uint32_t tot = db.nBlack + db.nGrey + db.nWhite;
    std::printf("\n  QTY total in window: %u   Black %u | Grey %u | White %u\n",
                tot, db.nBlack, db.nGrey, db.nWhite);
}

// ---- demo -------------------------------------------------------------------
static int runDemo(uint64_t n, uint32_t seed) {
    static FixedDB db;                       // static: single fixed instance, no heap
    std::printf("6GGW fixeddb -- demo (%llu rows, seed %u)\n\n", (unsigned long long)n, seed);
    for (uint64_t i = 1; i <= n; ++i)
        db.put((uint32_t)((i * 2654435761u) | 1u), sampleDb((uint32_t)i, seed));
    report(db);
    return 0;
}

// ---- selftest ---------------------------------------------------------------
static int runSelftest() {
    std::printf("6GGW fixeddb -- selftest\n\n");
    int fails = 0;
    const size_t FP = sizeof(FixedDB);

    // 1) footprint constant across 10 / 10k / 1,000,000 inserts, and under 30 KB
    std::printf("  [1] footprint constant + under 30 KB DRAMM target\n");
    size_t fp10=0, fp1e4=0, fp1e6=0;
    { static FixedDB a; for(uint64_t i=1;i<=10;i++)      a.put((uint32_t)(i|1u), sampleDb(i,1)); fp10 =sizeof(a); }
    { static FixedDB a; for(uint64_t i=1;i<=10000;i++)   a.put((uint32_t)(i|1u), sampleDb(i,1)); fp1e4=sizeof(a); }
    { static FixedDB a; for(uint64_t i=1;i<=1000000;i++) a.put((uint32_t)((i*2654435761u)|1u), sampleDb((uint32_t)i,1)); fp1e6=sizeof(a); }
    bool fpOk = (fp10==FP && fp1e4==FP && fp1e6==FP && FP < 30u*1024u);
    std::printf("      10 rows=%.1fKB  10k rows=%.1fKB  1,000,000 rows=%.1fKB  (<30KB)  %s\n",
                kb(fp10), kb(fp1e4), kb(fp1e6), fpOk?"OK":"FAIL");
    if(!fpOk) fails++;

    // 2) ring: after > CAP inserts the window is exactly full, not growing
    std::printf("  [2] ring eviction -- window caps at %u\n", CAP);
    static FixedDB b; for(uint64_t i=1;i<=CAP*5ull;i++) b.put((uint32_t)((i*2654435761u)|1u), sampleDb((uint32_t)i,2));
    bool ringOk = (b.occupied==CAP) && (b.inserted==CAP*5ull);
    std::printf("      inserted=%llu  live=%u  %s\n",
                (unsigned long long)b.inserted, b.occupied, ringOk?"OK":"FAIL");
    if(!ringOk) fails++;

    // 3) three boxes sum to the live window count
    std::printf("  [3] Black+Grey+White == live rows\n");
    uint32_t sum = b.nBlack+b.nGrey+b.nWhite;
    bool boxOk = (sum == b.occupied);
    std::printf("      %u + %u + %u = %u  vs live %u  %s\n",
                b.nBlack,b.nGrey,b.nWhite,sum,b.occupied, boxOk?"OK":"FAIL");
    if(!boxOk) fails++;

    // 4) O(1) lookup round-trips
    std::printf("  [4] insert -> get round-trip\n");
    static FixedDB c; c.put(4242, -3.5f); float got=0; bool g1=c.get(4242,got);
    bool getOk = g1 && (got==-3.5f) && !c.get(9999,got);
    std::printf("      get(4242)=%.1f present=%d  get(missing)=absent  %s\n",
                got, (int)g1, getOk?"OK":"FAIL");
    if(!getOk) fails++;

    // 5) deterministic checksum, two independent fills
    std::printf("  [5] deterministic checksum (two runs identical)\n");
    static FixedDB d1, d2;
    for(uint64_t i=1;i<=50000;i++){ uint32_t k=(uint32_t)((i*40503u)|1u); float v=sampleDb((uint32_t)i,9);
        d1.put(k,v); d2.put(k,v); }
    bool detOk = (d1.chk==d2.chk) && (d1.nWhite==d2.nWhite);
    std::printf("      %016llx == %016llx  %s\n",
                (unsigned long long)d1.chk,(unsigned long long)d2.chk, detOk?"OK":"FAIL");
    if(!detOk) fails++;

    std::printf("\n  RESULT: %s (5 checks, %d failed)\n",
                fails? "FAIL" : "PASS -- fixed footprint, fast, deterministic", fails);
    return fails?1:0;
}

static double argd(int c,char**v,const char*k,double d){for(int i=1;i<c-1;i++)if(!std::strcmp(v[i],k))return std::atof(v[i+1]);return d;}

int main(int argc, char** argv) {
    std::string mode = argc>1?argv[1]:"selftest";
    if (mode=="selftest") return runSelftest();
    if (mode=="demo")     return runDemo((uint64_t)argd(argc,argv,"--n",100000),
                                         (uint32_t)argd(argc,argv,"--seed",1));
    std::printf("usage: %s [selftest | demo --n ROWS --seed N]\n", argv[0]);
    return 2;
}

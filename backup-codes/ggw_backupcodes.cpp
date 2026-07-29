// ggw_backupcodes — 6GGW / NetSwitch account backup / recovery codes
//
// Generates a set of one-time backup codes (default 500) for user verification,
// the way GitHub / Google 2FA recovery codes work:
//
//   * The user is shown the 500 plaintext codes ONCE, to print / save.
//   * The .switchc config stores only SALTED SHA-256 HASHES of the codes, never
//     the codes themselves — a stolen config file can't be used to log in.
//   * Each code is SINGLE-USE: verifying a code marks it consumed (burned), so
//     it can never be replayed.
//
// This is the security-correct shape. Putting the raw codes in the config would
// mean anyone who reads the file owns the account — so we don't.
//
// Build:
//   Linux:   g++ -std=c++17 -O2 ggw_backupcodes.cpp -o ggw_backupcodes
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_backupcodes.cpp -o ggw_backupcodes.exe -static
//
// Use:
//   ggw_backupcodes gen [--count 500] [--sheet codes.txt] [--config codes.switchc.xml]
//   ggw_backupcodes verify --config codes.switchc.xml --code XXXXX-XXXXX

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
  #include <windows.h>
  #include <bcrypt.h>   // link with -lbcrypt
#else
  #include <fstream>
#endif

// ---------------------------------------------------------------------------
// CSPRNG — cryptographically strong random bytes (no std::rand)
// ---------------------------------------------------------------------------
static bool fill_random(void* buf, size_t n) {
#if defined(_WIN32)
    return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;  // STATUS_SUCCESS
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    if (!f) return false;
    f.read((char*)buf, (std::streamsize)n);
    return (bool)f;
#endif
}

// ---------------------------------------------------------------------------
// SHA-256 — self-contained (FIPS 180-4), no external crypto dependency
// ---------------------------------------------------------------------------
namespace sha256 {
struct Ctx { std::uint32_t s[8]; std::uint64_t len; std::uint8_t buf[64]; size_t n; };
static const std::uint32_t K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static inline std::uint32_t ror(std::uint32_t x, int r){ return (x>>r)|(x<<(32-r)); }
static void init(Ctx& c){ c.len=0; c.n=0;
    static const std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::memcpy(c.s,h,sizeof h); }
static void block(Ctx& c, const std::uint8_t* p){
    std::uint32_t w[64];
    for(int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
    for(int i=16;i<64;i++){ std::uint32_t s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);
        std::uint32_t s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    std::uint32_t a=c.s[0],b=c.s[1],cc=c.s[2],d=c.s[3],e=c.s[4],f=c.s[5],g=c.s[6],h=c.s[7];
    for(int i=0;i<64;i++){ std::uint32_t S1=ror(e,6)^ror(e,11)^ror(e,25); std::uint32_t ch=(e&f)^(~e&g);
        std::uint32_t t1=h+S1+ch+K[i]+w[i]; std::uint32_t S0=ror(a,2)^ror(a,13)^ror(a,22);
        std::uint32_t maj=(a&b)^(a&cc)^(b&cc); std::uint32_t t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
    c.s[0]+=a;c.s[1]+=b;c.s[2]+=cc;c.s[3]+=d;c.s[4]+=e;c.s[5]+=f;c.s[6]+=g;c.s[7]+=h;
}
static void update(Ctx& c, const void* data, size_t n){
    const std::uint8_t* p=(const std::uint8_t*)data; c.len+=n;
    while(n){ size_t take=64-c.n; if(take>n)take=n; std::memcpy(c.buf+c.n,p,take); c.n+=take; p+=take; n-=take;
        if(c.n==64){ block(c,c.buf); c.n=0; } }
}
static std::string hex(Ctx& c){
    std::uint64_t bits=c.len*8; std::uint8_t pad=0x80; update(c,&pad,1);
    std::uint8_t z=0; while(c.n!=56) update(c,&z,1);
    std::uint8_t L[8]; for(int i=0;i<8;i++) L[i]=(std::uint8_t)(bits>>(56-i*8)); update(c,L,8);
    char out[65]; for(int i=0;i<8;i++) std::snprintf(out+i*8,9,"%08x",c.s[i]); return std::string(out,64);
}
static std::string hash(const std::string& s){ Ctx c; init(c); update(c,s.data(),s.size()); return hex(c); }
} // namespace sha256

// ---------------------------------------------------------------------------
static std::string to_hex(const std::uint8_t* b, size_t n){
    static const char* H="0123456789abcdef"; std::string s; s.reserve(n*2);
    for(size_t i=0;i<n;i++){ s+=H[b[i]>>4]; s+=H[b[i]&15]; } return s;
}

// Crockford-style base32 alphabet (no I, L, O, U — no look-alikes). 32 symbols,
// and 256 % 32 == 0, so byte % 32 is perfectly uniform — no modulo bias.
static const char* ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static std::string make_code(){
    std::uint8_t r[10];
    fill_random(r, sizeof r);
    std::string c;
    for(int i=0;i<10;i++){ if(i==5) c+='-'; c+=ALPHABET[r[i]%32]; }
    return c;   // e.g. 7Q2FK-9XM3B  (10 symbols, 50 bits entropy)
}

// ---------------------------------------------------------------------------
static int do_gen(int count, const std::string& sheet, const std::string& config){
    if(count<=0) count=500;
    std::uint8_t saltb[16];
    if(!fill_random(saltb,sizeof saltb)){ std::fprintf(stderr,"no CSPRNG available\n"); return 2; }
    std::string salt = to_hex(saltb,sizeof saltb);

    std::vector<std::string> codes; codes.reserve(count);
    for(int i=0;i<count;i++) codes.push_back(make_code());

    // 1) user sheet — the plaintext codes, shown once, for the user to keep
    std::ofstream us(sheet);
    us << "6GGW / NetSwitch — account backup codes\n";
    us << "Keep these safe. Each code works ONCE. There are " << count << " of them.\n";
    us << "These are shown only here — the config file stores only hashes.\n\n";
    for(int i=0;i<count;i++){
        char n[16]; std::snprintf(n,sizeof n,"%3d",i+1);
        us << n << ". " << codes[i] << "\n";
    }
    us.close();

    // 2) config block — salted hashes only, safe to ship inside the .switchc
    std::ofstream cf(config);
    cf << "<backup-codes algo=\"SHA-256\" count=\"" << count << "\" salt=\"" << salt << "\">\n";
    for(int i=0;i<count;i++){
        std::string h = sha256::hash(salt + ":" + codes[i]);
        cf << "  <code i=\"" << (i+1) << "\" used=\"0\">" << h << "</code>\n";
    }
    cf << "</backup-codes>\n";
    cf.close();

    std::printf("Generated %d backup codes.\n", count);
    std::printf("  user sheet (plaintext, keep safe): %s\n", sheet.c_str());
    std::printf("  config block (hashes only, ship it): %s\n", config.c_str());
    std::printf("Paste the <backup-codes> block into the <security> section of your .switchc.\n");
    return 0;
}

// Minimal, robust line scan of the config block (no XML library needed).
static int do_verify(const std::string& config, const std::string& code){
    std::ifstream f(config);
    if(!f){ std::fprintf(stderr,"cannot open %s\n",config.c_str()); return 2; }
    std::stringstream ss; ss<<f.rdbuf(); std::string doc=ss.str(); f.close();

    // pull salt="..."
    auto sp = doc.find("salt=\"");
    if(sp==std::string::npos){ std::fprintf(stderr,"no salt in config\n"); return 2; }
    sp+=6; auto se=doc.find('"',sp);
    std::string salt=doc.substr(sp,se-sp);

    std::string want = sha256::hash(salt + ":" + code);

    // scan each <code i="N" used="U">HASH</code>
    size_t pos=0; bool matched=false, alreadyUsed=false; std::string idx;
    while((pos=doc.find("<code ",pos))!=std::string::npos){
        size_t gt=doc.find('>',pos); size_t end=doc.find("</code>",gt);
        if(gt==std::string::npos||end==std::string::npos) break;
        std::string attrs=doc.substr(pos,gt-pos);
        std::string hash=doc.substr(gt+1,end-gt-1);
        if(hash==want){
            // read used flag
            auto up=attrs.find("used=\""); std::string used = (up!=std::string::npos)? attrs.substr(up+6,1):"0";
            auto ip=attrs.find("i=\""); if(ip!=std::string::npos){ auto ie=attrs.find('"',ip+3); idx=attrs.substr(ip+3,ie-ip-3);}
            if(used=="1"){ alreadyUsed=true; }
            else {
                matched=true;
                // burn it: rewrite used="0" -> used="1" for THIS entry
                std::string block=doc.substr(pos,end+7-pos);
                std::string burned=block; auto bu=burned.find("used=\"0\"");
                if(bu!=std::string::npos) burned.replace(bu,8,"used=\"1\"");
                doc.replace(pos,end+7-pos,burned);
                std::ofstream out(config,std::ios::trunc); out<<doc; out.close();
            }
            break;
        }
        pos=end+7;
    }

    if(matched){ std::printf("OK — code accepted (backup code #%s), now consumed.\n",idx.c_str()); return 0; }
    if(alreadyUsed){ std::printf("REJECTED — that code was already used (#%s).\n",idx.c_str()); return 1; }
    std::printf("REJECTED — code not recognised.\n"); return 1;
}

int main(int argc,char** argv){
    if(argc<2){
        std::printf("ggw_backupcodes gen    [--count 500] [--sheet codes.txt] [--config codes.switchc.xml]\n");
        std::printf("ggw_backupcodes verify --config codes.switchc.xml --code XXXXX-XXXXX\n");
        return 0;
    }
    std::string mode=argv[1];
    int count=500; std::string sheet="backup-codes.txt", config="backup-codes.switchc.xml", code;
    for(int i=2;i<argc;i++){
        std::string a=argv[i];
        if(a=="--count"&&i+1<argc) count=std::atoi(argv[++i]);
        else if(a=="--sheet"&&i+1<argc) sheet=argv[++i];
        else if(a=="--config"&&i+1<argc) config=argv[++i];
        else if(a=="--code"&&i+1<argc) code=argv[++i];
    }
    if(mode=="gen") return do_gen(count,sheet,config);
    if(mode=="verify"){ if(code.empty()){ std::fprintf(stderr,"--code required\n"); return 2; } return do_verify(config,code); }
    std::fprintf(stderr,"unknown mode: %s\n",mode.c_str()); return 2;
}

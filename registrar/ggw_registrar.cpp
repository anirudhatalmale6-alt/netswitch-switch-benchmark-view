// ggw_registrar — 6GGW / NetSwitch user + device registrar (control plane)
//
// The "server component that keeps track of users" from the spec: a user proves
// who they are by presenting a one-time BACKUP CODE from the list (see
// backup-codes/), and on success the registrar logs their device's network
// parameters — IP / gateway / netmask / DNS / DNS2 / DNS(peripheral) / DHCP —
// as a session record. It's the security gate in front of the switch↔server↔
// serverless path.
//
//   enroll : verify a backup code (single-use, burned), then record the device
//   list   : show enrolled device sessions
//
// The backup-code check is byte-identical to backup-codes/ggw_backupcodes:
// salted SHA-256, the config holds only hashes, each code works once.
//
// Build:
//   Linux:   g++ -std=c++17 -O2 ggw_registrar.cpp -o ggw_registrar
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_registrar.cpp -o ggw_registrar.exe -static

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), self-contained — same as backup-codes/
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
static inline std::uint32_t ror(std::uint32_t x,int r){return (x>>r)|(x<<(32-r));}
static void init(Ctx&c){c.len=0;c.n=0;static const std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};std::memcpy(c.s,h,sizeof h);}
static void block(Ctx&c,const std::uint8_t*p){std::uint32_t w[64];
 for(int i=0;i<16;i++)w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
 for(int i=16;i<64;i++){std::uint32_t s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3);std::uint32_t s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
 std::uint32_t a=c.s[0],b=c.s[1],cc=c.s[2],d=c.s[3],e=c.s[4],f=c.s[5],g=c.s[6],h=c.s[7];
 for(int i=0;i<64;i++){std::uint32_t S1=ror(e,6)^ror(e,11)^ror(e,25);std::uint32_t ch=(e&f)^(~e&g);std::uint32_t t1=h+S1+ch+K[i]+w[i];std::uint32_t S0=ror(a,2)^ror(a,13)^ror(a,22);std::uint32_t maj=(a&b)^(a&cc)^(b&cc);std::uint32_t t2=S0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}
 c.s[0]+=a;c.s[1]+=b;c.s[2]+=cc;c.s[3]+=d;c.s[4]+=e;c.s[5]+=f;c.s[6]+=g;c.s[7]+=h;}
static void update(Ctx&c,const void*data,size_t n){const std::uint8_t*p=(const std::uint8_t*)data;c.len+=n;while(n){size_t take=64-c.n;if(take>n)take=n;std::memcpy(c.buf+c.n,p,take);c.n+=take;p+=take;n-=take;if(c.n==64){block(c,c.buf);c.n=0;}}}
static std::string hex(Ctx&c){std::uint64_t bits=c.len*8;std::uint8_t pad=0x80;update(c,&pad,1);std::uint8_t z=0;while(c.n!=56)update(c,&z,1);std::uint8_t L[8];for(int i=0;i<8;i++)L[i]=(std::uint8_t)(bits>>(56-i*8));update(c,L,8);char out[65];for(int i=0;i<8;i++)std::snprintf(out+i*8,9,"%08x",c.s[i]);return std::string(out,64);}
static std::string hash(const std::string&s){Ctx c;init(c);update(c,s.data(),s.size());return hex(c);}
} // namespace sha256

// ---------------------------------------------------------------------------
// Backup-code verify + burn (single-use). Returns the code index, or -1.
// status: 0=ok, 1=already-used, 2=not-found, 3=file/salt error
// ---------------------------------------------------------------------------
static int verify_and_burn(const std::string& config, const std::string& code, std::string& idxOut) {
    std::ifstream f(config);
    if(!f) return 3;
    std::stringstream ss; ss<<f.rdbuf(); std::string doc=ss.str(); f.close();
    auto sp=doc.find("salt=\""); if(sp==std::string::npos) return 3;
    sp+=6; auto se=doc.find('"',sp); std::string salt=doc.substr(sp,se-sp);
    std::string want=sha256::hash(salt+":"+code);
    size_t pos=0;
    while((pos=doc.find("<code ",pos))!=std::string::npos){
        size_t gt=doc.find('>',pos); size_t end=doc.find("</code>",gt);
        if(gt==std::string::npos||end==std::string::npos) break;
        std::string attrs=doc.substr(pos,gt-pos), hash=doc.substr(gt+1,end-gt-1);
        if(hash==want){
            auto ip=attrs.find("i=\""); if(ip!=std::string::npos){auto ie=attrs.find('"',ip+3);idxOut=attrs.substr(ip+3,ie-ip-3);}
            auto up=attrs.find("used=\""); std::string used=(up!=std::string::npos)?attrs.substr(up+6,1):"0";
            if(used=="1") return 1;
            std::string block=doc.substr(pos,end+7-pos), burned=block;
            auto bu=burned.find("used=\"0\""); if(bu!=std::string::npos) burned.replace(bu,8,"used=\"1\"");
            doc.replace(pos,end+7-pos,burned);
            std::ofstream out(config,std::ios::trunc); out<<doc; out.close();
            return 0;
        }
        pos=end+7;
    }
    return 2;
}

// ---------------------------------------------------------------------------
// Network parameter auto-detect (Linux best-effort). Fields already set by
// flags are left untouched.
// ---------------------------------------------------------------------------
struct Net { std::string ip, gw, mask, dns, dns2, dns_periph, dhcp; };

static std::string run1(const char* cmd) {
#if defined(_WIN32)
    FILE* p=_popen(cmd,"r");
#else
    FILE* p=popen(cmd,"r");
#endif
    std::string out; if(p){ char b[512]; while(std::fgets(b,sizeof b,p)) out+=b;
#if defined(_WIN32)
        _pclose(p);
#else
        pclose(p);
#endif
    } return out;
}

static std::string prefix_to_mask(int p){
    if(p<0||p>32) return "";
    std::uint32_t m = p==0?0:(0xFFFFFFFFu<<(32-p));
    char b[16]; std::snprintf(b,sizeof b,"%u.%u.%u.%u",(m>>24)&255,(m>>16)&255,(m>>8)&255,m&255);
    return b;
}

static void autodetect(Net& n) {
#if !defined(_WIN32)
    // default route: "default via <gw> dev <iface> proto <p> ..."
    std::string route=run1("ip route show default 2>/dev/null | head -1");
    std::string iface;
    { std::istringstream is(route); std::string tok;
      while(is>>tok){ if(tok=="via"){ is>>tok; if(n.gw.empty()) n.gw=tok; }
                      else if(tok=="dev"){ is>>iface; }
                      else if(tok=="proto"){ std::string pr; is>>pr; if(n.dhcp.empty()) n.dhcp=(pr=="dhcp")?"on":"static"; } } }
    // address + prefix on that iface: "... inet <ip>/<pfx> ... dynamic ..."
    if(!iface.empty()){
        std::string cmd="ip -o -f inet addr show dev "+iface+" 2>/dev/null | head -1";
        std::string a=run1(cmd.c_str());
        auto ipos=a.find("inet ");
        if(ipos!=std::string::npos){
            std::string rest=a.substr(ipos+5); std::string cidr=rest.substr(0,rest.find(' '));
            auto slash=cidr.find('/');
            if(n.ip.empty()) n.ip = (slash==std::string::npos)?cidr:cidr.substr(0,slash);
            if(n.mask.empty() && slash!=std::string::npos) n.mask=prefix_to_mask(std::atoi(cidr.c_str()+slash+1));
        }
        if(n.dhcp.empty()) n.dhcp = (a.find("dynamic")!=std::string::npos)?"on":"static";
    }
    // DNS servers from resolv.conf
    { std::ifstream r("/etc/resolv.conf"); std::string line; std::vector<std::string> ns;
      while(std::getline(r,line)){ std::istringstream is(line); std::string k,v; is>>k>>v;
          if(k=="nameserver"&&!v.empty()) ns.push_back(v); }
      if(n.dns.empty()      && ns.size()>0) n.dns=ns[0];
      if(n.dns2.empty()     && ns.size()>1) n.dns2=ns[1];
      if(n.dns_periph.empty()&&ns.size()>2) n.dns_periph=ns[2]; }
#endif
    // fill blanks so the record is complete
    auto na=[&](std::string& s){ if(s.empty()) s="n/a"; };
    na(n.ip); na(n.gw); na(n.mask); na(n.dns); na(n.dns2); na(n.dns_periph); na(n.dhcp);
}

static std::string now_str(){
    std::time_t t=std::time(nullptr); std::tm* lt=std::localtime(&t);
    char b[32]; std::strftime(b,sizeof b,"%Y-%m-%d %H:%M:%S",lt); return b;
}

static std::string jesc(const std::string& s){ std::string o; for(char c:s){ if(c=='"'||c=='\\') o+='\\'; o+=c; } return o; }

// ---------------------------------------------------------------------------
int main(int argc,char** argv){
    if(argc<2){
        std::printf("ggw_registrar enroll --config codes.switchc.xml --code XXXXX-XXXXX [--auto]\n");
        std::printf("                     [--ip .. --gw .. --mask .. --dns .. --dns2 .. --dns-periph .. --dhcp ..]\n");
        std::printf("                     [--user NAME] [--registry registry.log]\n");
        std::printf("ggw_registrar list   [--registry registry.log]\n");
        return 0;
    }
    std::string mode=argv[1];
    std::string config, code, user="anon", registry="registry.log"; bool autod=false; Net n;
    for(int i=2;i<argc;i++){ std::string a=argv[i]; auto nx=[&](){ return (i+1<argc)?argv[++i]:""; };
        if(a=="--config") config=nx(); else if(a=="--code") code=nx();
        else if(a=="--ip") n.ip=nx(); else if(a=="--gw") n.gw=nx(); else if(a=="--mask") n.mask=nx();
        else if(a=="--dns") n.dns=nx(); else if(a=="--dns2") n.dns2=nx(); else if(a=="--dns-periph") n.dns_periph=nx();
        else if(a=="--dhcp") n.dhcp=nx(); else if(a=="--user") user=nx();
        else if(a=="--registry") registry=nx(); else if(a=="--auto") autod=true; }

    if(mode=="list"){
        std::ifstream r(registry); if(!r){ std::printf("(no sessions yet)\n"); return 0; }
        std::string line; int c=0; while(std::getline(r,line)){ std::printf("%s\n",line.c_str()); ++c; }
        std::printf("-- %d session(s) --\n",c); return 0;
    }
    if(mode!="enroll"){ std::fprintf(stderr,"unknown mode: %s\n",mode.c_str()); return 2; }
    if(config.empty()||code.empty()){ std::fprintf(stderr,"--config and --code are required\n"); return 2; }

    std::string idx;
    int st=verify_and_burn(config,code,idx);
    if(st==3){ std::printf("ERROR — cannot read backup-codes config: %s\n",config.c_str()); return 2; }
    if(st==1){ std::printf("DENIED — backup code #%s was already used.\n",idx.c_str()); return 1; }
    if(st==2){ std::printf("DENIED — backup code not recognised. No session recorded.\n"); return 1; }

    if(autod) autodetect(n); else { auto na=[](std::string&s){ if(s.empty()) s="n/a"; };
        na(n.ip);na(n.gw);na(n.mask);na(n.dns);na(n.dns2);na(n.dns_periph);na(n.dhcp); }

    // append the session record (one JSON line)
    std::ifstream cnt(registry); int seq=0; { std::string l; while(std::getline(cnt,l)) ++seq; } cnt.close();
    ++seq;
    char rec[1024];
    std::snprintf(rec,sizeof rec,
        "{\"seq\":%d,\"time\":\"%s\",\"user\":\"%s\",\"code\":\"#%s\",\"ip\":\"%s\",\"gw\":\"%s\",\"mask\":\"%s\",\"dns\":\"%s\",\"dns2\":\"%s\",\"dns_periph\":\"%s\",\"dhcp\":\"%s\"}",
        seq, now_str().c_str(), jesc(user).c_str(), idx.c_str(),
        jesc(n.ip).c_str(), jesc(n.gw).c_str(), jesc(n.mask).c_str(),
        jesc(n.dns).c_str(), jesc(n.dns2).c_str(), jesc(n.dns_periph).c_str(), jesc(n.dhcp).c_str());
    std::ofstream out(registry,std::ios::app); out<<rec<<"\n"; out.close();

    std::printf("ENROLLED — user \"%s\" via backup code #%s (now burned).\n",user.c_str(),idx.c_str());
    std::printf("  IP %s  GW %s  MASK %s\n",n.ip.c_str(),n.gw.c_str(),n.mask.c_str());
    std::printf("  DNS %s / %s / %s (peripheral)  DHCP %s\n",n.dns.c_str(),n.dns2.c_str(),n.dns_periph.c_str(),n.dhcp.c_str());
    std::printf("  session #%d written to %s\n",seq,registry.c_str());
    return 0;
}

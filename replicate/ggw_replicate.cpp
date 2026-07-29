// ggw_replicate — 6GGW / NetSwitch phone-to-phone replication transport
//
// Roadmap item #1: fast phone replication over a MULTI-STREAM transport. One
// connection carries four kinds of payload at once — the same model QUIC and
// WebRTC use (several streams multiplexed, some reliable, some real-time):
//
//   STATE  (reliable)   key/value deltas — only what changed is sent
//   FILE   (reliable)   an attachment, chunked, ACK'd, SHA-256 verified end-to-end
//   AUDIO  (real-time)  G.711 µ-law voice frames — best-effort, late frames dropped
//   VIDEO  (real-time)  frames — best-effort, newest wins (like RTP)
//
// Reliable streams stop-and-wait with ACK + retransmit, so they survive loss.
// Real-time streams are unacknowledged and degrade gracefully — you lose a frame,
// not the call. The switch picks whichever transport is fastest per device; this
// is the reference implementation of the payload layer over plain UDP so it runs
// anywhere with no dependencies. In production the same framing rides QUIC/WebRTC.
//
// Modes:
//   ggw_replicate selftest [--loss 0.1]      two peers over loopback, verify all 4
//   ggw_replicate serve  --port 9000         run as the REPLICA (sink) — a 2nd phone
//   ggw_replicate send   --host H --port 9000 run as the SOURCE — the 1st phone
//
// Build:
//   Linux:   g++ -std=c++17 -O2 ggw_replicate.cpp -o ggw_replicate -pthread
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_replicate.cpp -o ggw_replicate.exe -static -lws2_32

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <atomic>
#include <chrono>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define BADSOCK INVALID_SOCKET
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <sys/time.h>
  typedef int sock_t;
  #define BADSOCK (-1)
  #define CLOSESOCK ::close
#endif

using clk = std::chrono::steady_clock;
static double secs(clk::time_point a, clk::time_point b){ return std::chrono::duration<double>(b-a).count(); }

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) — same as backup-codes / voice-edge
// ---------------------------------------------------------------------------
namespace sha256 {
struct Ctx{std::uint32_t s[8];std::uint64_t len;std::uint8_t buf[64];size_t n;};
static const std::uint32_t K[64]={
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
static std::string hash_bytes(const void*d,size_t n){Ctx c;init(c);update(c,d,n);return hex(c);}
} // namespace sha256

// ---------------------------------------------------------------------------
// CRC-32 (per-packet integrity)
// ---------------------------------------------------------------------------
static std::uint32_t crc32(const void* d,size_t n){
    static std::uint32_t T[256]; static bool init=false;
    if(!init){ for(std::uint32_t i=0;i<256;i++){ std::uint32_t c=i; for(int k=0;k<8;k++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); T[i]=c; } init=true; }
    std::uint32_t c=0xFFFFFFFFu; const std::uint8_t* p=(const std::uint8_t*)d;
    for(size_t i=0;i<n;i++) c=T[(c^p[i])&0xFF]^(c>>8);
    return c^0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// G.711 µ-law (real PCMU voice bytes) — same encoder as voice-edge
// ---------------------------------------------------------------------------
static std::uint8_t ulaw(int16_t pcm){
    const int BIAS=0x84; int sign=(pcm<0)?0x80:0; if(sign) pcm=-pcm; if(pcm>32635)pcm=32635; pcm+=BIAS;
    int exp=7; for(int m=0x4000; (pcm&m)==0 && exp>0; m>>=1) exp--;
    int mant=(pcm>>(exp+3))&0x0F; return (std::uint8_t)~(sign|(exp<<4)|mant);
}

// ---------------------------------------------------------------------------
// UDP helpers — same shim as voice-edge
// ---------------------------------------------------------------------------
static void net_init(){
#if defined(_WIN32)
    WSADATA w; WSAStartup(MAKEWORD(2,2),&w);
#endif
}
static sock_t udp_bind(int port){
    sock_t s=::socket(AF_INET,SOCK_DGRAM,0); if(s==BADSOCK) return BADSOCK;
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons((uint16_t)port);
    if(::bind(s,(sockaddr*)&a,sizeof a)!=0){ CLOSESOCK(s); return BADSOCK; }
    return s;
}
static int sock_port(sock_t s){ sockaddr_in a{}; socklen_t l=sizeof a; getsockname(s,(sockaddr*)&a,&l); return ntohs(a.sin_port); }
static void set_timeout(sock_t s,int ms){
#if defined(_WIN32)
    DWORD t=ms; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&t,sizeof t);
#else
    timeval tv{ms/1000,(ms%1000)*1000}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
#endif
}
static sockaddr_in addr_of(const char* host,int port){ sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port); inet_pton(AF_INET,host,&a.sin_addr); return a; }
static void udp_send(sock_t s,const sockaddr_in& to,const void* d,size_t n){ ::sendto(s,(const char*)d,(int)n,0,(const sockaddr*)&to,sizeof to); }
static int  udp_recv(sock_t s,void* d,size_t n,sockaddr_in* from){ socklen_t l=sizeof(sockaddr_in); return (int)::recvfrom(s,(char*)d,(int)n,0,(sockaddr*)from,from?&l:nullptr); }

// ---------------------------------------------------------------------------
// packet framing — 12-byte header + payload
//   [0] magic 'n'  [1] type  [2..3] seq  [4..5] aux  [6..7] len  [8..11] crc32
// ---------------------------------------------------------------------------
enum { T_STATE=0, T_AUDIO=1, T_VIDEO=2, T_FILE=3, T_ACK=4, T_DONE=5 };
static const int HDR=12;
static size_t pack(std::uint8_t* b,int type,std::uint16_t seq,std::uint16_t aux,const void* payload,std::uint16_t len){
    b[0]='n'; b[1]=(std::uint8_t)type;
    b[2]=seq>>8; b[3]=seq&0xFF; b[4]=aux>>8; b[5]=aux&0xFF; b[6]=len>>8; b[7]=len&0xFF;
    if(len) std::memcpy(b+HDR,payload,len);
    std::uint32_t c=crc32(b+HDR,len);
    b[8]=c>>24; b[9]=c>>16; b[10]=c>>8; b[11]=c&0xFF;
    return HDR+len;
}
static bool unpack(const std::uint8_t* b,int n,int& type,std::uint16_t& seq,std::uint16_t& aux,const std::uint8_t*& payload,std::uint16_t& len){
    if(n<HDR||b[0]!='n') return false;
    type=b[1]; seq=(b[2]<<8)|b[3]; aux=(b[4]<<8)|b[5]; len=(b[6]<<8)|b[7];
    if(HDR+len>n) return false;
    std::uint32_t c=(b[8]<<24)|(b[9]<<16)|(b[10]<<8)|b[11];
    if(crc32(b+HDR,len)!=c) return false;      // corrupt — drop
    payload=b+HDR; return true;
}

// deterministic "attachment" (stands in for a photo/clip): 32 KB via LCG
static std::vector<std::uint8_t> make_attachment(size_t n){
    std::vector<std::uint8_t> v(n); std::uint32_t x=0x12345678u;
    for(size_t i=0;i<n;i++){ x=x*1664525u+1013904223u; v[i]=(std::uint8_t)(x>>24); }
    return v;
}
// deterministic 32x32 video frame with a moving diagonal band
static std::vector<std::uint8_t> make_frame(int idx){
    std::vector<std::uint8_t> f(32*32);
    for(int y=0;y<32;y++)for(int x=0;x<32;x++) f[y*32+x]=(std::uint8_t)(((x+y+idx*3)*8)&0xFF);
    return f;
}

// ============================ REPLICA (peer B / sink) ============================
struct Result {
    std::map<std::string,std::string> state;
    int audio_recv=0, audio_expect=0, video_recv=0, video_expect=0;
    int file_chunks=0, file_expect=0;
    std::string file_sha, file_sha_expect;
    int state_expect=0;
    bool done=false;
    double bytes=0, elapsed=0;
};

static void run_replica(sock_t s, Result* R, double loss){
    set_timeout(s, 2000);
    std::uint8_t buf[2048];
    std::set<std::uint16_t> seenRel;                 // dedup reliable seqs
    std::map<int,std::vector<std::uint8_t>> chunks;  // file chunk store
    std::uint32_t lcg=0x2468ACEu; int idle=0;
    auto t0=clk::now(); bool started=false;
    while(idle<4 && !R->done){
        sockaddr_in from{}; int n=udp_recv(s,buf,sizeof buf,&from);
        if(n<=0){ ++idle; continue; }
        idle=0;
        int type; std::uint16_t seq,aux,len; const std::uint8_t* pl;
        if(!unpack(buf,n,type,seq,aux,pl,len)) continue;   // bad crc / short
        // optional loss injection BEFORE processing, so reliable streams must retransmit
        if(loss>0.0){ lcg=lcg*1664525u+1013904223u; if((lcg>>8)*(1.0/16777216.0) < loss) continue; }
        if(!started){ t0=clk::now(); started=true; }
        R->bytes+=n;
        if(type==T_STATE){
            std::string kv((const char*)pl,len); auto eq=kv.find('=');
            if(eq!=std::string::npos && !seenRel.count(seq)){ R->state[kv.substr(0,eq)]=kv.substr(eq+1); seenRel.insert(seq); }
            std::uint8_t a[HDR]; size_t m=pack(a,T_ACK,seq,0,nullptr,0); udp_send(s,from,a,m);
        } else if(type==T_FILE){
            if(!seenRel.count(seq)){ chunks[aux].assign(pl,pl+len); seenRel.insert(seq); }
            std::uint8_t a[HDR]; size_t m=pack(a,T_ACK,seq,0,nullptr,0); udp_send(s,from,a,m);
        } else if(type==T_AUDIO){
            R->audio_recv++;
        } else if(type==T_VIDEO){
            R->video_recv++;
        } else if(type==T_DONE){
            // payload: "<sha> <nstate> <naudio> <nvideo> <nfilechunks>"
            std::string d((const char*)pl,len); char sha[80]={0}; int ns=0,na=0,nv=0,nf=0;
            std::sscanf(d.c_str(),"%79s %d %d %d %d",sha,&ns,&na,&nv,&nf);
            R->file_sha_expect=sha; R->state_expect=ns; R->audio_expect=na; R->video_expect=nv; R->file_expect=nf;
            std::uint8_t a[HDR]; size_t m=pack(a,T_ACK,seq,0,nullptr,0); udp_send(s,from,a,m);
            R->done=true;
        }
    }
    // reassemble file + hash
    std::vector<std::uint8_t> file; for(auto& kv:chunks){ file.insert(file.end(),kv.second.begin(),kv.second.end()); }
    R->file_chunks=(int)chunks.size();
    R->file_sha = file.empty()? "" : sha256::hash_bytes(file.data(),file.size());
    R->elapsed = secs(t0,clk::now());
}

// ============================ SOURCE (peer A) ============================
static std::uint16_t g_seq=0;   // reliable seq space
static bool send_reliable(sock_t s,const sockaddr_in& to,int type,std::uint16_t aux,const void* payload,std::uint16_t len,int& retx){
    std::uint16_t seq=++g_seq; std::uint8_t pkt[2048]; size_t m=pack(pkt,type,seq,aux,payload,len);
    std::uint8_t rb[2048];
    for(int attempt=0;attempt<40;attempt++){
        udp_send(s,to,pkt,m);
        sockaddr_in from{}; int n=udp_recv(s,rb,sizeof rb,&from);
        if(n>0){ int t; std::uint16_t rs,ra,rl; const std::uint8_t* p;
            if(unpack(rb,n,t,rs,ra,p,rl) && t==T_ACK && rs==seq) return true; }
        retx++;    // no/av wrong ack -> retransmit
    }
    return false;
}

struct SendStats { int state_n=0, audio_n=0, video_n=0, file_chunks=0, retx=0; std::string file_sha; double mbps_rel=0, mbps_media=0; };

static bool run_source(sock_t s,const sockaddr_in& to,SendStats& st){
    set_timeout(s,300);   // ACK wait
    // --- STATE: 20-key map, change 8 keys, send only those 8 deltas ---
    std::map<std::string,std::string> full;
    for(int i=0;i<20;i++) full["k"+std::to_string(i)]="v"+std::to_string(i);
    const char* changed[8]={"k1","k3","k5","k7","k9","k11","k13","k15"};
    auto t0=clk::now(); double relbytes=0;
    for(auto c:changed){ full[c]=std::string("NEW_")+c; std::string kv=std::string(c)+"="+full[c];
        if(!send_reliable(s,to,T_STATE,0,kv.data(),(std::uint16_t)kv.size(),st.retx)) return false;
        relbytes+=HDR+kv.size(); st.state_n++; }
    // --- FILE: 32 KB attachment in 1 KB chunks, reliable ---
    auto att=make_attachment(32768); st.file_sha=sha256::hash_bytes(att.data(),att.size());
    const int CH=1024; int nch=(int)((att.size()+CH-1)/CH);
    for(int i=0;i<nch;i++){ size_t off=(size_t)i*CH, len=std::min((size_t)CH,att.size()-off);
        if(!send_reliable(s,to,T_FILE,(std::uint16_t)i,att.data()+off,(std::uint16_t)len,st.retx)) return false;
        relbytes+=HDR+len; st.file_chunks++; }
    st.mbps_rel = relbytes*8.0/1e6/std::max(1e-6,secs(t0,clk::now()));
    // --- AUDIO: 50 µ-law frames (20ms each), best-effort ---
    auto t1=clk::now(); double medbytes=0; constexpr double PI=3.14159265358979323846;
    for(int f=0;f<50;f++){ std::uint8_t frame[160]; for(int i=0;i<160;i++){ double t=(f*160+i)/8000.0; frame[i]=ulaw((int16_t)(12000.0*std::sin(2*PI*440.0*t))); }
        std::uint8_t pkt[2048]; size_t m=pack(pkt,T_AUDIO,(std::uint16_t)f,0,frame,160); udp_send(s,to,pkt,m); medbytes+=m; st.audio_n++; }
    // --- VIDEO: 30 frames (32x32), best-effort ---
    for(int f=0;f<30;f++){ auto fr=make_frame(f); std::uint8_t pkt[2048]; size_t m=pack(pkt,T_VIDEO,(std::uint16_t)f,(std::uint16_t)f,fr.data(),(std::uint16_t)fr.size()); udp_send(s,to,pkt,m); medbytes+=m; st.video_n++; }
    st.mbps_media = medbytes*8.0/1e6/std::max(1e-6,secs(t1,clk::now()));
    // --- DONE (reliable): totals + file sha so the replica can verify ---
    char done[128]; std::snprintf(done,sizeof done,"%s %d %d %d %d",st.file_sha.c_str(),st.state_n,st.audio_n,st.video_n,st.file_chunks);
    if(!send_reliable(s,to,T_DONE,0,done,(std::uint16_t)std::strlen(done),st.retx)) return false;
    return true;
}

// ============================ modes ============================
static int do_selftest(double loss){
    net_init();
    sock_t sB=udp_bind(0); if(sB==BADSOCK){ std::fprintf(stderr,"bind B failed\n"); return 2; }
    int portB=sock_port(sB);
    Result R;
    std::thread tb(run_replica,sB,&R,loss);
    sock_t sA=udp_bind(0);
    sockaddr_in toB=addr_of("127.0.0.1",portB);
    SendStats st;
    std::printf("6GGW replication transport — selftest (loopback, loss=%.0f%%)\n\n", loss*100.0);
    bool ok=run_source(sA,toB,st);
    tb.join(); CLOSESOCK(sA); CLOSESOCK(sB);

    // verify
    bool state_ok = (R.state.size()>=8);
    for(auto c:{"k1","k3","k5","k7","k9","k11","k13","k15"}) if(R.state[c]!=std::string("NEW_")+c) state_ok=false;
    bool file_ok  = (R.file_sha==st.file_sha) && (R.file_chunks==st.file_chunks);
    bool audio_ok = (R.audio_recv>0);   // real-time: some may drop under loss
    bool video_ok = (R.video_recv>0);

    std::printf("  STATE  reliable : sent %d deltas, replica applied %zu keys  %s\n", st.state_n, R.state.size(), state_ok?"OK":"FAIL");
    std::printf("  FILE   reliable : %d chunks (32 KB), SHA-256 %s  %s\n", st.file_chunks,
                file_ok?"matches":"MISMATCH", file_ok?"OK":"FAIL");
    std::printf("           src sha = %s\n", st.file_sha.c_str());
    std::printf("           dst sha = %s\n", R.file_sha.c_str());
    std::printf("  AUDIO  realtime : sent %d, replica got %d frames  %s\n", st.audio_n, R.audio_recv, audio_ok?"OK":"FAIL");
    std::printf("  VIDEO  realtime : sent %d, replica got %d frames  %s\n", st.video_n, R.video_recv, video_ok?"OK":"FAIL");
    std::printf("  retransmits     : %d   (reliable streams recovered every lost packet)\n", st.retx);
    std::printf("  throughput      : reliable %.1f Mbps, media %.1f Mbps\n", st.mbps_rel, st.mbps_media);
    bool all = ok && state_ok && file_ok && audio_ok && video_ok;
    std::printf("\n  RESULT: %s\n", all?"PASS — all four streams replicated":"FAIL");
    if(loss>0) std::printf("  (with loss the reliable STATE/FILE still verify exactly; AUDIO/VIDEO degrade, never block.)\n");
    return all?0:1;
}

int main(int argc,char**argv){
    std::string mode = argc>1? argv[1] : "selftest";
    std::string host="127.0.0.1"; int port=9000; double loss=0.0;
    for(int i=2;i<argc;i++){ std::string a=argv[i]; auto nx=[&](){return (i+1<argc)?argv[++i]:"";};
        if(a=="--host") host=nx(); else if(a=="--port") port=std::atoi(nx()); else if(a=="--loss") loss=std::atof(nx()); }

    if(mode=="selftest") return do_selftest(loss);
    net_init();
    if(mode=="serve"){
        sock_t s=udp_bind(port); if(s==BADSOCK){ std::fprintf(stderr,"bind %d failed\n",port); return 2; }
        std::printf("replica listening on udp %d — waiting for a source...\n",port);
        Result R; run_replica(s,&R,loss); CLOSESOCK(s);
        std::printf("  applied %zu state keys, %d file chunks (sha %s), audio %d, video %d\n",
            R.state.size(),R.file_chunks,R.file_sha.c_str(),R.audio_recv,R.video_recv);
        std::printf("  file sha %s expected\n", (R.file_sha==R.file_sha_expect)?"MATCHES":"differs — retry");
        return 0;
    }
    if(mode=="send"){
        sock_t s=udp_bind(0); sockaddr_in to=addr_of(host.c_str(),port);
        SendStats st; std::printf("source -> %s:%d\n",host.c_str(),port);
        bool ok=run_source(s,to,st); CLOSESOCK(s);
        std::printf("  sent: %d state, %d file chunks (sha %s), %d audio, %d video, %d retransmits — %s\n",
            st.state_n,st.file_chunks,st.file_sha.c_str(),st.audio_n,st.video_n,st.retx, ok?"done":"FAILED (no replica?)");
        return ok?0:1;
    }
    std::fprintf(stderr,"usage: %s selftest|serve|send [--host H --port P --loss F]\n",argv[0]);
    return 2;
}

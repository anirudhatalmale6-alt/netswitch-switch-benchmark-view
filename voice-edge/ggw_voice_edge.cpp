// ggw_voice_edge — 6GGW / NetSwitch voice slice: SIP register + RTP through an edge
//
// A real, self-contained slice of the voice-rerouting picture, no Kamailio /
// rtpengine install needed to see it work:
//
//   * a thin client REGISTERs to the edge, presenting a one-time BACKUP CODE
//     from the list (backup-codes/) — the edge verifies+burns it (gate).
//   * the client places a call (SIP INVITE / 200 / ACK); the edge allocates an
//     RTP port and returns it in SDP — this is the MEDIA EDGE.
//   * the client streams real RTP: G.711 µ-law (PCMU) packets carrying an 8 kHz
//     sine tone, 20 ms each. The edge RELAYS each packet back (the RTP handler),
//     and the client confirms every packet made the media round-trip.
//   * BYE tears the call down.
//
// It speaks enough real SIP (text/UDP) and real RTP (RFC 3550 header) to be a
// truthful demo — not a mock. Scale-up = point the same client at a real
// Kamailio+rtpengine edge; the wire format is the same.
//
// Roles:
//   ggw_voice_edge serve    [--sip-port 45060] [--config codes.switchc.xml]
//   ggw_voice_edge call     [--host 127.0.0.1] [--sip-port 45060] --code XXXXX-XXXXX [--packets 25]
//   ggw_voice_edge selftest [--config codes.switchc.xml] --code XXXXX-XXXXX   (edge+client in one process)
//
// Build:
//   Linux:   g++ -std=c++17 -O2 -pthread ggw_voice_edge.cpp -o ggw_voice_edge
//   Windows: x86_64-w64-mingw32-g++ -std=c++17 -O2 ggw_voice_edge.cpp -o ggw_voice_edge.exe -static -lws2_32

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>

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

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) — same as backup-codes/ and registrar/
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
static std::string hash(const std::string&s){Ctx c;init(c);update(c,s.data(),s.size());return hex(c);}
} // namespace sha256

static int verify_and_burn(const std::string& config,const std::string& code,std::string& idxOut){
    std::ifstream f(config); if(!f) return 3;
    std::stringstream ss; ss<<f.rdbuf(); std::string doc=ss.str(); f.close();
    auto sp=doc.find("salt=\""); if(sp==std::string::npos) return 3;
    sp+=6; auto se=doc.find('"',sp); std::string salt=doc.substr(sp,se-sp);
    std::string want=sha256::hash(salt+":"+code); size_t pos=0;
    while((pos=doc.find("<code ",pos))!=std::string::npos){
        size_t gt=doc.find('>',pos); size_t end=doc.find("</code>",gt);
        if(gt==std::string::npos||end==std::string::npos) break;
        std::string attrs=doc.substr(pos,gt-pos),hash=doc.substr(gt+1,end-gt-1);
        if(hash==want){
            auto ip=attrs.find("i=\""); if(ip!=std::string::npos){auto ie=attrs.find('"',ip+3);idxOut=attrs.substr(ip+3,ie-ip-3);}
            auto up=attrs.find("used=\""); std::string used=(up!=std::string::npos)?attrs.substr(up+6,1):"0";
            if(used=="1") return 1;
            std::string blk=doc.substr(pos,end+7-pos),bd=blk; auto bu=bd.find("used=\"0\""); if(bu!=std::string::npos) bd.replace(bu,8,"used=\"1\"");
            doc.replace(pos,end+7-pos,bd); std::ofstream out(config,std::ios::trunc); out<<doc; out.close(); return 0;
        }
        pos=end+7;
    }
    return 2;
}

// ---------------------------------------------------------------------------
// tiny UDP helpers
// ---------------------------------------------------------------------------
static void net_init(){
#if defined(_WIN32)
    WSADATA w; WSAStartup(MAKEWORD(2,2),&w);
#endif
}
static sock_t udp_bind(int port){ // port 0 = ephemeral
    sock_t s=::socket(AF_INET,SOCK_DGRAM,0); if(s==BADSOCK) return BADSOCK;
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons((uint16_t)port);
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
// G.711 µ-law encode (real PCMU voice bytes)
// ---------------------------------------------------------------------------
static std::uint8_t ulaw(int16_t pcm){
    const int BIAS=0x84; int sign=(pcm<0)?0x80:0; if(sign) pcm=-pcm; if(pcm>32635)pcm=32635; pcm+=BIAS;
    int exp=7; for(int m=0x4000; (pcm&m)==0 && exp>0; m>>=1) exp--;
    int mant=(pcm>>(exp+3))&0x0F; return (std::uint8_t)~(sign|(exp<<4)|mant);
}
// fill 160 samples (20ms @ 8kHz) of a 440Hz sine into PCMU
static void make_rtp(std::vector<std::uint8_t>& pkt,std::uint16_t seq,std::uint32_t ts,std::uint32_t ssrc,int phase){
    pkt.resize(12+160);
    pkt[0]=0x80; pkt[1]=0x00; // V2, PT=0 (PCMU)
    pkt[2]=seq>>8; pkt[3]=seq&0xFF;
    pkt[4]=ts>>24; pkt[5]=ts>>16; pkt[6]=ts>>8; pkt[7]=ts;
    pkt[8]=ssrc>>24; pkt[9]=ssrc>>16; pkt[10]=ssrc>>8; pkt[11]=ssrc;
    constexpr double PI=3.14159265358979323846;
    for(int i=0;i<160;i++){ double t=(phase+i)/8000.0; int16_t s=(int16_t)(12000.0*std::sin(2*PI*440.0*t)); pkt[12+i]=ulaw(s); }
}

// ---------------------------------------------------------------------------
// EDGE (serve)
// ---------------------------------------------------------------------------
static std::atomic<bool> g_rtp_stop{false};
static std::atomic<int>  g_rtp_relayed{0};
static std::atomic<bool> g_edge_stop{false};   // selftest: stop the edge after the client is done

static void rtp_echo_loop(sock_t rtp){
    set_timeout(rtp,200);
    std::vector<std::uint8_t> buf(2048);
    while(!g_rtp_stop.load()){
        sockaddr_in from{}; int n=udp_recv(rtp,buf.data(),buf.size(),&from);
        if(n<=0) continue;
        udp_send(rtp,from,buf.data(),n);   // relay the media packet back through the edge
        g_rtp_relayed.fetch_add(1);
    }
}

static int run_edge(int sipPort,const std::string& config,bool oneCall){
    net_init();
    sock_t sip=udp_bind(sipPort);
    if(sip==BADSOCK){ std::fprintf(stderr,"edge: cannot bind SIP port %d\n",sipPort); return 2; }
    std::printf("[edge] SIP registrar + media edge up on 127.0.0.1:%d\n",sipPort);
    set_timeout(sip,300);
    sock_t rtp=BADSOCK; std::thread rtpThread; bool callUp=false;
    std::vector<char> buf(4096);
    bool running=true;
    while(running){
        if(g_edge_stop.load()) break;
        sockaddr_in from{}; int n=udp_recv(sip,buf.data(),buf.size()-1,&from);
        if(n<=0){ continue; }
        buf[n]='\0'; std::string msg(buf.data(),n);
        char cli[64]; inet_ntop(AF_INET,&from.sin_addr,cli,sizeof cli);

        if(msg.rfind("REGISTER",0)==0){
            std::string idx="?"; std::string code;
            auto cp=msg.find("X-Backup-Code:");
            if(cp!=std::string::npos){ cp+=14; while(cp<msg.size()&&msg[cp]==' ')++cp; auto e=msg.find_first_of("\r\n",cp); code=msg.substr(cp,e-cp); }
            int st = code.empty()?2:verify_and_burn(config,code,idx);
            std::string reply = (st==0)
                ? "SIP/2.0 200 OK\r\nReason: backup-code #"+idx+" accepted (burned)\r\n\r\n"
                : "SIP/2.0 403 Forbidden\r\nReason: bad or used backup code\r\n\r\n";
            udp_send(sip,from,reply.data(),reply.size());
            std::printf("[edge] REGISTER from %s -> %s\n",cli, st==0?("200 OK (code #"+idx+")").c_str():"403 Forbidden");
        }
        else if(msg.rfind("INVITE",0)==0){
            // parse client's advertised RTP port from SDP: "m=audio <port> RTP/AVP"
            int cliRtp=0; auto mp=msg.find("m=audio ");
            if(mp!=std::string::npos) cliRtp=std::atoi(msg.c_str()+mp+8);
            if(rtp==BADSOCK){ rtp=udp_bind(0); g_rtp_stop=false; g_rtp_relayed=0; rtpThread=std::thread(rtp_echo_loop,rtp); }
            int edgeRtp=sock_port(rtp);
            std::string sdp="v=0\r\no=edge 0 0 IN IP4 127.0.0.1\r\ns=6ggw\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio "+std::to_string(edgeRtp)+" RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
            std::string reply="SIP/2.0 200 OK\r\nContent-Type: application/sdp\r\nContent-Length: "+std::to_string(sdp.size())+"\r\n\r\n"+sdp;
            udp_send(sip,from,reply.data(),reply.size()); callUp=true;
            std::printf("[edge] INVITE from %s (client rtp %d) -> 200 OK, media edge port %d\n",cli,cliRtp,edgeRtp);
        }
        else if(msg.rfind("ACK",0)==0){ std::printf("[edge] ACK — call established, relaying media\n"); }
        else if(msg.rfind("BYE",0)==0){
            std::string reply="SIP/2.0 200 OK\r\n\r\n"; udp_send(sip,from,reply.data(),reply.size());
            std::printf("[edge] BYE — relayed %d media packets this call. Tearing down.\n",g_rtp_relayed.load());
            if(rtp!=BADSOCK){ g_rtp_stop=true; if(rtpThread.joinable())rtpThread.join(); CLOSESOCK(rtp); rtp=BADSOCK; }
            callUp=false; if(oneCall) running=false;
        }
    }
    if(rtp!=BADSOCK){ g_rtp_stop=true; if(rtpThread.joinable())rtpThread.join(); CLOSESOCK(rtp); }
    CLOSESOCK(sip); return 0;
}

// ---------------------------------------------------------------------------
// CLIENT (call)
// ---------------------------------------------------------------------------
static int run_client(const std::string& host,int sipPort,const std::string& code,int packets){
    net_init();
    sock_t sip=udp_bind(0), rtp=udp_bind(0);
    if(sip==BADSOCK||rtp==BADSOCK){ std::fprintf(stderr,"client: socket bind failed\n"); return 2; }
    set_timeout(sip,1500); set_timeout(rtp,400);
    sockaddr_in edge=addr_of(host.c_str(),sipPort);
    int myRtp=sock_port(rtp);
    char rbuf[4096];

    // REGISTER (with the one-time backup code)
    std::string reg="REGISTER sip:edge SIP/2.0\r\nX-Backup-Code: "+code+"\r\nContact: client\r\n\r\n";
    udp_send(sip,edge,reg.data(),reg.size());
    int n=udp_recv(sip,rbuf,sizeof rbuf-1,nullptr);
    if(n<=0){ std::printf("[client] no response to REGISTER (edge down?)\n"); return 1; }
    rbuf[n]='\0'; std::string rr(rbuf);
    bool regOK = rr.rfind("SIP/2.0 200",0)==0;
    std::printf("[client] REGISTER -> %s\n", regOK?"200 OK (accepted)":"REJECTED");
    if(!regOK){ std::printf("[client] %s\n", rr.substr(0,rr.find("\r\n")).c_str()); return 1; }

    // INVITE
    std::string sdp="v=0\r\no=client 0 0 IN IP4 127.0.0.1\r\ns=6ggw\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio "+std::to_string(myRtp)+" RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
    std::string inv="INVITE sip:edge SIP/2.0\r\nContent-Type: application/sdp\r\nContent-Length: "+std::to_string(sdp.size())+"\r\n\r\n"+sdp;
    udp_send(sip,edge,inv.data(),inv.size());
    n=udp_recv(sip,rbuf,sizeof rbuf-1,nullptr);
    if(n<=0){ std::printf("[client] no response to INVITE\n"); return 1; }
    rbuf[n]='\0'; std::string ir(rbuf);
    int edgeRtp=0; auto mp=ir.find("m=audio "); if(mp!=std::string::npos) edgeRtp=std::atoi(ir.c_str()+mp+8);
    if(ir.rfind("SIP/2.0 200",0)!=0 || edgeRtp==0){ std::printf("[client] INVITE failed\n"); return 1; }
    std::printf("[client] INVITE -> 200 OK, media edge at port %d\n",edgeRtp);
    std::string ack="ACK sip:edge SIP/2.0\r\n\r\n"; udp_send(sip,edge,ack.data(),ack.size());

    // MEDIA: stream real PCMU RTP, verify the edge relays each packet back
    sockaddr_in edgeMedia=addr_of(host.c_str(),edgeRtp);
    std::uint32_t ssrc=0x6667C0DE, ts=0; int echoed=0, phase=0;
    std::vector<std::uint8_t> pkt, in(2048);
    for(int i=0;i<packets;i++){
        make_rtp(pkt,(std::uint16_t)i,ts,ssrc,phase); ts+=160; phase+=160;
        udp_send(rtp,edgeMedia,pkt.data(),pkt.size());
        sockaddr_in f{}; int m=udp_recv(rtp,in.data(),in.size(),&f);
        if(m==(int)pkt.size() && std::memcmp(in.data(),pkt.data(),m)==0) echoed++;
    }
    std::printf("[client] media: sent %d PCMU packets (0.5s of 440Hz tone), %d relayed back intact\n",packets,echoed);

    // BYE
    std::string bye="BYE sip:edge SIP/2.0\r\n\r\n"; udp_send(sip,edge,bye.data(),bye.size());
    n=udp_recv(sip,rbuf,sizeof rbuf-1,nullptr);
    std::printf("[client] BYE -> %s\n", (n>0 && std::string(rbuf,n).rfind("SIP/2.0 200",0)==0)?"200 OK (call closed)":"(no ack)");

    bool pass = regOK && edgeRtp && echoed==packets;
    std::printf("\n%s  register=%s  call=%s  media=%d/%d round-tripped through the edge\n",
                pass?"PASS":"PARTIAL", regOK?"ok":"no", edgeRtp?"ok":"no", echoed, packets);
    CLOSESOCK(sip); CLOSESOCK(rtp);
    return pass?0:1;
}

int main(int argc,char** argv){
    if(argc<2){
        std::printf("ggw_voice_edge serve    [--sip-port 45060] [--config codes.switchc.xml]\n");
        std::printf("ggw_voice_edge call     [--host 127.0.0.1] [--sip-port 45060] --code XXXXX-XXXXX [--packets 25]\n");
        std::printf("ggw_voice_edge selftest [--config codes.switchc.xml] --code XXXXX-XXXXX [--sip-port 45060] [--packets 25]\n");
        return 0;
    }
    std::string mode=argv[1], host="127.0.0.1", config="codes.switchc.xml", code; int sipPort=45060, packets=25;
    for(int i=2;i<argc;i++){ std::string a=argv[i]; auto nx=[&](){ return (i+1<argc)?argv[++i]:""; };
        if(a=="--host")host=nx(); else if(a=="--sip-port")sipPort=std::atoi(nx());
        else if(a=="--config")config=nx(); else if(a=="--code")code=nx(); else if(a=="--packets")packets=std::atoi(nx()); }

    if(mode=="serve")  return run_edge(sipPort,config,false);
    if(mode=="call"){ if(code.empty()){std::fprintf(stderr,"--code required\n");return 2;} return run_client(host,sipPort,code,packets); }
    if(mode=="selftest"){
        if(code.empty()){std::fprintf(stderr,"--code required\n");return 2;}
        g_edge_stop=false;
        std::thread edge(run_edge,sipPort,config,true);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        int rc=run_client(host,sipPort,code,packets);
        g_edge_stop=true;                 // release the edge whether the call closed or was rejected
        edge.join();
        return rc;
    }
    std::fprintf(stderr,"unknown mode: %s\n",mode.c_str()); return 2;
}

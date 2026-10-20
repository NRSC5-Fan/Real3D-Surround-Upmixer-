// LFE / bass management checks (crossover + soft FFT fade + copies). No SDK needed.
#include <cstdio>
#include <cmath>
#include <vector>
#include "freesurround_decoder.h"
static int failures=0;
#define CHECK(c,...) do{ if(!(c)){++failures;printf("FAIL: " __VA_ARGS__);printf("\n");} }while(0)

struct Result { std::vector<std::vector<double> > ch; std::vector<double> in; };
// mono (L=R) sine through the decoder; returns per-channel output after the first 4 blocks
static Result run(channel_setup s, unsigned N, double freq, bool en, double cut, double level, bool clr, bool cc, int mode, int slope, double sr=44100) {
  freesurround_decoder d(s,N); d.lfe(en,(float)cut,(float)level,clr,cc,mode,slope,(float)sr);
  unsigned C=freesurround_decoder::num_channels(s); Result r; r.ch.resize(C);
  std::vector<float> in(2*N); const unsigned blocks=(unsigned)(sr*1.0/N)+6;
  for(unsigned b=0;b<blocks;b++){
    for(unsigned k=0;k<N;k++){ double t=(double)b*N+k; float v=(float)(0.5*sin(2*3.14159265358979*freq*t/sr)); in[2*k]=v; in[2*k+1]=v; r.in.push_back(v); }
    float*o=d.decode(&in[0]);
    for(unsigned k=0;k<N;k++) for(unsigned c=0;c<C;c++) r.ch[c].push_back(o[k*C+c]);
  }
  return r;
}
static double rms(const std::vector<double>&v, size_t from){ double e=0; for(size_t i=from;i<v.size();i++) e+=v[i]*v[i]; return sqrt(e/(v.size()-from)); }
static double rmsf(const std::vector<double>&v, size_t from){ return rms(v,from); }
// best lag (in samples, output lags input) by cross-correlation over a small search range
static int best_lag(const std::vector<double>&out, const std::vector<double>&in, int lo, int hi){
  double best=-1e30; int bl=0; size_t from=20000, to=out.size()-hi-10;
  for(int lag=lo;lag<=hi;lag++){ double c=0; for(size_t i=from;i<to;i++) c+=out[i]*in[i-lag]; if(c>best){best=c;bl=lag;} }
  return bl; }

int main(){
  const unsigned N=4096; const channel_setup S=cs_stereo; // channels: FL FR LFE
  // 1. crossover: bass goes to the LFE channel, aligned with the decoder latency, mains lose it
  for(int slope=0;slope<3;slope++){
    Result lo=run(S,N,40,true,90,1,false,false,0,slope), hi=run(S,N,1000,true,90,1,false,false,0,slope);
    double lfeLo=rmsf(lo.ch[2],20000), lfeHi=rmsf(hi.ch[2],20000), flLo=rmsf(lo.ch[0],20000), flHi=rmsf(hi.ch[0],20000);
    printf("slope %d: 40Hz LFE=%.4f FL=%.4f | 1kHz LFE=%.5f FL=%.4f\n",slope,lfeLo,flLo,lfeHi,flHi);
    CHECK(lfeLo>0.3,"slope %d: 40Hz not in LFE",slope);
    CHECK(lfeHi<(slope==0?0.01:0.002),"slope %d: 1kHz leaks into LFE (%.5f)",slope,lfeHi);
    CHECK(flLo<0.1*lfeLo + 0.02,"slope %d: bass not removed from mains",slope);
    int lag=best_lag(lo.ch[2],lo.in,N/2-300,N/2+300);
    // a low-passed 40 Hz sine lags by the decoder latency N/2 plus the filter group delay (a few ms)
    CHECK(lag>=(int)(N/2) && lag<(int)(N/2)+450,"slope %d: LFE lag %d, expected N/2=%u plus the filter group delay",slope,lag,N/2);
  }
  // 2. flat sum: with copy to L/R the front channel at any frequency must equal the same channel without bass management
  for(double f: {30.0,60.0,90.0,150.0,400.0,2000.0}) for(int slope=0;slope<3;slope++){
    Result ref=run(S,N,f,false,0,1,false,false,0,1), x=run(S,N,f,true,90,1,true,false,0,slope);
    double a=rmsf(ref.ch[0],20000), b=rmsf(x.ch[0],20000);
    double gain=a/0.3535; // decoder gain for centred material at 1 (calibrates the copy against the decoder path)
    printf("flat sum f=%6.0f slope %d: ref FL=%.4f with copy FL=%.4f (ratio %.3f)\n",f,slope,a,b,b/a);
    CHECK(fabs(b/a-1)<0.12 || (f>=150 && fabs(b/a-1)<0.06),"f=%.0f slope %d: front level changed by %.1f%%",f,slope,(b/a-1)*100); (void)gain;
  }
  // 3. soft FFT fade
  { Result lo=run(S,N,40,true,90,1,false,false,1,1), hi=run(S,N,1000,true,90,1,false,false,1,1);
    double l=rmsf(lo.ch[2],20000), h=rmsf(hi.ch[2],20000); printf("FFT fade: 40Hz LFE=%.4f 1kHz LFE=%.5f\n",l,h);
    CHECK(l>0.3,"FFT fade: 40Hz not in LFE"); CHECK(h<0.002,"FFT fade: 1kHz leaks into LFE"); }
  // 4. level and cutoff 0
  { Result a=run(S,N,40,true,90,1,false,false,0,1), b=run(S,N,40,true,90,0.5,false,false,0,1), z=run(S,N,40,true,0,1,false,false,0,1);
    CHECK(fabs(rmsf(b.ch[2],20000)/rmsf(a.ch[2],20000)-0.5)<0.01,"level 0.5 not half");
    CHECK(rmsf(z.ch[2],20000)<1e-9,"cutoff 0 still produces LFE"); }
  // 5. copies reach the right channels (5.1: FL FC FR BL BR LFE)
  { Result n=run(cs_5point1,N,40,true,90,1,false,false,0,1), lr=run(cs_5point1,N,40,true,90,1,true,false,0,1), c=run(cs_5point1,N,40,true,90,1,false,true,0,1);
    double nfl=rmsf(n.ch[0],20000), nfc=rmsf(n.ch[1],20000), lrfl=rmsf(lr.ch[0],20000), lrfc=rmsf(lr.ch[1],20000), cfl=rmsf(c.ch[0],20000), cfc=rmsf(c.ch[1],20000);
    printf("5.1 copies: none FL=%.4f FC=%.4f | LR FL=%.4f FC=%.4f | C FL=%.4f FC=%.4f\n",nfl,nfc,lrfl,lrfc,cfl,cfc);
    CHECK(lrfl>nfl+0.1,"copy L/R did not raise FL"); CHECK(fabs(lrfc-nfc)<1e-6,"copy L/R changed FC");
    CHECK(cfc>nfc+0.1,"copy C did not raise FC"); CHECK(fabs(cfl-nfl)<1e-6,"copy C changed FL"); }
  // 6. 48 kHz, other block sizes, finite
  for(unsigned n: {1024u,2048u,8192u,16384u}) for(int m=0;m<2;m++){ Result r=run(cs_5point1,n,50,true,120,1.5,true,true,m,2,48000);
    bool ok=true; for(auto&ch:r.ch) for(double x:ch) if(!std::isfinite(x)) ok=false; CHECK(ok,"non-finite N=%u mode %d",n,m); }
  // 7. ringing: impulse through the crossover vs soft fade; print how long the LFE keeps moving after the main part
  for(int m=0;m<2;m++){ freesurround_decoder d(S,N); d.lfe(true,60,1,false,false,m,1,44100); std::vector<float> in(2*N,0.f), z(2*N,0.f);
    std::vector<double> out; for(int b=0;b<8;b++){ float*o; if(b==2){ in[2*100]=in[2*100+1]=1.f; o=d.decode(&in[0]); } else o=d.decode(&z[0]); for(unsigned k=0;k<N;k++) out.push_back(o[k*3+2]); }
    double pk=0; size_t pi=0; for(size_t i=0;i<out.size();i++) if(fabs(out[i])>pk){pk=fabs(out[i]);pi=i;}
    size_t last=0; for(size_t i=0;i<out.size();i++) if(fabs(out[i])>0.01*pk) last=i;
    size_t first=out.size(); for(size_t i=0;i<out.size();i++) if(fabs(out[i])>0.01*pk){first=i;break;}
    printf("impulse mode %d: peak %.4f at %zu, >1%% of peak from %zu to %zu (%zu samples = %.1f ms)\n",m,pk,pi,first,last,last-first,(last-first)*1000.0/44100); }
  printf(failures?"\n%d FAILURES\n":"\nall LFE checks passed\n",failures); return failures!=0; }

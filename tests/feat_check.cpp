#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdlib>
#include "freesurround_decoder.h"
static int failures=0;
#define CHECK(c,...) do{ if(!(c)){++failures;printf("FAIL: " __VA_ARGS__);printf("\n");} }while(0)

// run a stereo test signal and return per-channel energy
static std::vector<double> run(channel_setup s, unsigned N, float spread, bool centered, bool &finite){
  freesurround_decoder d(s,N); d.center_spread(spread);
  unsigned C=freesurround_decoder::num_channels(s); std::vector<double> e(C,0.0); std::vector<float> in(2*N); finite=true;
  const unsigned total=44100*2/N+4; 
  for(unsigned b=0;b<total;b++){
    for(unsigned k=0;k<N;k++){ float t=(float)((double)b*N+k); float v=0.5f*sinf(t*0.0713f);
      in[2*k]=v; in[2*k+1]=centered? v : 0; }
    float*o=d.decode(&in[0]);
    if(b<3) continue;
    for(unsigned k=0;k<N;k++) for(unsigned c=0;c<C;c++){ float x=o[k*C+c]; if(!std::isfinite(x)) finite=false; e[c]+=(double)x*x; }
  }
  return e;
}
static double sum(const std::vector<double>&v){double t=0;for(double x:v)t+=x;return t;}

int main(){
  const unsigned sizes[]={1024,2048,4096,8192,16384};
  for(unsigned N: sizes){
    bool f; auto e0=run(cs_5point1,N,0.0f,true,f); CHECK(f,"N=%u non-finite",N);
    // centered source: FC dominant, FL~FR
    CHECK(e0[1]>10*e0[0] && e0[1]>10*e0[2], "N=%u centre not dominant", N);
    double tot0=sum(e0);
    for(float sp: {0.25f,0.5f,1.0f}){
      auto e=run(cs_5point1,N,sp,true,f); CHECK(f,"N=%u sp=%.2f non-finite",N,sp);
      double tot=sum(e);
      double expectC=cos(sp*0.5*3.14159265358979); expectC*=expectC;
      double rel=fabs(tot-tot0)/tot0;
      CHECK(rel<0.125,"N=%u sp=%.2f total energy changed %.3f%% (expect <=~11%% from coherent L/R sum)",N,sp,rel*100);
      CHECK(fabs(e[1]/e0[1]-expectC)<0.03,"N=%u sp=%.2f FC ratio %.3f expected %.3f",N,sp,e[1]/e0[1],expectC);
      CHECK(fabs(e[0]-e[2])/(e[0]+e[2]+1e-12)<0.02,"N=%u sp=%.2f L/R asym",N,sp);
      if(N==4096) printf("N=%u spread=%.2f  FL=%.3g FC=%.3g FR=%.3g total=%.4g (off: %.4g)\n",N,sp,e[0],e[1],e[2],tot,tot0);
    }
    printf("N=%5u ok (delay of decoder = %u samples)\n",N,N/2);
  }
  // setups without a centre channel must be unaffected by spread
  for(channel_setup s: {cs_stereo,cs_4point1}){
    bool f; auto a=run(s,4096,0.0f,true,f), b=run(s,4096,1.0f,true,f);
    for(size_t c=0;c<a.size();c++) CHECK(a[c]==b[c],"setup %u ch %zu changed by spread",(unsigned)s,c);
  }
  // every setup stays finite with spread on
  channel_setup all[]={cs_stereo,cs_3stereo,cs_4point1,cs_5point1,cs_5stereo,cs_legacy,cs_6point1,cs_7point1,cs_7point1_panorama,cs_7point1_tricenter,cs_8point1,cs_9point1_wrap,cs_9point1_densepanorama,cs_11point1_densewrap,cs_13point1_totalwrap,cs_16point1};
  for(channel_setup s: all){ bool f; run(s,2048,0.7f,true,f); CHECK(f,"setup %u non-finite with spread",(unsigned)s); }
  // out-of-range spread values are clamped
  { bool f; auto a=run(cs_5point1,4096,1.0f,true,f), b=run(cs_5point1,4096,5.0f,true,f), c=run(cs_5point1,4096,-3.0f,true,f), d=run(cs_5point1,4096,0.0f,true,f);
    CHECK(a==b,"spread>1 not clamped"); CHECK(c==d,"spread<0 not clamped"); }
  printf(failures?"\n%d FAILURES\n":"\nall feature checks passed\n",failures); return failures!=0; }

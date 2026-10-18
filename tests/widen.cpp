#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "freesurround_decoder.h"
struct R { double L, R, M, S, corr; };
static R run(int kind, float fsep, float rsep) {
  const unsigned N = 4096; freesurround_decoder d(cs_stereo, N); d.front_separation(fsep); d.rear_separation(rsep);
  std::vector<float> in(2*N); srand(3); double eL=0,eR=0,eM=0,eS=0,eLR=0;
  for (int b = 0; b < 40; b++) {
    for (unsigned k = 0; k < N; k++) { float t = (float)((double)b*N + k), s = 0.5f*sinf(t*0.071f) + 0.3f*sinf(t*0.19f+1);
      float n1 = rand()/(float)RAND_MAX-0.5f, n2 = rand()/(float)RAND_MAX-0.5f;
      float l, r;
      if (kind==0) { l = s; r = 0.5f*s; }            // panned left-of-centre (level difference only)
      else if (kind==1) { l = s; r = s; }            // dead centre
      else if (kind==2) { l = n1; r = n2; }          // uncorrelated noise = "ambience"
      else { l = s + 0.3f*n1; r = 0.5f*s + 0.3f*n2; } // panned source plus some ambience
      in[2*k] = l; in[2*k+1] = r; }
    float *o = d.decode(&in[0]); if (b < 3) continue;
    for (unsigned k = 0; k < N; k++) { double l = o[k*3], r = o[k*3+1]; eL+=l*l; eR+=r*r; eLR+=l*r; double m=(l+r)/2, s=(l-r)/2; eM+=m*m; eS+=s*s; } }
  return { eL, eR, eM, eS, eLR/sqrt(eL*eR+1e-30) };
}
int main() {
  const char *nm[] = {"panned (L=1,R=0.5)", "centre (L=R)", "uncorrelated noise", "panned + ambience"};
  float fs[] = {0.0f, 1.0f, 1.5f, 2.0f};
  for (int k = 0; k < 4; k++) { printf("\n%s   [rear_sep=1]\n  front_sep   L/R level (dB)   side/mid (dB)   L-R correlation\n", nm[k]);
    for (float f : fs) { R r = run(k, f, 1.0f); printf("  %5.1f      %+7.2f          %+7.2f          %+.3f\n", f, 10*log10((r.L+1e-30)/(r.R+1e-30)), 10*log10((r.S+1e-30)/(r.M+1e-30)), r.corr); } }
  printf("\nuncorrelated noise, front_sep=1, varying rear_sep\n  rear_sep   side/mid (dB)   correlation\n");
  for (float rs : {0.0f, 1.0f, 2.0f, 4.0f}) { R r = run(2, 1.0f, rs); printf("  %5.1f      %+7.2f        %+.3f\n", rs, 10*log10(r.S/r.M), r.corr); }
  R ref = run(2, 1, 1); printf("\n(reference: raw uncorrelated noise would be side/mid ~ 0 dB, corr ~ 0)\n");
}

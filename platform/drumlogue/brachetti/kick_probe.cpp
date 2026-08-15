/**
 * kick_probe.cpp — kick-family knob probe (host-only tool).
 *
 * param_audit.cpp cannot see the kick knobs, for two separate reasons:
 *   1. its 2 s RMS window dilutes a ~58 ms thump ~35x, and
 *   2. RMS is the wrong metric here at all — the master chain is loudness-
 *      maximised (soft clip), so a thump reshapes the attack SPECTRUM without
 *      raising the level.
 * So measure a BAND RATIO over a short window instead: the 100-400 Hz "thump"
 * band against the boom's sub band (<100 Hz), over the first 100 ms.  On that
 * metric TubRad swings 48% where plain RMS shows ~1%.
 *
 * Build:  g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *             -DRUNTIME_COMMON_H_ kick_probe.cpp -o kick_probe && ./kick_probe
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks(){return 1;} uint8_t mock_get_num_samples_for_bank(uint8_t){return 1;}
const sample_wrapper_t* mock_get_sample(uint8_t,uint8_t){return nullptr;}
float ut_exciter_out=0,ut_delay_read=0,ut_voice_out=0;
#include "synth_engine.h"
static unit_runtime_desc_t D;
// Pin the PRNG seed per render — it is free-running in the DSP and no reset
// touches it, so otherwise each render starts from a different noise state.
static double thump_ratio(int preset,int note,int pidx,int pval){
  static BrachettiSynth s; s.Init(&D); s.LoadPreset(preset);
  if(pidx>=0) s.setParameter((uint8_t)pidx,pval);
  for(int i=0;i<NUM_VOICES;++i) s.state.voices[i].exciter.noise_gen.seed=2463534242UL;
  float st[256]; const int N=4800; s.NoteOn((uint8_t)note,100);
  const float a100=1.0f-expf(-2.0f*(float)M_PI*100.0f/48000.0f);
  const float a400=1.0f-expf(-2.0f*(float)M_PI*400.0f/48000.0f);
  float l100=0,l400=0; double lo=0,mid=0;
  for(int f=0;f<N;f+=128){ memset(st,0,sizeof(st)); s.processBlock(st,128);
    for(int i=0;i<128;++i){ float v=st[i*2];
      l100+=a100*(v-l100); l400+=a400*(v-l400);
      float m=l400-l100;           // 100-400 Hz thump band
      lo+=(double)l100*l100; mid+=(double)m*m; } }
  return sqrt(mid/N)/fmax(sqrt(lo/N),1e-9);   // thump-to-boom ratio
}
int main(){
  D.samplerate=48000; D.output_channels=2;
  D.get_num_sample_banks=mock_get_num_sample_banks; D.get_num_samples_for_bank=mock_get_num_samples_for_bank; D.get_sample=mock_get_sample;
  struct K{const char*n;int i;int lo;int hi;} ks[]={
    {"MlltRes",4,0,1000},{"MlltStif",5,10,500},{"VlMllRes",6,-100,100},{"VlMllStf",7,-100,100},
    {"Mterl",11,-10,30},{"TubRad",17,0,20},{"HitPos",13,-98,98},{"Inharm",15,-100,100}};
  struct T{int p;int n;const char*nm;} ts[]={{0,36,"Kick2"},{2,36,"808Sub"},{20,36,"KickDrum"}};
  for(auto&t:ts){ double base=thump_ratio(t.p,t.n,-1,0);
    printf("=== %s  attack(100ms) thump/boom base=%.4f\n",t.nm,base);
    for(auto&k:ks){ double lo=thump_ratio(t.p,t.n,k.i,k.lo), hi=thump_ratio(t.p,t.n,k.i,k.hi);
      double d=fabs(lo-hi)/fmax(fmax(lo,hi),1e-9);
      printf("  %-9s lo=%.4f hi=%.4f  swing=%5.1f%%  %s\n",k.n,lo,hi,d*100,
             d<0.02?"NO EFFECT":d<0.10?"weak":"ok"); } }
}

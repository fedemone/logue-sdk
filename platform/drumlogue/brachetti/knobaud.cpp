/**
 * knobaud.cpp — knob AUDIBILITY: how much of the signal does the knob change?
 * (host-only, throwaway)
 *
 * Metric: RMS of (render at knob max - render at shipped value), expressed in
 * dB relative to the signal RMS.  Unlike a band or total-RMS metric this
 * cannot be fooled by a downstream limiter that holds the LEVEL constant while
 * the character changes — which is exactly what hid these two knobs.
 *   >= -12 dB  clearly audible      -12..-20 dB  subtle      < -20 dB  inert
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks(){return 1;} uint8_t mock_get_num_samples_for_bank(uint8_t){return 1;}
const sample_wrapper_t* mock_get_sample(uint8_t,uint8_t){return nullptr;}
float ut_exciter_out=0,ut_delay_read=0,ut_voice_out=0;
#include "synth_engine.h"
static unit_runtime_desc_t D; static BrachettiSynth s; static const float SR=48000.f;
static std::vector<float> R(int p,uint8_t nt,int knob,int v){
  s.Init(&D); s.LoadPreset((uint8_t)p);
  if(knob>=0) s.setParameter((uint8_t)knob,v);
  for(int i=0;i<NUM_VOICES;++i) s.state.voices[i].exciter.noise_gen.seed=2463534242UL;
  s.NoteOn(nt,110); s.NoteOff(nt);
  std::vector<float> o; float b[256];
  for(int f=0;f<(int)(0.8f*SR);f+=128){memset(b,0,sizeof b);s.processBlock(b,128);
    for(int i=0;i<128;++i)o.push_back(b[i*2]);}
  return o;}
static double dr(const std::vector<float>&a,const std::vector<float>&b){
  double x=0; size_t n=std::min(a.size(),b.size());
  for(size_t i=0;i<n;++i)x+=(double)(a[i]-b[i])*(a[i]-b[i]); return sqrt(x/n);}
static double rr(const std::vector<float>&a){double x=0;for(float v:a)x+=(double)v*v;return sqrt(x/a.size());}
struct Row{const char*n;int i;uint8_t nt;};
int main(){
  D.samplerate=48000;D.output_channels=2;D.get_num_sample_banks=mock_get_num_sample_banks;
  D.get_num_samples_for_bank=mock_get_num_samples_for_bank;D.get_sample=mock_get_sample;
  static const Row rows[]={{"Kick2",0,36},{"808Sub",2,36},{"KickDrum",20,36},
    {"AcSnare",3,38},{"MrchSnr",8,65},{"RimShot",39,69},{"BrshSnr",38,38},
    {"AcTom",12,45},{"Conga",28,62},{"Djambe",6,48},{"Bongo",34,50},
    {"Timpani",5,52},{"Taiko",7,41},{"Cymbal",13,65},{"Ride",32,69},
    {"Marimba",1,72},{"Vibrph",10,72},{"Kalimba",15,65},
    {"Clap",21,60},{"Shaker",22,84},{"HHat-C",26,72},{"GtrStr",25,69}};
  printf("%-9s | %-22s | %-22s\n","preset","VlMllRes  -100 / +100","VlMllStf  -100 / +100");
  for(const Row&r:rows){
    auto base=R(r.i,r.nt,-1,0); double sg=rr(base);
    double v[4];
    v[0]=dr(base,R(r.i,r.nt,6,-100)); v[1]=dr(base,R(r.i,r.nt,6,100));
    v[2]=dr(base,R(r.i,r.nt,7,-100)); v[3]=dr(base,R(r.i,r.nt,7,100));
    auto d=[&](double x){return sg>1e-9&&x>1e-12?20*log10(x/sg):-99.0;};
    auto tag=[&](double a,double b){double m=fmax(a,b);return m>=-12?"LIVE  ":(m>=-20?"subtle":"INERT ");};
    printf("%-9s | %7.1f %7.1f dB %s | %7.1f %7.1f dB %s\n",r.n,
      d(v[0]),d(v[1]),tag(d(v[0]),d(v[1])), d(v[2]),d(v[3]),tag(d(v[2]),d(v[3])));
  }
  return 0;}

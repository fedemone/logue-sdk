#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks(){return 1;}
uint8_t mock_get_num_samples_for_bank(uint8_t){return 1;}
const sample_wrapper_t* mock_get_sample(uint8_t,uint8_t){return nullptr;}
float ut_exciter_out=0.0f, ut_delay_read=0.0f, ut_voice_out=0.0f;
#include "synth_engine.h"
static const int SR=48000;
static std::vector<float> render(int preset,int note,int inh){
    BrachettiSynth s; unit_runtime_desc_t d={}; d.samplerate=SR; d.output_channels=2;
    d.get_num_sample_banks=mock_get_num_sample_banks;
    d.get_num_samples_for_bank=mock_get_num_samples_for_bank;
    d.get_sample=mock_get_sample;
    s.Init(&d); s.LoadPreset((uint8_t)preset);
    for(int i=0;i<NUM_VOICES;++i) s.state.voices[i].exciter.noise_gen.seed=2463534242UL;
    s.setParameter(BrachettiSynth::k_paramInharm, inh);
    int n=SR; std::vector<float> mono(n,0.0f); float buf[128];
    s.GateOn(120); s.GateOff();
    for(int done=0;done<n;done+=64){ memset(buf,0,sizeof(buf)); s.processBlock(buf,64);
        for(int i=0;i<64&&done+i<n;++i) mono[done+i]=buf[i*2]; }
    return mono;
}
static float drms(const std::vector<float>&a,const std::vector<float>&b){
    double d=0,s=0; for(size_t i=0;i<a.size();++i){ double x=a[i]-b[i]; d+=x*x; s+=b[i]*b[i]; }
    if(s<=0) return -999; return 10.0f*log10f((float)(d/s));
}
int main(){
    struct T{int p;int n;const char*name;const char*mapping;};
    T ts[]={{29,62,"Handpan","modal spread"},{1,72,"Marimba","modal spread"},
            {5,52,"Timpani","kernel stretch"},{7,45,"Taiko","kernel stretch"},
            {13,65,"Cymbal","cymbal jitter"},{14,50,"Gong","cymbal jitter"},
            {9,60,"Koto","KS allpass"},{25,69,"GtrStr","KS allpass"}};
    printf("difference-RMS vs the shipped Inharm render (dB; below about -60 = inaudible/dead)\n");
    printf("%-9s %-15s %10s %10s %10s %10s\n","preset","mapping","-100","-50","+50","+100");
    for(auto&t:ts){
        auto base=render(t.p,t.n,0);
        printf("%-9s %-15s %10.1f %10.1f %10.1f %10.1f\n",t.name,t.mapping,
            drms(render(t.p,t.n,-100),base), drms(render(t.p,t.n,-50),base),
            drms(render(t.p,t.n,50),base),   drms(render(t.p,t.n,100),base));
    }
    return 0;
}

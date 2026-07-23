// stability_sweep.cpp — hammer the newly-wired knobs (Dkay/Mterl/HitPos/Rel/
// Inharm/TubRad/Resnc) at extreme combinations across every affected preset and
// assert no NaN/Inf and bounded peak.
#include <cstdio>
#include <cmath>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
#include "synth_engine.h"

int main(){
    unit_runtime_desc_t d={}; d.samplerate=48000; d.output_channels=2;
    d.get_num_sample_banks=mock_get_num_sample_banks; d.get_num_samples_for_bank=mock_get_num_samples_for_bank; d.get_sample=mock_get_sample;
    static BrachettiSynth s; const int sr=48000; const int block=128; static float st[block*2];

    // param idx: Dkay10 Mterl11 HitPos13 Rel14 Inharm15 TubRad17 Resnc23
    struct P{int idx;int lo;int hi;}; P ps[]={{10,0,200},{11,-10,30},{13,2,98},{14,0,20},{15,0,199},{17,0,20},{23,71,400}};
    const int NP=7;
    // affected presets across all three families + a couple neighbours
    int presets[]={0,2,20, 5,7,12,28,34,6,23, 13,14,27,32,33,37};
    int notes[]  ={36,36,36,52,45,45,50,55,48,40,69,50,79,69,60,76};
    int NPRE=sizeof(presets)/sizeof(int);

    int bad=0, combos=0; float worst=0;
    for(int pi=0; pi<NPRE; ++pi){
        // all 2^7 corner combinations of the 7 knobs
        for(int m=0; m<(1<<NP); ++m){
            s.Init(&d); s.LoadPreset((uint8_t)presets[pi]);
            for(int k=0;k<NP;++k) s.setParameter(ps[k].idx, (m&(1<<k))?ps[k].hi:ps[k].lo);
            // two velocities, retrigger
            for(int rep=0; rep<2; ++rep){
                s.NoteOn((uint8_t)notes[pi], rep? 40:120);
                int n=sr*3, f=0; bool released=false;
                while(f<n){ int t=block; if(f+t>n)t=n-f; s.processBlock(st,(size_t)t);
                    for(int i=0;i<t*2;++i){ float v=st[i];
                        if(!std::isfinite(v)){bad++; goto nextcombo;}
                        float a=fabsf(v); if(a>worst)worst=a;
                        if(a>1.5f){bad++; printf("  BLOWUP preset %d combo %d peak %.2f\n",presets[pi],m,a); goto nextcombo;} }
                    f+=t; if(!released && f>=sr/40){ s.NoteOff((uint8_t)notes[pi]); released=true; } }
            }
            nextcombo: combos++;
        }
    }
    printf("stability: %d combos across %d presets, worst |peak|=%.4f, %d problems\n", combos, NPRE, worst, bad);
    return bad?1:0;
}

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks(){return 1;}
uint8_t mock_get_num_samples_for_bank(uint8_t){return 1;}
const sample_wrapper_t* mock_get_sample(uint8_t,uint8_t){return nullptr;}
float ut_exciter_out=0.0f, ut_delay_read=0.0f, ut_voice_out=0.0f;
#include "synth_engine.h"
static const int SR=48000;
int main(int argc,char**argv){
    int preset = (argc>1)?atoi(argv[1]):2;
    printf("preset %d — dive start measured from the first half-cycles, vs Inharm\n",preset);
    printf("  Inharm   f@~3ms   f@~15ms   f@~50ms   rms250\n");
    for(int inh : {-100,-50,0,25,50,100}){
        BrachettiSynth s; unit_runtime_desc_t d={}; d.samplerate=SR; d.output_channels=2;
        d.get_num_sample_banks=mock_get_num_sample_banks;
        d.get_num_samples_for_bank=mock_get_num_samples_for_bank;
        d.get_sample=mock_get_sample;
        s.Init(&d); s.LoadPreset((uint8_t)preset);
        for(int i=0;i<NUM_VOICES;++i) s.state.voices[i].exciter.noise_gen.seed=2463534242UL;
        s.setParameter(BrachettiSynth::k_paramInharm, inh);
        int n=SR/2; float* mono=new float[n](); float buf[128];
        s.GateOn(110); s.GateOff();
        for(int done=0;done<n;done+=64){ memset(buf,0,sizeof(buf)); s.processBlock(buf,64);
            for(int i=0;i<64&&done+i<n;++i) mono[done+i]=buf[i*2]; }
        // zero-crossing instantaneous frequency near given times
        auto fAt=[&](float tms)->float{
            int c=(int)(tms*0.001f*SR); int lo=-1,hi=-1;
            for(int i=(c>200?c-200:1);i<c+3000&&i<n;++i)
                if((mono[i-1]<0)!=(mono[i]<0)){ if(lo<0&&i>=c) lo=i; else if(lo>=0&&i>lo){hi=i;break;} }
            return (lo>0&&hi>lo)? (float)SR/(2.0f*(hi-lo)) : 0.0f;
        };
        double e=0; for(int i=0;i<SR/4;++i) e+=mono[i]*mono[i];
        printf("  %6d  %7.1f  %8.1f  %8.1f   %.4f\n",inh,fAt(3),fAt(15),fAt(50),sqrt(e/(SR/4)));
        delete[] mono;
    }
    return 0;
}

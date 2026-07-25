// verify_blind.cpp — confirm params the 2s-RMS audit can't see actually move:
// boom PITCH, cymbal SIZE (HF fraction), and ring TAIL in the window where the
// preset is still ringing.  Kernel drums (Timpani/Taiko) vs legacy membranes.
#include <cstdio>
#include <cmath>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
#include "synth_engine.h"

static float buf[48000*6];
static void render(BrachettiSynth& s, int note, int n) {
    s.NoteOn((uint8_t)note, 110);
    const int block=128; static float st[block*2];
    int f=0; while(f<n){ int t=block; if(f+t>n)t=n-f; s.processBlock(st,(size_t)t);
        for(int i=0;i<t;++i) buf[f+i]=st[i*2]; f+=t; }
}
static float boom_pitch(const float* x,int n,int sr){
    static float y[48000*6]; float a=1.f-expf(-2.f*(float)M_PI*300.f/sr),s=0;
    for(int i=0;i<n;++i){s+=a*(x[i]-s);y[i]=s;}
    int mn=sr/400,mx=sr/25; double best=-1; int bl=mn; int W=sr/6; if(W>n-mx)W=n-mx;
    for(int lag=mn;lag<=mx;++lag){double acc=0;for(int i=0;i<W;++i)acc+=(double)y[i]*y[i+lag];
        if(acc>best){best=acc;bl=lag;}}
    return (float)sr/(float)bl;
}
static float hf_fraction(const float* x,int n,int sr){
    float lp=0,a=1.f-expf(-2.f*(float)M_PI*3000.f/sr); double hi=0,tot=0; int W=sr*3/10; if(W>n)W=n;
    for(int i=0;i<W;++i){lp+=a*(x[i]-lp);float h=x[i]-lp;hi+=(double)h*h;tot+=(double)x[i]*x[i];}
    return tot>1e-12?(float)(hi/tot):0;
}
static double win_rms(const float* x,int a,int b){double s=0;for(int i=a;i<b;++i)s+=(double)x[i]*x[i];return sqrt(s/(b-a));}

int main(){
    unit_runtime_desc_t d={}; d.samplerate=48000; d.output_channels=2;
    d.get_num_sample_banks=mock_get_num_sample_banks; d.get_num_samples_for_bank=mock_get_num_samples_for_bank; d.get_sample=mock_get_sample;
    static BrachettiSynth s; const int sr=48000;

    // tail over a chosen [a,b] second window
    auto tail=[&](int preset,int note,int pidx,int v,float aS,float bS){ int n=(int)(bS*sr);
        s.Init(&d); s.LoadPreset((uint8_t)preset); s.setParameter(pidx,v); render(s,note,n);
        return (float)(win_rms(buf,(int)(aS*sr),n)*1000.0); };
    auto hff=[&](int preset,int note,int pidx,int v){ s.Init(&d); s.LoadPreset((uint8_t)preset); s.setParameter(pidx,v); render(s,note,sr); return hf_fraction(buf,sr,sr); };
    auto pit=[&](int preset,int note,int pidx,int v){ s.Init(&d); s.LoadPreset((uint8_t)preset); s.setParameter(pidx,v); render(s,note,sr); return boom_pitch(buf,sr,sr); };

    printf("KICK boom pitch (TubRad bigger=lower):\n");
    printf("  KickDrum  %5.1f -> %5.1f Hz\n", pit(20,36,17,0), pit(20,36,17,20));
    printf("  808Sub    %5.1f -> %5.1f Hz\n", pit(2, 36,17,0), pit(2, 36,17,20));
    printf("  Kick2     %5.1f -> %5.1f Hz\n", pit(0, 36,17,0), pit(0, 36,17,20));

    printf("KERNEL drums TubRad (now wired via kernel; tail 1.5-2.5s, x1e3):\n");
    printf("  Timpani   %6.3f -> %6.3f   (HF frac %.3f -> %.3f)\n",
           tail(5,52,17,0,1.5f,2.5f), tail(5,52,17,20,1.5f,2.5f), hff(5,52,17,0), hff(5,52,17,20));
    printf("  Taiko     %6.3f -> %6.3f\n", tail(7,45,17,0,0.6f,1.2f), tail(7,45,17,20,0.6f,1.2f));

    printf("LEGACY membrane TubRad (modal-bank path; tail window per preset, x1e3):\n");
    printf("  Conga     %6.3f -> %6.3f\n", tail(28,50,17,0,0.4f,0.9f), tail(28,50,17,20,0.4f,0.9f));
    printf("  AcTom     %6.3f -> %6.3f\n", tail(12,45,17,0,0.4f,0.9f), tail(12,45,17,20,0.4f,0.9f));

    printf("CYMBAL tails (1-2s window where it still rings, x1e3):\n");
    printf("  Cymbal Dkay  %6.3f -> %6.3f\n", tail(13,69,10,0,1.f,2.f), tail(13,69,10,200,1.f,2.f));
    printf("  Cymbal Rel   %6.3f -> %6.3f\n", tail(13,69,14,0,1.f,2.f), tail(13,69,14,20,1.f,2.f));
    printf("  Cymbal Size(TubRad) HF %.3f -> %.3f\n", hff(13,69,17,0), hff(13,69,17,20));
    return 0;
}

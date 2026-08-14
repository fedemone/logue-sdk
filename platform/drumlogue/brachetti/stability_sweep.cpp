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
    // plus a combined VlMllRes6+VlMllStf7+Velocity3 extreme bit (the three
    // hit-hardness knobs move together, which keeps the corner count at 2^8
    // instead of 2^10 and is also the corner that matters: full wham on top of
    // maximum velocity sensitivity is the loudest strike the unit can produce).
    struct P{int idx;int lo;int hi;}; P ps[]={{10,0,200},{11,-10,30},{13,2,98},{14,0,20},{15,-100,100},{17,0,20},{23,71,400},{6,-100,100}};
    const int NP=8;
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
            s.setParameter(7, (m&(1<<(NP-1)))?100:-100);   // VlMllStf rides the VlMllRes bit
            s.setParameter(3, (m&(1<<(NP-1)))?100:-100);   // Velocity too: +100 = wham, -100 = ghost
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

    // ── Phase 2: ROLL stress ────────────────────────────────────────────────
    // Phase 1 leaves 3 s between hits, so it never exercises the roll paths:
    // voice fusion inside the fuse window, the snare wire-state restore that
    // rides on it, and the stacked round-robin just outside it.  Hammer every
    // preset with fast strokes at extreme knob settings — fused rolls reuse one
    // voice (its resonator state is restored, not zeroed, on the snare), and
    // stacked rolls pile up to Poly bodies, so both need a finiteness check.
    int rbad = 0, rolls = 0; float rworst = 0;
    for (int p = 0; p < BrachettiSynth::k_NumPrograms; ++p) {
        // gap 25/45 ms = fused; 120 ms = stacked.  Alternating notes defeat the
        // same-note test, so a fast alternating figure stacks even inside the
        // window — cover that too.
        const float gaps[] = {25.0f, 45.0f, 120.0f};
        for (int gi = 0; gi < 3; ++gi) {
            for (int alt = 0; alt < 2; ++alt) {
                for (int ext = 0; ext < 2; ++ext) {
                    s.Init(&d); s.LoadPreset((uint8_t)p);
                    // ext=0: shipped knobs.  ext=1: every swept knob at an
                    // extreme, Poly wide open so stacking is unrestricted.
                    if (ext) {
                        for (int k = 0; k < NP; ++k) s.setParameter(ps[k].idx, ps[k].hi);
                        s.setParameter(7, 100);
                        s.setParameter(3, 100);      // Velocity = full wham
                        s.setParameter(2, 4);        // Poly = 4
                    }
                    const int gap = (int)(gaps[gi] * 0.001f * (float)sr);
                    for (int k = 0; k < 20; ++k) {
                        uint8_t note = (uint8_t)(48 + (alt ? (k & 1) * 5 : 0));
                        s.NoteOn(note, (k & 1) ? 127 : 70);
                        s.NoteOff(note);
                        int done = 0;
                        while (done < gap) {
                            int t = (gap - done < block) ? (gap - done) : block;
                            s.processBlock(st, (size_t)t);
                            for (int i = 0; i < t * 2; ++i) {
                                float v = st[i];
                                if (!std::isfinite(v)) { rbad++; printf("  ROLL NaN preset %d gap %.0f alt %d ext %d\n", p, gaps[gi], alt, ext); goto nextroll; }
                                float a = fabsf(v); if (a > rworst) rworst = a;
                                if (a > 1.5f) { rbad++; printf("  ROLL BLOWUP preset %d gap %.0f alt %d ext %d peak %.2f\n", p, gaps[gi], alt, ext, a); goto nextroll; }
                            }
                            done += t;
                        }
                    }
                    nextroll: rolls++;
                }
            }
        }
    }
    printf("roll stress: %d rolls across %d presets, worst |peak|=%.4f, %d problems\n",
           rolls, (int)BrachettiSynth::k_NumPrograms, rworst, rbad);
    return (bad || rbad) ? 1 : 0;
}

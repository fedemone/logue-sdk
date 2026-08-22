/**
 * switch_probe.cpp — preset change while voices are ringing (host-only, throwaway).
 *
 * processBlock reads `kPresetEngine[m_preset_idx]` LIVE every block, and
 * LoadPreset only retires ringing voices on the Timpani/Taiko branch.  So a
 * voice struck under one engine can be rendered by another engine's per-sample
 * code.  Print peak/RMS per 25 ms across the switch and look for a jump.
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *            -DRUNTIME_COMMON_H_ switch_probe.cpp -o /tmp/switch && /tmp/switch
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0, ut_delay_read = 0, ut_voice_out = 0;
#include "synth_engine.h"

static unit_runtime_desc_t D;
static BrachettiSynth s;

static void win(int n, double* rms, double* pk) {
    const int W = 48000 / 40;   // 25 ms
    float st[256];
    for (int w = 0; w < n; ++w) {
        double acc = 0; double p = 0;
        for (int f = 0; f < W; f += 128) {
            memset(st, 0, sizeof(st));
            s.processBlock(st, 128);
            for (int i = 0; i < 128; ++i) {
                double v = st[i * 2];
                acc += v * v;
                if (fabs(v) > p) p = fabs(v);
            }
        }
        rms[w] = sqrt(acc / W); pk[w] = p;
    }
}

static void trial(int from, const char* fname, int to, const char* tname, int note) {
    s.Init(&D);
    s.LoadPreset((uint8_t)from);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
    s.GateOn(110);
    s.GateOff();

    double r1[6], p1[6], r2[8], p2[8];
    win(6, r1, p1);                      // 150 ms ringing under `from`
    int act = 0;
    for (int i = 0; i < NUM_VOICES; ++i) if (s.state.voices[i].is_active) ++act;
    s.LoadPreset((uint8_t)to);           // <-- switch mid-ring
    int act2 = 0;
    for (int i = 0; i < NUM_VOICES; ++i) if (s.state.voices[i].is_active) ++act2;
    win(8, r2, p2);                      // 200 ms after the switch

    printf("%-8s -> %-8s  active %d->%d\n", fname, tname, act, act2);
    printf("   before rms:"); for (int i = 0; i < 6; ++i) printf(" %.4f", r1[i]);
    printf("\n   before pk :"); for (int i = 0; i < 6; ++i) printf(" %.4f", p1[i]);
    printf("\n   after  rms:"); for (int i = 0; i < 8; ++i) printf(" %.4f", r2[i]);
    printf("\n   after  pk :"); for (int i = 0; i < 8; ++i) printf(" %.4f", p2[i]);
    double jump = (r1[5] > 1e-9) ? r2[0] / r1[5] : 0.0;
    // The honest test is against the signal that was ALREADY playing: a fade
    // may not be louder than the tail it is fading out.
    double pk_jump = (p1[5] > 1e-9) ? p2[0] / p1[5] : 0.0;
    printf("\n   rms  after/before = %.2f   peak after/before = %.2f%s\n\n", jump, pk_jump,
           (pk_jump > 1.2 || p2[0] > 0.95) ? "   <-- LOUDER THAN THE TAIL IT REPLACES" : "");
}

int main() {
    D.samplerate = 48000; D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;

    trial(14, "Gong",   9, "Koto", 50);   // CYMBAL -> KS
    trial(14, "Gong",   0,  "Kick2",  50);   // CYMBAL -> MEMBRANE
    trial(9, "Koto", 14, "Gong",   69);   // KS     -> CYMBAL
    trial(9, "Koto", 5,  "Timpani",69);   // KS     -> kernel (retires voices)
    trial(13, "Cymbal", 21, "Clap",   65);   // CYMBAL -> NOISE
    trial(5,  "Timpani",9, "Koto", 52);   // kernel -> KS
    return 0;
}

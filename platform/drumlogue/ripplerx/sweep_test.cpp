/**
 * sweep_test.cpp — Quantify how far each widened knob moves the sound.
 *
 * For a modal preset we render the note at the LOW and HIGH end of a knob
 * (everything else at the shipped default) and measure two monotone proxies:
 *   brightness  = sqrt(Σ(Δx)² / Σx²)        (upper-mode / centroid proxy)
 *   tail_ratio  = RMS(2–3 s) / RMS(0–100 ms) (ring-length proxy)
 * The SPREAD between low and high is the knob's audible authority.  Run this
 * on the widened build and the baseline build; a larger spread = stronger knob.
 *
 * Build: g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \
 *        -DRUNTIME_COMMON_H_ sweep_test.cpp -o sweep_test
 */
#include <cstdio>
#include <cstring>
#include <cmath>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;
#include "synth_engine.h"

static const int SR = 48000;

// Render preset `idx` with one param overridden, return metrics.
static void render(int idx, uint8_t note, int param, int value,
                   float dur_s, double& brightness, double& tail) {
    RipplerXWaveguide synth;
    unit_runtime_desc_t desc = {};
    desc.samplerate = SR; desc.output_channels = 2;
    desc.get_num_sample_banks     = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample               = mock_get_sample;
    synth.Init(&desc);
    synth.LoadPreset(idx);
    if (param >= 0) synth.setParameter((uint8_t)param, value);

    const int total = (int)(dur_s * SR);
    static float mono[48000 * 4];
    float st[256];
    synth.NoteOn(note, 100);
    int frame = 0; bool rel = false;
    while (frame < total) {
        int todo = 128; if (frame + todo > total) todo = total - frame;
        memset(st, 0, todo * 2 * sizeof(float));
        synth.processBlock(st, (size_t)todo);
        for (int i = 0; i < todo; ++i) mono[frame + i] = st[i * 2];
        frame += todo;
        if (!rel && frame >= SR / 20) { synth.NoteOff(note); rel = true; }
    }
    // brightness
    double e_tot = 0, e_hi = 0; float prev = 0;
    for (int n = 0; n < total; ++n) {
        e_tot += (double)mono[n] * mono[n];
        float d = mono[n] - prev; prev = mono[n];
        e_hi += (double)d * d;
    }
    brightness = sqrt(e_hi / (e_tot + 1e-12));
    // T40 decay time (ms): windowed-RMS envelope, time from peak to -40 dB.
    // Works for short sounds (unlike a fixed 2-3 s tail window).
    const int W = SR / 200;                   // 5 ms windows
    double peak = 0; int nb = total / W;
    static double env[ /*~600*/ 4000 ];
    for (int b = 0; b < nb && b < 4000; ++b) {
        double s = 0; for (int n = b*W; n < (b+1)*W; ++n) s += (double)mono[n]*mono[n];
        env[b] = sqrt(s / W); if (env[b] > peak) peak = env[b];
    }
    int pk = 0; for (int b = 0; b < nb && b < 4000; ++b) if (env[b] >= peak) { pk = b; break; }
    double thr = peak * 0.01;                  // -40 dB
    int t40 = pk;
    for (int b = pk; b < nb && b < 4000; ++b) { if (env[b] <= thr) { t40 = b; break; } t40 = b; }
    tail = (t40 - pk) * 5.0;                    // ms
}

struct Knob { const char* name; int param; int lo; int hi; bool ring; };

int main(int argc, char** argv) {
    int idx = (argc > 1) ? atoi(argv[1]) : 5;       // default Timpani
    uint8_t note = (idx == 7) ? 41 : 40;
    const char* pname = (idx == 7) ? "Taiko" : "Timpani";

    // param, lo, hi, ring?(use tail metric instead of brightness)
    Knob knobs[] = {
        { "MlltRes ",  4,    0, 1000, false },
        { "MlltStif",  5,   10,  500, false },
        { "Partls  ",  8,    0,    4, false },
        { "Mterl   ", 11,  -10,   30, false },
        { "HitPos  ", 13,    2,   98, false },
        { "Inharm  ", 15,    0, 1999, false },
        { "Dkay    ", 10,    0,  200, true  },
        { "Rel     ", 14,    0,   20, true  },
    };
    printf("=== %s (preset %d, note %d) — knob authority (spread lo→hi) ===\n",
           pname, idx, (int)note);
    printf("%-9s  %-22s  %-22s  %s\n", "KNOB", "metric @ LO", "metric @ HI", "SPREAD (|hi-lo|)");
    for (auto& k : knobs) {
        double blo, tlo, bhi, thi;
        render(idx, note, k.param, k.lo, 3.0f, blo, tlo);
        render(idx, note, k.param, k.hi, 3.0f, bhi, thi);
        if (k.ring) {
            printf("%-9s  T40=%-13.0fms  T40=%-13.0fms  %.0f ms\n",
                   k.name, tlo, thi, fabs(thi - tlo));
        } else {
            printf("%-9s  brt=%-17.5f  brt=%-17.5f  %.5f\n",
                   k.name, blo, bhi, fabs(bhi - blo));
        }
    }
    return 0;
}

/**
 * param_audit.cpp — empirical ParamIndex wiring audit (host-only tool).
 *
 * For each engine family exemplar preset, renders a baseline and then a
 * min/max sweep of every UI parameter, reporting feature deltas:
 *   rms      — overall loudness
 *   lowRMS   — energy below ~250 Hz   (1-pole LP)
 *   hiRMS    — energy above ~2.5 kHz  (1-pole HP)
 *   tailRMS  — RMS of the last 25% of the render (decay/sustain length)
 *
 * Usage: g++ -std=c++17 -O2 -I. -I.. -I../common param_audit.cpp -o param_audit && ./param_audit
 */

#include <cstdio>
#include <cstring>
#include <cmath>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

struct Features {
    float rms, low_rms, hi_rms, tail_rms;
};

static Features render_features(RipplerXWaveguide& synth, int note, float dur_s) {
    const int sr = 48000;
    const int total = (int)(dur_s * sr);
    const int block = 128;
    static float stereo[block * 2];

    synth.NoteOn((uint8_t)note, 100);

    double acc = 0, acc_lo = 0, acc_hi = 0, acc_tail = 0;
    int tail_start = total * 3 / 4;
    float lp250 = 0.0f, lp2500 = 0.0f;
    const float a250  = 1.0f - expf(-2.0f * (float)M_PI * 250.0f / sr);
    const float a2500 = 1.0f - expf(-2.0f * (float)M_PI * 2500.0f / sr);

    int frame = 0;
    bool released = false;
    while (frame < total) {
        int todo = block;
        if (frame + todo > total) todo = total - frame;
        synth.processBlock(stereo, (size_t)todo);
        for (int i = 0; i < todo; ++i) {
            float v = stereo[i * 2];
            lp250  += a250  * (v - lp250);
            lp2500 += a2500 * (v - lp2500);
            float hi = v - lp2500;
            acc    += (double)v * v;
            acc_lo += (double)lp250 * lp250;
            acc_hi += (double)hi * hi;
            if (frame + i >= tail_start) acc_tail += (double)v * v;
        }
        frame += todo;
        if (!released && frame >= sr / 50) {  // gate off after 20 ms (one-shot)
            synth.NoteOff((uint8_t)note);
            released = true;
        }
    }
    Features f;
    f.rms      = (float)sqrt(acc / total);
    f.low_rms  = (float)sqrt(acc_lo / total);
    f.hi_rms   = (float)sqrt(acc_hi / total);
    f.tail_rms = (float)sqrt(acc_tail / (total - tail_start));
    return f;
}

struct ParamSpec { const char* name; uint8_t idx; int32_t lo; int32_t hi; };

int main() {
    // name, index, audit-low, audit-high (from header.c ranges)
    static const ParamSpec specs[] = {
        {"MlltRes",  4,  0,    1000},
        {"MlltStif", 5,  10,   500},
        {"VlMllRes", 6,  -100, 100},
        {"VlMllStf", 7,  -100, 100},
        {"Partls",   8,  0,    4},
        {"Model",    9,  0,    8},
        {"Dkay",     10, 10,   200},
        {"Mterl",    11, -10,  30},
        {"Tone",     12, -10,  30},
        {"HitPos",   13, 2,    98},
        {"Rel",      14, 0,    20},
        {"Inharm",   15, 0,    1999},
        {"LowCut",   16, 1,    1500},
        {"TubRad",   17, 0,    20},
        {"Gain",     18, 0,    100},
        {"NzMix",    19, 0,    100},
        {"NzRes",    20, 0,    1000},
        {"NzFltr",   21, 0,    2},
        {"NzFltFrq", 22, 2,    2000},
        {"Resnc",    23, 707,  4000},
    };

    struct Target { const char* family; int preset; int note; };
    static const Target targets[] = {
        {"KS      ", 25, 69},  // GtrStr
        {"BAR     ", 1,  72},  // Marimba
        {"MEMBRANE", 5,  40},  // Timpani
        {"SNARE   ", 3,  38},  // AcSnare
        {"PLATE   ", 18, 67},  // Cowbell
        {"NOISE   ", 21, 60},  // Clap
    };

    unit_runtime_desc_t desc = {};
    desc.samplerate = 48000;
    desc.output_channels = 2;
    desc.get_num_sample_banks = mock_get_num_sample_banks;
    desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    desc.get_sample = mock_get_sample;

    static RipplerXWaveguide synth;

    for (const Target& t : targets) {
        if (synth.Init(&desc) != 0) { fprintf(stderr, "init fail\n"); return 1; }
        synth.LoadPreset((uint8_t)t.preset);
        Features base = render_features(synth, t.note, 2.0f);
        printf("\n=== %s preset %d (%s) note %d ===\n", t.family, t.preset,
               RipplerXWaveguide::getPresetName((uint8_t)t.preset), t.note);
        printf("  baseline: rms=%.5f low=%.5f hi=%.5f tail=%.6f\n",
               base.rms, base.low_rms, base.hi_rms, base.tail_rms);
        printf("  %-9s | %-32s | %-32s | verdict\n", "param", "value=LO  rms/low/hi/tail", "value=HI  rms/low/hi/tail");

        for (const ParamSpec& p : specs) {
            // fresh state per param
            synth.Init(&desc);
            synth.LoadPreset((uint8_t)t.preset);
            synth.setParameter(p.idx, p.lo);
            Features flo = render_features(synth, t.note, 2.0f);

            synth.Init(&desc);
            synth.LoadPreset((uint8_t)t.preset);
            synth.setParameter(p.idx, p.hi);
            Features fhi = render_features(synth, t.note, 2.0f);

            auto rel = [](float a, float b) {
                float m = fmaxf(fmaxf(fabsf(a), fabsf(b)), 1e-9f);
                return fabsf(a - b) / m;
            };
            float d = fmaxf(fmaxf(rel(flo.rms, fhi.rms), rel(flo.low_rms, fhi.low_rms)),
                            fmaxf(rel(flo.hi_rms, fhi.hi_rms), rel(flo.tail_rms, fhi.tail_rms)));
            const char* verdict = (d < 0.02f) ? "NO EFFECT" : (d < 0.10f) ? "weak" : "ok";
            printf("  %-9s | %.4f %.4f %.4f %.5f | %.4f %.4f %.4f %.5f | %s\n",
                   p.name,
                   flo.rms, flo.low_rms, flo.hi_rms, flo.tail_rms,
                   fhi.rms, fhi.low_rms, fhi.hi_rms, fhi.tail_rms,
                   verdict);
        }
    }
    return 0;
}

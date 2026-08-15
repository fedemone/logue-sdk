/**
 * plateau_probe.cpp — find knob travel that renders IDENTICALLY (host-only).
 *
 * The pass-36/37 defect class: a reference-anchored mapping whose factor is
 * CLAMPED hits its clamp partway through the knob's travel, so a whole stretch
 * of the range produces bit-identical audio.  The knob looks wired — it moves,
 * the audit reports "live" — but a third of it does nothing.  Inharm had two of
 * these (the dive depth floored at −30, the modal overtone spread floored at
 * −40) and neither showed up in any existing tool, because param_audit only
 * compares the two ENDS of a range and a plateau in the middle is invisible to
 * a min/max test.
 *
 * This probe walks each parameter across its whole declared range in N steps,
 * hashes the render at every step, and reports how many DISTINCT renders come
 * back.  Fewer distinct renders than steps = dead travel, and the step list
 * shows exactly where it collapses.
 *
 * Also reports whether the knob is ONE-SIDED on this preset: if every step at
 * or below the shipped value renders identically to the shipped render, the
 * downward half of the knob is dead — which is what "all 41 presets ship this
 * parameter on its floor" produces on an anchored mapping.
 *
 * KNOWN LIMITATION — read before believing a "plateau" flag.  The sweep takes
 * kSteps samples across the range whatever the range is, so on a parameter with
 * fewer than kSteps distinct integers (Partls 0..7, Model 0..8, NzFltr 0..2)
 * several steps land on the SAME value and the collapsed-span report is a
 * sampling artefact, not dead travel.  The DEAD and one-sided flags stay valid
 * there — those compare every distinct value that was actually set.
 *
 * Usage:
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       plateau_probe.cpp -o /tmp/plateau_probe && /tmp/plateau_probe
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

// MUST track header.c.  A stale bound here makes the probe sweep values that
// setParameter rejects, which is exactly how T27a/stability_sweep/param_audit
// spent a pass reporting PASS on a value they never set (see pass 36).
struct ParamRange { int idx; int lo; int hi; const char* name; };
static const ParamRange kParams[] = {
    { 4,    0, 1000, "MlltRes"  }, { 5,     0,   50, "MlltStif" },
    { 6, -100,  100, "VlMllRes" }, { 7,  -100,  100, "VlMllStf" },
    { 8,    0,    7, "Partls"   }, { 9,     0,    8, "Model"    },
    {10,    0,  210, "Dkay"     }, {11,   -10,   30, "Mterl"    },
    {12,  -10,   30, "Tone"     }, {13,   -98,   98, "HitPos"   },
    {14,    0,   20, "Rel"      }, {15,  -100,  100, "Inharm"   },
    {16,    1, 1999, "Cutoff"   }, {17,     0,   20, "TubRad"   },
    {18,    0,  100, "Gain"     }, {19,     0,  100, "NzMix"    },
    {20,    0, 1000, "NzRes"    }, {21,     0,    2, "NzFltr"   },
    {22,    2, 2000, "NzFltFrq" }, {23,    71,  400, "Resnc"    },
};
static const int kNumParams = (int)(sizeof(kParams) / sizeof(kParams[0]));

// One exemplar per engine family, at its own shipped note.
struct Exemplar { int preset; int note; const char* name; };
static const Exemplar kExemplars[] = {
    {  0, 36, "Kick2"   },   // MEMBRANE + boom
    {  1, 72, "Marimba" },   // BAR
    {  3, 38, "AcSnare" },   // SNARE
    {  5, 52, "Timpani" },   // dense kernel
    {  9, 60, "Koto"    },   // KS
    { 13, 69, "Cymbal"  },   // CYMBAL
    { 21, 60, "Clap"    },   // NOISE
    { 29, 52, "Handpan" },   // modal-rich MEMBRANE
};
static const int kNumExemplars = (int)(sizeof(kExemplars) / sizeof(kExemplars[0]));

static const int kSteps = 11;

static void pin_noise_seed(BrachettiSynth& s) {
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
}

// FNV-1a over the quantised render.  Quantising to 1e-7 keeps the hash from
// tripping on last-bit float noise while still separating anything audible.
static uint64_t render_hash(BrachettiSynth& s, int note, float dur_s) {
    pin_noise_seed(s);
    const int sr = 48000, block = 128;
    const int total = (int)(dur_s * sr);
    static float stereo[block * 2];
    s.NoteOn((uint8_t)note, 100);
    uint64_t h = 1469598103934665603ULL;
    int frame = 0; bool released = false;
    while (frame < total) {
        int todo = (total - frame < block) ? (total - frame) : block;
        s.processBlock(stereo, (size_t)todo);
        for (int i = 0; i < todo; ++i) {
            int32_t q = (int32_t)lrintf(stereo[i * 2] * 1.0e7f);
            for (int b = 0; b < 4; ++b) {
                h ^= (uint64_t)((q >> (b * 8)) & 0xFF);
                h *= 1099511628211ULL;
            }
        }
        frame += todo;
        if (!released && frame >= sr / 40) { s.NoteOff((uint8_t)note); released = true; }
    }
    return h;
}

int main() {
    unit_runtime_desc_t d = {};
    d.samplerate = 48000; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    static BrachettiSynth s;

    printf("plateau probe — %d steps across each parameter's full declared range\n", kSteps);
    printf("distinct = how many of those %d steps produce a DIFFERENT render\n", kSteps);
    printf("below/above = distinct renders at steps under / over the preset's shipped value\n");
    printf("(below=1 means the whole downward half of the knob is dead on this preset)\n\n");

    for (int e = 0; e < kNumExemplars; ++e) {
        printf("=== %-8s (preset %2d, note %d) ===\n",
               kExemplars[e].name, kExemplars[e].preset, kExemplars[e].note);
        printf("  %-9s %10s %9s %8s %8s   %s\n",
               "param", "shipped", "distinct", "below", "above", "collapsed spans");
        for (int p = 0; p < kNumParams; ++p) {
            const ParamRange& pr = kParams[p];
            s.Init(&d); s.LoadPreset((uint8_t)kExemplars[e].preset);
            int shipped = s.getParameterValue(pr.idx);

            uint64_t hashes[kSteps];
            int      values[kSteps];
            for (int k = 0; k < kSteps; ++k) {
                int v = pr.lo + (int)((int64_t)(pr.hi - pr.lo) * k / (kSteps - 1));
                values[k] = v;
                s.Init(&d); s.LoadPreset((uint8_t)kExemplars[e].preset);
                s.setParameter(pr.idx, v);
                hashes[k] = render_hash(s, kExemplars[e].note, 1.0f);
            }

            int distinct = 0, below = 0, above = 0;
            for (int k = 0; k < kSteps; ++k) {
                bool seen = false, seen_b = false, seen_a = false;
                for (int j = 0; j < k; ++j) {
                    if (hashes[j] == hashes[k]) {
                        seen = true;
                        if (values[j] <= shipped) seen_b = true;
                        if (values[j] >= shipped) seen_a = true;
                    }
                }
                if (!seen) distinct++;
                if (values[k] <= shipped && !seen_b) below++;
                if (values[k] >= shipped && !seen_a) above++;
            }

            char spans[256] = ""; int sl = 0;
            for (int k = 1; k < kSteps; ++k) {
                if (hashes[k] == hashes[k - 1]) {
                    int j = k;
                    while (j + 1 < kSteps && hashes[j + 1] == hashes[k - 1]) j++;
                    sl += snprintf(spans + sl, sizeof(spans) - sl, "%s%d..%d",
                                   sl ? ", " : "", values[k - 1], values[j]);
                    k = j;
                }
            }
            const char* mark = (distinct <= 1) ? "  <-- DEAD"
                             : (below <= 1)    ? "  <-- one-sided"
                             : (distinct < kSteps - 1) ? "  <-- plateau" : "";
            printf("  %-9s %10d %9d %8d %8d   %s%s\n",
                   pr.name, shipped, distinct, below, above, spans, mark);
        }
        printf("\n");
    }
    return 0;
}

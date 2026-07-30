/**
 * vlmod_probe.cpp — how much do VlMllRes / VlMllStf actually MOVE?
 * (host-only, throwaway)
 *
 * HW (pass 29): "VLMLLRES and VLMLLSTF are still too subtle (beside AcSnare)
 * and 'thump' is not really increased."
 *
 * param_audit's 2 s full-band RMS is blind to both knobs almost everywhere:
 * they shape a 40-100 ms transient (diluted ~50x by a 2 s window) and they
 * move energy BETWEEN bands rather than raising the total, which a
 * loudness-maximised master chain then flattens.  So this probe uses the two
 * metrics CLAUDE.md says to use: a short attack window, and band energy.
 *
 *   attack   RMS over the first 100 ms          (transient presence)
 *   thump    100-400 Hz energy, first 200 ms    (kick punch — the HW word)
 *   sub      30-90 Hz energy, first 200 ms      (boom, for the thump:sub ratio)
 *   hi       >3 kHz energy fraction             (crack / stick / sizzle)
 *
 * Each preset is rendered at knob = -100, shipped, +100 with the noise seed
 * pinned (param_audit's free-running-seed bug), and the swing is printed as
 * the max/min ratio across the three.  "live" wants >= ~1.35x on the metric
 * the knob is supposed to own.
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *            -DRUNTIME_COMMON_H_ vlmod_probe.cpp -o /tmp/vlmod && /tmp/vlmod
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0, ut_delay_read = 0, ut_voice_out = 0;
#include "synth_engine.h"

static unit_runtime_desc_t D;
static BrachettiSynth s;
static const float SR = 48000.0f;

// One-pole HP/LP cascade -> band energy.  Good enough to separate decades.
static double band_rms(const std::vector<float>& x, size_t n, float lo, float hi) {
    const float alo = 1.0f - expf(-6.2831853f * lo / SR);
    const float ahi = 1.0f - expf(-6.2831853f * hi / SR);
    float slo = 0.f, shi = 0.f;
    double acc = 0.0;
    for (size_t i = 0; i < n && i < x.size(); ++i) {
        shi += ahi * (x[i] - shi);          // low-passed at hi
        slo += alo * (shi - slo);           // low-passed at lo
        const float band = shi - slo;       // band-passed
        acc += (double)band * band;
    }
    return sqrt(acc / (double)n);
}
static double rms(const std::vector<float>& x, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n && i < x.size(); ++i) acc += (double)x[i] * x[i];
    return sqrt(acc / (double)n);
}

static std::vector<float> render(int preset, uint8_t note, int knob, int32_t val,
                                 float dur) {
    s.Init(&D);
    s.LoadPreset((uint8_t)preset);
    if (knob >= 0) s.setParameter((uint8_t)knob, val);
    for (int i = 0; i < NUM_VOICES; ++i)          // pin the seed (audit bug)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
    s.GateOn(110);
    s.GateOff();
    const int total = (int)(dur * SR);
    std::vector<float> out;
    out.reserve(total);
    float blk[256];
    for (int f = 0; f < total; f += 128) {
        memset(blk, 0, sizeof(blk));
        s.processBlock(blk, 128);
        for (int i = 0; i < 128; ++i) out.push_back(blk[i * 2]);
    }
    return out;
}

struct Row { const char* name; int idx; uint8_t note; };

static void sweep(const char* knobname, int knob, const Row* rows, int nrows) {
    printf("\n=== %s ===\n", knobname);
    printf("%-9s %8s %8s %8s   %8s %8s %8s   %s\n",
           "preset", "atk-", "atk0", "atk+", "thump-", "thump0", "thump+", "swing (atk/thump/hi)");
    for (int r = 0; r < nrows; ++r) {
        double atk[3], thump[3], hi[3];
        const int32_t vals[3] = { -100, 0, 100 };
        for (int k = 0; k < 3; ++k) {
            // value 0 is the shipped value for every preset (both columns are 0
            // everywhere except BrshSnr/RimShot/Kick2 VlMllStf) — use the real
            // shipped value for the middle point instead of a hard 0.
            std::vector<float> x;
            if (k == 1) x = render(rows[r].idx, rows[r].note, -1, 0, 0.6f);
            else        x = render(rows[r].idx, rows[r].note, knob, vals[k], 0.6f);
            const size_t n100 = (size_t)(0.100f * SR), n200 = (size_t)(0.200f * SR);
            atk[k]   = rms(x, n100);
            thump[k] = band_rms(x, n200, 100.f, 400.f);
            const double tot = rms(x, n200);
            hi[k] = tot > 1e-12 ? band_rms(x, n200, 3000.f, 16000.f) / tot : 0.0;
        }
        auto sw = [](double* v) {
            double lo = fmin(fmin(v[0], v[1]), v[2]);
            double hh = fmax(fmax(v[0], v[1]), v[2]);
            return lo > 1e-9 ? hh / lo : 0.0;
        };
        printf("%-9s %8.4f %8.4f %8.4f   %8.4f %8.4f %8.4f   %5.2fx %5.2fx %5.2fx\n",
               rows[r].name, atk[0], atk[1], atk[2],
               thump[0], thump[1], thump[2], sw(atk), sw(thump), sw(hi));
    }
}

int main() {
    D.samplerate = 48000; D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;

    static const Row rows[] = {
        { "Kick2",   0, 36 }, { "808Sub",  2, 36 }, { "KickDrum", 20, 36 },
        { "AcSnare", 3, 38 }, { "MrchSnr", 8, 65 }, { "RimShot",  39, 69 },
        { "AcTom",  12, 45 }, { "Conga",  28, 62 }, { "Djambe",    6, 48 },
        { "Timpani", 5, 52 }, { "Taiko",   7, 41 },
        { "Cymbal", 13, 65 }, { "Ride",   32, 69 },
        { "Marimba", 1, 72 }, { "Clap",   21, 60 }, { "GtrStr",   25, 69 },
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    sweep("VlMllRes (param 6)", 6, rows, n);
    sweep("VlMllStf (param 7)", 7, rows, n);
    return 0;
}

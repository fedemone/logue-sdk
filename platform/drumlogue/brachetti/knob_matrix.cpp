/**
 * knob_matrix.cpp — the evidence behind the README "which knobs are live" matrix.
 *
 * The matrix used to be re-derived from param_audit.cpp, whose verdict is a
 * relative change in total / band / tail RMS over 2 s on ONE preset per
 * family, at the two ends of each knob.  That is blind to what this unit's
 * knobs most often change — a short transient (a 40 ms thump is diluted ~50x
 * in a 2 s RMS), a shift of pitch or partials at a level the master limiter
 * holds, a member of the family the exemplar is not — and pass 48 found 23
 * cells where the fixed audit and the matrix disagreed, only 7 of them from
 * the audit's Velocity leak; regenerated with this tool, 51 of 144 changed.
 *
 * This tool renders EVERY preset, through the sequencer's own path (GateOn at
 * the preset's root, same-tick GateOff), with each matrix knob at the shipped
 * value and at four more points: both ends of the range and halfway from the
 * shipped value to each end (a knob can peak mid-travel — pass 37 measured
 * Koto's Inharm stronger at +50 than at +100).  Each render is compared with
 * the shipped one in three time windows (0-50 ms attack, 50-300 ms body,
 * 0.3-1.5 s tail) and third-octave bands from 40 Hz to 16 kHz; the score is
 * the largest energy change in any band that is not masked (within 30 dB of
 * that window's loudest band).  Bands above 16 kHz are left out on purpose:
 * Resnc at an open Cutoff puts a Q-4 peak at ~20 kHz, which any metric that
 * counts it calls "live" and almost nobody can hear.
 *
 * Output is one CSV row per (preset, knob, point); knob_matrix.py folds the
 * presets into the README's families and prints the matrix.
 *
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       knob_matrix.cpp -o /tmp/knob_matrix
 *   /tmp/knob_matrix > /tmp/km.csv && python3 knob_matrix.py /tmp/km.csv
 *
 * Every render starts from a fresh Init() + LoadPreset(), with the noise seed
 * and the voice index pinned, so two renders differ only by the knob.
 */
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0.0f, ut_delay_read = 0.0f, ut_voice_out = 0.0f;
#include "synth_engine.h"

static const int kSR = 48000;
static const int kFrames = (int)(1.5f * kSR);

// The matrix rows, minus Poly (a voice cap: one strike cannot show it).
// Ranges are header.c's; Partls is 0..4 except on the cymbals, where it is
// the bank density, 0..7 (5-7 are the ResA/ResB edit selector elsewhere).
struct Knob { const char* name; int idx; int lo; int hi; };
static const Knob kKnobs[] = {
    { "Velocity", BrachettiSynth::k_paramVelocity, -100, 100 },
    { "MlltRes",  BrachettiSynth::k_paramMlltRes,     0, 1000 },
    { "MlltStif", BrachettiSynth::k_paramMlltStif,    0,  50 },
    { "VlMllRes", BrachettiSynth::k_paramVlMllRes, -100, 100 },
    { "VlMllStf", BrachettiSynth::k_paramVlMllStf, -100, 100 },
    { "Partls",   BrachettiSynth::k_paramPartls,      0,   4 },
    { "Model",    BrachettiSynth::k_paramModel,       0,   8 },
    { "Dkay",     BrachettiSynth::k_paramDkay,        0, 200 },
    { "Mterl",    BrachettiSynth::k_paramMterl,     -10,  30 },
    { "Tone",     BrachettiSynth::k_paramTone,      -10,  30 },
    { "HitPos",   BrachettiSynth::k_paramHitPos,    -98,  98 },
    { "Rel",      BrachettiSynth::k_paramRel,         0,  20 },
    { "Inharm",   BrachettiSynth::k_paramInharm,   -100, 100 },
    { "TubRad",   BrachettiSynth::k_paramTubRad,      0,  20 },
    { "Resnc",    BrachettiSynth::k_paramResnc,      71, 400 },
};

static BrachettiSynth g_s;
static unit_runtime_desc_t g_d;

static std::vector<float> render(int preset, int knob, int value) {
    g_s.Init(&g_d);
    g_s.LoadPreset((uint8_t)preset);
    if (knob >= 0) g_s.setParameter((uint8_t)knob, value);
    for (int i = 0; i < NUM_VOICES; ++i) g_s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;
    g_s.state.next_voice_idx = 0;   // the KS jitter seed reads it; Init() does not reset it
    g_s.GateOn(100);
    g_s.GateOff();
    std::vector<float> out((size_t)kFrames);
    float buf[128];
    for (int done = 0; done < kFrames; done += 64) {
        memset(buf, 0, sizeof(buf));
        g_s.processBlock(buf, 64);
        for (int i = 0; i < 64 && done + i < kFrames; ++i) out[(size_t)(done + i)] = buf[i * 2];
    }
    return out;
}

static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const std::complex<double> wl(cos(ang), sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static const double kWin[3][2] = { { 0.0, 0.05 }, { 0.05, 0.30 }, { 0.30, 1.50 } };
static const int kBands = 26;   // third octaves, 40 Hz .. 16.3 kHz

// Energy per (window, band).
static std::vector<double> bands(const std::vector<float>& x) {
    std::vector<double> e(3 * kBands, 0.0);
    for (int w = 0; w < 3; ++w) {
        const size_t a = (size_t)(kWin[w][0] * kSR), b = (size_t)(kWin[w][1] * kSR);
        size_t n = 1;
        while (n < b - a) n <<= 1;
        std::vector<std::complex<double>> s(n);
        for (size_t i = a; i < b; ++i) s[i - a] = x[i];
        fft(s);
        for (size_t k = 1; k < n / 2; ++k) {
            const double f = (double)k * kSR / (double)n;
            if (f < 40.0) continue;
            const int band = (int)floor(3.0 * log2(f / 40.0));
            if (band >= kBands) break;
            e[(size_t)(w * kBands + band)] += std::norm(s[k]);
        }
    }
    return e;
}

static double score(const std::vector<double>& er, const std::vector<double>& ex) {
    double worst = 0.0;
    for (int w = 0; w < 3; ++w) {
        double mx = 0.0;
        for (int b = 0; b < kBands; ++b) mx = fmax(mx, er[(size_t)(w * kBands + b)]);
        const double floor_e = mx * 1e-3;
        for (int b = 0; b < kBands; ++b) {
            const double r = er[(size_t)(w * kBands + b)], y = ex[(size_t)(w * kBands + b)];
            if (r > floor_e || y > floor_e)
                worst = fmax(worst, fabs(10.0 * log10((y + 1e-30) / (r + 1e-30))));
        }
    }
    return worst;
}

int main() {
    g_d.samplerate = kSR; g_d.output_channels = 2;
    g_d.get_num_sample_banks = mock_get_num_sample_banks;
    g_d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    g_d.get_sample = mock_get_sample;
    printf("preset,name,knob,value,band_db\n");
    for (int p = 0; p < BrachettiSynth::k_NumPrograms; ++p) {
        const std::vector<double> ref = bands(render(p, -1, 0));
        g_s.Init(&g_d);
        g_s.LoadPreset((uint8_t)p);
        const bool cymbal = g_s.kPresetEngine[p] == BrachettiSynth::ENGINE_CYMBAL;
        for (const Knob& k : kKnobs) {
            g_s.Init(&g_d);
            g_s.LoadPreset((uint8_t)p);
            const int shipped = g_s.getParameterValue((uint8_t)k.idx);
            const int hi = (k.idx == BrachettiSynth::k_paramPartls && cymbal) ? 7 : k.hi;
            const int pts[4] = { k.lo, (k.lo + shipped) / 2, (shipped + hi) / 2, hi };
            for (int v : pts) {
                if (v == shipped) { printf("%d,%s,%s,%d,0.000\n", p, BrachettiSynth::getPresetName((uint8_t)p), k.name, v); continue; }
                const double s = score(ref, bands(render(p, k.idx, v)));
                printf("%d,%s,%s,%d,%.3f\n", p, BrachettiSynth::getPresetName((uint8_t)p), k.name, v, s);
            }
        }
        fflush(stdout);
    }
    return 0;
}

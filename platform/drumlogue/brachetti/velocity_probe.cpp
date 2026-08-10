/**
 * velocity_probe.cpp — map the Velocity knob (the ex-Rsntrs slot) per preset.
 *
 * Build (from the brachetti directory):
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       velocity_probe.cpp -o velocity_probe
 * Run:
 *   ./velocity_probe            # every preset
 *   ./velocity_probe 0 3 13 5   # selected presets
 *
 * Why this tool and not param_audit.cpp: the master stage is a peak-limited,
 * loudness-maximised chain, so a TOTAL-RMS metric reports "no change" for a
 * knob that reshapes a hit without raising its level (pass 30 found three
 * kick knobs that way).  So this prints, per knob position:
 *
 *   rms      — 250 ms RMS (what the limiter lets through)
 *   atk      — RMS of the first 30 ms only (the strike itself)
 *   dRMS     — RMS of the DIFFERENCE against the neutral-knob render, in dB
 *              relative to the neutral render.  This is the knobaud.cpp metric:
 *              it hears character changes that survive a constant level.
 *
 * The PRNG is pinned per render (param_audit.cpp's documented blind spot:
 * FastNoise::seed free-runs, so noise-dominated presets drift between runs).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

float ut_exciter_out = 0.0f;
float ut_delay_read  = 0.0f;
float ut_voice_out   = 0.0f;
#include "synth_engine.h"

static const int kSR = 48000;

static std::vector<float> render(int preset, int32_t vel_knob, uint8_t velocity,
                                 float seconds) {
    BrachettiSynth s;
    unit_runtime_desc_t d = {};
    d.samplerate = kSR;
    d.output_channels = 2;
    d.get_num_sample_banks     = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample               = mock_get_sample;
    s.Init(&d);
    s.LoadPreset((uint8_t)preset);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;   // pin the PRNG
    s.setParameter(BrachettiSynth::k_paramVelocity, vel_knob);

    const int n = (int)(seconds * kSR);
    std::vector<float> out((size_t)n, 0.0f);
    float buf[128];

    // Same-tick gate on+off: exactly what the drumlogue scheduler does.
    s.GateOn(velocity);
    s.GateOff();
    for (int done = 0; done < n; done += 64) {
        std::memset(buf, 0, sizeof(buf));
        s.processBlock(buf, 64);
        for (int i = 0; i < 64 && done + i < n; ++i) out[(size_t)(done + i)] = buf[i * 2];
    }
    return out;
}

static double rms(const std::vector<float>& x, int from, int to) {
    if (to > (int)x.size()) to = (int)x.size();
    double a = 0.0;
    for (int i = from; i < to; ++i) a += (double)x[i] * x[i];
    return (to > from) ? std::sqrt(a / (to - from)) : 0.0;
}

static double diff_rms_db(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    double d = 0.0, r = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double e = (double)a[i] - b[i];
        d += e * e;
        r += (double)b[i] * b[i];
    }
    if (r <= 0.0 || d <= 0.0) return -120.0;
    return 20.0 * std::log10(std::sqrt(d / n) / std::sqrt(r / n));
}

static double peak(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) { const double a = std::fabs((double)v); if (a > p) p = a; }
    return p;
}

int main(int argc, char** argv) {
    std::vector<int> presets;
    for (int i = 1; i < argc; ++i) presets.push_back(atoi(argv[i]));
    if (presets.empty())
        for (int i = 0; i < 40; ++i) presets.push_back(i);

    const int32_t knobs[] = {-100, -50, 0, 50, 100};
    const uint8_t vels[]  = {127, 64};

    printf("Velocity knob map — 250 ms window, PRNG pinned, same-tick gate.\n");
    printf("dRMS is against the SAME velocity at knob 0 (negative = quieter "
           "difference than the reference render).\n\n");

    for (int p : presets) {
        printf("── [%2d] %-9s ─────────────────────────────────────────────\n",
               p, BrachettiSynth::getPresetName((uint8_t)p));
        for (uint8_t v : vels) {
            const std::vector<float> ref = render(p, 0, v, 0.25f);
            printf("   vel %3d | %5s %10s %10s %9s %8s\n",
                   (int)v, "knob", "rms", "atk30ms", "dRMS dB", "peak");
            for (int32_t k : knobs) {
                const std::vector<float> x = (k == 0) ? ref : render(p, k, v, 0.25f);
                printf("           | %5d %10.5f %10.5f %9.1f %8.4f\n",
                       (int)k, rms(x, 0, (int)x.size()),
                       rms(x, 0, kSR * 30 / 1000),
                       (k == 0) ? -120.0 : diff_rms_db(x, ref), peak(x));
            }
        }
        printf("\n");
    }
    return 0;
}

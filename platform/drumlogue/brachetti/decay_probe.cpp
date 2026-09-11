/**
 * decay_probe.cpp — how long does a preset actually take to die?
 *
 * "Decay" on this unit is not one number in one place.  A membrane preset's
 * tail is the boom oscillator (`k_boom_decay` in model_param_presets) AND the
 * modal bank (the four `t60_*_ms` in modal_preset_configs), and the Dkay knob
 * is a REFERENCE ANCHOR on top of both — t60_scale is exactly 1 at the value
 * the preset ships, so moving the Dkay column changes where the knob sits and
 * nothing else.  That is why a decay request has to be answered by measuring
 * the rendered output rather than by reading a parameter: see the note on
 * modal_preset_configs[k_Kick2], and CLAUDE.md pass 47 for the Kick/DeepBs
 * numbers this probe was written for.
 *
 * Each preset is struck once at its OWN shipped Note through GateOn (the
 * sequencer path) at velocity 127, with no NoteOff, and the report is the time
 * from the loudest 5 ms frame down to -20/-40/-60 dB below it.  t-60 is the
 * T60 the codebase quotes elsewhere: `boom_decay` 0.99972 is documented as
 * "T60 ~ 515 ms" on k_RackTom and this probe measures RackTom at 500 ms, so
 * the two conventions agree.
 *
 * Build (from the brachetti directory):
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       decay_probe.cpp -o /tmp/decay_probe
 * Run:
 *   /tmp/decay_probe            # every preset
 *   /tmp/decay_probe 20 23 39   # just these
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0.f, ut_delay_read = 0.f, ut_voice_out = 0.f;
#include "synth_engine.h"

static const int   kSR    = 48000;
static const float kFrame = 0.005f;   // 5 ms envelope frames
static const float kDur   = 12.0f;    // longer than the longest tail (TblrBel)

static void one(int idx) {
    BrachettiSynth s;
    unit_runtime_desc_t d = {};
    d.samplerate = kSR; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    s.Init(&d); s.LoadPreset(idx);

    const int total = (int)(kDur * kSR);
    std::vector<float> o(total, 0.f);
    float st[256 * 2];
    s.GateOn(127);
    for (int f = 0; f < total; ) {
        int todo = 128; if (f + todo > total) todo = total - f;
        memset(st, 0, todo * 2 * sizeof(float));
        s.processBlock(st, (size_t)todo);
        for (int i = 0; i < todo; ++i) o[f + i] = st[i * 2];
        f += todo;
    }
    const int F = (int)(kFrame * kSR);
    std::vector<float> e;
    for (int i = 0; i + F <= total; i += F) {
        float p = 0.f;
        for (int j = 0; j < F; ++j) if (fabsf(o[i + j]) > p) p = fabsf(o[i + j]);
        e.push_back(p);
    }
    size_t pk = 0;
    for (size_t i = 0; i < e.size(); ++i) if (e[i] > e[pk]) pk = i;
    auto down = [&](double db) -> double {
        const double th = e[pk] * pow(10.0, -db / 20.0);
        for (size_t i = pk; i < e.size(); ++i) if (e[i] < th) return (i - pk) * kFrame * 1000.0;
        return -1.0;   // still above the threshold when the render ran out
    };
    printf("%-3d %-9s %9.0f %9.0f %9.0f %9.4f\n", idx,
           BrachettiSynth::getPresetName(idx), down(20), down(40), down(60), e[pk]);
}

int main(int argc, char** argv) {
    printf("%-3s %-9s %9s %9s %9s %9s\n",
           "idx", "name", "t-20 ms", "t-40 ms", "t-60 ms", "peak");
    printf("------------------------------------------------------\n");
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            const int idx = atoi(argv[i]);
            if (idx >= 0 && idx < BrachettiSynth::k_NumPrograms) one(idx);
        }
    } else {
        for (int idx = 0; idx < BrachettiSynth::k_NumPrograms; ++idx) one(idx);
    }
    return 0;
}

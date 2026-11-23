/**
 * knob_liveness.cpp — the renders behind knob_liveness.py, for ONE Brachetti
 * unit (built against it like equiv_driver.cpp).
 *
 *   stdin:  one line per knob, "<slot> <min> <max> <name>"
 *   argv:   <seconds> <out-dir>
 *
 * For every preset, on a freshly booted unit: the preset as shipped, then each
 * knob at its min and at its max (set after the preset loads), each render a
 * sequencer strike at velocity 110 and, halfway, a held MIDI note at 70.
 * Writes <out-dir>/p<preset>_base.raw and p<preset>_k<slot>_{lo,hi}.raw
 * (float32 mono) and prints the preset count.  Host tool only.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 0; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 0; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
#include "synth_engine.h"

static BrachettiSynth g_synth;   // static storage: the units are built to be constant-initialised
static int g_frames = 0;

static void boot() {
    g_synth = BrachettiSynth();
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    g_synth.Init(&d);
}

static void render(int preset, int slot, int value, const std::string& path) {
    boot();
    g_synth.LoadPreset((uint8_t)preset);
    if (slot >= 0) g_synth.setParameter((uint8_t)slot, value);
    std::vector<float> x(g_frames);
    float buf[128];
    g_synth.GateOn(110);
    g_synth.GateOff();
    for (int f = 0; f < g_frames; f += 64) {
        if (f == g_frames / 2) g_synth.NoteOn(g_synth.PlayedNote(), 70);
        if (f == g_frames / 2 + 48000 / 4) g_synth.NoteOff(g_synth.PlayedNote());
        g_synth.processBlock(buf, 64);
        for (int j = 0; j < 64; ++j) x[f + j] = buf[j * 2];
    }
    FILE* fp = fopen(path.c_str(), "wb");
    fwrite(x.data(), sizeof(float), x.size(), fp);
    fclose(fp);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <seconds> <out-dir> < knobs\n", argv[0]); return 1; }
    g_frames = (int)(atof(argv[1]) * 48000) / 64 * 64;
    const std::string dir = argv[2];
    int slot, lo, hi;
    char name[32];
    std::vector<int> slots, los, his;
    while (scanf("%d %d %d %31s", &slot, &lo, &hi, name) == 4) {
        slots.push_back(slot); los.push_back(lo); his.push_back(hi);
    }
    for (int p = 0; p < BrachettiSynth::k_NumPrograms; ++p) {
        const std::string base = dir + "/p" + std::to_string(p);
        render(p, -1, 0, base + "_base.raw");
        for (size_t k = 0; k < slots.size(); ++k) {
            render(p, slots[k], los[k], base + "_k" + std::to_string(slots[k]) + "_lo.raw");
            render(p, slots[k], his[k], base + "_k" + std::to_string(slots[k]) + "_hi.raw");
        }
    }
    printf("%d\n", BrachettiSynth::k_NumPrograms);
    return 0;
}

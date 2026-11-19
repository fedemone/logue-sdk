// strike_roll.cpp — a unit's output for repeated strikes (restrike_check.py
// builds it against one unit directory, as preset_probe.cpp is built).
//
//   strike_roll <preset> <gap_ms> <hits> <velocity> <poly> <secs> <out.f32> [note] [first_ms]
//
// Strikes <preset> <hits> times on <note> (default 60), <gap_ms> apart, the
// first at <first_ms> (default 0), with the Poly knob at <poly>, and writes
// the unit's left output (after the master stage: what the drumlogue plays)
// as raw float32, <secs> long.  A strike lands on its exact sample: the
// blocks are cut there.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../common/runtime.h"
static uint8_t no_banks() { return 0; }
static uint8_t no_samples(uint8_t) { return 0; }
static const sample_wrapper_t* no_sample(uint8_t, uint8_t) { return nullptr; }
#include "synth_engine.h"
static BrachettiSynth s_synth;
int main(int argc, char** argv) {
    if (argc < 8) { fprintf(stderr, "usage: strike_roll preset gap_ms hits vel poly secs out.f32 [note] [first_ms]\n"); return 2; }
    unit_runtime_desc_t d = {};
    d.samplerate = 48000; d.output_channels = 2;
    d.get_num_sample_banks = no_banks; d.get_num_samples_for_bank = no_samples; d.get_sample = no_sample;
    s_synth.Init(&d);
    s_synth.setParameter(0, atoi(argv[1]));   // Program
    s_synth.setParameter(2, atoi(argv[5]));   // Poly
    const int gap = (int)(atof(argv[2]) * 48.0), hits = atoi(argv[3]), vel = atoi(argv[4]);
    const int n = (int)(atof(argv[6]) * 48000);
    const int note = argc > 8 ? atoi(argv[8]) : 60;
    const int first = argc > 9 ? (int)(atof(argv[9]) * 48.0) : 0;
    std::vector<float> o(n);
    float buf[128];
    int h = 0, i = 0;
    while (i < n) {
        if (h < hits && i == first + h * gap) { s_synth.NoteOn(note, vel); ++h; }
        int blk = 64;
        if (h < hits && first + h * gap - i < blk) blk = first + h * gap - i;
        if (i + blk > n) blk = n - i;
        s_synth.processBlock(buf, blk);
        for (int j = 0; j < blk; ++j) o[i + j] = buf[2 * j];
        i += blk;
    }
    FILE* f = fopen(argv[7], "wb");
    if (!f) return 1;
    fwrite(o.data(), sizeof(float), n, f);
    fclose(f);
    return 0;
}

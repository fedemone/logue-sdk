// unit_bench.cpp — a unit's render cost through its drumlogue callbacks, for
// unit_cycles.sh (which links it with a unit's own unit.cc and header.c).
//
//   unit_bench <render|setup> <family-preset|-1> <program> <notes> <frames> [program2]
//
// Strikes <notes> stacked notes on <program> (after unit_load_preset of the
// family preset on Brachetti) and renders 1200 frames; with program2, changes
// Program while they ring and strikes <notes> again (a ring-over).  Then, in
// render mode only, renders <frames> frames: the part measured (setup mode
// stops before it, so the two runs' difference is that render).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "unit.h"
static uint8_t nb() { return 0; } static uint8_t ns(uint8_t) { return 0; }
static const sample_wrapper_t* sm(uint8_t, uint8_t) { return nullptr; }
int main(int argc, char** argv) {
    const bool rend = strcmp(argv[1], "render") == 0;
    const int fam = atoi(argv[2]), prog = atoi(argv[3]), notes = atoi(argv[4]), frames = atoi(argv[5]);
    const int prog2 = (argc > 6) ? atoi(argv[6]) : -1;
    unit_runtime_desc_t d = {}; d.target = unit_header.target; d.api = unit_header.api; d.samplerate = 48000;
    d.frames_per_buffer = 64; d.output_channels = 2; d.get_num_sample_banks = nb;
    d.get_num_samples_for_bank = ns; d.get_sample = sm;
    if (unit_init(&d)) return 2;
    if (fam >= 0) unit_load_preset((uint8_t)fam);
    float buf[128];
    unit_set_param_value(2, 4);   // Poly 4: the worst case (the shipped default is 2; no-op on the kernel)
    unit_set_param_value(0, prog);
    for (int k = 0; k < notes; ++k) unit_note_on((uint8_t)(48 + (k * 5) % 13), (uint8_t)(110 - k * 5));
    for (int f = 0; f < 1200; f += 64) unit_render(nullptr, buf, 64);
    if (prog2 >= 0) {
        unit_set_param_value(0, prog2);
        for (int k = 0; k < notes; ++k) unit_note_on((uint8_t)(48 + (k * 5) % 13), (uint8_t)(110 - k * 5));
    }
    if (rend) for (int f = 0; f < frames; f += 64) unit_render(nullptr, buf, 64);
    return 0;
}

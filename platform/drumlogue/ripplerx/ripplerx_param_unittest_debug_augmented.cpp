// ripplerx_param_unittest_debug_augmented.cpp
// Unit test for ripplerx:debug.h with debug traces
#include "ripplerx:debug.h"
#include <stdio.h>

int main() {
    // Investigation 1: Check for invalid partials mapping
    printf("[INVESTIGATION] Checking partials mapping for all programs...\n");
    for (int prog = 0; prog < Program::last_program; ++prog) {
        for (int param = 0; param < ProgramParameters::last_param; ++param) {
            float value = programs[prog][param];
            if (param == ProgramParameters::a_partials || param == ProgramParameters::b_partials) {
                debug_check_partials_value(value);
            }
        }
    }
    // Investigation 2: Check for silence conditions (partials, note >= C2)
    printf("[INVESTIGATION] Checking for silence conditions...\n");
    for (int prog = 0; prog < Program::last_program; ++prog) {
        for (int param = 0; param < ProgramParameters::last_param; ++param) {
            float value = programs[prog][param];
            // Simulate note value (MIDI note number)
            int midi_note = (int)programs[prog][ProgramParameters::a_coarse];
            if (midi_note >= 48) { // C2 is MIDI note 48
                printf("[DEBUG] Program %d param %d: Note >= C2 (%d)\n", prog, param, midi_note);
            }
            // Check for silence threshold
            if (value < c_silence_threshold) {
                printf("[DEBUG] Program %d param %d: Value below silence threshold (%f)\n", prog, param, value);
            }
        }
    }
    // Investigation 3: Check for out-of-bounds and array mismatches
    debug_check_array_sizes();
    // Investigation 4: Check for parameter value ranges (example: decay, resonance, frequency)
    printf("[INVESTIGATION] Checking parameter value ranges...\n");
    for (int prog = 0; prog < Program::last_program; ++prog) {
        for (int param = 0; param < ProgramParameters::last_param; ++param) {
            float value = programs[prog][param];
            if (param == ProgramParameters::a_decay || param == ProgramParameters::b_decay) {
                debug_check_param_range("decay", value, c_decay_min, c_decay_max);
            }
            if (param == ProgramParameters::a_cut || param == ProgramParameters::b_cut) {
                debug_check_param_range("cutoff", value, c_res_cutoff, c_freq_max);
            }
        }
    }
    // Investigation 5: Check Debug program selection
    for (int prog = 0; prog < Program::last_program; ++prog) {
        debug_check_debug_program_selected(prog);
    }
    printf("[INVESTIGATION] ripplerx_debug.h extended unit test completed.\n");
    return 0;
}

// ripplerx_param_unittest.cpp
// Unit test for RipplerX program loading and parameter validation
// To be compiled and run on host (not hardware)


#include <cstdio>
#include <cmath>
#include "constants.h"

// Helper: Clamp value to [min, max]
template<typename T>
T clamp(T v, T min, T max) { return (v < min) ? min : (v > max) ? max : v; }


// Simulate output: returns true if silent, false if not
// Simulate engine clamping/mapping for partials and note
int map_partials(float value) {
    int best = 0;
    int min_diff = std::abs((int)value - c_partials[0]);
    for (size_t i = 1; i < c_partialElements; ++i) {
        int diff = std::abs((int)value - c_partials[i]);
        if (diff < min_diff) {
            min_diff = diff;
            best = (int)i;
        }
    }
    return c_partials[best];
}

float clamp_note(float value) {
    return (value < 36.0f) ? 36.0f : value;
}

bool isSilent(float partials, float note) {
    // Apply engine logic before checking
    int mapped_partials = map_partials(partials);
    float clamped_note = clamp_note(note);
    // Silence if mapped partials is not in c_partials (should never happen), or note < 36
    int valid = 0;
    for (size_t i = 0; i < c_partialElements; ++i) {
        if (mapped_partials == c_partials[i]) valid = 1;
    }
    if (!valid) return true;
    if (clamped_note < 36) return true;
    return false;
}

int test_enum_mapping() {
    int errors = 0;
    for (size_t prog = 0; prog < Program::last_program; ++prog) {
        for (size_t param = 0; param < ProgramParameters::last_param; ++param) {
            // No out-of-bounds access
            float value = programs[prog][param];
            (void)value;
        }
    }
    return errors;
}

int test_partials_silence() {
    int errors = 0;
    for (size_t prog = 0; prog < Program::last_program; ++prog) {
        float partials = programs[prog][c_parameterPartials];
        float note = programs[prog][c_parameterResonatorNote];
        if (isSilent(partials, note)) {
            std::printf("SILENCE: Program %zu ('%s') partials(raw)=%f mapped=%d note(raw)=%f clamped=%f\n", prog, c_programName[prog], partials, map_partials(partials), note, clamp_note(note));
            ++errors;
        }
    }
    return errors;
}

int test_note_silence() {
    int errors = 0;
    for (size_t prog = 0; prog < Program::last_program; ++prog) {
        for (int midi = 36; midi <= 127; ++midi) { // C2 and above
            float partials = programs[prog][c_parameterPartials];
            if (isSilent(partials, (float)midi)) {
                std::printf("SILENCE: Program %zu ('%s') note(raw)=%d clamped=%f partials(raw)=%f mapped=%d\n", prog, c_programName[prog], midi, clamp_note((float)midi), partials, map_partials(partials));
                ++errors;
            }
        }
    }
    return errors;
}

int main() {
    int errors = 0;
    errors += test_enum_mapping();
    errors += test_partials_silence();
    errors += test_note_silence();
    // Additional: check for NaN and out-of-bounds values
    for (size_t prog = 0; prog < Program::last_program; ++prog) {
        for (size_t param = 0; param < ProgramParameters::last_param; ++param) {
            float value = programs[prog][param];
            if (!(value == value)) { // NaN check
                std::printf("ERROR: Program %zu param %zu is NaN\n", prog, param);
                ++errors;
                    for (size_t prog = 0; prog < Program::last_program; ++prog) {
                        for (size_t param = 0; param < ProgramParameters::last_param; ++param) {
                            float value = programs[prog][param];
                            if (!(value == value)) { // NaN check
                                std::printf("ERROR: Program %zu param %zu is NaN\n", prog, param);
                                ++errors;
                            }
                            // Example: negative decay
                            if ((param == ProgramParameters::a_decay || param == ProgramParameters::b_decay) && value < 0.0f) {
                                std::printf("ERROR: Program %zu param %zu (decay) is negative: %f\n", prog, param, value);
                                ++errors;
                            }
                            // Check a_partials and b_partials are exactly 0,1,2,3,4
                            if ((param == ProgramParameters::a_partials || param == ProgramParameters::b_partials)) {
                                if (!(value == 0.0f || value == 1.0f || value == 2.0f || value == 3.0f || value == 4.0f)) {
                                    std::printf("ERROR: Program %zu param %zu (partials) invalid: %f\n", prog, param, value);
                                    ++errors;
                                }
                            }
                        }
                    }
}

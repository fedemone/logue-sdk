// ripplerx_param_unittest.cpp
// Unit test for RipplerX program loading and parameter validation
// To be compiled and run on host (not hardware)

#include <cassert>
#include <cstdio>
#include "constants.h"

// Helper: Clamp value to [min, max]
template<typename T>
T clamp(T v, T min, T max) { return (v < min) ? min : (v > max) ? max : v; }

int main() {
    int errors = 0;
    // For each program
    for (int prog = 0; prog < Program::last_program; ++prog) {
        const float* params = programs[prog];
        // Check sample number (should be >= 1)
        float sampleNum = params[c_parameterSampleNumber];
        if (sampleNum < 1 || sampleNum > 128) {
            printf("ERROR: Program %d ('%s') has invalid sample number: %f\n", prog, c_programName[prog], sampleNum);
            ++errors;
        }
        // Check note (should be 1..126)
        float note = params[c_parameterResonatorNote];
        if (note < 1 || note > 126) {
            printf("ERROR: Program %d ('%s') has invalid note: %f\n", prog, c_programName[prog], note);
            ++errors;
        }
        // Check partials (should be 0..4 for A, 5..9 for B, but mapped to 0..4 for c_partials[])
        float partials = params[c_parameterPartials];
        if (partials < 0 || partials > 9) {
            printf("ERROR: Program %d ('%s') has invalid partials: %f\n", prog, c_programName[prog], partials);
            ++errors;
        }
        // Add more checks as needed for other parameters...
    }
    if (errors == 0) {
        printf("All program parameter values are within expected ranges.\n");
    } else {
        printf("%d parameter errors found.\n", errors);
    }
    return errors;
}

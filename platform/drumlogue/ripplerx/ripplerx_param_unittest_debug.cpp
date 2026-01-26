// ripplerx_param_unittest_debug.cpp
// Additional unit test for RipplerX parameter mapping and silence investigation
// ==== DEBUG PATCH BEGIN: Remove after debugging ====
#include "constants.h"
#include <stdio.h>
#include <math.h>
#include <stdio.h>

int main() {
    printf("RipplerX Parameter Mapping Debug Test\n");
    for (int prog = 0; prog < Program::last_program; ++prog) {
        float* preset = programs[prog];
        // Check a_partials and b_partials are valid indices (0-4)
        int a_partials = (int)preset[ProgramParameters::a_partials];
        int b_partials = (int)preset[ProgramParameters::b_partials];
        bool a_partials_valid = (a_partials >= 0 && a_partials < (int)c_partialElements);
        bool b_partials_valid = (b_partials >= 0 && b_partials < (int)c_partialElements);
        if (!a_partials_valid) {
            printf("Program %d (%s): INVALID a_partials index=%d\n", prog, c_programName[prog], a_partials);
        }
        if (!b_partials_valid) {
            printf("Program %d (%s): INVALID b_partials index=%d\n", prog, c_programName[prog], b_partials);
        }
        // Check a_coarse and b_coarse are within semitone offset range (-48..+48)
        float a_semitone = preset[ProgramParameters::a_coarse];
        float b_semitone = preset[ProgramParameters::b_coarse];
        if (a_semitone < -48 || a_semitone > 48) {
            float a_freq = c_midi_a4_converted * powf(c_semitoneFrequencyRatio, a_semitone);
            printf("Program %d (%s): a_coarse (semitone offset)=%g, freq=%g Hz OUT OF RANGE [-48..48]\n", prog, c_programName[prog], a_semitone, a_freq);
        }
        if (b_semitone < -48 || b_semitone > 48) {
            float b_freq = c_midi_a4_converted * powf(c_semitoneFrequencyRatio, b_semitone);
            printf("Program %d (%s): b_coarse (semitone offset)=%g, freq=%g Hz OUT OF RANGE [-48..48]\n", prog, c_programName[prog], b_semitone, b_freq);
        }
    }
    printf("Done.\n");
    return 0;
}
// ==== DEBUG PATCH END ====

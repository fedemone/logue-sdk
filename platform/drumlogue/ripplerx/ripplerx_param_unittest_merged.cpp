// ripplerx_param_unittest_merged.cpp
// Comprehensive unit test for RipplerX parameter mapping, preset propagation, and engine state validation
#include "constants.h"
#include <stdio.h>
#include <math.h>

// Always use the portable debug stub for host builds
int main() {
    int error_count = 0;
#if RX_ENGINE_AVAILABLE
    printf("[DYNAMIC TEST] Simulating parameter transitions...\n");
    RipplerX synth;
    unit_runtime_desc_t dummy_desc = {0};
    dummy_desc.samplerate = c_sampleRate;
    dummy_desc.output_channels = 2;
    dummy_desc.get_num_sample_banks = nullptr;
    dummy_desc.get_num_samples_for_bank = nullptr;
    dummy_desc.get_sample = nullptr;
    synth.Init(&dummy_desc);

    // 1. Test all model transitions
    printf("[MODEL TRANSITIONS]\n");
    for (int m1 = 0; m1 <= 3; ++m1) {
        for (int m2 = 0; m2 <= 3; ++m2) {
            if (m1 == m2) continue;
            synth.setParameter(ProgramParameters::a_model, m1);
            synth.setParameter(ProgramParameters::a_model, m2);
            if (synth.isSilent()) {
                printf("[FAIL] Model transition %d -> %d caused silence!\n", m1, m2);
                printf("    State: a_model=%d\n", synth.getParameterValue(ProgramParameters::a_model));
                ++error_count;
            }
        }
    }

    // 2. Test all partials transitions (A:32, A:64, B:64, etc.)
    printf("[PARTIALS TRANSITIONS]\n");
    int partial_indices[] = {0, 1, 2}; // Example: 0=A:32, 1=A:64, 2=B:64 (adjust as needed)
    int n_partials = sizeof(partial_indices)/sizeof(partial_indices[0]);
    for (int i = 0; i < n_partials; ++i) {
        for (int j = 0; j < n_partials; ++j) {
            if (i == j) continue;
            synth.setParameter(ProgramParameters::a_partials, partial_indices[i]);
            synth.trackPartialsTransition(partial_indices[i]);
            synth.setParameter(ProgramParameters::b_partials, partial_indices[j]);
            synth.trackPartialsTransition(partial_indices[j]);
            // Now transition back to A:64 (index 1)
            synth.setParameter(ProgramParameters::a_partials, 1);
            synth.trackPartialsTransition(1);
            if (synth.getParameterValue(ProgramParameters::a_partials) == 1 && synth.isSilent()) {
                printf("[FAIL] Partials transition %d -> %d -> 1 (A:64) caused silence!\n", partial_indices[i], partial_indices[j]);
                printf("    State: a_partials=%d, b_partials=%d\n", synth.getParameterValue(ProgramParameters::a_partials), synth.getParameterValue(ProgramParameters::b_partials));
                ++error_count;
            }
        }
    }
#endif

    printf("RipplerX Parameter Mapping & Preset Propagation Test\n");
    for (int prog = 0; prog < Program::last_program; ++prog) {
        float* preset = programs[prog];
        // 1. Check a_partials and b_partials are valid indices
        int a_partials = (int)preset[ProgramParameters::a_partials];
        int b_partials = (int)preset[ProgramParameters::b_partials];
        if (a_partials < 0 || a_partials >= (int)c_partialElements) {
            printf("ERROR: Program %d (%s): INVALID a_partials index=%d\n", prog, c_programName[prog], a_partials);
            ++error_count;
        }
        if (b_partials < 0 || b_partials >= (int)c_partialElements) {
            printf("ERROR: Program %d (%s): INVALID b_partials index=%d\n", prog, c_programName[prog], b_partials);
            ++error_count;
        }
        // 2. Check a_coarse and b_coarse are within semitone offset range
        float a_semitone = preset[ProgramParameters::a_coarse];
        float b_semitone = preset[ProgramParameters::b_coarse];
        if (a_semitone < -48 || a_semitone > 48) {
            float a_freq = c_midi_a4_converted * powf(c_semitoneFrequencyRatio, a_semitone);
            printf("ERROR: Program %d (%s): a_coarse (semitone offset)=%g, freq=%g Hz OUT OF RANGE [-48..48]\n", prog, c_programName[prog], a_semitone, a_freq);
            ++error_count;
        }
        if (b_semitone < -48 || b_semitone > 48) {
            float b_freq = c_midi_a4_converted * powf(c_semitoneFrequencyRatio, b_semitone);
            printf("ERROR: Program %d (%s): b_coarse (semitone offset)=%g, freq=%g Hz OUT OF RANGE [-48..48]\n", prog, c_programName[prog], b_semitone, b_freq);
            ++error_count;
        }
        // 3. Check for silence-inducing or invalid parameter values
        for (int param = 0; param < ProgramParameters::last_param; ++param) {
            float value = preset[param];
            if (!isfinite(value)) {
                printf("ERROR: Program %d (%s) param %d: NaN or Inf value\n", prog, c_programName[prog], param);
                ++error_count;
            }
            // Example: check for values below a silence threshold (customize as needed)
            if (value < 1e-6f && param != ProgramParameters::a_coarse && param != ProgramParameters::b_coarse) {
                printf("WARNING: Program %d (%s) param %d: Value below silence threshold (%g)\n", prog, c_programName[prog], param, value);
            }
        }
        // 4. (Optional) Simulate loading preset into engine and check state
        // This requires linking with the engine. Pseudocode:
        // RipplerX synth;
        // for (int param = 0; param < ProgramParameters::last_param; ++param)
        //     synth.setParameter(param, (int32_t)preset[param]);
        // // Now check that synth's internal state matches preset and is valid
    }
    // 5. Check for array size mismatches (if any arrays are used)
    if ((int)sizeof(c_partials)/sizeof(c_partials[0]) != (int)c_partialElements) {
        printf("ERROR: c_partials array size mismatch\n");
        ++error_count;
    }
    // Add more array size checks as needed
    printf("Test completed. Total errors: %d\n", error_count);
    if (error_count == 0) {
        printf("All parameter and preset checks passed.\n");
    }
    return error_count ? 1 : 0;
}

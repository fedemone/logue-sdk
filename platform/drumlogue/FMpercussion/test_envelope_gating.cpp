/**
 * @file test_envelope_gating.cpp
 * @brief Regression test to ensure presets emit sound and do NOT drone.
 * * Verifies that presets (specifically resonant ones) properly return to
 * absolute zero amplitude (ENV_STATE_OFF) after their envelope releases.
 * * Compile: arm-unknown-linux-gnueabihf-g++ -mfpu=neon -mfloat-abi=softfp -std=c++14 -o test_gate test_envelope_gating.cpp -lm
 * Run: ./test_gate (on drumlogue or ARM emulator)
 */

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <arm_neon.h>

#include "constants.h"
#include "fm_perc_synth.h"

// Presets to test:
// The user requested 1, 6, 8, 12.
// Assuming 0-based indexing for a 12-preset array, these are indices 0, 5, 7, 11.
// We will test exactly these indices to verify the resonant fixes.
static const uint8_t PRESETS_TO_TEST[] = {0, 5, 7, 11};
static const int NUM_TESTS = 4;

void test_preset_gating(uint8_t preset_idx) {
    printf("  Testing Preset Index %d... ", preset_idx);

    fm_perc_synth_t synth;
    fm_perc_synth_init(&synth);

    // Load the preset
    load_fm_preset(preset_idx, synth.params);

    // Force probabilities to 100% so we guarantee a trigger on all 4 lanes
    synth.params[PARAM_VOICE1_PROB] = 100;
    synth.params[PARAM_VOICE2_PROB] = 100;
    synth.params[PARAM_VOICE3_PROB] = 100;
    synth.params[PARAM_VOICE4_PROB] = 100;
    fm_perc_synth_update_params(&synth);

    // 1. Trigger all default drumlogue notes
    fm_perc_synth_note_on(&synth, 36, 127); // Kick
    fm_perc_synth_note_on(&synth, 38, 127); // Snare
    fm_perc_synth_note_on(&synth, 42, 127); // Metal
    fm_perc_synth_note_on(&synth, 45, 127); // Perc

    // Phase 1: ACTIVE PHASE
    // Process 0.5 seconds and verify that sound is actually being emitted
    float max_active_amp = 0.0f;
    for (int i = 0; i < (int)(SAMPLE_RATE * 0.5f); i++) {
        float out = fm_perc_synth_process(&synth);
        if (fabsf(out) > max_active_amp) {
            max_active_amp = fabsf(out);
        }
    }

    if (max_active_amp < 0.01f) {
        printf("FAIL (Silent)\n");
        assert(false && "Synth is completely silent during active phase!");
    }

    // Phase 2: RELEASE PHASE
    // Process another 2.5 seconds to let even the longest envelope fully release
    for (int i = 0; i < (int)(SAMPLE_RATE * 2.5f); i++) {
        fm_perc_synth_process(&synth);
    }

    // Phase 3: DRONE CHECK
    // Process 0.1 seconds of audio and ensure amplitude is absolute 0.0f
    float max_drone_amp = 0.0f;
    for (int i = 0; i < (int)(SAMPLE_RATE * 0.1f); i++) {
        float out = fm_perc_synth_process(&synth);
        if (fabsf(out) > max_drone_amp) {
            max_drone_amp = fabsf(out);
        }
    }

    // Verify NEON internal state machine reached OFF (3)
    uint32_t stage0 = vgetq_lane_u32(synth.envelope.stage, 0);
    uint32_t stage1 = vgetq_lane_u32(synth.envelope.stage, 1);
    uint32_t stage2 = vgetq_lane_u32(synth.envelope.stage, 2);
    uint32_t stage3 = vgetq_lane_u32(synth.envelope.stage, 3);

    bool states_off = (stage0 == ENV_STATE_OFF &&
                       stage1 == ENV_STATE_OFF &&
                       stage2 == ENV_STATE_OFF &&
                       stage3 == ENV_STATE_OFF);

    if (!states_off) {
        printf("FAIL (Envelope Stuck)\n");
        printf("    States: [%d, %d, %d, %d] (Expected %d)\n",
               stage0, stage1, stage2, stage3, ENV_STATE_OFF);
        assert(false && "Envelope state machine derailed!");
    }

    if (max_drone_amp > 1e-5f) {
        printf("FAIL (Drone Detected)\n");
        printf("    Residual Amp: %f\n", max_drone_amp);
        assert(false && "Drone detected! Resonant output bypassed envelope.");
    }

    printf("PASS (Max Active Amp: %.2f)\n", max_active_amp);
}

int main() {
    printf("\n==========================================================\n");
    printf(" Envelope Gating & Drone Regression Test\n");
    printf("==========================================================\n");

    for (int i = 0; i < NUM_TESTS; i++) {
        test_preset_gating(PRESETS_TO_TEST[i]);
    }

    printf("==========================================================\n");
    printf(" ALL TESTS PASSED! No drones detected.\n");
    printf("==========================================================\n\n");

    return 0;
}
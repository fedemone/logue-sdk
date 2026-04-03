/**
 *  @file perc_engine.h
 *  @brief 3-operator FM percussion/tom engine
 *
 *  Operators:
 *    Op1: Carrier
 *    Op2: Modulator 1 (ratio center)
 *    Op3: Modulator 2 (variation)
 *  Parameters:
 *    Param1: Ratio center (0-100%) - maps to 1.0-3.0
 *    Param2: Variation (0-100%) - secondary modulation amount
 */

#pragma once

#include <arm_neon.h>
#include "sine_neon.h"
#include "fm_voices.h"

#define PERC_RATIO_MIN 1.0f
#define PERC_RATIO_MAX 3.0f
#define PERC_VARIATION_MAX 1.5f

typedef struct {
    // Three operators
    float32x4_t phase[3];
    float32x4_t freq_base;     // Carrier base frequency
    float32x4_t ratio_center;   // Main modulator ratio
    float32x4_t variation;       // Secondary modulation amount

    // Parameters
    float32x4_t ratio_param;    // 0-1 mapped to ratio range
    float32x4_t var_param;      // 0-1 mapped to variation range
} perc_engine_t;

/**
 * Initialize perc engine
 */
fast_inline void perc_engine_init(perc_engine_t* perc) {
    for (int i = 0; i < 3; i++) {
        perc->phase[i] = vdupq_n_f32(0.0f);
    }

    perc->freq_base = vdupq_n_f32(200.0f);  // Mid tom default
    perc->ratio_center = vdupq_n_f32(2.0f);
    perc->variation = vdupq_n_f32(0.5f);

    perc->ratio_param = vdupq_n_f32(0.5f);
    perc->var_param = vdupq_n_f32(0.5f);
}

/**
 * Update perc engine parameters
 */
fast_inline void perc_engine_update(perc_engine_t* perc,
                                    float32x4_t param1,  // Ratio center
                                    float32x4_t param2) { // Variation
    perc->ratio_param = param1;
    perc->var_param = param2;

    // Map param1 (0-1) to ratio range (1.0-3.0)
    float32x4_t ratio_range = vdupq_n_f32(PERC_RATIO_MAX - PERC_RATIO_MIN);
    perc->ratio_center = vaddq_f32(vdupq_n_f32(PERC_RATIO_MIN),
                                   vmulq_f32(param1, ratio_range));

    // Map param2 to variation amount
    perc->variation = vmulq_f32(param2, vdupq_n_f32(PERC_VARIATION_MAX));
}

/**
 * Update perc engine just second parameter
 */
fast_inline void perc_engine_update2(perc_engine_t* perc,
                                     float32x4_t index_add,
                                     float32x4_t param2) { // Variation

    float32x4_t modded_param2 = vaddq_f32(perc->variation, index_add);
    modded_param2 = vmaxq_f32(vminq_f32(modded_param2, vdupq_n_f32(1.0f)), vdupq_n_f32(0.0f))
    perc->var_param = modded_param2;

    // Map param2 to variation amount
    perc->variation = vmulq_f32(param2, vdupq_n_f32(PERC_VARIATION_MAX));
}

/**
 * Set MIDI note (tunable percussion)
 */
fast_inline void perc_engine_set_note(perc_engine_t* perc,
                                      uint32x4_t voice_mask,
                                      float32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f/12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);

    float32x4_t two_pow = exp2_neon(exponent);

    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    perc->freq_base = vbslq_f32(voice_mask,
                                base_freq,
                                perc->freq_base);
}

/**
 * Process one sample of perc engine
 */
fast_inline float32x4_t perc_engine_process(perc_engine_t* perc,
                                            float32x4_t envelope,
                                            uint32x4_t active_mask,
                                            float32x4_t lfo_pitch_mult,
                                            float32x4_t lfo_index_add) {
    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);

    // 1. Transient Envelope for the "Thwack"
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);

    // 2. Apply LFO Pitch Modulation
    float32x4_t carrier_freq = vmulq_f32(perc->freq_base, lfo_pitch_mult);
    float32x4_t mod1_freq = vmulq_f32(carrier_freq, perc->ratio_center);

    // Mod2 shifts dynamically based on variation for a slight pitch-bend effect
    float32x4_t mod2_ratio = vaddq_f32(perc->ratio_center, vmulq_f32(perc->variation, env2));
    float32x4_t mod2_freq = vmulq_f32(carrier_freq, mod2_ratio);

    // 3. Update Phases
    float32x4_t freqs[3] = {carrier_freq, mod1_freq, mod2_freq};
    for (int i = 0; i < 3; i++) {
        perc->phase[i] = vaddq_f32(perc->phase[i], vmulq_f32(freqs[i], two_pi_over_sr));
        uint32x4_t wrap = vcgeq_f32(perc->phase[i], two_pi);
        perc->phase[i] = vbslq_f32(wrap, vsubq_f32(perc->phase[i], two_pi), perc->phase[i]);
    }

    // 4. FM Synthesis
    float32x4_t mod1 = neon_sin(perc->phase[1]);
    float32x4_t mod2 = neon_sin(perc->phase[2]);

    // Force modulators to decay rapidly (env4) so it isn't "melodic"
    float32x4_t index = vaddq_f32(perc->variation, lfo_index_add); // Base index + LFO
    float32x4_t modulation = vaddq_f32(vmulq_f32(mod1, env2), vmulq_f32(mod2, vmulq_f32(index, env4)));

    float32x4_t modulated_phase = vaddq_f32(perc->phase[0], vmulq_n_f32(modulation, 2.0f));
    float32x4_t output = neon_sin(modulated_phase);

    // Apply main envelope
    output = vmulq_f32(output, envelope);
    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}

// ========== UNIT TEST ==========
#ifdef TEST_PERC

void test_perc_ratio_range() {
    perc_engine_t perc;
    perc_engine_init(&perc);

    // Test ratio center mapping
    float32x4_t param1 = vdupq_n_f32(0.0f);  // Min ratio
    float32x4_t param2 = vdupq_n_f32(0.5f);
    perc_engine_update(&perc, param1, param2);

    float ratio = vgetq_lane_f32(perc.ratio_center, 0);
    assert(fabsf(ratio - PERC_RATIO_MIN) < 0.001f);

    param1 = vdupq_n_f32(1.0f);  // Max ratio
    perc_engine_update(&perc, param1, param2);

    ratio = vgetq_lane_f32(perc.ratio_center, 0);
    assert(fabsf(ratio - PERC_RATIO_MAX) < 0.001f);

    printf("Perc engine ratio range test PASSED\n");
}

#endif
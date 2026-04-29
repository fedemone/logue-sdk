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
#include "float_math.h"
#include "prng.h"

#define PERC_RATIO_MIN 1.0f
#define PERC_RATIO_MAX 3.0f
#define PERC_VARIATION_MAX 1.5f

typedef struct {
    // --- Exciter ---
    float32x4_t noise_state;
    float32x4_t click_phase;
    // --- Resonator (FM-based) ---
    // Three operators
    float32x4_t phase[3];           // carrier + 2 modes
    float32x4_t carrier_freq_base;  // Carrier base frequency
    float32x4_t ratio_center;       // Main modulator ratio (mod1 freq = carrier * ratio_center)
    float32x4_t variation;          // Secondary modulation: mod2 offset + transient pitch bend depth
                                    // (0-1 maps to 0-PERC_VARIATION_MAX via perc_engine_update)
    float32x4_t ratio_param;        // 0-1 stored for reference; drives ratio_center
} perc_engine_t;

/**
 * Initialize perc engine
 */
fast_inline void perc_engine_init(perc_engine_t* perc) {
    for (int i = 0; i < 3; i++) {
        perc->phase[i] = vdupq_n_f32(0.0f);
    }

    perc->carrier_freq_base = vdupq_n_f32(200.0f);  // Mid tom default
    perc->ratio_center = vdupq_n_f32(2.0f);
    perc->variation = vdupq_n_f32(0.5f);
    perc->ratio_param = vdupq_n_f32(0.5f);
    perc->click_phase = vdupq_n_f32(0.0f);
    float A[] = {63.1402,2.316,385.002,24.948}; // array with 4 elements
    perc->noise_state = vld1q_f32(A);            // load four random numbers
}

/**
 * Update perc engine parameters
 */
fast_inline void perc_engine_update(perc_engine_t* perc,
                                    float32x4_t param1,   // Ratio center (PRatio)
                                    float32x4_t param2) { // Variation (PVar)
    perc->ratio_param = param1;

    // Map param1 (0-1) to FM modulator ratio range (1.0-3.0)
    float32x4_t ratio_range = vdupq_n_f32(PERC_RATIO_MAX - PERC_RATIO_MIN);
    perc->ratio_center = vaddq_f32(vdupq_n_f32(PERC_RATIO_MIN),
                                   vmulq_f32(param1, ratio_range));

    // Map param2 (0-1) to variation depth (0-PERC_VARIATION_MAX).
    // variation controls: (a) mod2 ratio offset (static timbral shift) and
    // (b) the transient pitch-bend depth on mod2 (env^2-weighted), which adds a
    // short downward-pitched "thwack" on each hit, similar to conga body flex.
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
    float32x4_t twelfth = vdupq_n_f32(0.083333333f); // 1.0f/12.0f

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);

    float32x4_t two_pow = exp2_neon(exponent);

    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    perc->carrier_freq_base = vbslq_f32(voice_mask,
                                base_freq,
                                perc->carrier_freq_base);
}

/**
 * Process one sample of perc engine
 */
fast_inline float32x4_t perc_engine_process(perc_engine_t* perc,
                                            float32x4_t envelope,
                                            uint32x4_t active_mask,
                                            float32x4_t lfo_pitch_mult,
                                            float32x4_t lfo_index_add) {

    // APC BAILOUT: Check if all 4 voices are dead
    // Extract the max value across the 4 lanes of the mask
    #if defined(__aarch64__)
        uint32_t max_mask = vmaxvq_u32(active_mask);
    #else
        // 32-bit ARM fallback for vector max
        uint32x2_t max_half = vmax_u32(vget_low_u32(active_mask), vget_high_u32(active_mask));
        uint32_t max_mask = vget_lane_u32(vpmax_u32(max_half, max_half), 0);
    #endif

    // If the mask is zero across all lanes, SKIP THE MATH!
    if (max_mask == 0) {
        return vdupq_n_f32(0.0f);
    }


    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);

    // 1. Transient Envelope for the "Thwack"
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);

    // 2. Apply LFO Pitch Modulation
    float32x4_t carrier_freq = vmulq_f32(perc->carrier_freq_base, lfo_pitch_mult);
    float32x4_t detune = vmulq_f32(perc->variation, vdupq_n_f32(0.15f));
    float32x4_t mod1_freq = vmulq_f32(carrier_freq, vaddq_f32(perc->ratio_center, detune));

    // Mod2 shifts dynamically based on variation for a slight pitch-bend effect
    float32x4_t mod2_ratio = vaddq_f32(vmulq_n_f32(perc->ratio_center, 1.732f),  // √3 ≈ 1.732, wood / membrane feel
                                       vmulq_f32(detune, env2));
    float32x4_t mod2_freq = vmulq_f32(carrier_freq, mod2_ratio);

    // Fast downward pitch = drum-like behavior
    float32x4_t pitch_drop = vmulq_f32(env2, perc->variation);
    carrier_freq = vmulq_f32(carrier_freq,
                             vsubq_f32(vdupq_n_f32(1.0f),
                                       vmulq_n_f32(pitch_drop, 0.25f)));    // try 0.35f otherwise

    // 3. Update Phases
    float32x4_t freqs[3] = {carrier_freq, mod1_freq, mod2_freq};
    for (int i = 0; i < 3; i++) {
        perc->phase[i] = vaddq_f32(perc->phase[i], vmulq_f32(freqs[i], two_pi_over_sr));
        uint32x4_t wrap = vcgeq_f32(perc->phase[i], two_pi);
        perc->phase[i] = vbslq_f32(wrap, vsubq_f32(perc->phase[i], two_pi), perc->phase[i]);
    }

    // 4. FM Synthesis - multi mode
    float32x4_t mod1 = neon_sin(perc->phase[1]);
    float32x4_t mod2 = neon_sin(perc->phase[2]);

    // Make index transient, not static
    float32x4_t index = vmulq_f32(perc->variation, vaddq_f32(env2, env4));
    index = vaddq_f32(index, lfo_index_add); // Base index + LFO
    // Force modulators to decay rapidly (env4) so it isn't "melodic"
    float32x4_t modulation = vaddq_f32(vmulq_f32(mod1, env2),
                                       vmulq_f32(mod2, vmulq_f32(env4, index)));

    float32x4_t modulated_phase = vaddq_f32(perc->phase[0], vmulq_n_f32(modulation, 2.0f)); // try 2.5 too
    // resonator body
    float32x4_t output = neon_sin(modulated_phase);

    // Apply main envelope
    output = vmulq_f32(output, envelope);

    // Add a noise burst for impact
    float32x4_t noise = noise4(&perc->noise_state);
    float32x4_t attack_noise = vmulq_f32(noise, env4);
    // Click generator
    float32x4_t click = neon_sin(perc->click_phase);
    perc->click_phase = vaddq_f32(perc->click_phase, vdupq_n_f32(40.0f));
    float32x4_t exciter = vaddq_f32(attack_noise,
                                    vmulq_f32(click, vmulq_n_f32(env4, 0.4f)));  // try 0.7f too

    // Combine Exciter + Resonator
    output = vaddq_f32(output, exciter);

    // Soft saturation/Nonlinearity (perceived weight)
    output = fast_div_neon(output, vaddq_f32(vdupq_n_f32(1.0f), vabsq_f32(output)));

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
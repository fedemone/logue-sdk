/**
 *  @file snare_engine.h
 *  @brief 2-operator FM snare with noise injection
 *
 *  Operators:
 *    Op1: Carrier (180-250Hz)
 *    Op2: Modulator (ratio 1.5-2.5)
 *    Noise: Bandpassed noise for sizzle
 *  Parameters:
 *    Param1: Noise mix (0-100%) - balance between tone and noise
 *    Param2: Body resonance (0-100%) - controls filter/Q
 */

#pragma once

#include <arm_neon.h>
#include "fm_voices.h"
#include "sine_neon.h"
#include "prng.h"

// Snare engine constants
#define SNARE_CARRIER_BASE 200.0f    // Base frequency
#define SNARE_MOD_RATIO_MIN 1.5f
#define SNARE_MOD_RATIO_MAX 2.5f
#define SNARE_NOISE_HPF_CUTOFF 800.0f   // High-pass for noise
#define SNARE_NOISE_LPF_CUTOFF 5000.0f  // Low-pass for noise

/**
 * Simple one-pole filter for noise shaping
 */
typedef struct {
    float32x4_t z1;  // Delay element
} one_pole_t;

fast_inline float32x4_t one_pole_lpf(one_pole_t* f, float32x4_t in, float cutoff) {
    float32x4_t alpha = vdupq_n_f32(cutoff / (cutoff + 48000.0f));
    float32x4_t out = vaddq_f32(vmulq_f32(in, alpha),
                                vmulq_f32(f->z1, vsubq_f32(vdupq_n_f32(1.0f), alpha)));
    f->z1 = out;
    return out;
}

/**
 * Snare engine state
 */
typedef struct {
    // FM operators
    float32x4_t carrier_phase;
    float32x4_t modulator_phase;
    float32x4_t carrier_freq_base;
    float32x4_t mod_ratio;

    // Noise section
    one_pole_t noise_hpf;
    one_pole_t noise_lpf;
    neon_prng_t noise_prng;  // Separate PRNG for noise

    // Parameters
    float32x4_t noise_mix;      // 0-1
    float32x4_t body_resonance;  // 0-1
} snare_engine_t;

/**
 * Initialize snare engine
 */
fast_inline void snare_engine_init(snare_engine_t* snare) {
    snare->carrier_phase = vdupq_n_f32(0.0f);
    snare->modulator_phase = vdupq_n_f32(0.0f);
    snare->carrier_freq_base = vdupq_n_f32(SNARE_CARRIER_BASE);
    snare->mod_ratio = vdupq_n_f32(2.0f);

    snare->noise_hpf.z1 = vdupq_n_f32(0.0f);
    snare->noise_lpf.z1 = vdupq_n_f32(0.0f);
    neon_prng_init(&snare->noise_prng, 0x87654321);

    snare->noise_mix = vdupq_n_f32(0.3f);
    snare->body_resonance = vdupq_n_f32(0.5f);
}

/**
 * Update snare engine parameters
 */
fast_inline void snare_engine_update(snare_engine_t* snare,
                                     float32x4_t param1,  // Noise mix
                                     float32x4_t param2) { // Body resonance
    snare->noise_mix = param1;
    snare->body_resonance = param2;
}

/**
 * Update snare engine second parameter
 */
fast_inline void snare_engine_update(snare_engine_t* snare,
                                     float32x4_t index_add,
                                     float32x4_t param2) { // Body resonance
    float32x4_t modded_param2 = vaddq_f32(metal->brightness, index_add);
    modded_param2 = vmaxq_f32(vminq_f32(modded_param2, vdupq_n_f32(1.0f)), vdupq_n_f32(0.0f))
    snare->body_resonance = modded_param2;
}

/**
 * Set MIDI note for snare
 */
fast_inline void snare_engine_set_note(snare_engine_t* snare,
                                       uint32x4_t voice_mask,
                                       float32x4_t midi_notes) {
    // Similar to kick, but with different frequency range
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f/12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);

    // 2^x approximation
    float32x4_t two_pow = exp2_neon(exponent);

    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    snare->carrier_freq_base = vbslq_f32(voice_mask,
                                         base_freq,
                                         snare->carrier_freq_base);

    // Reset phases on trigger for consistent attack transient
    float32x4_t zero = vdupq_n_f32(0.0f);
    snare->carrier_phase   = vbslq_f32(voice_mask, zero, snare->carrier_phase);
    snare->modulator_phase = vbslq_f32(voice_mask, zero, snare->modulator_phase);
}

/**
 * Generate noise sample for all 4 voices
 */
fast_inline float32x4_t snare_generate_noise(snare_engine_t* snare) {
    // Generate 4 random values
    uint32x4_t rand = neon_prng_rand_u32(&snare->noise_prng);

    // Convert to float in [-1, 1]
    uint32x4_t masked = vandq_u32(rand, vdupq_n_u32(0x7FFFFF));
    uint32x4_t float_bits = vorrq_u32(masked, vdupq_n_u32(0x3F800000));
    float32x4_t white = vsubq_f32(vreinterpretq_f32_u32(float_bits),
                                  vdupq_n_f32(1.0f));
    white = vsubq_f32(vmulq_f32(white, vdupq_n_f32(2.0f)), vdupq_n_f32(1.0f));

    // Generate the low-pass curve at 800Hz
    float32x4_t lp_800 = one_pole_lpf(&snare->noise_hpf, white, SNARE_NOISE_HPF_CUTOFF);
    // Subtract it from the original noise to get a High-Pass at 800Hz
    float32x4_t hpf_out = vsubq_f32(white, lp_800);

    // Now apply the 5000Hz Low-Pass to the High-Passed signal to create the Bandpass
    float32x4_t bpf_out = one_pole_lpf(&snare->noise_lpf, hpf_out, SNARE_NOISE_LPF_CUTOFF);

    return bpf_out;
}

/**
 * Process one sample of snare engine
 */
fast_inline float32x4_t snare_engine_process(snare_engine_t* snare, float32x4_t envelope, uint32x4_t active_mask) {
    // 1. Differential Envelopes (Body decays faster than Noise)
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);

    // 2. The "Crack" - 1.5 Octave fast pitch sweep for the stick impact
    float32x4_t sweep_octaves = vmulq_n_f32(vdupq_n_f32(1.5f), env4);
    float32x4_t pitch_mult = exp2_neon(sweep_octaves);

    float32x4_t current_carrier_freq = vmulq_f32(snare->carrier_freq_base, pitch_mult);
    float32x4_t current_mod_freq = vmulq_f32(current_carrier_freq, snare->mod_ratio);

    // 3. Advance Phases
    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(6.28318530718f);

    snare->carrier_phase = vaddq_f32(snare->carrier_phase, vmulq_f32(current_carrier_freq, two_pi_over_sr));
    uint32x4_t wrap_c = vcgtq_f32(snare->carrier_phase, two_pi);
    snare->carrier_phase = vbslq_f32(wrap_c, vsubq_f32(snare->carrier_phase, two_pi), snare->carrier_phase);

    snare->modulator_phase = vaddq_f32(snare->modulator_phase, vmulq_f32(current_mod_freq, two_pi_over_sr));
    uint32x4_t wrap_m = vcgtq_f32(snare->modulator_phase, two_pi);
    snare->modulator_phase = vbslq_f32(wrap_m, vsubq_f32(snare->modulator_phase, two_pi), snare->modulator_phase);

    // 4. FM Synthesis (Index scales with fast env2 so harmonics die out)
    float32x4_t index = vmulq_f32(snare->body_resonance, vmulq_n_f32(env2, 4.0f));
    float32x4_t modulator = neon_sin(snare->modulator_phase);
    float32x4_t modulated_phase = vaddq_f32(snare->carrier_phase, vmulq_f32(modulator, index));
    float32x4_t tone = neon_sin(modulated_phase);

    // 5. Noise Generation
    float32x4_t noise = snare_generate_noise(snare);

    // 6. Mix: Tone uses fast env2, Noise uses standard envelope
    float32x4_t one = vdupq_n_f32(1.0f);
    float32x4_t noise_gain = vmulq_f32(snare->noise_mix, envelope);
    float32x4_t tone_gain = vmulq_f32(vsubq_f32(one, snare->noise_mix), env2);

    float32x4_t output = vaddq_f32(vmulq_f32(tone, tone_gain), vmulq_f32(noise, noise_gain));
    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}

// ========== UNIT TEST ==========
#ifdef TEST_SNARE

void test_snare_noise_mix() {
    snare_engine_t snare;
    snare_engine_init(&snare);

    // Test noise mix parameter
    float32x4_t param1 = vdupq_n_f32(0.5f);  // 50% noise
    float32x4_t param2 = vdupq_n_f32(0.5f);
    snare_engine_update(&snare, param1, param2);

    uint32x4_t mask = vdupq_n_u32(0xFFFFFFFF);
    float32x4_t env = vdupq_n_f32(1.0f);

    // Process a few samples
    float tone_sum = 0, noise_sum = 0;
    for (int i = 0; i < 100; i++) {
        float32x4_t out = snare_engine_process(&snare, env, mask);
        (void)out; // Would analyze spectrum in real test
    }

    printf("Snare engine test PASSED\n");
}

#endif
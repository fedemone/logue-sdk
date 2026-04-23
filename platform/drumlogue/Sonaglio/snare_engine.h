#pragma once

/**
 *  @file snare_engine.h
 *  @brief 2-operator FM snare with noise injection
 *
 *  Parameters:
 *    Param1: Attack / Energy / Brightness
 *    Param2: Body / Decay / Stability
 *
 *  Design intent:
 *  - Param1 increases crack, wire energy, and transient brightness.
 *  - Param2 increases shell/body weight and tonal support.
 */

#include <arm_neon.h>
#include "fm_voices.h"
#include "sine_neon.h"
#include "prng.h"

#define SNARE_CARRIER_BASE 200.0f
#define SNARE_NOISE_HPF_CUTOFF 800.0f
#define SNARE_NOISE_LPF_CUTOFF 5000.0f

typedef struct {
    // FM operators
    float32x4_t carrier_phase;
    float32x4_t modulator_phase;
    float32x4_t carrier_freq_base;

    // Derived internal controls
    float32x4_t attack;     // 0..1
    float32x4_t body;       // 0..1
    float32x4_t mod_ratio;  // derived from body
    float32x4_t noise_mix;  // derived from attack

    // Noise section
    one_pole_t  noise_hpf;
    one_pole_t  noise_lpf;
    neon_prng_t noise_prng;
} snare_engine_t;

/**
 * Initialize snare engine
 */
fast_inline void snare_engine_init(snare_engine_t* snare) {
    snare->carrier_phase = vdupq_n_f32(0.0f);
    snare->modulator_phase = vdupq_n_f32(0.0f);
    snare->carrier_freq_base = vdupq_n_f32(SNARE_CARRIER_BASE);

    snare->attack = vdupq_n_f32(0.5f);
    snare->body = vdupq_n_f32(0.5f);
    snare->mod_ratio = vdupq_n_f32(1.85f);
    snare->noise_mix = vdupq_n_f32(0.35f);

    snare->noise_hpf.z1 = vdupq_n_f32(0.0f);
    snare->noise_lpf.z1 = vdupq_n_f32(0.0f);

    {
        uint64_t s0[2] = { 0xDEADBEEFCAFEBABEULL, 0x0123456789ABCDEFULL };
        uint64_t s1[2] = { 0xFEDCBA9876543210ULL, 0xA5A5A5A5B4B4B4B4ULL };
        snare->noise_prng.state0 = vld1q_u64(s0);
        snare->noise_prng.state1 = vld1q_u64(s1);
    }
}

/**
 * Update snare engine parameters from UI
 */
fast_inline void snare_engine_update(snare_engine_t* snare,
                                     float32x4_t param1,  // Attack / Energy / Brightness
                                     float32x4_t param2) { // Body / Decay / Stability
    snare->attack = param1;
    snare->body = param2;

    // Attack increases crack/noise emphasis.
    snare->noise_mix = vaddq_f32(vdupq_n_f32(0.15f),
                                 vmulq_f32(param1, vdupq_n_f32(0.85f)));

    // Body shifts the shell resonance / tonal weight.
    snare->mod_ratio = vaddq_f32(vdupq_n_f32(1.35f),
                                 vmulq_f32(param2, vdupq_n_f32(0.95f)));
}

/**
 * Set MIDI note for snare
 */
fast_inline void snare_engine_set_note(snare_engine_t* snare,
                                       uint32x4_t voice_mask,
                                       float32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f / 12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);
    float32x4_t two_pow = exp2_neon(exponent);
    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    snare->carrier_freq_base = vbslq_f32(voice_mask,
                                         base_freq,
                                         snare->carrier_freq_base);

    // Reset phases for a consistent crack.
    float32x4_t zero = vdupq_n_f32(0.0f);
    snare->carrier_phase = vbslq_f32(voice_mask, zero, snare->carrier_phase);
    snare->modulator_phase = vbslq_f32(voice_mask, zero, snare->modulator_phase);
}

/**
 * Generate bandpassed noise sample for all 4 voices
 */
fast_inline float32x4_t snare_generate_noise(snare_engine_t* snare) {
    
    float32x4_t white = white_noise(&snare->noise_prng);

    float32x4_t lp_800 = one_pole_lpf(&snare->noise_hpf, white, SNARE_NOISE_HPF_CUTOFF);
    float32x4_t hpf_out = vsubq_f32(white, lp_800);
    float32x4_t bpf_out = one_pole_lpf(&snare->noise_lpf, hpf_out, SNARE_NOISE_LPF_CUTOFF);

    return bpf_out;
}

/**
 * Process one sample of snare engine
 */
fast_inline float32x4_t snare_engine_process(snare_engine_t* snare,
                                             float32x4_t envelope,
                                             uint32x4_t active_mask,
                                             float32x4_t lfo_pitch_mult,
                                             float32x4_t lfo_index_add,
                                             float32x4_t noise_add) {
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);
    float32x4_t env8 = vmulq_f32(env4, env4);

    // Slight pitch lift at the start helps the crack feel more physical.
    float32x4_t pitch_mult = exp2_neon(vmulq_f32(env4,
                                                 vaddq_f32(vdupq_n_f32(0.08f),
                                                           vmulq_f32(snare->body, vdupq_n_f32(0.22f)))));

    float32x4_t carrier_freq = vmulq_f32(snare->carrier_freq_base, lfo_pitch_mult);
    carrier_freq = vmulq_f32(carrier_freq, pitch_mult);

    float32x4_t mod_freq = vmulq_f32(carrier_freq, snare->mod_ratio);

    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    snare->carrier_phase = vaddq_f32(snare->carrier_phase, vmulq_f32(carrier_freq, two_pi_over_sr));
    snare->modulator_phase = vaddq_f32(snare->modulator_phase, vmulq_f32(mod_freq, two_pi_over_sr));

    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);
    uint32x4_t c_wrap = vcgeq_f32(snare->carrier_phase, two_pi);
    uint32x4_t m_wrap = vcgeq_f32(snare->modulator_phase, two_pi);
    snare->carrier_phase = vbslq_f32(c_wrap,
                                     vsubq_f32(snare->carrier_phase, two_pi),
                                     snare->carrier_phase);
    snare->modulator_phase = vbslq_f32(m_wrap,
                                       vsubq_f32(snare->modulator_phase, two_pi),
                                       snare->modulator_phase);

    // Attack makes the crack harder; body makes the shell larger.
    float32x4_t body_index = vaddq_f32(vdupq_n_f32(0.35f),
                                       vmulq_f32(snare->body, vdupq_n_f32(0.95f)));

    float32x4_t crack_index = vmulq_f32(env8,
                                        vaddq_f32(vdupq_n_f32(1.25f),
                                                  vmulq_f32(snare->attack, vdupq_n_f32(2.75f))));

    float32x4_t index = vaddq_f32(vmulq_f32(envelope, body_index), crack_index);
    index = vaddq_f32(index, lfo_index_add);

    float32x4_t modulator = neon_sin(snare->modulator_phase);
    float32x4_t modulated_phase = vaddq_f32(snare->carrier_phase,
                                             vmulq_f32(modulator, index));

    float32x4_t tone = neon_sin(modulated_phase);

    // Noise is driven by attack; body keeps more shell underneath.
    float32x4_t mix = vaddq_f32(snare->noise_mix, noise_add);
    mix = vminq_f32(vmaxq_f32(mix, vdupq_n_f32(0.0f)), vdupq_n_f32(1.0f));

    float32x4_t noise = snare_generate_noise(snare);

    float32x4_t tone_gain = vmulq_f32(envelope,
                                      vaddq_f32(vdupq_n_f32(0.30f),
                                                vmulq_f32(snare->body, vdupq_n_f32(0.70f))));
    float32x4_t noise_gain = vmulq_f32(envelope,
                                       vaddq_f32(vdupq_n_f32(0.25f),
                                                 vmulq_f32(mix, vdupq_n_f32(0.75f))));
    noise_gain = vmulq_f32(noise_gain,
                           vaddq_f32(vdupq_n_f32(0.35f),
                                     vmulq_f32(snare->attack, vdupq_n_f32(0.65f))));

    float32x4_t click = vmulq_f32(noise, vmulq_f32(env8, vdupq_n_f32(1.5f)));

    float32x4_t output = vaddq_f32(vmulq_f32(tone, tone_gain),
                                   vaddq_f32(vmulq_f32(noise, noise_gain), click));

    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}
#pragma once

/**
 *  @file perc_engine.h
 *  @brief 3-operator FM percussion engine
 *
 *  Parameters:
 *    Param1: Attack / Energy / Brightness
 *    Param2: Body / Decay / Stability
 *
 *  Design intent:
 *  - Param1 makes the hit sharper, brighter, and more forward.
 *  - Param2 makes the sound fuller, longer, and more stable.
 *
 *  This engine is the most flexible FM percussion voice:
 *  it can move between block / tom / conga-ish / digital hit territory.
 */

#include <arm_neon.h>
#include "sine_neon.h"
#include "fm_voices.h"

#define PERC_CARRIER_BASE 200.0f
#define PERC_RATIO_MIN 1.0f
#define PERC_RATIO_MAX 3.0f

typedef struct {
    // Three operators
    float32x4_t phase[3];

    // Base frequency
    float32x4_t carrier_freq_base;

    // Reinterpreted controls
    float32x4_t attack;        // Param1
    float32x4_t body;          // Param2
    float32x4_t ratio_center;  // derived from body
    float32x4_t variation;     // derived from attack/body
} perc_engine_t;

/**
 * Initialize perc engine
 */
fast_inline void perc_engine_init(perc_engine_t* perc) {
    for (int i = 0; i < 3; i++) {
        perc->phase[i] = vdupq_n_f32(0.0f);
    }

    perc->carrier_freq_base = vdupq_n_f32(PERC_CARRIER_BASE);
    perc->attack = vdupq_n_f32(0.5f);
    perc->body = vdupq_n_f32(0.5f);
    perc->ratio_center = vdupq_n_f32(2.0f);
    perc->variation = vdupq_n_f32(0.5f);
}

/**
 * Update perc engine parameters from UI
 */
fast_inline void perc_engine_update(perc_engine_t* perc,
                                    float32x4_t param1,  // Attack / Energy / Brightness
                                    float32x4_t param2) { // Body / Decay / Stability
    perc->attack = param1;
    perc->body = param2;

    // Body shifts the main ratio toward a more stable / resonant center.
    perc->ratio_center = vaddq_f32(vdupq_n_f32(PERC_RATIO_MIN),
                                   vmulq_f32(param2, vdupq_n_f32(PERC_RATIO_MAX - PERC_RATIO_MIN)));

    // Variation increases with attack, but body keeps it under control.
    // This gives sharper hits without collapsing into pure noise.
    perc->variation = vaddq_f32(vmulq_n_f32(param1, 1.00f),
                                vmulq_n_f32(param2, 0.50f));
}

/**
 * Set MIDI note (tunable percussion)
 */
fast_inline void perc_engine_set_note(perc_engine_t* perc,
                                      uint32x4_t voice_mask,
                                      float32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f / 12.0f);

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
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);
    float32x4_t env8 = vmulq_f32(env4, env4);

    // Attack controls how much the hit starts with a hard transient.
    // Body controls how much stable resonance remains after the strike.
    float32x4_t pitch_lift = vaddq_f32(vdupq_n_f32(0.05f),
                                       vmulq_f32(perc->attack, vdupq_n_f32(0.25f)));

    // Slight pitch lift at attack gives the hit more "strike" and less static tone.
    float32x4_t pitch_mult = exp2_neon(vmulq_f32(env4, pitch_lift));

    float32x4_t carrier_freq = vmulq_f32(perc->carrier_freq_base, lfo_pitch_mult);
    carrier_freq = vmulq_f32(carrier_freq, pitch_mult);

    // Modulator frequencies.
    // Mod1 follows the body / center ratio.
    // Mod2 adds controlled variation driven by attack and reduced by body.
    float32x4_t mod1_freq = vmulq_f32(carrier_freq, perc->ratio_center);

    float32x4_t mod2_ratio = vaddq_f32(perc->ratio_center,
                                       vmulq_f32(perc->variation, vdupq_n_f32(0.35f)));
    mod2_ratio = vaddq_f32(mod2_ratio,
                           vmulq_f32(env2, vdupq_n_f32(0.20f)));

    float32x4_t mod2_freq = vmulq_f32(carrier_freq, mod2_ratio);

    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    perc->phase[0] = vaddq_f32(perc->phase[0], vmulq_f32(carrier_freq, two_pi_over_sr));
    perc->phase[1] = vaddq_f32(perc->phase[1], vmulq_f32(mod1_freq, two_pi_over_sr));
    perc->phase[2] = vaddq_f32(perc->phase[2], vmulq_f32(mod2_freq, two_pi_over_sr));

    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);
    for (int i = 0; i < 3; i++) {
        uint32x4_t wrap = vcgeq_f32(perc->phase[i], two_pi);
        perc->phase[i] = vbslq_f32(wrap,
                                   vsubq_f32(perc->phase[i], two_pi),
                                   perc->phase[i]);
    }

    // FM stack:
    // Op2 and Op3 create the evolving body and strike complexity.
    float32x4_t mod1 = neon_sin(perc->phase[1]);
    float32x4_t mod2 = neon_sin(perc->phase[2]);

    // Attack controls the initial complexity.
    // Body controls the stable part of the stack.
    float32x4_t attack_index = vaddq_f32(vdupq_n_f32(0.30f),
                                         vmulq_f32(perc->attack, vdupq_n_f32(1.40f)));
    float32x4_t body_index = vaddq_f32(vdupq_n_f32(0.15f),
                                       vmulq_f32(perc->body, vdupq_n_f32(0.95f)));

    float32x4_t attack_part = vmulq_f32(mod2, vmulq_f32(env8, attack_index));
    float32x4_t body_part = vmulq_f32(mod1, vmulq_f32(envelope, body_index));

    float32x4_t modulation = vaddq_f32(body_part, attack_part);
    modulation = vaddq_f32(modulation, lfo_index_add);

    float32x4_t modulated_phase = vaddq_f32(perc->phase[0], vmulq_n_f32(modulation, 2.0f));
    float32x4_t output = neon_sin(modulated_phase);

    // Gain: body keeps the hit present, attack keeps it short and articulate.
    float32x4_t gain = vaddq_f32(vdupq_n_f32(0.45f),
                                 vmulq_f32(perc->body, vdupq_n_f32(0.55f)));

    output = vmulq_f32(output, vmulq_f32(envelope, gain));

    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}
#pragma once

/**
 *  @file kick_engine.h
 *  @brief 2-operator FM kick drum with attack/body controls
 *
 *  Parameters:
 *    Param1: Attack / Energy / Brightness
 *    Param2: Body / Decay / Stability
 *
 *  Design intent:
 *  - Param1 makes the hit harder, brighter, and more front-loaded.
 *  - Param2 makes the kick heavier, longer, and more stable.
 */

#include <arm_neon.h>
#include "fm_voices.h"
#include "sine_neon.h"
#include "envelope_rom.h"

// Kick engine constants
#define KICK_CARRIER_BASE 60.0f
#define KICK_SWEEP_OCTAVES 3.0f

typedef struct {
    float32x4_t carrier_phase;
    float32x4_t modulator_phase;
    float32x4_t carrier_freq_base;

    // Derived internal controls
    float32x4_t attack;     // 0..1
    float32x4_t body;       // 0..1
    float32x4_t mod_ratio;  // derived from body
} kick_engine_t;

/**
 * Initialize kick engine
 */
fast_inline void kick_engine_init(kick_engine_t* kick) {
    kick->carrier_phase = vdupq_n_f32(0.0f);
    kick->modulator_phase = vdupq_n_f32(0.0f);
    kick->carrier_freq_base = vdupq_n_f32(KICK_CARRIER_BASE);

    kick->attack = vdupq_n_f32(0.5f);
    kick->body = vdupq_n_f32(0.5f);
    kick->mod_ratio = vdupq_n_f32(2.0f);
}

/**
 * Update kick engine parameters from UI
 */
fast_inline void kick_engine_update(kick_engine_t* kick,
                                    float32x4_t param1,  // Attack / Energy / Brightness
                                    float32x4_t param2) { // Body / Decay / Stability
    kick->attack = param1;
    kick->body = param2;

    // Body shifts the tone from tighter/cleaner to denser/more complex.
    // Range: ~1.15 .. 3.00
    kick->mod_ratio = vaddq_f32(vdupq_n_f32(1.15f),
                                vmulq_n_f32(param2, 1.85f));
}

/**
 * Set MIDI note for kick
 */
fast_inline void kick_engine_set_note(kick_engine_t* kick,
                                      uint32x4_t voice_mask,
                                      float32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f / 12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);
    float32x4_t two_pow = exp2_neon(exponent);
    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    kick->carrier_freq_base = vbslq_f32(voice_mask,
                                        base_freq,
                                        kick->carrier_freq_base);

    // Reset phases for a consistent front edge.
    float32x4_t zero = vdupq_n_f32(0.0f);
    kick->carrier_phase = vbslq_f32(voice_mask, zero, kick->carrier_phase);
    kick->modulator_phase = vbslq_f32(voice_mask, zero, kick->modulator_phase);
}

/**
 * Process one sample of kick engine
 */
fast_inline float32x4_t kick_engine_process(kick_engine_t* kick,
                                            float32x4_t envelope,
                                            uint32x4_t active_mask,
                                            float32x4_t lfo_pitch_mult,
                                            float32x4_t lfo_index_add) {
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);
    float32x4_t env8 = vmulq_f32(env4, env4);

    // Attack controls how hard the sweep starts.
    // Body controls how much longer the lower body remains present.
    float32x4_t sweep_octaves = vmulq_f32(env4,
                                          vaddq_f32(vdupq_n_f32(0.35f),
                                                    vmulq_f32(kick->attack, vdupq_n_f32(2.65f))));
    float32x4_t pitch_mult = exp2_neon(sweep_octaves);

    float32x4_t carrier_freq = vmulq_f32(kick->carrier_freq_base, lfo_pitch_mult);
    carrier_freq = vmulq_f32(carrier_freq, pitch_mult);

    float32x4_t mod_freq = vmulq_f32(carrier_freq, kick->mod_ratio);

    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    kick->carrier_phase = vaddq_f32(kick->carrier_phase, vmulq_f32(carrier_freq, two_pi_over_sr));
    kick->modulator_phase = vaddq_f32(kick->modulator_phase, vmulq_f32(mod_freq, two_pi_over_sr));

    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);
    uint32x4_t c_wrap = vcgeq_f32(kick->carrier_phase, two_pi);
    uint32x4_t m_wrap = vcgeq_f32(kick->modulator_phase, two_pi);
    kick->carrier_phase = vbslq_f32(c_wrap,
                                    vsubq_f32(kick->carrier_phase, two_pi),
                                    kick->carrier_phase);
    kick->modulator_phase = vbslq_f32(m_wrap,
                                      vsubq_f32(kick->modulator_phase, two_pi),
                                      kick->modulator_phase);

    // Index: front-loaded click + body-dependent sustain.
    float32x4_t body_index = vaddq_f32(vdupq_n_f32(0.40f),
                                       vmulq_f32(kick->body, vdupq_n_f32(1.05f)));
    float32x4_t click_index = vmulq_f32(env8,
                                        vaddq_f32(vdupq_n_f32(0.75f),
                                                  vmulq_f32(kick->attack, vdupq_n_f32(2.25f))));
    float32x4_t index = vaddq_f32(vmulq_f32(envelope, body_index), click_index);
    index = vaddq_f32(index, lfo_index_add);

    float32x4_t modulator = neon_sin(kick->modulator_phase);
    float32x4_t modulated_phase = vaddq_f32(kick->carrier_phase,
                                             vmulq_f32(modulator, index));

    float32x4_t output = neon_sin(modulated_phase);

    // Body keeps the kick fuller; attack keeps the front edge harder.
    float32x4_t gain = vaddq_f32(vdupq_n_f32(0.55f),
                                 vmulq_f32(kick->body, vdupq_n_f32(0.45f)));
    output = vmulq_f32(output, vmulq_f32(envelope, gain));

    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}
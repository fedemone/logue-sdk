/**
 *  @file metal_engine.h
 *  @brief 4-operator FM metal/cymbal engine
 *
 *  Operators: All modulating each other in a cascaded cluster (Op4→Op3→Op2→Op1)
 *  Character 0 (Cymbal/DX7): ratios 1.0 / 1.483 / 1.932 / 2.546
 *  Character 1 (Gong):       ratios 1.0 / 2.756 / 3.752 / 5.404
 *  Character selected by EnvShape parameter bit 7 (0=Cymbal, 1=Gong).
 *  Parameters:
 *    Param1: Inharmonicity (0-100%) — spreads ratios from base; 0=all unison
 *    Param2: Brightness (0-100%)   — harmonic blend (Op2/3/4 contribution)
 */

#pragma once

#include <arm_neon.h>
#include "fm_voices.h"
#include "sine_neon.h"

// Character 0 — DX7-style cymbal (hi-hat/crash FM classic)
#define METAL_RATIO1 1.0f
#define METAL_RATIO2 1.483f
#define METAL_RATIO3 1.932f
#define METAL_RATIO4 2.546f

// Character 1 — Gong/tam-tam (widely-spaced inharmonic partials)
#define METAL_GONG_RATIO1 1.0f
#define METAL_GONG_RATIO2 2.756f
#define METAL_GONG_RATIO3 3.752f
#define METAL_GONG_RATIO4 5.404f

// Ratio spread range for inharmonicity parameter
#define METAL_SPREAD_MIN 1.0f
#define METAL_SPREAD_MAX 2.0f

typedef struct {
    // Four operators
    float32x4_t phase[4];
    float32x4_t base_ratio[4];    // Fixed ratios
    float32x4_t current_ratio[4]; // Modulated by inharmonicity

    // Carrier frequency (all derived from voice 0's carrier)
    float32x4_t carrier_freq_base;

    // Parameters
    float32x4_t inharmonicity;   // 0-1
    float32x4_t brightness;      // 0-1

    // Character variant (set from EnvShape bit 7)
    // 0 = Cymbal/DX7 (default), 1 = Gong/tam-tam
    uint32_t char_select;
} metal_engine_t;

/**
 * Initialize metal engine
 */
fast_inline void metal_engine_init(metal_engine_t* metal) {
    for (int i = 0; i < 4; i++) {
        metal->phase[i] = vdupq_n_f32(0.0f);
    }

    // Set base ratios
    metal->base_ratio[0] = vdupq_n_f32(METAL_RATIO1);
    metal->base_ratio[1] = vdupq_n_f32(METAL_RATIO2);
    metal->base_ratio[2] = vdupq_n_f32(METAL_RATIO3);
    metal->base_ratio[3] = vdupq_n_f32(METAL_RATIO4);

    for (int i = 0; i < 4; i++) {
        metal->current_ratio[i] = metal->base_ratio[i];
    }

    metal->carrier_freq_base = vdupq_n_f32(1000.0f);  // Mid-range default
    metal->inharmonicity = vdupq_n_f32(0.5f);
    metal->brightness = vdupq_n_f32(0.5f);
    metal->char_select = 0;
}

/**
 * Select metal engine character (ratio set).
 * Called when EnvShape parameter changes (bit 7 = character).
 *   0 = Cymbal / DX7-style  (EnvShape 0-127)
 *   1 = Gong  / tam-tam     (EnvShape 128-255)
 */
fast_inline void metal_engine_set_character(metal_engine_t* metal, uint32_t character) {
    if (character == metal->char_select) return;
    metal->char_select = character;
    if (character == 0) {
        metal->base_ratio[0] = vdupq_n_f32(METAL_RATIO1);
        metal->base_ratio[1] = vdupq_n_f32(METAL_RATIO2);
        metal->base_ratio[2] = vdupq_n_f32(METAL_RATIO3);
        metal->base_ratio[3] = vdupq_n_f32(METAL_RATIO4);
    } else {
        metal->base_ratio[0] = vdupq_n_f32(METAL_GONG_RATIO1);
        metal->base_ratio[1] = vdupq_n_f32(METAL_GONG_RATIO2);
        metal->base_ratio[2] = vdupq_n_f32(METAL_GONG_RATIO3);
        metal->base_ratio[3] = vdupq_n_f32(METAL_GONG_RATIO4);
    }
    // Reset current_ratio so engine_update picks up new base on next call
    for (int i = 0; i < 4; i++) {
        metal->current_ratio[i] = metal->base_ratio[i];
    }
}

/**
 * Update metal engine parameters
 */
fast_inline void metal_engine_update(metal_engine_t* metal,
                                     float32x4_t param1,  // Inharmonicity
                                     float32x4_t param2) { // Brightness
    metal->inharmonicity = param1;
    metal->brightness = param2;

    // Spread ratios based on inharmonicity
    // param1=0 -> all ratios = 1.0 (harmonic)
    // param1=1 -> ratios spread to max
    float32x4_t one = vdupq_n_f32(1.0f);
    float32x4_t spread_range = vdupq_n_f32(METAL_SPREAD_MAX - METAL_SPREAD_MIN);

    for (int i = 0; i < 4; i++) {
        // ratio = 1.0 + (base_ratio - 1.0) * inharmonicity * spread
        float32x4_t ratio_offset = vsubq_f32(metal->base_ratio[i], one);
        float32x4_t spread_factor = vmulq_f32(metal->inharmonicity, spread_range);
        metal->current_ratio[i] = vaddq_f32(one,
                                           vmulq_f32(ratio_offset, spread_factor));
    }
}

/**
 * Set MIDI note (affects all operators proportionally)
 */
fast_inline void metal_engine_set_note(metal_engine_t* metal,
                                       uint32x4_t voice_mask,
                                       float32x4_t midi_notes) {
    // Metal typically follows pitch roughly, but inharmonic
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f/12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);

    float32x4_t two_pow = exp2_neon(exponent);

    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    metal->carrier_freq_base = vbslq_f32(voice_mask,
                                         base_freq,
                                         metal->carrier_freq_base);
}

/**
 * Process one sample of metal engine. All 4 operators modulate each other.
 * Instead of mixing the operators together, this algorithm feeds them into each other
 * in a chaotic cascade. Crucially, it applies staggered, progressively faster envelopes
 * to the higher modulators so the initial metallic "clash" naturally decays into a smooth,
 * ringing fundamental.
 */
fast_inline float32x4_t metal_engine_process(metal_engine_t* metal,
                                             float32x4_t envelope,
                                             uint32x4_t active_mask,
                                             float32x4_t lfo_pitch_mult,
                                             float32x4_t lfo_index_add,
                                             float32x4_t brightness_add) {
    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);

    // 1. Apply LFO Pitch Modulation
    float32x4_t base_freq = vmulq_f32(metal->carrier_freq_base, lfo_pitch_mult);

    // 2. Advance all 4 phases
    for (int i = 0; i < NEON_LANES; i++) {
        float32x4_t freq = vmulq_f32(base_freq, metal->current_ratio[i]);
        metal->phase[i] = vaddq_f32(metal->phase[i], vmulq_f32(freq, two_pi_over_sr));
        uint32x4_t wrap = vcgeq_f32(metal->phase[i], two_pi);
        metal->phase[i] = vbslq_f32(wrap, vsubq_f32(metal->phase[i], two_pi), metal->phase[i]);
    }

    // 3. Staggered Envelopes for Acoustic Dissipation
    float32x4_t env2 = vmulq_f32(envelope, envelope);
    float32x4_t env4 = vmulq_f32(env2, env2);

    // 4. Cascaded FM Synthesis
    float32x4_t base_index = vaddq_f32(vaddq_f32(metal->brightness, lfo_index_add), brightness_add);
    float32x4_t index_high = vmulq_n_f32(base_index, 3.0f);

    // Op4 -> Modulates Op3 (Dies very fast - env4)
    float32x4_t op4 = neon_sin(metal->phase[3]);
    float32x4_t phase3_mod = vaddq_f32(metal->phase[2], vmulq_f32(op4, vmulq_f32(index_high, env4)));
    float32x4_t op3 = neon_sin(phase3_mod);

    // Op3 -> Modulates Op2 (Dies moderately fast - env2)
    float32x4_t phase2_mod = vaddq_f32(metal->phase[1], vmulq_f32(op3, vmulq_f32(base_index, env2)));
    float32x4_t op2 = neon_sin(phase2_mod);

    // Op2 -> Modulates Op1 (Sustains with normal envelope)
    float32x4_t phase1_mod = vaddq_f32(metal->phase[0], vmulq_f32(op2, vmulq_f32(base_index, envelope)));
    float32x4_t op1 = neon_sin(phase1_mod);

    // Mix all operators for metallic/cymbal character.
    // Adding inharmonic partials from Op2/3/4 directly to the output creates
    // the spectral density needed for metallic timbre; brightness controls blend.
    // Weights (0.5, 0.3, 0.15) are chosen so peak amplitude ≈ 2x at brightness=1,
    // acceptable for transient-heavy percussion.
    float32x4_t bright_w2 = vmulq_f32(metal->brightness, vdupq_n_f32(0.5f));
    float32x4_t bright_w3 = vmulq_f32(metal->brightness, vdupq_n_f32(0.3f));
    float32x4_t bright_w4 = vmulq_f32(metal->brightness, vdupq_n_f32(0.15f));

    float32x4_t harmonics = vaddq_f32(
        vaddq_f32(vmulq_f32(op2, bright_w2), vmulq_f32(op3, bright_w3)),
        vmulq_f32(op4, bright_w4)
    );
    float32x4_t output = vmulq_f32(vaddq_f32(op1, harmonics), envelope);
    return vbslq_f32(active_mask, output, vdupq_n_f32(0.0f));
}

// ========== UNIT TEST ==========
#ifdef TEST_METAL

void test_metal_inharmonicity() {
    metal_engine_t metal;
    metal_engine_init(&metal);

    // Test with harmonic setting (inharmonicity = 0)
    float32x4_t param1 = vdupq_n_f32(0.0f);
    float32x4_t param2 = vdupq_n_f32(0.5f);
    metal_engine_update(&metal, param1, param2);

    // All ratios should be 1.0
    for (int i = 0; i < 4; i++) {
        float ratio = vgetq_lane_f32(metal.current_ratio[i], 0);
        assert(fabsf(ratio - 1.0f) < 0.001f);
    }

    // Test with full inharmonicity
    param1 = vdupq_n_f32(1.0f);
    metal_engine_update(&metal, param1, param2);

    // Ratios should spread
    float r1 = vgetq_lane_f32(metal.current_ratio[0], 0);
    float r2 = vgetq_lane_f32(metal.current_ratio[3], 0);
    assert(r2 > r1);

    printf("Metal engine inharmonicity test PASSED\n");
}

#endif
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
#include "fm_voices.h"   // one_pole_t, one_pole_lpf_a
#include "sine_neon.h"
#include "prng.h"

// Precomputed one-pole HPF alpha for cymbal noise layer @ 3 kHz
// alpha = 2*pi*3000 / (2*pi*3000 + 48000) = 18849.56 / 66849.56
#define METAL_NOISE_HP_A 0.2820f

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

    // Cymbal noise layer (TR-909 inspired: same oscillator bank + separate noise VCA)
    // White noise HP-filtered @ 3 kHz to produce cymbal "hiss" texture.
    // Amplitude is scaled by brightness so noise appears only when harmonics are requested.
    neon_prng_t noise_prng;  // Independent PRNG per voice for uncorrelated noise
    one_pole_t  noise_hpf;   // HP filter state for noise shaping
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

    // Noise layer: different seeds from snare to prevent inter-engine correlation
    {
        uint64_t s0[2] = { 0xCAFEBABE12345678ULL, 0xFEDCBA0987654321ULL };
        uint64_t s1[2] = { 0x0F0F0F0F0F0F0F0FULL, 0xA1B2C3D4E5F60718ULL };
        metal->noise_prng.state0 = vld1q_u64(s0);
        metal->noise_prng.state1 = vld1q_u64(s1);
    }
    metal->noise_hpf.z1 = vdupq_n_f32(0.0f);
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
 * Applies staggered envelopes to the higher modulators so the initial metallic
 * "clash" naturally decays into a smooth ringing fundamental.
 *
 * TR-909 hi-hat inspiration: the 909 used the same oscillator bank for both
 * open and closed hi-hat, switching only the VCA decay time. We implement
 * this via metal_gate: a per-voice amplitude multiplier in [0,1] that the
 * caller drives from the LFO_TARGET_METAL_GATE ramp. High LFO rate → fast
 * gate close → closed hi-hat; low rate → open hi-hat.
 *
 * Noise layer: real cymbals have a broadband noise component above the
 * harmonic partials. We add HP-filtered (@ 3 kHz) noise scaled by brightness,
 * gated by the same envelope. This fills the "air" above the FM spectrum.
 */
fast_inline float32x4_t metal_engine_process(metal_engine_t* metal,
                                             float32x4_t envelope,
                                             uint32x4_t active_mask,
                                             float32x4_t lfo_pitch_mult,
                                             float32x4_t lfo_index_add,
                                             float32x4_t brightness_add,
                                             float32x4_t metal_gate) {

    // APC BAILOUT: Check if all 4 voices are dead
    // Extract the max value across the 4 lanes of the mask
    #if defined(__aarch64__)
        float max_mask = vmaxvq_f32(active_mask);
    #else
        // 32-bit ARM fallback for vector max
        float32x2_t max_half = vmax_f32(vget_low_f32(active_mask), vget_high_f32(active_mask));
        float max_mask = vget_lane_f32(vpmax_f32(max_half, max_half), 0);
    #endif

    // If the mask is zero across all lanes, SKIP THE MATH!
    if (max_mask == 0.0f) {
        return vdupq_n_f32(0.0f);
    }

    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);

    // 0. Apply hi-hat gate: scale envelope so MetalGate LFO can shorten decay
    //    metal_gate = 1.0 (fully open) → normal decay
    //    metal_gate → 0.0 (closing)   → early decay = closed hi-hat
    float32x4_t gated_env = vmulq_f32(envelope, metal_gate);

    // 1. Apply LFO Pitch Modulation
    float32x4_t base_freq = vmulq_f32(metal->carrier_freq_base, lfo_pitch_mult);

    // 2. Advance all 4 phases
    for (int i = 0; i < NEON_LANES; i++) {
        float32x4_t freq = vmulq_f32(base_freq, metal->current_ratio[i]);
        metal->phase[i] = vaddq_f32(metal->phase[i], vmulq_f32(freq, two_pi_over_sr));
        uint32x4_t wrap = vcgeq_f32(metal->phase[i], two_pi);
        metal->phase[i] = vbslq_f32(wrap, vsubq_f32(metal->phase[i], two_pi), metal->phase[i]);
    }

    // 3. Staggered Envelopes for Acoustic Dissipation (all derived from gated_env)
    float32x4_t genv2 = vmulq_f32(gated_env, gated_env);  // gate^2 * env^2
    float32x4_t genv4 = vmulq_f32(genv2, genv2);           // gate^4 * env^4

    // 4. Cascaded FM Synthesis
    float32x4_t base_index = vaddq_f32(vaddq_f32(metal->brightness, lfo_index_add), brightness_add);
    float32x4_t index_high = vmulq_n_f32(base_index, 3.0f);

    // Op4 -> Modulates Op3 (Dies very fast — genv4; gate further shortens this)
    float32x4_t op4 = neon_sin(metal->phase[3]);
    float32x4_t phase3_mod = vaddq_f32(metal->phase[2], vmulq_f32(op4, vmulq_f32(index_high, genv4)));
    float32x4_t op3 = neon_sin(phase3_mod);

    // Op3 -> Modulates Op2 (Dies moderately fast — genv2)
    float32x4_t phase2_mod = vaddq_f32(metal->phase[1], vmulq_f32(op3, vmulq_f32(base_index, genv2)));
    float32x4_t op2 = neon_sin(phase2_mod);

    // Op2 -> Modulates Op1 (Sustains — gated_env; gate brings it down over time)
    float32x4_t phase1_mod = vaddq_f32(metal->phase[0], vmulq_f32(op2, vmulq_f32(base_index, gated_env)));
    float32x4_t op1 = neon_sin(phase1_mod);

    // 5. Mix all operators for metallic/cymbal character.
    // Weights (0.5, 0.3, 0.15) give peak ≈ 2× at brightness=1; acceptable for
    // transient-heavy percussion with a band-separation HP filter downstream.
    float32x4_t bright_w2 = vmulq_f32(metal->brightness, vdupq_n_f32(0.5f));
    float32x4_t bright_w3 = vmulq_f32(metal->brightness, vdupq_n_f32(0.3f));
    float32x4_t bright_w4 = vmulq_f32(metal->brightness, vdupq_n_f32(0.15f));

    float32x4_t harmonics = vaddq_f32(
        vaddq_f32(vmulq_f32(op2, bright_w2), vmulq_f32(op3, bright_w3)),
        vmulq_f32(op4, bright_w4)
    );
    float32x4_t fm_output = vmulq_f32(vaddq_f32(op1, harmonics), gated_env);

    // 6. Cymbal noise layer (TR-909 / acoustic cymbal inspired)
    // HP white noise @ 3 kHz — fills the "air" above the inharmonic FM partials.
    // Amplitude ∝ brightness so dark settings stay clean, bright settings get hiss.
    // Gated by gated_env so noise also honours the hi-hat gate/decay.
    {
        uint32x4_t rand = neon_prng_rand_u32(&metal->noise_prng);
        // Float conversion: mask to 23-bit mantissa, add 1.0 bias, subtract 1.0 → [0,1)
        uint32x4_t masked    = vandq_u32(rand, vdupq_n_u32(0x7FFFFF));
        uint32x4_t float_bits = vorrq_u32(masked, vdupq_n_u32(0x3F800000));
        float32x4_t white = vsubq_f32(vreinterpretq_f32_u32(float_bits), vdupq_n_f32(1.0f));
        white = vsubq_f32(vmulq_f32(white, vdupq_n_f32(2.0f)), vdupq_n_f32(1.0f)); // [0,1)→[-1,1)
        // HP @ 3 kHz: subtract LP from input
        float32x4_t lp = one_pole_lpf_a(&metal->noise_hpf, white, METAL_NOISE_HP_A);
        float32x4_t noise_hp = vsubq_f32(white, lp);
        // noise_level = brightness * 0.15; max noise contribution ≈ 15% at full brightness
        float32x4_t noise_level = vmulq_n_f32(metal->brightness, 0.15f);
        fm_output = vaddq_f32(fm_output, vmulq_f32(vmulq_f32(noise_hp, noise_level), gated_env));
    }

    return vbslq_f32(active_mask, fm_output, vdupq_n_f32(0.0f));
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
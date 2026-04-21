#pragma once

/**
 *  @file metal_engine.h
 *  @brief 4-operator FM metal/cymbal engine
 *
 *  Parameters:
 *    Param1: Attack / Energy / Brightness
 *    Param2: Body / Decay / Stability
 *
 *  Design intent:
 *  - Param1 makes the hit brighter, harder, and more unstable at onset.
 *  - Param2 makes the ring denser, longer, and more body-heavy.
 */

#include <arm_neon.h>
#include "fm_voices.h"
#include "sine_neon.h"
#include "prng.h"

#define NUM_OPERATORS (4)

// Noise shaping for the bright transient layer
#define METAL_NOISE_HP_A 0.2820f

// Character 0 — DX7-style cymbal
#define METAL_RATIO1 1.0f
#define METAL_RATIO2 1.483f
#define METAL_RATIO3 1.932f
#define METAL_RATIO4 2.546f

// Character 1 — Gong / tam-tam
#define METAL_GONG_RATIO1 1.0f
#define METAL_GONG_RATIO2 2.756f
#define METAL_GONG_RATIO3 3.752f
#define METAL_GONG_RATIO4 5.404f

typedef struct {
    // Four operators
    float32x4_t phase[NUM_OPERATORS];
    float32x4_t base_ratio[NUM_OPERATORS];
    float32x4_t current_ratio[NUM_OPERATORS];

    // Carrier frequency
    float32x4_t carrier_freq_base;

    // Reinterpreted parameters
    float32x4_t attack;      // Param1
    float32x4_t body;        // Param2
    float32x4_t brightness;  // derived from Param1, used as a live spectral knob

    // Character variant (0 = Cymbal, 1 = Gong)
    uint32_t char_select;

    // Noise layer
    neon_prng_t noise_prng;
    one_pole_t noise_hpf;
} metal_engine_t;

/**
 * Initialize metal engine
 */
fast_inline void metal_engine_init(metal_engine_t* metal) {
    for (int i = 0; i < 4; i++) {
        metal->phase[i] = vdupq_n_f32(0.0f);
    }

    metal->base_ratio[0] = vdupq_n_f32(METAL_RATIO1);
    metal->base_ratio[1] = vdupq_n_f32(METAL_RATIO2);
    metal->base_ratio[2] = vdupq_n_f32(METAL_RATIO3);
    metal->base_ratio[3] = vdupq_n_f32(METAL_RATIO4);

    for (int i = 0; i < NUM_OPERATORS; i++) {
        metal->current_ratio[i] = metal->base_ratio[i];
    }

    metal->carrier_freq_base = vdupq_n_f32(1000.0f);
    metal->attack = vdupq_n_f32(0.5f);
    metal->body = vdupq_n_f32(0.5f);
    metal->brightness = vdupq_n_f32(0.5f);
    metal->char_select = 0;

    {
        uint64_t s0[2] = { 0xCAFEBABE12345678ULL, 0xFEDCBA0987654321ULL };
        uint64_t s1[2] = { 0x0F0F0F0F0F0F0F0FULL, 0xA1B2C3D4E5F60718ULL };
        metal->noise_prng.state0 = vld1q_u64(s0);
        metal->noise_prng.state1 = vld1q_u64(s1);
    }

    metal->noise_hpf.z1 = vdupq_n_f32(0.0f);
}

/**
 * Select metal engine character.
 *   0 = Cymbal / DX7-style
 *   1 = Gong / tam-tam
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

    for (int i = 0; i < NUM_OPERATORS; i++) {
        metal->current_ratio[i] = metal->base_ratio[i];
    }
}

/**
 * Update metal engine parameters from UI
 */
fast_inline void metal_engine_update(metal_engine_t* metal,
                                     float32x4_t param1,  // Attack / Energy / Brightness
                                     float32x4_t param2) { // Body / Decay / Stability
    metal->attack = param1;
    metal->body = param2;

    // Brightness is the live front-edge control used in process().
    // It follows attack but is softened slightly so body can still dominate the ring.
    metal->brightness = vaddq_f32(vdupq_n_f32(0.35f),
                                  vmulq_f32(param1, vdupq_n_f32(0.65f)));

    // Body spreads the base ratios farther away from unison, creating denser inharmonicity.
    // At body=0: close to the base ratios.
    // At body=1: pushed fully toward the character ratios.
    float32x4_t one = vdupq_n_f32(1.0f);
    float32x4_t spread = vaddq_f32(vdupq_n_f32(1.0f),
                                   vmulq_f32(param2, vdupq_n_f32(1.0f)));

    for (int i = 0; i < NUM_OPERATORS; i++) {
        float32x4_t offset = vsubq_f32(metal->base_ratio[i], one);
        metal->current_ratio[i] = vaddq_f32(one, vmulq_f32(offset, spread));
    }
}

/**
 * Set MIDI note (affects the whole operator cluster)
 */
fast_inline void metal_engine_set_note(metal_engine_t* metal,
                                       uint32x4_t voice_mask,
                                       float32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(440.0f);
    float32x4_t a4_midi = vdupq_n_f32(69.0f);
    float32x4_t twelfth = vdupq_n_f32(1.0f / 12.0f);

    float32x4_t exponent = vmulq_f32(vsubq_f32(midi_notes, a4_midi), twelfth);
    float32x4_t two_pow = exp2_neon(exponent);
    float32x4_t base_freq = vmulq_f32(a4_freq, two_pow);

    metal->carrier_freq_base = vbslq_f32(voice_mask,
                                         base_freq,
                                         metal->carrier_freq_base);
}

/**
 * Process one sample of metal engine.
 *
 * Arguments:
 * - envelope: shared amplitude envelope
 * - active_mask: voice gate mask
 * - lfo_pitch_mult: shared pitch modulation
 * - lfo_index_add: shared index modulation
 * - brightness_add: extra spectral brightness from synth/process layer
 * - metal_gate: per-voice closing gate for open/closed behavior
 */
fast_inline float32x4_t metal_engine_process(metal_engine_t* metal,
                                             float32x4_t envelope,
                                             uint32x4_t active_mask,
                                             float32x4_t lfo_pitch_mult,
                                             float32x4_t lfo_index_add,
                                             float32x4_t brightness_add,
                                             float32x4_t metal_gate) {
    float32x4_t two_pi_over_sr = vdupq_n_f32(2.0f * M_PI * INV_SAMPLE_RATE);
    float32x4_t two_pi = vdupq_n_f32(2.0f * M_PI);

    // Gate the envelope so the gate target can shorten the ring.
    float32x4_t gated_env = vmulq_f32(envelope, metal_gate);

    // Pitch modulation.
    float32x4_t base_freq = vmulq_f32(metal->carrier_freq_base, lfo_pitch_mult);

    // Advance all 4 phases.
    for (int i = 0; i < NUM_OPERATORS; i++) {
        float32x4_t freq = vmulq_f32(base_freq, metal->current_ratio[i]);
        metal->phase[i] = vaddq_f32(metal->phase[i], vmulq_f32(freq, two_pi_over_sr));
        uint32x4_t wrap = vcgeq_f32(metal->phase[i], two_pi);
        metal->phase[i] = vbslq_f32(wrap,
                                    vsubq_f32(metal->phase[i], two_pi),
                                    metal->phase[i]);
    }

    // Staggered decay domains.
    float32x4_t env2 = vmulq_f32(gated_env, gated_env);
    float32x4_t env4 = vmulq_f32(env2, env2);

    // Attack drives the immediate metallic edge.
    // Body drives how much of the cluster remains audible after the strike.
    float32x4_t base_index = vaddq_f32(vaddq_f32(metal->brightness, lfo_index_add), brightness_add);
    float32x4_t attack_index = vmulq_n_f32(metal->attack, 3.0f);
    float32x4_t index_high = vaddq_f32(base_index, attack_index);

    // Operator cascade: Op4 -> Op3 -> Op2 -> Op1.
    float32x4_t op4 = neon_sin(metal->phase[3]);
    float32x4_t phase3_mod = vaddq_f32(metal->phase[2],
                                       vmulq_f32(op4, vmulq_f32(index_high, env4)));
    float32x4_t op3 = neon_sin(phase3_mod);

    float32x4_t phase2_mod = vaddq_f32(metal->phase[1],
                                       vmulq_f32(op3, vmulq_f32(base_index, env2)));
    float32x4_t op2 = neon_sin(phase2_mod);

    float32x4_t phase1_mod = vaddq_f32(metal->phase[0],
                                       vmulq_f32(op2, vmulq_f32(base_index, gated_env)));
    float32x4_t op1 = neon_sin(phase1_mod);

    // Weighting: bright attack + denser body.
    float32x4_t bright_w2 = vmulq_f32(metal->brightness, vdupq_n_f32(0.50f));
    float32x4_t bright_w3 = vmulq_f32(metal->brightness, vdupq_n_f32(0.30f));
    float32x4_t bright_w4 = vmulq_f32(metal->brightness, vdupq_n_f32(0.15f));

    float32x4_t harmonics = vaddq_f32(
        vaddq_f32(vmulq_f32(op2, bright_w2), vmulq_f32(op3, bright_w3)),
        vmulq_f32(op4, bright_w4)
    );

    float32x4_t fm_output = vmulq_f32(vaddq_f32(op1, harmonics), gated_env);

    // Broadband noise layer for the bright transient.
    {
        uint32x4_t rand = neon_prng_rand_u32(&metal->noise_prng);
        uint32x4_t masked = vandq_u32(rand, vdupq_n_u32(0x7FFFFF));
        uint32x4_t float_bits = vorrq_u32(masked, vdupq_n_u32(0x3F800000));

        float32x4_t white = vsubq_f32(vreinterpretq_f32_u32(float_bits),
                                      vdupq_n_f32(1.0f));
        white = vsubq_f32(vmulq_f32(white, vdupq_n_f32(2.0f)),
                          vdupq_n_f32(1.0f));

        float32x4_t lp = one_pole_lpf_a(&metal->noise_hpf, white, METAL_NOISE_HP_A);
        float32x4_t noise_hp = vsubq_f32(white, lp);

        float32x4_t noise_level = vmulq_n_f32(metal->brightness, 0.15f);
        fm_output = vaddq_f32(fm_output, vmulq_f32(vmulq_f32(noise_hp, noise_level), gated_env));
    }

    return vbslq_f32(active_mask, fm_output, vdupq_n_f32(0.0f));
}

#ifdef TEST_METAL

void test_metal_inharmonicity() {
    metal_engine_t metal;
    metal_engine_init(&metal);

    float32x4_t param1 = vdupq_n_f32(0.0f);
    float32x4_t param2 = vdupq_n_f32(0.5f);
    metal_engine_update(&metal, param1, param2);

    for (int i = 0; i < NUM_OPERATORS; i++) {
        float ratio = vgetq_lane_f32(metal.current_ratio[i], 0);
        assert(fabsf(ratio - 1.0f) < 0.001f);
    }

    param1 = vdupq_n_f32(1.0f);
    metal_engine_update(&metal, param1, param2);

    float r1 = vgetq_lane_f32(metal.current_ratio[0], 0);
    float r2 = vgetq_lane_f32(metal.current_ratio[3], 0);
    assert(r2 > r1);

    printf("Metal engine inharmonicity test PASSED\n");
}

#endif
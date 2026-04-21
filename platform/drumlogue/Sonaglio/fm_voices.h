
/**
 * @file fm_voices.h
 * @brief NEON-aligned data structures for 4-voice FM synthesis
 *
 * FIXED: Using central constants from constants.h
 */

#ifndef CE1E60E1_B031_4A97_B2C2_CDB35865D5B7
#define CE1E60E1_B031_4A97_B2C2_CDB35865D5B7

#include <arm_neon.h>
#include <stdint.h>
#include "constants.h"
#include "envelope_rom.h"
#include "lfo_enhanced.h"

    // Maximum operators per voice
    constexpr int MAX_OPERATORS = 4;

/**
 * Simple one-pole filter state.
 * Shared by snare (noise shaping) and metal (cymbal noise HPF).
 */
typedef struct {
    float32x4_t z1;  // Delay element (one state per NEON lane = per voice)
} one_pole_t;

/**
 * One-pole LPF with precomputed alpha — no per-call division.
 * alpha = 2*pi*f / (2*pi*f + SAMPLE_RATE)
 */
fast_inline float32x4_t one_pole_lpf_a(one_pole_t* f, float32x4_t in, float alpha) {
    float32x4_t a   = vdupq_n_f32(alpha);
    float32x4_t out = vaddq_f32(vmulq_f32(in, a),
                                 vmulq_f32(f->z1, vsubq_f32(vdupq_n_f32(1.0f), a)));
    f->z1 = out;
    return out;
}

/**
 * One-pole LPF with runtime cutoff frequency (Hz).
 * Matched-z: alpha = 2*pi*f / (2*pi*f + sr)
 */
fast_inline float32x4_t one_pole_lpf(one_pole_t* f, float32x4_t in, float cutoff) {
    const float two_pi_f = 2.0f * (float)M_PI * cutoff;
    float32x4_t alpha = vdupq_n_f32(two_pi_f / (two_pi_f + SAMPLE_RATE));
    float32x4_t out = vaddq_f32(vmulq_f32(in, alpha),
                                vmulq_f32(f->z1, vsubq_f32(vdupq_n_f32(1.0f), alpha)));
    f->z1 = out;
    return out;
}

/**
 * Operator data for 4 voices (SoA format)
 */
typedef struct {
    float32x4_t phase[MAX_OPERATORS];
    float32x4_t freq[MAX_OPERATORS];
    float32x4_t index[MAX_OPERATORS];
    float32x4_t output[MAX_OPERATORS];
} fm_operators_t;

/**
 * Per-voice data
 */
typedef struct {
    uint32x4_t active;
    float32x4_t envelope;
    uint32x4_t env_stage;
    uint32x4_t stage_time;
    float32x4_t param1;
    float32x4_t param2;
    float32x4_t gain;

    // Note frequency (can use interval ratios from constants.h)
    float32x4_t note_freq;
} fm_voices_t;

/**
 * Complete FM synthesis state
 */
typedef struct {
    fm_operators_t  ops __attribute__((aligned(VECTOR_ALIGN)));
    fm_voices_t     voices __attribute__((aligned(VECTOR_ALIGN)));
    float32x4_t     lfo_phase[2] __attribute__((aligned(VECTOR_ALIGN)));
    uint32x4_t      prng_state[4] __attribute__((aligned(VECTOR_ALIGN)));
    int8_t          params[24];
} fm_state_t;

// Ensure proper alignment
static_assert(sizeof(fm_state_t) % VECTOR_ALIGN == 0,
              "fm_state_t must be 16-byte aligned for NEON");

/**
 * High-precision NEON 2^x
 * Adapted and optimized from Julien Pommier's neon_mathfun.h
 * licensed under the zlib license
 */
fast_inline float32x4_t exp2_neon(float32x4_t x) {
    // 1. Clamp to valid exponent range to prevent underflow/overflow
    x = vmaxq_f32(x, vdupq_n_f32(-126.0f));
    x = vminq_f32(x, vdupq_n_f32(126.0f));

    // 2. Separate integer (n) and fractional (f) parts
    // Add 0.5 to round to nearest integer
    float32x4_t fx = vaddq_f32(x, vdupq_n_f32(0.5f));
    int32x4_t n = vcvtq_s32_f32(fx); // Truncates towards zero
    float32x4_t n_float = vcvtq_f32_s32(n);

    // Fix truncation for negative numbers (Pommier's trick)
    uint32x4_t mask = vcltq_f32(fx, n_float);
    n = vaddq_s32(n, vreinterpretq_s32_u32(mask)); // mask is -1 where true
    n_float = vcvtq_f32_s32(n);

    // f is the fractional part, bound to [-0.5, 0.5]
    float32x4_t f = vsubq_f32(x, n_float);

    // 3. Compute e^(f * ln(2)) using Pommier's minimax polynomial
    // Multiply by ln(2)
    float32x4_t z = vmulq_f32(f, vdupq_n_f32(0.6931471805599453f));

    // Horner evaluation of the polynomial
    float32x4_t y = vdupq_n_f32(1.9875691500E-4f);
    y = vmlaq_f32(vdupq_n_f32(1.3981999507E-3f), y, z);
    y = vmlaq_f32(vdupq_n_f32(8.3334519073E-3f), y, z);
    y = vmlaq_f32(vdupq_n_f32(4.1665795894E-2f), y, z);
    y = vmlaq_f32(vdupq_n_f32(1.6666665459E-1f), y, z);
    y = vmlaq_f32(vdupq_n_f32(5.0000001201E-1f), y, z);
    y = vmlaq_f32(z, y, z);                 // y = z + y * z
    y = vaddq_f32(y, vdupq_n_f32(1.0f));    // y = 1.0 + y

    // 4. Compute 2^n by shifting into the IEEE-754 exponent field
    n = vaddq_s32(n, vdupq_n_s32(127));     // Add float exponent bias
    n = vshlq_n_s32(n, 23);                 // Shift to exponent bits
    float32x4_t pow2n = vreinterpretq_f32_s32(n);

    // 5. Final result: 2^n * 2^f
    return vmulq_f32(y, pow2n);
}

/**
 * Convert MIDI note to frequency using high-precision NEON Math
 * Formula: Freq = A4_FREQ * 2 ^ ((MIDI - A4_MIDI) / 12)
 */
fast_inline float32x4_t midi_to_freq_neon(uint32x4_t midi_notes) {
    float32x4_t a4_freq = vdupq_n_f32(A4_FREQ);
    float32x4_t a4_midi = vdupq_n_f32(A4_MIDI);

    // 1. Calculate semitone offset (MIDI - 69.0)
    float32x4_t offset = vsubq_f32(vcvtq_f32_u32(midi_notes), a4_midi);

    // 2. Divide by 12.0 to get octaves
    float32x4_t octaves = vmulq_f32(offset, vdupq_n_f32(1.0f / 12.0f));

    // 3. Calculate 2^octaves using adapted Pommier math
    float32x4_t pow2 = exp2_neon(octaves);

    // 4. Multiply by base tuning (440.0 * 2^x)
    return vmulq_f32(pow2, a4_freq);
}

/**
 * Initialize FM state
 */
fast_inline void fm_state_init(fm_state_t* state) {
    for (int op = 0; op < MAX_OPERATORS; op++) {
        state->ops.phase[op] = vdupq_n_f32(0.0f);
        state->ops.freq[op] = vdupq_n_f32(0.0f);
        state->ops.index[op] = vdupq_n_f32(0.0f);
        state->ops.output[op] = vdupq_n_f32(0.0f);
    }

    state->voices.active = vdupq_n_u32(0);
    state->voices.envelope = vdupq_n_f32(0.0f);
    state->voices.env_stage = vdupq_n_u32(ENV_STATE_OFF);  // Using constant
    state->voices.stage_time = vdupq_n_u32(0);
    state->voices.param1 = vdupq_n_f32(0.5f);
    state->voices.param2 = vdupq_n_f32(0.5f);
    state->voices.gain = vdupq_n_f32(0.25f);
    state->voices.note_freq = vdupq_n_f32(A4_FREQ);

    state->lfo_phase[0] = vdupq_n_f32(0.0f);
    state->lfo_phase[1] = vdupq_n_f32(LFO_PHASE_OFFSET);  // Using constant

    for (int i = 0; i < 4; i++) {
        state->prng_state[i] = vdupq_n_u32(0x9E3779B9);
    }
}


#endif /* CE1E60E1_B031_4A97_B2C2_CDB35865D5B7 */

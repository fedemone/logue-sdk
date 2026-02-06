#include "Partial.h"
#include "constants.h"

/**
 * Updates the filter coefficients based on physical modeling parameters.
 * Optimized for ARMv7 using pre-calculated identities and bit-manipulation.
 */
void Partial::update(float32_t f_0, float32_t ratio, float32_t ratio_max, float32_t vel, bool isRelease)
{
    const float log2e = 1.44269504f;
    const float inv_srate_2pi = M_TWOPI / srate;
    const float log_vel = vel * M_TWOLN100;

    // 1. Inharmonicity: f = inharm * 2^(vel * vel_inharm * log_vel * log2e)
    float offset_inharm = (vel * vel_inharm * log_vel) * log2e;
    union { float f; int32_t i; } u_inharm;
    u_inharm.i = (int32_t)(offset_inharm * 8388608.0f) + 1065353216;

    float inharm_k = fminf(1.0f, inharm * u_inharm.f) - 0.0001f;
    float r_minus_1 = ratio - 1.0f;
    inharm_k = fasterSqrt(1.0f + inharm_k * (r_minus_1 * r_minus_1));

    float f_k = f_0 * ratio * inharm_k;

    // 2. Decay calculation: d = decay * 2^(vel * vel_decay * log_vel * log2e)
    float offset_decay = (vel * vel_decay * log_vel) * log2e;
    union { float f; int32_t i; } u_decay;
    u_decay.i = (int32_t)(offset_decay * 8388608.0f) + 1065353216;

    float decay_k = fminf(c_decay_max, decay * u_decay.f);
    if (isRelease) decay_k *= rel;

    // Boundary check for stability and Nyquist
    // PREVENT CRASH: If decay is below threshold or freq is invalid, mute and exit.
    const float f_nyquist = c_nyquist_factor * srate;
    if (f_k >= f_nyquist || f_k < c_freq_min || decay_k < c_decay_min) {
        vb0 = vb2 = va1 = va2 = vdupq_n_f32(0.0f);
        // Note: We don't return 0 for a0; we just ensure the numerator (b) is 0.
        return;
    }

    // 3. Damping and Tone (Base-e power identity)
    float f_max = fminf(c_freq_max, f_0 * ratio_max * inharm_k);

    float damp_base = (damp <= 0 ? f_0 : f_max) / f_k;
    float damp_k = e_expff(fasterlogf(damp_base) * (damp * 2.0f));
    decay_k /= damp_k;

    float tone_base = (tone <= 0 ? f_k / f_0 : f_k / f_max);
    float tone_gain = e_expff(fasterlogf(tone_base) * (tone * 2.0f));

    // 4. Hit modulation
    float hit_mod = fminf(0.5f, hit + vel * vel_hit * 0.5f);
    float amp_k = 35.0f * fabsf(fastersinfullf(M_PI * (float)k * hit_mod));

    // 5. Filter Coefficients
    float omega = f_k * inv_srate_2pi;
    float inv_decay = inv_srate_2pi / decay_k;

    // Calculate raw coefficients as locals
    float _b0 = inv_srate_2pi * tone_gain * amp_k;
    float inv_a0 = 1.0f / (1.0f + inv_decay);

    // 6. Pre-normalize and Bake into NEON vectors
    // These vectors are used directly by the inlined process() in the header
    vb0 = vdupq_n_f32(_b0 * inv_a0);
    vb2 = vdupq_n_f32(-_b0 * inv_a0);
    va1 = vdupq_n_f32(-2.0f * fastercosfullf(omega) * inv_a0);
    va2 = vdupq_n_f32((1.0f - inv_decay) * inv_a0);
}

/**
 * Resets the filter delay lines to zero.
 * Crucial to call this when a voice is recycled to prevent "pops".
 */
void Partial::clear()
{
    vb0 = vb2 = va1 = va2 = vdupq_n_f32(0.0f);
    x1  = x2  = y1  = y2  = vdupq_n_f32(0.0f);
}
#pragma once

/**
 * @file filters.h
 * @brief NEON-optimized filters for sidechain processing
 *
 * Includes:
 * - Sidechain HPF (Biquad, Transposed Direct Form II)
 * - Envelope detector (Peak / RMS / Blend, sample-accurate ballistics)
 * - Shelving filters for the Overlord tone stack
 * - linear_to_db for the threshold comparisons
 */

#include <arm_neon.h>
#include <math.h>
#include "float_math.h"

/* ---------------------------------------------------------------------------
 * 1. SIDECHAIN HPF - 12dB/oct Bessel for clean sidechain
 * --------------------------------------------------------------------------- */

typedef struct {
    float z1, z2;              // Scalar biquad state (sequential IIR feedback)
    float b0, b1, b2, a1, a2; // Biquad coefficients
    float cutoff_hz;
    float sample_rate;
} sidechain_hpf_t;


/**
 * Initialize sidechain HPF (Bessel for clean phase response)
 * Called also at setParameter()
 */
fast_inline void sidechain_hpf_init(sidechain_hpf_t* f, float cutoff, float sr) {
    f->cutoff_hz = cutoff;
    f->sample_rate = sr;
    f->z1 = 0.0f;
    f->z2 = 0.0f;

    // Digital angular frequency for coefficient calculation
    float w0 = 2.0f * M_PI * cutoff / sr;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float Q = 0.5f;  // Bessel Q

    // Calculate alpha
    float alpha = sin_w0 / (2.0f * Q);  // TODO: review this, as with current settings this division is doing nothing (2.0 * 0.5)

    // Biquad coefficients (normalized)
    f->b0 = (1.0f + cos_w0) * 0.5f;
    f->b1 = -(1.0f + cos_w0);
    f->b2 = f->b0;
    f->a1 = -2.0f * cos_w0;
    f->a2 = 1.0f - alpha;

    // Normalize by a0 = 1 + alpha
    float a0 = 1.0f + alpha;
    f->b0 /= a0;
    f->b1 /= a0;
    f->b2 /= a0;
    f->a1 /= a0;
    f->a2 /= a0;
}

// Process 4 consecutive mono samples through the sidechain HPF.
// Scalar state guarantees correct IIR feedback: each y[n] updates z1/z2 before y[n+1].
fast_inline float32x4_t sidechain_hpf_process(sidechain_hpf_t* f, float32x4_t in) {
    float buf[4];
    vst1q_f32(buf, in);
    float lz1 = f->z1, lz2 = f->z2;
    const float b0 = f->b0, b1 = f->b1, b2 = f->b2, a1 = f->a1, a2 = f->a2;
    for (int i = 0; i < 4; ++i) {
        const float x = buf[i];
        const float y = b0 * x + lz1;
        lz1 = b1 * x - a1 * y + lz2;
        lz2 = b2 * x - a2 * y;
        buf[i] = y;
    }
    f->z1 = lz1; f->z2 = lz2;
    return vld1q_f32(buf);
}

/**
 * Update cutoff frequency (recalculates coefficients for smooth transition)
 */
fast_inline void sidechain_hpf_set_cutoff(sidechain_hpf_t* f, float cutoff) {
    if (fabsf(cutoff - f->cutoff_hz) > 1.0f) {
        // Recompute coefficients only; preserve filter state to avoid click.
        float sr = f->sample_rate;
        f->cutoff_hz = cutoff;
        float w0 = 2.0f * M_PI * cutoff / sr;
        float cos_w0 = cosf(w0);
        float sin_w0 = sinf(w0);
        float Q = 0.5f;
        float alpha = sin_w0 / (2.0f * Q);
        float a0 = 1.0f + alpha;
        f->b0 = (1.0f + cos_w0) * 0.5f / a0;
        f->b1 = -(1.0f + cos_w0) / a0;
        f->b2 = f->b0;
        f->a1 = -2.0f * cos_w0 / a0;
        f->a2 = (1.0f - alpha) / a0;
    }
}

/* ---------------------------------------------------------------------------
 * 2. ENVELOPE DETECTOR - Peak/RMS with selectable mode
 * --------------------------------------------------------------------------- */

#define DETECT_MODE_PEAK 0
#define DETECT_MODE_RMS  1
#define DETECT_MODE_BLEND 2

// Scalar state: the histories below are updated once per sample, in order.
// They used to be float32x4_t, which gave each of the 4 lanes its own detector
// advanced once per block — so a coefficient derived for 48 kHz was applied at
// 12 kHz and every time constant ran 4x long, with the lanes free to diverge.
typedef struct {
    float rms_accum;             // Running mean square
    float env_state;             // Smoothed envelope (linear)
    float rms_alpha;             // Per-sample coefficient of the RMS window
    uint32_t hold_counter;       // Samples left to hold before releasing
    uint32_t hold_samples;       // Hold length in samples
    uint8_t mode;                // Detection mode
    float attack_coeff;          // Attack smoothing
    float release_coeff;         // Release smoothing
    float sample_rate;
} envelope_detector_t;

/**
 * Initialize envelope detector
 */
fast_inline void envelope_detector_init(envelope_detector_t* env, float sr) {
    env->rms_accum = 0.0f;
    env->env_state = 0.0f;
    env->hold_counter = 0;
    env->hold_samples = (uint32_t)(ENV_HOLD_MS * 0.001f * sr);
    env->mode = DETECT_MODE_PEAK;
    env->sample_rate = sr;

    // Default 10ms attack, 100ms release
    env->attack_coeff = e_expff(-1.0f / (0.01f * sr));
    env->release_coeff = e_expff(-1.0f / (0.1f * sr));
    env->rms_alpha = 1.0f - e_expff(-1.0f / (ENV_RMS_WINDOW_MS * 0.001f * sr));
}

// Set attack/release times
fast_inline void envelope_set_attack_release(envelope_detector_t* env,
                                             float attack_ms,
                                             float release_ms) {
    env->attack_coeff = e_expff(-1.0f / (attack_ms * 0.001f * env->sample_rate));
    env->release_coeff = e_expff(-1.0f / (release_ms * 0.001f * env->sample_rate));
}

// Process 4 samples through the envelope detector, one sample at a time so the
// attack/release coefficients mean what they say.
//
// Peak mode used to latch: a hold counter incremented once per block, so the
// "10ms hold" was really 417 ms, and when it expired it applied a single 0.999
// step before resetting — about 0.009 dB of decay per 417 ms. The envelope
// could rise but effectively never fell, so gain reduction never recovered
// after a transient and the RELEASE control did nothing. Peak now rectifies and
// lets the attack/release one-pole below provide the ballistics, which is what
// makes RELEASE audible.
//
// Blend mode read peak_hold, which only the peak branch ever wrote — and a
// switch runs one branch, so it was stuck at 0 and the "blend" was 0.3x RMS,
// i.e. 10.5 dB low. It now derives peak locally like the other modes.
fast_inline float32x4_t envelope_detect(envelope_detector_t* env,
                                        float32x4_t sidechain) {
    float x[4], out[4];
    vst1q_f32(x, sidechain);

    float rms_accum = env->rms_accum;
    float state     = env->env_state;
    uint32_t hold   = env->hold_counter;
    const float att = env->attack_coeff;
    const float rel = env->release_coeff;
    const float ra  = env->rms_alpha;
    const uint32_t hold_samples = env->hold_samples;
    const uint8_t mode = env->mode;

    for (int i = 0; i < 4; ++i) {
        const float ax = fabsf(x[i]);
        float target;

        switch (mode) {
            case DETECT_MODE_RMS:
                rms_accum += ra * (x[i] * x[i] - rms_accum);
                target = sqrtf(rms_accum);
                break;

            case DETECT_MODE_BLEND:
                // Peak and RMS in the SSL proportion
                rms_accum += ra * (x[i] * x[i] - rms_accum);
                target = 0.7f * ax + 0.3f * sqrtf(rms_accum);
                break;

            case DETECT_MODE_PEAK:
            default:
                target = ax;
                break;
        }

        // Rising: attack, and re-arm the hold. Falling: sit still for the hold
        // period, then release. Without the hold a peak detector sags between
        // waveform peaks, and at fast release settings that ripple modulates the
        // VCA at audio rate. This is the 10 ms hold the old code documented but
        // never actually applied.
        if (target > state) {
            state = target + att * (state - target);
            hold = hold_samples;
        } else if (hold > 0) {
            --hold;
        } else {
            state = target + rel * (state - target);
        }
        out[i] = state;
    }

    env->rms_accum = rms_accum;
    env->env_state = state;
    env->hold_counter = hold;
    return vld1q_f32(out);
}

/* ---------------------------------------------------------------------------
 * 3. OPERATION OVERLORD FILTERS
 *
 * The standalone gain computer and attack/release smoother that used to live
 * here were never called: every mode computes its own curve inline and smooths
 * in the dB domain. The knee logic they carried now lives in
 * distressor_knee(), where it is actually reachable.
 * --------------------------------------------------------------------------- */

// Biquad state with scalar IIR history and cached coefficients.
// Coefficients are recomputed only when freq/gain_db change (not every block).
typedef struct {
    float z1, z2;              // Scalar state for correct sequential IIR feedback
    float b0, b1, b2, a1, a2; // Cached normalized coefficients
    float last_freq;
    float last_gain_db;
    int   last_low_shelf;
} biquad_state_t;

fast_inline void biquad_init_state(biquad_state_t* state) {
    state->z1 = 0.0f; state->z2 = 0.0f;
    state->b0 = state->b1 = state->b2 = state->a1 = state->a2 = 0.0f;
    state->last_freq = -1.0f;
    state->last_gain_db = -999.0f;
    state->last_low_shelf = -1;
}

// Shelving filter — Audio EQ Cookbook (RBJ).
// Coefficients are cached in state and only recomputed when freq or gain_db change.
// 4 samples processed sequentially for correct IIR feedback.
fast_inline float32x4_t shelving_filter(float32x4_t in,
                                        biquad_state_t* state,
                                        float freq,
                                        float gain_db,
                                        int low_shelf,
                                        float sr) {
    if (fabsf(gain_db) < 0.01f) return in;

    // Recompute coefficients only when parameters actually change.
    if (freq != state->last_freq || gain_db != state->last_gain_db ||
        low_shelf != state->last_low_shelf) {

        // A = 10^(dB/40) = exp(dB * ln(10)/40).  fasterpowf is only good to a
        // few percent here, which is a visible shelf-gain error; e_expff holds
        // well under 0.01 dB across the +/-12 dB range and is cached anyway.
        float A      = e_expff(gain_db * (INV_DB_COEFF * 0.5f));
        float sqrtA  = fasterSqrt(A);
        float w0     = 2.0f * M_PI * freq / sr;
        float cos_w0 = fastercosfullf(w0);
        float sin_w0 = fastersinfullf(w0);
        float alpha  = sin_w0 * 0.70711f;   // sin(w0) / sqrt(2)

        float b0, b1, b2, a0, a1, a2;
        if (low_shelf) {
            b0 =    A * ((A+1) - (A-1)*cos_w0 + 2.0f*sqrtA*alpha);
            b1 =  2*A * ((A-1) - (A+1)*cos_w0);
            b2 =    A * ((A+1) - (A-1)*cos_w0 - 2.0f*sqrtA*alpha);
            a0 =        ((A+1) + (A-1)*cos_w0 + 2.0f*sqrtA*alpha);
            a1 =  -2  * ((A-1) + (A+1)*cos_w0);
            a2 =        ((A+1) + (A-1)*cos_w0 - 2.0f*sqrtA*alpha);
        } else {
            b0 =    A * ((A+1) + (A-1)*cos_w0 + 2.0f*sqrtA*alpha);
            b1 = -2*A * ((A-1) + (A+1)*cos_w0);
            b2 =    A * ((A+1) + (A-1)*cos_w0 - 2.0f*sqrtA*alpha);
            a0 =        ((A+1) - (A-1)*cos_w0 + 2.0f*sqrtA*alpha);
            a1 =   2  * ((A-1) - (A+1)*cos_w0);
            a2 =        ((A+1) - (A-1)*cos_w0 - 2.0f*sqrtA*alpha);
        }
        float inv_a0 = 1.0f / a0;
        state->b0 = b0 * inv_a0;
        state->b1 = b1 * inv_a0;
        state->b2 = b2 * inv_a0;
        state->a1 = a1 * inv_a0;
        state->a2 = a2 * inv_a0;
        state->last_freq      = freq;
        state->last_gain_db   = gain_db;
        state->last_low_shelf = low_shelf;
    }

    // Sequential scalar IIR — correct feedback chain across all 4 samples.
    float buf[4];
    vst1q_f32(buf, in);
    float lz1 = state->z1, lz2 = state->z2;
    const float b0 = state->b0, b1 = state->b1, b2 = state->b2;
    const float a1 = state->a1, a2 = state->a2;
    for (int i = 0; i < 4; ++i) {
        const float x = buf[i];
        const float y = b0 * x + lz1;
        lz1 = b1 * x - a1 * y + lz2;
        lz2 = b2 * x - a2 * y;
        buf[i] = y;
    }
    state->z1 = lz1; state->z2 = lz2;
    return vld1q_f32(buf);
}

// High-shelf convenience wrapper
fast_inline float32x4_t high_shelf_filter(float32x4_t in,
                                          biquad_state_t* state,
                                          float freq,
                                          float gain_db,
                                          float q,
                                          float sr) {
    (void)q; // Not used in first-order shelf
    return shelving_filter(in, state, freq, gain_db, 0, sr);
}

// Low-shelf wrapper
fast_inline float32x4_t low_shelf_filter(float32x4_t in,
                                         biquad_state_t* state,
                                         float freq,
                                         float gain_db,
                                         float q,
                                         float sr) {
    (void)q;
    return shelving_filter(in, state, freq, gain_db, 1, sr);
}

// Convert linear to dB.  The bare mantissa interpolation this used to inline
// was good to only 0.52 dB, which showed up directly as threshold error in
// Standard and Distressor modes; neon_log2q_f32 is the shared 0.005 dB version.
fast_inline float32x4_t linear_to_db(float32x4_t linear) {
  return vmulq_n_f32(neon_log2q_f32(linear), 6.0206f); // 20 * log10(2)
}

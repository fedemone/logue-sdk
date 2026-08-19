#pragma once

/**
 * @file filters.h
 * @brief NEON-optimized filters for sidechain processing
 *
 * Includes:
 * - Sidechain HPF (Biquad, Transposed Direct Form II)
 * - Envelope detector (Peak/RMS)
 * - Gain computer with knee
 * - Attack/release smoothing
 * Complete filter suite for OmniPress
 * Includes: Sidechain HPF, Envelope followers, Smoothing filters
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
 * 3. GAIN COMPUTER - With soft/hard knee and ratio logic
 * --------------------------------------------------------------------------- */

#define KNEE_HARD 0
#define KNEE_SOFT 1
#define KNEE_MEDIUM 2

typedef struct {
    float knee_width;      // dB width of soft knee
    uint8_t knee_type;     // 0=hard, 1=soft, 2=medium
} gain_computer_t;

/**
 * Initialize gain computer
 */
fast_inline void gain_computer_init(gain_computer_t* gc) {
  // TODO these values are never updated - but function is now obsolete.
  gc->knee_width = 6.0f; // 6dB soft knee
  gc->knee_type = KNEE_MEDIUM;
}

// Compute gain reduction with knee - obsolete
fast_inline float32x4_t gain_computer_process(gain_computer_t* gc,
                                              float32x4_t envelope_db,
                                              float thresh_db,
                                              float ratio) {
    float32x4_t thresh = vdupq_n_f32(thresh_db);
    float32x4_t ratio_v = vdupq_n_f32(ratio);
    float32x4_t one = vdupq_n_f32(1.0f);

    // Calculate overshoot
    float32x4_t overshoot = vsubq_f32(envelope_db, thresh);

    // Apply knee based on type
    float32x4_t gain_red;

    switch (gc->knee_type) {
        case KNEE_HARD: {
            // Hard knee - instantaneous
            uint32x4_t above = vcgtq_f32(envelope_db, thresh);
            gain_red = vbslq_f32(above,
                                 vmulq_f32(overshoot, vsubq_f32(one, vrecpeq_f32(ratio_v))),
                                 vdupq_n_f32(0.0f));
            break;
        }

        case KNEE_SOFT: {
            // Soft knee - polynomial transition
            float32x4_t knee_w = vdupq_n_f32(gc->knee_width);
            float32x4_t knee_start = vsubq_f32(thresh, vmulq_f32(knee_w, vdupq_n_f32(0.5f)));
            float32x4_t knee_end = vaddq_f32(thresh, vmulq_f32(knee_w, vdupq_n_f32(0.5f)));

            // In knee region
            uint32x4_t in_knee = vandq_u32(vcgtq_f32(envelope_db, knee_start),
                                           vcltq_f32(envelope_db, knee_end));

            // Above knee
            uint32x4_t above_knee = vcgtq_f32(envelope_db, knee_end);

            // AES soft-knee: GR = overshoot² / (2*knee_w) * (1 - 1/ratio)
            // Previous code used x=(overshoot/knee_w), knee_gr=0.5*x²*(…) which
            // gave overshoot²/knee_w² — off by factor knee_w (too little GR).
            float32x4_t overshoot_knee = vsubq_f32(envelope_db, knee_start);
            float32x4_t knee_gr = vmulq_f32(vmulq_f32(overshoot_knee, overshoot_knee),
                                             fast_div_neon(vdupq_n_f32(0.5f), knee_w));
            knee_gr = vmulq_f32(knee_gr, vsubq_f32(one, vrecpeq_f32(ratio_v)));

            gain_red = vbslq_f32(in_knee, knee_gr,
                        vbslq_f32(above_knee,
                                 vmulq_f32(overshoot, vsubq_f32(one, vrecpeq_f32(ratio_v))),
                                 vdupq_n_f32(0.0f)));
            break;
        }

        case KNEE_MEDIUM:
        default: {
            // Medium knee - linear transition
            float32x4_t knee_w = vdupq_n_f32(gc->knee_width);
            float32x4_t knee_start = vsubq_f32(thresh, vmulq_f32(knee_w, vdupq_n_f32(0.5f)));

            // Linear interpolation in knee region
            float32x4_t slope = vsubq_f32(one, vrecpeq_f32(ratio_v));
            float32x4_t in_knee_amt = fast_div_neon(vsubq_f32(envelope_db, knee_start), knee_w);
            in_knee_amt = vmaxq_f32(vminq_f32(in_knee_amt, one), vdupq_n_f32(0.0f));

            gain_red = vmulq_f32(vmulq_f32(overshoot, slope), in_knee_amt);
            break;
        }
    }

    return vnegq_f32(gain_red);  // Return negative dB (gain reduction)
}

/* ---------------------------------------------------------------------------
 * 4. ATTACK/RELEASE SMOOTHING - With auto time constants
 * --------------------------------------------------------------------------- */


typedef struct {
    float32x4_t current_gain;      // Current gain reduction (dB)
    float attack_coeff;             // Attack smoothing coefficient
    float release_coeff;            // Release smoothing coefficient
    float sample_rate;
} smoothing_t;

/**
 * Initialize smoother
 */
fast_inline void smoothing_init(smoothing_t* sm, float sr) {
    sm->current_gain = vdupq_n_f32(0.0f);
    sm->attack_coeff = e_expff(-1.0f / (0.01f * sr));    // 10ms default
    sm->release_coeff = e_expff(-1.0f / (0.1f * sr));    // 100ms default
    sm->sample_rate = sr;
}

/**
 * Process one sample of smoothing - obsolete as this is now integrated into the envelope detector, but keeping for reference.
 */
fast_inline float32x4_t smoothing_process(smoothing_t* sm,
                                          float32x4_t target_gain) {
    // Determine if we're attacking or releasing
    uint32x4_t attacking = vcltq_f32(target_gain, sm->current_gain);

    // Select appropriate coefficient
    float32x4_t coeff = vbslq_f32(attacking,
                                  vdupq_n_f32(sm->attack_coeff),
                                  vdupq_n_f32(sm->release_coeff));

    // One-pole smoothing: y[n] = a * x[n] + (1-a) * y[n-1]
    // But here we use: current = target*(1-coeff) + current*coeff
    sm->current_gain = vaddq_f32(vmulq_f32(target_gain, vsubq_f32(vdupq_n_f32(1.0f), coeff)),
                                 vmulq_f32(sm->current_gain, coeff));

    return sm->current_gain;
}

/**
 * Set time constants
 */
fast_inline void smoothing_set_times(smoothing_t* sm,
                                     float attack_ms,
                                     float release_ms) {
    sm->attack_coeff = e_expff(-1.0f / (attack_ms * 0.001f * sm->sample_rate));
    sm->release_coeff = e_expff(-1.0f / (release_ms * 0.001f * sm->sample_rate));
}



/* ---------------------------------------------------------------------------
 * 5. OPERATION OVERLORD DISTORTION
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

#pragma once
/*
 * File: distressor_mode.h
 *
 * Distressor-style compression curves and harmonic generation
 * Emulates Empirical Labs EL8 Distressor
 */

#include <arm_neon.h>
#include <math.h>
#include "filters.h"

// Distressor ratio modes (8 total)
#define DIST_RATIO_1_1   0  // Warm mode - no compression, just harmonics
#define DIST_RATIO_2_1   1  // Mild compression
#define DIST_RATIO_3_1   2  // Mild compression
#define DIST_RATIO_4_1   3  // Medium compression
#define DIST_RATIO_6_1   4  // Medium compression
#define DIST_RATIO_10_1  5  // "Opto" mode - optical emulation, slow release
#define DIST_RATIO_20_1  6  // Hard limiting
#define DIST_RATIO_NUKE  7  // Brick-wall limiting

// Distortion modes (5 modes - wavefolder added as 4)
#define DIST_MODE_CLEAN  0  // No harmonics
#define DIST_MODE_DIST2  1  // 2nd harmonic emphasis (tube-like)
#define DIST_MODE_DIST3  2  // 3rd harmonic emphasis (tape-like)
#define DIST_MODE_BOTH   3  // Both harmonics
#define DIST_MODE_WAVE   4  // Wavefolder
#define DIST_MODE_TOTAL  4  // Counter

// Detector modes (bit flags)
#define DETECT_NONE      0
#define DETECT_HPF       (1 << 0)  // 100 Hz high-pass
#define DETECT_BAND_EMPH (1 << 1)  // 6 kHz boost
#define DETECT_LINK      (1 << 2)  // Stereo link

// Audio modes (bit flags)
#define AUDIO_NONE       0
#define AUDIO_HPF        (1 << 0)  // 80 Hz Bessel HPF
#define AUDIO_DIST2      (1 << 1)  // 2nd harmonic
#define AUDIO_DIST3      (1 << 2)  // 3rd harmonic

// Distressor Mode String Display
static const char* distressor_dist_strings[5] = {
    "Off",      // 0 - Clean
    "Dist2",    // 1 - Tube-like 2nd harmonic
    "Dist3",    // 2 - Tape-like 3rd harmonic
    "Both",     // 3 - Combined harmonics
    "Wave",     // 4 - Wavefolder (new)
};

// Display strings for UI
static const char* distressor_ratio_strings[8] = {
    "1:1", "2:1", "3:1", "4:1", "6:1", "Opto", "20:1", "NUKE"
};


// Distressor state structure
typedef struct {
    // Mode selection
    uint8_t ratio_mode;      // 0-7
    uint8_t dist_mode;       // 0-4 (CLEAN, DIST2, DIST3, BOTH, WAVE)
    uint8_t detector_mode;   // Bit flags

    // Time constants
    float attack_ms;         // 0.05 to 30 ms
    float release_ms;        // 50 to 3500 ms (up to 20000 in opto)
    float attack_coeff;
    float release_coeff;

    // Harmonic generation state
    float32x4_t harmonic_state;
    float32x4_t last_input;

    // Opto mode state (slow release simulation)
    float opto_release_mult;  // Up to 20s in opto mode
    float opto_coeff ;        // deduced from above

    // NEW: Distressor-specific detector components
    sidechain_hpf_t detect_hpf;     // 100 Hz HPF for detector
    sidechain_hpf_t detect_emph;    // 6 kHz emphasis for detector
    envelope_detector_t distressor_env;  // Dedicated envelope detector
    float32x4_t detector_state;

} distressor_t;

fast_inline void update_opto_coeff(distressor_t* d, float release_coeff_) {
    // Opto mode slows release by raising the coefficient to 1/mult power,
    // which is equivalent to multiplying the release time constant by mult
    // while keeping the coefficient safely in (0,1).
    d->opto_coeff = (d->opto_release_mult > 1.0f)
        ? fasterpowf(release_coeff_, 1.0f / d->opto_release_mult): release_coeff_;
}

// Initialize Distressor with detector
fast_inline void distressor_init(distressor_t* d, float sample_rate) {
    d->ratio_mode = DIST_RATIO_4_1;
    d->dist_mode = DIST_MODE_CLEAN;
    d->detector_mode = DETECT_NONE;
    d->attack_ms = 0.5f;      // Much faster than standard (0.5ms)
    d->release_ms = 200.0f;
    d->attack_coeff = expf(-1.0f / (d->attack_ms * 0.001f * sample_rate));
    d->release_coeff = expf(-1.0f / (d->release_ms * 0.001f * sample_rate));
    d->harmonic_state = vdupq_n_f32(0.0f);
    d->last_input = vdupq_n_f32(0.0f);
    d->opto_release_mult = 1.0f;
    d->detector_state = vdupq_n_f32(0.0f);

    // Initialize detector HPF at 100 Hz (removes low-end pumping)
    sidechain_hpf_init(&d->detect_hpf, 100.0f, sample_rate);

    // Initialize emphasis filter at 6 kHz (boosts transients)
    sidechain_hpf_init(&d->detect_emph, 6000.0f, sample_rate);

    // Initialize distressor envelope detector
    envelope_detector_init(&d->distressor_env, sample_rate);

    // Set faster attack/release for distressor detector
    envelope_set_attack_release(&d->distressor_env, d->attack_ms, d->release_ms);
}

// Distressor-specific envelope detector (called in masterfx.h)
fast_inline float32x4_t distressor_detect(distressor_t* d,
                                           float32x4_t sidechain,
                                           float sample_rate) {
    (void)sample_rate;

    // 1. Apply detector HPF at 100 Hz (less low-end pumping)
    float32x4_t detected = sidechain_hpf_process(&d->detect_hpf, sidechain);

    // 2. Apply 6 kHz boost for detector (more sensitivity to transients)
    detected = sidechain_hpf_process(&d->detect_emph, detected);

    // 3. Full-wave rectification (like Distressor's detector)
    detected = vabsq_f32(detected);

    // 4. Distressor-specific envelope smoothing with faster attack
    return envelope_detect(&d->distressor_env, detected);
}

fast_inline void distressor_reset(distressor_t* d, float sample_rate) {
    distressor_init(d, sample_rate);
}

// Distressor-specific smoothing with opto mode support
fast_inline float32x4_t distressor_smooth(distressor_t* d,
                                          float32x4_t target_db,
                                          float attack_coeff,
                                          float release_coeff) {
    // One-pole smoothing with mode-specific adjustments
    uint32x4_t attacking = vcltq_f32(target_db, d->harmonic_state);
    float32x4_t coeff = vbslq_f32(attacking,
                                  vdupq_n_f32(attack_coeff),
                                  vdupq_n_f32(release_coeff));

    d->harmonic_state = vaddq_f32(vmulq_f32(target_db, vsubq_f32(vdupq_n_f32(1.0f), coeff)),
                                   vmulq_f32(d->harmonic_state, coeff));

    return d->harmonic_state;
}

// Set ratio mode (updates compression curve and opto behavior)
fast_inline void distressor_set_ratio(distressor_t* d, uint8_t mode) {
    d->ratio_mode = mode;

    // In 10:1 "Opto" mode, release can be up to 20 seconds [citation:4]
    if (mode == DIST_RATIO_10_1) {
        d->opto_release_mult = 5.7f;  // Scale release to 20s max
    } else {
        d->opto_release_mult = 1.0f;
    }
}

// Generate 2nd/3rd harmonics (Dist 2 / Dist 3)
fast_inline float32x4_t generate_harmonics(distressor_t* d,
                                           float32x4_t in,
                                           uint8_t mode) {
    (void)d;
    float32x4_t out = in;

    switch (mode) {
        case DIST_MODE_DIST2: {
            // 2nd harmonic: emphasize even-order
            // y = x + 0.5 * x^2 (soft tube-like saturation)
            float32x4_t x2 = vmulq_f32(in, in);
            out = vaddq_f32(in, vmulq_f32(x2, vdupq_n_f32(0.5f)));
            break;
        }
        case DIST_MODE_DIST3: {
            // 3rd harmonic: tape-like odd-order
            // y = x - x^3/3 (similar to tanh but with 3rd harmonic emphasis)
            float32x4_t x2 = vmulq_f32(in, in);
            float32x4_t x3 = vmulq_f32(in, x2);
            out = vsubq_f32(in, vmulq_f32(x3, vdupq_n_f32(0.333f)));
            break;
        }
        case DIST_MODE_BOTH: {
            // Combined harmonics
            float32x4_t x2 = vmulq_f32(in, in);
            float32x4_t x3 = vmulq_f32(in, x2);
            out = vaddq_f32(in, vmulq_f32(x2, vdupq_n_f32(0.25f)));
            out = vsubq_f32(out, vmulq_f32(x3, vdupq_n_f32(0.166f)));
            break;
        }
        default:
            break;
    }

    return out;
}

// Distressor gain computer (8 unique curves)
fast_inline float32x4_t distressor_gain_computer(distressor_t* d,
                                                  float32x4_t envelope_db,
                                                  float thresh_db) {
    float32x4_t gain_db = vdupq_n_f32(0.0f);
    float32x4_t thresh = vdupq_n_f32(thresh_db);
    float32x4_t excess = vsubq_f32(envelope_db, thresh);
    excess = vmaxq_f32(excess, vdupq_n_f32(0.0f));

    switch (d->ratio_mode) {
        case DIST_RATIO_1_1:
            // 1:1 - no compression, just harmonics
            gain_db = vdupq_n_f32(0.0f);
            break;

        case DIST_RATIO_2_1:
            // 2:1 - gentle parabolic knee
            // GR = excess * 0.5 with soft knee extending up to 30dB [citation:4]
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.5f));
            break;

        case DIST_RATIO_3_1:
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.667f));
            break;

        case DIST_RATIO_4_1:
            // 4:1 - steeper knee
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.75f));
            break;

        case DIST_RATIO_6_1:
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.833f));
            break;

        case DIST_RATIO_10_1:
            // "Opto" mode - special circuit emulating optical compressors [citation:4]
            // Uses separate detection circuitry
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.9f));
            break;

        case DIST_RATIO_20_1:
            // Hard limiting
            gain_db = vmulq_f32(excess, vdupq_n_f32(0.95f));
            break;

        case DIST_RATIO_NUKE: {
            // Brick-wall limiting - infinite compression above threshold [citation:3]
            // Maps excess to very high gain reduction
            uint32x4_t above_thresh = vcgtq_f32(envelope_db, thresh);
            gain_db = vbslq_f32(above_thresh,
                                vdupq_n_f32(-40.0f),  // 40dB reduction when triggered
                                vdupq_n_f32(0.0f));
            break;
        }
    }

    return vnegq_f32(gain_db);  // Return negative dB for gain reduction
}
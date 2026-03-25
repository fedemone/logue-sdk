// New file: operation_overlord.h

#pragma once
/**
 * @file operation_overlord.h
 * @brief EHX Operation Overlord drum machine overdrive emulation
 *
 * Features:
 * - Tube-like asymmetric clipping
 * - Bass/Treble EQ (pre-drive)
 * - Blend control (parallel drive)
 * - Presence control (post-drive high-end)
 */

#include <arm_neon.h>
#include <filters.h>

typedef struct {
    // Tube emulation stages
    float32x4_t tube_state;      // First tube stage
    float32x4_t tube_state2;     // Second tube stage

    // EQ filters
    biquad_state_t bass_boost;
    biquad_state_t treble_boost;
    biquad_state_t presence;

    // Blend
    float32x4_t dry_wet;

    // Parameters
    float drive;          // 0-100%
    float bass;           // 0-100% (-12 to +12 dB)
    float treble;         // 0-100% (-12 to +12 dB)
    float blend;          // 0-100% (dry/wet)
    float presence;       // 0-100% (high-end sparkle)
} overlord_t;

// Initialize Operation Overlord emulation
fast_inline void overlord_init(overlord_t* ov, float sample_rate) {
    ov->drive = 0.0f;
    ov->bass = 0.5f;
    ov->treble = 0.5f;
    ov->blend = 1.0f;
    ov->presence = 0.5f;

    ov->tube_state = vdupq_n_f32(0.0f);
    ov->tube_state2 = vdupq_n_f32(0.0f);

    // Initialize EQ filters (Baxandall topology)
    biquad_init_state(&ov->bass_boost);
    biquad_init_state(&ov->treble_boost);
    biquad_init_state(&ov->presence);
}

/**
 * Set drive amount (0-100%)
 */
fast_inline void overlord_set_drive(overlord_t* ov, float drive_percent) {
    float drive = drive_percent / 100.0f;
    ov->drive = drive;
}

// Tube saturation stage (asymmetric clipping)
fast_inline float32x4_t tube_saturate(float32x4_t in, float drive) {
    // Asymmetric clipping: positive side clips later than negative
    // Simulates 12AX7 tube characteristics

    float32x4_t pos_clip = vdupq_n_f32(0.8f);
    float32x4_t neg_clip = vdupq_n_f32(0.6f);
    float32x4_t one = vdupq_n_f32(1.0f);

    // Apply drive gain
    float32x4_t driven = vmulq_f32(in, vaddq_f32(one, vmulq_f32(vdupq_n_f32(drive), vdupq_n_f32(3.0f))));

    // Asymmetric clipping
    uint32x4_t pos = vcgtq_f32(driven, pos_clip);
    uint32x4_t neg = vcltq_f32(driven, vnegq_f32(neg_clip));

    float32x4_t clipped = driven;
    clipped = vbslq_f32(pos, pos_clip, clipped);
    clipped = vbslq_f32(neg, vnegq_f32(neg_clip), clipped);

    // Soft knee for tube warmth
    float32x4_t knee_region = vandq_u32(
        vcgtq_f32(driven, vmulq_f32(pos_clip, vdupq_n_f32(0.9f))),
        vcltq_f32(driven, pos_clip)
    );

    float32x4_t knee = vsubq_f32(driven, vmulq_f32(pos_clip, vdupq_n_f32(0.9f)));
    knee = vmulq_f32(knee, vdupq_n_f32(10.0f));  // 10:1 knee slope

    clipped = vbslq_f32(knee_region, vaddq_f32(vmulq_f32(pos_clip, vdupq_n_f32(0.9f)), knee), clipped);

    return clipped;
}

// Baxandall bass/treble EQ (used in many tube preamps)
fast_inline float32x4_t baxandall_eq(float32x4_t in,
                                     biquad_state_t* state,
                                     float bass,
                                     float treble,
                                     float sample_rate) {
    // Simplified Baxandall: two shelving filters in parallel
    float bass_gain = -12.0f + bass * 24.0f;   // -12 to +12 dB
    float treble_gain = -12.0f + treble * 24.0f;

    // Bass shelf at 100 Hz
    float32x4_t bass_shelf = shelving_filter(in, state, 100.0f, bass_gain, 0.5f, sample_rate);

    // Treble shelf at 10 kHz
    float32x4_t treble_shelf = shelving_filter(bass_shelf, state, 10000.0f, treble_gain, 0.5f, sample_rate);

    return treble_shelf;
}

// Main Overlord processing
fast_inline float32x4x2_t overlord_process(overlord_t* ov,
                                           float32x4_t in_l,
                                           float32x4_t in_r,
                                           float sample_rate) {
                                             // Check if any processing is active
    float active_threshold = 0.01f;
    if (ov->drive < active_threshold &&
        fabsf(ov->bass - 0.5f) < active_threshold &&  // 0.5 = flat
        fabsf(ov->treble - 0.5f) < active_threshold &&
        ov->blend < active_threshold) {
        // Bypass - return dry signal
        float32x4x2_t bypass;
        bypass.val[0] = in_l;
        bypass.val[1] = in_r;
        return bypass;
    }

    // Save dry signal for blend
    float32x4_t dry_l = in_l;
    float32x4_t dry_r = in_r;

    // 1. Pre-drive EQ (bass/treble boost/cut)
    float32x4_t eq_l = baxandall_eq(in_l, &ov->bass_boost, ov->bass, ov->treble, sample_rate);
    float32x4_t eq_r = baxandall_eq(in_r, &ov->treble_boost, ov->bass, ov->treble, sample_rate);

    // 2. Tube saturation (dual stage)
    float32x4_t tube1_l = tube_saturate(eq_l, ov->drive);
    float32x4_t tube1_r = tube_saturate(eq_r, ov->drive);

    float32x4_t tube2_l = tube_saturate(tube1_l, ov->drive * 0.7f);
    float32x4_t tube2_r = tube_saturate(tube1_r, ov->drive * 0.7f);

    // 3. Presence control (high shelf boost)
    float32x4_t presence_gain = 0.0f + ov->presence * 12.0f;  // 0 to +12 dB
    float32x4_t presence_l = high_shelf_filter(tube2_l, &ov->presence, 5000.0f,
                                                presence_gain, 0.5f, sample_rate);
    float32x4_t presence_r = high_shelf_filter(tube2_r, &ov->presence, 5000.0f,
                                                presence_gain, 0.5f, sample_rate);

    // 4. Blend (parallel processing)
    float32x4_t wet_gain = vdupq_n_f32(ov->blend);
    float32x4_t dry_gain = vdupq_n_f32(1.0f - ov->blend);

    float32x4x2_t out;
    out.val[0] = vaddq_f32(vmulq_f32(dry_l, dry_gain), vmulq_f32(presence_l, wet_gain));
    out.val[1] = vaddq_f32(vmulq_f32(dry_r, dry_gain), vmulq_f32(presence_r, wet_gain));

    return out;
}
#pragma once

/**
 * @file engine_mapping.h
 * @brief Stateless parameter mapping helpers for Sonaglio
 *
 * This header intentionally does not cache any derived macro state.
 * Parameters stay in params[] and are projected directly into the
 * per-engine control surfaces inside the process path.
 */

#include <arm_neon.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

fast_inline float fm_clampf01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

fast_inline float fm_clamp_bipolar(float x) {
    if (x < -1.0f) return -1.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

fast_inline float fm_norm_from_percent(int8_t v) {
    return fm_clampf01((float)v * (1.0f / 100.0f));
}

fast_inline float fm_bipolar_from_percent(int8_t v) {
    return fm_clamp_bipolar((float)v * (1.0f / 100.0f));
}

fast_inline float32x4_t fm_vclamp01(float32x4_t x) {
    return vminq_f32(vmaxq_f32(x, vdupq_n_f32(0.0f)), vdupq_n_f32(1.0f));
}

fast_inline float32x4_t fm_make_transient_env(float32x4_t envelope, float32x4_t hit_shape) {
    // Sharper hit_shape increases the front edge and transient emphasis.
    float32x4_t a = vaddq_f32(vdupq_n_f32(0.75f), vmulq_n_f32(hit_shape, 0.50f));
    float32x4_t b = vaddq_f32(vdupq_n_f32(0.85f), vmulq_n_f32(hit_shape, 0.15f));
    return vmulq_f32(vmulq_f32(envelope, a), b);
}

fast_inline float32x4_t fm_make_body_env(float32x4_t envelope, float32x4_t body_tilt) {
    // Body tilt shifts energy toward the low-mid part of the decay.
    float32x4_t a = vaddq_f32(vdupq_n_f32(0.65f), vmulq_n_f32(body_tilt, 0.60f));
    float32x4_t b = vmulq_f32(vmulq_f32(envelope, envelope), vmulq_n_f32(body_tilt, 0.25f));
    return vaddq_f32(vmulq_f32(envelope, a), b);
}

fast_inline float32x4_t fm_make_drive_gain(float32x4_t drive) {
    return vaddq_f32(vdupq_n_f32(1.0f), vmulq_n_f32(drive, 0.85f));
}

fast_inline float32x4_t fm_soft_clip(float32x4_t x) {
    return vdivq_f32(x, vaddq_f32(vdupq_n_f32(1.0f), vabsq_f32(x)));
}

#ifdef __cplusplus
}
#endif

#pragma once

/**
 * @file engine_mapping.h
 * @brief Parameter-to-macro mapping layer for FM Percussion Synth
 *
 * This file is the single source of truth for parameter semantics.
 *
 * Design goals:
 * - 2 params per engine (Attack / Body)
 * - 3 global reclaimed controls (HitShape / BodyTilt / Drive)
 * - fixed 4-engine instrument identity
 * - no heap, no dynamic dispatch
 * - SIMD-friendly scalar-to-vector projection
 */

#include <arm_neon.h>
#include <stdint.h>

#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Normalized helpers                                                          */
/* -------------------------------------------------------------------------- */

fast_inline float fm_clampf01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

fast_inline float fm_norm_from_percent(int8_t v) {
    return fm_clampf01((float)v * (1.0f / 100.0f));
}

fast_inline float fm_bipolar_norm_from_percent(int8_t v) {
    float x = (float)v * (1.0f / 100.0f);
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    return x;
}

fast_inline float32x4_t fm_vdup_norm(float x) {
    return vdupq_n_f32(fm_clampf01(x));
}

fast_inline float32x4_t fm_vdup_bipolar(float x) {
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    return vdupq_n_f32(x);
}

fast_inline float32x4_t fm_vclamp01(float32x4_t x) {
    return vminq_f32(vmaxq_f32(x, vdupq_n_f32(0.0f)), vdupq_n_f32(1.0f));
}

fast_inline float32x4_t fm_vclamp_bipolar(float32x4_t x) {
    return vminq_f32(vmaxq_f32(x, vdupq_n_f32(-1.0f)), vdupq_n_f32(1.0f));
}

/* -------------------------------------------------------------------------- */
/* Macro targets                                                               */
/* -------------------------------------------------------------------------- */

typedef struct {
    float32x4_t excitation_gain;
    float32x4_t attack_click;
    float32x4_t attack_brightness;
    float32x4_t pitch_drop_depth;
    float32x4_t fm_index_attack;
    float32x4_t fm_index_body;
    float32x4_t noise_amount;
    float32x4_t decay_scale;
    float32x4_t stability;
    float32x4_t dynamic_filter_open;
    float32x4_t drive_amount;
    float32x4_t ratio_bias;
    float32x4_t variation_bias;
} fm_engine_macro_t;

typedef struct {
    float32x4_t hit_shape;   // global transient hardening
    float32x4_t body_tilt;   // global low-mid weight
    float32x4_t drive;       // global saturation / grit

    fm_engine_macro_t kick;
    fm_engine_macro_t snare;
    fm_engine_macro_t metal;
    fm_engine_macro_t perc;
} fm_engine_macros_t;

/* -------------------------------------------------------------------------- */
/* Default values                                                              */
/* -------------------------------------------------------------------------- */

fast_inline fm_engine_macro_t fm_engine_macro_default(void) {
    fm_engine_macro_t m;
    m.excitation_gain = vdupq_n_f32(0.5f);
    m.attack_click = vdupq_n_f32(0.25f);
    m.attack_brightness = vdupq_n_f32(0.25f);
    m.pitch_drop_depth = vdupq_n_f32(0.25f);
    m.fm_index_attack = vdupq_n_f32(0.5f);
    m.fm_index_body = vdupq_n_f32(0.5f);
    m.noise_amount = vdupq_n_f32(0.1f);
    m.decay_scale = vdupq_n_f32(0.5f);
    m.stability = vdupq_n_f32(0.75f);
    m.dynamic_filter_open = vdupq_n_f32(0.25f);
    m.drive_amount = vdupq_n_f32(0.1f);
    m.ratio_bias = vdupq_n_f32(0.25f);
    m.variation_bias = vdupq_n_f32(0.25f);
    return m;
}

fast_inline fm_engine_macros_t fm_engine_macros_default(void) {
    fm_engine_macros_t bus;
    bus.hit_shape = vdupq_n_f32(0.5f);
    bus.body_tilt = vdupq_n_f32(0.5f);
    bus.drive = vdupq_n_f32(0.5f);
    bus.kick = fm_engine_macro_default();
    bus.snare = fm_engine_macro_default();
    bus.metal = fm_engine_macro_default();
    bus.perc = fm_engine_macro_default();
    return bus;
}

/* -------------------------------------------------------------------------- */
/* Engine-specific mapping                                                     */
/* -------------------------------------------------------------------------- */

fast_inline fm_engine_macro_t fm_map_kick(float p1, float p2, float hit_shape, float body_tilt, float drive) {
    const float32x4_t v_hit = vdupq_n_f32(hit_shape);
    const float32x4_t v_body = vdupq_n_f32(body_tilt);
    const float32x4_t v_drive = vdupq_n_f32(drive);
    const float32x4_t vp1 = vdupq_n_f32(fm_clampf01(p1));
    const float32x4_t vp2 = vdupq_n_f32(fm_clampf01(p2));

    fm_engine_macro_t m;
    m.excitation_gain = vaddq_f32(vdupq_n_f32(0.55f), vmulq_n_f32(vp1, 0.70f));
    m.excitation_gain = vaddq_f32(m.excitation_gain, vmulq_n_f32(v_body, 0.10f));

    m.attack_click = vmulq_f32(vmulq_f32(vp1, vp1), vaddq_f32(vdupq_n_f32(0.4f), vmulq_n_f32(v_hit, 0.6f)));
    m.attack_brightness = vmulq_f32(vp1, vaddq_f32(vdupq_n_f32(0.5f), vmulq_n_f32(v_hit, 0.5f)));
    m.pitch_drop_depth = vaddq_f32(vdupq_n_f32(0.15f), vaddq_f32(vmulq_n_f32(vp2, 0.55f), vmulq_n_f32(v_body, 0.20f)));
    m.fm_index_attack = vaddq_f32(vdupq_n_f32(0.20f), vaddq_f32(vmulq_n_f32(vp1, 1.20f), vmulq_n_f32(v_drive, 0.30f)));
    m.fm_index_body = vaddq_f32(vdupq_n_f32(0.05f), vaddq_f32(vmulq_n_f32(vp2, 0.55f), vmulq_n_f32(v_body, 0.15f)));
    m.noise_amount = vdupq_n_f32(0.0f);
    m.decay_scale = vaddq_f32(vdupq_n_f32(0.60f), vaddq_f32(vmulq_n_f32(vp2, 0.70f), vmulq_n_f32(v_body, 0.20f)));
    m.stability = vsubq_f32(vdupq_n_f32(0.90f), vmulq_n_f32(vp1, 0.20f));
    m.dynamic_filter_open = vaddq_f32(vdupq_n_f32(0.20f), vaddq_f32(vmulq_n_f32(vp1, 0.60f), vmulq_n_f32(v_hit, 0.20f)));
    m.drive_amount = vaddq_f32(vdupq_n_f32(0.10f), vaddq_f32(vmulq_n_f32(v_drive, 0.35f), vmulq_n_f32(vp1, 0.05f)));
    m.ratio_bias = vaddq_f32(vdupq_n_f32(0.15f), vmulq_n_f32(vp2, 0.35f));
    m.variation_bias = vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(vp1, 0.50f));
    return m;
}

fast_inline fm_engine_macro_t fm_map_snare(float p1, float p2, float hit_shape, float body_tilt, float drive) {
    const float32x4_t v_hit = vdupq_n_f32(hit_shape);
    const float32x4_t v_body = vdupq_n_f32(body_tilt);
    const float32x4_t v_drive = vdupq_n_f32(drive);
    const float32x4_t vp1 = vdupq_n_f32(fm_clampf01(p1));
    const float32x4_t vp2 = vdupq_n_f32(fm_clampf01(p2));

    fm_engine_macro_t m;
    m.excitation_gain = vaddq_f32(vdupq_n_f32(0.45f), vaddq_f32(vmulq_n_f32(vp1, 0.55f), vmulq_n_f32(vdupq_n_f32(0.20f), vdupq_n_f32(1.0f))));
    m.excitation_gain = vaddq_f32(m.excitation_gain, vmulq_n_f32(v_body, 0.08f));

    m.attack_click = vmulq_f32(vmulq_f32(vp1, vp1), vaddq_f32(vdupq_n_f32(0.35f), vmulq_n_f32(v_hit, 0.65f)));
    m.attack_brightness = vaddq_f32(vdupq_n_f32(0.35f), vmulq_n_f32(vp1, 0.65f));
    m.pitch_drop_depth = vaddq_f32(vdupq_n_f32(0.08f), vaddq_f32(vmulq_n_f32(vp2, 0.25f), vmulq_n_f32(v_body, 0.15f)));
    m.fm_index_attack = vaddq_f32(vdupq_n_f32(0.35f), vaddq_f32(vmulq_n_f32(vp1, 1.10f), vmulq_n_f32(v_drive, 0.20f)));
    m.fm_index_body = vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(vp2, 0.70f));
    m.noise_amount = vaddq_f32(vdupq_n_f32(0.15f), vmulq_n_f32(vmulq_f32(vp1, vp1), 0.85f));
    m.decay_scale = vaddq_f32(vdupq_n_f32(0.45f), vaddq_f32(vmulq_n_f32(vp2, 0.60f), vmulq_n_f32(v_body, 0.15f)));
    m.stability = vsubq_f32(vdupq_n_f32(0.85f), vmulq_n_f32(vp1, 0.30f));
    m.dynamic_filter_open = vaddq_f32(vdupq_n_f32(0.30f), vaddq_f32(vmulq_n_f32(vp1, 0.50f), vmulq_n_f32(v_hit, 0.20f)));
    m.drive_amount = vaddq_f32(vdupq_n_f32(0.10f), vaddq_f32(vmulq_n_f32(v_drive, 0.40f), vmulq_n_f32(vp1, 0.10f)));
    m.ratio_bias = vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(vp2, 0.30f));
    m.variation_bias = vaddq_f32(vdupq_n_f32(0.25f), vmulq_n_f32(vp1, 0.45f));
    return m;
}

fast_inline fm_engine_macro_t fm_map_metal(float p1, float p2, float hit_shape, float body_tilt, float drive) {
    const float32x4_t v_hit = vdupq_n_f32(hit_shape);
    const float32x4_t v_body = vdupq_n_f32(body_tilt);
    const float32x4_t v_drive = vdupq_n_f32(drive);
    const float32x4_t vp1 = vdupq_n_f32(fm_clampf01(p1));
    const float32x4_t vp2 = vdupq_n_f32(fm_clampf01(p2));

    fm_engine_macro_t m;
    m.excitation_gain = vaddq_f32(vdupq_n_f32(0.40f), vaddq_f32(vmulq_n_f32(vp1, 0.65f), vmulq_n_f32(vdupq_n_f32(0.20f), vdupq_n_f32(1.0f))));
    m.excitation_gain = vaddq_f32(m.excitation_gain, vmulq_n_f32(v_body, 0.08f));

    m.attack_click = vmulq_f32(vmulq_f32(vp1, vp1), vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(v_hit, 0.60f)));
    m.attack_brightness = vaddq_f32(vdupq_n_f32(0.45f), vaddq_f32(vmulq_n_f32(vp1, 0.55f), vmulq_n_f32(v_drive, 0.10f)));
    m.pitch_drop_depth = vaddq_f32(vdupq_n_f32(0.05f), vaddq_f32(vmulq_n_f32(vp2, 0.15f), vmulq_n_f32(v_body, 0.10f)));
    m.fm_index_attack = vaddq_f32(vdupq_n_f32(0.40f), vaddq_f32(vmulq_n_f32(vp1, 1.30f), vmulq_n_f32(v_drive, 0.25f)));
    m.fm_index_body = vaddq_f32(vdupq_n_f32(0.25f), vmulq_n_f32(vp2, 0.65f));
    m.noise_amount = vaddq_f32(vdupq_n_f32(0.05f), vmulq_n_f32(vp1, 0.20f));
    m.decay_scale = vaddq_f32(vdupq_n_f32(0.40f), vmulq_n_f32(vp2, 0.80f));
    m.stability = vaddq_f32(vdupq_n_f32(0.55f), vsubq_f32(vmulq_n_f32(vp2, 0.10f), vmulq_n_f32(vp1, 0.40f)));
    m.dynamic_filter_open = vaddq_f32(vdupq_n_f32(0.35f), vaddq_f32(vmulq_n_f32(vp1, 0.55f), vmulq_n_f32(v_hit, 0.10f)));
    m.drive_amount = vaddq_f32(vdupq_n_f32(0.08f), vaddq_f32(vmulq_n_f32(v_drive, 0.35f), vmulq_n_f32(vp1, 0.10f)));
    m.ratio_bias = vaddq_f32(vdupq_n_f32(0.10f), vmulq_n_f32(vp1, 0.60f));
    m.variation_bias = vaddq_f32(vdupq_n_f32(0.15f), vmulq_n_f32(vp2, 0.45f));
    return m;
}

fast_inline fm_engine_macro_t fm_map_perc(float p1, float p2, float hit_shape, float body_tilt, float drive) {
    const float32x4_t v_hit = vdupq_n_f32(hit_shape);
    const float32x4_t v_body = vdupq_n_f32(body_tilt);
    const float32x4_t v_drive = vdupq_n_f32(drive);
    const float32x4_t vp1 = vdupq_n_f32(fm_clampf01(p1));
    const float32x4_t vp2 = vdupq_n_f32(fm_clampf01(p2));

    fm_engine_macro_t m;
    m.excitation_gain = vaddq_f32(vdupq_n_f32(0.45f), vaddq_f32(vmulq_n_f32(vp1, 0.60f), vmulq_n_f32(vdupq_n_f32(0.20f), vdupq_n_f32(1.0f))));
    m.excitation_gain = vaddq_f32(m.excitation_gain, vmulq_n_f32(v_body, 0.08f));

    m.attack_click = vmulq_f32(vmulq_f32(vp1, vp1), vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(v_hit, 0.70f)));
    m.attack_brightness = vaddq_f32(vdupq_n_f32(0.25f), vaddq_f32(vmulq_n_f32(vp1, 0.60f), vmulq_n_f32(v_hit, 0.15f)));
    m.pitch_drop_depth = vaddq_f32(vdupq_n_f32(0.12f), vaddq_f32(vmulq_n_f32(vp2, 0.45f), vmulq_n_f32(v_body, 0.20f)));
    m.fm_index_attack = vaddq_f32(vdupq_n_f32(0.25f), vaddq_f32(vmulq_n_f32(vp1, 1.00f), vmulq_n_f32(v_drive, 0.20f)));
    m.fm_index_body = vaddq_f32(vdupq_n_f32(0.10f), vaddq_f32(vmulq_n_f32(vp2, 0.75f), vmulq_n_f32(v_body, 0.15f)));
    m.noise_amount = vaddq_f32(vdupq_n_f32(0.02f), vmulq_n_f32(vp1, 0.12f));
    m.decay_scale = vaddq_f32(vdupq_n_f32(0.45f), vaddq_f32(vmulq_n_f32(vp2, 0.65f), vmulq_n_f32(v_body, 0.20f)));
    m.stability = vaddq_n_f32(vsubq_f32(vdupq_n_f32(0.92f), vmulq_n_f32(vp1, 0.25f)), vmulq_n_f32(vp2, 0.10f));
    m.dynamic_filter_open = vaddq_f32(vdupq_n_f32(0.20f), vaddq_f32(vmulq_n_f32(vp1, 0.50f), vmulq_n_f32(v_hit, 0.15f)));
    m.drive_amount = vaddq_f32(vdupq_n_f32(0.10f), vaddq_f32(vmulq_n_f32(v_drive, 0.30f), vmulq_n_f32(vp1, 0.10f)));
    m.ratio_bias = vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(vp2, 0.50f));
    m.variation_bias = vaddq_f32(vdupq_n_f32(0.20f), vmulq_n_f32(vp1, 0.60f));
    return m;
}

/* -------------------------------------------------------------------------- */
/* Bus refresh                                                                 */
/* -------------------------------------------------------------------------- */

fast_inline void fm_engine_macros_set_global(fm_engine_macros_t* bus,
                                             float hit_shape,
                                             float body_tilt,
                                             float drive) {
    bus->hit_shape = fm_vdup_norm(hit_shape);
    bus->body_tilt = fm_vdup_norm(body_tilt);
    bus->drive = fm_vdup_norm(drive);
}

fast_inline void fm_engine_macros_set_kick(fm_engine_macros_t* bus,
                                           float p1,
                                           float p2,
                                           float32x4_t velocity) {
    (void)velocity;
    bus->kick = fm_map_kick(p1, p2,
                            vgetq_lane_f32(bus->hit_shape, 0),
                            vgetq_lane_f32(bus->body_tilt, 0),
                            vgetq_lane_f32(bus->drive, 0));
}

fast_inline void fm_engine_macros_set_snare(fm_engine_macros_t* bus,
                                            float p1,
                                            float p2,
                                            float32x4_t velocity) {
    (void)velocity;
    bus->snare = fm_map_snare(p1, p2,
                              vgetq_lane_f32(bus->hit_shape, 0),
                              vgetq_lane_f32(bus->body_tilt, 0),
                              vgetq_lane_f32(bus->drive, 0));
}

fast_inline void fm_engine_macros_set_metal(fm_engine_macros_t* bus,
                                            float p1,
                                            float p2,
                                            float32x4_t velocity) {
    (void)velocity;
    bus->metal = fm_map_metal(p1, p2,
                              vgetq_lane_f32(bus->hit_shape, 0),
                              vgetq_lane_f32(bus->body_tilt, 0),
                              vgetq_lane_f32(bus->drive, 0));
}

fast_inline void fm_engine_macros_set_perc(fm_engine_macros_t* bus,
                                           float p1,
                                           float p2,
                                           float32x4_t velocity) {
    (void)velocity;
    bus->perc = fm_map_perc(p1, p2,
                            vgetq_lane_f32(bus->hit_shape, 0),
                            vgetq_lane_f32(bus->body_tilt, 0),
                            vgetq_lane_f32(bus->drive, 0));
}

#ifdef __cplusplus
}
#endif

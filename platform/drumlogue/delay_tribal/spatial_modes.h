#pragma once
/**
 * @file spatial_modes.h
 * @brief Spatial mode definitions for the percussion ensemble spatializer.
 *
 * The effect is intentionally not a classic chorus. It is a micro-ensemble
 * model that turns one percussion hit into a small group of staggered players.
 *
 * Each mode defines:
 * - clone placement
 * - pan law
 * - stereo scatter behavior
 * - spectral profile
 */

#include <cstdint>

typedef enum {
    MODE_TRIBAL   = 0,  // circular / ring-like
    MODE_MILITARY = 1,  // linear / row-like
    MODE_ANGEL    = 2,  // scattered / diffuse
    MODE_COUNT    = 3,
} spatial_mode_t;

typedef enum {
    PAN_MODEL_CIRCLE  = 0,
    PAN_MODEL_LINE    = 1,
    PAN_MODEL_SCATTER = 2,
} pan_model_t;

enum {
    CLONE_MIN = 2,
    CLONE_MAX = 6,
    CLONE_DEFAULT = 4,
};

typedef struct {
    float delay_ms[CLONE_MAX];      // base onset for each follower
    float gain[CLONE_MAX];          // per-clone level
    float pan_x[CLONE_MAX];         // pan positions in [-1,1]
    float pan_l[CLONE_MAX];         // per-clone left gain
    float pan_r[CLONE_MAX];         // per-clone right gain
    float hp_hz;                    // mode low-cut floor
    float lp_hz;                    // mode high-cut ceiling
    float jitter_ms;                // per-hit random timing variation
    float attack_soften;            // 0..1 softens later clones
    float spread;                   // stereo width 0..1
    float wobble_ms;                // micro detune / timing wobble
    float pan_exponent;             // mode-specific pan law
    float scatter_amount;           // mode-specific spatial randomness
    pan_model_t pan_model;          // circle / line / scatter
} spatial_profile_t;

static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float signf(float x) {
    return (x > 0.0f) - (x < 0.0f);
}

#pragma once
/**
 * @file spatial_modes.h
 * @brief Spatial mode definitions for the percussion ensemble spatializer.
 *
 * This effect is deliberately not a chorus. It is a micro-ensemble model:
 * one leader hit plus staggered, darker, quieter followers.
 *
 * Clone counts are quantized to a five-step family:
 *   2, 4, 6, 8, 10
 *
 * Scatter is a dedicated detachment control:
 * it increases time jitter, spatial randomness, and follower looseness.
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

static constexpr int kCloneValues[5] = { 2, 4, 6, 8, 10 };

enum {
    CLONE_SET_2   = 0,
    CLONE_SET_4   = 1,
    CLONE_SET_6   = 2,
    CLONE_SET_8   = 3,
    CLONE_SET_10  = 4,
    CLONE_SET_CNT = 5,
};

typedef struct {
    float delay_ms[10];
    float gain[10];
    float pan_x[10];
    float pan_l[10];
    float pan_r[10];
    float hp_hz;
    float lp_hz;
    float jitter_ms;
    float attack_soften;
    float spread;
    float wobble_ms;
    float pan_exponent;
    float scatter_amount;
    pan_model_t pan_model;
} spatial_profile_t;

static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

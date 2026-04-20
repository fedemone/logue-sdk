#ifndef ENGINE_MAPPING_H
#define ENGINE_MAPPING_H

/**
 * @file engine_mapping.h
 * @brief Parameter-to-behavior mapping for the 4-engine FM percussion synth.
 *
 * This layer defines the semantic control contract used by the instrument.
 * It is intentionally scalar, deterministic, and easy to test.
 *
 * The runtime can broadcast the resulting values into NEON lanes as needed.
 */

#include <arm_neon.h>
#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef ENGINE_MAPPING_INLINE
#define ENGINE_MAPPING_INLINE static inline
#endif

typedef enum {
    ENGINE_KICK = 0,
    ENGINE_SNARE = 1,
    ENGINE_METAL = 2,
    ENGINE_PERC = 3,
    ENGINE_COUNT = 4
} engine_id_t;

typedef struct {
    float p1;       /* Engine Param1 in normalized range 0..1 */
    float p2;       /* Engine Param2 in normalized range 0..1 */
    float velocity; /* Note velocity in normalized range 0..1 */
} engine_input_t;

typedef struct {
    float hit_shape;   /* 0..1, transient hardness */
    float body_tilt;   /* 0..1, low-mid weight */
    float drive;       /* 0..1, nonlinear aggression */
} global_controls_t;

typedef struct {
    float excitation_gain;
    float attack_brightness;
    float attack_click;
    float pitch_drop_depth;
    float fm_index_attack;
    float fm_index_body;
    float noise_amount;
    float decay_scale;
    float stability;
    float dynamic_filter_open;
    float drive_amount;

    /* Engine-specific extras used by selected engines */
    float inharmonic_spread; /* Metal */
    float ring_density;      /* Metal */
    float ratio_bias;        /* Perc */
    float variation_bias;    /* Perc */
} engine_macro_targets_t;

typedef struct {
    global_controls_t global;
    engine_macro_targets_t kick;
    engine_macro_targets_t snare;
    engine_macro_targets_t metal;
    engine_macro_targets_t perc;
} mapping_result_t;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

ENGINE_MAPPING_INLINE float mapping_clamp01f(float x) {
    return (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
}

ENGINE_MAPPING_INLINE float mapping_lerp(float a, float b, float t) {
    return a + (b - a) * mapping_clamp01f(t);
}

ENGINE_MAPPING_INLINE float mapping_sq(float x) {
    return x * x;
}

ENGINE_MAPPING_INLINE float mapping_pow4(float x) {
    float x2 = x * x;
    return x2 * x2;
}

ENGINE_MAPPING_INLINE float mapping_pow8(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x4 * x4;
}

ENGINE_MAPPING_INLINE float mapping_norm_percent(int8_t v) {
    /* Parameters are stored as signed bytes in the project, but sound controls
     * are interpreted as 0..100. Values outside the range are clamped. */
    int iv = (int)v;
    if (iv < 0) iv = 0;
    if (iv > 100) iv = 100;
    return (float)iv * (1.0f / 100.0f);
}

ENGINE_MAPPING_INLINE float mapping_attack_exp(float hit_shape) {
    /* 4..8 is a practical range for percussive transients. */
    return 4.0f + 4.0f * mapping_clamp01f(hit_shape);
}

ENGINE_MAPPING_INLINE float mapping_attack_env(float env, float hit_shape) {
    /* Shape the transient more aggressively as HitShape increases. */
    float e = mapping_clamp01f(env);
    float exp = mapping_attack_exp(hit_shape);
    if (exp <= 4.25f) {
        return mapping_pow4(e);
    }
    if (exp <= 6.0f) {
        float e2 = e * e;
        return e2 * e2; /* env^4 */
    }
    /* env^8 */
    return mapping_pow8(e);
}

ENGINE_MAPPING_INLINE float mapping_body_env(float env) {
    float e = mapping_clamp01f(env);
    return e * e;
}

ENGINE_MAPPING_INLINE float mapping_tail_env(float env) {
    return mapping_clamp01f(env);
}

ENGINE_MAPPING_INLINE global_controls_t mapping_compute_globals(float hit_shape,
                                                                float body_tilt,
                                                                float drive) {
    global_controls_t g;
    g.hit_shape = mapping_clamp01f(hit_shape);
    g.body_tilt = mapping_clamp01f(body_tilt);
    g.drive = mapping_clamp01f(drive);
    return g;
}

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_zero_targets(void) {
    engine_macro_targets_t t;
    t.excitation_gain = 0.0f;
    t.attack_brightness = 0.0f;
    t.attack_click = 0.0f;
    t.pitch_drop_depth = 0.0f;
    t.fm_index_attack = 0.0f;
    t.fm_index_body = 0.0f;
    t.noise_amount = 0.0f;
    t.decay_scale = 0.0f;
    t.stability = 0.0f;
    t.dynamic_filter_open = 0.0f;
    t.drive_amount = 0.0f;
    t.inharmonic_spread = 0.0f;
    t.ring_density = 0.0f;
    t.ratio_bias = 0.0f;
    t.variation_bias = 0.0f;
    return t;
}

/* -------------------------------------------------------------------------
 * Engine mappings
 * ------------------------------------------------------------------------- */

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_compute_kick(float p1,
                                                                   float p2,
                                                                   const global_controls_t *g,
                                                                   float velocity) {
    engine_macro_targets_t t = mapping_zero_targets();
    float x = mapping_clamp01f(p1);
    float y = mapping_clamp01f(p2);
    float v = mapping_clamp01f(velocity);

    t.excitation_gain   = 0.55f + 0.70f * x + 0.25f * v;
    t.attack_click      = mapping_sq(x) * (0.40f + 0.60f * g->hit_shape);
    t.attack_brightness = x * (0.50f + 0.50f * g->hit_shape);
    t.pitch_drop_depth  = 0.15f + 0.55f * y + 0.20f * g->body_tilt;
    t.fm_index_attack   = 0.20f + 1.20f * x + 0.30f * g->drive;
    t.fm_index_body     = 0.05f + 0.55f * y + 0.15f * g->body_tilt;
    t.decay_scale       = 0.60f + 0.70f * y + 0.20f * g->body_tilt;
    t.stability         = 0.90f - 0.20f * x;
    t.dynamic_filter_open = 0.20f + 0.60f * x + 0.20f * g->hit_shape;
    t.drive_amount      = 0.10f + 0.35f * g->drive + 0.15f * v;
    return t;
}

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_compute_snare(float p1,
                                                                    float p2,
                                                                    const global_controls_t *g,
                                                                    float velocity) {
    engine_macro_targets_t t = mapping_zero_targets();
    float x = mapping_clamp01f(p1);
    float y = mapping_clamp01f(p2);
    float v = mapping_clamp01f(velocity);

    t.excitation_gain   = 0.45f + 0.55f * x + 0.20f * v;
    t.attack_click      = 0.25f + 0.75f * x * g->hit_shape;
    t.attack_brightness = 0.35f + 0.65f * x;
    t.pitch_drop_depth  = 0.08f + 0.25f * y + 0.15f * g->body_tilt;
    t.fm_index_attack   = 0.35f + 1.10f * x + 0.20f * g->drive;
    t.fm_index_body     = 0.20f + 0.70f * y;
    t.noise_amount      = 0.15f + 0.85f * mapping_sq(x);
    t.decay_scale       = 0.45f + 0.60f * y + 0.15f * g->body_tilt;
    t.stability         = 0.85f - 0.30f * x;
    t.dynamic_filter_open = 0.30f + 0.50f * x + 0.20f * g->hit_shape;
    t.drive_amount      = 0.10f + 0.40f * g->drive + 0.10f * x;
    return t;
}

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_compute_metal(float p1,
                                                                    float p2,
                                                                    const global_controls_t *g,
                                                                    float velocity) {
    engine_macro_targets_t t = mapping_zero_targets();
    float x = mapping_clamp01f(p1);
    float y = mapping_clamp01f(p2);
    float v = mapping_clamp01f(velocity);

    t.excitation_gain   = 0.40f + 0.65f * x + 0.20f * v;
    t.attack_click      = 0.20f + 0.60f * x * g->hit_shape;
    t.attack_brightness = 0.45f + 0.55f * x + 0.10f * g->drive;
    t.fm_index_attack   = 0.40f + 1.30f * x + 0.25f * g->drive;
    t.fm_index_body     = 0.25f + 0.65f * y;
    t.noise_amount      = 0.05f + 0.20f * x;
    t.decay_scale       = 0.40f + 0.80f * y;
    t.stability         = 0.95f - 0.40f * x + 0.10f * y;
    t.dynamic_filter_open = 0.35f + 0.55f * x + 0.10f * g->hit_shape;
    t.drive_amount      = 0.08f + 0.35f * g->drive + 0.10f * x;
    t.inharmonic_spread  = 0.20f + 0.60f * x + 0.15f * g->drive;
    t.ring_density       = 0.20f + 0.75f * y;
    return t;
}

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_compute_perc(float p1,
                                                                   float p2,
                                                                   const global_controls_t *g,
                                                                   float velocity) {
    engine_macro_targets_t t = mapping_zero_targets();
    float x = mapping_clamp01f(p1);
    float y = mapping_clamp01f(p2);
    float v = mapping_clamp01f(velocity);

    t.excitation_gain   = 0.45f + 0.60f * x + 0.20f * v;
    t.attack_click      = 0.20f + 0.70f * x * g->hit_shape;
    t.attack_brightness = 0.25f + 0.60f * x + 0.15f * g->hit_shape;
    t.pitch_drop_depth  = 0.12f + 0.45f * y + 0.20f * g->body_tilt;
    t.fm_index_attack   = 0.25f + 1.00f * x + 0.20f * g->drive;
    t.fm_index_body     = 0.10f + 0.75f * y + 0.15f * g->body_tilt;
    t.noise_amount      = 0.02f + 0.12f * x;
    t.decay_scale       = 0.45f + 0.65f * y + 0.20f * g->body_tilt;
    t.stability         = 0.92f - 0.25f * x + 0.10f * y;
    t.dynamic_filter_open = 0.20f + 0.50f * x + 0.15f * g->hit_shape;
    t.drive_amount      = 0.10f + 0.30f * g->drive + 0.10f * v;
    t.ratio_bias        = 0.25f + 0.50f * y;
    t.variation_bias    = 0.20f + 0.60f * x;
    return t;
}

ENGINE_MAPPING_INLINE engine_macro_targets_t mapping_compute_engine(engine_id_t engine,
                                                                    float p1,
                                                                    float p2,
                                                                    const global_controls_t *g,
                                                                    float velocity) {
    switch (engine) {
        case ENGINE_KICK:  return mapping_compute_kick(p1, p2, g, velocity);
        case ENGINE_SNARE: return mapping_compute_snare(p1, p2, g, velocity);
        case ENGINE_METAL: return mapping_compute_metal(p1, p2, g, velocity);
        case ENGINE_PERC:  return mapping_compute_perc(p1, p2, g, velocity);
        default:           return mapping_zero_targets();
    }
}

ENGINE_MAPPING_INLINE void mapping_compute_all(mapping_result_t *out,
                                               const engine_input_t inputs[ENGINE_COUNT],
                                               float hit_shape,
                                               float body_tilt,
                                               float drive) {
    global_controls_t g = mapping_compute_globals(hit_shape, body_tilt, drive);
    out->global = g;
    out->kick  = mapping_compute_kick(inputs[ENGINE_KICK].p1,  inputs[ENGINE_KICK].p2,  &g, inputs[ENGINE_KICK].velocity);
    out->snare = mapping_compute_snare(inputs[ENGINE_SNARE].p1, inputs[ENGINE_SNARE].p2, &g, inputs[ENGINE_SNARE].velocity);
    out->metal = mapping_compute_metal(inputs[ENGINE_METAL].p1, inputs[ENGINE_METAL].p2, &g, inputs[ENGINE_METAL].velocity);
    out->perc  = mapping_compute_perc(inputs[ENGINE_PERC].p1,  inputs[ENGINE_PERC].p2,  &g, inputs[ENGINE_PERC].velocity);
}

/* -------------------------------------------------------------------------
 * NEON helper for broadcasting scalar targets
 * ------------------------------------------------------------------------- */

ENGINE_MAPPING_INLINE float32x4_t mapping_broadcast4(float x) {
    return vdupq_n_f32(x);
}

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_MAPPING_H */

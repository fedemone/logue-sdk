#pragma once

/**
 * @file constants.h
 * @brief Active constants for Sonaglio
 *
 * Trimmed to the fixed 4-engine instrument model.
 * Legacy aliases are kept where useful to ease incremental cleanup.
 */

#ifdef __cplusplus
#include <cstdint>
#include <cmath>
#else
#include <stdint.h>
#include <math.h>
#define constexpr static const
#endif

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline))
#endif

// ============================================================================
// Core audio constants
// ============================================================================
constexpr int   NEON_LANES     = 4;
constexpr int   VECTOR_ALIGN    = 16;
constexpr int   CACHE_LINE_SIZE = 64;
constexpr float SAMPLE_RATE     = 48000.0f;
constexpr float INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;
constexpr float NYQUIST_FREQ    = SAMPLE_RATE * 0.5f;

// ============================================================================
// MIDI / tuning constants
// ============================================================================
constexpr float A4_FREQ = 440.0f;
constexpr int   A4_MIDI  = 69;
constexpr float SEMITONE_RATIO = 1.0594630943592953f;
constexpr float LFO_PHASE_OFFSET = 0.25f;

// ============================================================================
// Parameter indexes — 24 controls
// ============================================================================
constexpr uint8_t PARAM_INSTRUMENT = 0;
constexpr uint8_t PARAM_BLEND = 1;
constexpr uint8_t PARAM_GAP = 2;
constexpr uint8_t PARAM_SCATTER = 3;

constexpr uint8_t PARAM_KICK_ATK  = 4;
constexpr uint8_t PARAM_KICK_BODY = 5;
constexpr uint8_t PARAM_SNARE_ATK = 6;
constexpr uint8_t PARAM_SNARE_BODY = 7;
constexpr uint8_t PARAM_METAL_ATK  = 8;
constexpr uint8_t PARAM_METAL_BODY = 9;
constexpr uint8_t PARAM_PERC_ATK   = 10;
constexpr uint8_t PARAM_PERC_BODY  = 11;

constexpr uint8_t PARAM_LFO1_SHAPE  = 12;
constexpr uint8_t PARAM_LFO1_RATE   = 13;
constexpr uint8_t PARAM_LFO1_TARGET = 14;
constexpr uint8_t PARAM_LFO1_DEPTH  = 15;

constexpr uint8_t PARAM_EUCL_TUN    = 16;
constexpr uint8_t PARAM_LFO2_RATE   = 17;
constexpr uint8_t PARAM_LFO2_TARGET = 18;
constexpr uint8_t PARAM_LFO2_DEPTH  = 19;

constexpr uint8_t PARAM_ENV_SHAPE   = 20;
constexpr uint8_t PARAM_HIT_SHAPE   = 21;
constexpr uint8_t PARAM_BODY_TILT   = 22;
constexpr uint8_t PARAM_DRIVE       = 23;
constexpr uint8_t PARAM_TOTAL       = 24;

// ============================================================================
// LFO targets - TODO make this enum
// ============================================================================
constexpr int LFO_TARGET_NONE       = 0;
constexpr int LFO_TARGET_PITCH      = 1;
constexpr int LFO_TARGET_INDEX      = 2;
constexpr int LFO_TARGET_ENV        = 3;
constexpr int LFO_TARGET_LFO2_PHASE = 4;
constexpr int LFO_TARGET_LFO1_PHASE = 5;
constexpr int LFO_TARGET_RES_FREQ   = 6;
constexpr int LFO_TARGET_RESONANCE  = 7;
constexpr int LFO_TARGET_NOISE_MIX  = 8;
constexpr int LFO_TARGET_RES_MORPH  = 9;
constexpr int LFO_TARGET_METAL_GATE = 10;
constexpr int LFO_TARGET_COUNT      = 11;

// ============================================================================
// LFO shapes
// ============================================================================
constexpr int LFO_SHAPE_TRIANGLE = 0;
constexpr int LFO_SHAPE_RAMP     = 1;
constexpr int LFO_SHAPE_CHORD    = 2;
constexpr int LFO_SHAPE_COMBO_COUNT = 9;

// ============================================================================
// Euclidean tuning - TODO enum
// ============================================================================
constexpr int EUCLID_MODE_OFF   = 0;
constexpr int EUCLID_MODE_CLSTR = 1;
constexpr int EUCLID_MODE_MINOR = 2;
constexpr int EUCLID_MODE_DIATN = 3;
constexpr int EUCLID_MODE_WHOLE = 4;
constexpr int EUCLID_MODE_PENTA = 5;
constexpr int EUCLID_MODE_DIM7  = 6;
constexpr int EUCLID_MODE_AUG8  = 7;
constexpr int EUCLID_MODE_TRIT  = 8;
constexpr int EUCLID_MODE_COUNT = 9;

// ============================================================================
// Envelope ROM / states
// ============================================================================
constexpr int ENV_SHAPE_MIN     = 0;
constexpr int ENV_SHAPE_MAX     = 127;
constexpr int ENV_SHAPE_DEFAULT = 40;
constexpr int ENV_ROM_SIZE      = 128;
constexpr int ENV_STAGE_ATTACK  = 0;
constexpr int ENV_STAGE_DECAY   = 1;
constexpr int ENV_STAGE_RELEASE = 2;
constexpr int ENV_STATE_OFF     = 3;
constexpr int ENV_CURVE_LINEAR  = 0;
constexpr int ENV_CURVE_EXPONENTIAL = 1;

// ============================================================================
// PRNG / presets / smoothing
// ============================================================================
constexpr uint32_t PRNG_DEFAULT_SEED = 0x9E3779B9u;
constexpr uint32_t RAND_DEFAULT_SEED = 0x12345678u;
constexpr int NUM_OF_PRESETS = 26;
constexpr int NAME_LENGTH = 12;
constexpr int SMOOTH_FRAMES = 48;

// ============================================================================
// Legacy resonant constants retained for future work - TODO enum
// ============================================================================
constexpr int RES_MODE_LOWPASS  = 0;
constexpr int RES_MODE_BANDPASS = 1;
constexpr int RES_MODE_HIGHPASS = 2;
constexpr int RES_MODE_NOTCH    = 3;
constexpr int RES_MODE_PEAK     = 4;
constexpr int RES_MODE_COUNT    = 5;
constexpr float RES_FC_MIN = 50.0f;
constexpr float RES_FC_MAX = 8000.0f;
constexpr float RESONANT_DENOM_EPSILON = 1e-6f;

// Utility helpers
static inline float interval_ratio(int semitones) {
    return powf(2.0f, semitones / 12.0f);
}

static inline float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static inline float linear_to_db(float linear) {
    return 20.0f * log10f(linear);
}

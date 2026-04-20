#pragma once

/**
 * @file synth.h
 * @brief Top-level Drumlogue FM percussion synth controller.
 *
 * This file is intentionally structured as the control and routing layer:
 * - user parameter entry point (setParameter)
 * - preset loading
 * - voice/engine selection
 * - note handling
 * - per-sample process orchestration
 *
 * DSP details live in:
 * - engine_mapping.h (parameter semantics / macro targets)
 * - kick_engine.h / snare_engine.h / metal_engine.h / perc_engine.h
 * - future optional resonant project (kept out of the active path)
 *
 * Design rules:
 * - no heap allocation
 * - fixed 4-voice SIMD layout
 * - ARM NEON preferred
 * - deterministic real-time safe behavior
 */

#include <arm_neon.h>
#include <stdint.h>
#include <stddef.h>

#include "constants.h"
#include "prng.h"
#include "midi_handler.h"
#include "envelope_rom.h"
#include "lfo_enhanced.h"
#include "lfo_smoothing.h"
#include "fm_presets.h"
#include "engine_mapping.h"

#include "kick_engine.h"
#include "snare_engine.h"
#include "metal_engine.h"
#include "perc_engine.h"
#include "resonant_synthesis.h"   // kept in code, not part of the active 4-engine path

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Fixed instrument layout
// -----------------------------------------------------------------------------

typedef enum {
    ENGINE_KICK = 0,
    ENGINE_SNARE = 1,
    ENGINE_METAL = 2,
    ENGINE_PERC  = 3,
    ENGINE_RESONANT = 4,  // kept for future use; not called in current instrument path
    ENGINE_COUNT
} engine_type_t;

// Four voices, fixed positions.
// Voice allocation is removed from the UI/control budget.
//
typedef enum {
    VOICE_KICK = 0,
    VOICE_SNARE = 1,
    VOICE_METAL = 2,
    VOICE_PERC = 3,
    VOICE_COUNT = 4
} voice_pos_t;

// -----------------------------------------------------------------------------
// User parameters
// -----------------------------------------------------------------------------

// Active instrument controls (8 total)
// 2 per engine: Kick/Snare/Metal/Perc
//
// Reclaimed globals (3 total)
// - HitShape
// - BodyTilt
// - Drive
//
// Existing support controls remain here only if still useful in the host UI:
// - velocity/probability routing
// - euclidean tuning mode
// - LFO routing and envelope shape selection

typedef enum {
    PARAM_KICK_ATK = 0,
    PARAM_KICK_BODY,
    PARAM_SNARE_ATK,
    PARAM_SNARE_BODY,
    PARAM_METAL_ATK,
    PARAM_METAL_BODY,
    PARAM_PERC_ATK,
    PARAM_PERC_BODY,

    PARAM_HIT_SHAPE,   // global transient character
    PARAM_BODY_TILT,   // global low-mid weight
    PARAM_DRIVE,       // global nonlinear aggression

    // Optional retained controls (if UI needs them)
    PARAM_VOICE1_PROB,
    PARAM_VOICE2_PROB,
    PARAM_VOICE3_PROB,
    PARAM_VOICE4_PROB,
    PARAM_LFO1_RATE,
    PARAM_LFO1_DEPTH,
    PARAM_LFO1_TARGET,
    PARAM_LFO1_SHAPE,
    PARAM_LFO2_RATE,
    PARAM_LFO2_DEPTH,
    PARAM_LFO2_TARGET,
    PARAM_LFO2_SHAPE,
    PARAM_ENV_SHAPE,
    PARAM_PRESET,
    PARAM_NOTE_MODE,

    PARAM_TOTAL
} synth_param_id_t;

// -----------------------------------------------------------------------------
// Main synth state
// -----------------------------------------------------------------------------

typedef struct {
    // Engines
    kick_engine_t kick;
    snare_engine_t snare;
    metal_engine_t metal;
    perc_engine_t perc;
    resonant_synth_t resonant; // retained but unused in the active path

    // Control / timing
    neon_prng_t prng;
    midi_handler_t midi;
    neon_envelope_t envelope;
    lfo_enhanced_t lfo;
    lfo_smoother_t lfo_smooth;

    // Parameter cache (normalized UI values stored as integers where useful)
    int8_t params[PARAM_TOTAL];

    // Fixed voice layout: no runtime allocation table in this version.
    uint32x4_t voice_mask[VOICE_COUNT];

    // Voice probability and triggering
    uint32_t voice_probs[VOICE_COUNT];
    float32x4_t voice_velocity;
    uint32x4_t voice_triggered;

    // Global macros derived from current parameters.
    // These are the single source of truth for engine mapping.
    fm_engine_macros_t macros;

    // LFO outputs (smoothed)
    float32x4_t lfo_pitch_mult;
    float32x4_t lfo_index_add;

    // Envelope mode
    uint8_t current_env_shape;

    // Master gain
    float master_gain;

    // Runtime note assignment / tuning
    float32x4_t euclid_offsets;

    // Optional active mode flags
    uint8_t note_mode;
    uint8_t preset_index;
} fm_perc_synth_t;

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

fast_inline static float clampf01(float x) {
    return (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
}

fast_inline static float param_to_norm(int8_t v) {
    // user-facing params are typically stored as -100..100 or 0..100 depending on control
    // normalize defensively.
    return clampf01((float)v / 100.0f);
}

fast_inline static uint32x4_t all_voices_mask(void) {
    return vdupq_n_u32(0xFFFFFFFFu);
}

// -----------------------------------------------------------------------------
// Mapping layer refresh
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_refresh_mapping(fm_perc_synth_t *synth) {
    fm_engine_macros_t m = fm_engine_macros_default();

    const float kick_atk  = param_to_norm(synth->params[PARAM_KICK_ATK]);
    const float kick_body = param_to_norm(synth->params[PARAM_KICK_BODY]);
    const float snr_atk   = param_to_norm(synth->params[PARAM_SNARE_ATK]);
    const float snr_body  = param_to_norm(synth->params[PARAM_SNARE_BODY]);
    const float mtl_atk   = param_to_norm(synth->params[PARAM_METAL_ATK]);
    const float mtl_body  = param_to_norm(synth->params[PARAM_METAL_BODY]);
    const float prc_atk   = param_to_norm(synth->params[PARAM_PERC_ATK]);
    const float prc_body  = param_to_norm(synth->params[PARAM_PERC_BODY]);

    const float hit_shape = param_to_norm(synth->params[PARAM_HIT_SHAPE]);
    const float body_tilt = param_to_norm(synth->params[PARAM_BODY_TILT]);
    const float drive     = param_to_norm(synth->params[PARAM_DRIVE]);

    fm_engine_macros_set_global(&m, hit_shape, body_tilt, drive);

    fm_engine_macros_set_kick(&m,  kick_atk, kick_body, synth->voice_velocity);
    fm_engine_macros_set_snare(&m, snr_atk, snr_body, synth->voice_velocity);
    fm_engine_macros_set_metal(&m, mtl_atk, mtl_body, synth->voice_velocity);
    fm_engine_macros_set_perc(&m,  prc_atk, prc_body, synth->voice_velocity);

    synth->macros = m;
}

// -----------------------------------------------------------------------------
// Presets
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_load_preset(fm_perc_synth_t *synth, uint8_t index) {
    if (index >= NUM_OF_PRESETS) {
        index = 0;
    }

    synth->preset_index = index;
    load_fm_preset(index, synth->params);
    fm_perc_synth_refresh_mapping(synth);
}

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_init(fm_perc_synth_t *synth) {
    kick_engine_init(&synth->kick);
    snare_engine_init(&synth->snare);
    metal_engine_init(&synth->metal);
    perc_engine_init(&synth->perc);
    resonant_synth_init(&synth->resonant);

    neon_prng_init(&synth->prng, RAND_DEFAULT_SEED);
    midi_handler_init(&synth->midi);
    neon_envelope_init(&synth->envelope);
    lfo_enhanced_init(&synth->lfo);
    lfo_smoother_init(&synth->lfo_smooth);

    for (int i = 0; i < PARAM_TOTAL; ++i) {
        synth->params[i] = 0;
    }

    synth->master_gain = 0.25f;
    synth->current_env_shape = 0;
    synth->note_mode = 0;
    synth->preset_index = 0;
    synth->voice_velocity = vdupq_n_f32(1.0f);
    synth->voice_triggered = vdupq_n_u32(0);
    synth->euclid_offsets = vdupq_n_f32(0.0f);

    for (int i = 0; i < VOICE_COUNT; ++i) {
        synth->voice_mask[i] = vdupq_n_u32((i < VOICE_COUNT) ? 0xFFFFFFFFu : 0u);
    }

    // Default probabilities and defaults for the new macros.
    for (int i = 0; i < VOICE_COUNT; ++i) {
        synth->voice_probs[i] = 100;
    }

    fm_perc_synth_load_preset(synth, 0);
}

// -----------------------------------------------------------------------------
// Parameter entry point
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_setParameter(fm_perc_synth_t *synth,
                                            synth_param_id_t id,
                                            int8_t value) {
    if ((unsigned)id >= (unsigned)PARAM_TOTAL) {
        return;
    }

    synth->params[id] = value;

    // Keep this function as a routing/state function only.
    // Sound math happens in engine_mapping.h and the engines themselves.
    switch (id) {
        case PARAM_PRESET:
            fm_perc_synth_load_preset(synth, (uint8_t)value);
            break;

        case PARAM_ENV_SHAPE:
            synth->current_env_shape = (uint8_t)value;
            break;

        case PARAM_NOTE_MODE:
            synth->note_mode = (uint8_t)value;
            break;

        default:
            fm_perc_synth_refresh_mapping(synth);
            break;
    }
}

// -----------------------------------------------------------------------------
// Note handling
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_note_on(fm_perc_synth_t *synth,
                                       uint8_t note,
                                       uint8_t velocity) {
    (void)note;
    synth->voice_velocity = vdupq_n_f32((float)velocity / 127.0f);

    // The existing MIDI routing and per-voice probability policy can stay here.
    // NOTE: allocation is fixed now, so no voice remapping is needed.
    // TODO: apply Euclidean tuning offsets and fixed voice-note routing here.
    (void)synth;
}

fast_inline void fm_perc_synth_note_off(fm_perc_synth_t *synth,
                                        uint8_t note,
                                        uint8_t velocity) {
    (void)synth;
    (void)note;
    (void)velocity;
    // Intentionally empty for now.
    // Sustain/release policies will be designed per envelope family.
}

// -----------------------------------------------------------------------------
// Per-sample process
// -----------------------------------------------------------------------------

fast_inline float32x4_t fm_perc_synth_process(fm_perc_synth_t *synth) {
    // This is the orchestration layer only.
    // Engines consume the mapped macros and return 4-lane audio.

    // TODO:
    // - advance envelope
    // - advance LFOs
    // - apply transient/body/tail macro mapping
    // - call active engines
    // - sum outputs
    // - apply bus drive / final gain

    (void)synth;
    return vdupq_n_f32(0.0f);
}

// -----------------------------------------------------------------------------
// Placeholder / legacy functions intentionally kept empty or minimal
// -----------------------------------------------------------------------------

fast_inline void fm_perc_synth_update_params(fm_perc_synth_t *synth) {
    fm_perc_synth_refresh_mapping(synth);
}

fast_inline void fm_perc_synth_set_preset(fm_perc_synth_t *synth, uint8_t index) {
    fm_perc_synth_load_preset(synth, index);
}

fast_inline void fm_perc_synth_set_master_gain(fm_perc_synth_t *synth, float gain) {
    if (gain < 0.0f) gain = 0.0f;
    synth->master_gain = gain;
}

fast_inline void fm_perc_synth_reset(fm_perc_synth_t *synth) {
    fm_perc_synth_init(synth);
}

#ifdef __cplusplus
}
#endif

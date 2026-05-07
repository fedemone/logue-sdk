#pragma once

/**
 * @file fm_perc_synth_process.h
 * @brief Core Sonaglio synth state and rendering functions
 *
 * This is the active 4-engine implementation.
 * Legacy 5-engine allocation logic and resonant morphing are intentionally removed
 * from the live path.
 */

#include <arm_neon.h>
#include <stdint.h>
#include "constants.h"
#include "engine_mapping.h"
#include "fm_voices.h"
#include "kick_engine.h"
#include "snare_engine.h"
#include "metal_engine.h"
#include "perc_engine.h"
#include "lfo_enhanced.h"
#include "lfo_smoothing.h"
#include "envelope_rom.h"
#include "prng.h"
#include "midi_handler.h"
#include "fm_presets.h"

// ============================================================================
// Euclidean tuning offsets
// ============================================================================
// offsets[mode][voice] = semitones above root for that voice.
// Derived from E(4,n): position[i] = floor(i * n / 4), i = 0..3.
static const float EUCLID_OFFSETS[EUCLID_MODE_COUNT][4] = {
    { 0.f,  0.f,  0.f,  0.f},  // 0: Off
    { 0.f,  1.f,  2.f,  3.f},  // 1: E(4,4)
    { 0.f,  1.f,  3.f,  4.f},  // 2: E(4,6)
    { 0.f,  1.f,  3.f,  5.f},  // 3: E(4,7)
    { 0.f,  2.f,  4.f,  6.f},  // 4: E(4,8)
    { 0.f,  2.f,  5.f,  7.f},  // 5: E(4,10)
    { 0.f,  3.f,  6.f,  9.f},  // 6: E(4,12)
    { 0.f,  4.f,  8.f, 12.f},  // 7: E(4,16)
    { 0.f,  6.f, 12.f, 18.f},  // 8: E(4,24)
};

/**
 * single or combined instruments
 */
typedef enum {
    INST_KICK = 0,
    INST_SNARE,
    INST_TOM,
    INST_METAL,
    INST_KS,
    INST_KT,
    INST_KM,
    INST_ST,
    INST_SM,
    INST_TM,
    INST_COUNT
} sonaglio_instrument_t;

typedef struct {
    uint8_t  active;
    uint8_t  engine;
    uint8_t  midi_note;
    float    note_f;
    uint32_t delay_samples;
    float    gain;
} pending_trigger_t;

/**
 * Complete synthesizer state
 */
typedef struct {
    // PRNG
    neon_prng_t prng;

    // Fixed engines
    kick_engine_t  kick;
    snare_engine_t snare;
    metal_engine_t metal;
    perc_engine_t  perc;

    // LFO system
    lfo_enhanced_t lfo;
    lfo_smoother_t lfo_smooth;

    // Envelope
    neon_envelope_t envelope;
    uint8_t current_env_shape;

    // MIDI handler
    midi_handler_t midi;

    // User parameters
    int8_t params[PARAM_TOTAL];

    // New routing model
    uint8_t instrument_sel;                  // 0..9
    float blend;                             // 0..1
    float gap;                               // 0..1
    float scatter;                           // 0..1

    // Per-engine post-gain used by combo routing
    float engine_gain[ENGINE_COUNT];

    // Per-note bitmask: which engines were used for this note
    uint8_t note_engine_mask[128];

    // Small trigger queue so Gap can schedule delayed secondary hits
    pending_trigger_t pending[8];

    // Note tuning
    float32x4_t euclid_offsets;

    // Output gain
    float master_gain;
} fm_perc_synth_t;

// ============================================================================
// Utility helpers
// ============================================================================

fast_inline float neon_horizontal_sum(float32x4_t v) {
    float32x2_t sum_low = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
    float32x2_t sum_total = vpadd_f32(sum_low, sum_low);
    return vget_lane_f32(sum_total, 0);
}

fast_inline float neon_horizontal_sum_alt(float32x4_t v) {
#if defined(__aarch64__)
    return vaddvq_f32(v);
#else
    float32x2_t sum_low = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
    float32x2_t sum_total = vpadd_f32(sum_low, sum_low);
    return vget_lane_f32(sum_total, 0);
#endif
}
static constexpr uint32_t kDefaultAllLanes = 0xFFFFFFFFu;

static fast_inline uint32x4_t all_lanes_mask() {
    return vdupq_n_u32(kDefaultAllLanes);
}

static fast_inline void queue_trigger(fm_perc_synth_t* synth,
                                      uint8_t engine,
                                      uint8_t midi_note,
                                      float note_f,
                                      float gain,
                                      uint32_t delay_samples) {
    for (int i = 0; i < 8; ++i) {
        if (!synth->pending[i].active) {
            synth->pending[i].active = 1;
            synth->pending[i].engine = engine;
            synth->pending[i].midi_note = midi_note;
            synth->pending[i].note_f = note_f;
            synth->pending[i].gain = gain;
            synth->pending[i].delay_samples = delay_samples;
            return;
        }
    }

    // Fallback: overwrite slot 0 if the queue is full.
    synth->pending[0].active = 1;
    synth->pending[0].engine = engine;
    synth->pending[0].midi_note = midi_note;
    synth->pending[0].note_f = note_f;
    synth->pending[0].gain = gain;
    synth->pending[0].delay_samples = delay_samples;
}

static fast_inline void fire_engine(fm_perc_synth_t* synth,
                                    uint8_t engine,
                                    float note_f) {
    const uint32x4_t lane = all_lanes_mask();
    const float32x4_t note_vec = vdupq_n_f32(note_f);

    switch (engine) {
        case ENGINE_KICK:
            kick_engine_set_note(&synth->kick, lane, note_vec);
            break;
        case ENGINE_SNARE:
            snare_engine_set_note(&synth->snare, lane, note_vec);
            break;
        case ENGINE_METAL:
            metal_engine_set_note(&synth->metal, lane, note_vec);
            break;
        case ENGINE_PERC:
            perc_engine_set_note(&synth->perc, lane, note_vec);
            break;
        default:
            break;
    }
}

static fast_inline void process_pending_triggers(fm_perc_synth_t* synth) {
    for (int i = 0; i < 8; ++i) {
        pending_trigger_t& ev = synth->pending[i];
        if (!ev.active) continue;

        if (ev.delay_samples > 0) {
            --ev.delay_samples;
            continue;
        }

        synth->engine_gain[ev.engine] = ev.gain;
        fire_engine(synth, ev.engine, ev.note_f);
        ev.active = 0;
    }
}
// ============================================================================
// Parameter / preset handling
// ============================================================================

fast_inline void fm_perc_synth_update_params(fm_perc_synth_t* synth) {
    int8_t* p = synth->params;

    synth->instrument_sel = (uint8_t)(p[PARAM_INSTRUMENT] < 0 ? 0 :
                                      (p[PARAM_INSTRUMENT] >= INST_COUNT ? INST_COUNT - 1 : p[PARAM_INSTRUMENT]));
    synth->blend   = p[PARAM_BLEND]   * 0.01f;
    synth->gap     = p[PARAM_GAP]     * 0.01f;
    synth->scatter = p[PARAM_SCATTER] * 0.01f;

    kick_engine_update(&synth->kick,
                       vdupq_n_f32(p[PARAM_KICK_ATK]  * 0.01f),
                       vdupq_n_f32(p[PARAM_KICK_BODY] * 0.01f));

    snare_engine_update(&synth->snare,
                        vdupq_n_f32(p[PARAM_SNARE_ATK]  * 0.01f),
                        vdupq_n_f32(p[PARAM_SNARE_BODY] * 0.01f));

    metal_engine_update(&synth->metal,
                        vdupq_n_f32(p[PARAM_METAL_ATK]  * 0.01f),
                        vdupq_n_f32(p[PARAM_METAL_BODY] * 0.01f));

    perc_engine_update(&synth->perc,
                       vdupq_n_f32(p[PARAM_PERC_ATK]  * 0.01f),
                       vdupq_n_f32(p[PARAM_PERC_BODY] * 0.01f));

    synth->current_env_shape = (uint8_t)p[PARAM_ENV_SHAPE];

    metal_engine_set_character(&synth->metal,
                               (uint32_t)(synth->current_env_shape >> 7));

    {
        uint8_t mode = (uint8_t)p[PARAM_EUCL_TUN];
        if (mode >= EUCLID_MODE_COUNT) mode = 0;
        synth->euclid_offsets = vld1q_f32(EUCLID_OFFSETS[mode]);
    }
}

fast_inline void load_fm_preset(uint8_t idx, int8_t* params) {
    if (idx >= NUM_OF_PRESETS || params == NULL) return;

    const fm_preset_t* p = &FM_PRESETS[idx];

    params[PARAM_INSTRUMENT] = p->instrument_sel;
    params[PARAM_BLEND]      = p->gap;
    params[PARAM_GAP]        = p->blend;
    params[PARAM_SCATTER]    = p->scatter;

    params[PARAM_KICK_ATK]   = p->kick_attack;
    params[PARAM_KICK_BODY]  = p->kick_body;
    params[PARAM_SNARE_ATK]  = p->snare_attack;
    params[PARAM_SNARE_BODY] = p->snare_body;

    params[PARAM_METAL_ATK]  = p->metal_attack;
    params[PARAM_METAL_BODY] = p->metal_body;
    params[PARAM_PERC_ATK]   = p->perc_attack;
    params[PARAM_PERC_BODY]  = p->perc_body;

    params[PARAM_LFO1_SHAPE]  = p->lfo1_shape;
    params[PARAM_LFO1_RATE]   = p->lfo1_rate;
    params[PARAM_LFO1_TARGET] = p->lfo1_target;
    params[PARAM_LFO1_DEPTH]  = p->lfo1_depth;

    params[PARAM_EUCL_TUN]    = p->eucl_tun;
    params[PARAM_LFO2_RATE]   = p->lfo2_rate;
    params[PARAM_LFO2_TARGET] = p->lfo2_target;
    params[PARAM_LFO2_DEPTH]  = p->lfo2_depth;

    params[PARAM_ENV_SHAPE] = p->env_shape;
    params[PARAM_HIT_SHAPE] = p->hit_shape;
    params[PARAM_BODY_TILT] = p->body_tilt;
    params[PARAM_DRIVE]     = p->drive;
}

// ============================================================================
// Initialization
// ============================================================================

fast_inline void fm_perc_synth_init(fm_perc_synth_t* synth) {
    kick_engine_init(&synth->kick);
    snare_engine_init(&synth->snare);
    metal_engine_init(&synth->metal);
    perc_engine_init(&synth->perc);

    lfo_enhanced_init(&synth->lfo);
    lfo_smoother_init(&synth->lfo_smooth);
    neon_envelope_init(&synth->envelope);

    neon_prng_init(&synth->prng, RAND_DEFAULT_SEED);
    midi_handler_init(&synth->midi);

    synth->current_env_shape = ENV_SHAPE_DEFAULT;
    synth->voice_triggered = vdupq_n_u32(0);
    synth->euclid_offsets = vdupq_n_f32(0.0f);
    synth->master_gain = 0.25f;

    synth->instrument_sel = INST_KICK;
    synth->blend = 0.5f;
    synth->gap = 0.5f;
    synth->scatter = 0.25f;

    std::memset(synth->note_engine_mask, 0, sizeof(synth->note_engine_mask));
    std::memset(synth->pending, 0, sizeof(synth->pending));

    load_fm_preset(0, synth->params);
    fm_perc_synth_update_params(synth);
}

// ============================================================================
// MIDI note handling
// ============================================================================

fast_inline void fm_perc_synth_note_on(fm_perc_synth_t* synth,
                                       uint8_t note,
                                       uint8_t velocity) {
    const float vel = (float)velocity / 127.0f;
    const uint8_t inst = synth->instrument_sel;

    // Route table: single engines or pairs
    // Tom is routed to Perc for now (Perc is the tom/block/wood proxy).
    uint8_t engine_a = ENGINE_KICK;
    uint8_t engine_b = 0xFF;
    bool combo = false;

    switch (inst) {
        case INST_KICK:   engine_a = ENGINE_KICK;  break;
        case INST_SNARE:  engine_a = ENGINE_SNARE; break;
        case INST_TOM:    engine_a = ENGINE_PERC;  break;
        case INST_METAL:  engine_a = ENGINE_METAL; break;

        case INST_KS:     engine_a = ENGINE_KICK;  engine_b = ENGINE_SNARE; combo = true; break;
        case INST_KT:     engine_a = ENGINE_KICK;  engine_b = ENGINE_PERC;   combo = true; break;
        case INST_KM:     engine_a = ENGINE_KICK;  engine_b = ENGINE_METAL;  combo = true; break;
        case INST_ST:     engine_a = ENGINE_SNARE; engine_b = ENGINE_PERC;   combo = true; break;
        case INST_SM:     engine_a = ENGINE_SNARE; engine_b = ENGINE_METAL;  combo = true; break;
        case INST_TM:     engine_a = ENGINE_PERC;  engine_b = ENGINE_METAL;  combo = true; break;
        default:          engine_a = ENGINE_KICK;  break;
    }

    const float gap_ms = 4.0f + synth->gap * 80.0f;
    const float scatter_ms = synth->scatter * 8.0f;
    const float jitter_ms = ((float)neon_prng_rand_u32(&synth->prng).v[0] / (float)UINT32_MAX * 2.0f - 1.0f) * scatter_ms;

    const uint32_t gap_samples = (uint32_t)((gap_ms + jitter_ms) * (float)SAMPLE_RATE * 0.001f);

    // Use Blend differently for single vs combo:
    // - single: controls how strong the delayed shadow hit is
    // - combo : balances the two engines
    const float blend = synth->blend;

    // Store note mask for note-off / cancellation
    // bit0 = kick, bit1 = snare, bit2 = metal, bit3 = perc
    uint8_t mask = 0;
    mask |= (engine_a == ENGINE_KICK)  ? (1u << 0) : 0u;
    mask |= (engine_a == ENGINE_SNARE) ? (1u << 1) : 0u;
    mask |= (engine_a == ENGINE_METAL) ? (1u << 2) : 0u;
    mask |= (engine_a == ENGINE_PERC)  ? (1u << 3) : 0u;
    if (combo) {
        mask |= (engine_b == ENGINE_KICK)  ? (1u << 0) : 0u;
        mask |= (engine_b == ENGINE_SNARE) ? (1u << 1) : 0u;
        mask |= (engine_b == ENGINE_METAL) ? (1u << 2) : 0u;
        mask |= (engine_b == ENGINE_PERC)  ? (1u << 3) : 0u;
    }
    synth->note_engine_mask[note] = mask;

    const float32x4_t base_note = vdupq_n_f32((float)note);
    const float32x4_t tuned_note = vaddq_f32(base_note, synth->euclid_offsets);

    // Main hit now
    queue_trigger(synth, engine_a, note, (float)note, 1.0f, 0);

    if (combo) {
        // Secondary engine delayed
        const float gain_a = 1.0f - blend;
        const float gain_b = blend;

        synth->engine_gain[engine_a] = gain_a;
        synth->engine_gain[engine_b] = gain_b;

        // slightly detune secondary layer with scatter for chaos
        const float note_b = (float)note + ((synth->scatter * 2.0f - 1.0f) * 0.35f);
        queue_trigger(synth, engine_b, note, note_b, gain_b, gap_samples);
    } else {
        // Single instrument: add a quieter delayed shadow hit on the same engine
        const float shadow_gain = 0.12f + blend * 0.55f;
        const float note_shadow = (float)note + ((synth->scatter * 2.0f - 1.0f) * 0.25f);
        queue_trigger(synth, engine_a, note, note_shadow, shadow_gain, gap_samples);
        synth->engine_gain[engine_a] = 1.0f;
    }

    // Keep current envelope behavior
    neon_envelope_trigger(&synth->envelope, vdupq_n_u32(0xFFFFFFFFu), synth->current_env_shape);
}

fast_inline void fm_perc_synth_note_off(fm_perc_synth_t* synth, uint8_t note) {
    // Cancel queued secondary hits for this note
    for (int i = 0; i < 8; ++i) {
        pending_trigger_t& ev = synth->pending[i];
        if (ev.active && ev.midi_note == note) {
            ev.active = 0;
        }
    }

    synth->note_engine_mask[note] = 0;
    neon_envelope_release(&synth->envelope, vdupq_n_u32(0xFFFFFFFFu));
}

// ============================================================================
// Audio render
// ============================================================================

fast_inline float fm_perc_synth_process(fm_perc_synth_t* synth) {

    process_pending_triggers(synth);
    lfo_smoother_process(&synth->lfo_smooth);

    synth->lfo.shape_combo = (uint32_t)synth->params[PARAM_LFO1_SHAPE];
    synth->lfo.target1 = vgetq_lane_u32(synth->lfo_smooth.current_target1, 0);
    synth->lfo.target2 = vgetq_lane_u32(synth->lfo_smooth.current_target2, 0);
    synth->lfo.depth1 = synth->lfo_smooth.current_depth1;
    synth->lfo.depth2 = synth->lfo_smooth.current_depth2;
    synth->lfo.rate1 = synth->lfo_smooth.current_rate1;
    synth->lfo.rate2 = synth->lfo_smooth.current_rate2;

    float32x4_t lfo1 = vdupq_n_f32(0.0f);
    float32x4_t lfo2 = vdupq_n_f32(0.0f);
    lfo_enhanced_process(&synth->lfo, &lfo1, &lfo2);

    neon_envelope_process(&synth->envelope);
    float32x4_t envelope = synth->envelope.level;

    float32x4_t lfo_pitch_mult = vdupq_n_f32(1.0f);
    float32x4_t lfo_index_add  = vdupq_n_f32(0.0f);
    float32x4_t lfo_noise_add  = vdupq_n_f32(0.0f);
    float32x4_t lfo_env_mult   = vdupq_n_f32(1.0f);
    float32x4_t lfo_metal_gate = vdupq_n_f32(1.0f);

    const uint32_t t1 = synth->lfo.target1;
    const uint32_t t2 = synth->lfo.target2;
    const float32x4_t d1 = synth->lfo.depth1;
    const float32x4_t d2 = synth->lfo.depth2;

    switch (t1) {
        case LFO_TARGET_PITCH:
            lfo_pitch_mult = vaddq_f32(lfo_pitch_mult, vmulq_f32(lfo1, d1));
            break;
        case LFO_TARGET_INDEX:
            lfo_index_add = vaddq_f32(lfo_index_add,
                                      vmulq_f32(vmulq_f32(lfo1, d1), vdupq_n_f32(0.25f)));
            break;
        case LFO_TARGET_ENV:
            lfo_env_mult = vaddq_f32(lfo_env_mult, vmulq_f32(lfo1, d1));
            break;
        case LFO_TARGET_NOISE_MIX:
            lfo_noise_add = vaddq_f32(lfo_noise_add,
                                      vmulq_f32(vmulq_f32(lfo1, d1), vdupq_n_f32(0.15f)));
            break;
        case LFO_TARGET_METAL_GATE:
            lfo_metal_gate = vaddq_f32(vdupq_n_f32(0.5f),
                                       vmulq_f32(vmulq_f32(lfo1, d1), vdupq_n_f32(0.5f)));
            lfo_metal_gate = fm_vclamp01(lfo_metal_gate);
            break;
        default:
            break;
    }

    switch (t2) {
        case LFO_TARGET_PITCH:
            lfo_pitch_mult = vaddq_f32(lfo_pitch_mult, vmulq_f32(lfo2, d2));
            break;
        case LFO_TARGET_INDEX:
            lfo_index_add = vaddq_f32(lfo_index_add,
                                      vmulq_f32(vmulq_f32(lfo2, d2), vdupq_n_f32(0.25f)));
            break;
        case LFO_TARGET_ENV:
            lfo_env_mult = vaddq_f32(lfo_env_mult, vmulq_f32(lfo2, d2));
            break;
        case LFO_TARGET_NOISE_MIX:
            lfo_noise_add = vaddq_f32(lfo_noise_add,
                                      vmulq_f32(vmulq_f32(lfo2, d2), vdupq_n_f32(0.15f)));
            break;
        case LFO_TARGET_METAL_GATE:
            lfo_metal_gate = vaddq_f32(vdupq_n_f32(0.5f),
                                       vmulq_f32(vmulq_f32(lfo2, d2), vdupq_n_f32(0.5f)));
            lfo_metal_gate = fm_vclamp01(lfo_metal_gate);
            break;
        default:
            break;
    }

    lfo_pitch_mult = vmaxq_f32(lfo_pitch_mult, vdupq_n_f32(0.125f));
    envelope = vmulq_f32(envelope, fm_vclamp01(lfo_env_mult));

    uint32x4_t active_mask = vmvnq_u32(vceqq_u32(synth->envelope.stage,
                                                 vdupq_n_u32(ENV_STATE_OFF)));

    float32x4_t hit_shape = vdupq_n_f32(synth->params[PARAM_HIT_SHAPE] * 0.01f);
    float32x4_t body_tilt = vdupq_n_f32(synth->params[PARAM_BODY_TILT] * 0.01f);
    float32x4_t drive = vdupq_n_f32(synth->params[PARAM_DRIVE] * 0.01f);

    float32x4_t transient_env = fm_make_transient_env(envelope, hit_shape);
    float32x4_t body_env = fm_make_body_env(envelope, body_tilt);

    float32x4_t kick_out = vmulq_f32(
        kick_engine_process(&synth->kick, envelope, active_mask, lfo_pitch_mult, lfo_index_add),
        vdupq_n_f32(synth->engine_gain[ENGINE_KICK])
    );

    float32x4_t snare_out = vmulq_f32(
        snare_engine_process(&synth->snare, envelope, active_mask, lfo_pitch_mult, lfo_index_add, lfo_noise_add),
        vdupq_n_f32(synth->engine_gain[ENGINE_SNARE])
    );

    float32x4_t metal_out = vmulq_f32(
        metal_engine_process(&synth->metal, envelope, active_mask, lfo_pitch_mult, lfo_index_add, lfo_metal_gate),
        vdupq_n_f32(synth->engine_gain[ENGINE_METAL])
    );

    float32x4_t perc_out = vmulq_f32(
        perc_engine_process(&synth->perc, envelope, active_mask, lfo_pitch_mult, lfo_index_add),
        vdupq_n_f32(synth->engine_gain[ENGINE_PERC])
    );

    float32x4_t mix = vdupq_n_f32(0.0f);
    mix = vaddq_f32(mix, vmulq_n_f32(kick_out,  0.28f));
    mix = vaddq_f32(mix, vmulq_n_f32(snare_out, 0.24f));
    mix = vaddq_f32(mix, vmulq_n_f32(metal_out, 0.22f));
    mix = vaddq_f32(mix, vmulq_n_f32(perc_out,  0.26f));

    mix = vmulq_f32(mix, fm_make_drive_gain(drive));
    mix = fm_soft_clip(mix);
    mix = vmulq_n_f32(mix, synth->master_gain);

    return neon_horizontal_sum_alt(mix);
}

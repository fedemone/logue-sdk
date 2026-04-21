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

    // Voice probabilities (fixed 4 voices)
    uint32_t voice_probs[ENGINE_COUNT];
    uint32x4_t voice_triggered;

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

// ============================================================================
// Parameter / preset handling
// ============================================================================

fast_inline void fm_perc_synth_update_params(fm_perc_synth_t* synth) {
    int8_t* p = synth->params;

    synth->voice_probs[0] = (uint32_t)p[PARAM_KPROB];
    synth->voice_probs[1] = (uint32_t)p[PARAM_SPROB];
    synth->voice_probs[2] = (uint32_t)p[PARAM_MPROB];
    synth->voice_probs[3] = (uint32_t)p[PARAM_PPROB];

    kick_engine_update(&synth->kick,
                       vdupq_n_f32(p[PARAM_KICK_ATK] / 100.0f),
                       vdupq_n_f32(p[PARAM_KICK_BODY] / 100.0f));

    snare_engine_update(&synth->snare,
                        vdupq_n_f32(p[PARAM_SNARE_ATK] / 100.0f),
                        vdupq_n_f32(p[PARAM_SNARE_BODY] / 100.0f));

    metal_engine_update(&synth->metal,
                        vdupq_n_f32(p[PARAM_METAL_ATK] / 100.0f),
                        vdupq_n_f32(p[PARAM_METAL_BODY] / 100.0f));

    perc_engine_update(&synth->perc,
                       vdupq_n_f32(p[PARAM_PERC_ATK] / 100.0f),
                       vdupq_n_f32(p[PARAM_PERC_BODY] / 100.0f));

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

    params[PARAM_KPROB] = p->prob_kick;
    params[PARAM_SPROB] = p->prob_snare;
    params[PARAM_MPROB] = p->prob_metal;
    params[PARAM_PPROB] = p->prob_perc;

    params[PARAM_KICK_ATK] = p->kick_attack;
    params[PARAM_KICK_BODY] = p->kick_body;
    params[PARAM_SNARE_ATK] = p->snare_attack;
    params[PARAM_SNARE_BODY] = p->snare_body;

    params[PARAM_METAL_ATK] = p->metal_attack;
    params[PARAM_METAL_BODY] = p->metal_body;
    params[PARAM_PERC_ATK] = p->perc_attack;
    params[PARAM_PERC_BODY] = p->perc_body;

    params[PARAM_LFO1_SHAPE] = p->lfo1_shape;
    params[PARAM_LFO1_RATE] = p->lfo1_rate;
    params[PARAM_LFO1_TARGET] = p->lfo1_target;
    params[PARAM_LFO1_DEPTH] = p->lfo1_depth;

    params[PARAM_EUCL_TUN] = p->eucl_tun;
    params[PARAM_LFO2_RATE] = p->lfo2_rate;
    params[PARAM_LFO2_TARGET] = p->lfo2_target;
    params[PARAM_LFO2_DEPTH] = p->lfo2_depth;

    params[PARAM_ENV_SHAPE] = p->env_shape;
    params[PARAM_HIT_SHAPE] = p->hit_shape;
    params[PARAM_BODY_TILT] = p->body_tilt;
    params[PARAM_DRIVE] = p->drive;
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

    for (int i = 0; i < ENGINE_COUNT; ++i) {
        synth->voice_probs[i] = 100;
    }

    load_fm_preset(0, synth->params);
    fm_perc_synth_update_params(synth);
}

// ============================================================================
// MIDI note handling
// ============================================================================

fast_inline void fm_perc_synth_note_on(fm_perc_synth_t* synth,
                                       uint8_t note,
                                       uint8_t velocity) {
    (void)velocity;

    // Route to matching voice(s); otherwise excite all four positions.
    uint32_t route_bits = 0xF;
    if (note == synth->midi.kick_note) {
        route_bits = 0x1;
    } else if (note == synth->midi.snare_note) {
        route_bits = 0x2;
    } else if (note == synth->midi.metal_note) {
        route_bits = 0x4;
    } else if (note == synth->midi.perc_note) {
        route_bits = 0x8;
    }

    const uint32_t route_lanes[4] = {
        (route_bits & 0x1) ? 0xFFFFFFFFu : 0u,
        (route_bits & 0x2) ? 0xFFFFFFFFu : 0u,
        (route_bits & 0x4) ? 0xFFFFFFFFu : 0u,
        (route_bits & 0x8) ? 0xFFFFFFFFu : 0u
    };
    const uint32x4_t route_mask = vld1q_u32(route_lanes);

    uint32x4_t gate = probability_gate_neon(&synth->prng,
                                            synth->voice_probs[0],
                                            synth->voice_probs[1],
                                            synth->voice_probs[2],
                                            synth->voice_probs[3]);
    gate = vandq_u32(gate, route_mask);
    synth->voice_triggered = gate;

    float32x4_t base_note = vdupq_n_f32((float)note);
    float32x4_t tuned_note = vaddq_f32(base_note, synth->euclid_offsets);

    if (vgetq_lane_u32(gate, 0)) {
        uint32x4_t lane = vdupq_n_u32(0u);
        lane = vsetq_lane_u32(0xFFFFFFFFu, lane, 0);
        kick_engine_set_note(&synth->kick, lane, tuned_note);
        synth->midi.active_notes[note] |= 1u << 0;
    }
    if (vgetq_lane_u32(gate, 1)) {
        uint32x4_t lane = vdupq_n_u32(0u);
        lane = vsetq_lane_u32(0xFFFFFFFFu, lane, 1);
        snare_engine_set_note(&synth->snare, lane, tuned_note);
        synth->midi.active_notes[note] |= 1u << 1;
    }
    if (vgetq_lane_u32(gate, 2)) {
        uint32x4_t lane = vdupq_n_u32(0u);
        lane = vsetq_lane_u32(0xFFFFFFFFu, lane, 2);
        metal_engine_set_note(&synth->metal, lane, tuned_note);
        synth->midi.active_notes[note] |= 1u << 2;
    }
    if (vgetq_lane_u32(gate, 3)) {
        uint32x4_t lane = vdupq_n_u32(0u);
        lane = vsetq_lane_u32(0xFFFFFFFFu, lane, 3);
        perc_engine_set_note(&synth->perc, lane, tuned_note);
        synth->midi.active_notes[note] |= 1u << 3;
    }

    if (vgetq_lane_u32(gate, 0) || vgetq_lane_u32(gate, 1) ||
        vgetq_lane_u32(gate, 2) || vgetq_lane_u32(gate, 3)) {
        neon_envelope_trigger(&synth->envelope, gate, synth->current_env_shape);
    }
}

fast_inline void fm_perc_synth_note_off(fm_perc_synth_t* synth, uint8_t note) {
    uint8_t releasing[4];
    uint32_t count = midi_note_off(&synth->midi, note, releasing);
    if (count == 0) return;

    uint32_t rel_bits = 0;
    for (uint32_t i = 0; i < count; ++i) {
        rel_bits |= (1u << releasing[i]);
    }

    const uint32_t rel_lanes[4] = {
        (rel_bits & 1u) ? 0xFFFFFFFFu : 0u,
        (rel_bits & 2u) ? 0xFFFFFFFFu : 0u,
        (rel_bits & 4u) ? 0xFFFFFFFFu : 0u,
        (rel_bits & 8u) ? 0xFFFFFFFFu : 0u,
    };
    neon_envelope_release(&synth->envelope, vld1q_u32(rel_lanes));
}

// ============================================================================
// Audio render
// ============================================================================

fast_inline float fm_perc_synth_process(fm_perc_synth_t* synth) {
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

    float32x4_t hit_shape = vdupq_n_f32(synth->params[PARAM_HIT_SHAPE] / 100.0f);
    float32x4_t body_tilt = vdupq_n_f32(synth->params[PARAM_BODY_TILT] / 100.0f);
    float32x4_t drive = vdupq_n_f32(synth->params[PARAM_DRIVE] / 100.0f);

    float32x4_t transient_env = fm_make_transient_env(envelope, hit_shape);
    float32x4_t body_env = fm_make_body_env(envelope, body_tilt);

    float32x4_t kick_out = kick_engine_process(&synth->kick,
                                               transient_env,
                                               active_mask,
                                               lfo_pitch_mult,
                                               lfo_index_add);

    float32x4_t snare_out = snare_engine_process(&synth->snare,
                                                 transient_env,
                                                 active_mask,
                                                 lfo_pitch_mult,
                                                 vaddq_f32(lfo_index_add, lfo_noise_add),
                                                 vaddq_f32(synth->snare.noise_mix, lfo_noise_add));

    float32x4_t metal_out = metal_engine_process(&synth->metal,
                                                 body_env,
                                                 active_mask,
                                                 lfo_pitch_mult,
                                                 lfo_index_add,
                                                 synth->metal.brightness,
                                                 lfo_metal_gate);

    float32x4_t perc_out = perc_engine_process(&synth->perc,
                                               body_env,
                                               active_mask,
                                               lfo_pitch_mult,
                                               lfo_index_add);

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

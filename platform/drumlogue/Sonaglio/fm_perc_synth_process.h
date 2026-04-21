#pragma once

/**
 * @file fm_perc_synth.h
 * @brief FM Percussion Synth - 4 voices, 5 instruments, one instance per instrument
 *
 * Features:
 * - 5 instruments: Kick, Snare, Metal, Perc, Resonant
 * - 4 voices, each with unique instrument (no duplicates)
 * - 12 valid allocations encoded in VoiceAlloc param
 * - Per-voice probability triggering
 * - Enhanced LFO system with bipolar modulation
 * - Envelope ROM
 * - Parameter smoothing
 * - Preset system
 */

#include <arm_neon.h>
#include "constants.h"
#include "kick_engine.h"
#include "snare_engine.h"
#include "metal_engine.h"
#include "perc_engine.h"
#include "resonant_synthesis.h"
#include "lfo_enhanced.h"
#include "lfo_smoothing.h"
#include "envelope_rom.h"
#include "prng.h"
#include "midi_handler.h"
#include "fm_presets.h"

// Euclidean tuning offset table
// offsets[mode][voice] = semitones above root for that voice.
// Derived from E(4,n): position[i] = floor(i * n / 4), i = 0..3.
static const float EUCLID_OFFSETS[EUCLID_MODE_COUNT][4] = {
    { 0.f,  0.f,  0.f,  0.f},  // 0: Off         — all unison
    { 0.f,  1.f,  2.f,  3.f},  // 1: E(4,4)  [0,1,2,3]  chromatic cluster
    { 0.f,  1.f,  3.f,  4.f},  // 2: E(4,6)  [0,1,3,4]  minor 3rd pairs
    { 0.f,  1.f,  3.f,  5.f},  // 3: E(4,7)  [0,1,3,5]  diatonic cluster
    { 0.f,  2.f,  4.f,  6.f},  // 4: E(4,8)  [0,2,4,6]  whole tone
    { 0.f,  2.f,  5.f,  7.f},  // 5: E(4,10) [0,2,5,7]  pentatonic/5th
    { 0.f,  3.f,  6.f,  9.f},  // 6: E(4,12) [0,3,6,9]  diminished 7th
    { 0.f,  4.f,  8.f, 12.f},  // 7: E(4,16) [0,4,8,12] augmented + octave
    { 0.f,  6.f, 12.f, 18.f},  // 8: E(4,24) [0,6,12,18] tritone spread
};

// Voice allocation table - 12 combinations (no duplicates)
// Format: [voice0, voice1, voice2, voice3] engine assignments
static const uint8_t VOICE_ALLOC_TABLE[VOICE_ALLOC_COUNT][VOICE_ALLOC_MAX] = {
    {ENGINE_KICK, ENGINE_SNARE, ENGINE_METAL, ENGINE_PERC},     // 0: K-S-M-P (no resonant)
};

/**
 * Complete synthesizer state
 */
typedef struct {
    // Probability PRNG (4 independent streams)
    neon_prng_t prng;

    // FM Engines (5 total)
    kick_engine_t  kick;
    snare_engine_t snare;
    metal_engine_t metal;
    perc_engine_t  perc;

    // LFO System
    lfo_enhanced_t lfo;
    lfo_smoother_t lfo_smooth;

    // Envelope
    neon_envelope_t envelope;
    uint8_t current_env_shape;

    // shared variables among engines
    float32x4_t hit_shape;
    float32x4_t body_tilt;
    float32x4_t drive;

    // MIDI handler
    midi_handler_t midi;

    // Current parameters (cached)
    int8_t      params[PARAM_TOTAL];

    // Voice allocation
    uint8_t     voice_engine[VOICE_ALLOC_MAX];  // Engine type for each voice (0-4)
    uint8_t     allocation_idx;         // Current allocation (0-11)

    // Masks for efficient NEON processing
    uint32x4_t  engine_mask[ENGINE_COUNT];

    // Voice activity and probabilities
    float32x4_t voice_active;
    uint32x4_t  voice_triggered;
    uint32_t    voice_probs[VOICE_ALLOC_MAX];         // Per-voice probabilities (0-100)

    // Per-voice velocity (set on note-on, persists until next trigger)
    float32x4_t voice_velocity;      // 0-1 per lane

    // Euclidean tuning: per-voice semitone offsets applied at note-on.
    // Loaded from EUCLID_OFFSETS[EuclTun] in update_params; [0,0,0,0] when mode=Off.
    float32x4_t euclid_offsets;

    // Output gain
    float master_gain;

    // Per-engine output band filters — keeps each engine in its natural frequency
    // range so voices don't mask each other.  Each one_pole_t holds one float32x4_t
    // state word (one state per NEON lane = per voice).
    // Precomputed alpha = 2πf/(2πf+sr) — see constants below.
    one_pole_t kick_out_lpf;   // LP 250 Hz  — keeps kick body, removes highs
    one_pole_t snare_hpf;      // LP state for HP 100 Hz subtraction — removes kick rumble
    one_pole_t snare_out_lpf;  // LP 7000 Hz — removes ultrasonic hash
    one_pole_t metal_hpf;      // LP state for HP 800 Hz subtraction — removes lows
    one_pole_t perc_hpf;       // LP state for HP 80 Hz subtraction  — removes sub rumble
    one_pole_t perc_out_lpf;   // LP 3000 Hz — removes high-frequency clash with metal
} fm_perc_synth_t;

// Precomputed one-pole filter alpha values: 2πf / (2πf + 48000)
// These are constant across the lifetime of the plugin so computing once is correct.
static const float FILT_KICK_LP_A  = 1570.796f  / (1570.796f  + 48000.0f); // 250 Hz
static const float FILT_SNARE_HP_A = 628.318f   / (628.318f   + 48000.0f); // 100 Hz
static const float FILT_SNARE_LP_A = 43982.297f / (43982.297f + 48000.0f); // 7000 Hz
static const float FILT_METAL_HP_A = 5026.548f  / (5026.548f  + 48000.0f); // 800 Hz
static const float FILT_PERC_HP_A  = 502.655f   / (502.655f   + 48000.0f); // 80 Hz
static const float FILT_PERC_LP_A  = 18849.556f / (18849.556f + 48000.0f); // 3000 Hz

/**
 * OBSOLETE: possible for future project
 * Apply resonant morph parameter - controls multiple dimensions
 */
fast_inline void apply_resonant_morph(resonant_synth_t * res, float morph, uint8_t mode) {
  uint32x4_t all_voices = vdupq_n_u32(0xFFFFFFFF);

  // Morph zones with different behaviors based on mode
  switch (mode) {
    case 0:  // LowPass - morph controls cutoff frequency
    {
      float fc = 50.0f + morph * 7950.0f;  // 50-8000 Hz
      resonant_synth_set_center(res, all_voices, vdupq_n_f32(fc));
      resonant_synth_set_resonance(res, all_voices, 50.0f);  // Fixed resonance
    } break;

    case 1:  // BandPass - morph controls Q/resonance
    {
      float fc = 1000.0f;                       // Fixed center
      float resonance = 10.0f + morph * 80.0f;  // 10-90%
      resonant_synth_set_center(res, all_voices, vdupq_n_f32(fc));
      resonant_synth_set_resonance(res, all_voices, resonance);
    } break;

    case 2:  // HighPass - morph controls cutoff with inverse curve
    {
      float fc = 8000.0f - morph * 7950.0f;  // 8000-50 Hz (inverse)
      resonant_synth_set_center(res, all_voices, vdupq_n_f32(fc));
      resonant_synth_set_resonance(res, all_voices, 30.0f);
    } break;

    case 3:  // Notch - morph controls notch sharpness
    {
      // Higher resonance 'a' = narrower notch (resonance IS the width control)
      float fc = 1000.0f;
      resonant_synth_set_center(res, all_voices, vdupq_n_f32(fc));
      resonant_synth_set_resonance(res, all_voices, 20.0f + morph * 60.0f);
    } break;

    case 4:  // Peak - morph controls both frequency and gain
    {
      float fc = 200.0f + morph * 3800.0f;  // 200-4000 Hz
      float gain = 1.0f + morph * 3.0f;     // 1-4x gain
      resonant_synth_set_center(res, all_voices, vdupq_n_f32(fc));
      resonant_synth_set_resonance(res, all_voices, 30.0f + morph * 60.0f);
      res->gain = vdupq_n_f32(gain);
    } break;
  }
}

/**
 * Update all parameters from UI
 */
fast_inline void fm_perc_synth_update_params(fm_perc_synth_t* synth) {
    int8_t* p = synth->params;

    // =================================================================
    // Update voice probabilities (Page 1, params 0-3)
    // =================================================================
    synth->voice_probs[PARAM_VOICE1_PROB] = p[PARAM_VOICE1_PROB];
    synth->voice_probs[PARAM_VOICE2_PROB] = p[PARAM_VOICE2_PROB];
    synth->voice_probs[PARAM_VOICE3_PROB] = p[PARAM_VOICE3_PROB];
    synth->voice_probs[PARAM_VOICE4_PROB] = p[PARAM_VOICE4_PROB];


    // =================================================================
    // Update FM engines (always update all - they'll be used based on allocation)
    // =================================================================

    // Kick engine: param1 = sweep depth (0-1), param2 = decay shape (0-1)
    kick_engine_update(&synth->kick,
                        vdupq_n_f32(p[PARAM_KICK_ATTACK] / 100.0f),   // Kick sweep
                        vdupq_n_f32(p[PARAM_KICK_BODY] / 100.0f),
                        voice_velocity);  // Kick decay

    // Snare engine: param1 = noise mix (0-1), param2 = body resonance (0-1)
    snare_engine_update(&synth->snare,
                        vdupq_n_f32(p[PARAM_SNARE_ATTACK] / 100.0f),  // Snare noise mix
                        vdupq_n_f32(p[PARAM_SNARE_BODY] / 100.0f),
                        voice_velocity);  // Snare body resonance

    // Metal engine: param1 = inharmonicity (0-1), param2 = brightness (0-1)
    metal_engine_update(&synth->metal,
                        vdupq_n_f32(p[PARAM_METAL_ATTACK] / 100.0f),   // Metal inharmonicity
                        vdupq_n_f32(p[PARAM_METAL_BODY] / 100.0f),
                        voice_velocity);  // Metal brightness

    // Perc engine: param1 = ratio center (0-1), param2 = variation (0-1)
    perc_engine_update(&synth->perc,
                        vdupq_n_f32(p[PARAM_PERC_ATTACK] / 100.0f),  // Perc ratio center
                        vdupq_n_f32(p[PARAM_PERC_BODY] / 100.0f),
                        voice_velocity);   // Perc variation

    // =================================================================
    // Update resonant base parameters (mode from param 22)
    // =================================================================
    resonant_synth_set_mode(&synth->resonant,
                            vdupq_n_u32(0xFFFFFFFF),
                            (resonant_mode_t)(p[PARAM_BODY_TILT] % 5));

    // =================================================================
    // Update LFO (params 12-19)
    // =================================================================
    uint32x4_t all_voices = vdupq_n_u32(0xFFFFFFFF);
    int8_t depth1 = p[PARAM_LFO1_DEPTH];
    int8_t depth2 = p[PARAM_LFO2_DEPTH];

    lfo_smoother_set_rate(&synth->lfo_smooth, 0, p[PARAM_LFO1_RATE] / 100.0f, all_voices);
    lfo_smoother_set_rate(&synth->lfo_smooth, 1, p[PARAM_LFO2_RATE] / 100.0f, all_voices);
    lfo_smoother_set_depth(&synth->lfo_smooth, 0, depth1 / 100.0f, all_voices);
    lfo_smoother_set_depth(&synth->lfo_smooth, 1, depth2 / 100.0f, all_voices);
    lfo_smoother_set_target(&synth->lfo_smooth, 0, p[PARAM_LFO1_TARGET], all_voices);
    lfo_smoother_set_target(&synth->lfo_smooth, 1, p[PARAM_LFO2_TARGET], all_voices);

    // =================================================================
    // Update envelope shape and metal character (param 20)
    // EnvShape encoding: bit 7 = metal character (0=Cymbal, 1=Gong)
    //                    bits[6:0] = envelope ROM index (0-127)
    // =================================================================
    synth->current_env_shape = (uint8_t)p[PARAM_ENV_SHAPE];
    metal_engine_set_character(&synth->metal,
                               (uint32_t)(synth->current_env_shape >> 7));

    // =================================================================
    // Update Euclidean tuning offsets (param 16 / EuclTun)
    // Loads the per-voice semitone offset vector from the static lookup
    // table.  Applied at note-on so each voice plays a different pitch
    // derived from E(4,n): position[i] = floor(i * n / 4).
    // =================================================================
    {
        uint8_t mode = (uint8_t)p[PARAM_EUCLIDEAN_TUNE];
        if (mode >= EUCLID_MODE_COUNT) mode = 0;
        synth->euclid_offsets = vld1q_f32(EUCLID_OFFSETS[mode]);
    }

    // TODO: missing hit_shape, body_tilt and drive
}

/**
 * standalone function similar to load_preset
 */
fast_inline void load_fm_preset(uint8_t idx, int8_t * params) {
    if (idx >= NUM_OF_PRESETS) return;

    const fm_preset_t * p = &FM_PRESETS[idx];

    // Page 1
    params[PARAM_VOICE1_PROB]    = p->prob_kick;
    params[PARAM_VOICE2_PROB]    = p->prob_snare;
    params[PARAM_VOICE3_PROB]    = p->prob_metal;
    params[PARAM_VOICE4_PROB]    = p->prob_perc;

    // Page 2
    params[PARAM_KICK_ATTACK]    = p->kick_sweep;
    params[PARAM_KICK_BODY]      = p->kick_decay;
    params[PARAM_SNARE_ATTACK]   = p->snare_noise;
    params[PARAM_SNARE_BODY]     = p->snare_body;

    // Page 3
    params[PARAM_METAL_ATTACK]   = p->metal_inharm;
    params[PARAM_METAL_BODY]     = p->metal_bright;
    params[PARAM_PERC_ATTACK]    = p->perc_ratio;
    params[PARAM_PERC_BODY]      = p->perc_var;

    // Page 4 (LFO1)
    params[PARAM_LFO1_SHAPE]     = p->lfo1_shape;
    params[PARAM_LFO1_RATE]      = p->lfo1_rate;
    params[PARAM_LFO1_TARGET]    = p->lfo1_target;
    params[PARAM_LFO1_DEPTH]     = p->lfo1_depth;  // -100..100, stored directly in int8_t

    // Page 5 (LFO2)
    params[PARAM_EUCLIDEAN_TUNE] = p->lfo2_shape;
    params[PARAM_LFO2_RATE]      = p->lfo2_rate;
    params[PARAM_LFO2_TARGET]    = p->lfo2_target;
    params[PARAM_LFO2_DEPTH]     = p->lfo2_depth;  // -100..100, stored directly in int8_t

    // Page 6
    params[PARAM_ENV_SHAPE]      = p->env_shape;
    params[PARAM_VOICE_HIT_SHAPE]= p->hit_shape;
    params[PARAM_BODY_TILT]      = p->body_tilt;
    params[PARAM_DRIVE]          = p->drive;
}

/**
* Initialize synthesizer
*/
fast_inline void fm_perc_synth_init(fm_perc_synth_t * synth) {
    // Copy allocation to voice_engine array
    // now static, as only 4 engines are designed
    for (int v = 0; v < VOICE_ALLOC_MAX; v++) {
        synth->voice_engine[v] = VOICE_ALLOC_TABLE[alloc_idx][v];
    }

    // Initialize all engines
    kick_engine_init(&synth->kick);
    snare_engine_init(&synth->snare);
    metal_engine_init(&synth->metal);
    perc_engine_init(&synth->perc);
    // resonant_synth_init(&synth->resonant);

    // Initialize LFO and envelope
    lfo_enhanced_init(&synth->lfo);
    lfo_smoother_init(&synth->lfo_smooth);
    neon_envelope_init(&synth->envelope);

    // Initialize PRNG and MIDI
    neon_prng_init(&synth->prng, RAND_DEFAULT_SEED);
    midi_handler_init(&synth->midi);

    // Initialize per-engine band filter states
    synth->kick_out_lpf.z1 = vdupq_n_f32(0.0f);
    synth->snare_hpf.z1    = vdupq_n_f32(0.0f);
    synth->snare_out_lpf.z1= vdupq_n_f32(0.0f);
    synth->metal_hpf.z1    = vdupq_n_f32(0.0f);
    synth->perc_hpf.z1     = vdupq_n_f32(0.0f);
    synth->perc_out_lpf.z1 = vdupq_n_f32(0.0f);

    // Initialize parameters
    synth->voice_active    = vdupq_n_f32(0.0f);
    synth->voice_triggered = vdupq_n_u32(0);
    synth->voice_velocity  = vdupq_n_f32(1.0f);
    synth->euclid_offsets  = vdupq_n_f32(0.0f);  // Off: all voices unison
    synth->master_gain     = 0.25f;
    synth->current_env_shape = 40;

    // Default probabilities (all 30%)
    for (int i = 0; i < VOICE_ALLOC_MAX; i++) {
      synth->voice_probs[i] = 30;
    }

    // Load default preset
    load_fm_preset(0, synth->params);

    // Update voice allocation from params
    fm_perc_synth_update_params(synth);
}

/**
 * Fast NEON horizontal sum of 4 floats
 * Returns sum of all 4 lanes
 */
fast_inline float neon_horizontal_sum(float32x4_t v) {
    // Step 1: Pairwise add low and high halves
    float32x2_t sum_low = vpadd_f32(vget_low_f32(v), vget_high_f32(v));

    // Step 2: Pairwise add again to get final sum
    float32x2_t sum_total = vpadd_f32(sum_low, sum_low);

    // Step 3: Extract result
    return vget_lane_f32(sum_total, 0);
}

/**
 * Alternative method using vaddvq_f32 for ARMv8/AArch64
 * For ARMv7 (drumlogue), use the vpadd method above
 */
fast_inline float neon_horizontal_sum_alt(float32x4_t v) {
    #if defined(__aarch64__)
    // ARMv8/AArch64 has dedicated instruction
    return vaddvq_f32(v);
    #else
    // ARMv7 fallback
    float32x2_t sum_low = vpadd_f32(vget_low_f32(v), vget_high_f32(v));
    float32x2_t sum_total = vpadd_f32(sum_low, sum_low);
    return vget_lane_f32(sum_total, 0);
    #endif
}

/**
 * MIDI Note On handler with per-voice probability and note routing.
 *
 * Routing: each voice position has a dedicated drum note (kick=36, snare=38,
 * metal=42, perc=45 by default in midi_handler).  A note matching one of those
 * four only triggers voices at the matching position; any other note triggers
 * all voices (general trigger / sequencer mode).
 *
 * LFO phase sync: phases are reset on every trigger so a one-shot ramp at
 * slow rate acts as a secondary envelope.
 */
fast_inline void fast_inline void fm_perc_synth_note_on(fm_perc_synth_t* synth,
                                       uint8_t note,
                                       uint8_t velocity) {
    // Normalize velocity once.
    const float32x4_t vel = vdupq_n_f32((float)velocity / 127.0f);
    synth->voice_velocity = vel;

    // Decide which voices are eligible.
    // Dedicated drum-note routing:
    //   kick  = 36 (C2)
    //   snare = 38 (D2)
    //   metal = 42 (F#2)
    //   perc  = 45 (A2)
    //
    // Any other note acts as a "global trigger" and can excite all voices.
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

    // Probability gate per voice.
    // Each lane is kept only if the random draw is <= configured probability.
    uint32x4_t gate = probability_gate_neon(&synth->prng,
                                            synth->voice_probs[0],
                                            synth->voice_probs[1],
                                            synth->voice_probs[2],
                                            synth->voice_probs[3]);

    gate = vandq_u32(gate, route_mask);

    // Store a compact triggered mask for diagnostics / future use.
    synth->voice_triggered = gate;

    // Build a per-voice note vector with Euclidean spread.
    // Offsets are in semitones and are applied on top of the incoming note.
    float32x4_t base_note = vdupq_n_f32((float)note);
    float32x4_t tuned_note = vaddq_f32(base_note, synth->euclid_offsets);

    // Route note to engines by fixed voice position.
    // No allocation table is needed anymore.
    if (vgetq_lane_u32(gate, 0)) {
        uint32x4_t lane0 = vdupq_n_u32(0u);
        lane0 = vsetq_lane_u32(0xFFFFFFFFu, lane0, 0);
        kick_engine_set_note(&synth->kick, lane0, tuned_note);
    }

    if (vgetq_lane_u32(gate, 1)) {
        uint32x4_t lane1 = vdupq_n_u32(0u);
        lane1 = vsetq_lane_u32(0xFFFFFFFFu, lane1, 1);
        snare_engine_set_note(&synth->snare, lane1, tuned_note);
    }

    if (vgetq_lane_u32(gate, 2)) {
        uint32x4_t lane2 = vdupq_n_u32(0u);
        lane2 = vsetq_lane_u32(0xFFFFFFFFu, lane2, 2);
        metal_engine_set_note(&synth->metal, lane2, tuned_note);
    }

    if (vgetq_lane_u32(gate, 3)) {
        uint32x4_t lane3 = vdupq_n_u32(0u);
        lane3 = vsetq_lane_u32(0xFFFFFFFFu, lane3, 3);
        perc_engine_set_note(&synth->perc, lane3, tuned_note);
    }

    // Reset the envelope on any successful trigger.
    // If your envelope helper exposes a different trigger function name,
    // replace this single call accordingly.
    if (vgetq_lane_u32(gate, 0) ||
        vgetq_lane_u32(gate, 1) ||
        vgetq_lane_u32(gate, 2) ||
        vgetq_lane_u32(gate, 3)) {
        neon_envelope_trigger(&synth->envelope, synth->current_env_shape);
    }
}

/**
 * MIDI Note Off — triggers per-voice envelope release for voices that
 * were triggered by this specific note.
 */
fast_inline void fm_perc_synth_note_off(fm_perc_synth_t* synth, uint8_t note) {
    uint8_t releasing[4];
    uint32_t num_releasing = midi_note_off(&synth->midi, note, releasing);

    if (num_releasing == 0) return;

    // Build NEON voice mask for releasing voices
    uint32_t rel_bits = 0;
    for (uint32_t i = 0; i < num_releasing; i++)
        rel_bits |= (1u << releasing[i]);

    const uint32_t rel_lanes[4] = {
        (rel_bits & 1) ? 0xFFFFFFFFU : 0U,
        (rel_bits & 2) ? 0xFFFFFFFFU : 0U,
        (rel_bits & 4) ? 0xFFFFFFFFU : 0U,
        (rel_bits & 8) ? 0xFFFFFFFFU : 0U,
    };
    neon_envelope_release(&synth->envelope, vld1q_u32(rel_lanes));
}

/**
  * Process one audio sample with full LFO modulation support
 */
fast_inline float fm_perc_synth_process(fm_perc_synth_t* synth) {
    // Advance LFO smoothing and mirror smoothed routing into the live LFO state.
    lfo_smoother_process(&synth->lfo_smooth);

    synth->lfo.shape_combo = static_cast<uint32_t>(synth->params[Synth::PARAM_LFO1_SHAPE]);
    synth->lfo.target1 = vgetq_lane_u32(synth->lfo_smooth.current_target1, 0);
    synth->lfo.target2 = vgetq_lane_u32(synth->lfo_smooth.current_target2, 0);
    synth->lfo.depth1  = synth->lfo_smooth.current_depth1;
    synth->lfo.depth2  = synth->lfo_smooth.current_depth2;
    synth->lfo.rate1   = synth->lfo_smooth.current_rate1;
    synth->lfo.rate2   = synth->lfo_smooth.current_rate2;

    // Advance LFO waveforms.
    float32x4_t lfo1 = vdupq_n_f32(0.0f);
    float32x4_t lfo2 = vdupq_n_f32(0.0f);
    lfo_enhanced_process(&synth->lfo, &lfo1, &lfo2);

    // Advance envelope and read its current level.
    neon_envelope_process(&synth->envelope);
    float32x4_t envelope = synth->envelope.level;

    // LFO accumulators.
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

    // Active mask: everything that is not OFF.
    uint32x4_t active_mask = vmvnq_u32(vceqq_u32(synth->envelope.stage,
                                                 vdupq_n_u32(ENV_STATE_OFF)));

    // Global controls stay in params[].
    const float32x4_t hit_shape = vdupq_n_f32(
        synth->params[Synth::PARAM_HIT_SHAPE] / 100.0f);

    const float32x4_t body_tilt = vdupq_n_f32(
        synth->params[Synth::PARAM_BODY_TILT] / 100.0f);

    const float32x4_t drive = vdupq_n_f32(
        synth->params[Synth::PARAM_DRIVE] / 100.0f);

    // Transient and body shaping before the engines run.
    float32x4_t transient_env = fm_make_transient_env(envelope, hit_shape);
    float32x4_t body_env = fm_make_body_env(envelope, body_tilt);

    // Engine outputs.
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

    // Mix. Kick and Snare stay more transient; Metal and Perc lean more body-weighted.
    float32x4_t mix = vdupq_n_f32(0.0f);
    mix = vaddq_f32(mix, vmulq_n_f32(kick_out,  0.28f));
    mix = vaddq_f32(mix, vmulq_n_f32(snare_out, 0.24f));
    mix = vaddq_f32(mix, vmulq_n_f32(metal_out, 0.22f));
    mix = vaddq_f32(mix, vmulq_n_f32(perc_out,  0.26f));

    // Global drive from params[].
    float32x4_t drive_gain = fm_make_drive_gain(drive);
    mix = vmulq_f32(mix, drive_gain);

    // Soft clip for punch and glue.
    mix = fm_soft_clip(mix);

    // Master gain.
    mix = vmulq_n_f32(mix, synth->master_gain);

    return neon_horizontal_sum_alt(mix);
}

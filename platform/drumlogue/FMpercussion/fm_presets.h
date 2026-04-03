/**
 * @file fm_presets.h
 * @brief Factory presets for FM Percussion Synth
 *
 * Now includes 4 resonant synthesis presets
 */

#pragma once

#include <stdint.h>
#include "constants.h"
#include "resonant_synthesis.h"

// LFO target values (from lfo_enhanced.h)
#define LFO_TARGET_NONE       (0)
#define LFO_TARGET_PITCH      (1)
#define LFO_TARGET_INDEX      (2)
#define LFO_TARGET_ENV        (3)
#define LFO_TARGET_LFO2_PHASE (4)
#define LFO_TARGET_LFO1_PHASE (5)

// Engine modes
#define ENGINE_KICK     (0)
#define ENGINE_SNARE    (1)
#define ENGINE_METAL    (2)
#define ENGINE_PERC     (3)
#define ENGINE_RESONANT (4)

// from header.c
#define NUM_OF_PRESETS (12)
#define NAME_LENGTH    (12)

typedef enum {
  DEEP_TRIBAL = 0,
  METAL_STORM,
  CHORDAL_PERC,
  PHASE_DANCE,
  BIPOLAR_BASS,
  SNARE_ROLL,
  AMBIENT_METL,
  POLYRHYTHM,
  RESOKICK,
  RESOTOM,
  RESOSNARE,
  RESOMETAL,
  TOTAL_PRESETS = NUM_OF_PRESETS
} preset_numer_t;

typedef struct {
  char name[NAME_LENGTH];

  // Page 1: Probabilities
  uint8_t prob_kick;
  uint8_t prob_snare;
  uint8_t prob_metal;
  uint8_t prob_perc;

  // Page 2: Kick + Snare
  uint8_t kick_sweep;
  uint8_t kick_decay;
  uint8_t snare_noise;
  uint8_t snare_body;

  // Page 3: Metal + Perc
  uint8_t metal_inharm;
  uint8_t metal_bright;
  uint8_t perc_ratio;
  uint8_t perc_var;

  // Page 4: LFO1
  uint8_t lfo1_shape;   // 0-8
  uint8_t lfo1_rate;    // 0-100
  uint8_t lfo1_target;  // 0-5
  int8_t lfo1_depth;    // -100 to 100

  // Page 5: LFO2
  uint8_t lfo2_shape;   // 0-8
  uint8_t lfo2_rate;    // 0-100
  uint8_t lfo2_target;  // 0-5
  int8_t lfo2_depth;    // -100 to 100

  // Page 6: Envelope
  uint8_t env_shape;    // 0-127
  uint8_t voice_index;  // 0-VOICE_ALLOC_COUNT

  // NEW: Resonant parameters (using params 21-23)
  uint8_t resonant_mode;    // 0-4 (LP, BP, HP, Notch, Peak)
  uint8_t resonant_morph;   // 0-100
  uint8_t resonant_res;     // 0-100
  uint8_t resonant_center;  // 0-100 (maps to 50-8000 Hz)
  uint8_t engine_map[4];    // Which engine each voice uses
} fm_preset_t;

// Array defined in fm_presets.c (C compilation) to support C99 designated
// initializers that GCC 6 C++ does not implement for char array / array fields.
#ifdef __cplusplus
extern "C" {
#endif
extern const fm_preset_t FM_PRESETS[NUM_OF_PRESETS];
#ifdef __cplusplus
}
#endif
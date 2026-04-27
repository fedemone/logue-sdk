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

// LFO target values (from constants.h)
#define LFO_TARGET_NONE       (0)
#define LFO_TARGET_PITCH      (1)
#define LFO_TARGET_INDEX      (2)
#define LFO_TARGET_ENV        (3)
#define LFO_TARGET_LFO2_PHASE (4)
#define LFO_TARGET_LFO1_PHASE (5)
#define LFO_TARGET_RES_FREQ   (6)
#define LFO_TARGET_RESONANCE  (7)
#define LFO_TARGET_NOISE_MIX  (8)
#define LFO_TARGET_RES_MORPH  (9)
#define LFO_TARGET_METAL_GATE (10)

// Engine modes
#define ENGINE_KICK     (0)
#define ENGINE_SNARE    (1)
#define ENGINE_METAL    (2)
#define ENGINE_PERC     (3)
#define ENGINE_RESONANT (4)

// from header.c
#define NUM_OF_PRESETS (26)
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
  // New presets exploiting LFO phase sync, NOISE_MIX and RES_MORPH targets
  SLOW_ENV,      // 12: slow ramp LFO as second envelope
  WAH_DRUM,      // 13: LFO→RES_MORPH for auto-wah filter sweep
  NOISE_SWEEP,   // 14: LFO→NOISE_MIX for snare/metal texture sweep
  FM_BUZZ,       // 15: near-audio-rate LFO→INDEX for AM/FM texture
  GHOST_SNARE,   // 16: sparse ghostly snare hits with resonant filter
  RIM_PITCH,     // 17: slow ramp pitch envelope on metal/perc
  TOM_WAH,       // 18: resonant tom with filter morph sweep
  SHAKER,        // 19: high-density rattling metal texture
  // Character 1 (Gong) presets — EnvShape bit 7 set (128+env_index)
  GONG_HIT,      // 20: pure gong strike, long resonant tail
  TEMPLE_BELL,   // 21: temple bell, bright ring with slow LFO sweep
  METAL_GONG,    // 22: metal/gong hybrid, fast LFO index buzz
  // Euclidean tuning + MetalGate showcase presets
  DIM_KIT,       // 23: EuclTun=Dim7 [0,3,6,9] — all-voice dim7 chord spread
  WHOLE_PERC,    // 24: EuclTun=Whole [0,2,4,6] — whole-tone pitched perc
  HIHAT_SWITCH,  // 25: MetalGate LFO → open/closed hi-hat gate
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
  int8_t  lfo1_depth;    // -100 to 100

  // Page 5: EuclTun + LFO2
  // lfo2_shape is repurposed as EuclTun (Euclidean voice tuning spread).
  // 0=Off (all voices same pitch), 1-8=Euclidean spread modes (see EUCLID_MODE_*).
  // lfo2_rate/target/depth remain active LFO2 parameters.
  uint8_t lfo2_shape;   // 0-8 → EuclTun mode (EUCLID_MODE_*)
  uint8_t lfo2_rate;    // 0-100
  uint8_t lfo2_target;  // 0-10
  int8_t  lfo2_depth;    // -100 to 100

  // Page 6: Envelope
  uint8_t env_shape;    // 0-255: bit7=metal character (0=Cymbal, 1=Gong), bits[6:0]=envelope index 0-127
  uint8_t voice_index;  // 0-VOICE_ALLOC_COUNT

  // NEW: Resonant parameters (using params 21-23)
  uint8_t resonant_mode;    // 0-4 (LP, BP, HP, Notch, Peak)
  uint8_t resonant_morph;   // 0-100
  uint8_t resonant_res;     // 0-100
  uint8_t resonant_center;  // 0-100 (maps to 50-8000 Hz) - TODO unused and not mapped by UI
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
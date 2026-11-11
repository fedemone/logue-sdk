#pragma once
// Brachetti Bar — the presets: what each one plays and how it is voiced.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       preset_column_in
//   knobs.h          name_at
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalLayout
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"

// The knob layout is the modal engine's (brc::ModalLayout); header.c MUST
// match it slot for slot.
using Param = brc::ModalLayout;

enum Preset : uint8_t {
    k_Marimba = 0,      // 0  -sample: marimba-hit-c4_C_minor.wav (524Hz +/- 50Hz)
    k_TubularBell,      // 1  -samples: tubular-bell-47849.wav (oscillates between 1500Hz and 280Hz then settles to 1230Hz)
    k_Vibraphone,       // 2  -sample: vibraphone_C_major.wav (goes up to 1398Hz then settles to 273Hz), vibraphone_C_major1.wav  (262Hz +/- 20Hz)
    k_Woodblock,        // 3  -sample: Woodblock.wav (3500Hz +/- 100Hz), Woodblock1.wav (858Hz +/- 30Hz)
    k_Kalimba,          // 4  -sample: kalimba-e_E.wav (1398Hz +/- 50Hz)
    k_SteelPan,         // 5  -sample: steel-pan-Nova Drum Real C 432.wav (257Hz +/- 30Hz), steel-pan-PERCY-C4-SHort.wav (260Hz +/- 50Hz)
    k_Claves,           // 6  -sample: percussion-clave-like-hit-107112.mp3 (950Hz +/- 40Hz), wetclave.wav  (2629Hz +/- 20Hz)
    k_GlassBowl,        // 7  -sample: glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav
    k_SlitDrum,         // 8  -sample:
    k_GlassBottle,      // 9  -sample: GlassBottle.wav (2636Hz +/- 200Hz)
    kNumPresets         // 10 — count
};

// What the Program knob shows: this family's own preset names, in this unit's
// order.  A table of its own (taken from the original brachetti's 40-name
// table, slots noted), and `static`: internal linkage, so it is private to
// this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Marmba",   // slot 1 of the original's 40
    "TblrBel",  // slot 4 of the original's 40
    "Vibrph",   // slot 10 of the original's 40
    "Wodblk",   // slot 11 of the original's 40
    "Kalimba",  // slot 15 of the original's 40
    "StelPan",  // slot 16 of the original's 40
    "Claves",   // slot 17 of the original's 40
    "GlsBwl",   // slot 24 of the original's 40
    "SltDrm",   // slot 30 of the original's 40
    "GlsBotl",  // slot 34 of the original's 40
};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == kNumPresets, "one name per preset");

static inline const char* preset_name(uint8_t idx) {
    return brc::name_at(kPresetNames, idx, "Unknown");
}

// One row per preset, one column per knob in slot order: the knob
// values each preset ships with.  The Pitch column holds the preset's ROOT
// NOTE (MIDI), not a knob value: the Pitch knob is an offset in semitones
// from it.  The columns of the knobs the host owns (Pitch, Poly, Velocity,
// VlMllRes, Tone, Cutoff, Resnc) are never loaded; the static_asserts below
// pin them so a preset cannot quietly ship its own.
// int16_t: the stored range is [-10, 1999].
static constexpr int16_t kPresetKnobs[kNumPresets][Param::k_lastParamIndex] = {
    //  Prg Root  Ply  Vel MlRs MlSt VlRs VlSt Ptls  Mdl  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain NzMx NzRs NzFl NzFq Rsnc
    {   0,  72,   0,   1, 800,  13,   0,   0,   0,   6, 194,  -7,   0,   0,   5,   2,1999,   7,  20,   0, 300,   0,1200,  71},   // 0: Marimba   — exemplar BAR voice
    {   1,  72,   0,   1, 900,  34,   0,   0,   0,   1, 200,  30,   0,   0,  20,   1,1999,  18,   0,   5, 300,   0,1500,  71},   // 1: TblrBel
    {   2,  72,   0,   1, 500,  30,   0,   0,   0,   1, 200,  28,   0,   0,  18,   0,1999,  13,   0,   0, 300,   0,1000,  71},   // 2: Vibrph
    {   3,  48,   0,   1, 900,  50,   0,   0,   0,   2, 156,  24,   0,   0,   2,   1,1999,   3,   0,   5, 420,   0, 900,  71},   // 3: Wodblk
    {   4,  65,   0,   1, 700,  39,   0,   0,   0,   1, 192,   6,   0,   0,   5,   0,1999,   7,   3,  10, 260,   0, 720,  71},   // 4: Kalimba
    {   5,  60,   0,   1, 600,   0,   0,   0,   0,   4, 200,  18,   0,   0,  12,   0,1999,   9,   5,   0, 300,   0,1000,  71},   // 5: StelPan
    {   6,  79,   0,   1, 900,  48,   0,   0,   0,   2,  13,  -3,   0,   0,   1,   0,1999,   1,   0,  20, 260,   0, 800,  71},   // 6: Claves
    {   7,  76,   0,   1, 700,   5,   0,   0,   0,   4, 200,  30,   0,   0,  18,   1,1999,  18,   0,   0, 300,   0,1200,  71},   // 7: GlsBwl
    {   8,  60,   0,   1, 700,  27,   0,   0,   0,   6, 177,   8,   0,   0,  10,   1,1999,   3,   0,   0, 300,   0, 800,  71},   // 8: SltDrm
    {   9,  88,   0,   1, 100,  45,   0,   0,   0,   7, 200,   5,   0,   0,   5,   0,1999,  19,   0,  50, 110,   0, 390,  71}    // 9: GlsBotl
};
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramDkay, 0, 200),
              "Dkay is 0..200: every consumer saturates at 200");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramPartls, 0, 4),
              "Partls is 0..4 in this unit");

// Each preset's calibrated modal bank: ratios of modes 2-4, T60s of modes 1-4
// (ms), LoadPreset's mix, mode amplitudes 1-4, mode count, then the optional
// ratios and amplitudes of modes 5 and 6.
static const brc::ModalPresetConfig kModalConfigs[kNumPresets] = {
    /* k_Marimba: tuned-bar ratios 1:4:10; T60 calibrated to marimba-hit-c4 */ {4.00f, 10.0f, 0.0f, 1200.0f, 350.0f, 100.0f, 0.0f, 0.18f, 0.72f, 0.50f, 0.22f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TubularBell */ {2.756f, 5.404f, 0.0f, 2000.0f, 3000.0f, 0.0f, 0.0f, 0.22f, 0.18f, 0.90f, 0.55f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Vibraphone */ {4.00f, 10.0f, 20.0f, 800.0f, 360.0f, 200.0f, 120.0f, 0.20f, 0.80f, 0.58f, 0.40f, 0.30f, 6, 24.0f, 30.0f, 0.22f, 0.16f},
    /* k_Woodblock: T60 160ms per WoodBlock1.wav reference */ {2.756f, 0.0f, 0.0f, 160.0f, 80.0f, 0.0f, 0.0f, 0.18f, 0.90f, 0.60f, 0.0f, 0.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Kalimba: tine ratios 1:4:10.  Ref kalimba-e_E rings 2.4 s with centroid
       ~1.7 kHz; the old 600 ms / dark render measured 460 ms at 554 Hz.  T60
       extended and overtone lifted toward the measured reference. */ {4.00f, 10.0f, 0.0f, 1500.0f, 400.0f, 0.0f, 0.0f, 0.15f, 0.80f, 0.62f, 0.22f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SteelPan */ {2.00f, 3.00f, 4.00f, 1200.0f, 900.0f, 700.0f, 500.0f, 0.22f, 0.90f, 0.75f, 0.55f, 0.35f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Claves */ {2.756f, 5.404f, 0.0f, 60.0f, 25.0f, 0.0f, 0.0f, 0.16f, 0.70f, 0.45f, 0.0f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_GlassBowl: modes 5/6 at 6.37/8.10 for overtone content */ {2.09f, 3.35f, 4.77f, 2000.0f, 1600.0f, 1200.0f, 800.0f, 0.20f, 0.85f, 0.70f, 0.50f, 0.35f, 6, 6.37f, 8.10f, 0.22f, 0.15f},
    /* k_SlitDrum: tongue-drum bar modes 1:1.52:2.08 */ {1.52f, 2.08f, 0.0f, 1000.0f, 700.0f, 450.0f, 0.0f, 0.18f, 0.80f, 0.60f, 0.35f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_GlassBottle: Helmholtz cavity + neck modes 1:1.47:2.42 */ {1.47f, 2.42f, 0.0f, 500.0f, 320.0f, 190.0f, 0.0f, 0.20f, 0.85f, 0.60f, 0.30f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f}
};

// The rest of each preset's voicing.
struct Voicing {
    float modal_mix;       // the modal bank's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
    float chirp_hz;        // metallic FM chirp on the strike, 0 = none
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
};
static const Voicing kVoicing[kNumPresets] = {
    //                    modal_mix  onset_ms  chirp_hz  band_mix  split
    /* k_Marimba      */ {0.18000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_TubularBell  */ {0.22000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Vibraphone   */ {0.18000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Woodblock    */ {0.18000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Kalimba      */ {0.15000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_SteelPan     */ {0.22000f, 1.50000f, 200.00000f, 0.00000f, 0.00000f},
    /* k_Claves       */ {0.18000f, 0.50000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_GlassBowl    */ {0.20000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_SlitDrum     */ {0.18000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_GlassBottle  */ {0.20000f, 0.50000f, 0.00000f, 0.00000f, 0.00000f}
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Marmba  */ 0.15178f,
    /* 1  TblrBel */ 0.65466f,
    /* 2  Vibrph  */ 0.61489f,
    /* 3  Wodblk  */ 1.00000f,
    /* 4  Kalimba */ 0.47918f,
    /* 5  StelPan */ 0.17152f,
    /* 6  Claves  */ 1.00000f,
    /* 7  GlsBwl  */ 0.44971f,
    /* 8  SltDrm  */ 0.68733f,
    /* 9  GlsBotl */ 0.60871f
};

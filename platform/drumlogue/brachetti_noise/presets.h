#pragma once
// Brachetti Noise — the knob layout and the presets.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   common_knobs.h   CommonLayout
//   dsp_math.h       preset_column_in
//   knobs.h          HostKnob, name_at
#include "../brachetti_common/common_knobs.h"
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"

// ── The knobs, slot by slot ─────────────────────────────────────────────────
// MUST match header.c slot for slot: the preset table below has one column per
// slot, and a parameter added here without its row in header.c (or vice
// versa) silently shifts every column of every preset.  Pages 1-2 are the
// common knobs (../brachetti_common/common_knobs.h); Pitch's slot is blank:
// nothing in a noise voice follows the note.  Pages 3-5 are this family's:
// the grains, the noise burst, its balance, tail and velocity tone.
enum ParamIndex : uint8_t {
    k_paramProgram   = brc::CommonLayout::k_paramProgram,  // 0  page 1: common (brc::CommonLayout)
    k_paramNoPitch   = brc::CommonLayout::k_paramPitch,  // 1  blank: no Pitch
    k_paramPoly      = brc::CommonLayout::k_paramPoly,  // 2
    k_paramVelocity  = brc::CommonLayout::k_paramVelocity,  // 3
    k_paramTone      = brc::CommonLayout::k_paramTone,  // 4  page 2: common (brc::CommonLayout)
    k_paramCutoff    = brc::CommonLayout::k_paramCutoff,  // 5
    k_paramResnc     = brc::CommonLayout::k_paramResnc,  // 6
    k_paramGain      = brc::CommonLayout::k_paramGain,  // 7
    k_paramGrnRate   = brc::CommonLayout::k_firstDedicated,  // 8  page 3: dedicated — the grains
    k_paramGrnDepth,   // 9   (the original's MlltStif and MlltRes: also the exciter's mallet)
    k_paramGrnFade,    // 10
    k_paramSwell,      // 11
    k_paramNzMix,      // 12  page 4: dedicated
    k_paramNzRes,      // 13
    k_paramNzFltr,     // 14
    k_paramNzFltFrq,   // 15
    k_paramBand,       // 16  page 5: dedicated
    k_paramRel,        // 17
    k_paramVelTone,    // 18
    k_lastParamIndex     // marker
};
static_assert(k_lastParamIndex == 19, "header.c declares 19 params");

// ── The presets ─────────────────────────────────────────────────────────────
enum Preset : uint8_t {
    k_Clap = 0,         // 0  -sample: 07_Clap_05_SP.wav (1532 +/- 100Hz)
    k_Shaker,           // 1  — grain-pulse noise (enveloped-LFO AM)
    k_HiHatClosed,      // 2  — a short, bright noise chick
    k_ShakerFwd,        // 3  shaker, strong stroke: the seeds hit the end wall on the beat (voiced after 033_ShakerFwd.wav)
    k_ShakerBack,       // 4  shaker, weak stroke: a softer hit, more slide (voiced after 034_ShakerBack.wav)
    k_Maracas,          // 5  a pair of maracas: a bright seed cloud (voiced after 035_Maracas.wav)
    k_Caxixi,           // 6  caxixi: seeds on the basket's hard gourd base, a sharp chk (voiced after 036_Caxixi.wav)
    k_Cabasa,           // 7  cabasa: a steel bead chain twisted on a ridged cylinder (voiced after 037_Cabasa.wav)
    kNumPresets         // 3 — count
};

// What the Program knob shows: this family's own preset names, in this unit's
// order.  A table of its own (taken from the original brachetti's 40-name
// table, slots noted), and `static`: internal linkage, so it is private to
// this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Clap",     // slot 21 of the original's 40
    "Shaker",   // slot 22 of the original's 40
    "HHat-C",   // slot 25 of the original's 40
    "ShkFwd",  // added: voiced after the Pelle e Legno sample
    "ShkBack", // added: voiced after the Pelle e Legno sample
    "Maracas", // added: voiced after the Pelle e Legno sample
    "Caxixi",  // added: voiced after the Pelle e Legno sample
    "Cabasa",  // added: voiced after the Pelle e Legno sample
};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == kNumPresets, "one name per preset");

static inline const char* preset_name(uint8_t idx) {
    return brc::name_at(kPresetNames, idx, "Unknown");
}

// One row per preset: one column per knob in slot order (the knob values the
// preset ships with), then two hidden voicing columns after the bar:
//  * Root: the note the sequencer plays;
//  * Dky:  the preset's decay setting.  There is no Dkay knob here (it moves
//    nothing a noise voice plays); its one effect is how fast AllNoteOff
//    fades a voice, which this column keeps exact.
// The columns of the knobs the host owns (Poly, Velocity, VlMllRes, Tone,
// Cutoff, Resnc) are never loaded; the static_asserts below pin them so a
// preset cannot quietly ship its own.  int16_t: the stored range is [-10, 1999].
constexpr int kRootColumn  = k_lastParamIndex;
constexpr int kDecayColumn = k_lastParamIndex + 1;
static constexpr int16_t kPresetKnobs[kNumPresets][k_lastParamIndex + 2] = {
    //  Prg    -  Ply  Vel  Ton  Cut Rsnc Gain GRat GDep GFad Swel NzMx NzRs NzFl NzFq Band  Rel VTon |Root  Dky
    {   0,   0,   0,   1,   0,1999,  71,   5,  27, 500,   0,   0,  95, 950,   1, 200,   0,  19,   0,  60,  15},   // 0: Clap      — multi-burst AM (~55 Hz, NoteOn) + Rel 19 for the 'tcha' tail; NzFq 200 (BP 2 kHz) toward the reference centroid (~3 kHz)
    {   1,   0,   0,   1,   0,1999,  71,   5,  45, 500,   0,   0,  95, 940,   1, 550,   0,  19,   0,  84,  30},   // 1: Shaker    — Rel 19 so the sustained rattle (noise_am_decay = 1.0) is audible by default; raise Rel for a longer rattle
    {   2,   0,   0,   1,   0,1999,  71,   3,  37, 100,   0,   0,  90, 900,   2, 800,   0,  18,   0,  72,  12},    // 2: HHat-C    — a short, bright noise chick: a closed hi-hat
    {   3,   0,   0,   1,   0,1999,  71,   5,  40, 500,   0,   0,  95, 900,   2, 650,   0,  18,   0,  84,  20},   // 3: ShkFwd
    {   4,   0,   0,   1,   0,1999,  71,   5,  40, 500,   0,   0,  95, 900,   2, 505,   0,  19,   0,  84,  20},   // 4: ShkBack
    {   5,   0,   0,   1,   0,1999,  71,   5,  40, 500,   0,   0,  95, 900,   2, 400,   0,  19,   0,  84,  20},   // 5: Maracas
    {   6,   0,   0,   1,   0,1999,  71,   5,  40, 500,   0,   0,  95, 600,   1, 911,   0,  17,   0,  84,  20},   // 6: Caxixi
    {   7,   0,   0,   1,   0,1999,  71,   5,  40, 500,   0,   0,  95, 900,   2, 636,   0,  19,   0,  84,  20},   // 7: Cabasa
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramGrnFade, 0, 0) &&
              brc::preset_column_in(kPresetKnobs, k_paramSwell, 0, 0) &&
              brc::preset_column_in(kPresetKnobs, k_paramBand, 0, 0) &&
              brc::preset_column_in(kPresetKnobs, k_paramVelTone, 0, 0),
              "GrnFade, Swell, Band and VelTone are 0 on every preset: their mappings are the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");

// The knobs the host owns, with their header.c defaults: a Program change
// leaves them where they are and Init() seeds them (brc::HostKnob, knobs.h).
// MUST track header.c's `.init` fields for these slots.
static const brc::HostKnob kHostKnobs[] = {
    { k_paramPoly,     4    },
    { k_paramVelocity, 0    },
    { k_paramTone,     0    },
    { k_paramCutoff,   1999 },
    { k_paramResnc,    71   },
};

// What the knobs this unit's own knobs replaced shipped with — the exciter
// still reads them, so every preset plays as voiced: the strike position
// (HitPos 50), velocity → stiffness and → noise attack (VlMllStf, VlMllRes
// 0), and the Model knob, whose only job here was the noise band balance
// where a preset has none of its own.
constexpr int32_t kShippedHitPos  = 50;
constexpr int32_t kShippedVelKnob = 0;
static const uint8_t kShippedModel[kNumPresets] = {5, 6, 5, 6, 6, 6, 6, 6};

// The rest of each preset's voicing.
struct Voicing {
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
    // ── Presets added after the split (Clap and Shaker keep their own code
    // in gate_noise, and leave these 0) ──────────────────────────────────────
    // The noise gate from the table: an enveloped LFO on the noise level
    // (brc::NoiseGate).  gate_hz 0 = no gate.
    float gate_hz;         // grain / ridge rate
    float gate_depth;      // 0..0.99
    float gate_decay;      // per sample; 1.0 = it never fades
    float attack_rate;     // both noise envelopes' attack, 0 = NzRes's
};
static const Voicing kVoicing[kNumPresets] = {
    //                    band_mix  split
    /* k_Clap         */ {0.00000f, 0.00000f},
    /* k_Shaker       */ {0.00000f, 0.00000f},
    /* k_HiHatClosed  */ {0.00000f, 0.00000f},
    /* k_ShakerFwd    */{0.05f, 0.95f, 30.0f, 0.01f, 0.9995f, 0.106066017f},
    /* k_ShakerBack   */{0.05f, 0.95f, 35.4511971f, 0.5009375f, 0.9998f, 0.00532723691f},
    /* k_Maracas      */{0.34375f, 0.5f, 47.5682846f, 0.79f, 0.9995f, 0.00297301779f},
    /* k_Caxixi       */{0.05f, 0.95f, 0.0f, 0.0f, 1.0f, 0.9f},
    /* k_Cabasa       */{0.16875f, 0.44375f, 120.0f, 0.4f, 0.9995f, 0.0176776695f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Clap    */ 1.00000f,
    /* 1  Shaker  */ 0.30340f,
    /* 2  HHat-C  */ 0.86956f,
    /* 3  ShkFwd  */ 0.67162f,
    /* 4  ShkBack */ 0.49569f,
    /* 5  Maracas */ 0.18896f,
    /* 6  Caxixi  */ 1.00000f,
    /* 7  Cabasa  */ 0.69850f,
};

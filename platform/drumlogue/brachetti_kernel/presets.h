#pragma once
// Brachetti Kernel — the knob layout and the presets.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   common_knobs.h   CommonLayout
//   dsp_math.h       preset_column_in
//   knobs.h          HostKnob, name_at
#include "../brachetti_common/common_knobs.h"
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "modal_drum_data.h"

// ── The knobs, slot by slot ─────────────────────────────────────────────────
// MUST match header.c slot for slot: the preset table below has one column per
// slot, and a parameter added here without its row in header.c (or vice
// versa) silently shifts every column of every preset.  Pages 1-2 are the
// common knobs (../brachetti_common/common_knobs.h); Poly's slot is blank: the
// kernel always has its two kettles.  Pages 3-6 are this family's (no Model,
// NzRes or NzFltr: the kernel's noise is its own wedge).
enum ParamIndex : uint8_t {
    k_paramProgram   = brc::CommonLayout::k_paramProgram,  // 0  page 1: common (brc::CommonLayout)
    k_paramPitch     = brc::CommonLayout::k_paramPitch,  // 1
    k_paramNoPoly    = brc::CommonLayout::k_paramPoly,  // 2  blank: no Poly
    k_paramVelocity  = brc::CommonLayout::k_paramVelocity,  // 3
    k_paramTone      = brc::CommonLayout::k_paramTone,  // 4  page 2: common (brc::CommonLayout)
    k_paramCutoff    = brc::CommonLayout::k_paramCutoff,  // 5
    k_paramResnc     = brc::CommonLayout::k_paramResnc,  // 6
    k_paramGain      = brc::CommonLayout::k_paramGain,  // 7
    k_paramMlltRes   = brc::CommonLayout::k_firstDedicated,  // 8  page 3: dedicated
    k_paramMlltStif,   // 9
    k_paramVlMllRes,   // 10
    k_paramVlMllStf,   // 11
    k_paramPartls,     // 12  page 4: dedicated
    k_paramDkay,       // 13
    k_paramRel,        // 14
    k_paramMterl,      // 15
    k_paramHitPos,     // 16  page 5: dedicated
    k_paramInharm,     // 17
    k_paramTubRad,     // 18
    k_paramPunch,      // 19  the transient shaper (brc::Punch)
    k_paramNzMix,      // 20  page 6: dedicated
    k_paramNzFltFrq,   // 21
    k_lastParamIndex     // marker
};
static_assert(k_lastParamIndex == 22, "header.c declares 22 params");

// ── The presets ─────────────────────────────────────────────────────────────
enum Preset : uint8_t {
    k_Timpani = 0,      // 0  -sample: Orchestral-Timpani-C.wav (239 Hz ± 20 Hz)
    k_Taiko,            // 1  -sample: Taiko-Hit.wav (1582 Hz ± 50 Hz)
    kNumPresets         // 2  count
};

// What the Program knob shows: this family's own preset names, in this unit's
// order.  A table of its own (taken from the original brachetti's 40-name
// table, slots noted), and `static`: internal linkage, so it is private to
// this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Timpni",   // slot 5 of the original's 40
    "Taiko",    // slot 7 of the original's 40
};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == kNumPresets, "one name per preset");

static inline const char* preset_name(uint8_t idx) {
    return brc::name_at(kPresetNames, idx, "Unknown");
}

// One row per preset, one column per knob in slot order: the knob values the
// preset ships with.  The Pitch column holds the preset's ROOT NOTE (MIDI),
// not a knob value: the Pitch knob is an offset in semitones from it.  The
// columns of the knobs the host owns (Velocity, VlMllRes, Tone, Cutoff,
// Resnc) are never loaded; the static_asserts below pin them so a preset
// cannot quietly ship its own.  int16_t: the stored range is [-10, 1999].
//
//  * Timpani: note 52 (E3) — the kettle's dominant sustained partial measures
//    165.5 Hz ≈ E3 (the 110 Hz A2 principal sits a fifth below), so the
//    display names the pitch you hear; 52 is also the kernel recipe's root,
//    so the shipped row plays the approved render at ratio 1.
//  * Taiko: a bright open "TAAAN" — data-driven inharmonic modes and a bright
//    1472 Hz partial (ratio 16.86, the "AAN" vowel) from Taiko-Hit.wav, a
//    brighter crack (NzMix 52, NzFltFrq 800 ≈ 8 kHz) and a leaner boom for a
//    brighter, longer ring.
static constexpr int16_t kPresetKnobs[kNumPresets][k_lastParamIndex] = {
    //  Prg Root    -  Vel  Ton  Cut Rsnc Gain MlRs MlSt VlRs VlSt Ptls  Dky  Rel  Mtr  Hit InHm TbRd Pnch NzMx NzFq
    {   0,  52,   0,   1,   0,1999,  71,   4, 360,  30,   0,  40,   2, 150,  18,  10,  36,   1,   0,   0,   1, 380},   // 0: Timpani
    {   1,  41,   0,   1,   0,1999,  71,   5, 250,  45,   0,   0,   1, 120,  15,  10,  30,   0,  16,   0,  52, 800}    // 1: Taiko
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 (off) on every preset");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramDkay, 0, 200),
              "Dkay is 0..200: every consumer saturates at 200");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPartls, 0, 4),
              "Partls is 0..4 in this unit");

// The knobs the host owns, with their header.c defaults: a Program change
// leaves them where they are and Init() seeds them (brc::HostKnob, knobs.h).
// MUST track header.c's `.init` fields for these slots.
static const brc::HostKnob kHostKnobs[] = {
    { k_paramPitch,    0    },
    { k_paramVelocity, 0    },
    { k_paramTone,     0    },
    { k_paramCutoff,   1999 },
    { k_paramResnc,    71   },
    { k_paramVlMllRes, 0    },
};

// The kernel recipe behind each preset: the approved standalone renders
// (105_timp_wedge / 110_taiko_wedge; see modal_drum_data.h).
static const ModalDrumRecipe* const kRecipes[kNumPresets] = {
    &kTimpaniRecipe,
    &kTaikoRecipe
};

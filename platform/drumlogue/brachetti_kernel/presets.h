#pragma once
// Brachetti Kernel — the knob layout and the presets.
#include <cstdint>
#include "../brachetti_common/dsp_math.h"
#include "modal_drum_data.h"

// ── The knobs, slot by slot ─────────────────────────────────────────────────
// MUST match header.c slot for slot: the preset table below has one column per
// slot, and a parameter added here without its row in header.c (or vice
// versa) silently shifts every column of every preset.  (No Poly: the kernel
// has its own two kettles.  No Model, NzRes or NzFltr: the kernel's noise is
// its own wedge.)
enum ParamIndex : uint8_t {
    k_paramProgram = 0,  // 0
    k_paramPitch,        // 1
    k_paramVelocity,     // 2
    k_paramMlltRes,      // 3
    k_paramMlltStif,     // 4
    k_paramVlMllRes,     // 5
    k_paramVlMllStf,     // 6
    k_paramPartls,       // 7
    k_paramDkay,         // 8
    k_paramMterl,        // 9
    k_paramTone,         // 10
    k_paramHitPos,       // 11
    k_paramRel,          // 12
    k_paramInharm,       // 13
    k_paramCutoff,       // 14
    k_paramTubRad,       // 15
    k_paramGain,         // 16
    k_paramNzMix,        // 17
    k_paramNzFltFrq,     // 18
    k_paramResnc,        // 19
    k_lastParamIndex     // marker
};
static_assert(k_lastParamIndex == 20, "header.c declares 20 params");

// ── The presets ─────────────────────────────────────────────────────────────
enum Preset : uint8_t {
    k_Timpani = 0,      // 0  -sample: Orchestral-Timpani-C.wav (239 Hz ± 20 Hz)
    k_Taiko,            // 1  -sample: Taiko-Hit.wav (1582 Hz ± 50 Hz)
    kNumPresets         // 2  count
};

inline const char* preset_name(uint8_t idx) {
    static const char* const names[kNumPresets] = { "Timpni", "Taiko" };
    return (idx < kNumPresets) ? names[idx] : "Unknown";
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
    //  Prg Root  Vel MlRs MlSt VlRs VlSt Ptls  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain NzMx NzFq Rsnc
    {   0,  52,   1, 360,  30,   0,  40,   2, 150,  10,   0,  36,  18,   1,1999,   0,   4,   1, 380,  71},   // 0: Timpani
    {   1,  41,   1, 250,  45,   0,   0,   1, 120,  10,   0,  30,  15,   0,1999,  16,   5,  52, 800,  71}    // 1: Taiko
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
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

// The kernel recipe behind each preset: the approved standalone renders
// (105_timp_wedge / 110_taiko_wedge; see modal_drum_data.h).
static const ModalDrumRecipe* const kRecipes[kNumPresets] = {
    &kTimpaniRecipe,
    &kTaikoRecipe
};

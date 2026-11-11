#pragma once
// Brachetti Noise — the knob layout and the presets.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       preset_column_in
//   knobs.h          HostKnob, name_at
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"

// ── The knobs, slot by slot ─────────────────────────────────────────────────
// MUST match header.c slot for slot: the preset table below has one column per
// slot, and a parameter added here without its row in header.c (or vice
// versa) silently shifts every column of every preset.  (No Pitch, Partls,
// Dkay, Mterl, Inharm or TubRad: nothing in a noise voice depends on the
// note, and there is no modal body for the others to shape.)
enum ParamIndex : uint8_t {
    k_paramProgram = 0,  // 0
    k_paramPoly,         // 1
    k_paramVelocity,     // 2
    k_paramMlltRes,      // 3
    k_paramMlltStif,     // 4
    k_paramVlMllRes,     // 5
    k_paramVlMllStf,     // 6
    k_paramModel,        // 7
    k_paramTone,         // 8
    k_paramHitPos,       // 9
    k_paramRel,          // 10
    k_paramCutoff,       // 11
    k_paramGain,         // 12
    k_paramNzMix,        // 13
    k_paramNzRes,        // 14
    k_paramNzFltr,       // 15
    k_paramNzFltFrq,     // 16
    k_paramResnc,        // 17
    k_lastParamIndex     // marker
};
static_assert(k_lastParamIndex == 18, "header.c declares 18 params");

// ── The presets ─────────────────────────────────────────────────────────────
enum Preset : uint8_t {
    k_Clap = 0,         // 0  -sample: 07_Clap_05_SP.wav (1532 +/- 100Hz)
    k_Shaker,           // 1  — grain-pulse noise (enveloped-LFO AM)
    k_HiHatClosed,      // 2  — a short, bright noise chick
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
    //  Prg  Ply  Vel MlRs MlSt VlRs VlSt  Mdl  Ton  Hit  Rel  Cut Gain NzMx NzRs NzFl NzFq Rsnc  |Root  Dky
    {   0,   0,   1, 500,  27,   0,   0,   5,   0,  50,  19,1999,   5,  95, 950,   1, 200,  71,  60,  15},   // 0: Clap      — multi-burst AM (~55 Hz, NoteOn) + Rel 19 for the 'tcha' tail; NzFq 200 (BP 2 kHz) toward the reference centroid (~3 kHz)
    {   1,   0,   1, 500,  45,   0,   0,   6,   0,  50,  19,1999,   5,  95, 940,   1, 550,  71,  84,  30},   // 1: Shaker    — Rel 19 so the sustained rattle (noise_am_decay = 1.0) is audible by default; raise Rel for a longer rattle
    {   2,   0,   1, 100,  37,   0,   0,   5,   0,  50,  18,1999,   3,  90, 900,   2, 800,  71,  72,  12}    // 2: HHat-C    — a short, bright noise chick: a closed hi-hat
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
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
    { k_paramVlMllRes, 0    },
    { k_paramTone,     0    },
    { k_paramCutoff,   1999 },
    { k_paramResnc,    71   },
};

// The rest of each preset's voicing.
struct Voicing {
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
};
static const Voicing kVoicing[kNumPresets] = {
    //                    band_mix  split
    /* k_Clap         */ {0.00000f, 0.00000f},
    /* k_Shaker       */ {0.00000f, 0.00000f},
    /* k_HiHatClosed  */ {0.00000f, 0.00000f}
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
    /* 2  HHat-C  */ 0.86956f
};

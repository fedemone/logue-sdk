#pragma once
// Brachetti Kick — the presets: what each one plays and how it is voiced.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       phase_inc, preset_column_in
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
    k_Kick2 = 0,        // 0  — a solid kettledrum-bodied kick
    k_808Sub,           // 1
    k_KickDrum,         // 2  -sample: KickA-Hard-012-CloseRoom.wav (1016Hz +/- 100Hz)
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
    "Kick2",    // slot 0 of the original's 40
    "808Sub",   // slot 2 of the original's 40
    "Kick",     // slot 20 of the original's 40
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
    {   0,  36,   0,   1, 360,  30,   0,  40,   2,   3, 200,  10,   0,  36,  18,   1,1999,   0,   4,   1, 420,   0, 380,  71},   // 0: Kick2     — a solid kettledrum-bodied kick
    {   1,  36,   0,   1, 350,  35,   0,   0,   2,   5, 195,  -5,   0,  38,   6,   0,1999,   3,  14,   5, 220,   0, 220,  71},   // 1: 808Sub    — boom_osc pitch dive 160→45 Hz, depth on Inharm
    {   2,  36,   0,   1, 380,  35,   0,   0,   2,   5, 195,  -5,   0,  38,   6,   0,1999,   3,  12,  15, 220,   0, 220,  71}    // 2: Kick Drum
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
    /* k_Kick2: a fundamental-dominant kettledrum thump.  The boom carries
       most of this preset (boom_mix 0.85 vs modal_mix 0.46), so `boom_decay`
       in kVoicing must move with these T60s — shorten only one
       half and the tail only half shortens.  Dkay is the reference anchor, so
       the shipped knob position plays exactly this data either way. */
    {1.340f, 1.664f, 1.980f, 660.0f, 240.0f, 120.0f, 60.0f, 0.38f, 0.95f, 0.20f, 0.12f, 0.08f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_808Sub: no modal bank */ {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_KickDrum: no modal bank */ {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f}
};

// The rest of each preset's voicing.
struct Voicing {
    float modal_mix;       // the modal bank's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
    float chirp_hz;        // metallic FM chirp on the strike, 0 = none
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
    // The pitch envelope LoadPreset gives a voice (a strike restores it only
    // where the unit says so).
    float pitch_env;       // start level
    float pitch_decay;     // per sample
    float pitch_amount;    // Hz at pitch_env = 1
    // The boom: a low sine body under the strike (boom_mix 0 = none).
    float boom_inc;        // phase increment (fixed booms; swept ones recompute it)
    float boom_env;        // start level
    float boom_decay;      // per sample
    float boom_mix;
    float boom_attack_env; // attack ramp start
    float boom_attack_inc; // attack ramp step per sample
};
static const Voicing kVoicing[kNumPresets] = {
    //                    modal  onset  chirp  band  split | pitch env decay amount | boom inc env decay mix attack_env attack_inc
    /* k_Kick2        */ {0.46000f, 2.00000f, 200.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, brc::phase_inc(58.0f), 1.00000f, 0.9996333f, 0.85000f, 0.00000f, 0.00300f},
    /* k_808Sub       */ {0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 1.00000f, 0.99900f, 115.00000f, 0.00589f, 1.00000f, 0.99982f, 0.60000f, 0.00000f, 0.00100f},
    /* k_KickDrum     */ {0.00000f, 2.00000f, 0.00000f, 0.00000f, 0.05000f, 1.00000f, 0.99890f, 9.00000f, brc::phase_inc(58.0f), 1.00000f, 0.99990448f, 0.70000f, 0.00000f, 0.00350f}
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Kick2   */ 0.26584f,
    /* 1  808Sub  */ 0.35315f,
    /* 2  Kick    */ 0.29166f
};

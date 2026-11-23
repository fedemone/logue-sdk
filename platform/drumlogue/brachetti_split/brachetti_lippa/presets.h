#pragma once
// Brachetti Lippa — the presets: what each one plays and how it is voiced.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       preset_column_in
//   knobs.h          name_at
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalLayout
//   models.h         k_Drumhead
#include "../../brachetti_common/dsp_math.h"
#include "../../brachetti_common/knobs.h"
#include "../../brachetti_common/modal_bank.h"
#include "../../brachetti_common/modal_synth.h"
#include "../../brachetti_common/models.h"

// The knob layout is the modal engine's (brc::ModalLayout); header.c MUST
// match it slot for slot.
using Param = brc::ModalLayout;
// Six standard slots carry knobs of BrcLippa's own: the knobs they displaced
// acted on parts of the voice these presets do not use (the bank's mode count
// and template, the noise burst's filter) or were a second ring length.  Each
// loads with the preset and ships 0, where its mapping is the identity.  Punch
// is the family's transient shaper, as on every Brachetti unit.
enum LoopParam : uint8_t {
    k_paramBend    = Param::k_paramVlMllRes,  // 10: the glide's depth (× ¼ .. × 4, and added where none)
    k_paramBendTm  = Param::k_paramPartls,    // 12: the glide's time   (× ¼ .. × 4)
    k_paramSnares  = Param::k_paramModel,     // 13: the snares' level  (× ¼ .. × 4, and added where none)
    k_paramPunch   = Param::k_paramRel,       // 15: the transient shaper (brc::Punch)
    k_paramBlend   = Param::k_paramNzRes,     // 21: Karplus & Strong's drum blend, added (0 .. 100 %)
    k_paramSnrTone = Param::k_paramNzFltr,    // 22: the snares' band, ± 1 octave
};

// Presets 0-2 are voiced after recordings of drums, by measurement (the
// README, "How the presets were voiced", and
// ../../tools/brachetti_equiv/match_specs.py): the original brachetti's reference
// recordings (its samples/ folder, on the original_brachetti branch) and a
// Pelle e Legno sample.  3-7 are Miguel Moreno's drums.  (Five more presets,
// voiced as drums the same way, rang as strings: they are BrcPizzico's.)
enum Preset : uint8_t {
    k_Snare,            // 0  acoustic snare: the head through an all-pass, the wires on its motion (voiced after acoustic-snare.wav)
    k_SnareHeavy,       // 1  heavy snare: a deep, slack snare struck hard (voiced after snare heavy.wav)
    k_Bendir,           // 2  bendir: a frame drum with a snare string across the head (voiced after 020_BendirDoum.wav)
    // Miguel Moreno's own drums: mymembrane~'s help and demo settings, through its port (voice.h mm::Membrane)
    k_MmSnare,          // 3  mymembrane~'s help patch: a snare-like drum, its snares on (Miguel Moreno's settings)
    k_MmKick,           // 4  mymembrane~ demo 1: a dark, deep drum, the all-pass at full gain (Miguel Moreno's settings)
    k_MmSnare2,         // 5  mymembrane~ demo 2: a lower snare-like drum (Miguel Moreno's settings)
    k_MmTom,            // 6  mymembrane~ demo 3: a tom, the all-pass's gain negative (Miguel Moreno's settings)
    k_MmFloor,          // 7  mymembrane~ demo 4: a long, bright floor drum (Miguel Moreno's settings)
    kNumPresets         // count
};

// What the Program knob shows.  `static`, at namespace scope: private to this
// unit (a static local of an inline function would be STB_GNU_UNIQUE, and the
// drumlogue's loader shares one copy of those among every loaded unit).
static const char* const kPresetNames[] = {
    "Snare",  
    "SnrHvy", 
    "Bendir", 
    "MmSnare", // Miguel Moreno's settings
    "MmKick",  // Miguel Moreno's settings
    "MmSnr2",  // Miguel Moreno's settings
    "MmTom",   // Miguel Moreno's settings
    "MmFloor", // Miguel Moreno's settings
};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == kNumPresets, "one name per preset");

static inline const char* preset_name(uint8_t idx) {
    return brc::name_at(kPresetNames, idx, "Unknown");
}

// One row per preset, one column per knob in slot order: the knob values each
// preset ships with.  The Pitch column holds the preset's ROOT NOTE (MIDI):
// the Pitch knob is an offset in semitones from it.  The columns of the knobs
// the host owns (Pitch, Poly, Velocity, Tone, Cutoff, Resnc) are never
// loaded; the static_asserts below pin them.
static constexpr int16_t kPresetKnobs[kNumPresets][Param::k_lastParamIndex] = {
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain MlRs MlSt Bend VlSt BnTm Snrs  Dky Pnch  Mtr  Hit InHm ApDl NzMx Blnd SnTn NzFq
    {   0,  60,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 0: Snare
    {   1,  47,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 1: SnrHvy
    {   2,  41,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 2: Bendir
    {   3,  54,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 3: MmSnare
    {   4,  32,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 4: MmKick
    {   5,  45,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 5: MmSnr2
    {   6,  48,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 6: MmTom
    {   7,  41,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 7: MmFloor
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBend, 0, 0),
              "Bend is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBendTm, 0, 0),
              "BendTm is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramSnares, 0, 0),
              "Snares is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 (off) on every preset");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBlend, 0, 0),
              "Blend is 0 on every preset: it only adds to the preset's own blend");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramSnrTone, 0, 0),
              "SnrTone is 0 on every preset: its mapping is the identity there");
// What the standard knobs whose slots went to the family's own play at: Rel
// (the exciter's noise release, the bank's fine ring length) each preset's
// own; Partls the bank's own mode count (2 = no offset); Model the drum head
// (the bank's template and the noise burst's band balance); NzRes and NzFltr
// the noise burst's filter at its header.c defaults (30 %, low-pass).
static const int8_t kShippedRel[kNumPresets] = {10, 10, 10, 10, 10, 10, 10, 10};
constexpr int32_t kFixedPartls = 2;
constexpr int32_t kFixedModel  = brc::k_Drumhead;
constexpr int32_t kFixedNzRes  = 300;
constexpr int32_t kFixedNzFltr = 0;
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramDkay, 0, 200),
              "Dkay is 0..200: every consumer saturates at 200");

// Each preset's modal bank (mode count 0 = none: the loop alone): ratios of
// modes 2-4, T60s of modes 1-4 (ms), LoadPreset's mix, mode amplitudes 1-4,
// mode count, then the optional ratios and amplitudes of modes 5 and 6.
static const brc::ModalPresetConfig kModalConfigs[kNumPresets] = {
    /* k_Snare: acoustic snare: the head through an all-pass, the wires on its motion */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SnareHeavy: heavy snare: a deep, slack snare struck hard */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Bendir: bendir: a frame drum with a snare string across the head */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmSnare: mymembrane~'s help patch: a snare-like drum, its snares on (Miguel Moreno's settings) */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmKick: mymembrane~ demo 1: a dark, deep drum, the all-pass at full gain (Miguel Moreno's settings) */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmSnare2: mymembrane~ demo 2: a lower snare-like drum (Miguel Moreno's settings) */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmTom: mymembrane~ demo 3: a tom, the all-pass's gain negative (Miguel Moreno's settings) */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmFloor: mymembrane~ demo 4: a long, bright floor drum (Miguel Moreno's settings) */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
};

// The rest of each preset's voicing: the exciter beside the loop, and the
// loop drum (voice.h).  Times in ms, frequencies in Hz; the loop follows the
// played note from the preset's root.
struct Voicing {
    float modal_mix;       // the modal bank's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
    float exciter_gain;    // the mallet + noise burst beside the loop (0 = silent)
    // The loop: its level; the frequency of its first resonance at the root;
    // its T60 there; the T60 at 3 kHz as a fraction of that (the loss
    // filter is designed from the two: lower = darker, the highs dying sooner);
    // the all-pass's delay as a fraction of the period and its gain (the
    // inharmonicity); Karplus & Strong's drum blend (the chance a trip round
    // the loop flips sign, 0 = off).
    float lp_level, lp_hz, lp_t60_ms, lp_hf, ap_ratio, ap_gain, blend;
    // The excitation: the sine cycle's length in periods, the noise beside it
    // and its low-pass corner, the strike position (0 = centre; the comb's
    // delay in periods).
    float exc_ratio, exc_noise, exc_noise_hz, exc_pos;
    // The glide: how far above the note a full-velocity strike starts
    // (0.1 = 10 %), and its time constant.
    float bend, bend_ms;
    // The snares: their level, the band-pass the noise rings through (3 kHz,
    // Q 2 in the article; 3 kHz, Q 1 in mymembrane~), the follower's attack
    // and release, and how far their noise is the sparse crackle of
    // mymembrane~ (0 = white noise, 1 = all crackle).
    float sn_level, sn_hz, sn_q, sn_att_ms, sn_rel_ms, sn_crackle;
    // mymembrane~ (voice.h mm::Membrane) in place of the loop drum, when
    // mm_level > 0 (lp_level then 0): its level, and the patch's own controls —
    // the delay's frequency (Hz at the root: not the pitch, which the
    // all-pass and the filters pull down), feedback, damping (0-1), the
    // all-pass's gain and frequency (Hz at the root), the snares' noise level.
    float mm_level, mm_hz, mm_feedback, mm_damping, mm_apgain, mm_apf_hz, mm_snares;
};
static const Voicing kVoicing[kNumPresets] = {
    /* k_Snare        */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 264.0f, 352.919097f, 0.182287104f, 0.238125f, -0.0375f, 0.0f, 0.707106781f, 0.0f, 1600.7106f, 0.0f, 0.0f, 40.0f, 0.978572062f, 2075.86482f, 0.7f, 1.0f, 5.0f, 0.2921875f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SnareHeavy   */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 127.0f, 367.695526f, 0.115665812f, 0.334160156f, 0.29296875f, 0.0f, 1.09050773f, 2.24609375f, 931.393359f, 0.0f, 0.04015625f, 48.6098944f, 0.917004043f, 4019.00257f, 1.61923514f, 1.0f, 29.3571619f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Bendir       */{0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 1.0f, 100.870445f, 494.974747f, 0.115665812f, 0.2725f, 0.525f, 0.0f, 0.840896415f, 1.7421875f, 809.070598f, 0.0f, 0.03984375f, 20.8854756f, 0.126134462f, 4831.471f, 0.989949494f, 1.0f, 21.4442001f, 0.846875f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MmSnare      */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 277.6f, 0.7908f, 0.4409f, 0.6378f, 169.2f, 0.1018f},
    /* k_MmKick       */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 109.7f, 1.0f, 0.07874f, 1.0f, 500.0f, 0.01786f},
    /* k_MmSnare2     */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 245.3f, 0.8677f, 0.4016f, 0.3071f, 174.5f, 0.1266f},
    /* k_MmTom        */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 314.2f, 0.9214f, 0.2362f, -0.5591f, 257.0f, 0.01156f},
    /* k_MmFloor      */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 134.1f, 0.9838f, 0.748f, 0.3701f, 309.4f, 0.001f},
};

// Output trim: gain-stages each preset into the master limiter, calibrated as
// every Brachetti unit's (the body peak over [10 ms, 1 s] at velocity 127 on
// the root, on a body target of 1.5, clamped to <= 1):
// ../../tools/brachetti_equiv/preset_probe calib reprints this table.
static const float kOutTrim[kNumPresets] = {
    /* 0  Snare   */ 0.48404f,
    /* 1  SnrHvy  */ 0.44734f,
    /* 2  Bendir  */ 0.54714f,
    /* 3  MmSnare */ 1.00000f,
    /* 4  MmKick  */ 1.00000f,
    /* 5  MmSnr2  */ 1.00000f,
    /* 6  MmTom   */ 1.00000f,
    /* 7  MmFloor */ 1.00000f,
};

#pragma once
// Brachetti Pizzico — the presets: what each one plays and how it is voiced.
#include <cstdint>

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       preset_column_in
//   knobs.h          name_at
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalLayout
//   models.h         k_Drumhead
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"
#include "../brachetti_common/models.h"

// The knob layout is the modal engine's (brc::ModalLayout); header.c MUST
// match it slot for slot.
using Param = brc::ModalLayout;
// Six standard slots carry knobs of the unit's own (BrcLippa's, the snares
// renamed Buzz: on a string they are a buzzing bridge): the knobs they
// displaced acted on parts of the voice these presets do not use (the bank's
// mode count and template, the noise burst's filter) or were a second ring
// length.  Each loads with the preset and ships 0, where its mapping is the
// identity.  Punch is the family's transient shaper, as on every Brachetti unit.
enum LoopParam : uint8_t {
    k_paramBend    = Param::k_paramVlMllRes,  // 10: the glide's depth (× ¼ .. × 4, and added where none)
    k_paramBendTm  = Param::k_paramPartls,    // 12: the glide's time   (× ¼ .. × 4)
    k_paramBuzz    = Param::k_paramModel,     // 13: the buzz's level   (× ¼ .. × 4, and added where none)
    k_paramPunch   = Param::k_paramRel,       // 15: the transient shaper (brc::Punch)
    k_paramBlend   = Param::k_paramNzRes,     // 21: Karplus & Strong's drum blend, added (0 .. 100 %)
    k_paramBuzzTone = Param::k_paramNzFltr,   // 22: the buzz's band, ± 1 octave
};

// The five strings were voiced in BrcLippa as drums, by measurement, after
// recordings of a rack tom, a floor tom, a kick, a taiko and a surdo (the
// README, "Where the strings come from"); they rang like plucked strings (the
// loop is the Karplus-Strong string algorithm), so each is named for the
// string it comes closest to, and its loop sits at the exact pitch of its
// root.
enum Preset : uint8_t {
    k_Harp,             // 0  harp: a gut string plucked, warm, half a second (voiced in BrcLippa as a drum after Tom1-001-CloseRoom.wav)
    k_Oud,              // 1  oud: a fretless lute's string, warm, a second, the pitch settling after the pluck (voiced in BrcLippa as a drum after Tom2-004-CloseRoom.wav)
    k_Fretless,         // 2  fretless bass: a low string thumbed hard, its pitch dropping into the note (voiced in BrcLippa as a drum after KickA-Hard-012-CloseRoom.wav)
    k_Guitar,           // 3  guitar: a low string, the brightest and longest of these (voiced in BrcLippa as a drum after Taiko-Hit.wav)
    k_BassPizz,         // 4  double bass pizzicato: very dark, about 0.6 s (voiced in BrcLippa as a drum after 027_SurdoOpen.wav)
    kNumPresets         // count
};

// What the Program knob shows.  `static`, at namespace scope: private to this
// unit (a static local of an inline function would be STB_GNU_UNIQUE, and the
// drumlogue's loader shares one copy of those among every loaded unit).
static const char* const kPresetNames[] = {
    "Harp",   
    "Oud",    
    "Fretls", 
    "Guitar", 
    "BassPiz",
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
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain MlRs MlSt Bend VlSt BnTm Buzz  Dky Pnch  Mtr  Hit InHm ApDl NzMx Blnd BzTn NzFq
    {   0,  59,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 0: Harp
    {   1,  52,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 1: Oud
    {   2,  39,   0,   1,   0,1999,  71,   0, 800,  45,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 2: Fretls
    {   3,  42,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 3: Guitar
    {   4,  34,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 4: BassPiz
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBend, 0, 0),
              "Bend is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBendTm, 0, 0),
              "BendTm is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBuzz, 0, 0),
              "Buzz is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 (off) on every preset");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBlend, 0, 0),
              "Blend is 0 on every preset: it only adds to the preset's own blend");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramBuzzTone, 0, 0),
              "BuzzTon is 0 on every preset: its mapping is the identity there");
// What the standard knobs whose slots went to the family's own play at: Rel
// (the exciter's noise release, the bank's fine ring length) each preset's
// own; Partls the bank's own mode count (2 = no offset); Model the drum head
// (the bank's template and the noise burst's band balance); NzRes and NzFltr
// the noise burst's filter at its header.c defaults (30 %, low-pass).
static const int8_t kShippedRel[kNumPresets] = {10, 10, 10, 10, 10};
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
    /* k_Harp: harp: a gut string plucked, warm, half a second */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Oud: oud: a fretless lute's string, warm, a second, the pitch settling after the pluck */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Fretless: fretless bass: a low string thumbed hard, its pitch dropping into the note */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Guitar: guitar: a low string, the brightest and longest of these */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BassPizz: double bass pizzicato: very dark, about 0.6 s */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
};

// The rest of each preset's voicing: the exciter beside the loop, and the
// loop (voice.h: BrcLippa's loop drum, unchanged).  Times in ms, frequencies
// in Hz; the loop follows the played note from the preset's root.
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
    // The buzz (BrcLippa's snares, 0 on every string): its level, the
    // band-pass the noise rings through, the follower's attack and release,
    // and how far the noise is mymembrane~'s sparse crackle.
    float sn_level, sn_hz, sn_q, sn_att_ms, sn_rel_ms, sn_crackle;
};
static const Voicing kVoicing[kNumPresets] = {
    /* k_Harp         */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 246.941651f, 821.401454f, 0.393288363f, 0.1625f, 0.185742187f, 0.0f, 1.04427378f, 0.828125f, 2020.41268f, 0.109375f, 0.088671875f, 42.685616f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Oud          */{0.0f, 0.0f, 0.0f, 0.0f, 0.0625f, 1.0f, 164.813778f, 1706.27112f, 0.11660917f, 0.4375f, 0.553125f, 0.0f, 0.978572062f, 0.5f, 4430.47844f, 0.0458984375f, 0.1984375f, 39.1428825f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Fretless     */{0.0f, 0.0f, 0.0f, 0.0f, 0.068359375f, 1.0f, 77.7817459f, 713.766576f, 0.380715287f, 0.3f, 0.0754394531f, 0.0f, 0.978572062f, 0.5f, 1268.1936f, 0.09765625f, 0.348828125f, 40.0f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Guitar       */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 92.4986057f, 1783.81067f, 0.626564269f, 0.3f, -0.0515625f, 0.0f, 0.68080451f, 1.625f, 2536.3872f, 0.0625f, 0.19953125f, 15.5058488f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_BassPizz     */{0.0f, 0.0f, 0.0f, 0.0f, 0.01171875f, 1.0f, 58.2704702f, 1280.13653f, 0.226374664f, 0.41859375f, 0.3f, 0.0f, 0.703287997f, 0.171875f, 3491.17458f, 0.0f, 0.051171875f, 71.0150597f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
};

// Output trim: gain-stages each preset into the master limiter, calibrated as
// every Brachetti unit's (the body peak over [10 ms, 1 s] at velocity 127 on
// the root, on a body target of 1.5, clamped to <= 1):
// ../tools/brachetti_equiv/preset_probe calib reprints this table.
static const float kOutTrim[kNumPresets] = {
    /* 0  Harp    */ 0.67314f,
    /* 1  Oud     */ 0.94968f,
    /* 2  Fretls  */ 0.68473f,
    /* 3  Guitar  */ 0.61405f,
    /* 4  BassPiz */ 0.62008f,
};

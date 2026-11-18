#pragma once
// Brachetti Lippa — the presets: what each one plays and how it is voiced.
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

// Every preset is voiced after a recording, by measurement: see the README,
// "How the presets were voiced", and ../tools/brachetti_equiv/match_specs.py.
// The kit pieces are voiced after the original brachetti's reference
// recordings (its samples/ folder, on the original_brachetti branch), the
// frame drum and the surdo after the Pelle e Legno samples.
enum Preset : uint8_t {
    k_RackTom,          // 0  rack tom: a 12-inch tom, close-miked (voiced after Tom1-001-CloseRoom.wav)
    k_FloorTom,         // 1  floor tom: a 16-inch tom, its pitch sagging as it rings (voiced after Tom2-004-CloseRoom.wav)
    k_Snare,            // 2  acoustic snare: the head through an all-pass, the wires on its motion (voiced after acoustic-snare.wav)
    k_SnareHeavy,       // 3  heavy snare: a deep, slack snare struck hard (voiced after snare heavy.wav)
    k_Kick,             // 4  kick: a hard beater on a damped bass drum (voiced after KickA-Hard-012-CloseRoom.wav)
    k_Taiko,            // 5  taiko: a big cowhide head, a heavy bachi (voiced after Taiko-Hit.wav)
    k_Bendir,           // 6  bendir: a frame drum with a snare string across the head (voiced after 020_BendirDoum.wav)
    k_Surdo,            // 7  surdo: the samba bass drum, open (voiced after 027_SurdoOpen.wav)
    kNumPresets         // count
};

// What the Program knob shows.  `static`, at namespace scope: private to this
// unit (a static local of an inline function would be STB_GNU_UNIQUE, and the
// drumlogue's loader shares one copy of those among every loaded unit).
static const char* const kPresetNames[] = {
    "RackTom",
    "FlrTom", 
    "Snare",  
    "SnrHvy", 
    "Kick",   
    "Taiko",  
    "Bendir", 
    "Surdo",  
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
    {   0,  58,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 0: RackTom
    {   1,  52,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 1: FlrTom
    {   2,  60,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 2: Snare
    {   3,  47,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 3: SnrHvy
    {   4,  40,   0,   1,   0,1999,  71,   0, 800,  45,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 4: Kick
    {   5,  42,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 5: Taiko
    {   6,  41,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 6: Bendir
    {   7,  34,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   0, 160,   0,   0,   0,   0,  10,  10,   0,   0, 800},   // 7: Surdo
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
    /* k_RackTom: rack tom: a 12-inch tom, close-miked */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_FloorTom: floor tom: a 16-inch tom, its pitch sagging as it rings */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Snare: acoustic snare: the head through an all-pass, the wires on its motion */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SnareHeavy: heavy snare: a deep, slack snare struck hard */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Kick: kick: a hard beater on a damped bass drum */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Taiko: taiko: a big cowhide head, a heavy bachi */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Bendir: bendir: a frame drum with a snare string across the head */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Surdo: surdo: the samba bass drum, open */
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
};
static const Voicing kVoicing[kNumPresets] = {
    /* k_RackTom      */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 240.0f, 821.401454f, 0.393288363f, 0.1625f, 0.185742187f, 0.0f, 1.04427378f, 0.828125f, 2020.41268f, 0.109375f, 0.088671875f, 42.685616f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_FloorTom     */{0.0f, 0.0f, 0.0f, 0.0f, 0.0625f, 1.0f, 166.098104f, 1706.27112f, 0.11660917f, 0.4375f, 0.553125f, 0.0f, 0.978572062f, 0.5f, 4430.47844f, 0.0458984375f, 0.1984375f, 39.1428825f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Snare        */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 264.0f, 352.919097f, 0.182287104f, 0.238125f, -0.0375f, 0.0f, 0.707106781f, 0.0f, 1600.7106f, 0.0f, 0.0f, 40.0f, 0.978572062f, 2075.86482f, 0.7f, 1.0f, 5.0f, 0.2921875f},
    /* k_SnareHeavy   */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 127.0f, 367.695526f, 0.115665812f, 0.334160156f, 0.29296875f, 0.0f, 1.09050773f, 2.24609375f, 931.393359f, 0.0f, 0.04015625f, 48.6098944f, 0.917004043f, 4019.00257f, 1.61923514f, 1.0f, 29.3571619f, 0.3f},
    /* k_Kick         */{0.0f, 0.0f, 0.0f, 0.0f, 0.068359375f, 1.0f, 79.0f, 713.766576f, 0.380715287f, 0.3f, 0.0754394531f, 0.0f, 0.978572062f, 0.5f, 1268.1936f, 0.09765625f, 0.348828125f, 40.0f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Taiko        */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 90.0286297f, 1783.81067f, 0.626564269f, 0.3f, -0.0515625f, 0.0f, 0.68080451f, 1.625f, 2536.3872f, 0.0625f, 0.19953125f, 15.5058488f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
    /* k_Bendir       */{0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 1.0f, 100.870445f, 494.974747f, 0.115665812f, 0.2725f, 0.525f, 0.0f, 0.840896415f, 1.7421875f, 809.070598f, 0.0f, 0.03984375f, 20.8854756f, 0.126134462f, 4831.471f, 0.989949494f, 1.0f, 21.4442001f, 0.846875f},
    /* k_Surdo        */{0.0f, 0.0f, 0.0f, 0.0f, 0.01171875f, 1.0f, 58.0f, 1280.13653f, 0.226374664f, 0.41859375f, 0.3f, 0.0f, 0.703287997f, 0.171875f, 3491.17458f, 0.0f, 0.051171875f, 71.0150597f, 0.0f, 3000.0f, 2.0f, 1.0f, 30.0f, 0.0f},
};

// Output trim: gain-stages each preset into the master limiter, calibrated as
// every Brachetti unit's (the body peak over [10 ms, 1 s] at velocity 127 on
// the root, on a body target of 1.5, clamped to <= 1):
// ../tools/brachetti_equiv/preset_probe calib reprints this table.
static const float kOutTrim[kNumPresets] = {
    /* 0  RackTom */ 0.68381f,
    /* 1  FlrTom  */ 0.94044f,
    /* 2  Snare   */ 0.48404f,
    /* 3  SnrHvy  */ 0.44734f,
    /* 4  Kick    */ 0.69208f,
    /* 5  Taiko   */ 0.60808f,
    /* 6  Bendir  */ 0.54714f,
    /* 7  Surdo   */ 0.62087f,
};

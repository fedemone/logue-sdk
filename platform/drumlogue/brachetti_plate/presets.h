#pragma once
// Brachetti Plate — the presets: what each one plays and how it is voiced.
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
// One standard slot carries a plate knob of its own: VlMllRes' (velocity →
// noise-attack speed reached only Tick) is Chirp, the metallic FM chirp of
// the strike.  It loads with the preset (0 on every row).
enum PlateParam : uint8_t {
    k_paramChirp = Param::k_paramVlMllRes,   // 10
    k_paramPunch = Param::k_paramRel,   // 15: the transient shaper (brc::Punch)
};

enum Preset : uint8_t {
    k_Cowbell = 0,      // 0  -sample: Cowbell_2.wav (408Hz +/- 30Hz)
    k_Triangle,         // 1  -sample: Triangle-Bell_C#.wav (3753Hz +/- 100Hz), Triangle-Bell_F5.wav (795Hz +/- 100Hz)
    k_BellTree,         // 2  -sample:
    k_Tick,             // 3  — a metallic closed-hat chick + a low "clack" mode
    k_AgogoHi,          // 4  agogo, the high cone bell, A#5 (voiced after 053_AgogoHi.wav)
    k_AgogoLo,          // 5  agogo, the low cone bell, F5 (voiced after 054_AgogoLo.wav)
    k_Zills,            // 6  finger cymbals clashed: slowly beating partials from 2.5 kHz (voiced after 059_Zills.wav)
    kNumPresets         // 4 — count
};

// What the Program knob shows: this family's own preset names, in this unit's
// order.  A table of its own (taken from the original brachetti's 40-name
// table, slots noted), and `static`: internal linkage, so it is private to
// this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Cowbel",   // slot 18 of the original's 40
    "Trngle",   // slot 19 of the original's 40
    "BelTre",   // slot 29 of the original's 40
    "Tick",     // slot 35 of the original's 40
    "AgogoHi", // added: voiced after the Pelle e Legno sample
    "AgogoLo", // added: voiced after the Pelle e Legno sample
    "Zills",   // added: voiced after the Pelle e Legno sample
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
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain MlRs MlSt Chrp VlSt Ptls  Mdl  Dky Pnch  Mtr  Hit InHm TbRd NzMx NzRs NzFl NzFq
    {   0,  67,   0,   1,   0,1999,  71,  30, 800,  42,   0,   0,   0,   4, 175,   0,  20,   0,   3,   3,  40, 300,   0,1000},   // 0: Cowbell   — short clank (modal cfg 180 ms) + strike noise (NzMx 40) toward the bright reference clank
    {   1,  69,   0,   1,   0,1999,  71,   0, 900,  47,   0,   0,   0,   1,  60,   0,  20,   0,   2,  20,  10, 300,   0,1500},   // 1: Triangle  — NzMx 10 feeds the raised shimmer (NoteOn) for the >8 kHz metal sheen.  Dkay 60 is the ANCHOR of the short T60s in kModalConfigs — change the two together or the knob just re-lengthens the ring.
    {   2,  84,   0,   1,   0,1999,  71,   0, 900,  42,   0,   0,   0,   1, 200,   0,  20,   0,   1,   3,   0, 300,   0,1200},   // 2: BelTre
    {   3,  79,   0,   1,   0,1999,  71,   0, 900,  50,   0,   0,   0,   4, 160,   0,  14,   0,   2,   3,  58, 960,   2, 900},    // 3: Tick      — a metallic closed-hat chick + clack mode (modal cfg)
    {   4,  82,   0,   1,   0,1999,  71,   0, 900,  45,   0,   0,   2,   4, 175,   0,  20,   0,   0,  10, 100, 300,   0, 849},   // 4: AgogoHi
    {   5,  77,   0,   1,   0,1999,  71,   0, 900,  45,   0,   0,   2,   4, 175,   0,  20,   0,   0,  10,  58, 300,   0,1200},   // 5: AgogoLo
    {   6,  99,   0,   1,   0,1999,  71,   0, 900,  41,   0,   0,   2,   4, 175,   0,  20,   0,   0,  10,  15,  62,   0,1800},   // 6: Zills
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramChirp, 0, 0),
              "Chirp is 0 on every preset: its mapping is the identity there");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 (off) on every preset");
// What Rel, whose slot Punch took, shipped with: the exciter's noise release
// and the ring's fine length still read it, so every preset plays as voiced.
static const int8_t kShippedRel[kNumPresets] = {4, 15, 8, 2, 10, 10, 10};
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
    /* k_Cowbell: plate ratios.  Ref Cowbell_2 is a 120 ms clank with a ~5.6 kHz
       early centroid; the 500 ms ring read as a mellow bell.  T60s shortened to
       the clank, upper modes lifted, and modes 5/6 (ratios 6.9/9.8 ≈ 2.7/3.8 kHz
       at the shipped note) supply the missing clank brightness. */ {2.00f, 2.68f, 4.30f, 180.0f, 140.0f, 100.0f, 70.0f, 0.22f, 0.70f, 0.72f, 0.60f, 0.48f, 6, 6.90f, 9.80f, 0.55f, 0.40f},
    /* k_Triangle: 6 modes.  A real triangle's audible spectrum is dominated by
       >5 kHz partials (ref centroid 8.5 kHz); modes 4-6 add the 4-8 kHz sheen.
       The ring is short (t60 ≈ 275 ms).  Dkay is a REFERENCE ANCHOR —
       LoadPreset captures the row's own Dkay (60, displayed 600) into
       m_anchor.decay, so t60_scale is exactly 1.0 at the shipped value: to
       change the length, scale the T60s here, not the Dkay column. */
    {2.756f, 5.404f, 9.00f, 790.0f, 658.0f, 461.0f, 329.0f, 0.15f, 0.80f, 0.55f, 0.30f, 0.85f, 6, 13.1f, 17.9f, 0.80f, 0.70f},
    /* k_BellTree */ {2.01f, 2.76f, 0.0f, 900.0f, 700.0f, 0.0f, 0.0f, 0.17f, 0.80f, 0.60f, 0.0f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Tick: a metallic closed-hat chick + a low "clack" under it (mode 4
       at ratio 1.40, 50ms, strong) */
    {2.92f, 6.37f, 1.40f, 45.0f, 28.0f, 316.0f, 70.0f, 0.30f, 0.85f, 0.70f, 0.50f, 1.25f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_AgogoHi: agogo, the high cone bell, A#5 */
    {1.0059703f, 2.46992708f, 3.91988428f, 1100.0f, 707.106781f, 550.0f, 350.0f, 0.2f, 1.0f, 0.366666667f, 0.888888889f, 0.892857143f, 6, 5.4298397f, 7.0997904f, 0.666666667f, 0.5f},
    /* k_AgogoLo: agogo, the low cone bell, F5 */
    {1.00606271f, 2.47015396f, 3.92024435f, 1100.0f, 125.0f, 550.0f, 350.0f, 0.2f, 1.0f, 0.366666667f, 0.888888889f, 0.892857143f, 6, 5.43033847f, 7.10044257f, 0.666666667f, 0.5f},
    /* k_Zills: finger cymbals clashed: slowly beating partials from 2.5 kHz */
    {0.992400261f, 1.45041261f, 1.46234504f, 2508.16779f, 2500.0f, 2300.0f, 2500.0f, 0.2f, 0.39f, 1.0f, 0.546666667f, 0.3f, 6, 2.12529782f, 2.57933269f, 0.6f, 0.5f},
};

// The rest of each preset's voicing.
struct Voicing {
    float modal_mix;       // the modal bank's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
    float chirp_hz;        // metallic FM chirp on the strike (every plate preset
                           // chirps; at 0 Hz it only opens the bank's attack)
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
    float noise_gain;      // level of the noise burst beside the bank
    // Shimmer: a high-pass of the noise burst mixed after the bank, keeping
    // the upper metal alive.  shimmer_decay 1.0 = it lasts as long as the
    // voice: Cowbell and Bell Tree ship that way (the original meant 0.9992,
    // but its max() against the reset value 1.0 never let it decay).
    float shimmer_mix;     // 0 = none
    float shimmer_decay;   // per sample
    float click_decay;     // click-band decay override, 0 = NzRes's (Triangle:
                           // keeps the click band feeding the sheen ~1.8 s)
    bool  hat;             // the hat filter on the click band (see voice.h)
    float hat_mode;        // FastSVF mode: 0 LP, 1 BP, 2 HP
    float hat_hz;          // 0 = keep the filter's current coefficients
    float hat_q;           // < 0.1 = 1.1
};
static const Voicing kVoicing[kNumPresets] = {
    //               modal_mix  onset  chirp_hz  band_mix  split  gain  shim_mix shim_dec click_dec hat  mode  hz  q
    /* k_Cowbell   */ {0.22000f, 0.00000f, 900.00000f, 0.45000f, 0.75000f, 5.0f, 0.22f, 1.0f, 0.0f, false, 0.00000f, 0.00000f, 0.00000f},
    /* k_Triangle  */ {0.40000f, 0.00000f, 1800.00000f, 0.15000f, 0.96000f, 7.0f, 0.35f, 0.9996f, 0.000080f, false, 0.00000f, 0.00000f, 0.00000f},
    /* k_BellTree  */ {0.17000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 5.0f, 0.22f, 1.0f, 0.0f, false, 0.00000f, 0.00000f, 0.00000f},
    /* k_Tick      */ {0.24000f, 0.50000f, 3400.00000f, 0.86000f, 0.39000f, 5.0f, 0.0f, 1.0f, 0.0f, true, 2.00000f, 6000.00000f, 0.80000f},
    /* k_AgogoHi      */{1.5f, 0.0f, 900.0f, 0.0f, 0.0f, 5.0f, 0.0f, 1.0f, 0.0f, false, 0.0f, 0.0f, 0.0f},
    /* k_AgogoLo      */{1.5f, 0.0f, 700.0f, 0.0f, 0.0f, 5.0f, 0.0f, 1.0f, 0.0f, false, 0.0f, 0.0f, 0.0f},
    /* k_Zills        */{1.03747164f, 0.0f, 2102.24104f, 0.0f, 0.0f, 5.0f, 0.1f, 0.9998f, 0.0f, false, 0.0f, 0.0f, 0.0f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Cowbel  */ 0.11212f,
    /* 1  Trngle  */ 0.25227f,
    /* 2  BelTre  */ 0.60091f,
    /* 3  Tick    */ 1.00000f,
    /* 4  AgogoHi */ 0.04782f,
    /* 5  AgogoLo */ 0.04987f,
    /* 6  Zills   */ 0.10432f,
};

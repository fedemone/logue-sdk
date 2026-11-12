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
// One standard slot carries a bar knob of its own: VlMllRes' (velocity →
// noise-attack speed did nothing on the bars that have no noise) is Punch,
// the voice's transient: a punchier hit, or a slower swell.  It loads with the preset (0 on every row).
enum BarParam : uint8_t {
    k_paramPunch = Param::k_paramVlMllRes,   // 10
};

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
    k_WoodBlockHi,      // 10 high wood block: 1.15 kHz over a 1.11 kHz partner (voiced after 044_WoodBlockHi.wav)
    k_TempleBlock,      // 11 temple block (mokugyo): a hollow carved wood bell around C5 (voiced after 046_TempleBlock.wav)
    k_SlitDrumHi,       // 12 log-drum tongue, E4, rubber mallet, over the trunk's cavity (voiced after 047_SlitDrumHi.wav)
    k_SlitDrumLo,       // 13 log-drum tongue, A3 (voiced after 048_SlitDrumLo.wav)
    k_Bamboo,           // 14 bamboo tube knocked with a stick: odd air-column harmonics over the wall (voiced after 049_BambooKnock.wav)
    k_Coconut,          // 15 two coconut half-shells clapped: a hollow clop (voiced after 050_Coconut.wav)
    k_UduTone,          // 16 udu: a fingertip on the clay belly over the pot's 112 Hz air (voiced after 052_UduTone.wav)
    k_KalimbaE4,        // 17 kalimba tine, E4: a clamped-free bar, its 'ping' 6.37x up (voiced after 055_KalimbaE4.wav)
    k_KalimbaA4,        // 18 kalimba tine, A4 (voiced after 056_KalimbaA4.wav)
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
    "WdBlkHi", // added: voiced after the Pelle e Legno sample
    "TmplBlk", // added: voiced after the Pelle e Legno sample
    "SltDrHi", // added: voiced after the Pelle e Legno sample
    "SltDrLo", // added: voiced after the Pelle e Legno sample
    "Bamboo",  // added: voiced after the Pelle e Legno sample
    "Coconut", // added: voiced after the Pelle e Legno sample
    "UduTone", // added: voiced after the Pelle e Legno sample
    "KlmbE4",  // added: voiced after the Pelle e Legno sample
    "KlmbA4",  // added: voiced after the Pelle e Legno sample
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
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain MlRs MlSt Pnch VlSt Ptls  Mdl  Dky  Rel  Mtr  Hit InHm TbRd NzMx NzRs NzFl NzFq
    {   0,  72,   0,   1,   0,1999,  71,  20, 800,  13,   0,   0,   0,   6, 194,   5,  -7,   0,   2,   7,   0, 300,   0,1200},   // 0: Marimba   — exemplar BAR voice
    {   1,  72,   0,   1,   0,1999,  71,   0, 900,  34,   0,   0,   0,   1, 200,  20,  30,   0,   1,  18,   5, 300,   0,1500},   // 1: TblrBel
    {   2,  72,   0,   1,   0,1999,  71,   0, 500,  30,   0,   0,   0,   1, 200,  18,  28,   0,   0,  13,   0, 300,   0,1000},   // 2: Vibrph
    {   3,  48,   0,   1,   0,1999,  71,   0, 900,  50,   0,   0,   0,   2, 156,   2,  24,   0,   1,   3,   5, 420,   0, 900},   // 3: Wodblk
    {   4,  65,   0,   1,   0,1999,  71,   3, 700,  39,   0,   0,   0,   1, 192,   5,   6,   0,   0,   7,  10, 260,   0, 720},   // 4: Kalimba
    {   5,  60,   0,   1,   0,1999,  71,   5, 600,   0,   0,   0,   0,   4, 200,  12,  18,   0,   0,   9,   0, 300,   0,1000},   // 5: StelPan
    {   6,  79,   0,   1,   0,1999,  71,   0, 900,  48,   0,   0,   0,   2,  13,   1,  -3,   0,   0,   1,  20, 260,   0, 800},   // 6: Claves
    {   7,  76,   0,   1,   0,1999,  71,   0, 700,   5,   0,   0,   0,   4, 200,  18,  30,   0,   1,  18,   0, 300,   0,1200},   // 7: GlsBwl
    {   8,  60,   0,   1,   0,1999,  71,   0, 700,  27,   0,   0,   0,   6, 177,  10,   8,   0,   1,   3,   0, 300,   0, 800},   // 8: SltDrm
    {   9,  88,   0,   1,   0,1999,  71,   0, 100,  45,   0,   0,   0,   7, 200,   5,   5,   0,   0,  19,  50, 110,   0, 390},    // 9: GlsBotl
    {  10,  85,   0,   1,   0,1999,  71,   0, 800,  45,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  17, 420,   0, 637},   // 10: WdBlkHi
    {  11,  72,   0,   1,   0,1999,  71,   0, 800,   2,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   0, 420,   0, 566},   // 11: TmplBlk
    {  12,  64,   0,   1,   0,1999,  71,   0,1000,   5,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   0, 300,   0, 300},   // 12: SltDrHi
    {  13,  57,   0,   1,   0,1999,  71,   0, 800,  20,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   0, 300,   0, 300},   // 13: SltDrLo
    {  14,  64,   0,   1,   0,1999,  71,   0, 576,  45,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   8, 420,   0, 900},   // 14: Bamboo
    {  15,  74,   0,   1,   0,1999,  71,   0, 688,  28,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   0, 420,   0, 700},   // 15: Coconut
    {  16,  45,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   5, 300,   0, 400},   // 16: UduTone
    {  17,  64,   0,   1,   0,1999,  71,   0, 800,  39,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  12, 260,   0, 605},   // 17: KlmbE4
    {  18,  69,   0,   1,   0,1999,  71,   0, 632,  33,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,   0, 260,   0, 720},   // 18: KlmbA4
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 on every preset: its mapping is the identity there");
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
    /* k_GlassBottle: Helmholtz cavity + neck modes 1:1.47:2.42 */ {1.47f, 2.42f, 0.0f, 500.0f, 320.0f, 190.0f, 0.0f, 0.20f, 0.85f, 0.60f, 0.30f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_WoodBlockHi: high wood block: 1.15 kHz over a 1.11 kHz partner */
    {1.03947711f, 1.10982784f, 2.66521029f, 118.920712f, 190.0f, 90.0f, 50.0f, 0.2f, 0.228f, 1.0f, 0.266666667f, 0.15f, 5, 4.60526692f, 0.0f, 0.233333333f, 0.0f},
    /* k_TempleBlock: temple block (mokugyo): a hollow carved wood bell around C5 */
    {0.79789603f, 0.931483893f, 1.50979129f, 227.042032f, 137.948025f, 257.0f, 120.0f, 0.2f, 0.8f, 0.413333333f, 0.231111111f, 1.0f, 6, 2.31055401f, 3.59865443f, 0.8f, 0.4f},
    /* k_SlitDrumHi: log-drum tongue, E4, rubber mallet, over the trunk's cavity */
    {0.4999582f, 2.91996218f, 3.84979949f, 602.92273f, 424.264069f, 220.0f, 120.0f, 0.2f, 1.0f, 0.416666667f, 0.888888889f, 0.785714286f, 6, 6.10082488f, 8.70072887f, 0.555555556f, 0.416666667f},
    /* k_SlitDrumLo: log-drum tongue, A3 */
    {0.5f, 2.92f, 3.85f, 723.681731f, 713.524269f, 220.0f, 120.0f, 0.2f, 1.0f, 0.416666667f, 0.888888889f, 0.785714286f, 6, 6.1f, 8.7f, 0.555555556f, 0.416666667f},
    /* k_Bamboo: bamboo tube knocked with a stick: odd air-column harmonics over the wall */
    {1.03419751f, 3.10259254f, 4.39890406f, 218.814109f, 184.0f, 100.0f, 60.0f, 0.2f, 0.0458181818f, 0.848484848f, 0.395959596f, 1.0f, 6, 5.17098757f, 10.5573698f, 0.424242424f, 0.848484848f},
    /* k_Coconut: two coconut half-shells clapped: a hollow clop */
    {0.926566717f, 0.958405743f, 1.04370709f, 31.25f, 169.281943f, 280.0f, 300.0f, 0.2f, 0.0918f, 0.18f, 0.236f, 0.154285714f, 6, 2.01147725f, 2.05199965f, 1.0f, 0.39f},
    /* k_UduTone: udu: a fingertip on the clay belly over the pot's 112 Hz air */
    {3.73545455f, 7.24545455f, 10.6272727f, 303.56024f, 238.618478f, 90.0f, 60.0f, 0.2f, 0.909090909f, 1.0f, 0.707070707f, 0.649350649f, 5, 15.2818182f, 0.0f, 0.505050505f, 0.0f},
    /* k_KalimbaE4: kalimba tine, E4: a clamped-free bar, its 'ping' 6.37x up */
    {6.36991646f, 17.798876f, 0.0f, 2934.41294f, 356.762135f, 80.0f, 0.0f, 0.15f, 0.666666667f, 1.0f, 0.118518519f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_KalimbaA4: kalimba tine, A4 */
    {6.37f, 17.8f, 0.0f, 2934.41294f, 327.15232f, 80.0f, 0.0f, 0.15f, 0.666666667f, 1.0f, 0.118518519f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
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
    /* k_GlassBottle  */ {0.20000f, 0.50000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_WoodBlockHi  */{1.5f, 0.5625f, 0.0f, 0.0f, 0.0f},
    /* k_TempleBlock  */{0.4f, 0.375f, 0.0f, 0.0f, 0.0f},
    /* k_SlitDrumHi   */{0.282842712f, 0.5625f, 0.0f, 0.0f, 0.0f},
    /* k_SlitDrumLo   */{0.282842712f, 2.25f, 0.0f, 0.0f, 0.0f},
    /* k_Bamboo       */{1.5f, 0.1875f, 0.0f, 0.0f, 0.0f},
    /* k_Coconut      */{0.282842712f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_UduTone      */{0.8f, 0.5625f, 0.0f, 0.0f, 0.0f},
    /* k_KalimbaE4    */{0.6f, 0.84375f, 0.0f, 0.0f, 0.0f},
    /* k_KalimbaA4    */{0.6f, 0.375f, 0.0f, 0.0f, 0.0f},
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
    /* 9  GlsBotl */ 0.60871f,
    /* 10 WdBlkHi */ 0.25923f,
    /* 11 TmplBlk */ 0.45358f,
    /* 12 SltDrHi */ 0.37759f,
    /* 13 SltDrLo */ 0.40214f,
    /* 14 Bamboo  */ 0.16548f,
    /* 15 Coconut */ 1.00000f,
    /* 16 UduTone */ 0.13682f,
    /* 17 KlmbE4  */ 0.19476f,
    /* 18 KlmbA4  */ 0.19050f,
};

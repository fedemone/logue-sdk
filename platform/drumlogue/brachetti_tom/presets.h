#pragma once
// Brachetti Tom — the presets: what each one plays and how it is voiced.
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
    k_Djambe = 0,       // 0  -samples: Djambe-B3.wav (starts over 600Hz and settles to 215Hz), Djambe-A3.wav (starts over 1100Hz and settles to 747Hz +/- 10Hz)
    k_AcousticTom,      // 1  -sample: Tom1-001-CloseRoom.wav (428Hz +/- 50Hz), Tom2-004-CloseRoom.wav (288Hz + 50Hz)
    k_Taiko2,           // 2  — displayed as "DeepBs": a deep Taiko-style membrane as a bass voice
    k_Conga,            // 3  -sample: Bongo_Conga2.wav (286Hz +/- 10Hz)
    k_Handpan,          // 4  -sample: Tabla-Drum-Hit-D4_.wav (237Hz +/- 30Hz)
    k_Bongo,            // 5  -sample: Bongo_Conga_Mute4.wav (430Hz +/- 100Hz)
    k_RackTom,          // 6  — mounted rack tom at F3 (174.6 Hz): the high drum to Ac Tom's low one.  Tighter head, shorter ring, brighter stick, less shell air (voiced from rock-rack-tom-1.wav).
    k_DjembeTone,       // 7  tone: the head's 262 Hz pitch with its 0.6-1.9 kHz ring (voiced after 002_DjembeTone.wav)
    k_DjembeSlap,       // 8  slap: a skin crack over a 1.07 kHz ring (voiced after 003_DjembeSlap.wav)
    k_DjembeBass,       // 9  bass: the goblet's 74 Hz air, pushed by the palm, settling from 7 % sharp (voiced after 001_DjembeBass.wav)
    k_CongaOpen,        // 10 open tone at 271 Hz (mode 1 is its weak partner at the C4 root) (voiced after 004_CongaOpen.wav)
    k_CongaMute,        // 11 muted tone: 657 Hz, closed by the hand (voiced after 005_CongaMute.wav)
    k_CongaSlap,        // 12 open slap: the head's edge-mode cloud under a skin crack (voiced after 006_CongaSlap.wav)
    k_CongaHeel,        // 13 heel of the palm: a closed low thud (voiced after 007_CongaHeel.wav)
    k_Tumba,            // 14 the big conga's open tone, G3 (voiced after 008_TumbaOpen.wav)
    k_BongoMacho,       // 15 the small bongo (voiced after 009_BongoMacho.wav)
    k_BongoHembra,      // 16 the large bongo (voiced after 010_BongoHembra.wav)
    k_TablaNa,          // 17 Na: the dayan's harmonic series on C#4, 2nd and 3rd harmonics ringing (voiced after 011_TablaNa.wav)
    k_TablaTin,         // 18 Tin: harmonics of A#3, the 3rd strongest (voiced after 012_TablaTin.wav)
    k_TablaTun,         // 19 Tun: the open centre stroke, the C4 fundamental rings (voiced after 013_TablaTun.wav)
    k_TablaGe,          // 20 Ge: the bayan's open bass on G2, settling from 6 % sharp (voiced after 014_TablaGe.wav)
    k_TablaGeBend,      // 21 Ge with the heel sliding toward the syahi: the bass bends up ~5 semitones (voiced after 015_TablaGeBend.wav)
    k_TablaKe,          // 22 Ke: a flat palm on the bayan, left there (voiced after 016_TablaKe.wav)
    k_DarbukaDoum,      // 23 doum: the goblet's 96 Hz air under the head (voiced after 017_DarbukaDoum.wav)
    k_DarbukaTek,       // 24 tek: the ring finger on the rim, the bright edge modes ring (voiced after 018_DarbukaTek.wav)
    k_DarbukaKa,        // 25 ka: the other hand's rim stroke, a softer tek (voiced after 019_DarbukaKa.wav)
    k_TalkUp,           // 26 talking drum squeezed as it rings: D3 up about a fifth (voiced after 025_TalkDrumUp.wav)
    k_TalkDown,         // 27 talking drum released as it rings: down about a fifth to D3 (voiced after 026_TalkDrumDn.wav)
    k_SurdoOpen,        // 28 surdo, felt mallet, A#1: a long 58 Hz boom settling from 6 % sharp (voiced after 027_SurdoOpen.wav)
    k_SurdoMute,        // 29 the surdo damped by the hand (voiced after 028_SurdoMute.wav)
    k_Tamborim,         // 30 tamborim: a small tight head under a thin stick (voiced after 029_Tamborim.wav)
    k_UduBass,          // 31 udu: the palm over the side hole, the pot's air rising 85 -> 112 Hz (voiced after 051_UduBass.wav)
    kNumPresets         // 7 — count
};

// What the Program knob shows: this family's own preset names, in this unit's
// order.  A table of its own (taken from the original brachetti's 40-name
// table, slots noted), and `static`: internal linkage, so it is private to
// this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Djambe",   // slot 6 of the original's 40
    "Ac Tom",   // slot 12 of the original's 40
    "DeepBs",   // slot 23 of the original's 40
    "Conga",    // slot 27 of the original's 40
    "Handpn",   // slot 28 of the original's 40
    "Bongo",    // slot 33 of the original's 40
    "RackTom",  // slot 39 of the original's 40
    "DjmTone", // added: voiced after the Pelle e Legno sample
    "DjmSlap", // added: voiced after the Pelle e Legno sample
    "DjmBass", // added: voiced after the Pelle e Legno sample
    "CngOpen", // added: voiced after the Pelle e Legno sample
    "CngMute", // added: voiced after the Pelle e Legno sample
    "CngSlap", // added: voiced after the Pelle e Legno sample
    "CngHeel", // added: voiced after the Pelle e Legno sample
    "Tumba",   // added: voiced after the Pelle e Legno sample
    "BngMach", // added: voiced after the Pelle e Legno sample
    "BngHmbr", // added: voiced after the Pelle e Legno sample
    "TblaNa",  // added: voiced after the Pelle e Legno sample
    "TblaTin", // added: voiced after the Pelle e Legno sample
    "TblaTun", // added: voiced after the Pelle e Legno sample
    "TblaGe",  // added: voiced after the Pelle e Legno sample
    "TblGeBd", // added: voiced after the Pelle e Legno sample
    "TblaKe",  // added: voiced after the Pelle e Legno sample
    "DrbDoum", // added: voiced after the Pelle e Legno sample
    "DrbTek",  // added: voiced after the Pelle e Legno sample
    "DrbKa",   // added: voiced after the Pelle e Legno sample
    "TalkUp",  // added: voiced after the Pelle e Legno sample
    "TalkDn",  // added: voiced after the Pelle e Legno sample
    "SurdOpn", // added: voiced after the Pelle e Legno sample
    "SurdMut", // added: voiced after the Pelle e Legno sample
    "Tambrim", // added: voiced after the Pelle e Legno sample
    "UduBass", // added: voiced after the Pelle e Legno sample
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
    {   0,  48,   0,   1, 600,  35,   0,   0,   1,   5, 152,   0,   0,  35,  12,   1,1999,  15,   5,   7, 450,   0, 500,  71},   // 0: Djambe
    {   1,  45,   0,   1, 450,  30,   0,   0,   2,   5, 200,   0,   0,  44,  11,   0,1999,   9,   5,  30, 360,   0, 520,  71},   // 1: Ac Tom    — stick transient layer + NzMx 30 for the close-mic attack brightness
    {   2,  41,   0,   1, 250,  39,   0,   0,   1,   5, 200,  10,   0,  30,  15,   0,1999,  11,   5,   9, 550,   0, 130,  71},   // 2: Taiko2    — a deep Taiko-style membrane as a bass voice
    {   3,  62,   0,   1, 600,  43,   0,   0,   1,   5, 158,   3,   0,   0,  10,   1,1999,   9,   0,  15, 520,   0, 450,  71},   // 3: Conga     — 250 ms open tone; slap softened by NzFq 450
    {   4,  62,   0,   1, 700,  30,   0,   0,   0,   4, 190,  22,   0,   0,  20,   0,1999,  18,   0,   5, 300,   0,1000,  71},   // 4: Handpn
    {   5,  50,   0,   1, 650,  41,   0,   0,   0,   5, 162, -10,   0,   0,   8,   0,1999,   0,   0,   0, 520,   0, 450,  71},   // 5: Bongo     — + wood 'tock' mode 5 in modal config
    {   6,  53,   0,   1, 420,  36,   0,   0,   2,   5, 190,   0,   0,  52,  12,   0,1999,   6,   5,  48, 300,   0, 700,  71},    // 6: RackTom   — note 53 (F3, 174.6 Hz), the rack tom over Ac Tom's 45/110 Hz.  What voices it are the ABSOLUTE columns: NzMx 38 and NzFq 700 (7 kHz vs Ac Tom's 5.2) put more bright stick in the mix, NzRs 300 keeps it short.  Dky/Rel/Hit/Mtr/TbRd are REFERENCE ANCHORS — LoadPreset captures each into m_anchor from this very row, so every consumer's 2^(k·Δ) is exactly 1 here and they set where the knobs SIT, not the voicing.  Ring length lives in kModalConfigs, shell in kVoicing
    {   7,  60,   0,   1, 600,  50,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,  30, 441,   1, 400,  71},   // 7: DjmTone
    {   8,  84,   0,   1, 600,  18,   0,   0,   2,   5, 160,   0,   0,  60,  10,   0,1999,  10,   0,  46, 381,   2, 501,  71},   // 8: DjmSlap
    {   9,  38,   0,   1, 300,  10,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,   0, 300,   0, 180,  71},   // 9: DjmBass
    {  10,  60,   0,   1, 600,  41,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,  40, 500,   0, 450,  71},   // 10: CngOpen
    {  11,  76,   0,   1, 600,   4,   0,   0,   2,   5, 160,   0,   0,  20,  12,   0,1999,  10,   0,  40, 857,   0, 600,  71},   // 11: CngMute
    {  12,  72,   0,   1, 600,  36,   0,   0,   2,   5, 160,   0,   0,  60,  10,   0,1999,  10,   0,   0, 500,   2, 150,  71},   // 12: CngSlap
    {  13,  51,   0,   1, 300,   8,   0,   0,   2,   5, 160,   0,   0,   0,  12,   0,1999,  10,   0,  70, 300,   0,  30,  71},   // 13: CngHeel
    {  14,  55,   0,   1, 600,  41,   0,   0,   2,   5, 160,   0,   0,  20,  15,   0,1999,  10,   0,  52, 500,   0, 208,  71},   // 14: Tumba
    {  15,  70,   0,   1, 600,  32,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,  20, 500,   0, 560,  71},   // 15: BngMach
    {  16,  69,   0,   1, 600,  50,   0,   0,   2,   5, 160,   0,   0,  20,   0,   0,1999,  10,   0,  20, 797,   0, 734,  71},   // 16: BngHmbr
    {  17,  61,   0,   1, 600,  45,   0,   0,   2,   0, 160,   0,   0,  60,  10,   0,1999,  10,   0,  20, 857,   1, 501,  71},   // 17: TblaNa
    {  18,  58,   0,   1, 600,  48,   0,   0,   2,   0, 160,   0,   0,  50,   8,   0,1999,  10,   0,  15, 500,   2, 141,  71},   // 18: TblaTin
    {  19,  60,   0,   1, 600,  42,   0,   0,   2,   0, 160,   0,   0,  10,  10,   0,1999,  10,   0,  81, 400,   0, 177,  71},   // 19: TblaTun
    {  20,  43,   0,   1, 400,  15,   0,   0,   2,   0, 160,   0,   0,  20,  10,   0,1999,  10,   0,  50, 657,   0, 389,  71},   // 20: TblaGe
    {  21,  43,   0,   1, 400,  45,   0,   0,   2,   0, 160,   0,   0,  20,   8,   0,1999,  10,   0,  68, 300,   0,  59,  71},   // 21: TblGeBd
    {  22,  43,   0,   1, 300,   2,   0,   0,   2,   5, 160,   0,   0,   0,  10,   0,1999,  10,   0,   0, 300,   0, 200,  71},   // 22: TblaKe
    {  23,  43,   0,   1, 400,  32,   0,   0,   2,   5, 160,   0,   0,  10,  12,   0,1999,  10,   0,  22, 300,   0, 249,  71},   // 23: DrbDoum
    {  24,  84,   0,   1, 600,  39,   0,   0,   2,   5, 160,   0,   0,  70,  17,   0,1999,  10,   0,  72, 500,   2,1003,  71},   // 24: DrbTek
    {  25,  84,   0,   1, 600,  50,   0,   0,   2,   5, 160,   0,   0,  60,  10,   0,1999,  10,   0,   3, 500,   2, 501,  71},   // 25: DrbKa
    {  26,  50,   0,   1, 600,  35,   0,   0,   2,   5, 160,   0,   0,  30,  10,   0,1999,  10,   0,  12, 941,   0, 270,  71},   // 26: TalkUp
    {  27,  50,   0,   1, 600,  20,   0,   0,   2,   5, 160,   0,   0,  30,  14,   0,1999,  10,   0,  12, 162,   0, 832,  71},   // 27: TalkDn
    {  28,  34,   0,   1, 350,   2,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,   2, 300,   0, 106,  71},   // 28: SurdOpn
    {  29,  34,   0,   1, 350,  36,   0,   0,   2,   5, 160,   0,   0,  20,  10,   0,1999,  10,   0,  68, 300,   0,  30,  71},   // 29: SurdMut
    {  30,  73,   0,   1, 600,  38,   0,   0,   2,   5, 160,   0,   0,  50,  10,   0,1999,  10,   0,   0, 500,   2, 300,  71},   // 30: Tambrim
    {  31,  45,   0,   1, 300,   2,   0,   0,   2,   5, 160,   0,   0,   0,  10,   0,1999,  10,   0,  27, 300,   0, 138,  71},   // 31: UduBass
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
    /* k_Djambe: 240ms body + bright slap modes 5/6 */ {1.59f, 2.14f, 2.30f, 240.0f, 150.0f, 90.0f, 55.0f, 0.22f, 0.70f, 0.48f, 0.32f, 0.20f, 6, 2.90f, 3.70f, 0.40f, 0.28f},
    /* k_AcousticTom: T60 500ms (close-room ref t40 490 ms) + stick
       transient layer in NoteOn for the bright contact attack */ {1.59f, 2.14f, 2.30f, 500.0f, 260.0f, 150.0f, 95.0f, 0.18f, 0.65f, 0.48f, 0.32f, 0.20f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Taiko2 (DeepBs): a deep Taiko-style membrane as a bass voice, ~500 ms.
       The four T60s share one scale, which keeps the decay SHAPE (the upper
       modes still die first, which is what makes it a struck head and not an
       organ); boom_decay in kVoicing matches the same length. */
    {1.59f, 2.14f, 2.90f, 534.0f, 266.0f, 149.0f, 84.0f, 0.28f, 0.80f, 0.55f, 0.38f, 0.25f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Conga: ref Bongo_Conga2 rings ~300 ms (render died at 70 ms) — open-tone
       body extended; slap softened via the preset row NzFq. */ {1.59f, 2.14f, 2.30f, 250.0f, 120.0f, 60.0f, 38.0f, 0.20f, 0.70f, 0.52f, 0.35f, 0.22f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Handpan */ {2.00f, 3.00f, 4.00f, 900.0f, 700.0f, 500.0f, 350.0f, 0.20f, 0.85f, 0.65f, 0.45f, 0.30f, 5, 5.04f, 0.0f, 0.30f, 0.0f},
    /* k_Bongo: + mode 5 at 3.80 (short, strong) — the wood "tock" */ {1.59f, 2.14f, 2.30f, 320.0f, 200.0f, 120.0f, 70.0f, 0.18f, 0.65f, 0.48f, 0.32f, 0.20f, 5, 3.80f, 0.0f, 0.45f, 0.0f},
    /* k_RackTom: DATA-DRIVEN from the rock-rack-tom-1.wav reference.
       THE MODE CLUSTER WAS AN ARTEFACT — do not put it back.  A windowed FFT
       of the reference shows what looks like a tight cluster of partials at
       ratios 1.071 / 1.272 / 1.350, and as static modes those sound "too string
       like (sdeng) instead of a clear thump".  Tracking the dominant partial in
       30 ms windows shows why —
       there is only ONE partial, and it SLIDES:

         t=  0 ms  160 Hz      t= 60 ms  127 Hz      t=150 ms  113 Hz
         t= 20 ms  142 Hz      t= 80 ms  122 Hz      t=250 ms  112 Hz
         t= 40 ms  133 Hz      t=100 ms  119 Hz      t=400 ms  104 Hz

       i.e. a 160 → 110 Hz head bend (~650 cents, τ ≈ 55 ms).  A stationary FFT
       cannot represent a glide, so it smears one moving partial into several
       fixed ones — and resynthesising those fixed ones builds a detuned chord
       that BEATS.  Measured with those modes: the ones at 1.000 and 1.071
       beat at 12.4 Hz, whose first constructive maximum lands ~60 ms after the
       strike, so the drum SWELLED to its peak instead of decaying from it
       (envelope rms 0.35 → 0.65 at 60 ms, against a reference that peaks at
       t=0 and falls monotonically).  That swell is the "sdeng".
       The glide lives where this engine puts pitch bends — the boom
       oscillator, see the k_RackTom branch in the render loop — and this bank
       holds genuine, WELL-SEPARATED air-loaded membrane ratios that
       cannot beat, held short and quiet so they colour the body without
       droning under the thump.
       env1 is deliberately LOW (0.28, against 0.40 on mode 2): the boom
       carries the fundamental, so a loud mode 1 would merely duplicate it —
       and worse, the boom glides THROUGH it.  Measured with env1 at 0.55 and a
       520 ms mode 1, the two cancelled as the glide passed: the envelope fell
       0.29 → 0.06 → 0.14 across 80-120 ms, an audible "wow" notch.  Keeping
       mode 1 quiet and the tail moderate removes it. */
    {1.59f, 2.14f, 2.30f, 380.0f, 220.0f, 140.0f, 90.0f, 0.12f, 0.28f, 0.40f, 0.26f, 0.16f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_DjembeTone: tone: the head's 262 Hz pitch with its 0.6-1.9 kHz ring */
    {2.24366453f, 3.15374378f, 4.08599213f, 320.867608f, 339.411f, 313.0f, 483.0f, 0.35f, 0.136153846f, 0.230769231f, 0.430769231f, 0.824175824f, 6, 4.34781669f, 7.22253576f, 1.0f, 0.538461538f},
    /* k_DjembeSlap: slap: a skin crack over a 1.07 kHz ring */
    {1.01796244f, 1.08341859f, 1.23210436f, 90.3964f, 432.0f, 331.0f, 216.0f, 0.35f, 0.048f, 1.0f, 0.133333333f, 0.107142857f, 6, 1.44032178f, 1.64997255f, 0.133333333f, 0.25f},
    /* k_DjembeBass: bass: the goblet's 74 Hz air, pushed by the palm, settling from 7 % sharp */
    {1.29126828f, 2.78821326f, 0.0f, 261.63f, 20.0f, 225.0f, 0.0f, 0.1f, 0.857142857f, 1.0f, 0.761904762f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CongaOpen: open tone at 271 Hz (mode 1 is its weak partner at the C4 root) */
    {1.03583149f, 1.24299779f, 1.77505589f, 430.9919f, 434.660025f, 378.0f, 241.0f, 0.4f, 0.042f, 1.0f, 0.573333333f, 0.492857143f, 6, 2.45923979f, 1.94552852f, 0.133333333f, 0.15f},
    /* k_CongaMute: muted tone: 657 Hz, closed by the hand */
    {0.950466651f, 1.0898664f, 1.6231956f, 183.848f, 155.563f, 50.0f, 45.0f, 0.4f, 0.583333333f, 0.340277778f, 0.298148148f, 1.0f, 6, 1.58148185f, 0.87158975f, 0.875f, 0.875f},
    /* k_CongaSlap: open slap: the head's edge-mode cloud under a skin crack */
    {1.3339675f, 1.39130134f, 1.50902684f, 40.0f, 289.810972f, 160.0f, 149.0f, 0.35f, 0.0904109589f, 0.189041096f, 0.365296804f, 0.569471624f, 6, 1.61108109f, 1.98757335f, 0.584474886f, 1.0f},
    /* k_CongaHeel: heel of the palm: a closed low thud */
    {0.692321821f, 1.16544054f, 0.505902761f, 155.563f, 220.0f, 100.0f, 90.0f, 0.4f, 0.782608696f, 0.404347826f, 0.365217391f, 0.642857143f, 6, 0.426192542f, 2.04546707f, 1.0f, 0.456521739f},
    /* k_Tumba: the big conga's open tone, G3 */
    {1.19746292f, 1.70971378f, 1.84593986f, 316.915455f, 575.585f, 352.0f, 331.0f, 0.4f, 1.0f, 0.45f, 0.533333333f, 0.357142857f, 6, 0.91939846f, 2.37298681f, 0.388888889f, 0.5f},
    /* k_BongoMacho: the small bongo */
    {1.30254655f, 1.39757753f, 1.75260299f, 281.204874f, 55.0008f, 197.0f, 198.0f, 0.35f, 0.15483871f, 0.109677419f, 0.860215054f, 0.290322581f, 6, 2.06451054f, 2.13208336f, 0.537634409f, 1.0f},
    /* k_BongoHembra: the large bongo */
    {1.06704545f, 1.21181818f, 1.33840909f, 259.541f, 228.0f, 242.0f, 206.0f, 0.35f, 0.105671642f, 0.202985075f, 0.290547264f, 0.55010661f, 6, 1.57295455f, 1.83522727f, 0.995024876f, 1.0f},
    /* k_TablaNa: Na: the dayan's harmonic series on C#4, 2nd and 3rd harmonics ringing */
    {2.00012533f, 2.99982722f, 3.99988988f, 212.132034f, 1626.35f, 1900.0f, 1200.0f, 0.35f, 0.048f, 1.0f, 0.933333333f, 0.385714286f, 6, 4.99995254f, 6.0000152f, 0.433333333f, 0.6f},
    /* k_TablaTin: Tin: harmonics of A#3, the 3rd strongest */
    {2.00015548f, 2.99980418f, 3.99988192f, 239.99982f, 1697.06f, 2800.0f, 1500.0f, 0.35f, 0.36f, 0.506666667f, 0.888888889f, 0.414285714f, 6, 4.99995965f, 6.00003739f, 1.0f, 0.633333333f},
    /* k_TablaTun: Tun: the open centre stroke, the C4 fundamental rings */
    {2.00018679f, 3.00008907f, 3.99999136f, 1157.46823f, 1390.85459f, 781.0f, 510.0f, 0.35f, 1.0f, 0.433333333f, 0.355555556f, 0.178571429f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaGe: Ge: the bayan's open bass on G2, settling from 6 % sharp */
    {2.02145211f, 3.01942291f, 3.95004599f, 738.413301f, 594.603742f, 350.0f, 220.0f, 0.25f, 0.133333333f, 1.0f, 0.651851852f, 0.476190476f, 5, 4.90005705f, 0.0f, 0.37037037f, 0.0f},
    /* k_TablaGeBend: Ge with the heel sliding toward the syahi: the bass bends up ~5 semitones */
    {2.02145211f, 3.01942291f, 0.0f, 178.381038f, 504.537767f, 120.0f, 0.0f, 0.15f, 0.2f, 1.0f, 0.444444444f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaKe: Ke: a flat palm on the bayan, left there */
    {0.908173839f, 0.746947472f, 1.23674909f, 201.815f, 29.9999951f, 60.0f, 55.0f, 0.3f, 0.210526316f, 0.224561404f, 0.285380117f, 0.428571429f, 6, 1.68165223f, 1.96941068f, 0.842105263f, 1.0f},
    /* k_DarbukaDoum: doum: the goblet's 96 Hz air under the head */
    {2.32451686f, 2.48880449f, 3.65820586f, 450.0f, 243.952f, 373.0f, 233.0f, 0.2f, 0.2f, 1.0f, 0.444444444f, 0.428571429f, 5, 5.17148878f, 0.0f, 0.666666667f, 0.0f},
    /* k_DarbukaTek: tek: the ring finger on the rim, the bright edge modes ring */
    {0.966553095f, 1.01375796f, 1.18002612f, 149.999976f, 422.85f, 386.0f, 322.0f, 0.35f, 0.012f, 0.134f, 0.266666667f, 0.274285714f, 6, 1.29268713f, 1.54705829f, 0.526666667f, 1.0f},
    /* k_DarbukaKa: ka: the other hand's rim stroke, a softer tek */
    {0.966839765f, 0.740084402f, 0.771235792f, 265.931216f, 271.959332f, 293.0f, 328.0f, 0.35f, 0.152941176f, 0.392156863f, 0.266666667f, 0.537815126f, 6, 1.1596726f, 1.29268713f, 0.993464052f, 1.0f},
    /* k_TalkUp: talking drum squeezed as it rings: D3 up about a fifth */
    {1.44654738f, 1.87833224f, 0.0f, 20.0f, 91.1030908f, 70.0f, 0.0f, 0.15f, 0.3f, 1.0f, 1.0f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TalkDown: talking drum released as it rings: down about a fifth to D3 */
    {1.44654738f, 1.87833224f, 0.0f, 20.0f, 23.7841423f, 70.0f, 0.0f, 0.15f, 0.3f, 1.0f, 1.0f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SurdoOpen: surdo, felt mallet, A#1: a long 58 Hz boom settling from 6 % sharp */
    {1.55310228f, 2.20008522f, 2.75954526f, 1323.07714f, 1066.0f, 884.0f, 817.0f, 0.25f, 0.175f, 0.35f, 0.894444444f, 1.0f, 6, 3.28639874f, 3.92136874f, 0.972222222f, 0.4375f},
    /* k_SurdoMute: the surdo damped by the hand */
    {1.21845593f, 1.62689609f, 2.23097565f, 88.3883476f, 128.0f, 246.0f, 249.0f, 0.3f, 0.0466666667f, 0.35f, 0.425185185f, 1.0f, 6, 2.91056515f, 2.62053832f, 0.518518519f, 0.35f},
    /* k_Tamborim: tamborim: a small tight head under a thin stick */
    {1.03577919f, 1.59858501f, 1.62708608f, 254.558107f, 226.274f, 140.0f, 130.0f, 0.35f, 0.107368421f, 0.226315789f, 0.350877193f, 0.372180451f, 6, 2.14768147f, 2.28693983f, 0.666666667f, 1.0f},
    /* k_UduBass: udu: the palm over the side hole, the pot's air rising 85 -> 112 Hz */
    {1.82727273f, 0.0f, 0.0f, 20.0f, 49.9998741f, 0.0f, 0.0f, 0.05f, 1.0f, 1.0f, 0.0f, 0.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f},
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
    // ── Presets added after the split (the original's presets leave these 0
    // and keep their own code paths, so they render as before) ──────────────
    // The slap: a band-passed noise burst at the strike (Transient), from the
    // table instead of a per-preset branch.  slap_gain 0 = none.
    float slap_gain;       // level
    float slap_decay;      // per sample
    float slap_a_lo;       // low corner (one-pole coefficient)
    float slap_a_hi;       // high corner (one-pole coefficient)
    // A TRACKED boom: boom_hz > 0 tunes the boom every sample to
    // (boom_hz + glide · pitch_amount · pitch_env) × the played note's ratio to
    // the preset's root, so it follows Pitch and MIDI notes; with a pitch
    // envelope it glides — glide +1 settles DOWN onto boom_hz (a hard hit
    // stretching the head), −1 rises UP to it (a squeezed talking drum, the
    // udu's opening hole, the tabla bayan under the heel).  0 = the fixed
    // boom_inc above.
    float boom_hz;
    float glide;
    // The bend's SHAPE: glide_ms > 0 bends the tracked boom along an S-curve
    // (brc::GlideCurve) — held for glide_hold_ms, then eased across glide_ms
    // — instead of the exponential pitch envelope; pitch_amount stays its
    // depth in Hz.  A hand on a talking drum or a bayan bends that way.
    float glide_hold_ms;
    float glide_ms;
};
static const Voicing kVoicing[kNumPresets] = {
    //                    modal  onset  chirp  band  split | pitch env decay amount | boom inc env decay mix attack_env attack_inc
    /* k_Djambe       */ {0.22000f, 3.50000f, 200.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_AcousticTom  */ {0.18000f, 3.50000f, 200.00000f, 0.40000f, 0.05000f, 0.00000f, 0.00000f, 0.00000f, brc::phase_inc(110.0f), 1.00000f, 0.99945f, 0.18000f, 0.00000f, 0.00080f},
    /* k_Taiko2       */ {0.28000f, 4.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, brc::phase_inc(70.0f), 1.00000f, 0.99973035f, 0.58000f, 0.00000f, 0.00220f},
    /* k_Conga        */ {0.22000f, 3.50000f, 400.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Handpan      */ {0.20000f, 3.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Bongo        */ {0.18000f, 3.50000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.01047f, 1.00000f, 0.99952f, 0.18000f, 0.00000f, 0.00200f},
    /* k_RackTom      */ {0.12000f, 1.20000f, 200.00000f, 0.46000f, 0.09000f, 1.00000f, 0.999619f, 79.50000f, brc::phase_inc(175.0f), 1.00000f, 0.99972f, 0.45000f, 0.00000f, 0.01040f},
    /* k_DjembeTone   */{0.247487f, 0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.994260074f, 0.279095954f, 0.649080193f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_DjembeSlap   */{0.35f, 0.0f, 0.0f, 0.14375f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.82843f, 0.995214435f, 0.197350641f, 0.60000285f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_DjembeBass   */{0.168178859f, 2.25f, 0.0f, 0.5f, 0.3f, 1.0f, 0.999305797f, 5.2f, 0.00972584726f, 1.0f, 0.999611125f, 0.8f, 0.0f, 0.00520833333f, 0.125f, 0.996408675f, 0.0258402153f, 0.104959359f, 74.3f, 1.0f, 0.0f, 0.0f},
    /* k_CongaOpen    */{0.8f, 0.25f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CongaMute    */{0.282843f, 1.5f, 0.0f, 0.05f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CongaSlap    */{0.35f, 1.5f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.995896683f, 0.0749554219f, 0.917938012f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CongaHeel    */{0.0500000508f, 3.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Tumba        */{0.4f, 0.203125f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BongoMacho   */{0.269886487f, 0.5390625f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BongoHembra  */{0.832444287f, 0.1875f, 0.0f, 0.796875f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaNa      */{0.0309359f, 0.0f, 0.0f, 0.975f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3.53553391f, 0.998801456f, 0.0867579227f, 0.339621517f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaTin     */{0.0218749847f, 0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.99904105f, 0.0277837086f, 0.171108116f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaTun     */{0.0875f, 0.75f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TablaGe      */{0.10976576f, 2.25f, 0.0f, 0.5f, 0.3f, 1.0f, 0.99958342f, 5.9f, 0.01282817f, 1.0f, 0.999808136f, 0.8f, 0.0f, 0.00694444444f, 0.0f, 0.0f, 0.0f, 0.0f, 98.0f, 1.0f, 0.0f, 0.0f},
    /* k_TablaGeBend  */{0.15f, 1.5f, 0.0f, 0.5f, 0.3f, 1.0f, 1.0f, 31.4f, 0.0169384204f, 1.0f, 0.999808136f, 0.565685425f, 0.0f, 0.00694444444f, 0.0f, 0.0f, 0.0f, 0.0f, 129.4f, -1.0f, 45.0f, 260.0f},
    /* k_TablaKe      */{0.424264f, 0.515625f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0156915f, 0.996408675f, 0.02f, 0.229645721f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_DarbukaDoum  */{0.141421f, 1.125f, 0.0f, 0.5f, 0.3f, 1.0f, 0.999305797f, 4.8f, 0.0125532806f, 1.0f, 0.999687198f, 0.8f, 0.0f, 0.00694444444f, 0.0f, 0.0f, 0.0f, 0.0f, 95.9f, 1.0f, 0.0f, 0.0f},
    /* k_DarbukaTek   */{0.35f, 0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.06276598f, 0.990451772f, 0.279095954f, 0.614440512f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_DarbukaKa    */{0.763355f, 0.84375f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 0.990451772f, 0.285207359f, 0.77975632f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TalkUp       */{0.178381067f, 0.5625f, 0.0f, 0.5f, 0.3f, 1.0f, 1.0f, 56.760508f, 0.0284706834f, 1.0f, 0.999851649f, 0.518735432f, 0.0f, 0.0104166667f, 0.0f, 0.0f, 0.0f, 0.0f, 217.5f, -1.0f, 20.0f, 180.0f},
    /* k_TalkDown     */{0.334315969f, 0.515625f, 0.0f, 0.5f, 0.3f, 1.0f, 1.0f, 67.5f, 0.0196349541f, 1.0f, 0.999851649f, 0.480862693f, 0.0f, 0.0104166667f, 0.0f, 0.0f, 0.0f, 0.0f, 150.0f, 1.0f, 25.0f, 237.841423f},
    /* k_SurdoOpen    */{0.125f, 0.9375f, 0.0f, 0.5f, 0.3f, 1.0f, 0.999479302f, 3.5f, 0.00767072206f, 1.0f, 0.999866757f, 0.790313708f, 0.0f, 0.00520833333f, 0.0f, 0.0f, 0.0f, 0.0f, 58.6f, 1.0f, 0.0f, 0.0f},
    /* k_SurdoMute    */{0.313282135f, 0.84375f, 0.0f, 0.5f, 0.3f, 1.0f, 0.999479302f, 3.5f, 0.0075267324f, 1.0f, 0.999424519f, 1.27279f, 0.0f, 0.00520833333f, 0.0f, 0.0f, 0.0f, 0.0f, 57.5f, 1.0f, 0.0f, 0.0f},
    /* k_Tamborim     */{1.5f, 0.375f, 0.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.5f, 0.988078994f, 0.0203588936f, 0.792120424f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_UduBass      */{0.0522136314f, 0.0f, 0.0f, 0.5f, 0.3f, 1.0f, 1.0f, 22.4f, 0.0146607657f, 1.0f, 0.999764107f, 1.79999688f, 0.0f, 0.00416666667f, 0.0f, 0.0f, 0.0f, 0.0f, 112.0f, -1.0f, 15.3125f, 120.0f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Djambe  */ 0.34426f,
    /* 1  Ac Tom  */ 0.37714f,
    /* 2  DeepBs  */ 0.26320f,
    /* 3  Conga   */ 0.82344f,
    /* 4  Handpn  */ 0.46107f,
    /* 5  Bongo   */ 0.84861f,
    /* 6  RackTom */ 0.53567f,
    /* 7  DjmTone */ 0.91946f,
    /* 8  DjmSlap */ 0.43479f,
    /* 9  DjmBass */ 0.56612f,
    /* 10 CngOpen */ 0.20258f,
    /* 11 CngMute */ 0.81552f,
    /* 12 CngSlap */ 0.75885f,
    /* 13 CngHeel */ 1.00000f,
    /* 14 Tumba   */ 0.37584f,
    /* 15 BngMach */ 0.91334f,
    /* 16 BngHmbr */ 0.52040f,
    /* 17 TblaNa  */ 0.77495f,
    /* 18 TblaTin */ 0.77441f,
    /* 19 TblaTun */ 1.00000f,
    /* 20 TblaGe  */ 0.67738f,
    /* 21 TblGeBd */ 0.73748f,
    /* 22 TblaKe  */ 1.00000f,
    /* 23 DrbDoum */ 0.72070f,
    /* 24 DrbTek  */ 0.71956f,
    /* 25 DrbKa   */ 0.39297f,
    /* 26 TalkUp  */ 0.79085f,
    /* 27 TalkDn  */ 0.87470f,
    /* 28 SurdOpn */ 0.48234f,
    /* 29 SurdMut */ 0.73453f,
    /* 30 Tambrim */ 0.17541f,
    /* 31 UduBass */ 0.42184f,
};

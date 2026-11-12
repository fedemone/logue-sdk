#pragma once
// Brachetti World — the presets: what each one plays and how it is voiced.
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
// One standard slot carries a knob of BrcWorld's own: VlMllRes' (velocity →
// noise-attack speed did nothing here) is Rattle, the layers' contact noise.
// It loads with the preset (0 on every row).
enum WorldParam : uint8_t {
    k_paramRattle = Param::k_paramVlMllRes,   // 10
};

// Every preset is voiced after a sample of the Pelle e Legno kit
// (../pelle_legno/Samples), by measurement: see the README, "How the presets
// were voiced", and ../tools/brachetti_equiv/match_specs.py.
enum Preset : uint8_t {
    k_Cuica = 0,        // 0  cuica: the stick-slip voice, the thumb bending it up and back (the 'laugh') (voiced after 031_Cuica.wav)
    k_Berimbau,         // 1  berimbau: a struck steel wire, its gourd swung away and back -- the 'wah' (voiced after 060_Berimbau.wav)
    k_BerimbauHi,       // 2  berimbau with the coin pressed on the wire: a whole tone up, shorter (voiced after 061_BerimbauHi.wav)
    k_BalafonC4,        // 3  balafon, C4: a rosewood bar over a gourd with a buzzing mirliton (voiced after 057_BalafonC4.wav)
    k_BalafonG4,        // 4  balafon, G4 (voiced after 058_BalafonG4.wav)
    k_Riq,              // 5  riq: a finger 'tek' on the small head, five pairs of brass jingles bouncing (voiced after 022_Riq.wav)
    k_Pandeiro,         // 6  pandeiro: the thumb on the edge, then the cupped platinelas (voiced after 030_Pandeiro.wav)
    k_Shekere,          // 7  shekere: the heel on the gourd's base, the bead net thrown (voiced after 038_ShekereHit.wav)
    k_Rainstick,        // 8  rainstick: pebbles cascading through the thorns of a dried cactus tube (voiced after 040_Rainstick.wav)
    kNumPresets         // count
};

// What the Program knob shows.  `static`, at namespace scope: private to this
// unit (a static local of an inline function would be STB_GNU_UNIQUE, and the
// drumlogue's loader shares one copy of those among every loaded unit).
static const char* const kPresetNames[] = {
    "Cuica",  
    "Berimbu",
    "BerimHi",
    "BalafnC",
    "BalafnG",
    "Riq",    
    "Pandeir",
    "Shekere",
    "Rainstk",
};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == kNumPresets, "one name per preset");

static inline const char* preset_name(uint8_t idx) {
    return brc::name_at(kPresetNames, idx, "Unknown");
}

// One row per preset, one column per knob in slot order: the knob values each
// preset ships with.  The Pitch column holds the preset's ROOT NOTE (MIDI):
// the Pitch knob is an offset in semitones from it.  The columns of the knobs
// the host owns (Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc) are
// never loaded; the static_asserts below pin them.
static constexpr int16_t kPresetKnobs[kNumPresets][Param::k_lastParamIndex] = {
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain MlRs MlSt Rttl VlSt Ptls  Mdl  Dky  Rel  Mtr  Hit InHm TbRd NzMx NzRs NzFl NzFq
    {   0,  69,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  10, 300,   0, 800},   // 0: Cuica
    {   1,  46,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  24, 200,   2, 200},   // 1: Berimbu
    {   2,  48,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  40, 200,   2, 200},   // 2: BerimHi
    {   3,  60,   0,   1,   0,1999,  71,   0, 800,  25,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  10, 300,   0, 800},   // 3: BalafnC
    {   4,  67,   0,   1,   0,1999,  71,   0, 800,  13,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  10, 300,   0, 800},   // 4: BalafnG
    {   5,  63,   0,   1,   0,1999,  71,   0, 800,  29,   0,   0,   2,   3, 160,  10,   0,  60,   0,  10,   0, 281,   2, 500},   // 5: Riq
    {   6,  53,   0,   1,   0,1999,  71,   0, 800,  23,   0,   0,   2,   3, 160,  10,   0,  50,   0,  10,  54, 519,   0, 356},   // 6: Pandeir
    {   7,  54,   0,   1,   0,1999,  71,   0, 400,   2,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  10, 419,   0, 300},   // 7: Shekere
    {   8,  72,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   2,   1, 160,  10,   0,   0,   0,  10,  10, 300,   0, 800},   // 8: Rainstk
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramRattle, 0, 0),
              "Rattle is 0 on every preset: its mapping is the identity there");
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

// Each preset's modal bank, from its sample's measured partials: ratios of
// modes 2-4, T60s of modes 1-4 (ms), LoadPreset's mix, mode amplitudes 1-4,
// mode count (0 = no bank: the preset is all layers), then the optional ratios
// and amplitudes of modes 5 and 6.
static const brc::ModalPresetConfig kModalConfigs[kNumPresets] = {
    /* k_Cuica: cuica: the stick-slip voice, the thumb bending it up and back (the 'laugh') */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Berimbau: berimbau: a struck steel wire, its gourd swung away and back -- the 'wah' */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BerimbauHi: berimbau with the coin pressed on the wire: a whole tone up, shorter */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BalafonC4: balafon, C4: a rosewood bar over a gourd with a buzzing mirliton */
    {3.9201062f, 9.20017123f, 0.0f, 574.35058f, 160.0f, 50.0f, 0.0f, 0.25f, 1.0f, 0.5f, 0.133333333f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BalafonG4: balafon, G4 */
    {3.9199436f, 9.20010712f, 0.0f, 550.0f, 160.0f, 50.0f, 0.0f, 0.25f, 1.0f, 0.5f, 0.133333333f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Riq: riq: a finger 'tek' on the small head, five pairs of brass jingles bouncing */
    {1.59388297f, 2.13578389f, 2.29584715f, 250.625708f, 100.0f, 90.0f, 80.0f, 0.2f, 0.24f, 0.36f, 0.426666667f, 0.6f, 6, 2.65293608f, 2.91777971f, 0.8f, 1.0f},
    /* k_Pandeiro: pandeiro: the thumb on the edge, then the cupped platinelas */
    {1.59380013f, 2.13613887f, 2.29591977f, 246.863322f, 250.0f, 200.0f, 160.0f, 0.3f, 0.6f, 0.6f, 0.666666667f, 0.642857143f, 6, 2.6527065f, 2.91786261f, 0.833333333f, 1.0f},
    /* k_Shekere: shekere: the heel on the gourd's base, the bead net thrown */
    {3.35140187f, 5.69738318f, 8.71364486f, 120.0f, 50.0f, 40.0f, 30.0f, 0.3f, 1.0f, 0.833333333f, 0.666666667f, 0.535714286f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Rainstick: rainstick: pebbles cascading through the thorns of a dried cactus tube */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
};

// The rest of each preset's voicing: the exciter, and the five layers of
// voice.h, each off at level 0.  Times in ms, frequencies in Hz; a layer's
// pitched frequencies follow the played note from the preset's root.
struct Voicing {
    float modal_mix;       // the modal bank's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
    float noise_band_mix;  // noise burst body/click balance, 0 = the Model knob's default
    float noise_split;     // body/click split corner (one-pole coefficient)
    float exciter_gain;    // the mallet + noise burst beside the layers (0 = silent)
    // Friction (cuica): f = hz · (1 + rise·up − fall·down), up and down two
    // S-curves; an onset of 20 ms, held, then a T60 decay; two formants.
    float fr_level, fr_hz, fr_rise, fr_rise_ms, fr_fall, fr_fall_hold_ms, fr_fall_ms;
    float fr_hold_ms, fr_t60_ms, fr_formant1_hz, fr_formant2_hz, fr_noise;
    // Wire (berimbau): a Karplus-Strong loop at hz, its T60 at the
    // fundamental, the loss filter's coefficient (lower = darker), the length
    // of the noise burst that strikes it, its level beside the gourd; the
    // gourd: a resonance at gourd_hz, swept up by gourd_up_hz over
    // gourd_up_ms, then back down by gourd_down_hz.
    float wire_level, wire_hz, wire_t60_ms, wire_damp, wire_burst_ms, wire_direct;
    float gourd_hz, gourd_up_hz, gourd_up_ms, gourd_down_hz, gourd_down_hold_ms, gourd_down_ms, gourd_q;
    // Buzz (balafon): the mirliton rides out the part of the bank's motion
    // above thresh (a fraction of its level at the strike), ringing at hz.
    float buzz_level, buzz_thresh, buzz_hz, buzz_q;
    // Jingles (riq, pandeiro): five pairs, three plate modes each drawn from
    // [lo, hi] at every strike, ringing t60; each pair bounces every
    // bounce_lo..bounce_hi ms until spread_ms; a clash is a noise burst of
    // clash_ms.
    float jg_level, jg_lo_hz, jg_hi_hz, jg_t60_ms, jg_bounce_lo_ms, jg_bounce_hi_ms, jg_spread_ms, jg_clash_ms;
    // Particles (shekere, rainstick): collisions at up to rate per second
    // under an attack / hold / T60 envelope, each a noise click of click_ms,
    // through two shell resonances (f1, q1; f2, q2 at mix2) and a direct part.
    float pt_level, pt_rate, pt_attack_ms, pt_hold_ms, pt_t60_ms, pt_click_ms;
    float pt_f1_hz, pt_q1, pt_f2_hz, pt_q2, pt_mix2, pt_direct;
};
static const Voicing kVoicing[kNumPresets] = {
    /* k_Cuica       */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 430.0f, 0.75f, 130.860928f, 0.6028125f, 130.0f, 381.837662f, 313.14306f, 302.74936f, 900.0f, 1800.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Berimbau    */{0.0f, 0.0f, 0.0f, 0.0f, 0.03125f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.61721856f, 117.0f, 905.121418f, 0.3390625f, 1.0f, 0.0f, 380.0f, 520.0f, 160.0f, 400.0f, 160.0f, 160.0f, 7.33603235f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BerimbauHi  */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.4f, 131.3f, 650.538239f, 0.44375f, 1.0f, 0.0f, 380.0f, 520.0f, 160.0f, 400.0f, 160.0f, 160.0f, 3.85552706f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BalafonC4   */{1.41421356f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0795495129f, 0.25f, 1300.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BalafonG4   */{1.41421356f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.45f, 0.566875f, 1300.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Riq         */{0.0896140518f, 0.0f, 0.0f, 0.0f, 0.625f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.707106781f, 2969.84848f, 14000.0f, 166.488996f, 6.0f, 20.0f, 120.0f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Pandeiro    */{0.334316023f, 0.0f, 0.0f, 0.0f, 0.6875f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 3000.0f, 10000.0f, 119.955851f, 8.0f, 25.0f, 100.0f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Shekere     */{0.252268925f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 2500.0f, 2.0f, 60.0f, 300.0f, 0.3f, 3300.0f, 6.0f, 4600.0f, 6.0f, 1.0f, 0.0f},
    /* k_Rainstick   */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 3676.95526f, 350.0f, 594.603558f, 2378.41423f, 0.0581862429f, 4268.78892f, 3.0f, 6500.0f, 2.5f, 0.8f, 0.3f},
};

// Output trim: gain-stages each preset into the master limiter, calibrated as
// every Brachetti unit's (the body peak over [10 ms, 1 s] at velocity 127 on
// the root, on a body target of 1.5, clamped to <= 1):
// ../tools/brachetti_equiv/preset_probe calib reprints this table.
static const float kOutTrim[kNumPresets] = {
    /* 0  Cuica   */ 0.48193f,
    /* 1  Berimbu */ 1.00000f,
    /* 2  BerimHi */ 1.00000f,
    /* 3  BalafnC */ 0.09096f,
    /* 4  BalafnG */ 0.09020f,
    /* 5  Riq     */ 0.28478f,
    /* 6  Pandeir */ 0.21595f,
    /* 7  Shekere */ 0.73908f,
    /* 8  Rainstk */ 0.89028f,
};

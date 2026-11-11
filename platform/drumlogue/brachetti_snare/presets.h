#pragma once
// Brachetti Snare — the presets: what each one plays and how it is voiced.
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
    k_AcSnare = 0,      // 0  -sample: acoustic-snare.wav (1436Hz +/- 20Hz), snare heavy.wav (1287Hz +/- 80Hz)
    k_MarchSnare,       // 1  -sample: Marching-Snare-Drum-A#-minor (1750Hz +/- 100Hz)
    k_BrushSnare,       // 2  — sample: snare_brush_hard.wav, snare_brush_medium.wav, snare_brush_soft.wav. brush-swept snare: slow swish onset + ~4 Hz swirl AM + diffuse wire hiss
    k_RimShot,          // 3  — sample: rimshot-snare.wav rimshot: hard stick crack + bright rim-ring mode cluster + tight short buzz
    k_BendirDoum,       // 4  bendir, centre: a large thin head on 88 Hz over its gut snares (voiced after 020_BendirDoum.wav)
    k_BendirTek,        // 5  bendir, edge: the head's upper modes, the snares lighter (voiced after 021_BendirTek.wav)
    k_CajonBass,        // 6  cajon bass: the box's port air and the tapa's low modes (voiced after 023_CajonBass.wav)
    k_CajonSlap,        // 7  cajon slap: the tapa's edge with the snare wires (voiced after 024_CajonSlap.wav)
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
    "AcSnre",   // slot 3 of the original's 40
    "MrchSnr",  // slot 8 of the original's 40
    "BrshSnr",  // slot 37 of the original's 40
    "RimShot",  // slot 38 of the original's 40
    "BndDoum", // added: voiced after the Pelle e Legno sample
    "BndTek",  // added: voiced after the Pelle e Legno sample
    "CajBass", // added: voiced after the Pelle e Legno sample
    "CajSlap", // added: voiced after the Pelle e Legno sample
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
    {   0,  38,   0,   1, 120,  28,   0,   0,   2,   5, 168,  -7,   0,  46,   9,   0,1999,   8,   7,  52, 950,   2, 300,  71},   // 0: AcSnare   — wire path live; NzRs 950: buzz T60 ≈ 0.4 s matches acoustic-snare.wav t40 ≈ 280 ms; NzFq 300 (HP 3 kHz) pulls the centroid toward the darker reference
    {   1,  65,   0,   1, 720,  50,   0,   0,   1,   5, 190,  20,   0,  50,   8,   2,1999,  19,   5,  55, 940,   2, 105,  71},   // 1: MrchSnr   — click+buzz land together; NzRs 940: buzz T60 ≈ 0.36 s matches the Marching-Snare reference t40 ≈ 220 ms
    {   2,  55,   0,   1, 120,   8,   0,  60,   2,   5, 160,  -7,   0,  46,  17,   0,1999,   8,   5,  95, 975,   1, 320,  71},   // 2: BrshSnr   — DATA-DRIVEN (brush refs: snare_brush_hard/medium/soft.wav): BP noise 3.6kHz (ref centroid ~4.2kHz, 2-6kHz≈57%, flatness≈0.31 = colored not white), NzMx 95 (mallet ~silent), VlMllStf 60 = velocity→decay length (soft 185ms→hard 315ms), ~22ms swish onset
    {   3,  69,   0,   1, 500,  48,   0,  20,   2,   5, 168,   5,   0,  80,   6,   0,1999,   8,   7,  55, 540,   1, 300,  71},    // 3: RimShot   — DATA-DRIVEN (rimshot-snare.wav): note 69 anchors the 877Hz honk at ratio 2.0; BP noise 3kHz (ref centroid 3.1k, 56% in 1-3k, 10% in 3-6k); NzRs 540 → tight buzz (ref t40 45ms)
    {   4,  41,   0,   1, 300,   2,   0,   0,   2,   5, 168,   0,   0,  20,   9,   0,1999,   8,   5,  78, 928,   2, 424,  71},   // 4: BndDoum
    {   5,  66,   0,   1, 300,  38,   0,   0,   2,   5, 168,   0,   0,  70,   9,   0,1999,   8,   5,  38, 900,   2,  75,  71},   // 5: BndTek
    {   6,  40,   0,   1, 200,   2,   0,   0,   2,   5, 168,   0,   0,   0,   9,   0,1999,   8,   5,  20, 956,   2, 106,  71},   // 6: CajBass
    {   7,  65,   0,   1, 300,  42,   0,   0,   2,   5, 168,   0,   0,  70,   9,   0,1999,   8,   5,  92, 928,   2, 600,  71},   // 7: CajSlap
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
    /* k_AcSnare: very short body ring (80ms) so the snare-wire sizzle dominates */ {1.59f, 2.14f, 2.30f, 80.0f, 50.0f, 30.0f, 18.0f, 0.24f, 0.70f, 0.50f, 0.34f, 0.20f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_MarchSnare: very tight click body (30ms); wires dominate */ {1.59f, 2.14f, 2.30f, 30.0f, 20.0f, 12.0f, 7.0f, 0.16f, 0.65f, 0.48f, 0.32f, 0.18f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BrushSnare: softer/darker body than AcSnare (brush barely moves the head);
       ring slightly longer (110ms) so the head tone breathes under the swish */
    {1.59f, 2.14f, 2.30f, 210.0f, 130.0f, 70.0f, 40.0f, 0.20f, 0.60f, 0.42f, 0.26f, 0.15f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_RimShot: DATA-DRIVEN from rimshot-snare.wav — measured peaks
       877/945/1017/1107 Hz (the woody honk cluster) + 1754 + 2785 Hz, t40
       45 ms, centroid 3.1 kHz, <300 Hz ≈ 1% of energy.  At the shipped note
       69 (440 Hz): ratio 2.0 → 877, 2.29 → the 1000-1100 cluster centre,
       3.99 → 1754, 6.33 → 2785.  Fundamental quiet (env1 0.30) as measured. */
    /* env weights counter the fixed modal_sum taper (0.6/0.45/0.28/0.18 for
       modes 2-5) so the 877-1107 cluster carries the energy as measured
       (ref: 56% in 1-3k, fundamental barely present). */
    {2.00f, 2.29f, 3.99f, 40.0f, 70.0f, 60.0f, 50.0f, 0.30f, 0.10f, 0.55f, 1.10f, 1.30f, 5, 6.33f, 0.0f, 1.60f, 0.0f},
    /* k_BendirDoum: bendir, centre: a large thin head on 88 Hz over its gut snares */
    {1.61040818f, 2.31481858f, 2.96081447f, 505.995588f, 544.656859f, 431.0f, 390.0f, 0.3f, 0.4005f, 0.435f, 1.0f, 0.466071429f, 6, 3.6365903f, 4.26998698f, 0.25f, 0.15f},
    /* k_BendirTek: bendir, edge: the head's upper modes, the snares lighter */
    {1.09866521f, 1.16704462f, 1.31650633f, 330.0f, 360.0f, 347.0f, 347.0f, 0.3f, 0.1f, 0.277777778f, 0.185185185f, 0.422619048f, 6, 1.41731866f, 1.47867094f, 0.546296296f, 1.0f},
    /* k_CajonBass: cajon bass: the box's port air and the tapa's low modes */
    {1.15524322f, 1.41371675f, 1.69281963f, 292.0f, 421.435642f, 231.0f, 224.0f, 0.35f, 1.0f, 0.516666667f, 0.755555556f, 0.821428571f, 5, 0.711105595f, 0.0f, 0.833333333f, 0.0f},
    /* k_CajonSlap: cajon slap: the tapa's edge with the snare wires */
    {1.08324576f, 1.63073872f, 1.69029863f, 229.251011f, 249.4251f, 182.0f, 166.0f, 0.3f, 0.15971831f, 0.122535211f, 0.152112676f, 0.905432596f, 6, 2.26699885f, 2.37208772f, 1.0f, 0.464788732f},
};

// The rest of each preset's voicing.
struct Voicing {
    float modal_mix;       // the modal head's level at a strike
    float onset_ms;        // attack ramp of the whole voice, 0 = none
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
    // The snare wires (voice.h).
    float wire_mix;        // wire rattle vs plain filtered noise
    float wire_onset_env;  // where the wires' fade-in starts (1 = no crack phase)
    float wire_onset_attack;  // its step per sample
    float wire_a1, wire_a2;   // band A as LoadPreset leaves it (a strike recomputes it)
    float band_b_hz, band_b_r;   // band B centre and pole radius (< 100 Hz / < 0.3: 4.5 kHz, 0.86)
    float band_c_hz, band_c_r;   // band C (< 100 Hz / < 0.3: 7.2 kHz, 0.82)
};
static const Voicing kVoicing[kNumPresets] = {
    //                    modal  onset  band  split | pitch env decay amount | boom inc env decay mix attack_env attack_inc | wires mix onset_env onset_atk a1 a2 | B hz r | C hz r
    /* k_AcSnare      */ {0.10000f, 0.00000f, 0.42000f, 0.86000f, 1.00000f, 0.99850f, 18.00000f, brc::phase_inc(175.0f), 1.00000f, 0.99920f, 0.12000f, 0.00000f, 0.00180f, 0.85000f, 0.00000f, 0.00260f, 1.76000f, 0.91800f, 4500.00000f, 0.86000f, 7200.00000f, 0.82000f},
    /* k_MarchSnare   */ {0.06000f, 0.00000f, 0.50000f, 0.89000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.72000f, 0.00000f, 0.00280f, 1.74500f, 0.91200f, 5200.00000f, 0.88000f, 8500.00000f, 0.84000f},
    /* k_BrushSnare   */ {0.20000f, 2.00000f, 0.70000f, 0.35000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.10000f, 1.00000f, 0.00080f, 1.76000f, 0.91800f, 3600.00000f, 0.78000f, 6300.00000f, 0.72000f},
    /* k_RimShot      */ {0.26000f, 0.00000f, 0.55000f, 0.80000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.45000f, 0.00000f, 0.01000f, 1.76000f, 0.91800f, 5000.00000f, 0.84000f, 8200.00000f, 0.78000f},
    /* k_BendirDoum   */{0.389051866f, 0.0f, 0.45f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.490625f, 1.0f, 0.0026f, 1.76f, 0.918f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_BendirTek    */{1.5f, 0.0f, 0.45f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.475f, 1.0f, 0.0026f, 1.76f, 0.918f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CajonBass    */{1.5f, 0.0f, 0.45f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 1.0f, 0.0026f, 1.76f, 0.918f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_CajonSlap    */{0.32715232f, 0.0f, 0.45f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.45f, 1.0f, 0.0026f, 1.76f, 0.918f, 0.0f, 0.0f, 0.0f, 0.0f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  AcSnre  */ 0.23584f,
    /* 1  MrchSnr */ 0.32283f,
    /* 2  BrshSnr */ 0.30136f,
    /* 3  RimShot */ 0.56418f,
    /* 4  BndDoum */ 0.12634f,
    /* 5  BndTek  */ 0.09902f,
    /* 6  CajBass */ 0.06677f,
    /* 7  CajSlap */ 0.36629f,
};

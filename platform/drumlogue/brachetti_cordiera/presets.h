#pragma once
// Brachetti Cordiera — the presets: what each one plays and how it is voiced.
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
// One standard slot carries a snare knob of its own: Partls' (the head's mode
// count, next to nothing on the classic snares, whose head is a faint layer)
// is Punch, the transient shaper (brc::Punch).  It loads with the preset (0
// on every row: off).
enum SnareParam : uint8_t {
    k_paramPunch = Param::k_paramPartls,   // 12
};

// The instruments: none of them is in another Brachetti unit.  Voiced from
// their physical descriptions to measured targets (README.md, "How the
// instruments were voiced"): there is no recording of them among the family's
// references.
enum Preset : uint8_t {
    k_Tabor,            // 0  tabor: a deep two-headed drum, a gut snare across the head it is struck on, buzzing with every stroke
    k_Daf,              // 1  daf: a large frame drum, metal rings along the inside of its frame rattling on the head's motion
    k_MbiraBuzz,        // 2  mbira with buzzers: a steel tine over a soundboard, bottle caps rattling while it sings
    k_TomBuzz,          // 3  tom with the snares on: a kit tom ringing, the snare drum beside it buzzing in sympathy
    k_Marranzano,       // 4  marranzano: the Sicilian jaw harp, a steel reed plucked before the mouth, its formant swept
    k_SpringDrum,       // 5  spring drum (thunder drum): a long steel spring hanging from a drum head, its dispersion a falling chirp
    k_Gopichand,        // 6  gopichand: a steel string from a skin head to a split bamboo neck, squeezed to bend the pitch down and back
    kNumPresets         // count
};

// What the Program knob shows, in this unit's order.  `static`: internal
// linkage, so it is private to this unit.  It must not be a static local of an inline function — GCC makes
// those STB_GNU_UNIQUE, and the drumlogue's loader then shares one copy among
// every unit that defines the same symbol (every family unit once showed the
// first-loaded unit's names).
static const char* const kPresetNames[] = {
    "Tabor",   // tabor: a gut snare across the batter head
    "Daf",     // daf: rings rattling on a big frame drum
    "MbiraBz", // mbira: a tine with bottle-cap buzzers
    "TomBuzz", // tom: a snare drum's wires buzzing in sympathy
    "Marranz", // marranzano: the Sicilian jaw harp
    "Spring",  // spring drum: a long spring on a drum head
    "Gopichd", // gopichand: a string bent by its neck
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
    //  Prg Root  Ply  Vel  Ton  Cut Rsnc Gain Buzz BzTn Crck Tght Pnch  Mdl  Dky BzLn  Mtr  Hit InHm TbRd NzMx NzRs NzFl NzFq
    {   0,  50,   0,   1,   0,1999,  71,   5, 400,  20,   0,   0,   0,   5, 168,   9,   0,  30,   0,   8,  50, 920,   2, 300},   // 0: Tabor
    {   1,  38,   0,   1,   0,1999,  71,   5, 300,   5,   0,   0,   0,   5, 168,   9,   0,  20,   0,   8,  40, 900,   2, 500},   // 1: Daf
    {   2,  55,   0,   1,   0,1999,  71,   5, 800,  35,   0,   0,   0,   5, 168,   9,   0,   0,   0,   8,  15, 950,   2, 600},   // 2: MbiraBz
    {   3,  45,   0,   1,   0,1999,  71,   5, 500,  30,   0,   0,   0,   5, 168,   9,   0,  40,   0,   8,  10, 930,   2, 300},   // 3: TomBuzz
    {   4,  45,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   1, 160,  10,   0,   0,   0,  10,  24, 200,   2, 200},   // 4: Marranz
    {   5,  26,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   1, 160,  10,   0,   0,   0,  10,  24, 200,   2, 200},   // 5: Spring
    {   6,  50,   0,   1,   0,1999,  71,   0, 800,  30,   0,   0,   0,   1, 160,  10,   0,   0,   0,  10,  24, 200,   2, 200},   // 6: Gopichd
};
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPunch, 0, 0),
              "Punch is 0 (off) on every preset");
// What Partls, whose slot Punch took, shipped with: the head's mode count.
static const int8_t kShippedPartls[kNumPresets] = {2, 2, 2, 2, 2, 2, 2};
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");
static_assert(brc::preset_column_in(kPresetKnobs, Param::k_paramDkay, 0, 200),
              "Dkay is 0..200: every consumer saturates at 200");

// Each preset's calibrated modal bank: ratios of modes 2-4, T60s of modes 1-4
// (ms), LoadPreset's mix, mode amplitudes 1-4, mode count, then the optional
// ratios and amplitudes of modes 5 and 6.
static const brc::ModalPresetConfig kModalConfigs[kNumPresets] = {
    /* k_Tabor: tabor: a gut snare across the batter head */
    {1.594f, 2.136f, 2.296f, 380.0f, 300.0f, 240.0f, 200.0f, 0.35f, 1.0f, 0.7f, 0.6f, 0.5f, 6, 2.653f, 2.918f, 0.4f, 0.3f},
    /* k_Daf: daf: rings rattling on a big frame drum */
    {1.594f, 2.136f, 2.296f, 650.0f, 500.0f, 420.0f, 380.0f, 0.4f, 1.0f, 0.8f, 0.6f, 0.5f, 6, 2.653f, 2.918f, 0.4f, 0.3f},
    /* k_MbiraBuzz: mbira: a tine with bottle-cap buzzers */
    {6.267f, 17.55f, 0.0f, 1800.0f, 300.0f, 80.0f, 0.0f, 0.6f, 1.0f, 0.5f, 0.15f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_TomBuzz: tom: a snare drum's wires buzzing in sympathy */
    {1.594f, 2.136f, 2.296f, 650.0f, 450.0f, 350.0f, 300.0f, 0.45f, 1.0f, 0.6f, 0.5f, 0.4f, 4, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Marranzano: marranzano: the Sicilian jaw harp */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SpringDrum: spring drum: a long spring on a drum head */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Gopichand: gopichand: a string bent by its neck */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f},
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
    // BrcCordiera's own, 0 on BrcSnare's presets as they were copied:
    float exc_gain;        // the berimbau's mallet + noise burst beside the wire (BrcWorld's exciter_gain)
    // The wires on the head (voice.h HeadDrive): the drive's level (0 = off),
    // the follower's release, and how far its noise is crackle.
    float head_follow, head_rel_ms, head_crackle;
    // The berimbau's wire (BrcWorld's: a Karplus-Strong loop at hz, its T60
    // at the fundamental, the loss filter's coefficient, the length of the
    // noise burst that strikes it, its level beside the gourd; the gourd: a
    // resonance at gourd_hz, swept up by gourd_up_hz over gourd_up_ms, then
    // back down by gourd_down_hz) ...
    float bw_level, bw_hz, bw_t60_ms, bw_damp, bw_burst_ms, bw_direct;
    float gourd_hz, gourd_up_hz, gourd_up_ms, gourd_down_hz, gourd_down_hold_ms, gourd_down_ms, gourd_q;
    // ... and its stiffness: the all-passes' coefficient and how many (0 = none).
    float bw_stiff, bw_stages;
    // Which of BrcSnare's per-preset branches a snare takes (synth_engine.h
    // SnareKind): 0 none on every Cordiera preset (1-4 are BrcSnare's classic
    // snares' own branches, kept with the engine).
    float snare_kind;
    // The wires' lowest band, Hz (0 = the snare kind's: 2.8 kHz).
    float band_a_hz;
    // The wire's pitch bend on the gourd's two curves (a gopichand's neck
    // squeezed, then let go): fractions of the pitch, 0 = none.
    float bw_bend_up, bw_bend_down;
};
static const Voicing kVoicing[kNumPresets] = {
    /* k_Tabor        */{0.35f, 0.0f, 0.5f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.85f, 1.0f, 0.0026f, 1.76f, 0.918f, 3200.0f, 0.86f, 5500.0f, 0.82f, 0.0f, 0.035f, 45.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1800.0f, 0.0f, 0.0f},
    /* k_Daf          */{0.4f, 0.0f, 0.5f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 1.0f, 0.0026f, 1.76f, 0.918f, 6200.0f, 0.92f, 9500.0f, 0.9f, 0.0f, 0.04f, 80.0f, 0.95f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3500.0f, 0.0f, 0.0f},
    /* k_MbiraBuzz    */{0.6f, 0.0f, 0.5f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f, 1.0f, 0.0026f, 1.76f, 0.918f, 4000.0f, 0.8f, 6500.0f, 0.78f, 0.0f, 0.1f, 20.0f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2400.0f, 0.0f, 0.0f},
    /* k_TomBuzz      */{0.45f, 0.0f, 0.5f, 0.85f, 0.0f, 0.0f, 0.0f, 0.0143989663f, 1.0f, 0.999712218f, 0.15f, 0.0f, 0.0018f, 0.55f, 1.0f, 0.0026f, 1.76f, 0.918f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.06f, 35.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Marranzano   */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 0.0f, 0.0f, 0.0f, 3.3f, 110.0f, 2500.0f, 0.9f, 0.6f, 0.3f, 700.0f, 900.0f, 250.0f, 600.0f, 300.0f, 400.0f, 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_SpringDrum   */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.0f, 1.0f, 36.7f, 3500.0f, 0.45f, 4.0f, 0.5f, 160.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f, 0.85f, 16.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Gopichand    */{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.03f, 0.0f, 0.0f, 0.0f, 2.0f, 146.8f, 1400.0f, 0.55f, 0.8f, 0.5f, 330.0f, 0.0f, 180.0f, 0.0f, 250.0f, 220.0f, 2.5f, 0.5f, 1.0f, 0.0f, 0.0f, -0.16f, -0.16f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Tabor   */ 0.06238f,
    /* 1  Daf     */ 0.09116f,
    /* 2  MbiraBz */ 0.04136f,
    /* 3  TomBuzz */ 0.08511f,
    /* 4  Marranz */ 1.00000f,
    /* 5  Spring  */ 0.51965f,
    /* 6  Gopichd */ 0.98133f,
};

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
    kNumPresets         // 7 — count
};

inline const char* preset_name(uint8_t idx) {
    static const char* const names[kNumPresets] = {
        "Djambe", "Ac Tom", "DeepBs", "Conga", "Handpn", "Bongo", "RackTom"
    };
    return brc::name_at(names, idx, "Unknown");
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
    {   6,  53,   0,   1, 420,  36,   0,   0,   2,   5, 190,   0,   0,  52,  12,   0,1999,   6,   5,  48, 300,   0, 700,  71}    // 6: RackTom   — note 53 (F3, 174.6 Hz), the rack tom over Ac Tom's 45/110 Hz.  What voices it are the ABSOLUTE columns: NzMx 38 and NzFq 700 (7 kHz vs Ac Tom's 5.2) put more bright stick in the mix, NzRs 300 keeps it short.  Dky/Rel/Hit/Mtr/TbRd are REFERENCE ANCHORS — LoadPreset captures each into m_anchor from this very row, so every consumer's 2^(k·Δ) is exactly 1 here and they set where the knobs SIT, not the voicing.  Ring length lives in kModalConfigs, shell in kVoicing
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
    {1.59f, 2.14f, 2.30f, 380.0f, 220.0f, 140.0f, 90.0f, 0.12f, 0.28f, 0.40f, 0.26f, 0.16f, 4, 0.0f, 0.0f, 0.0f, 0.0f}
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
    /* k_Djambe       */ {0.22000f, 3.50000f, 200.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_AcousticTom  */ {0.18000f, 3.50000f, 200.00000f, 0.40000f, 0.05000f, 0.00000f, 0.00000f, 0.00000f, brc::phase_inc(110.0f), 1.00000f, 0.99945f, 0.18000f, 0.00000f, 0.00080f},
    /* k_Taiko2       */ {0.28000f, 4.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, brc::phase_inc(70.0f), 1.00000f, 0.99973035f, 0.58000f, 0.00000f, 0.00220f},
    /* k_Conga        */ {0.22000f, 3.50000f, 400.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Handpan      */ {0.20000f, 3.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f},
    /* k_Bongo        */ {0.18000f, 3.50000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.01047f, 1.00000f, 0.99952f, 0.18000f, 0.00000f, 0.00200f},
    /* k_RackTom      */ {0.12000f, 1.20000f, 200.00000f, 0.46000f, 0.09000f, 1.00000f, 0.999619f, 79.50000f, brc::phase_inc(175.0f), 1.00000f, 0.99972f, 0.45000f, 0.00000f, 0.01040f}
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
    /* 6  RackTom */ 0.53567f
};

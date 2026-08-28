#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#include <float_math.h>
#include "../common/runtime.h" // Drumlogue OS functions
#include "unit.h"
#include "dsp_core.h"
#include "modal_drum_kernel.h" // dense coupled-resonator kernel (Timpani/Taiko)

// ==============================================================================
// UNIT TEST DEBUG HOOKS
// ==============================================================================
#ifdef UNIT_TEST_DEBUG
extern float ut_exciter_out;
extern float ut_delay_read;
extern float ut_voice_out;
#endif

/**
 * The Architectural Wins Here:
 * Pre-Calculated Math: Notice the apply_skew and division happens purely in setParameter. The DSP struct (WaveguideState) now holds a pure float like 0.993f. In Phase 3, the Audio Thread will just do a single multiplication (buffer[i] * feedback_gain).
 * Crash-Proof Samples: The sample loader has the exact pointer checks we discussed, but it pushes the metadata to all 4 voices instantly.
 * Sequencer Routing: GateOn properly routes to m_ui_note, ensuring the internal drum machine plays the pitch defined on the screen.
 *
*/

// Utility for fast skewing
inline float apply_skew(float normalized_val, float skew) {
    if (skew == 1.0f) return normalized_val;
    // Inverse exponent mapping for log-style potentiometer curves
    return fasterpowf(normalized_val, 1.0f / skew);
}

// 2^x for the REFERENCE-ANCHORED knob curves (NoteOn / LoadPreset /
// RefreshKernelMods).  Their precision is not critical — ±0.3% on a knob's
// response curve is inaudible — so fasterpow2f is fine, EXCEPT at the anchor:
// fasterpow2f(0) ≈ 0.9614, NOT 1.0, which would silently break the
// byte-identical guarantee for every shipped preset (the anchor delta is
// exactly 0 there and the factor must be exactly 1).  Guard the anchor.
// NEVER use this for tuning or decay-coefficient math — see the CLAUDE.md
// fasterexpf / fastercosfullf gotchas (exact expf/cosf/exp2f there).
inline float knob_exp2(float x) {
    return (x == 0.0f) ? 1.0f : fasterpow2f(x);
}

FastTables g_tables;

// ==============================================================================
// CONSTANTS
// ==============================================================================
static constexpr float    default_sample_rate = 48000.0f;
static constexpr float    inverse_default_sample_rate = 2.0833333333e-5f; // 1 / 48000
static constexpr uint16_t pitch_centre = 8192;
static constexpr float    kToneLpMix = 0.3f;
static constexpr float    kToneCutDivisor = 10.0f;
static constexpr float    kToneBoostDivisor = 15.0f;
static constexpr float    kInvToneCutDivisor = 0.1f;         // 1 / 10
static constexpr float    kInvToneBoostDivisor = 0.13333334f; // 1 / 7.5 (widened ×2: every preset ships Tone=0, so no regression)
static constexpr float    zeroThreshold = 0.0f;
static constexpr float    alpha = 0.01f;
static constexpr float    limiter = 0.99f;
static constexpr int      kSquelchGuardSamples = 1000; // ~20 ms
static constexpr float    kSquelchThreshold = 0.0001f; // -80 dB
static constexpr float    k_log_2_of_200 = 7.643856f;
static constexpr float    k_log_0001 = -6.907755279f; // logf(0.001f) — T60→decay coefficient
static constexpr float    stage2_modal_amp_ratio_2 = 0.6f;
static constexpr float    silence_threshold = 1e-5f;
// Master peak limiter: unity below the threshold, tanh knee above.  Same curve
// the Timpani/Taiko kernel master stage uses.  See the Stage-4b comment.
// Limiter threshold.  Raised 0.55 -> 0.75 once the stage became a gain
// envelope: a waveshaper had to start early to keep peaks down, but an
// envelope limiter holds the ceiling by construction at any threshold, so the
// threshold only trades loudness against how much gain riding happens.  Swept
// 0.55/0.65/0.75/0.85 over all 40 presets — 0.75 recovers 0.54 dB over 0.55
// while every kick keeps a crest factor well above the pre-pass-29 build the
// HW called "perfect" (Kick2 1.43 vs 1.15, KickDrum 1.63 vs 1.34, 808Sub 1.64
// vs 1.51) and the worst peak stays at 0.9890, under the brickwall.
static constexpr float    kMasterLimThr     = 0.75f;
static constexpr float    kMasterLimCeil    = 0.99f;        // == the brickwall
static constexpr float    kMasterLimSpan    = kMasterLimCeil - kMasterLimThr;
// Master limiter release, ms.  Chosen by measurement over 10/20/30/40/60/90/
// 180/350 ms — the table is in CLAUDE.md pass 30.  20 ms comes out ahead of
// BOTH earlier master stages on harmonic content and on crest factor for all
// three kicks, at 1.16 dB of the 3.02 dB pass 29 added; longer releases keep
// getting quieter without buying back distortion (808Sub's H3/H4/H5 are all
// worse at 40 ms than at 20).  Two hits 150 ms apart show no ducking
// (2nd/1st peak within 0.01 dB) anywhere below 350 ms.
static constexpr float    kMasterLimRelMs   = 20.0f;
// 1-exp(-1/tau) ~= 1/tau for tau this large (960 samples at 20 ms).
static constexpr float    kMasterLimRelCoef =
    1.0f / (kMasterLimRelMs * 0.001f * 48000.0f);

// Stage-2 pilot defaults (override-able at compile time for quick sweeps).
#define STAGE2_MODAL_RATIO_2    2.80f
#define STAGE2_MODAL_ENV1       0.9f
#define STAGE2_MODAL_ENV2       0.7f
#define STAGE2_MODAL_T60_1_MS   70.0f
#define STAGE2_MODAL_T60_2_MS   110.0f
#define STAGE2_MODAL_DECAY1     0.99905f
#define STAGE2_MODAL_DECAY2     0.99810f
#define STAGE2_MODAL_MIX        0.08f

// ==============================================================================
// MAIN CLASS
// ==============================================================================
class alignas(16) BrachettiSynth {
public:
    // ==============================================================================
    // PARAMETER INDEX ENUM (Strictly matches header.c)
    // ==============================================================================
    enum ParamIndex {
        k_paramProgram = 0,
        k_paramNote,        // 1
        k_paramCymPoly,     // 2 — ex-Bank: global voice cap (1-4), cymbals included
        k_paramVelocity,    // 3 — ex-Rsntrs: velocity bias (-100 ghost .. +100 wham)
        k_paramMlltRes,     // 4
        k_paramMlltStif,    // 5
        k_paramVlMllRes,    // 6
        k_paramVlMllStf,    // 7
        k_paramPartls,      // 8
        k_paramModel,       // 9
        k_paramDkay,        // 10
        k_paramMterl,       // 11
        k_paramTone,        // 12
        k_paramHitPos,      // 13
        k_paramRel,         // 14
        k_paramInharm,      // 15
        k_paramLowCut,      // 16
        k_paramTubRad,      // 17
        k_paramGain,        // 18
        k_paramNzMix,       // 19
        k_paramNzRes,       // 20
        k_paramNzFltr,      // 21
        k_paramNzFltFrq,    // 22
        k_paramResnc,       // 23
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of all 40 presets, which reads as "the unit
    // loads the wrong sound" rather than as a build error.
    static_assert(k_lastParamIndex == 24, "header.c declares 24 params");
    enum ProgramIndex {
        k_Kick2 = 0,        // 0  — solid kettledrum-kick (the pre-redesign Timpani body, HW-approved as a kick)
        k_Marimba,          // 1  -sample: marimba-hit-c4_C_minor.wav (524Hz +/- 50Hz)
        k_808Sub,           // 2
        k_AcSnare,          // 3  -sample: acoustic-snare.wav (1436Hz +/- 20Hz), snare heavy.wav (1287Hz +/- 80Hz)
        k_TubularBell,      // 4  -samples: tubular-bell-47849.wav (oscillates between 1500Hz and 280Hz then settles to 1230Hz)
        k_Timpani,          // 5  -sample: Orchestral-Timpani-C.wav (239Hz +/- 20Hz)
        k_Djambe,           // 6  -samples: Djambe-B3.wav (starts over 600Hz and settles to 215Hz), Djambe-A3.wav (starts over 1100Hz and settles to 747Hz +/- 10Hz)
        k_Taiko,            // 7  -sample: Taiko-Hit.wav (1582Hz +/- 50Hz)
        k_MarchSnare,       // 8  -sample: Marching-Snare-Drum-A#-minor (1750Hz +/- 100Hz)
        k_Koto,             // 9  -sample: Koto-B5.wav (starts as 700Hz and settles to 290Hz), Koto-Stab-F#.wav (750Hz +/- 100Hz)
        k_Vibraphone,       // 10  -sample: vibraphone_C_major.wav (goes up to 1398Hz then settles to 273Hz), vibraphone_C_major1.wav  (262Hz +/- 20Hz)
        k_Woodblock,        // 11  -sample: Woodblock.wav (3500Hz +/- 100Hz), Woodblock1.wav (858Hz +/- 30Hz)
        k_AcousticTom,      // 12  -sample: Tom1-001-CloseRoom.wav (428Hz +/- 50Hz), Tom2-004-CloseRoom.wav (288Hz + 50Hz)
        k_Cymbal,           // 13  -sample: cymbal-Crash16Inch.wav (starts as 2000Hz goes down to 650Hz and settles to 1000Hz - a lot of oscillations)
        k_Gong,             // 14  -sample: Chinese-Gong.wav, Gong-long-G#.wav (starts with 800Hz and settles to 1680Hz +/- 10Hz)
        k_Kalimba,          // 15  -sample: kalimba-e_E.wav (1398Hz +/- 50Hz)
        k_SteelPan,         // 16  -sample: steel-pan-Nova Drum Real C 432.wav (257Hz +/- 30Hz), steel-pan-PERCY-C4-SHort.wav (260Hz +/- 50Hz)
        k_Claves,           // 17  -sample: percussion-clave-like-hit-107112.mp3 (950Hz +/- 40Hz), wetclave.wav  (2629Hz +/- 20Hz)
        k_Cowbell,          // 18  -sample: Cowbell_2.wav (408Hz +/- 30Hz)
        k_Triangle,         // 19  -sample: Triangle-Bell_C#.wav (3753Hz +/- 100Hz), Triangle-Bell_F5.wav (795Hz +/- 100Hz)
        k_KickDrum,         // 20  -sample: KickA-Hard-012-CloseRoom.wav (1016Hz +/- 100Hz)
        k_Clap,             // 21  -sample: 07_Clap_05_SP.wav (1532 +/- 100Hz)
        k_Shaker,           // 22  — woodblock body + grain-pulse noise (enveloped-LFO AM)
        k_Taiko2,           // 23  — displayed as "DeepBs": the pre-redesign Taiko membrane, HW-approved as a bass voice
        k_GlassBowl,        // 24  -sample: glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav
        k_HiHatClosed,      // 25  — the pre-redesign Shaker noise voice ("a perfect closed hi-hat" per HW report)
        k_HiHatOpen,        // 26  -sample: TightClosedHat.wav (11635Hz +/- 100Hz)
        k_Conga,            // 27  -sample: Bongo_Conga2.wav (286Hz +/- 10Hz)
        k_Handpan,          // 28  -sample: Tabla-Drum-Hit-D4_.wav (237Hz +/- 30Hz)
        k_BellTree,         // 29  -sample:
        k_SlitDrum,         // 30  -sample:
        k_Ride,             // 31  -sample: cymbal-Ride18Inch.wav (start over 2000Hz and settles to 761Hz)
        k_RideBell,         // 32  -sample: cymbal-RideBell20InchSabian.wav (starts over 2000Hz, settles to 867Hz)
        k_Bongo,            // 33  -sample: Bongo_Conga_Mute4.wav (430Hz +/- 100Hz)
        k_GlassBottle,      // 34  -sample: GlassBottle.wav (2636Hz +/- 200Hz)
        k_Tick,             // 35  — the pre-redesign HHat-C metallic chick + added "clack" mode (HW request)
        k_Splash,           // 36  — small pitched splash cymbal (ENGINE_CYMBAL, splash anchor table)
        k_BrushSnare,       // 37  — sample: snare_brush_hard.wav, snare_brush_medium.wav, snare_brush_soft.wav. brush-swept snare: slow swish onset + ~4 Hz swirl AM + diffuse wire hiss
        k_RimShot,          // 38  — sample: rimshot-snare.wav rimshot: hard stick crack + bright rim-ring mode cluster + tight short buzz
        k_RackTom,          // 39  — mounted rack tom at F3 (174.6 Hz): the high drum to Ac Tom's low one.  Tighter head, shorter ring, brighter stick, less shell air.  NOT yet calibrated against a reference sample — see CLAUDE.md pass 33.
        k_NumPrograms       // 40 — marker (count)
    };

    enum ModelsIndex {
        k_String,
        k_Beam,
        k_SquarePlate,
        k_Membrane,
        k_Plate,
        k_Drumhead,
        k_MarimbaBar,
        k_OpenTube,
        k_ClosedTube,
        k_lastModel
    };

    // Engine type determines the DSP signal path for each preset.
    // processBlock routes via kPresetEngine[m_preset_idx].
    enum EngineType : uint8_t {
        ENGINE_KS,        // Karplus-Strong string/pluck
        ENGINE_BAR,       // Free bar modal bank (marimba, vibe, kalimba...)
        ENGINE_MEMBRANE,  // Circular membrane modal bank (kick, toms, timpani...)
        ENGINE_SNARE,     // Membrane body + snare-wire resonators
        ENGINE_PLATE,     // Dense inharmonic plate modes (cymbal, gong, hi-hat...)
        ENGINE_NOISE,     // Noise burst only (clap, shaker)
        ENGINE_REMOVED,   // Silent placeholder (flute, clarinet removed)
        ENGINE_CYMBAL,    // Dense resonator cymbal (ex-cymbal_synthesis port)
    };

enum ModelParamIndex : uint8_t {
    k_base_fm_hz,
    k_snare_wire_z1,
    k_snare_wire_z2,
    k_snare_wire_mix,
    k_snare_wire_a1,
    k_snare_wire_a2,
    k_wire_onset_env,
    k_wire_onset_attack,
    k_noise_lp_state,
    k_noise_band_mix,
    k_noise_hi_lp_state,
    k_noise_hi_lp_coeff,
    k_use_hat_filter,
    k_diffuser_mix,
    k_pitch_env,
    k_pitch_env_decay,
    k_pitch_env_amt,
    k_boom_inc,
    k_boom_env,
    k_boom_decay,
    k_boom_mix,
    k_boom_attack_env,
    k_boom_attack_inc,
    k_reed_nl_enabled,
    k_reed_nl_drive,
    k_snare_freq_b,    // Band B centre frequency (Hz); 0 → fallback 4500 Hz
    k_snare_r_b,       // Band B pole radius base;  0 → fallback 0.86
    k_snare_freq_c,    // Band C centre frequency (Hz); 0 → fallback 7200 Hz
    k_snare_r_c,       // Band C pole radius base;  0 → fallback 0.82
    k_modal_mix,       // Modal bank mix (0.0 = off, overrides ModalPresetConfig.mix)
    k_onset_attack_ms, // Global onset ramp (ms); 0 = instant (no ramp applied)
    k_model_param_total
};

static constexpr float kck_bm = (M_TWOPI * 58.0f) * inverse_default_sample_rate;
static constexpr float tak_bm = (M_TWOPI * 70.0f) * inverse_default_sample_rate;
static constexpr float tom_bm = (M_TWOPI * 110.0f) * inverse_default_sample_rate;
// Rack tom: the shell air resonance tracks the head tuning, so a drum tuned an
// octave-ish above Ac Tom (110 Hz) gets its boom at ~175 Hz rather than sharing
// the low one — reusing tom_bm here would put a floor-tom body under a rack-tom
// head, which is exactly the "toy kit" artefact of a single tom sample pitched up.
static constexpr float rtm_bm = (M_TWOPI * 175.0f) * inverse_default_sample_rate;
static constexpr float asn_bm = (M_TWOPI * 175.0f) * inverse_default_sample_rate;
// AcSnare: add short resonant wire-like sizzle emphasis. snare_wire_a1 = 1.7220f; // slightly brighter/tighter wire crack; wire_onset_attack = 0.0014f; // ~15 ms to full wire excitation
// March snare: drier/tighter wire. wire_onset_attack = 0.0018f; // slightly faster than AcSnare
// HHat-C: short, crisp "chick". noise_hi_lp_coeff = 0.42f; // Chamberlin SVF LP at 5500 Hz gives power-weighted noise centroid ~7 kHz. (BP mode was tried but Chamberlin BP near Nyquist has centroid ~18 kHz, not fc — the LP mode is accurate up to ~fs/8.) mode = 1; // BP
// HHat-O: longer shimmering wash. noise_hi_lp_coeff = 0.30f; // SVF LP at 6000 Hz gives centroid ~8.6 kHz for the noise burst.
// Metallic presets: enable light Schroeder diffusion in fek loop for pseudo-modal density at lU cost
// k_808Sub: 808 bass: classic TR-808 sine-sweep character. Starts 18 semitones (1.5 oct) above the note and sweeps down in ~100ms. The long KS sustain (T60~2s) provides the sub-bass body.
// k_KickDrum: Kick: pitch sweep + low boom. KS feedback_gain is set short (~175ms). by the Drumhead gain curve (Dkay=55) so the boom oscillator dominates after the initial attack transient. boom_decay = 0.99940f; // ~270ms boom tail boom_mix = 0.40f;      // boom dominates after KS decays boom_attack_inc = 0.0010f; // ~20ms onset ramp (0.0032 was 77% by 5ms, too dominant)
// k_Timpani: Modal bank (4 circular-membrane modes) replaces the fixed-frequency boom.
// k_Taiko: Taiko: sub-octave boom (~70 Hz) under the main membrane fundamental.  Gives the deep chest-thud of a real taiko strike. boom_decay = 0.99950f; // ~360ms
// k_AcousticTom: boom_mix = 0.05f;  // reduced from 0.24: was dominating sub band at 70%+ vs ref 11%. boom_attack_inc = 0.0008f;    // reduced from 0.0025 (The boom at C4 (261 Hz ≈ sub boundary) reaches 60% by 5 ms): pushes full boom onset to ~26 ms, giving the KS mallet transient time to register
struct ModalPresetConfig {
    float ratio2; float ratio3; float ratio4;
    float t60_1_ms; float t60_2_ms; float t60_3_ms; float t60_4_ms;
    float mix;
    float env1; float env2; float env3; float env4;
    uint8_t mode_count;
    float ratio5; float ratio6;
    // Per-mode amplitude weights for modes 5 & 6.  Kept here so every
    // instrument tuning parameter lives in the config table, not in the
    // render loop.  For presets with mode_count <= 4 leave both as 0.
    float env5; float env6;
};
// NOTE: Must be 'static' only (no const/constexpr).  On GCC 6.5, const/constexpr
// places these arrays in .rodata, which is counted by the Drumlogue firmware in its
// per-unit .text segment size check (~30 KB limit).  Plain 'static' puts them in
// .data, which is checked separately and has a much larger budget.
ModalPresetConfig kDefaultModalPresetConfig{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f};

// ENGINE_CYMBAL inharmonic anchor tables (ported from cymbal_synthesis).
// Non-static members (no 'static'/'const'/'constexpr') so their initial values
// live in .data, not .rodata — same rule as the modal tables above.
float m_cym_crash_hz[16] = {
    343.f, 421.f, 512.f, 731.f, 973.f, 1289.f, 1627.f, 2143.f,
    2879.f, 3659.f, 4721.f, 6121.f, 7841.f, 10061.f, 13109.f, 16987.f };
float m_cym_ride_hz[16] = {
    481.f, 603.f, 759.f, 1013.f, 1349.f, 1811.f, 2399.f, 3191.f,
    4211.f, 5603.f, 7421.f, 9829.f, 12113.f, 14591.f, 16879.f, 18193.f };
float m_cym_splash_hz[14] = {
    1123.f, 1409.f, 1801.f, 2251.f, 2749.f, 3407.f, 4211.f, 5233.f,
    6473.f, 7963.f, 9787.f, 12043.f, 14813.f, 17041.f };
float m_cym_gong_hz[16] = {
    162.f, 219.f, 287.f, 361.f, 452.f, 579.f, 733.f, 911.f,
    1139.f, 1451.f, 1847.f, 2381.f, 3097.f, 4181.f, 5741.f, 7919.f };
// Hi-hat: everything above ~3.2 kHz with heavy jitter reads as a bright
// metallic noise continuum (the very property that was WRONG for the pitched
// splash is exactly a closed-hat "tick" sizzle — no low pitched body).
float m_cym_hihat_hz[16] = {
    3271.f, 3907.f, 4523.f, 5219.f, 6007.f, 6907.f, 7949.f, 9133.f,
    10501.f, 12073.f, 13879.f, 15959.f, 17041.f, 18353.f, 19141.f, 19709.f };
ModalPresetConfig modal_preset_configs[k_NumPrograms] = {
    /* k_Kick2: the pre-redesign Timpani body — fundamental-dominant kettledrum
       thump.  T60s cut 40 % in pass 42 (HW request), 1100/400/200/100 →
       660/240/120/60.  The boom carries most of this preset (boom_mix 0.85 vs
       modal_mix 0.46) so `boom_decay` in model_param_presets moves with them —
       both halves have to be cut or the tail only half shortens.  Dkay is NOT
       touched: it is the reference anchor, so the shipped knob position still
       plays exactly this data and the knob keeps its full travel either way. */
    {1.340f, 1.664f, 1.980f, 660.0f, 240.0f, 120.0f, 60.0f, 0.38f, 0.95f, 0.20f, 0.12f, 0.08f, 4, 0, 0.0f},
    /* k_Marimba: tuned-bar ratios 1:4:10; T60 calibrated to marimba-hit-c4 */ {4.00f, 10.0f, 0.0f, 1200.0f, 350.0f, 100.0f, 0.0f, 0.18f, 0.72f, 0.50f, 0.22f, 0.0f, 3, 0, 0.0f},
    /* k_808Sub */ kDefaultModalPresetConfig,
    /* k_AcSnare: very short body ring (80ms) so the snare-wire sizzle dominates */ {1.59f, 2.14f, 2.30f, 80.0f, 50.0f, 30.0f, 18.0f, 0.24f, 0.70f, 0.50f, 0.34f, 0.20f, 4, 0, 0.0f},
    /* k_TubularBell */ {2.756f, 5.404f, 0.0f, 2000.0f, 3000.0f, 0.0f, 0.0f, 0.22f, 0.18f, 0.90f, 0.55f, 0.0f, 3, 0, 0.0f},
    /* k_Timpani: DATA-DRIVEN from Orchestral-Timpani-C.wav (modal_extract.py, the
       DAFx2020 "Advanced Fourier Decomposition" peak-track + per-mode T60 method).
       MEASURED series 1 : 1.495 : 1.980 : 2.601 : 3.414 : 4.010 (the air-loaded
       Rossing 1:1.5:2:2.5:3 family, slightly stretched); measured amps 1.00 / 0.50 /
       0.38 / 0.14 / 0.08 / 0.05; measured T60s ~2.4-3.2 s on the pitched modes.
       HW pass-16 said "explosion + rough ripple, not clean bright ringing".  ROOT CAUSE
       found by the measurement: the previous upper pair (3.02/3.55) sat only ~0.5 ratio
       apart AND was held LOUD (env 0.45/0.32) — that close, loud cluster beat against
       mode 4 = the "rough ripple".  In the real sample those upper modes are quiet
       (0.14/0.08/0.05).  Fix: ratios snapped to measured (3.41/4.01 — wider, quieter
       cluster), upper modes tamed to near-measured levels (env4-6 = 0.30/0.18/0.12), and
       the bright pitched modes 2/3 EXTENDED to 1.8/1.5 s for a clean sustained ring.
       Mode 1 is a 450 ms body thump (boom is present, gives way to the pitched ring).
       PORT RETUNE (modal-drum port): upper modes lifted/extended (env4-6 → 0.40/0.26/
       0.18, t3/t4 → 1.7/1.4 s) and mode 2 eased (env → 0.85) to brighten the sustained
       tail; pairs with modal_mix 0.55 + Dkay 150 in the preset row so the bright modal
       ring leads the dark waveguide fundamental. */
    {1.50f, 1.98f, 2.60f, 450.0f, 1800.0f, 1700.0f, 1400.0f, 0.40f, 0.30f, 0.85f, 0.66f, 0.40f, 6, 3.41f, 4.01f, 0.26f, 0.18f},
    /* k_Djambe: 240ms body + bright slap modes 5/6 */ {1.59f, 2.14f, 2.30f, 240.0f, 150.0f, 90.0f, 55.0f, 0.22f, 0.70f, 0.48f, 0.32f, 0.20f, 6, 2.90f, 3.70f, 0.40f, 0.28f},
    /* k_Taiko: DATA-DRIVEN from Taiko-Hit.wav (modal_extract.py, the DAFx2020 peak-track
       method).  MEASURED inharmonic series 1 : 1.377 : 1.746 : 2.100 : 2.423 : 2.754 …
       (open wooden-stave shell → inharmonic, NOT the Bessel membrane ratios) PLUS a
       strong bright partial at ratio 16.86 ≈ 1472 Hz (matches the enum's "~1582 Hz" and
       carries the open "AAN" vowel).  HW "TUNNN not TAAAN" = too dark/closed: ROOT CAUSE
       from the measurement was that the bright 1472 Hz partial WAS NOT BEING SYNTHESIZED
       (old config stopped at ratio 2.756, mode_count=4) and the low fundamental dominated.
       Fix: 6 modes on the measured inharmonic ratios; dominance shifted off the 87 Hz
       fundamental onto the 212 Hz mid (env4=1.00) for an open vowel; mode 6 = the bright
       1472 Hz partial (env 0.40), sustained ~490 ms (= 0.70×t60_4) so the "AAAN" rings
       bright instead of dying as a click.  At the shipped note 41, base_f=87.3 Hz so
       ratio 16.86 lands exactly on the measured 1472 Hz.  Boom osc carries the sub thud.
       PORT RETUNE (modal-drum port): the bright 1472 Hz partial env raised 0.60→0.88 and
       the modal T60s extended (≈1.4-1.5 s) for the long open "TAAAN"; fundamental eased
       (env1 0.42→0.28).  Pairs with modal_mix 0.60 + a leaner boom (mix 0.58→0.22,
       shorter decay) + brighter noise crack so it reads bright/long, not a dark thud. */
    {1.377f, 2.100f, 2.423f, 450.0f, 1500.0f, 1400.0f, 1400.0f, 0.30f, 0.28f, 0.75f, 0.70f, 1.00f, 6, 2.754f, 16.86f, 0.62f, 0.88f},
    /* k_MarchSnare: very tight click body (30ms); wires dominate */ {1.59f, 2.14f, 2.30f, 30.0f, 20.0f, 12.0f, 7.0f, 0.16f, 0.65f, 0.48f, 0.32f, 0.18f, 4, 0, 0.0f},
    /* k_Koto: harmonic-overtone reinforcement on top of the KS string (mix 0.10
       in model_param_presets).  Strong 2nd/3rd partials + a slightly sharp 4.2
       shimmer are the missing koto colour reported from HW. */
    {2.005f, 3.012f, 4.215f, 1100.0f, 800.0f, 600.0f, 420.0f, 0.34f, 0.80f, 0.74f, 0.66f, 0.56f, 6, 5.42f, 6.81f, 0.46f, 0.36f},
    /* k_Vibraphone */ {4.00f, 10.0f, 20.0f, 800.0f, 360.0f, 200.0f, 120.0f, 0.20f, 0.80f, 0.58f, 0.40f, 0.30f, 6, 24.0f, 30.0f, 0.22f, 0.16f},
    /* k_Woodblock: T60 160ms per WoodBlock1.wav reference */ {2.756f, 0.0f, 0.0f, 160.0f, 80.0f, 0.0f, 0.0f, 0.18f, 0.90f, 0.60f, 0.0f, 0.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_AcousticTom: T60 500ms (close-room ref t40 490 ms; was 350) + stick
       transient layer in NoteOn for the bright contact attack */ {1.59f, 2.14f, 2.30f, 500.0f, 260.0f, 150.0f, 95.0f, 0.18f, 0.65f, 0.48f, 0.32f, 0.20f, 4, 0, 0.0f},
    /* k_Cymbal: env5=0.22*env4, env6=0.16*env4; T60 3000→1800ms for a clear decay (HW: "continues while held") */ {2.92f, 6.37f, 11.75f, 1800.0f, 1300.0f, 950.0f, 700.0f, 0.15f, 0.90f, 0.75f, 0.55f, 0.36f, 6, 14.0f, 19.0f, 0.0792f, 0.0576f},
    /* k_Gong: HW "still a big explosion, not metallic".  The nonlinear modal→wash
       cascade (crash_couple 0.60) pulls the broadband energy into the pitched partials;
       upper modes lifted/extended modestly (T60 700/500, env 0.55/0.42/0.22/0.16) so the
       sustain gains metallic shimmer WITHOUT an over-bright clangy attack (ref ~1147 Hz
       early). */
    {1.479f, 1.932f, 2.332f, 1500.0f, 900.0f, 700.0f, 500.0f, 0.22f, 0.90f, 0.58f, 0.55f, 0.42f, 6, 2.549f, 2.840f, 0.22f, 0.16f},
    /* k_Kalimba: tine ratios 1:4:10.  Ref kalimba-e_E rings 2.4 s with centroid
       ~1.7 kHz; the old 600 ms / dark render measured 460 ms at 554 Hz.  T60
       extended and overtone lifted toward the measured reference. */ {4.00f, 10.0f, 0.0f, 1500.0f, 400.0f, 0.0f, 0.0f, 0.15f, 0.80f, 0.62f, 0.22f, 0.0f, 3, 0, 0.0f},
    /* k_SteelPan */ {2.00f, 3.00f, 4.00f, 1200.0f, 900.0f, 700.0f, 500.0f, 0.22f, 0.90f, 0.75f, 0.55f, 0.35f, 4, 0, 0.0f},
    /* k_Claves */ {2.756f, 5.404f, 0.0f, 60.0f, 25.0f, 0.0f, 0.0f, 0.16f, 0.70f, 0.45f, 0.0f, 0.0f, 3, 0, 0.0f},
    /* k_Cowbell: plate ratios.  Ref Cowbell_2 is a 120 ms clank with a ~5.6 kHz
       early centroid; the 500 ms ring read as a mellow bell.  T60s shortened to
       the clank, upper modes lifted, and modes 5/6 (ratios 6.9/9.8 ≈ 2.7/3.8 kHz
       at the shipped note) supply the missing clank brightness. */ {2.00f, 2.68f, 4.30f, 180.0f, 140.0f, 100.0f, 70.0f, 0.22f, 0.70f, 0.72f, 0.60f, 0.48f, 6, 6.90f, 9.80f, 0.55f, 0.40f},
    /* k_Triangle: extended to 6 modes.  The old 3-mode bank topped out at
       ratio 5.4 (2.4 kHz at the shipped note 69) = exactly the measured render
       centroid; a real triangle's audible spectrum is dominated by >5 kHz
       partials (ref centroid 8.5 kHz).  Modes 4-6 add the 4-8 kHz sheen. */
    /* T60s scaled ×0.1317 in pass 41 (HW: "triangle decay is too high, 600 is
       ok").  Dkay is a REFERENCE ANCHOR — LoadPreset captures the row's own
       Dkay into m_modal_dkay_ref, so t60_scale is exactly 1.0 at the shipped
       value and simply lowering the Dkay column would have changed NOTHING.
       The knob position the user liked (Dkay 60, displayed 600) against the
       old anchor 190 gives 2^(4.5·(0.30−0.95)) = 0.1317, so that factor is
       baked into the T60s here and the column moved to 60 to match.  Measured:
       t60 2175 ms → 275 ms, which reproduces the Dkay=600 render exactly. */
    {2.756f, 5.404f, 9.00f, 790.0f, 658.0f, 461.0f, 329.0f, 0.15f, 0.80f, 0.55f, 0.30f, 0.85f, 6, 13.1f, 17.9f, 0.80f, 0.70f},
    /* k_KickDrum */ kDefaultModalPresetConfig,
    /* k_Clap */ kDefaultModalPresetConfig,
    /* k_Shaker: HW redesign — small woodblock body (bar ratio, very short) under
       the grain-pulse noise; the enveloped-LFO AM lives in NoteOn/processBlock. */
    {2.756f, 0.0f, 0.0f, 50.0f, 25.0f, 0.0f, 0.0f, 0.15f, 0.80f, 0.50f, 0.0f, 0.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Taiko2: the pre-redesign Taiko — deep long membrane, HW-approved as bass voice */ {1.59f, 2.14f, 2.90f, 1800.0f, 900.0f, 500.0f, 280.0f, 0.28f, 0.80f, 0.55f, 0.38f, 0.25f, 4, 0, 0.0f},
    /* k_GlassBowl: modes 5/6 at 6.37/8.10 for overtone content */ {2.09f, 3.35f, 4.77f, 2000.0f, 1600.0f, 1200.0f, 800.0f, 0.20f, 0.85f, 0.70f, 0.50f, 0.35f, 6, 6.37f, 8.10f, 0.22f, 0.15f},
    /* k_HiHatClosed: pure noise voice (the pre-redesign Shaker) — no modal body */ kDefaultModalPresetConfig,
    /* k_HiHatOpen: plate ratios for metallic shimmer */ {2.9200f,6.3700f,11.7500f,100.0000f,400.0000f,250.0000f,160.0000f,0.3000f,0.9000f,0.7500f,0.5500f,0.3500f,4,0.0000f,0.0000f},
    /* k_Conga: ref Bongo_Conga2 rings ~300 ms (render died at 70 ms) — open-tone
       body extended; slap softened via the preset row NzFq. */ {1.59f, 2.14f, 2.30f, 250.0f, 120.0f, 60.0f, 38.0f, 0.20f, 0.70f, 0.52f, 0.35f, 0.22f, 4, 0, 0.0f},
    /* k_Handpan */ {2.00f, 3.00f, 4.00f, 900.0f, 700.0f, 500.0f, 350.0f, 0.20f, 0.85f, 0.65f, 0.45f, 0.30f, 5, 5.04f, 0.0f, 0.30f, 0.0f},
    /* k_BellTree */ {2.01f, 2.76f, 0.0f, 900.0f, 700.0f, 0.0f, 0.0f, 0.17f, 0.80f, 0.60f, 0.0f, 0.0f, 3, 0, 0.0f},
    /* k_SlitDrum: tongue-drum bar modes 1:1.52:2.08 */ {1.52f, 2.08f, 0.0f, 1000.0f, 700.0f, 450.0f, 0.0f, 0.18f, 0.80f, 0.60f, 0.35f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Ride: HW reported "a string sound" — the old near-harmonic Bessel ratios
       (1.479/1.932/2.332) read as a harmonic series.  Thick-plate cymbal ratios
       (2.92/6.37/11.75, Chaigne) restore the inharmonic metallic wash; the
       noise-ring cross-modulation (modal_rm_depth) supplies the sizzle motion. */
    {2.92f, 6.37f, 11.75f, 2200.0f, 1400.0f, 1000.0f, 700.0f, 0.16f, 0.90f, 0.68f, 0.48f, 0.32f, 6, 14.0f, 19.0f, 0.09f, 0.06f},
    /* k_RideBell: bell-partial set 1:2:3.01:4.7 (campanological minor-third stack) —
       same string-like complaint as Ride; previous 2.01/2.76/3.56 was too harmonic. */
    {2.00f, 3.01f, 4.70f, 1500.0f, 1100.0f, 800.0f, 600.0f, 0.20f, 0.85f, 0.70f, 0.55f, 0.40f, 4, 0, 0.0f},
    /* k_Bongo: + mode 5 at 3.80 (short, strong) — the missing wood "tock" from HW */ {1.59f, 2.14f, 2.30f, 320.0f, 200.0f, 120.0f, 70.0f, 0.18f, 0.65f, 0.48f, 0.32f, 0.20f, 5, 3.80f, 0.0f, 0.45f, 0.0f},
    /* k_GlassBottle: Helmholtz cavity + neck modes 1:1.47:2.42 */ {1.47f, 2.42f, 0.0f, 500.0f, 320.0f, 190.0f, 0.0f, 0.20f, 0.85f, 0.60f, 0.30f, 0.0f, 3, 0.0f, 0.0f, 0.0f, 0.0f},
    /* k_Tick: the pre-redesign HHat-C metallic chick + a low "clack" (mode 4 at
       ratio 1.40, 50ms, strong) per HW request ("HHat-C as Tick, adding some clack under") */
    {2.92f, 6.37f, 1.40f, 45.0f, 28.0f, 316.0f, 70.0f, 0.30f, 0.85f, 0.70f, 0.50f, 1.25f, 4, 0.0f, 0.0f},
    /* k_Splash: ENGINE_CYMBAL — modal bank bypassed; plate ratios kept valid */
    {2.92f, 6.37f, 11.75f, 300.0f, 200.0f, 120.0f, 80.0f, 0.20f, 0.80f, 0.50f, 0.30f, 0.20f, 4, 0.0f, 0.0f},
    /* k_BrushSnare: softer/darker body than AcSnare (brush barely moves the head);
       ring slightly longer (110ms) so the head tone breathes under the swish */
    {1.59f, 2.14f, 2.30f, 210.0f, 130.0f, 70.0f, 40.0f, 0.20f, 0.60f, 0.42f, 0.26f, 0.15f, 4, 0, 0.0f},
    /* k_RimShot: DATA-DRIVEN from rimshot-snare.wav — measured peaks
       877/945/1017/1107 Hz (the woody honk cluster) + 1754 + 2785 Hz, t40
       45 ms, centroid 3.1 kHz, <300 Hz ≈ 1% of energy.  At the shipped note
       69 (440 Hz): ratio 2.0 → 877, 2.29 → the 1000-1100 cluster centre,
       3.99 → 1754, 6.33 → 2785.  Fundamental quiet (env1 0.30) as measured. */
    /* env weights counter the fixed modal_sum taper (0.6/0.45/0.28/0.18 for
       modes 2-5) so the 877-1107 cluster carries the energy as measured
       (ref: 56% in 1-3k, fundamental barely present). */
    {2.00f, 2.29f, 3.99f, 40.0f, 70.0f, 60.0f, 50.0f, 0.30f, 0.10f, 0.55f, 1.10f, 1.30f, 5, 6.33f, 0.0f, 1.60f, 0.0f},
    /* k_RackTom: DATA-DRIVEN from samples/rock-rack-tom-1.wav.  The physical
       THE MODE CLUSTER WAS AN ARTEFACT — do not put it back.  A windowed FFT
       of the reference shows what looks like a tight cluster of partials at
       ratios 1.071 / 1.272 / 1.350, and shipping those as static modes is what
       the HW listen came back on: "too string like (sdeng) instead of a clear
       thump".  Tracking the dominant partial in 30 ms windows shows why —
       there is only ONE partial, and it SLIDES:

         t=  0 ms  160 Hz      t= 60 ms  127 Hz      t=150 ms  113 Hz
         t= 20 ms  142 Hz      t= 80 ms  122 Hz      t=250 ms  112 Hz
         t= 40 ms  133 Hz      t=100 ms  119 Hz      t=400 ms  104 Hz

       i.e. a 160 → 110 Hz head bend (~650 cents, τ ≈ 55 ms).  A stationary FFT
       cannot represent a glide, so it smears one moving partial into several
       fixed ones — and resynthesising those fixed ones builds a detuned chord
       that BEATS.  Measured on the shipped render: modes at 1.000 and 1.071
       beat at 12.4 Hz, whose first constructive maximum lands ~60 ms after the
       strike, so the drum SWELLED to its peak instead of decaying from it
       (envelope rms 0.35 → 0.65 at 60 ms, against a reference that peaks at
       t=0 and falls monotonically).  That swell is the "sdeng".
       The glide now lives where this engine puts pitch bends — the boom
       oscillator, see the k_RackTom branch in the render loop — and this bank
       is back to genuine, WELL-SEPARATED air-loaded membrane ratios that
       cannot beat, held short and quiet so they colour the body without
       droning under the thump.
       env1 is deliberately LOW (0.28, against 0.40 on mode 2): the boom now
       carries the fundamental, so a loud mode 1 would merely duplicate it —
       and worse, the boom glides THROUGH it.  Measured with env1 at 0.55 and a
       520 ms mode 1, the two cancelled as the glide passed: the envelope fell
       0.29 → 0.06 → 0.14 across 80-120 ms, an audible "wow" notch.  Keeping
       mode 1 quiet and the tail moderate removes it. */
    {1.59f, 2.14f, 2.30f, 380.0f, 220.0f, 140.0f, 90.0f, 0.12f, 0.28f, 0.40f, 0.26f, 0.16f, 4, 0.0f, 0.0f, 0.0f, 0.0f}};

float model_param_presets[k_NumPrograms][k_model_param_total]{
    /*               k_base_fm_hz, k_snare_wire_z1, k_snare_wire_z2, k_snare_wire_mix, k_snare_wire_a1, k_snare_wire_a2, k_wire_onset_env, k_wire_onset_attack, k_noise_lp_state, k_noise_band_mix, k_noise_hi_lp_state, k_noise_hi_lp_coeff, k_use_hat_filter, k_diffuser_mix, k_pitch_env, k_pitch_env_decay, k_pitch_env_amt, k_boom_inc, k_boom_env, k_boom_decay, k_boom_mix, k_boom_attack_env, k_boom_attack_inc, k_reed_nl_enabled, k_reed_nl_drive, k_snare_freq_b, k_snare_r_b, k_snare_freq_c, k_snare_r_c, k_modal_mix, k_onset_attack_ms */
    /* k_Kick2       */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, kck_bm, 1.00000f, 0.9996333f, 0.85000f,    0.00000f, 0.00300f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.46000f,    2.00000f},
    /* k_Marimba     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_808Sub      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    1.00000f,    0.99900f,  115.00000f,    0.00589f,    1.00000f,    0.99982f,    0.60000f,    0.00000f,    0.00100f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_AcSnare     */ {   0.00000f,    0.00000f,    0.00000f, 0.85000f,    1.76000f,    0.91800f,    0.00000f,    0.00260f,    0.00000f,    0.42000f,    0.00000f,    0.86000f, false,    0.00000f,    1.00000f,    0.99850f,   18.00000f, asn_bm,    1.00000f,    0.99920f,    0.12000f,    0.00000f,    0.00180f, false,    0.00000f, 4500.00000f,    0.86000f, 7200.00000f,    0.82000f,    0.10000f,    0.00000f},
    /* k_TubularBell */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    0.00000f},
    /* k_Timpani     */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.55000f,    0.00000f},
    /* k_Djambe      */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.04000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    3.50000f},
    /* k_Taiko       */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, tak_bm,    1.00000f,    0.99975f,    0.22000f,    0.00000f,    0.00220f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.60000f, 0.00000f},
    /* k_MarchSnare  */ {   0.00000f,    0.00000f,    0.00000f,    0.72000f,    1.74500f,    0.91200f,    0.00000f,    0.00280f,    0.00000f,    0.50000f,    0.00000f,    0.89000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f, 5200.00000f,    0.88000f, 8500.00000f,    0.84000f,    0.06000f,    0.00000f},
    /* k_Koto        */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    1.00000f,    0.99900f,    1.50000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.22000f,    0.00000f},
    /* k_Vibraphone  */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_Woodblock   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_AcousticTom */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.40000f,    0.00000f,    0.05000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, tom_bm,    1.00000f,    0.99945f,    0.18000f,    0.00000f,    0.00080f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    3.50000f},
    /* k_Cymbal      */ {3400.00000f,    1.00000f, 11000.00000f,    0.00000f,    0.00000f,    1.10000f,    0.00000f,    0.00000f,    0.00000f,    0.98000f,    0.00000f,    0.91000f, true,    0.30000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.15000f,    0.00000f},
    /* k_Gong        */ { 900.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.50000f,    0.00000f,    0.86000f, false,    0.24000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.26000f,    0.00000f},
    /* k_Kalimba     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.03000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.15000f,    0.00000f},
    /* k_SteelPan    */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    1.50000f},
    /* k_Claves      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.50000f},
    /* k_Cowbell     */ { 900.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.45000f,    0.00000f,    0.75000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    0.00000f},
    /* k_Triangle    */ {1800.00000f,    0.00000f,    0.00000f,    0.00000f,    1.26000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.15000f,    0.00000f,    0.96000f, false,    0.16000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.40000f,    0.00000f},
    /* k_KickDrum    */ {   0.00000f,    0.00000f,    0.00000f,    0.03000f,    1.20000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.05000f, false,    0.00000f,    1.00000f,    0.99890f,    9.00000f, kck_bm,    1.00000f,    0.99982f, 0.70000f,    0.00000f,    0.00350f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    2.00000f},
    /* k_Clap        */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Shaker: modal_mix 0.04→0 — the woodblock body was a struck "tok" at onset
       (HW: "too much hit sound, should not be there").  Shaker = pure rattling
       noise (AM-modulated), no pitched hit. */
    /* k_Shaker      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.00000f,    0.00000f},
    /* k_Taiko2      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, tak_bm,    1.00000f,    0.99981f,    0.58000f,    0.00000f,    0.00220f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.28000f,    4.00000f},
    /* k_GlassBowl   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.00000f},
    /* k_HiHatClosed */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_HiHatOpen   */ {3600.00000f,    1.00000f, 12000.00000f,    0.00000f,    0.00000f,    0.80000f,    0.00000f,    0.00000f,    0.00000f,    1.00000f,    0.00000f,    0.93000f, false,    0.36000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.12000f,    0.50000f},
    /* k_Conga       */ { 400.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    3.50000f},
    /* k_Handpan     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    3.00000f},
    /* k_BellTree    */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.17000f,    0.00000f},
    /* k_SlitDrum    */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_Ride        */ {1450.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.95000f,    0.00000f,    0.05000f, false,    0.32000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.18000f,    0.00000f},
    /* k_RideBell    */ {1200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, 0.90000f,    0.00000f,    0.05000f, false,    0.32000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.00000f},
    /* k_Bongo       */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.01047f,    1.00000f,    0.99952f, 0.18000f,    0.00000f,    0.00200f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    3.50000f},
    /* k_GlassBottle */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.50000f},
    /* k_Tick        */ {3400.00000f,    2.00000f, 6000.00000f,    0.00000f,    0.00000f,    0.80000f,    0.00000f,    0.00000f,    0.00000f,    0.86000f,    0.00000f,    0.39000f, true,    0.34000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.24000f,    0.50000f},
    /* k_Splash      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.50000f},
    /* k_BrushSnare: DATA-DRIVEN from the CORRECTED reference set
       snare_brush_hard / _medium / _soft.wav — a mid-focused COLOURED swish,
       NOT the bright white hiss the earlier (mislabelled) samples implied.
       Refs: centroid ~4.2 kHz, energy 2-6 kHz ≈ 57 %, only ~20 % above 6 kHz,
       flatness ≈ 0.31 (band-limited, not flat), body <300 Hz ≈ 1 %, velocity
       maps mainly to level + decay length (soft 185 ms → hard 315 ms).  So the
       noise SVF is a band-pass at ~4.9 kHz (NzFltr=1) instead of the old
       2.5 kHz high-pass; band_mix centred ~0.62 (velocity-tilted in NoteOn);
       wire mix 0.10 (a whisper of buzz, not a ring); modal body 0.02. */
    /* k_BrushSnare  */ {   0.00000f,    0.00000f,    0.00000f,    0.10000f,    1.76000f,    0.91800f,    1.00000f,    0.00080f,    0.00000f,    0.70000f,    0.00000f,    0.35000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f, 3600.00000f,    0.78000f, 6300.00000f,    0.72000f,    0.20000f,    2.00000f},
    /* k_RimShot     */ {   0.00000f,    0.00000f,    0.00000f,    0.45000f,    1.76000f,    0.91800f,    0.00000f,    0.01000f,    0.00000f,    0.55000f,    0.00000f,    0.80000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f, 5000.00000f,    0.84000f, 8200.00000f,    0.78000f,    0.26000f,    0.00000f},
    /* k_RackTom: the BOOM is this preset's fundamental, not a support layer —
       it is the only oscillator here that can glide, and the reference's whole
       thump is a 160 → 110 Hz glide (see the modal config).  So, unlike Ac Tom
       (boom_mix 0.05) and unlike this row's own first cut (0.12), the boom
       carries the voice at 0.45 and the modal bank is trimmed to 0.10.

       pitch_env 1.0 / decay 0.999619 / amt 79.5 realise the glide: τ = 55 ms
       measured, and 175 + 79.5 = 254.5 Hz = 175 x 1.4545, the measured 160/110
       start-to-rest ratio applied to this preset's resting pitch.

       boom_attack_inc 0.00115 -> 0.0104 is part of the "sdeng" fix: at 0.00115
       the boom needed ~18 ms to reach full, so the hit did not land on the hit;
       0.0104 is a ~2 ms onset.
       boom_decay 0.99972 (T60 ≈ 515 ms) is a measured compromise, not a
       calculation.  The reference decays in TWO stages — fast to ~100 ms, then
       a slower tail — which one exponential cannot do, so this knob trades
       attack share against tail length: 0.99976 gives t40 383 ms with 29.7 % of
       the energy in the first 25 ms, 0.99960 gives 44.2 % but t40 collapses to
       231 ms.  0.99972 lands at 34.4 % / 329 ms against the reference's
       39.8 % / 400 ms, and the complaint being answered was thump, so the
       balance is deliberately tipped toward the attack.
       NOTE the two-table gotcha: this k_modal_mix is the audible one, and
       modal_preset_configs' `mix` must equal it — both are 0.12. */
    /* k_RackTom     */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.46000f,    0.00000f,    0.09000f, false,    0.02000f,    1.00000f,    0.999619f,   79.50000f, rtm_bm,    1.00000f,    0.99972f,    0.45000f,    0.00000f,    0.01040f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.12000f,    1.20000f}};

// Preset → engine routing table.
// NOTE: Must be 'static' only (no const/constexpr) — same .rodata rule as above.
EngineType kPresetEngine[k_NumPrograms] = {
    /* k_Kick2(0)         */ ENGINE_MEMBRANE,  // kettledrum kick (ex-Timpani body)
    /* k_Marimba(1)       */ ENGINE_BAR,
    /* k_808Sub(2)        */ ENGINE_MEMBRANE,  // sub kick: boom_osc + pitch_env sweep
    /* k_AcSnare(3)       */ ENGINE_SNARE,
    /* k_TubularBell(4)   */ ENGINE_BAR,
    /* k_Timpani(5)       */ ENGINE_MEMBRANE,
    /* k_Djambe(6)        */ ENGINE_MEMBRANE,
    /* k_Taiko(7)         */ ENGINE_MEMBRANE,  // woodblock attack + TAANNG ring
    /* k_MarchSnare(8)    */ ENGINE_SNARE,
    /* k_Koto(9)          */ ENGINE_KS,  // + harmonic-overtone modal bank
    /* k_Vibraphone(10)   */ ENGINE_BAR,
    /* k_Woodblock(11)    */ ENGINE_BAR,
    /* k_AcousticTom(12)  */ ENGINE_MEMBRANE,
    /* k_Cymbal(13)       */ ENGINE_CYMBAL,  // dense resonator cymbal (was PLATE)
    /* k_Gong(14)         */ ENGINE_CYMBAL,
    /* k_Kalimba(15)      */ ENGINE_BAR,
    /* k_SteelPan(16)     */ ENGINE_BAR,
    /* k_Claves(17)       */ ENGINE_BAR,
    /* k_Cowbell(18)      */ ENGINE_PLATE,
    /* k_Triangle(19)     */ ENGINE_PLATE,
    /* k_KickDrum(20)     */ ENGINE_MEMBRANE,
    /* k_Clap(21)         */ ENGINE_NOISE,  // multi-burst AM
    /* k_Shaker(22)       */ ENGINE_NOISE,  // grain-pulse AM + woodblock body
    /* k_Taiko2(23)       */ ENGINE_MEMBRANE,  // ex-Taiko deep membrane
    /* k_GlassBowl(24)    */ ENGINE_BAR,
    /* k_HiHatClosed(26)  */ ENGINE_NOISE,  // ex-Shaker noise voice
    /* k_HiHatOpen(27)    */ ENGINE_CYMBAL,  // dense resonator cymbal (was PLATE)
    /* k_Conga(28)        */ ENGINE_MEMBRANE,
    /* k_Handpan(29)      */ ENGINE_MEMBRANE,
    /* k_BellTree(30)     */ ENGINE_PLATE,
    /* k_SlitDrum(31)     */ ENGINE_BAR,
    /* k_Ride(32)         */ ENGINE_CYMBAL,  // dense resonator cymbal (was PLATE)
    /* k_RideBell(33)     */ ENGINE_CYMBAL,
    /* k_Bongo(34)        */ ENGINE_MEMBRANE,
    /* k_GlassBottle(35)  */ ENGINE_BAR,
    /* k_Tick(36)         */ ENGINE_PLATE,  // ex-HHat-C chick + clack
    /* k_Splash(37)       */ ENGINE_CYMBAL, // small pitched splash cymbal
    /* k_BrushSnare(38)   */ ENGINE_SNARE,  // brush-swept snare (swish + swirl)
    /* k_RimShot(39)      */ ENGINE_SNARE,  // rimshot crack + rim-ring ping
    /* k_RackTom(40)      */ ENGINE_MEMBRANE,  // mounted rack tom (high drum to Ac Tom's low)
};

// ModelsIndex → modal frequency-ratio template: modes 2..6 relative to the
// fundamental.  Used by the modal engines (BAR/MEMBRANE/SNARE/PLATE) when the
// user moves the Model knob away from the preset's shipped value, and as the
// fallback ratio source when Partls raises the mode count beyond what the
// calibrated modal_preset_configs row defines.
// Sources: Fletcher & Rossing, "The Physics of Musical Instruments", 2nd ed.
//   String/OpenTube: harmonic series.       ClosedTube: odd harmonics.
//   Beam: free-free bar (3.011²:5²:7²… /3.011²).  Membrane: Bessel J zeros.
//   SquarePlate: free square plate (Chladni).     Plate: thick circular plate
//   (Chaigne & Doutaut cymbal set).  Drumhead: air-loaded kettledrum
//   quasi-harmonic principal modes.  MarimbaBar: deep-arch tuned bar 1:4:10.
// NOTE: Must be a plain member (no static/const/constexpr) — same .rodata rule
// as the preset tables above.
float kModelModalRatios[k_lastModel][5] = {
    /* k_String      */ {2.000f, 3.000f, 4.000f, 5.000f, 6.000f},
    /* k_Beam        */ {2.756f, 5.404f, 8.933f, 13.345f, 18.638f},
    /* k_SquarePlate */ {1.520f, 1.940f, 2.710f, 3.210f, 3.730f},
    /* k_Membrane    */ {1.594f, 2.136f, 2.296f, 2.653f, 2.918f},
    /* k_Plate       */ {2.920f, 6.370f, 11.750f, 14.000f, 19.000f},
    /* k_Drumhead    */ {1.500f, 1.983f, 2.444f, 2.896f, 3.341f},
    /* k_MarimbaBar  */ {3.984f, 10.668f, 18.000f, 25.000f, 32.000f},
    /* k_OpenTube    */ {2.000f, 3.000f, 4.000f, 5.000f, 6.000f},
    /* k_ClosedTube  */ {3.000f, 5.000f, 7.000f, 9.000f, 11.000f},
};

inline float preset_param(ProgramIndex program, ModelParamIndex param) {
  return model_param_presets[program][param];
}

SynthState state;

#ifdef UNIT_TEST_DEBUG
    // Expose private members for unit test introspection (test binary only).
    float m_coupling_depth_ut() const { return m_coupling_depth; }
    // Which resonator(s) the editor is pointed at.  T40 asserts a cymbal
    // density of 5-7 leaves this alone (it survives a preset change).
    bool  m_is_resonator_a_ut() const { return m_is_resonator_a; }
    bool  m_is_resonator_b_ut() const { return m_is_resonator_b; }
#endif

    // ==============================================================================
    // 0. Lifecycle & Initialization
    // ==============================================================================

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        // 1. Hardware Sanity Checks
        // The Drumlogue is strictly 48kHz, stereo.
        // If Korg ever releases a 96kHz device, this prevents your delay math from breaking.
        if (desc->samplerate != (uint32_t)default_sample_rate) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;

        g_tables.generate(default_sample_rate); // Pre-calculate all tuning math

        // 2. Clear all memory explicitly at boot
        Reset();

        // 3. Stash runtime functions to manage samples.
        m_get_num_sample_banks_ptr = desc->get_num_sample_banks;
        m_get_num_samples_for_bank_ptr = desc->get_num_samples_for_bank;
        m_get_sample = desc->get_sample;

        // 4. Load default preset so DSP parameters are not all-zero after Reset().
        // Without this, mallet_stiffness=0 (no exciter energy), feedback_gain=0
        // (no resonance), and lowpass_coeff=0 (feedback path silenced) — all silence.
        LoadPreset(0);

        return k_unit_err_none;
    }

    inline void Teardown() {
        // We use static memory, so there are no raw pointers to free() or delete.
        // If we were using dynamic memory, we would release it here.
    }

    // Called when the user changes programs or the engine needs a hard flush.
    // This prevents loud "pops" from old delay line data playing unexpectedly.
    inline void Reset() {
        m_drum_kernel.Flush();   // dense-kernel ring dies with the voices
        m_kernel_tone_lp = 0.0f;
        for (int i = 0; i < NUM_VOICES; ++i) {
            // Wipe delay-line and diffuser buffers without allocating a ~17 KB
            // VoiceState() temporary on the stack.  A "voices[i] = VoiceState()"
            // assignment asks the compiler to construct a full copy-assignment
            // temporary (≈17 KB) which risks a stack overflow inside unit_init()
            // on the drumlogue firmware whose loading thread may have a restricted
            // stack size.  IEEE 754: 0x00000000 == 0.0f, so memset on float[] is safe.
            memset(state.voices[i].resA.buffer,        0, sizeof(state.voices[i].resA.buffer));
            memset(state.voices[i].resB.buffer,        0, sizeof(state.voices[i].resB.buffer));
            memset(state.voices[i].resA.diffuser_buf1, 0, sizeof(state.voices[i].resA.diffuser_buf1));
            memset(state.voices[i].resA.diffuser_buf2, 0, sizeof(state.voices[i].resA.diffuser_buf2));
            memset(state.voices[i].resA.diffuser_buf3, 0, sizeof(state.voices[i].resA.diffuser_buf3));
            memset(state.voices[i].resA.diffuser_buf4, 0, sizeof(state.voices[i].resA.diffuser_buf4));
            memset(state.voices[i].resB.diffuser_buf1, 0, sizeof(state.voices[i].resB.diffuser_buf1));
            memset(state.voices[i].resB.diffuser_buf2, 0, sizeof(state.voices[i].resB.diffuser_buf2));
            memset(state.voices[i].resB.diffuser_buf3, 0, sizeof(state.voices[i].resB.diffuser_buf3));
            memset(state.voices[i].resB.diffuser_buf4, 0, sizeof(state.voices[i].resB.diffuser_buf4));
            state.voices[i].resA.diffuser_i1 = state.voices[i].resA.diffuser_i2 = 0;
            state.voices[i].resA.diffuser_i3 = state.voices[i].resA.diffuser_i4 = 0;
            state.voices[i].resB.diffuser_i1 = state.voices[i].resB.diffuser_i2 = 0;
            state.voices[i].resB.diffuser_i3 = state.voices[i].resB.diffuser_i4 = 0;

            // Clear voice active / pitch memory
            state.voices[i].is_active    = false;
            state.voices[i].is_releasing = false;
            state.voices[i].base_delay_A = 0.0f;
            state.voices[i].base_delay_B = 0.0f;

            // Restore non-zero waveguide defaults that PartialReset() reads before
            // overwriting them (e.g. transient_lp_base = resA.lowpass_coeff).
            state.voices[i].resA.lowpass_coeff = 1.0f;
            state.voices[i].resB.lowpass_coeff = 1.0f;
            state.voices[i].resA.loss_g_dc     = 1.0f;
            state.voices[i].resB.loss_g_dc     = 1.0f;
            state.voices[i].resA.loss_g_hf     = 1.0f;
            state.voices[i].resB.loss_g_hf     = 1.0f;
            state.voices[i].resA.phase_mult    = 1.0f;
            state.voices[i].resB.phase_mult    = 1.0f;

            // Reset all runtime modulation state: envelopes, transient shaper,
            // modal bank, pitch env, boom, metal FM, onset, snare wire, etc.
            state.voices[i].PartialReset();

            // Noise filter: LP mode, fully open (12 kHz)
            state.voices[i].exciter.noise_filter.mode = 0;
            state.voices[i].exciter.noise_filter.set_coeffs(12000.0f, 0.707f, default_sample_rate);
            // set_coeffs() only updates f/q — explicitly zero the SVF accumulators
            // to avoid a click on the next NoteOn after a patch change.
            state.voices[i].exciter.noise_filter.lp = 0.0f;
            state.voices[i].exciter.noise_filter.bp = 0.0f;
            state.voices[i].exciter.noise_filter.hp = 0.0f;
            // Hat filter: BP mode (hi-hat centroid shaping)
            state.voices[i].exciter.hat_filter.mode = 1;
            state.voices[i].exciter.hat_filter.set_coeffs(7000.0f, 1.1f, default_sample_rate);
            state.voices[i].exciter.hat_filter.lp = 0.0f;
            state.voices[i].exciter.hat_filter.bp = 0.0f;
            state.voices[i].exciter.hat_filter.hp = 0.0f;
        }

        // 1.5 -> 2.3: as loud as the bus can take before the master limiter's
        // dynamic-range guarantee (T39c: a ghosted hit must stay under 60% of
        // a neutral one) starts to erode — the limiter's soft knee compresses
        // loud hits much harder than quiet ones, so pushing gain further in
        // narrows velocity sensitivity rather than adding audible level.
        state.master_gain  = 2.3f;  // was low overall; +~3.7dB, capped by T39c
        state.master_drive = 1.0f;
        // Reset() kills every voice, so a master drive deferred behind a
        // preset-change fade has nothing left to wait for — and unlike
        // LoadPreset (whose parameter loop always rewrites Gain, which clears
        // the queue) nothing here would ever clear it.  Suspend() is
        // AllNoteOff() + Reset(), so leaving it queued means the first block
        // after Resume() installs the drive of whatever preset was mid-fade
        // when the unit was suspended, on top of the one actually loaded.
        m_pending_drive    = -1.0f;
        // Master-stage state dies with the voices too: a limiter envelope left
        // high would ride the first strike after Resume() down for ~20 ms, and
        // a non-zero idle counter would keep the master chain spinning on an
        // empty buffer.
        state.master_lim_env = 0.0f;
        m_idle_flush_blocks  = 0;
        state.mix_ab       = 0.5f; // Equal A/B mix
        state.tone         = 0.0f; // Neutral tilt EQ (LoadPreset restores the preset value)
        m_pitch_bend_mult  = 1.0f; // Clear any held bend so the next note plays in tune.

        // Always return to ResA edit context so LoadPreset (called next in Init)
        // applies preset data symmetrically to both resonators.
        m_is_resonator_a = true;
        m_is_resonator_b = true;

        // Master filter is a LOWPASS "Cutoff" (mode 0), default fully open.
        // It was a highpass ("LowCut"), but every HW pass reported the knob as
        // reversed: raising it silenced the sound.  Players expect a drum-synth
        // master cutoff to OPEN as it rises — so it is now an LP with max = open.
        state.master_filter.mode = 0;
        state.master_filter.set_coeffs(16000.0f, 0.707f, default_sample_rate);
        state.master_filter.lp = 0.0f;
        state.master_filter.bp = 0.0f;
        state.master_filter.hp = 0.0f;
    }

    inline void Resume() {
        // Called when the audio thread wakes up
    }

    inline void Suspend() {
        AllNoteOff();
        Reset();
    }

    inline void ChannelPressure(uint8_t pressure) { (void)pressure; }

    inline void Aftertouch(uint8_t note, uint8_t aftertouch) {
        (void)note;
        (void)aftertouch;
    }


    // ==============================================================================
    // 1. UI State & Preset Management
    // ==============================================================================

    // Tracks the raw UI integer for all 24 parameter slots (indices 0-23)
    int32_t m_params[k_lastParamIndex] = {0};
    uint8_t m_preset_idx = 0;
    // Modal T60 reference: the normalized Dkay the current preset shipped with.
    // Modal ring length scales RELATIVE to this so the calibrated modal_preset_configs
    // T60 always plays at the preset's default Dkay (no regression), and the Dkay knob
    // trims around it.  Captured in LoadPreset; defaults to 0.975 (Dkay≈195).
    float m_modal_dkay_ref = 0.975f;
    // Rel reference (normalized 0..1) — Rel trims modal ring length on modal
    // engines (it otherwise only gated the noise tail = dead on tonal presets).
    float m_modal_rel_ref = 0.9f;

    // ── Dense modal-drum kernel (Timpani/Taiko) ────────────────────────────
    // Single mono instance: one physical membrane, shared across hits.  The
    // remaining anchors below follow the same shipped-value pattern as the
    // m_modal_*_ref family (captured in LoadPreset for the active kernel
    // preset only).
    ModalDrumKernel m_drum_kernel;
    float   m_kernel_tone_lp   = 0.0f;   // Stage-4a tilt state for the kernel path
    float   m_kernel_vlstf_ref = 0.0f;
    float   m_kernel_vlres_ref = 0.0f;
    float   m_kernel_nzmix_ref = 0.0f;
    float   m_kernel_nzfrq_ref = 380.0f;

    // Modal brightness reference: the normalized MlltStif the preset shipped with.
    // Mallet stiffness tilts the higher modal modes' initial energy (stiffer mallet =
    // brighter strike = more high-mode energy).  Pivoted at this reference so the
    // calibrated config envs play at the default mallet, and the knob (plus velocity
    // via VlMllStf) tilts brightness around it.  Captured in LoadPreset.
    float m_modal_stiff_ref = 0.26f;
    // REFERENCE-ANCHOR pivots for the remaining modal-engine parameter mappings
    // (Model → ratio set, Partls → mode count, Inharm → ratio spread,
    //  Mterl → upper-mode material damping, HitPos → strike-position tilt).
    // All captured in LoadPreset so every preset plays its calibrated
    // modal_preset_configs sound at the shipped knob values; only knob
    // MOVEMENT away from the shipped value alters the modal bank.
    uint8_t m_modal_model_ref  = 0;     // shipped Model index
    int32_t m_modal_partls_ref = 0;     // shipped Partls UI value (0-4)
    float   m_modal_inharm_ref = 0.0f;  // shipped Inharm, normalized 0..1
    float   m_modal_mterl_ref  = 0.5f;  // shipped Mterl, normalized 0..1
    float   m_modal_hitpos_ref = 0.26f; // shipped HitPos, normalized 0..1
    float   m_modal_tubrad_ref = 0.25f; // shipped TubRad, normalized 0..1
    // On ENGINE_PLATE presets MlltRes is repurposed as the crash-bank intensity
    // (the mallet-LP it normally drives is inaudible under a cymbal wash).
    // Anchored so the shipped MlltRes plays the calibrated crash level.
    float   m_modal_mltres_ref = 0.5f;  // shipped MlltRes, normalized 0..1
    // Snare param-design anchors: VlMllStf / VlMllRes were near-dead on the
    // snare family (mallet masked by the noise; VlMllRes' attack target is
    // overridden).  Repurposed to the wire buzz (see NoteOn ENGINE_SNARE
    // block), anchored at the shipped value (×0.01, range −1..+1) so shipped
    // presets play bit-identical and only knob movement bites.
    float   m_snare_vlstf_ref  = 0.0f;  // shipped VlMllStf * 0.01
    float   m_snare_vlres_ref  = 0.0f;  // shipped VlMllRes * 0.01

    // Called by unit_get_param_value so the OS knows what to draw on the screen.
    // For Model, return the value for the currently-selected resonator so the OLED
    // stays in sync with what the user is editing via the A/B Partls selector.
    inline int32_t getParameterValue(uint8_t index) const {
        // CRITICAL UI FIX: Prevent OS out-of-bounds reads
        if (index >= k_lastParamIndex) return 0;
        if (index == k_paramModel) {
            return m_is_resonator_a ? (int32_t)m_model_a : (int32_t)m_model_b;
        }
        return m_params[index];
    }

    inline uint8_t getPresetIndex() const {
        return m_preset_idx;
    }

    // Called by unit_set_param_value(0, value) to load a new patch
    inline void LoadPreset(uint8_t idx) {
        // Reject an out-of-range preset BEFORE anything is written.  This guard
        // used to sit ~65 lines lower, after `m_preset_idx = idx`, so a bad
        // index was retained and then indexed kPresetEngine[], modal_preset_
        // configs[] and model_param_presets[] — all sized k_NumPrograms — out of
        // bounds on the next NoteOn.  header.c caps Program at 39 so a
        // well-behaved OS never reached it, but the guard exists for one that
        // does not, and where it stood it could not do its job.
        if (idx >= k_NumPrograms) return;

        // A voice cannot survive a preset change: the rest of this function
        // rewrites the shared per-voice DSP coefficients underneath it.  Arm a
        // ~10 ms fade-out so it retires click-free under its OWN latched engine
        // instead of being cut dead or re-excited by the incoming preset.
        // Only on a REAL change — Init()'s LoadPreset(0) and re-selecting the
        // current preset must stay no-ops so shipped renders are unaffected.
        const bool preset_changed = (idx != m_preset_idx);
        const float prev_drive = state.master_drive;
        if (preset_changed) {
            const float fmul = cym_env_mul(kPresetFadeTauSec, default_sample_rate);
            for (int i = 0; i < NUM_VOICES; ++i) {
                VoiceState& fv = state.voices[i];
                if (!fv.is_active) continue;
                if (fv.fade_mul >= 1.0f) fv.fade_mul = fmul;   // don't restart an existing fade
            }
        }
        m_preset_idx = idx;
        // Keep m_params[0] in sync so unit_get_param_value(0) returns the correct
        // preset index regardless of whether LoadPreset was called via setParameter
        // (Program knob) or directly via unit_load_preset().
        m_params[k_paramProgram] = idx;

        // Columns Map: NOTE keep the justification so it's easier to read!!
        // 0:Prgram | 1:Note | 2:Poly | 3:Vel    | 4:MlltRes | 5:MlltStif | 6:VlMllR | 7:VlMllS
        // 8:Prtls  | 9:Model| 10:Dkay| 11:Mterl | 12:Tone   | 13:HitPos  | 14:Rel   | 15:Inharm
        // 16:LCut  | 17:TRad| 18:Gain| 19:NzMix | 20:NzRes  | 21:NzFltr  | 22:NzFrq | 23:Resnc
        // Column order matches the ParamIndex enum above.
        // Current parameters cover all core physical-modelling dimensions (exciter, resonator,
        // noise, master FX). Phase 12/13 in PROGRESS.md track future additions (TubRad, Tone, etc.).
        // Columns 15 (Inharm) and 16 (LowCut) store 1/10th of the effective value.
        // setParameter multiplies them back by 10 so the encoder travels 10× fewer steps.
        // int16_t, not int32_t: this table is the ONE large `static const` the
        // unit still keeps in .rodata (the other big arrays are non-static class
        // members precisely so their initialisers land in .data — see the size
        // rule in CLAUDE.md), and it is read exactly twice per preset load, both
        // times through a widening conversion.  The stored range is [-10, 1999],
        // a tenth of what int16_t holds, so halving the element width is free:
        // 3840 B → 1920 B of code-segment budget, and every value round-trips
        // unchanged.  Widen it again if any column ever needs > ±32767.
        static const int16_t presets[k_NumPrograms][k_lastParamIndex] = {
            //  Prg  Nte  Ply  Vel - MlRs MlSt VlRs VlSt - Ptls Mdl  Dky  Mtr - Ton  Hit  Rel  InHm - Cut  TbRd Gain NzMx - NzRs NzFl NzFq Rsnc
            //
            // Cols 2 (Poly) and 3 (Velocity) are GLOBAL performance controls:
            // LoadPreset skips them, so the stale values still sitting in those
            // columns are never read.  Col 8 (Partls) doubles as the cymbal
            // resonator DENSITY on the six ENGINE_CYMBAL rows (13/14/27/32/33/
            // 37), where the shared modal bank it normally drives is bypassed:
            // density = 25 + 5 x Partls %, so their shipped 3 = the 40 % the
            // ex-"Rsntrs" knob defaulted to.
            //
            // COUPLING RULE (Phase 25: dynamic clamp in render loop):
            //   safe_coupling ≤ (1 − feedback_gain) × 0.8 guarantees stability at
            //   ANY Partials/Decay combination (KS engine only).
            //
            // Col 16 is the master LOWPASS Cutoff (stored ÷10): 1999 = fully open.
            // Col 15 (Inharm) is stored ÷10 (0-199): code uses value×0.005 and the
            // encoder shows value×10 (coarsened from the old 0-1999 step-1 range).
            {   0,  36,   0,   1, 360,  30,   0,  40,   2,   3, 200,  10,   0,  36,  18,   1,1999,   0,   4,   1, 420,   0, 380,  71},        // 0:  Kick2     — the pre-redesign Timpani body as a solid kettledrum kick (HW-approved)
            {   1,  72,   0,   1, 800,  13,   0,   0,   0,   6, 194,  -7,   0,   0,   5,   2,1999,   7,  20,   0, 300,   0,1200,  71},        // 1:  Marimba   — exemplar BAR voice (HW: ok)
            {   2,  36,   0,   1, 350,  35,   0,   0,   2,   5, 195,  -5,   0,  38,   6,   0,1999,   3,  14,   5, 220,   0, 220,  71},        // 2:  808Sub    — boom_osc pitch dive 160→45Hz, depth on Inharm (pass 22's mapping, live again since pass 35).  InHm is back to 0 here: pass 35 had moved it to 40 purely to re-centre that knob, and pass 36 made Inharm BIPOLAR (-100..100) which centres EVERY preset at once, so the per-preset hack is obsolete.  Inharm's other two roles are dead on this preset anyway — the ap_coeff write is KS-only and the overtone spread needs modal modes, and 808Sub's modal config is empty

            {   3,  38,   0,   1, 120,  28,   0,   0,   2,   5, 168,  -7,   0,  46,   9,   0,1999,   8,   7,  52, 950,   2, 300,  71},        // 3:  AcSnare   — wire path live; NzRs 740→950: buzz T60≈0.4s matches acoustic-snare.wav t40≈280ms; NzFq 480→300 (HP 3kHz) pulls centroid toward the darker reference
            {   4,  72,   0,   1, 900,  34,   0,   0,   0,   1, 200,  30,   0,   0,  20,   1,1999,  18,   0,   5, 300,   0,1500,  71},        // 4:  TblrBel
            {   5,  52,   0,   1, 360,  30,   0,  40,   2,   3, 150,  10,   0,  36,  18,   1,1999,   0,   4,   1, 420,   0, 380,  71},        // 5:  Timpani   — dense-kernel voice.  Note 52 (E3): the kettle's dominant sustained partial measures 165.5 Hz ≈ E3 (the 110 Hz A2 principal sits a fifth below) — the display now names the pitch you hear; 52 is also the kernel recipe root, so the shipped row plays the approved render at ratio 1
            {   6,  48,   0,   1, 600,  35,   0,   0,   1,   5, 152,   0,   0,  35,  12,   1,1999,  15,   5,   7, 450,   0, 500,  71},        // 6:  Djambe    — (HW: ok)
            {   7,  41,   0,   1, 250,  45,   0,   0,   1,   5, 120,  10,   0,  30,  15,   0,1999,  16,   5,  52, 180,   0, 800,  71},        // 7:  Taiko     — bright open "TAAAN": data-driven inharmonic modes + bright 1472Hz partial (ratio 16.86, the "AAN" vowel) from Taiko-Hit.wav. Port retune: modal_mix 0.60 + brighter crack (NzMix 36→52, NzFltFrq 360→800 ≈8kHz) + leaner boom for a brighter, longer ring
            {   8,  65,   0,   1, 720,  50,   0,   0,   1,   5, 190,  20,   0,  50,   8,   2,1999,  19,   5,  55, 940,   2, 105,  71},        // 8:  MrchSnr   — click+buzz land together; NzRs 800→940: buzz T60≈0.36s matches Marching-Snare ref t40≈220ms
            {   9,  60,   0,   1, 600,  42,   0,   0,   0,   0, 200,  20,   0,   0,  12,   0,1999,  18,   0,   0, 300,   0,1000,  71},        // 9:  Koto      — + harmonic-overtone modal bank (mix 0.10)
            {  10,  72,   0,   1, 500,  30,   0,   0,   0,   1, 200,  28,   0,   0,  18,   0,1999,  13,   0,   0, 300,   0,1000,  71},        // 10: Vibrph
            {  11,  48,   0,   1, 900,  50,   0,   0,   0,   2, 156,  24,   0,   0,   2,   1,1999,   3,   0,   5, 420,   0, 900,  71},        // 11: Wodblk    — (HW: ok)
            {  12,  45,   0,   1, 450,  30,   0,   0,   2,   5, 200,   0,   0,  44,  11,   0,1999,   9,   5,  30, 360,   0, 520,  71},        // 12: Ac Tom    — body extended (modal 350→500ms) + stick transient layer + NzMx 20→30 for the close-mic attack brightness
            {  13,  65,   0,   1, 800,  45,   0,   0,   3,   4, 200,  28,   0,   0,  18,   1,1999,  15,   5,  62, 640,   2,1200,  71},        // 13: Cymbal    — noise⇄ring cross-modulation (modal_rm_depth 0.70)
            {  14,  50,   0,   1, 200,   2,   0,   0,   3,   4, 190,   1,   0,   0,  20,   1,1999,  20,  20,  34, 860,   0,  30,  71},        // 14: Gong      — NzMx 19→26 + FM depth 0.18 for more crash onset; rm_depth 0.60
            {  15,  65,   0,   1, 700,  39,   0,   0,   0,   1, 192,   6,   0,   0,   5,   0,1999,   7,   3,  10, 260,   0, 720,  71},        // 15: Kalimba
            {  16,  60,   0,   1, 600,   0,   0,   0,   0,   4, 200,  18,   0,   0,  12,   0,1999,   9,   5,   0, 300,   0,1000,  71},        // 16: StelPan
            {  17,  79,   0,   1, 900,  48,   0,   0,   0,   2,  13,  -3,   0,   0,   1,   0,1999,   1,   0,  20, 260,   0, 800,  71},        // 17: Claves
            {  18,  67,   0,   1, 800,  42,   0,   0,   0,   4, 175,  20,   0,   0,   4,   3,1999,   3,  30,  40, 300,   0,1000,  71},        // 18: Cowbell   — clank shortened (modal cfg 500→180ms) + more strike noise (NzMx 25→40) toward the bright ref clank
            {  19,  69,   0,   1, 900,  47,   0,   0,   0,   1,  60,  20,   0,   0,  15,   2,1999,  20,   0,  10, 300,   0,1500,  71},        // 19: Triangle  — NzMx 5→10: feeds the raised hf_branch (NoteOn) for the missing >8kHz metal sheen.  Dkay 190→60 (pass 41) moves the ANCHOR with the shortened T60s in modal_preset_configs — the two must change together or the knob just re-lengthens the ring.
            {  20,  36,   0,   1, 380,  35,   0,   0,   2,   5, 195,  -5,   0,  38,   6,   0,1999,   3,  12,  15, 220,   0, 220,  71},        // 20: Kick Drum — (HW: ok)
            {  21,  60,   0,   1, 500,  27,   0,   0,   2,   5,  15,   5,   0,  50,  19,   0,1999,   3,   5,  95, 950,   1, 200,  71},        // 21: Clap      — multi-burst AM (~55Hz, NoteOn) + Rel 19 for the 'tcha' tail; NzFq 300→200 (BP 2kHz) toward ref centroid 2986Hz (was 4540)
            {  22,  84,   0,   1, 500,  45,   0,   0,   2,   6,  30,   5,   0,  50,  19,   0,1999,   3,   5,  95, 940,   1, 550,  71},        // 22: Shaker    — Rel 18→19 longer tail so the now-sustained 17Hz rattle (noise_am_decay=1.0) is audible by default; raise Rel for a longer rattle
            {  23,  41,   0,   1, 250,  39,   0,   0,   1,   5, 200,  10,   0,  30,  15,   0,1999,  11,   5,   9, 550,   0, 130,  71},        // 23: Taiko2    — the pre-redesign Taiko (deep membrane), replaces PluckBass per HW request
            {  24,  76,   0,   1, 700,   5,   0,   0,   0,   4, 200,  30,   0,   0,  18,   1,1999,  18,   0,   0, 300,   0,1200,  71},        // 24: GlsBwl
            {  25,  72,   0,   1, 100,  37,   0,   0,   2,   5,  12,  10,   0,  50,  18,   0,1999,   3,   3,  90, 900,   2, 800,  71},        // 25: HHat-C    — the pre-redesign Shaker noise voice ('a perfect closed hi-hat' per HW)
            {  26,  79,   0,   1, 900,  49,   0,   0,   3,   4, 210,  18,   0,   0,  18,   1,1999,  12,   0,  90,1000,   2,1300,  71},        // 26: HHat-O    — ring and noise now cross-modulated (rm_depth 0.50)
            {  27,  62,   0,   1, 600,  43,   0,   0,   1,   5, 158,   3,   0,   0,  10,   1,1999,   9,   0,  15, 520,   0, 450,  71},        // 27: Conga     — open tone extended (modal cfg 90→250ms), slap softened (NzFq 710→450)
            {  28,  62,   0,   1, 700,  30,   0,   0,   0,   4, 190,  22,   0,   0,  20,   0,1999,  18,   0,   5, 300,   0,1000,  71},        // 28: Handpn
            {  29,  84,   0,   1, 900,  42,   0,   0,   0,   1, 200,  20,   0,   0,   8,   1,1999,   3,   0,   0, 300,   0,1200,  71},        // 29: BelTre
            {  30,  60,   0,   1, 700,  27,   0,   0,   0,   6, 177,   8,   0,   0,  10,   1,1999,   3,   0,   0, 300,   0, 800,  71},        // 30: SltDrm
            {  31,  69,   0,   1, 900,  50,   0,   0,   3,   4, 190,  28,   0,   0,  18,   1,1999,  17,   0,  66, 950,   2,1300,  71},        // 31: Ride      — thick-plate modal ratios replace near-harmonic set (was 'a string sound')
            {  32,  60,   0,   1, 900,  49,   0,   0,   3,   4, 200,  16,   0,   0,   8,   1,1999,   1,   0,  60, 950,   2,1300,  71},        // 32: RidBel    — bell-partial ratios 1:2:3.01:4.7
            {  33,  50,   0,   1, 650,  41,   0,   0,   0,   5, 162, -10,   0,   0,   8,   0,1999,   0,   0,   0, 520,   0, 450,  71},        // 33: Bongo     — + wood 'tock' mode 5 in modal config
            {  34,  88,   0,   1, 100,  45,   0,   0,   0,   7, 200,   5,   0,   0,   5,   0,1999,  19,   0,  50, 110,   0, 390,  71},        // 34: GlsBotl   — (HW: ok)
            {  35,  79,   0,   1, 900,  50,   0,   0,   0,   4, 160,  14,   0,   0,   2,   2,1999,   3,   0,  58, 960,   2, 900,  71},        // 35: Tick      — the pre-redesign HHat-C chick + clack mode (modal cfg)
            {  36,  76,   0,   1, 800,  45,   0,   0,   3,   4, 200,  28,   0,   0,  18,   1,1999,  15,   5,  62, 640,   2,1200,  71},        // 36: Splash    — small pitched splash (ENGINE_CYMBAL)
            {  37,  55,   0,   1, 120,   8,   0,  60,   2,   5, 160,  -7,   0,  46,  17,   0,1999,   8,   5,  95, 975,   1, 320,  71},        // 37: BrshSnr   — DATA-DRIVEN (corrected brush refs: snare_brush_hard/medium/soft.wav): BP noise 3.6kHz (ref centroid ~4.2kHz, 2-6kHz≈57%, flatness≈0.31 = colored not white), NzMx 95 (mallet ~silent), VlMllStf 60 = velocity→decay length (soft 185ms→hard 315ms), ~22ms swish onset
            {  38,  69,   0,   1, 500,  48,   0,  20,   2,   5, 168,   5,   0,  80,   6,   0,1999,   8,   7,  55, 540,   1, 300,  71},        // 38: RimShot   — DATA-DRIVEN (rimshot-snare.wav): note 69 anchors the 877Hz honk at ratio 2.0; BP noise 3kHz (ref centroid 3.1k, 56% in 1-3k, 10% in 3-6k); NzRs 540 → tight buzz (ref t40 45ms)
            {  39,  53,   0,   1, 420,  36,   0,   0,   2,   5, 190,   0,   0,  52,  12,   0,1999,   6,   5,  48, 300,   0, 700,  71}       // 39: RackTom   — note 53 (F3, 174.6 Hz) = the rack tom an octave-ish over Ac Tom's 45/110 Hz.  What voices it are the ABSOLUTE columns: NzMx 38 and NzFq 700 (7 kHz vs Ac Tom's 5.2) put more bright stick in the mix, NzRs 300 keeps it short.  Dky/Rel/Hit/Mtr/TbRd are REFERENCE ANCHORS on this engine — LoadPreset captures each into m_modal_*_ref from this very row, so every consumer's 2^(k·Δ) is exactly 1 here and they set where the knobs SIT, not the voicing.  (Mterl/TubRad additionally write resA's loss filter absolutely, but that is read only under voice_engine == ENGINE_KS, so it is dead on a membrane.)  Ring length lives in modal_preset_configs, shell in model_param_presets
        };

        // Preset loading always targets ResA first, then ResB, regardless of the
        // current editor selection.  Save both flags and restore them afterwards so
        // a preset load never changes which resonator(s) the user is editing.
        bool saved_is_a = m_is_resonator_a;
        bool saved_is_b = m_is_resonator_b;
        m_is_resonator_a = true;
        m_is_resonator_b = false;

        // Apply parameters, SKIPPING INDEX 0 to prevent infinite recursion stack overflow!
        // Also skip Poly and Velocity (the two ex-Bank/Sample slots): they are
        // global performance settings, not per-preset sound design — the user's
        // choice persists across preset changes.  (The cymbal resonator density
        // that used to live in slot 3 is NOT global any more: it rides on
        // Partls, which is a normal per-preset column and loads with the row.)
        for (uint8_t param_id = 0; param_id < k_lastParamIndex; ++param_id) {
            if (param_id == k_paramProgram) continue;
            if (param_id == k_paramCymPoly) continue;
            if (param_id == k_paramVelocity) continue;

            // FIX: Enforce ResA-only routing on every single parameter
            // so k_paramPartls (index 8) cannot hijack the rest of the loop.
            m_is_resonator_a = true;
            m_is_resonator_b = false;

            setParameter(param_id, presets[idx][param_id]);
        }

        // Mirror the four per-resonator physical params to ResB so both resonators
        // start identically on every preset load (user can diverge them afterwards).
        m_is_resonator_a = false;
        m_is_resonator_b = true;
        setParameter(k_paramModel,  presets[idx][k_paramModel]);
        setParameter(k_paramDkay,   presets[idx][k_paramDkay]);
        setParameter(k_paramMterl,  presets[idx][k_paramMterl]);
        setParameter(k_paramInharm, presets[idx][k_paramInharm]);
        // NOTE: If ResA and ResB have the exact same decay and material, how do we get that chaotic,
        // realistic 2D drum sound? It happens in NoteOn function, driven entirely by the Model parameter,
        // where resonator B is given an irrational tuning ratio of 0.68 to simulate the metallic,
        // clashing overtones of the drum skin (the edge mode).
        // Resonator A acts as the fundamental "thump" of the drum (the center mode).

        // Capture the preset's shipped Dkay as the modal T60 reference point.
        // At this Dkay the modal ring plays exactly the calibrated config T60.
        m_modal_dkay_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramDkay] * 0.005f));
        // Same pattern for mallet stiffness → modal brightness pivot.
        m_modal_stiff_ref = fmaxf(0.01f, fminf(1.0f, (float)presets[idx][k_paramMlltStif] * 0.02f));
        // Anchors for the modal-engine mappings of Model / Partls / Inharm /
        // Mterl / HitPos (see NoteOn): neutral at the shipped knob values.
        m_modal_model_ref  = (uint8_t)presets[idx][k_paramModel];
        m_modal_partls_ref = (presets[idx][k_paramPartls] <= 4) ? presets[idx][k_paramPartls] : 0;
        m_modal_inharm_ref = fmaxf(-1.0f, fminf(1.0f, (float)presets[idx][k_paramInharm] * 0.01f));
        m_modal_mterl_ref  = (fmaxf(-10.0f, fminf(30.0f, (float)presets[idx][k_paramMterl])) + 10.0f) * 0.025f;
        m_modal_hitpos_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramHitPos] * 0.01f));
        m_modal_tubrad_ref = fmaxf(0.0f, fminf(20.0f, (float)presets[idx][k_paramTubRad])) * 0.05f;
        m_modal_mltres_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramMlltRes] * 0.001f));
        m_modal_rel_ref    = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramRel] * 0.05f));
        m_snare_vlstf_ref  = (float)presets[idx][k_paramVlMllStf] * 0.01f;
        m_snare_vlres_ref  = (float)presets[idx][k_paramVlMllRes] * 0.01f;

        // ── Dense modal-drum kernel (Timpani/Taiko): bind the recipe behind
        // the approved standalone renders (105_timp_wedge / 110_taiko_wedge)
        // and capture the remaining shipped-value anchors.  ref_drive is the
        // master_drive the shipped Gain column produces; the kernel master
        // stage divides it back out so the shipped preset is transparent.
        if (idx == k_Timpani || idx == k_Taiko) {
            m_kernel_vlstf_ref = (float)presets[idx][k_paramVlMllStf] * 0.01f;
            m_kernel_vlres_ref = (float)presets[idx][k_paramVlMllRes] * 0.01f;
            m_kernel_nzmix_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramNzMix] * 0.01f));
            m_kernel_nzfrq_ref = fmaxf(20.0f, (float)presets[idx][k_paramNzFltFrq]);
            float ref_drive = 1.0f + fmaxf(0.0f, (float)presets[idx][k_paramGain] * 0.01f) * 20.0f;
            m_drum_kernel.Configure((idx == k_Timpani) ? &kTimpaniRecipe : &kTaikoRecipe, ref_drive);
            m_kernel_tone_lp = 0.0f;
            RefreshKernelMods();
            // The legacy voice loop is bypassed entirely while a kernel preset
            // is active, so a fading voice would never get rendered and would
            // hold its slot forever.  This is the one switch the ~10 ms fade
            // cannot cover: retire the voices outright (as before), and clear
            // the fade so the per-voice preset state above is re-applied on the
            // next non-kernel load instead of being skipped as "still fading".
            for (int vi = 0; vi < NUM_VOICES; ++vi) {
                state.voices[vi].is_active = false;
                state.voices[vi].is_releasing = false;
                state.voices[vi].fade = 1.0f;
                state.voices[vi].fade_mul = 1.0f;
            }
        } else {
            m_drum_kernel.Deactivate();
        }

        // Restore both flags so the user's edit context survives preset loads.
        m_is_resonator_a = saved_is_a;
        m_is_resonator_b = saved_is_b;

        // Hold the OUTGOING master drive until the fades finish.  Gain is a
        // master parameter, so the incoming preset's drive would otherwise hit
        // the outgoing tail: GtrStr (Gain 0, drive 1.0) -> Gong (Gain 20, drive
        // 5.0) drove a quiet fading string into the soft clip at 0.83, well
        // above the 0.52 tail it was replacing.  Pre-scaling the voice cannot
        // fix this — the ±0.99 clamp sits between the voice and the drive, so
        // attenuating the voice just moves it out from under the clamp.
        // Deferring the drive keeps the fading voice's chain bit-for-bit what
        // it was; the new drive lands ~10 ms later, before any new hit can
        // meaningfully need it.
        //
        // A deferral cannot go stale here: the parameter loop above always
        // writes k_paramGain, whose case clears m_pending_drive before this
        // block can re-arm it, so the queue only ever holds the drive of the
        // preset now loading.  (Reset() has no such loop — see the explicit
        // clear there.)
        if (preset_changed && state.master_drive != prev_drive) {
            bool fading = false;
            for (int i = 0; i < NUM_VOICES; ++i)
                if (state.voices[i].is_active && state.voices[i].fade_mul < 1.0f) fading = true;
            if (fading) {
                m_pending_drive  = state.master_drive;
                state.master_drive = prev_drive;
            }
        }

        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            // Skip a voice that is fading out from the PREVIOUS preset: these
            // writes would retune its wire/boom/pitch state mid-fade, and
            // init_modal_modes below would re-strike its modal bank at full
            // amplitude.  It gets these values on its next NoteOn anyway.
            if (v.is_active && v.fade_mul < 1.0f) continue;
            v.exciter.snare_wire_z1 = preset_param(static_cast<ProgramIndex>(idx), k_snare_wire_z1);
            v.exciter.snare_wire_z2 = preset_param(static_cast<ProgramIndex>(idx), k_snare_wire_z2);
            v.exciter.snare_wire_mix = preset_param(static_cast<ProgramIndex>(idx), k_snare_wire_mix);
            v.exciter.snare_wire_a1 = preset_param(static_cast<ProgramIndex>(idx), k_snare_wire_a1);
            v.exciter.snare_wire_a2 = preset_param(static_cast<ProgramIndex>(idx), k_snare_wire_a2);
            v.exciter.wire_onset_env = preset_param(static_cast<ProgramIndex>(idx), k_wire_onset_env);
            v.exciter.wire_onset_attack = preset_param(static_cast<ProgramIndex>(idx), k_wire_onset_attack);
            v.exciter.noise_lp_state = preset_param(static_cast<ProgramIndex>(idx), k_noise_lp_state);
            v.exciter.noise_band_mix = preset_param(static_cast<ProgramIndex>(idx), k_noise_band_mix);
            v.exciter.noise_hi_lp_state = preset_param(static_cast<ProgramIndex>(idx), k_noise_hi_lp_state);
            v.exciter.noise_hi_lp_coeff = preset_param(static_cast<ProgramIndex>(idx), k_noise_hi_lp_coeff);
            v.exciter.use_hat_filter = (bool)preset_param(static_cast<ProgramIndex>(idx), k_use_hat_filter);
            v.resA.diffuser_mix = preset_param(static_cast<ProgramIndex>(idx), k_diffuser_mix);
            v.resB.diffuser_mix        = v.resA.diffuser_mix;
            v.pitch_env = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env);
            v.pitch_env_decay = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env_decay);
            v.pitch_env_amt = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env_amt);
            v.boom_inc = preset_param(static_cast<ProgramIndex>(idx), k_boom_inc);
            v.boom_env = preset_param(static_cast<ProgramIndex>(idx), k_boom_env);
            v.boom_decay = preset_param(static_cast<ProgramIndex>(idx), k_boom_decay);
            v.boom_mix = preset_param(static_cast<ProgramIndex>(idx), k_boom_mix);
            v.boom_attack_env = preset_param(static_cast<ProgramIndex>(idx), k_boom_attack_env);
            v.boom_attack_inc = preset_param(static_cast<ProgramIndex>(idx), k_boom_attack_inc);
            v.reed_nl_enabled = (bool)preset_param(static_cast<ProgramIndex>(idx), k_reed_nl_enabled);
            v.reed_nl_drive = preset_param(static_cast<ProgramIndex>(idx), k_reed_nl_drive);

            const ModalPresetConfig& modal_cfg = modal_preset_configs[idx];
            if (modal_cfg.mode_count > 0) {
                v.init_modal_modes(modal_cfg.ratio2, modal_cfg.ratio3, modal_cfg.ratio4,
                                   modal_cfg.t60_1_ms, modal_cfg.t60_2_ms,
                                   modal_cfg.t60_3_ms, modal_cfg.t60_4_ms,
                                   modal_cfg.mix, modal_cfg.env1, modal_cfg.env2,
                                   modal_cfg.env3, modal_cfg.env4, modal_cfg.mode_count,
                                   modal_cfg.ratio5, modal_cfg.ratio6,
                                   modal_cfg.env5, modal_cfg.env6);
            }
        }
    }

    static inline const char * getPresetName(uint8_t idx) {
        static const char* const preset_names[] = {
            "Kick2",   "Marmba", "808Sub", "AcSnre",
            "TblrBel", "Timpni", "Djambe", "Taiko",
            "MrchSnr", "Koto",   "Vibrph",
            "Wodblk",  "Ac Tom", "Cymbal", "Gong",
            "Kalimba", "StelPan","Claves", "Cowbel",
            "Trngle",  "Kick",   "Clap",   "Shaker",
            "DeepBs",  "GlsBwl",
            "HHat-C",  "HHat-O", "Conga",  "Handpn",
            "BelTre",  "SltDrm",
            "Ride",    "RidBel",
            "Bongo",   "GlsBotl","Tick",
            "Splash",  "BrshSnr","RimShot",
            "RackTom"
        };
        if (idx < k_NumPrograms) return preset_names[idx];
        return "Unknown";
    }

    // ==============================================================================
    // 2. Parameter Binding (UI Thread)
    // ==============================================================================
    inline void setParameter(uint8_t index, int32_t value) {
        // CRITICAL UI FIX: Prevent OS out-of-bounds writes
        if (index >= k_lastParamIndex) return;
        m_params[index] = value;

        switch(index) {
            case k_paramProgram:
                LoadPreset((uint8_t)value);
                break;

            case k_paramNote:
                m_ui_note = (uint8_t)fmaxf(1.0f, fminf(126.0f, value));
                break;

            case k_paramCymPoly:
                // Global voice cap 1-4 (HW: "Poly always showed 2, not the
                // actual polyphony").  Stacking engines round-robin within the
                // first m_poly slots.  The cymbal family used to be clamped to
                // 2 here for the CPU budget; it now honours the full range —
                // repeated gong/crash hits must accumulate, and the CPU is
                // bounded instead by kCymResonatorBudget (a hard ceiling on the
                // TOTAL bank size across simultaneous cymbal voices), which is
                // a stronger guarantee than a voice count alone.
                m_poly     = (uint8_t)((value < 1) ? 1 : ((value > 4) ? 4 : value));
                m_cym_poly = m_poly;
                break;

            case k_paramVelocity:
                // Global performance control: bias every incoming strike.  0 is
                // NEUTRAL (the note plays at exactly the velocity it was sent
                // with, so the shipped presets and every render are unchanged),
                // negative = ghost notes, positive = "wham".  This stores the
                // raw -1..+1 knob; the curve lives in vel_bias_apply().
                m_vel_bias = fmaxf(-1.0f, fminf(1.0f, (float)value * 0.01f));
                break;
            case k_paramMlltStif: {
                // Stored ÷100 (0-50 represents 0-5000). Divide by 50 (the max).
                float norm = fmaxf(0.01f, fminf(1.0f, (float)value * 0.02f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.mallet_stiffness = norm;
                }
                break;
            }

            case k_paramMlltRes: {
                // UI range 0-1000 (displays with 1 decimal via frac_type=1).
                // Maps to a second 1-pole LP coefficient stacked after mallet_stiffness LP.
                // Low value → darker/softer mallet body. High value → brighter/sharper.
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.001f));
                float coeff = 0.01f + (norm * 0.99f);
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.mallet_res_coeff = coeff;
                }
                break;
            }

            // resonator A/B parameters
            case k_paramPartls: {
                if (value < 0) break;   // never index partial_counts[] negatively
                // ── Cymbal family: Partls IS the resonator-bank density ──────
                // The dense-resonator cymbal bypasses the shared modal bank, so
                // Partls (mode count / ResB coupling) was inert on all six
                // ENGINE_CYMBAL presets while a whole GUI slot (ex-"Rsntrs")
                // carried nothing else.  Fold the density onto the dead knob and
                // the slot is free for the Velocity control.  Unlike Rsntrs the
                // density is now a normal per-preset column: LoadPreset walks
                // it like every other param (m_preset_idx is already the
                // incoming preset by then), so each cymbal row carries its own
                // bank size and a non-cymbal row cannot resize the cymbals.
                // The 8 knob positions span the old Rsntrs range 25-60 % in
                // steps of 5; index 3 = 40 %, the shipped default the six
                // cymbal rows now store.  Computed, not tabled, so it costs no
                // .rodata (see the size rule in CLAUDE.md).
                //
                // On a cymbal preset this is the WHOLE meaning of the knob: the
                // modal-bank half below is skipped, and in particular positions
                // 5-7 must not touch the ResA/ResB EDIT SELECTOR.  LoadPreset
                // saves and restores that selector across a preset change, so a
                // density of 60 % (7 = "ResB only") would otherwise follow the
                // user out of the cymbal preset and leave Model/Dkay/Mterl/
                // Inharm writing to ResB alone on the next drum they loaded.
                if (kPresetEngine[m_preset_idx] == ENGINE_CYMBAL) {
                    if (value <= 7) m_cym_reso_scale = (uint8_t)(25 + 5 * value);
                    break;
                }
                if (value < 5) {
                    // Map the UI index (0-4) to the actual partial count (4/8/16/32/64).
                    // m_active_partials stores the count so comparisons are self-documenting.
                    // DSP effect: counts < 16 disable ResB (single resonator, lower CPU);
                    // counts >= 16 enable ResB (dual resonator, richer harmonic content).
                    // Partials is intentionally global (not per-resonator) because it
                    // controls CPU budget, not per-resonator timbre.
                    static const uint8_t partial_counts[] = {4, 8, 16, 32, 64};
                    m_active_partials = partial_counts[value];
                    // Store coupling depth from UI index so Partls=5/6
                    // (editor-select modes) never overwrite this.
                    m_coupling_depth = (float)value * 0.25f;
                } else {
                    // Resonator edit selector:
                    // 5 => edit both (AB), 6 => ResA only, 7 => ResB only.
                    m_is_resonator_a = (value == 5) || (value == 6);
                    m_is_resonator_b = (value == 5) || (value == 7);
                }
                break;
            }

            case k_paramModel: {
                if (m_is_resonator_a)
                    m_model_a = value;
                if (m_is_resonator_b)
                    m_model_b = value;
                // Per-model baseline allpass dispersion — gives each physical model a
                // distinct inharmonic character independently of the Inharm (ap_coeff) knob.
                // Values calibrated to physical stiffness constants:
                //   String≈0 (flexible), Beam≈0.06 (mild stiffness), Square≈0.12,
                //   Membrane≈0.01 (nearly harmonic), Plate≈0.08, Drumhead≈0.02,
                //   Marimba≈0.04 (tuned bar), OpenTube=0/ClosedTube=0 (perfectly harmonic).
                static const float ap_base_by_model[] = {
                    0.00f, // 0: String
                    0.06f, // 1: Beam
                    0.12f, // 2: Square plate
                    0.01f, // 3: Membrane
                    0.08f, // 4: Plate
                    0.02f, // 5: Drumhead
                    0.04f, // 6: Marimba bar
                    0.00f, // 7: Open Tube
                    0.00f, // 8: Closed Tube
                };
                for (int i = 0; i < NUM_VOICES; ++i) {
                    if (m_model_a == k_OpenTube || m_model_a == k_ClosedTube) {
                        state.voices[i].resA.phase_mult = -1.0f;
                    } else {
                        state.voices[i].resA.phase_mult = 1.0f;
                    }
                    if (m_model_b == k_OpenTube || m_model_b == k_ClosedTube) {
                        state.voices[i].resB.phase_mult = -1.0f;
                    } else {
                        state.voices[i].resB.phase_mult = 1.0f;
                    }
                    if (m_is_resonator_a && m_model_a < k_lastModel)
                        state.voices[i].resA.model_ap_base = ap_base_by_model[m_model_a];
                    if (m_is_resonator_b && m_model_b < k_lastModel)
                        state.voices[i].resB.model_ap_base = ap_base_by_model[m_model_b];
                }
                break;
            }

            case k_paramDkay: {
                // 0.85 = instant dead thud. 0.999 = rings for ~5 seconds.
                // Stored ÷10 (0-200 represents 0-2000). Divide by 200 (new max).
                if (value <= 200) {
                    float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.005f));
                    // Drumhead (model 5): percussion skins need a shorter minimum decay
                    // than strings/bars. Formula [0.45, 0.999] lets Dkay=55 give
                    // T60≈175ms@65Hz — natural kick/tom body ring before boom takes over.
                    // Membrane (3) and all other models keep [0.85, 0.999].
                    float g = ((m_is_resonator_a && m_model_a == 5) || (m_is_resonator_b && m_model_b == 5))
                              ? (0.45f + norm * 0.549f)
                              : (0.85f + norm * 0.149f);
                    // master_env gate: exponential 50ms (Decay=0) → 10s (Decay=200).
                    // Decay is the primary sustain control; Rel only gates the noise
                    // burst.  Without this, the master_env would kill the waveguide
                    // resonance at ~28 ms (default Rel) regardless of Decay setting.
                    float t_s = fasterpow2f(k_log_2_of_200 * norm); // 50ms..10s - was fasterpowf(200.0f, norm)
                    float master_rate = M_THREELN10 * 20 * inverse_default_sample_rate / t_s;  // was 3.0f * M_LN10
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        if (m_is_resonator_a)
                            state.voices[i].resA.feedback_gain = g;
                        if (m_is_resonator_b)
                            state.voices[i].resB.feedback_gain = g;
                        // Always update regardless of which resonator is selected —
                        // master_env is voice-level, not per-resonator.
                        state.voices[i].exciter.master_env.release_rate = master_rate;
                        // Auto-decay rate: 30% of release rate.  Ensures sounds
                        // decay naturally even while the gate is held (percussion
                        // on a drum machine should never sustain indefinitely).
                        // NoteOff switches to the faster release_rate for a clean tail.
                        state.voices[i].exciter.master_env.decay_rate = master_rate * 0.3f;
                    }
                }
                break;
            }

            case k_paramMterl:
            case k_paramTubRad: {
                // Combine Material (-10 to 30) and Tube Radius (0 to 20).
                // Either parameter changing recalculates the coefficient from both stored values.
                float mterl_norm = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
                float tubrad_norm = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
                // Base material loss (0.01 = dull wood to 1.0 = lossless metal)
                float coeff = 0.01f + (mterl_norm * 0.99f);
                // Wider tube pulls the coefficient towards 1.0 (less high-frequency loss)
                coeff = coeff + ((1.0f - coeff) * (tubrad_norm * 0.8f));
                float hf_loss = fmaxf(0.15f, fminf(1.0f, 0.25f + (coeff * 0.75f)));
                float dc_gain = fmaxf(0.85f, fminf(1.0f, 0.90f + (coeff * 0.10f)));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    if (m_is_resonator_a) {
                        state.voices[i].resA.lowpass_coeff = coeff;
                        state.voices[i].resA.loss_g_dc = dc_gain;
                        state.voices[i].resA.loss_g_hf = hf_loss;
                        state.voices[i].transient_lp_base_a = coeff;
                    }
                    if (m_is_resonator_b) {
                        state.voices[i].resB.lowpass_coeff = coeff;
                        state.voices[i].resB.loss_g_dc = dc_gain;
                        state.voices[i].resB.loss_g_hf = hf_loss;
                        state.voices[i].transient_lp_base_b = coeff;
                    }
                }
                break;
            }
            // HitPos parameter acts as the physical mixer between these two modes.
            // If HitPos is 0, you only hear ResA (hitting dead center).
            // If HitPos is 100, you hear mostly ResB (hitting the rim).
            case k_paramTone: {
                state.tone = fmaxf(-10.0f, fminf(30.0f, (float)value));
                break;
            }

            case k_paramHitPos: {
                // HitPos's SECOND absolute consumer (the strike radius in
                // NoteOn is the first), and the floor at 0 is deliberate now
                // that the range is bipolar (pass 39): mix_ab is a 0..1 blend
                // of ResA into ResB, so 0 already IS the extreme (all ResA)
                // and there is nothing below it.  Inert under centre on
                // ENGINE_KS, like Inharm's allpass — not a missed clamp.
                state.mix_ab = fmaxf(0.0f, fminf(1.0f, (float)value * 0.01f));
                break;
            }

            case k_paramRel: {
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.05f));
                // Rel controls only the noise burst release time (0→fast snap,
                // 20→slow noise tail).  master_env gate is tied to Decay instead,
                // so the waveguide resonance isn't prematurely killed by a short Rel.
                float rel_rate = 0.00005f + ((1.0f - norm) * 0.01f);
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_env.release_rate = rel_rate;
                    // High band should decay faster than low-band body.
                    state.voices[i].exciter.noise_env_hi.release_rate = fminf(0.99f, rel_rate * 2.5f);
                }
                break;
            }

            case k_paramInharm: {
                if (value <= 100) {
                    // Bipolar -100..100 (÷10 encoder).  ×0.01 normalises to
                    // -1..1.  This one consumer is ABSOLUTE, not anchored: it
                    // is the KS allpass coefficient, which must stay >= 0, so
                    // the negative half of the knob floors here rather than
                    // inverting the allpass.  Both KS presets ship Inharm 0,
                    // so nothing is lost that used to work.
                    // Capped at 0.995, NOT 1.0: this feeds a one-pole allpass,
                    // and a coefficient of exactly 1.0 puts its pole on the
                    // unit circle.  The old 0..199 range reached 199×0.005 =
                    // 0.995 and so was implicitly safe; ×0.01 would hit 1.000
                    // at the new maximum, so the bound is now explicit.
                    float norm = fmaxf(0.0f, fminf(0.995f, (float)value * 0.01f));
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        if (m_is_resonator_a) {
                            state.voices[i].resA.ap_coeff = norm;
                            state.voices[i].transient_ap_base_a = norm;
                        }
                        if (m_is_resonator_b) {
                            state.voices[i].resB.ap_coeff = norm;
                            state.voices[i].transient_ap_base_b = norm;
                        }
                    }
                }
                break;
            }
            case k_paramLowCut: {
                // Master LP "Cutoff".  Stored 1-1999; effective 10-19990 Hz
                // (×10 scaling), internally capped at 16 kHz by set_coeffs.
                // High values = open (full spectrum), low values = dark.
                m_master_cutoff = (float)value * 10.0f;
                // Divide by 1000: UI stores 707-4000, filter needs 0.707-4.0
                float res_val = fmaxf(0.707f, 0.707f + ((float)m_params[k_paramResnc] - 71.0f) * 0.01f);
                state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
                break;
            }
            case k_paramGain: {
                float norm = fmaxf(0.0f, (float)value * 0.01f);
                state.master_drive = 1.0f + (norm * 20.0f);
                m_pending_drive = -1.0f;   // an explicit Gain turn wins
                break;
            }

            // resonator parameters
            case k_paramNzMix: {
                // Updated for the new 0-100 header.c range
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.01f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_decay_coeff = norm;
                }
                break;
            }

            case k_paramNzRes: {
                // Leaving this at the old 0-1000 scale
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.001f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_env.attack_rate = 0.9f - (norm * 0.8f);
                    // Slower decay so the noise actually injects energy into the tube
                    state.voices[i].exciter.noise_env.decay_rate = 0.0001f + ((1.0f - norm) * 0.005f);
                    // High-band click: very fast attack + faster decay.
                    state.voices[i].exciter.noise_env_hi.attack_rate = fminf(0.99f, (0.95f - (norm * 0.3f)));
                    state.voices[i].exciter.noise_env_hi.decay_rate = 0.003f + ((1.0f - norm) * 0.015f);
                }
                break;
            }
            case k_paramResnc: {
                // Stored ÷10 (71-400 = display 710-4000), step 10 on the encoder.
                // Q = 0.707 + (value-71)*0.01 → the shipped default (71) maps to
                // EXACTLY the old Q 0.707 (bit-identical), 400 → ~4.0.
                float res_val = fmaxf(0.707f, 0.707f + ((float)value - 71.0f) * 0.01f);
                state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
                break;
            }

            case k_paramNzFltr: {
                int mode = (int)fmaxf(0.0f, fminf(2.0f, (float)value));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_filter.mode = mode;
                }
                break;
            }

            case k_paramNzFltFrq: {
                // Stored ÷10 (2-2000 represents 20-20000 Hz). Multiply by 10 for real Hz.
                float freq = fmaxf(20.0f, fminf(20000.0f, (float)value * 10.0f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_filter.set_coeffs(freq, 0.707f, default_sample_rate);
                    // NOTE: noise_hi_lp_coeff (the body/sizzle split corner) is a
                    // per-preset constant from model_param_presets, NOT tied to NzFq.
                    // The old coupling (split corner = 2.2×NzFq) acted in REVERSE:
                    // raising the cutoff raised the subtraction corner, which
                    // REMOVED sizzle.  The user filter colours the noise via the
                    // SVF above; the split only routes body vs click envelopes.
                }
                break;
            }

            default:
                break;
        }

        // Dense-kernel presets: re-derive the kernel modifiers whenever any
        // mapped parameter changes.  Cheap scalar math here; mods that move
        // decay poles amortize inside ModalDrumKernel::Process().  Note is
        // NOT a mod — it rides each NoteOn and retunes a kettle there.
        // (k_paramProgram routes through LoadPreset, which configures the
        // kernel itself — skip it to avoid double work on preset switch.)
        if (index != k_paramProgram && kernel_preset_active() && m_drum_kernel.IsActive()) {
            RefreshKernelMods();
        }
    }

    // Show A or B label depending on the resonator selected via the Partls selector.
    // IMPORTANT: always index into arrays with the function's `value` argument —
    // never with stored state — so scrolling through values shows the correct label.
    inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
        static const char* const model_names_a[] = {
            "A:Strng", "A:Beam",  "A:Sqre", "A:Mbrn", "A:Plate",
            "A:Drmhd", "A:Mrmb",  "A:OpTb", "A:ClTb"
        };
        static const char* const model_names_b[] = {
            "B:Strng", "B:Beam",  "B:Sqre", "B:Mbrn", "B:Plate",
            "B:Drmhd", "B:Mrmb",  "B:OpTb", "B:ClTb"
        };
        static const char* const model_names_ab[] = {
            "AB:Strng", "AB:Beam",  "AB:Sqre", "AB:Mbrn", "AB:Plate",
            "AB:Drmhd", "AB:Mrmb",  "AB:OpTb", "AB:ClTb"
        };
        // Values 0-4: partial count labels (shown with A/B indicator).
        // Values 5, 6: resonator-select mode labels.
        static const char* const partial_names_a[]  = {"A:4",  "A:8",  "A:16",  "A:32",  "A:64"};
        static const char* const partial_names_b[]  = {"B:4",  "B:8",  "B:16",  "B:32",  "B:64"};
        static const char* const partial_names_ab[] = {"AB:4", "AB:8", "AB:16", "AB:32", "AB:64"};
        static const char* const nz_filter_names[]  = {"LP", "BP", "HP"};

        if (index == k_paramProgram) {
            // value IS the preset index being browsed — use it directly.
            return getPresetName((uint8_t)value);
        } else if (index == k_paramModel) {
            if (value >= 0 && value < 9)
                return m_is_resonator_a && m_is_resonator_b ? model_names_ab[value] :
                    m_is_resonator_a ? model_names_a[value] : model_names_b[value];
        } else if (index == k_paramPartls) {
            // On the cymbal family this knob is the resonator-bank DENSITY (the
            // ex-"Rsntrs" control), so show what it actually does there — the
            // partial-count / ResA-ResB labels below would be a lie: the dense
            // resonator engine has no shared modal bank to give partials to.
            if (kPresetEngine[m_preset_idx] == ENGINE_CYMBAL) {
                static char cr_buf[8];
                int d = (value < 0) ? 0 : ((value > 7) ? 7 : (int)value);
                snprintf(cr_buf, sizeof(cr_buf), "Rs%d%%", 25 + 5 * d);
                return cr_buf;
            }
            if (value == 5) return "ResA+B";
            if (value == 6) return "ResA";
            if (value == 7) return "ResB";
            if (value >= 0 && value < 5)
                return m_is_resonator_a && m_is_resonator_b ? partial_names_ab[value] :
                    m_is_resonator_a ? partial_names_a[value] : partial_names_b[value];
        } else if (index == k_paramNzFltr) {
            if (value >= 0 && value < 3) return nz_filter_names[value];
        } else if (index == k_paramMlltStif) {
            // Stored ÷100; show real ×100 value (0-5000, step 100).  HW: at the
            // old ÷10 step the neighbouring values were indistinguishable, so
            // the knob read as "too subtle" across its whole travel.
            static char ms_buf[8];
            snprintf(ms_buf, sizeof(ms_buf), "%d", (int)(value * 100));
            return ms_buf;
        } else if (index == k_paramDkay) {
            // Stored ÷10; show real ×10 value (0-2000)
            static char dk_buf[8];
            snprintf(dk_buf, sizeof(dk_buf), "%d", (int)(value * 10));
            return dk_buf;
        } else if (index == k_paramNzFltFrq) {
            static char nf_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                int32_t khz_i = hz / 1000;
                int32_t khz_d = (hz % 1000) / 100;
                snprintf(nf_buf, sizeof(nf_buf), "%d.%dkHz", khz_i, khz_d);
            } else {
                snprintf(nf_buf, sizeof(nf_buf), "%dHz", hz);
            }
            return nf_buf;
        } else if (index == k_paramLowCut) {
            static char lc_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                int32_t khz_i = hz / 1000;
                int32_t khz_d = (hz % 1000) / 100;
                snprintf(lc_buf, sizeof(lc_buf), "%d.%dkHz", khz_i, khz_d);
            } else {
                snprintf(lc_buf, sizeof(lc_buf), "%dHz", hz);
            }
            return lc_buf;
        } else if (index == k_paramResnc) {
            // Stored ÷10; show real ×10 value (710-4000), same as MlltStif/Dkay.
            static char rs_buf[8];
            snprintf(rs_buf, sizeof(rs_buf), "%d", (int)(value * 10));
            return rs_buf;
        // NOTE: no k_paramInharm branch.  Inharm was `type_strings` and
        // displayed value×10, which is exactly what broke it on hardware once
        // pass 36 made it bipolar — the OS routes a `type_strings` parameter
        // through here as an UNSIGNED selector, so -100 rendered as a huge
        // positive number.  It is `k_unit_param_type_none` since pass 41 and
        // the OS formats the signed value itself; this branch would never be
        // called again and is removed rather than left to rot.
        } else if (index == k_paramCymPoly) {
            // Show the EFFECTIVE polyphony for the current preset (HW: "Poly
            // always showed 2, not updated with actual polyphony").  When a
            // family cap trims the requested value, show both: "4(2)".
            static char pl_buf[8];
            int req = (value < 1) ? 1 : ((value > 4) ? 4 : (int)value);
            int eff = req;
            const EngineType pe = kPresetEngine[m_preset_idx];
            // ENGINE_KS is no longer forced to 1 (pass 41): a string preset now
            // stacks DIFFERENT notes and only reuses a slot when the same note
            // is replucked, so the Poly knob means what it says on Koto/GtrStr.
            if (kernel_preset_active())       eff = (req > 2) ? 2 : req;  // 2 kettles
            if (eff != req) snprintf(pl_buf, sizeof(pl_buf), "%d(%d)", req, eff);
            else            snprintf(pl_buf, sizeof(pl_buf), "%d", eff);
            return pl_buf;
        }

        // Unconditional failsafe to prevent OS screen crashes
        return "---";
    }

    // ==============================================================================
    // 4. Sequencer and MIDI Routing
    // ==============================================================================

    // Roll window: strokes closer together than this belong to ONE physical
    // roll (pressed roll / buzz roll).  Shared by BOTH the voice-fusion test in
    // NoteOn and the snare wire-continuity restore below it — keep them on one
    // constant so the two mechanisms can never disagree about what a roll is.
    static constexpr float kRollFuseSec = 0.080f;

    // A cymbal voice younger than this is off-limits as a steal candidate — see
    // the ENGINE_CYMBAL voice policy in NoteOn.  It covers more than the gong's
    // 0.25 s driver attack on purpose: magEnv is a 10 ms average of a dense
    // inharmonic wash, so within the first half-second it fluctuates enough to
    // rank a fresher (louder) voice below an older one.  Inside the window the
    // age ordering is the reliable signal, so the OLDEST voice is stolen; past
    // it the tails have separated and the faintest is genuinely the faintest.
    static constexpr float kCymStealProtectSec = 0.60f;

    // ── Cymbal CPU budget, in resonator-lane equivalents ───────────────────
    // HW (pass 29): "cymbal: additional hit leads to silence (audio crash)";
    // "gong: crackling then silence, possible CPU overload."  Pass 26 raised
    // the cymbal cap from 2 voices to 4 and replaced the voice-count guard
    // with a ceiling on the TOTAL resonator count, on the stated theory that
    // "bounding the aggregate is a STRONGER CPU guarantee than the voice
    // count".  That theory is wrong, and `cym_cpu_probe.cpp` measures by how
    // much: the FIXED per-voice cost of cymbal_process — pink noise, the two
    // driver one-poles, the PM block, the DC blocker, the magnitude envelope —
    // is worth ~124 resonator lanes on its own, i.e. 79 % of a default 32-lane
    // gong voice.  A resonator-only budget therefore bounded about a fifth of
    // the real cost and never once bound at the default Rsntrs (4 gong voices
    // ask for 128 lanes against a 240 ceiling), so pass 26's raise landed as a
    // near-doubling of the worst case.  Measured 4 voices = 3.7x one voice.
    //
    // The budget is now expressed in the same units as the cost: every active
    // cymbal voice charges kCymVoiceFixedLanes PLUS its resonators.
    static constexpr int kCymVoiceFixedLanes = 124;

    // The ceiling is the pre-pass-26 worst case — 2 voices at the largest bank
    // the Rsntrs knob can ask for (60 lanes) — because that is the only cymbal
    // CPU level this unit has field evidence for: it ran for 25 passes without
    // an audio crash, and the first report of one came after pass 26 doubled
    // it.  At the default Rsntrs this allows 2 stacked cymbal voices; a third
    // is only affordable if the banks are small enough to pay for it.
    static constexpr int kCymCostBudget = 2 * (kCymVoiceFixedLanes + 60);  // 368

    // Smallest bank that still reads as a cymbal rather than a chord.
    static constexpr int kCymMinResonators = 32;

    // Preset-change voice fade-out time constant.  6.9 tau reaches -60 dB, so
    // 1.5 ms tau = a ~10 ms fade: long enough to be click-free, short enough
    // that the outgoing preset does not audibly overlap the new one.
    static constexpr float kPresetFadeTauSec = 0.0015f;

    // ── Velocity knob ──────────────────────────────────────────────────────
    // How far a full "wham" may push a strike PAST the calibrated maximum.
    // Every preset is voiced at velocity 1.0, so without an over-range the top
    // of the knob would do literally nothing whenever the sequencer already
    // sends 127 — the exact "this knob is dead" class of report this unit keeps
    // collecting.  1.30 is a hit ~2.3 dB hotter with a correspondingly harder /
    // longer strike; the shaping terms downstream clamp at their calibrated
    // maxima (a stick cannot get harder than hard), so the over-range reads as
    // energy, not as a new timbre, and the master limiter still bounds output.
    static constexpr float kVelWhamMax = 1.30f;
    // Floor: a ghosted strike must stay a strike.  Below this the exciter is
    // quiet enough that the magnitude squelch (-80 dB) can retire the voice
    // before it speaks, which reads as a dropped note rather than a ghost.
    static constexpr float kVelGhostMin = 0.02f;

    // Velocity knob curve.  knob = 0 returns the incoming velocity EXACTLY
    // (not approximately — knob_exp2(0) is exactly 1.0), so the default knob
    // keeps all 40 presets and every render byte-identical.
    //   knob < 0 → ghost notes: down to x0.19 (velocity 127 lands at ~24)
    //   knob > 0 → wham:        up to x5.28, clamped at kVelWhamMax, so any
    //                           stroke above ~velocity 25 saturates to a
    //                           full-force hit no matter what the step stores.
    // Velocity is the unit's whole dynamics axis — it drives mallet stiffness,
    // wire mix/decay, boom weight, cymbal drive and the kernel impulse — so
    // biasing it here, once, gives all of that for free on every engine.
    inline float vel_bias_apply(float vel_norm) const {
        if (m_vel_bias == 0.0f) return vel_norm;
        const float scaled = vel_norm * knob_exp2(2.4f * m_vel_bias);
        const float ceil_v = (m_vel_bias > 0.0f)
                             ? (1.0f + (kVelWhamMax - 1.0f) * m_vel_bias)
                             : 1.0f;
        return fmaxf(kVelGhostMin, fminf(ceil_v, scaled));
    }

    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // The Velocity knob biases the strike ONCE, here, before either engine
        // path reads it (see vel_bias_apply).
        const float vel_norm = vel_bias_apply((float)velocity * 0.007874015f);
        // ── Dense modal-drum kernel path (Timpani/Taiko) ───────────────────
        // Two kettles: a repeat of a kettle's note retriggers that drum in
        // place (energy accumulates — a roll); a new note takes the other
        // kettle, retuned synchronously, so the first ring is untouched.
        // NoteOff is ignored — drums ring out under their measured decays.
        if (kernel_preset_active() && m_drum_kernel.IsActive()) {
            float ratio = exp2f(((float)note - m_drum_kernel.RootNote()) * 0.0833333333f);
            m_drum_kernel.Trigger(note, ratio, vel_norm);
            return;
        }
        // Global Poly cap: stacking engines round-robin within the first
        // m_poly slots so the knob truly sets (and the display truly shows)
        // the polyphony.  ENGINE_KS keeps the full-range increment — GateOff
        // pins it to one slot, and capping here would change which slot that
        // wrap lands on.
        {
            // ENGINE_KS now honours the Poly knob like everything else.  It
            // used to take the full NUM_VOICES range because GateOff pinned it
            // to a single slot; that pin is gone (see below and in GateOff).
            const uint8_t cap = m_poly;
            // ROLL FUSION (HW: "pressed rolls feel less smooth, Djambe
            // especially muddy").  Pass 23 let the drum families stack, which is
            // right for flams and roll TAILS but wrong for a PRESSED roll: at
            // 15-25 strokes/s every stroke took a fresh voice, so up to `cap`
            // whole bodies — each with its own crack/slap burst — piled up into
            // mud, and it also disabled the buzz-roll wire continuity below
            // (which can only fire when the slot being reused is the one still
            // rattling).  Fix: strokes inside kRollFuseSec on the SAME note
            // REUSE the last voice (pre-pass-23 behaviour, for genuine rolls);
            // anything slower still stacks.  Sustained engines are excluded —
            // for cymbal swells and marimba rolls the overlap IS the sound.
            const EngineType ne = kPresetEngine[m_preset_idx];
            const bool fusable = (ne == ENGINE_MEMBRANE || ne == ENGINE_SNARE ||
                                  ne == ENGINE_NOISE);
            const VoiceState& lastv = state.voices[state.next_voice_idx];
            // DeepBs fuses over a LONGER window than everything else (HW pass
            // 40: "not stacking hits properly, too muddy and undistinguished").
            // It measured as stacking perfectly — 1→2→3→4 voices on repeated
            // hits — and that is the problem, not the fix: its body is the
            // longest membrane in the unit (t60_1 = 1800 ms), so at any normal
            // sequencer rate four whole 1.8 s bodies overlap and the low end
            // turns to porridge.  A real bass drum cannot do that; the beater
            // damps the head it just struck.  So repeated strikes on the same
            // note keep ONE body and re-excite it (energy accumulates, attack
            // stays distinct) out to 300 ms instead of 80.  Other presets are
            // unaffected — kRollFuseSec is still the shared constant the
            // snare-wire continuity test keys on.
            const float fuse_sec = (m_preset_idx == k_Taiko2) ? 0.300f : kRollFuseSec;
            const bool fuse = fusable && lastv.is_active &&
                              lastv.current_note == note &&
                              (lastv.exciter.current_frame <
                               (uint32_t)(fuse_sec * default_sample_rate));
            // ENGINE_KS: one slot per STRING, not one per INSTRUMENT.
            // HW pass 41: "Koto: stacking notes feels unnatural."  KS was hard
            // mono — GateOff pinned every pluck to the same voice — so with
            // Dkay 200 each new note chopped the previous one off mid-ring.  A
            // koto has thirteen strings and they ring together; cutting them
            // is the unnatural part.
            //
            // The original rationale (avoid same-pitch beating between
            // overlapping plucks) only ever applied to the SAME note, so that
            // is exactly what is kept: replucking a string reuses its slot —
            // which is also what physically happens, the plectrum stops the
            // string before exciting it again — while a DIFFERENT note takes
            // its own voice and both ring on.  No time window here, unlike the
            // roll fusion above: a string is stopped by being replucked no
            // matter how long you waited.
            const bool ks_same_string = (ne == ENGINE_KS) && lastv.is_active &&
                                        lastv.current_note == note;
            if (!fuse && !ks_same_string)
                state.next_voice_idx = (uint8_t)((state.next_voice_idx + 1) % cap);
        }
        // ENGINE_CYMBAL voice policy: the cymbal cap (the Poly knob) bounds the
        // simultaneous cymbal voices.  Below the cap a silent slot is used; at
        // the cap a ringing voice must be stolen.
        //
        // HW: "multiple gong hits are not stacking correctly."  The old rule —
        // steal the smallest magEnv — inverted itself on every slow-swelling
        // cymbal.  magEnv is a ~10 ms one-pole of |out| that starts at 0, and a
        // gong's driver takes lowAttackSec = 0.25 s to open, so for the first
        // third of a second the NEWEST hit is by far the quietest voice in the
        // bank.  A second gong struck while the first was still blooming
        // therefore killed itself on the third strike, and repeated hits ping-
        // ponged between two slots instead of accumulating.  Two fixes:
        //   1. voices younger than kCymStealProtectSec are off-limits (they are
        //      still swelling, i.e. the loudest thing about to happen);
        //   2. only genuinely faded tails are candidates; if every voice is
        //      still young the OLDEST one goes, never the one just struck.
        if (kPresetEngine[m_preset_idx] == ENGINE_CYMBAL) {
            const int cap = (int)m_cym_poly;
            const uint32_t protect =
                (uint32_t)(kCymStealProtectSec * default_sample_rate);
            int active = 0, freeIdx = -1, faded = -1, oldest = -1;
            int busy_cost = 0;
            float wmag = 1e9f;
            uint32_t oldage = 0u;
            for (int i = 0; i < NUM_VOICES; ++i) {
                VoiceState& cv = state.voices[i];
                if (cv.is_active && cv.cymbal.active) {
                    ++active;
                    busy_cost += kCymVoiceFixedLanes + (int)cv.cymbal.resCount;
                    if (cv.cymbal.sampleIndex >= protect && cv.cymbal.magEnv < wmag) {
                        wmag = cv.cymbal.magEnv; faded = i;
                    }
                    if (oldest < 0 || cv.cymbal.sampleIndex >= oldage) {
                        oldage = cv.cymbal.sampleIndex; oldest = i;
                    }
                } else if (freeIdx < 0) {
                    freeIdx = i;
                }
            }
            // A new voice is only ADDED to the stack if the cheapest bank it
            // could be given still fits the cost budget; otherwise the strike
            // reuses a slot (steal) so the aggregate cost cannot grow.  This is
            // the guard the resonator-only budget could not provide: it charges
            // for the fixed cost that dominates a cymbal voice.
            const bool affordable =
                (busy_cost + kCymVoiceFixedLanes + kCymMinResonators) <= kCymCostBudget;
            if (active < cap && affordable && freeIdx >= 0)
                                                state.next_voice_idx = (uint8_t)freeIdx;
            else if (faded >= 0)                state.next_voice_idx = (uint8_t)faded;
            else if (oldest >= 0)               state.next_voice_idx = (uint8_t)oldest;
            else if (freeIdx >= 0)              state.next_voice_idx = (uint8_t)freeIdx;
        }
        VoiceState& v = state.voices[state.next_voice_idx];

        // Capture before is_active is overwritten: only a previously-active voice
        // has residual delay-line data that needs clearing on retrigger.
        // A fresh slot (never used since Reset()) is already zero — skip the work.
        const bool had_residual = v.is_active || v.is_releasing;

        // Buzz-roll continuity (snare family): on a fast retrigger (inside
        // kRollFuseSec — press rolls, buzz rolls) the wires are still physically
        // rattling when the next stroke lands.  Capture the wire resonator states
        // here, BEFORE PartialReset() zeroes them; the snare wire-restore block
        // below puts them back and skips the crack-burst phase so rolls read
        // as one continuous buzz instead of a machine-gun row of cracks.
        // Same window as the roll-fusion test above — that fusion is what keeps
        // this reachable, since it only fires on the slot that is still ringing.
        const bool wires_in_motion =
            (kPresetEngine[m_preset_idx] == ENGINE_SNARE) && v.is_active &&
            (v.exciter.current_frame < (uint32_t)(kRollFuseSec * default_sample_rate));
        // ENGINE_CYMBAL restrike continuity: a gong/crash that is still ringing
        // when its slot is re-struck (or stolen) must not have its resonator
        // bank zeroed — a struck gong keeps sounding through the next strike,
        // and the hard reset is what made stacked hits read as "the previous
        // one disappears".  Captured before PartialReset() clears the flag.
        const bool cym_still_ringing =
            (kPresetEngine[m_preset_idx] == ENGINE_CYMBAL) && v.cymbal.active;

        const float roll_z1  = v.exciter.snare_wire_z1,  roll_z2  = v.exciter.snare_wire_z2;
        const float roll_z1b = v.exciter.snare_wire_z1b, roll_z2b = v.exciter.snare_wire_z2b;
        const float roll_z1c = v.exciter.snare_wire_z1c, roll_z2c = v.exciter.snare_wire_z2c;

        // CRITICAL FIX 2: Ensure the voice actually turns on!
        v.is_active = true;
        v.is_releasing = false;
        // Latch the engine for this strike.  processBlock routes on this, not
        // on the live kPresetEngine[m_preset_idx], so a preset change mid-tail
        // can no longer hand a ringing voice to another engine's code.
        v.engine = (uint8_t)kPresetEngine[m_preset_idx];

        // PCM sample layering removed: it was never part of the approved
        // synthesized sounds, and dropping it freed the Bank/Sample GUI params
        // for the Poly / Velocity performance controls.
        v.exciter.sample_ptr = nullptr;
        v.exciter.sample_frames = 0;

        v.current_note = note;
        v.current_velocity = vel_norm;   // MIDI velocity x the Velocity knob
        // BrshSnr: shape the dynamics.  A brush stroke physically cannot
        // "crack" — even a hard pad hit is a soft, drawn-out sweep — so the
        // hard-hit ceiling stays capped (≈0.72 at full velocity).  HW: a soft
        // hit must have "almost no hit at all" — the previous linear 0.18+0.54v
        // put a mid velocity at ~0.37 (read as "medium").  Use a QUADRATIC
        // curve with a low floor so the whole lower half of the range stays a
        // genuinely quiet crawl and only accents build presence.
        if (m_preset_idx == k_BrushSnare) {
            float vv = v.current_velocity;
            v.current_velocity = 0.08f + 0.64f * vv * vv;
        }
        // --- 2D DRUMHEAD STRIKE PHYSICS ---
        // 1. Calculate the physical strike location once for the entire voice
        // FLOORED AT 0 — this is one of HitPos's two ABSOLUTE consumers, and it
        // must not follow the knob below centre (pass 39 made the range
        // bipolar).  hit_x is a physical distance from the centre of the head
        // and it is consumed through a MAGNITUDE (sqrtsum2acc below), so a
        // negative x would fold back onto its positive mirror: HitPos −50 would
        // render identically to +50 and the knob would stop being monotonic.
        // 0 already IS the physical extreme — struck dead centre — so there is
        // nothing below it to reach, exactly as with Inharm's KS allpass.
        float hit_x = fmaxf(0.0f, (float)m_params[k_paramHitPos] * 0.01f);
        float hit_y = (1.0f - v.current_velocity) * hit_x * 0.5f;

        // Use our fast-math approximation to find distance from center (0.0 to 1.0)
        float radius = sqrtsum2acc(hit_x, hit_y); //
        radius = fminf(1.0f, radius);

        // --- VELOCITY MODULATION ---
        // VlMllStf: harder hit → stiffer (brighter) mallet.
        // Override the global mallet_stiffness on this specific voice only,
        // so soft hits are round and hard hits are sharp without changing other voices.
        {
            float base_stiff = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
            float stif_mod   = (float)m_params[k_paramVlMllStf] * 0.01f; // -1.0 to +1.0
            // Add up to a 50% stiffness boost when striking at the absolute edge
            float rim_stiffness_boost = radius * 0.5f;
            // ── Knob travel is scaled into the REMAINING headroom, not added
            // into a clamp ───────────────────────────────────────────────────
            // This used to be `clamp(base + mod*vel + rim, 0.01, 1.0)`: linear
            // into a hard bound, which is the pass-37 defect (a mapping that
            // saturates partway through its own travel and silently does
            // nothing after that).  `base_stiff` is `MlltStif * 0.02` over a
            // 0..50 range, so it ALONE spans the entire legal stiffness — and
            // wherever a preset ships MlltStif near an end, VlMllStf ran out of
            // room almost immediately.  Measured dead spans (plateau_probe):
            // Marimba −100..−40, Timpani −100..−40 AND 80..100, Clap 40..100,
            // Handpan −100..−80 AND 60..100 — 20-60 % of the knob.
            //
            // Now the delta is scaled by whatever headroom is actually left on
            // the side it is moving toward, so it approaches the bound instead
            // of hitting it: full travel is live at every MlltStif setting, and
            // the result is monotonic and cannot leave [0.01, 1.0] by
            // construction (no clamp needed).
            //
            // ANCHORED at the shipped VlMllStf, so `d == 0` for every shipped
            // preset and `t0` reproduces the old clamped expression exactly —
            // all 41 renders stay byte-identical.  It has to be anchored rather
            // than pivoted at zero: 4 presets ship VlMllStf non-zero (20/40/60)
            // and pivoting at 0 would re-voice them.
            //
            // Honest limit: a preset shipping MlltStif = 50 has `t0` == 1.0 and
            // therefore NO upward headroom — the mallet is already as stiff as
            // the exciter allows, so VlMllStf up is legitimately inert there
            // (5 presets).  That is a real ceiling, not a mapping bug; the knob
            // to move first is MlltStif.
            // The delta is normalised by the travel actually LEFT on the knob
            // on that side (a preset shipping VlMllStf 60 has only 40 units of
            // up left, 160 of down), so full knob travel maps to full headroom
            // at full velocity whatever the preset ships.
            float t0 = fmaxf(0.01f, fminf(1.0f,
                base_stiff + m_snare_vlstf_ref * v.current_velocity + rim_stiffness_boost));
            float raw = stif_mod - m_snare_vlstf_ref;
            float d   = ((raw >= 0.0f) ? (raw / fmaxf(1e-6f, 1.0f - m_snare_vlstf_ref))
                                       : (raw / fmaxf(1e-6f, 1.0f + m_snare_vlstf_ref)))
                        * v.current_velocity;
            v.exciter.mallet_stiffness = (d >= 0.0f) ? (t0 + d * (1.0f - t0))
                                                     : (t0 + d * (t0 - 0.01f));
        }

        // VlMllRes: harder hit → faster noise attack (sharper transient).
        // Override the noise_env attack_rate on this voice so it responds to accents.
        {
            float base_nz     = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzRes] * 0.001f));
            float base_attack = 0.9f - (base_nz * 0.8f);
            float res_mod     = (float)m_params[k_paramVlMllRes] * 0.01f; // -1.0 to +1.0
            // Add up to a 10% speed boost to the attack rate for extreme rim hits
            float rim_snap_boost = radius * 0.1f;

            // Velocity depth widened 0.5→1.0 (HW: "effect too weak"); default
            // VlMllRes=0 keeps this a no-op so shipped presets are unchanged.
            v.exciter.noise_env.attack_rate = fmaxf(0.01f, fminf(0.99f,
                base_attack + (res_mod * v.current_velocity * 1.0f) + rim_snap_boost)); //
            // High-band burst should stay snappier than low-band burst.
            v.exciter.noise_env_hi.attack_rate = fmaxf(0.05f, fminf(0.99f,
                v.exciter.noise_env.attack_rate * 1.25f));
            // Snare-family physical staging: body thud first, then wire buzz.
            // AcSnare attack_rate=0.001: 21% noise at 5ms, 62% at 20ms, 91% at 50ms.
            // MarchSnare uses 0.012 (~90% in 4ms): the slow stage made the stick
            // click and the wire noise read as two separate events on hardware —
            // a marching snare's crack and buzz must land together.
            // BrshSnr keeps a slow ~30 ms swish build instead — a brush drags
            // across the head, it does not crack.
            if (m_preset_idx == k_AcSnare || m_preset_idx == k_MarchSnare ||
                m_preset_idx == k_RimShot) {
                // 6th HW pass: AcSnare too — the 0.001 staging still read as
                // "sprayed noise over the click"; crack and buzz must land together.
                float snare_attack = 0.012f;
                v.exciter.noise_env.attack_rate = snare_attack;
                v.exciter.noise_env_hi.attack_rate = snare_attack;
            } else if (m_preset_idx == k_BrushSnare) {
                // HW model: a brush is many small flexible straws sweeping the
                // head — the NOISE must dominate, moderate and continuous, not a
                // struck hit (modal body zeroed, like Shaker's "tok").  Velocity
                // sets the sweep speed, NOT a hit: soft = brush CRAWLING slowly
                // along the membrane (gentle ~30 ms swish-in); hard = a SWIFT
                // sweep (~8 ms) that lands with only a small tap.  So attack
                // speeds UP with velocity (the reverse of a struck drum).
                // Onset shape measured from the references (careful analysis):
                // a SOFT brush hit turns ON IMMEDIATELY at a moderate flat level
                // (RMS 43% at t=0, 80% by 8 ms, peak ~12 ms) then sits flat — it
                // is a steady wash, NOT a swell and NOT a transient (crest 5.6).
                // A HARD hit BUILDS more slowly to a later peak (~22 ms) then
                // decays sharply (crest 18).  So the attack SLOWS as velocity
                // rises — the reverse of a struck drum, and the reverse of the
                // previous code, which made soft a slow crescendo (peak ~56 ms,
                // heard as a hit) and hard a fast percussive onset (the "violent
                // hit").
                float bvq = fmaxf(0.0f, fminf(1.0f, (v.current_velocity - 0.08f) * 1.5625f));
                float brush_attack = 0.0060f - 0.0028f * bvq;
                v.exciter.noise_env.attack_rate = brush_attack;
                v.exciter.noise_env_hi.attack_rate = brush_attack;
            }
            // NOTE: the velocity-dependent wire-band coefficients are set AFTER
            // PartialReset() below — PartialReset restores the band defaults, so
            // anything written here would be clobbered before the first sample.
        }

        // --- THE PHYSICS OF PITCH ---
        // 1. O(1) Array Lookup for absolute baseline pitch
        float base_delay = g_tables.note_to_delay_length[note & 127];

        // 2. Structural Routing: each resonator uses its own model for inharmonic offset.
        // ResA: Membrane/Drumhead models use standard pitch (resA is always the root).
        v.resA.delay_length = base_delay;
        // Tube models (OpenTube=7, ClosedTube=8) use phase_mult=-1 (inverted feedback),
        // which doubles the resonance period to T=2N, halving the resonant frequency.
        // Halve the delay here to compensate so the pitch matches the MIDI note.
        if (m_model_a == k_OpenTube || m_model_a == k_ClosedTube) {
            v.resA.delay_length *= 0.5f;
        }
        // ResB: its own model (m_model_b) determines whether it tracks an irrational offset.
        if (m_model_b == k_Membrane || m_model_b == k_Drumhead) {
            // Membrane / Drumhead Logic:
            // A circular membrane's overtone ratios are determined by the zeros of the
            // Bessel function J_mn.  The dominant second mode (1,1) has ratio ≈ 1.5926
            // relative to the fundamental, so ResB should be at 1/1.5926 ≈ 0.628× the
            // fundamental delay.  The old value of 0.68 (ratio 1.47) was not a Bessel
            // zero and produced an off-character "wrong" shimmer.
            // --- 2D DRUMHEAD STRIKE PHYSICS ---
            // 4. Interpolate between Bessel modes based on strike radius
            // Center (r=0): Mode (1,1) -> ratio ~ 0.628
            // Edge   (r=1): Mode (2,1) -> ratio ~ 0.466
            const float mode_1_1 = 0.628f;
            const float mode_2_1 = 0.466f;
            float dynamic_ratio = mode_1_1 + radius * (mode_2_1 - mode_1_1);

            v.resB.delay_length = base_delay * dynamic_ratio;
        } else {
            // Standard matched resonators (Strings, Tubes, Bars)
            v.resB.delay_length = base_delay;
        }
        if (m_model_b == k_OpenTube || m_model_b == k_ClosedTube) {
            v.resB.delay_length *= 0.5f;
        }

        // Micro-detune ResB by ~5 cents to break perfect mathematical beating.
        // Two resonators at identical delay lengths create digitally-precise
        // normal-mode splitting with metronomic amplitude modulation.  A real
        // instrument always has slight manufacturing asymmetry between modes.
        // 5 cents ≈ 0.3% — audible as warmth/chorus, not as out-of-tune.
        v.resB.delay_length *= 1.003f;
        // --- PITCH COMPENSATION FOR LOOP FILTER GROUP DELAY ---
        // Both the 1-pole LP and the allpass extend the effective loop period by
        // their group delay, making the pitch flat.  Subtract the combined group
        // delay from the nominal delay line length so the loop oscillates at f₀.
        //
        // DC-limit approximations (valid for all MIDI notes at 48 kHz, ω₀ ≪ 1):
        //   LP  H(z) = α/(1-pa·z⁻¹),  pole at pa=1-α  →  τ_LP = pa/(1-pa)
        //   AP  H(z) = (c+z⁻¹)/(1+c·z⁻¹)              →  τ_AP = (1-c)/(1+c)
        //
        // Derivation (LP): phase φ = -arctan(pa·sinω/(1-pa·cosω))
        //   τ = -dφ/dω = pa·(cosω-pa)/(1-2pa·cosω+pa²)
        //   At DC: pa·(1-pa)/(1-pa)² = pa/(1-pa).
        //   Sanity: pa=0 (α=1, passthrough) → τ=0; pa→1 (dark) → τ→∞.  Both ✓
        //
        // AP derivation: for H(z) = (c + z⁻¹) / (1 + c·z⁻¹):
        //   Phase = arg(c + e^{-jω}) - arg(1 + c·e^{-jω})
        //   τ = -dPhase/dω = (1-c²)/(1+c²+2c·cosω)
        //   At DC (ω=0): τ_AP = (1-c²)/(1+c)² = (1-c)/(1+c).
        //   At c=0: τ=1 (pure z⁻¹ delay). ✓
        //   NOTE: The incorrect formula (1+c)/(1-c) over-compensates, making pitch sharp.
        //   That formula applies to H(z) = (-c + z⁻¹)/(1 - c·z⁻¹), which has the
        //   opposite dispersion direction and is NOT what this code implements.
        {
            // ResA
            float pa = 1.0f - v.resA.lowpass_coeff;          // LP pole
            float ca = v.resA.ap_coeff;                      // AP coefficient
            float lp_del_A = pa / (1.0f - pa);               // τ_LP: pa/(1-pa)
            if (m_preset_idx == k_AcousticTom) lp_del_A *= 2.0f;
            float ap_del_A = (1.0f - ca) / (1.0f + ca);      // τ_AP: (1-c)/(1+c) ≤ 1
            v.resA.delay_length = fmaxf(2.0f, v.resA.delay_length - lp_del_A - ap_del_A);

            // ResB
            float pb = 1.0f - v.resB.lowpass_coeff;
            float cb = v.resB.ap_coeff;
            float lp_del_B = pb / (1.0f - pb);
            if (m_preset_idx == k_AcousticTom) lp_del_B *= 2.0f;
            float ap_del_B = (1.0f - cb) / (1.0f + cb);
            v.resB.delay_length = fmaxf(2.0f, v.resB.delay_length - lp_del_B - ap_del_B);
        }

        // Store pre-bend lengths so PitchBend() can always re-derive from the root pitch.
        // Then apply any bend that was already active when this note was struck.
        v.base_delay_A = v.resA.delay_length;
        v.base_delay_B = v.resB.delay_length;
        apply_pitch_bend_to_voice(v);


        // Reset the magnitude-envelope squelch tracker so residual energy from the
        // previous note on this voice slot doesn't prematurely kill the new note's attack.
        // Clear waveguide delay line, LP state, and write pointer.
        //
        // After write_ptr is reset to 0, the read position starts at
        // (0 - delay_length) mod DELAY_BUFFER_SIZE ≈ (DELAY_BUFFER_SIZE - delay_length).
        // The read pointer advances with the write pointer.  At sample delay_length,
        // the read pointer reaches position 0, which was just written by this note —
        // from that point forward every read is from freshly-computed data.
        // Only the tail window [DELAY_BUFFER_SIZE - ceil(delay_length) - 1 … end] is ever
        // read before new data covers it; clearing that window is 10-37× cheaper than
        // zeroing the full 8 KB buffer.
        //
        // Skip entirely on a fresh (never-triggered) slot: Reset() already zeroed it.
        v.PartialReset();

        // Seed noise from current delay buffer state so each note trigger has unique
        // stochastic character. Prevents repetitive artifacts on rapid re-triggering.
        // User suggestion: delay line contents provide entropy without timestamps.
        {
            uint32_t inject = 0;
            for (int s = 0; s < 8; ++s) {
                uint32_t w = *reinterpret_cast<const uint32_t*>(
                    &v.resA.buffer[(v.resA.write_ptr + (uint32_t)(s * 97)) & DELAY_MASK]);
                inject ^= w ^ (inject * 1664525UL + 1013904223UL);
            }
            if (inject != 0) v.exciter.noise_gen.seed ^= inject;
        }

        // Metallic rod bypass: Triangle, BellTree, Cowbell use pure-gain loop,
        // not LP-filtered sustain. Source: Rossing & Fletcher, "Principles of
        // Vibration and Sound", 2nd ed., ch. 3 — rod/plate modes decay without
        // LP-style spectral darkening.
        bool use_lp_bypass = (m_preset_idx == k_Triangle || m_preset_idx == k_BellTree || m_preset_idx == k_Cowbell);
        v.resA.bypass_loop_lp = use_lp_bypass;
        v.resB.bypass_loop_lp = use_lp_bypass;
        // AcousticTom: second cascaded loss pole (darker skin).  A flag on the
        // waveguide keeps the per-sample loop free of preset-index compares.
        const bool use_double_lp = (m_preset_idx == k_AcousticTom);
        v.resA.double_lp = use_double_lp;
        v.resB.double_lp = use_double_lp;

        if (had_residual) {
            auto clear_tail = [](float* buf, float delay_len) {
                uint32_t len = (uint32_t)ceilf(delay_len) + 2;  // +2: frac interp safety
                if (len >= DELAY_BUFFER_SIZE) {
                    // Delay longer than buffer (very low notes): clear everything.
                    memset(buf, 0, DELAY_BUFFER_SIZE * sizeof(float));
                } else {
                    memset(&buf[DELAY_BUFFER_SIZE - len], 0, len * sizeof(float));
                }
            };
            clear_tail(v.resA.buffer, v.resA.delay_length);
            clear_tail(v.resB.buffer, v.resB.delay_length);
        }

        // Clear noise SVF delay states so rapid re-triggering doesn't produce
        // a click from residual filter memory.  set_coeffs() (called once from
        // setParameter) only updates f/q and never zeroes lp/bp/hp.
        v.exciter.noise_filter.lp = 0.0f;
        v.exciter.noise_filter.bp = 0.0f;
        v.exciter.noise_filter.hp = 0.0f;

        // Trigger the envelopes when a note hits
        v.exciter.noise_env.trigger();
        v.exciter.noise_env_hi.trigger();
        // Master envelope gate.  decay_rate and release_rate set by setParameter(k_paramDkay).
        //
        // ENGINE_KS / ENGINE_NOISE: auto-decay from 1.0 → 0.0 at decay_rate while gate
        // is held, so KS strings and noise bursts decay naturally without modal T60 limits.
        // NoteOff switches to the faster release_rate.
        //
        // All other engines (BAR, MEMBRANE, SNARE, PLATE): sustain at 1.0 while gate
        // is held — do NOT auto-decay.  Modal T60 values determine ring duration; the
        // master_env must not cut the ring short.  Voice deactivates when modal ring dies
        // (mag_env < kSquelchThreshold) rather than when master_env reaches ENV_IDLE.
        //
        // Direct assignment avoids the trigger() + process() pattern that relied on
        // value >= 0.99f after one multiply-add.  ARM -ffast-math may emit an FMA whose
        // rounding leaves value fractionally below 0.99f.
        {
            const EngineType ne = kPresetEngine[m_preset_idx];
            // ENGINE_KS: auto-decay while gate held (Dkay controls master_env duration).
            // All other engines (modal + NOISE): master_env holds at 1.0.
            //   - Modal engines: modal T60 controls ring duration; master_env must not cut it.
            //   - NOISE engines: noise_env (NzRes) controls duration; Rel controls the tail.
            //     master_env.release() is NOT called on NoteOff for NOISE — noise_env
            //     release handles fade so the "tschaa" tail is audible.
            v.exciter.master_env.sustain_level = (ne == ENGINE_KS) ? 0.0f : 1.0f;
            v.exciter.master_env.value = 1.0f;
            v.exciter.master_env.state = ENV_DECAY;
        }

        // Stage-1 transient complexity: short coefficient modulation window.
        // Deterministic per-hit micro-randomization from note/voice/velocity.
        // Jitter depth follows the BIASED strike (vel_norm above), clamped back
        // into [0,1]: the modulation window is a transient-shaping depth, not
        // an energy, so a wham strike must not widen it past its tuned maximum.
        const float vel_jit = fmaxf(0.0f, fminf(1.0f, vel_norm));
        uint32_t seed = (uint32_t)note * 1103515245u
                      ^ (uint32_t)state.next_voice_idx * 12345u
                      ^ (uint32_t)velocity * 2654435761u;
        float r = ((float)((seed >> 8) & 0xFFFFu) * 3.05180437934e-5f) - 1.0f; // [-1, +1] - approx 1 / 32767.5f
        v.transient_frames_total = (uint32_t)(default_sample_rate * 0.035f); // 35 ms
        v.transient_frames_left = v.transient_frames_total;
        v.transient_inv_total = (v.transient_frames_total > 0) ? (1.0f / (float)v.transient_frames_total) : 0.0f;
        v.transient_lp_jitter = fmaxf(-0.08f, fminf(0.08f, (0.05f * vel_jit) + (0.02f * r)));
        v.transient_ap_jitter = fmaxf(-0.03f, fminf(0.03f, (0.015f * vel_jit) - (0.01f * r)));

        // Stage-1 model-specific transient presets.
        // Simple profile map: percussion gets longer/stronger transient modulation.
        uint8_t model_profile = m_model_a;
        bool percussion_model = (model_profile == k_SquarePlate || model_profile == k_Membrane ||
                                 model_profile == k_Plate || model_profile == k_Drumhead ||
                                 model_profile == k_MarimbaBar);
        bool tube_model = (model_profile == k_OpenTube || model_profile == k_ClosedTube);
        if (percussion_model) {
            v.transient_frames_total = (uint32_t)(default_sample_rate * 0.045f); // 45 ms
            v.transient_frames_left = v.transient_frames_total;
            v.transient_inv_total = (v.transient_frames_total > 0) ? (1.0f / (float)v.transient_frames_total) : 0.0f;
            v.transient_lp_jitter = fmaxf(-0.10f, fminf(0.10f, v.transient_lp_jitter * 1.25f));
            v.transient_ap_jitter = fmaxf(-0.04f, fminf(0.04f, v.transient_ap_jitter * 1.20f));
            v.exciter.noise_band_mix = 0.70f;
        } else if (tube_model) {
            v.transient_frames_total = (uint32_t)(default_sample_rate * 0.020f); // 20 ms
            v.transient_frames_left = v.transient_frames_total;
            v.transient_inv_total = (v.transient_frames_total > 0) ? (1.0f / (float)v.transient_frames_total) : 0.0f;
            v.transient_lp_jitter *= 0.6f;
            v.transient_ap_jitter *= 0.6f;
            v.exciter.noise_band_mix = 0.35f;
        } else {
            v.exciter.noise_band_mix = 0.50f;
        }
        // Per-preset noise band balance takes precedence over the model-profile
        // defaults above.  Without this, every NoteOn clobbered the calibrated
        // k_noise_band_mix from model_param_presets (e.g. HHat-C shipped 0.86 but
        // played at 0.70, which kept its dedicated hat_filter path — gated on
        // mix > 0.80 — permanently disabled).  A value of 0 in the table means
        // "no preset opinion": the model-profile default stands.
        {
            float preset_band_mix = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_noise_band_mix);
            if (preset_band_mix > 0.001f) {
                // Clamp at the write site: process_exciter reads this per sample
                // and relies on it staying inside [0, 1].
                v.exciter.noise_band_mix = fminf(1.0f, preset_band_mix);
            }
        }
        // Bullet-1 step 3 start: dedicated metallic HF exciter emphasis.
        // For cymbal/gong/open-hat, keep a stronger independent high-band path
        // so upper shimmer is less tied to the KS loop loss behavior.
        if (m_preset_idx == k_Cymbal || m_preset_idx == k_Gong || m_preset_idx == k_HiHatOpen) {
          // noise_band_mix comes from the preset table (restored above) — the old
          // fmax(mix, 0.92) floor here silently overrode Gong's calibrated 0.50.
          v.exciter.noise_hi_lp_coeff = fmaxf(v.exciter.noise_hi_lp_coeff, 0.90f);
          // sustain the high-band burst slightly longer than default metallic click
          v.exciter.noise_env_hi.decay_rate = fmaxf(0.002f, v.exciter.noise_env_hi.decay_rate * 0.75f);
          // Decoupled high-band branch (post-resonator) to preserve shimmer.
          v.hf_branch_env = 1.0f;
          v.hf_branch_decay = 0.9992f;
          v.hf_branch_mix = 0.30f;
          v.hf_branch_lp = 0.0f;
        }
        // Triangle-specific sustain fix:
        // Keep loop HF loss close to DC loss so upper partials do not collapse
        // in the first ~50 ms (common KS 1-pole LP failure mode for triangles).
        if (m_preset_idx == k_Triangle) {
            v.resA.loss_g_hf = fmaxf(v.resA.loss_g_hf, 0.96f);
            v.resB.loss_g_hf = fmaxf(v.resB.loss_g_hf, 0.96f);
            v.resA.lowpass_coeff = fmaxf(v.resA.lowpass_coeff, 0.93f);
            v.resB.lowpass_coeff = fmaxf(v.resB.lowpass_coeff, 0.93f);
            v.transient_lp_base_a = v.resA.lowpass_coeff;
            v.transient_lp_base_b = v.resB.lowpass_coeff;
            v.transient_lp_jitter = fminf(v.transient_lp_jitter, 0.01f);
            // Missing >8 kHz metal sheen (ref centroid 8.5 kHz vs render 2.4k):
            // push the post-resonator high band, not the KS loop (the KS attack
            // floor is documented).  Sustained hi noise feeds the branch.
            v.hf_branch_mix = 0.35f;
            v.hf_branch_decay = 0.9996f;
            v.exciter.noise_env_hi.decay_rate = 0.000080f;  // keep sheen alive ~1.8s
        }
        // Bullet-1 step 2 start: metallic low-loss loop mode.
        // Keep upper partials alive in metallic families by reducing per-cycle LP loss.
        if (m_preset_idx == k_Cymbal || m_preset_idx == k_Gong ||
            m_preset_idx == k_HiHatOpen || m_preset_idx == k_Ride ||
            m_preset_idx == k_RideBell || m_preset_idx == k_Triangle ||
            m_preset_idx == k_BellTree || m_preset_idx == k_Cowbell) {
            v.resA.loss_g_hf = fmaxf(v.resA.loss_g_hf, 0.95f);
            v.resB.loss_g_hf = fmaxf(v.resB.loss_g_hf, 0.95f);
            v.resA.lowpass_coeff = fmaxf(v.resA.lowpass_coeff, 0.91f);
            v.resB.lowpass_coeff = fmaxf(v.resB.lowpass_coeff, 0.91f);
            v.transient_lp_base_a = v.resA.lowpass_coeff;
            v.transient_lp_base_b = v.resB.lowpass_coeff;
            // Do not let transient LP jitter darken metallic attacks.
            v.transient_lp_jitter = fminf(v.transient_lp_jitter, 0.008f);
            v.hf_branch_env = fmaxf(v.hf_branch_env, 1.0f);
            v.hf_branch_decay = fmaxf(v.hf_branch_decay, 0.9992f);
            v.hf_branch_mix = fmaxf(v.hf_branch_mix, 0.22f);
        }
        // Cymbal: extended noise envelopes so shimmer sustains through the full 4-second render.
        // KS at note=90 (1480Hz) with Dkay=200 gives T60~4.6s and harmonics at 1480/2960/4440/5920Hz.
        if (m_preset_idx == k_Cymbal) {
            v.exciter.noise_env_hi.decay_rate = 0.000020f;  // keep bright hi band alive through the tail
            v.exciter.noise_env.decay_rate    = 0.000020f;
            v.hf_branch_decay = 0.99990f;                   // long upper shimmer
        }
        // Gong: without this override, NzRs=860 gives noise_env_hi T60~4ms (too short).
        // The hf_branch_env runs for ~720ms but needs a live noise signal to modulate.
        // Setting T60≈1.4s / 2.9s gives the hf_branch sustained shimmer to work with.
        if (m_preset_idx == k_Gong) {
            v.exciter.noise_env_hi.decay_rate = 0.000100f; // T60≈1.44s HF shimmer
            v.exciter.noise_env.decay_rate    = 0.000050f; // T60≈2.9s wash
        }
        // HHat-O: low band is already long (NzRs=1000→T60≈1.44s) but noise_env_hi
        // decays in ~36ms after the metallic block, depriving the hf_branch of signal.
        if (m_preset_idx == k_HiHatOpen) {
            v.exciter.noise_env_hi.decay_rate = 0.000100f; // T60≈1.44s — matches low band
        }
        // PartialReset() clears boom helper fields; restore them from preset params
        // on every NoteOn, without preset-specific branching.
        const ProgramIndex preset = static_cast<ProgramIndex>(m_preset_idx);
        // hat_filter per-preset: repurpose snare_wire_z1/z2/a2 cols (safe when snare_wire_mix=0)
        // col k_snare_wire_z1 = mode (0=LP,1=BP,2=HP), col k_snare_wire_z2 = freq Hz, col k_snare_wire_a2 = Q
        if (v.exciter.use_hat_filter) {
            float hf_freq = preset_param(preset, k_snare_wire_z2);
            if (hf_freq > 0.0f) {
                int hf_mode = (int)roundf(preset_param(preset, k_snare_wire_z1));
                float hf_q  = preset_param(preset, k_snare_wire_a2);
                if (hf_q < 0.1f) hf_q = 1.1f;
                v.exciter.hat_filter.mode = hf_mode;
                v.exciter.hat_filter.set_coeffs(hf_freq, hf_q, default_sample_rate);
            }
        }
        v.boom_inc = preset_param(preset, k_boom_inc);
        v.boom_env = preset_param(preset, k_boom_env);
        v.boom_decay = preset_param(preset, k_boom_decay);
        v.boom_mix = preset_param(preset, k_boom_mix);
        v.boom_attack_env = preset_param(preset, k_boom_attack_env);
        v.boom_attack_inc = preset_param(preset, k_boom_attack_inc);
        // ── Pitch-envelope restore (MUST be after PartialReset) ──────────────
        // Same defect as the snare-wire bug below: PartialReset() zeroes
        // pitch_env / _decay / _amt, only LoadPreset ever wrote them, and the
        // boom_* restores above never covered them — so the FIRST NoteOn
        // cleared them permanently and every pitch_env-driven sweep in the unit
        // was dead.  808Sub measured flat at 45 Hz for the whole hit against
        // its documented 160 → 45 Hz dive, and pass 22's Inharm → dive-depth
        // knob was scaling a zero.  (KickDrum's sweep works only because it is
        // written against boom_env, which IS restored above.)
        //
        // Deliberately NOT a blanket restore.  Five presets carry non-zero
        // pitch_env data and four of them are enabled here; **KickDrum (amt 9)
        // is left out and loses nothing by it**, because its sweep formula is
        // written against `boom_env`, not `pitch_env` — restoring the field
        // would not change a sample of its output.  Adding a preset here
        // CHANGES ITS SOUND: that is a voicing decision, not a bug fix.
        // AcSnare and Koto were held back in pass 35 as HW-approved presets
        // nobody had asked about, and added in pass 38 on an explicit request
        // to hear the difference.
        if (m_preset_idx == k_808Sub || m_preset_idx == k_RackTom ||
            m_preset_idx == k_AcSnare || m_preset_idx == k_Koto) {
            v.pitch_env       = preset_param(preset, k_pitch_env);
            v.pitch_env_decay = preset_param(preset, k_pitch_env_decay);
            v.pitch_env_amt   = preset_param(preset, k_pitch_env_amt);
        }
        // ── Snare-wire exciter restore (MUST be after PartialReset) ──────────
        // PartialReset() zeroes snare_wire_mix and restores default band
        // coefficients on every hit; LoadPreset only wrote them once at preset-
        // load time.  Without this per-NoteOn restore the entire 3-band wire
        // rattle was dead on every live hit — the snare family played body +
        // plain noise only.  Restored for ENGINE_SNARE presets only: other
        // presets with a non-zero table mix (e.g. KickDrum 0.03) were HW-
        // approved with the wire silent, so their live behaviour is kept.
        if (m_preset_idx == k_AcSnare || m_preset_idx == k_MarchSnare ||
            m_preset_idx == k_BrushSnare || m_preset_idx == k_RimShot) {
            v.exciter.snare_wire_mix    = fmaxf(0.0f, fminf(1.0f, preset_param(preset, k_snare_wire_mix)));
            v.exciter.wire_onset_env    = preset_param(preset, k_wire_onset_env);
            v.exciter.wire_onset_attack = preset_param(preset, k_wire_onset_attack);
            if (wires_in_motion) {
                // Fast retrigger: wires already rattling — restore their motion
                // and skip the crack burst (onset treated as complete).
                v.exciter.wire_onset_env = 1.0f;
                v.exciter.snare_wire_z1  = roll_z1;  v.exciter.snare_wire_z2  = roll_z2;
                v.exciter.snare_wire_z1b = roll_z1b; v.exciter.snare_wire_z2b = roll_z2b;
                v.exciter.snare_wire_z1c = roll_z1c; v.exciter.snare_wire_z2c = roll_z2c;
            }
            const float vq = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
            // ── Snare param-design knobs (REFERENCE-ANCHORED) ────────────────
            // The wire BUZZ (not the ~10 % modal body) is a snare's defining
            // voice, so the exciter/resonator knobs that were inaudible on the
            // snare family are wired to the WIRE here.  Each mapping is a DELTA
            // from the preset's shipped knob value, so at the shipped values
            // every term below is a no-op → shipped presets render bit-identical
            // and only knob MOVEMENT bites.  Curves are deliberately strong (the
            // old modal-body mappings gave "no audible difference" on snares):
            //   Rel      → buzz TAIL length  (Rel was DEAD — snare skips the
            //              noise-env release — so it is free real estate)
            //   MlltRes  → buzz AMOUNT       (wire vs plain filtered noise)
            //   MlltStif → buzz BRIGHTNESS   (wire band centre frequencies)
            //   VlMllStf → buzz TIGHTNESS/Q  (wire pole radius: loose↔zingy)
            //   VlMllRes → crack / SNAP      (initial broadband crack burst;
            //              VlMllRes was DEAD — its attack target is overridden)
            const float sn_rel_n = fmaxf(0.0f,  fminf(1.0f, (float)m_params[k_paramRel]      * 0.05f));
            const float sn_mr_n  = fmaxf(0.0f,  fminf(1.0f, (float)m_params[k_paramMlltRes]  * 0.001f));
            const float sn_st_n  = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
            const float sn_vs_n  = (float)m_params[k_paramVlMllStf] * 0.01f;
            const float sn_vr_n  = (float)m_params[k_paramVlMllRes] * 0.01f;
            // Depth pass (July 2026, HW: "from subtle to live"): every coefficient
            // here was widened and its clamp opened.  All stay anchored at Δ=0, so
            // the four shipped snares are untouched and only the travel grew.
            // Dkay joins Rel on the buzz tail, but ONLY on BrshSnr (HW pass 41:
            // "BrshSnre: dkay parameter seems not have effect").  It is not
            // "subtle" there, it was mathematically inaudible: Dkay scales the
            // modal body's T60, and BrshSnr USED TO ship k_modal_mix = 0.0, so
            // the bank Dkay controls was mixed in at ZERO.  There was nothing
            // for the knob to act on.  (Pass 44 gave the preset a real head at
            // k_modal_mix 0.20, so Dkay now moves the body as well; the two
            // roles agree — Dkay is the coarse decay of the whole voice — so
            // this mapping stays.)  The brush's main voice is the noise swish, so
            // Dkay becomes its COARSE tail control with Rel staying the fine
            // trim — the same coarse/fine split Dkay and Rel already have on the
            // modal engines.  Scoped to this one preset because the other three
            // snares do have an audible body, and pass 23 deliberately kept
            // Dkay off Rel's territory there.
            const float sn_dk_n = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
            const float sn_dkay_mult = (m_preset_idx == k_BrushSnare)
                                     ? knob_exp2(-3.0f * (sn_dk_n - m_modal_dkay_ref))
                                     : 1.0f;
            const float sn_decay_mult = knob_exp2(-4.0f * (sn_rel_n - m_modal_rel_ref)) * sn_dkay_mult; // long Rel/Dkay = long buzz
            const float sn_mix_mult   = knob_exp2( 3.0f * (sn_mr_n  - m_modal_mltres_ref)); // ±~8× buzz mix
            const float sn_bright     = fmaxf(0.33f, fminf(3.0f, knob_exp2(1.6f * (sn_st_n - m_modal_stiff_ref)))); // ±1.6 oct
            const float sn_tight      = fmaxf(-0.12f, fminf(0.12f, (sn_vs_n - m_snare_vlstf_ref) * 0.12f));     // pole-radius shift
            v.exciter.snare_crack_gain = fmaxf(0.0f, fminf(9.0f, knob_exp2(4.0f * (sn_vr_n - m_snare_vlres_ref)))); // ±snap
            // TubRad → snare BODY depth/tone (REFERENCE-ANCHORED).  On the snare
            // family Dkay/TubRad only touched the quiet modal body, so TubRad was
            // "little effect".  Give it a distinct, audible axis: shift ONLY the
            // low wire band (Band A, the drum's body tone) — a bigger shell drops
            // it for a deeper "thunk" under the buzz, a smaller shell lifts it for
            // a tighter piccolo body.  MlltStif still moves ALL three bands
            // together (overall brightness); this moves the body band alone, so
            // the two knobs are independent.
            //
            // ASYMMETRIC on purpose (HW: "TubRad body-depth sounds a bit
            // toy-ish at lower values").  Deepening is a shell getting bigger —
            // there is no limit to how deep that reads, so DOWN keeps the full
            // -1.3 oct range.  Thinning is not symmetric: pushing Band A far
            // ABOVE the shipped body pitch stops reading as "shallow piccolo"
            // and starts reading as a toy/boxy snare, and on a preset shipping
            // TubRad=20 the old curve reached ×2.46 (≈ +1.3 oct) at the knob
            // floor.  So UP runs at half rate and caps at ×1.15 (≈ +2.4
            // semitones) — audibly tighter, still a snare.  Δ=0 hits the first
            // branch, where knob_exp2(0) is exactly 1.0 → shipped presets
            // byte-identical.
            const float sn_tr_n  = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
            const float sn_tr_d  = sn_tr_n - m_modal_tubrad_ref;
            const float sn_body  = (sn_tr_d >= 0.0f)
                                 ? fmaxf(0.28f, knob_exp2(-1.9f * sn_tr_d))
                                 : fminf(1.15f, knob_exp2(-0.5f * sn_tr_d));
            // Velocity → buzz character: soft hits (ghost notes) are mostly head
            // tone with a short, loose rattle; hard hits press the wires into the
            // head for a tighter, brighter, longer buzz.  Anchored at vq=1 so a
            // full-velocity hit plays the calibrated table values unchanged.
            if (m_preset_idx == k_BrushSnare) {
                // Brush wires rattle differently from a struck snare: the rattle
                // is a SMALL detail that must stay audible and SUSTAIN even on
                // soft "crawl" hits (soft ref sustains ~28% of peak with a
                // ~10 Hz flutter).  So DON'T fade the wire mix on soft hits
                // (a touch MORE if anything), and hold a slow buzz decay (soft
                // slightly slower) so the small rattle rings on rather than dying.
                v.exciter.snare_wire_mix = 0.20f + 0.06f * (1.0f - vq);
                float nz_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzRes] * 0.001f));
                float base_decay = 0.0001f + ((1.0f - nz_norm) * 0.005f);
                v.exciter.noise_env.decay_rate = base_decay * (0.80f + 0.20f * vq);
            } else {
                v.exciter.snare_wire_mix *= (0.60f + 0.40f * vq);
                // Soft hits also die sooner: scale the natural buzz decay up to
                // ~2× faster at zero velocity (×1.0 at full velocity).  Recomputed
                // from the NzRs knob value — decay_rate is shared voice state, so
                // multiplying it in place would compound across hits.
                float nz_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzRes] * 0.001f));
                float base_decay = 0.0001f + ((1.0f - nz_norm) * 0.005f);
                v.exciter.noise_env.decay_rate = base_decay * (2.0f - vq);
            }
            // Apply the reference-anchored knob deltas to the amount + tail the
            // velocity branches just set (no-ops at the shipped knob values).
            v.exciter.snare_wire_mix = fmaxf(0.0f, fminf(1.0f, v.exciter.snare_wire_mix * sn_mix_mult));
            v.exciter.noise_env.decay_rate *= sn_decay_mult;
            // Band A: velocity-controlled centre (~2.8 kHz AcSnare, ~3.5 kHz
            // MrchSnr, ~2.2 kHz BrshSnr — brushes read lower/softer).
            // sn_bright (MlltStif) shifts the centre; sn_tight (VlMllStf) shifts
            // the pole radius, clamped < 0.995 for stability.
            float r_a = 0.90f + (0.07f * vq);
            float freq_a = (m_preset_idx == k_MarchSnare) ? 3500.0f
                         : (m_preset_idx == k_BrushSnare) ? 2200.0f
                         : (m_preset_idx == k_RimShot)    ? 3200.0f : 2800.0f;
            if (m_preset_idx == k_BrushSnare) r_a = 0.83f + (0.03f * vq); // low-Q diffuse rattle — no resonant "ack" ring (chuff, not shack)
            freq_a = fminf(20000.0f, freq_a * sn_bright * sn_body);   // sn_body = TubRad body-depth (Band A only)
            r_a = fmaxf(0.5f, fminf(0.995f, r_a + sn_tight));
            float w_a = (M_TWOPI * freq_a) * inverse_default_sample_rate;
            v.exciter.snare_wire_a1 = 2.0f * r_a * fastercosfullf(w_a);
            v.exciter.snare_wire_a2 = r_a * r_a;

            // Band B: per-preset centre freq + pole radius (fallback: 4.5 kHz, r=0.86)
            float freq_b = preset_param(preset, k_snare_freq_b);
            float r_b_base = preset_param(preset, k_snare_r_b);
            if (freq_b < 100.0f) freq_b = 4500.0f;
            if (r_b_base < 0.3f) r_b_base = 0.86f;
            freq_b = fminf(20000.0f, freq_b * sn_bright);
            float r_b = fmaxf(0.5f, fminf(0.995f, r_b_base + (0.05f * vq) + sn_tight));
            float w_b = (M_TWOPI * freq_b) * inverse_default_sample_rate;
            v.exciter.snare_wire_a1b = 2.0f * r_b * fastercosfullf(w_b);
            v.exciter.snare_wire_a2b = r_b * r_b;

            // Band C: per-preset centre freq + pole radius (fallback: 7.2 kHz, r=0.82)
            float freq_c = preset_param(preset, k_snare_freq_c);
            float r_c_base = preset_param(preset, k_snare_r_c);
            if (freq_c < 100.0f) freq_c = 7200.0f;
            if (r_c_base < 0.3f) r_c_base = 0.82f;
            freq_c = fminf(20000.0f, freq_c * sn_bright);
            float r_c = fmaxf(0.5f, fminf(0.995f, r_c_base + (0.03f * vq) + sn_tight));
            float w_c = (M_TWOPI * freq_c) * inverse_default_sample_rate;
            v.exciter.snare_wire_a1c = 2.0f * r_c * fastercosfullf(w_c);
            v.exciter.snare_wire_a2c = r_c * r_c;
        }
        // BrshSnr texture: a brush is many small flexible straws sweeping the
        // head — a soft breathy "chuff", NOT a snappy "shack".  Three things
        // create snap and must go: (1) the fast high-band burst (a ~6 ms bright
        // click at onset), (2) too much bright high-branch content, (3) a
        // resonant wire ring.  A faint ~5 Hz flutter models the straws' contact.
        float brush_onset_scale = 1.0f;   // consumed by the onset block below
        if (m_preset_idx == k_BrushSnare) {
            v.noise_am_depth = 0.15f;
            v.noise_am_inc   = (M_TWOPI * 5.0f) * inverse_default_sample_rate; // slow rattle (HW: "rattle must be slower")
            v.noise_am_decay = 1.0f;                    // subtle flutter persists
            v.noise_am_phase = M_3_PI_2;             // full level on frame 0
            // BrshSnr's velocity curve is the QUADRATIC one set above, so
            // `current_velocity` only ever spans 0.08..0.72 — and every mapping
            // written against it therefore saw a third of a knob.  HW pass 41:
            // "even with negative velocity the hit is too hard, it's just
            // lowering the volume not softening", and the measurement agreed:
            // across velocity 127→30 the peak fell 8 dB while the centroid moved
            // 11 % and t40 5 %.  Normalise back to 0..1 first so the character
            // mappings below get the whole stroke, not a slice of it.
            const float vqb = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
            const float vbn = fmaxf(0.0f, fminf(1.0f, (vqb - 0.08f) * 1.5625f));
            // No bright click: the "high" burst decays WITH the soft body instead
            // of snapping in ~6 ms.  This is the single biggest "shack"->"chuff".
            v.exciter.noise_env_hi.decay_rate = v.exciter.noise_env.decay_rate;
            // Darker/softer split — mostly the low (breathy) branch, little of the
            // bright high branch, so the puff reads as air not sizzle.  The range
            // was 0.208..0.272 (inaudible); a soft stroke is now almost pure air
            // and only an accent brings the bright branch up.
            v.exciter.noise_band_mix = 0.08f + 0.19f * vbn;
            // …and it ARRIVES more slowly.  A brush drawn softly across a head
            // has no contact transient at all; the fixed 2 ms onset is what made
            // a quiet stroke still read as a hit rather than a sweep.
            brush_onset_scale = 1.0f + 8.0f * (1.0f - vbn);
        }
        {
            float atk_ms = preset_param(preset, k_onset_attack_ms) * brush_onset_scale;
            if (atk_ms > 0.001f) {
                v.onset_env = 0.0f;
                v.onset_inc = 1000.0f / (atk_ms * default_sample_rate);
            } else {
                v.onset_env = 1.0f;
                v.onset_inc = 0.0f;
            }
        }

        // Metallic transient FM chirp for recognizable sweep character.
        bool metallic_diff = (preset_param(static_cast<ProgramIndex>(m_preset_idx), k_base_fm_hz) > 0.0f) &&
                             (preset_param(static_cast<ProgramIndex>(m_preset_idx), k_diffuser_mix) > 0.0f);
        if (metallic_diff || m_preset_idx == k_Cowbell ||
            m_preset_idx == k_Triangle || m_preset_idx == k_BellTree) {
          float base_fm_hz = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_base_fm_hz);
          v.metal_fm_phase = 0.0f;
          v.metal_fm_inc = (M_TWOPI * base_fm_hz) * inverse_default_sample_rate;
          v.metal_fm_env = 1.0f;
          v.metal_fm_decay = (m_preset_idx == k_HiHatClosed) ? 0.9955f : 0.9978f;
          // HHat-O: lower FM depth (0.16→0.06) so the chirp doesn't re-excite KS
          // harmonics above 5 kHz as strongly; SVF BP@8kHz noise can then dominate.
          v.metal_fm_depth = (m_preset_idx == k_HiHatClosed) ? 0.08f
                           : (m_preset_idx == k_HiHatOpen)  ? 0.06f
                           : (m_preset_idx == k_Gong)       ? 0.18f  // stronger FM chirp: HW asked for more "crash" onset
                           : (m_preset_idx == k_Timpani)    ? 0.0f   // no FM: mode 6 carries the shimmer; FM only muddied the low end
                           : 0.16f;
          }
        // Noise ⇄ ring cross-modulation depths for the metallic plates.  The
        // parallel noise is ring-modulated by the modal output (processBlock)
        // so wash and ring interact instead of overlaying — HW report: "two
        // sounds overlaid ... expected some modulation between the two".
        switch (m_preset_idx) {
            case k_Cymbal:    v.modal_rm_depth = 0.40f; break;
            case k_Gong:      v.modal_rm_depth = 0.35f; break;
            case k_HiHatOpen: v.modal_rm_depth = 0.30f; break;
            case k_Ride:      v.modal_rm_depth = 0.0f; break;
            case k_RideBell:  v.modal_rm_depth = 0.30f; break;
            default: break;
        }
        // ── ENGINE_CYMBAL strike (ported dense-resonator cymbal) ───────────────
        // Replaces the old plate crash-bank + FDN for the metallic cymbal family.
        // Per-preset config is explicit so each preset tunes independently.
        if (kPresetEngine[m_preset_idx] == ENGINE_CYMBAL) {
            CymbalConfig cc{};
            float ring_scale = 1.0f, pm_amt = 1.0f;
            int ref_note = 60;  // preset's shipped default Note (REFERENCE-ANCHOR)
            // Resonator counts trimmed from the prototype's 112/104/96 for the
            // target CPU budget (4 stacked voices overloaded the interface);
            // level is count-independent via the 1/sqrt(N) normalisation.
            switch (m_preset_idx) {
                case k_Cymbal:
                    cc = { m_cym_crash_hz, 16, 96, 300.f, 20000.f, 1.8f,
                           0.018f, 0.44f, 0.040f, 0.30f, 0.0035f, 0.85f, 0.045f,
                           0.154f, 0.30f, 0.020f, 0.10f };
                    ref_note = 65;
                    break;
                case k_Ride:
                    cc = { m_cym_ride_hz, 16, 88, 450.f, 18000.f, 1.2f,
                           0.015f, 0.55f, 0.035f, 0.34f, 0.0030f, 0.75f, 0.040f,
                           0.52f, 0.34f, 0.016f, 0.08f };
                    ref_note = 69;
                    break;
                case k_RideBell:
                    // Stronger, longer stick ping than Ride = the bell "tang".
                    cc = { m_cym_ride_hz, 16, 88, 450.f, 18000.f, 1.2f,
                           0.015f, 0.55f, 0.035f, 0.34f, 0.0045f, 0.95f, 0.040f,
                           0.52f, 0.34f, 0.016f, 0.08f };
                    ref_note = 60;
                    break;
                case k_Gong:
                    cc = { m_cym_gong_hz, 16, 80, 150.f, 14000.f, 2.4f,
                           0.25f, 1.70f, 0.50f, 1.20f, 0.020f, 0.32f, 0.035f,
                           0.126f, 0.22f, 0.010f, 0.15f };
                    ref_note = 50;
                    break;
                case k_HiHatOpen:
                    // Long CLOSED-hat sizzle ("tick", not "tong"): all anchors
                    // above 3.2 kHz with heavy jitter = bright metallic noise
                    // continuum, no pitched low body; short stick tick; decay
                    // longer than a closed chick but far shorter than a crash.
                    // resonatorLevel 6.5: the 1/f pink drive has ~10 dB less
                    // energy at these >3.2 kHz anchors, so the bank needs a
                    // much hotter tap than the low-anchor presets.
                    cc = { m_cym_hihat_hz, 16, 64, 2800.f, 20000.f, 1.5f,
                           0.006f, 0.55f, 0.012f, 0.30f, 0.0015f, 0.60f, 0.045f,
                           6.50f, 0.55f, 0.010f, 0.05f };
                    ring_scale = 0.55f;
                    ref_note = 79;
                    break;
                case k_Splash:
                    // Prototype splash: pitched modal body from ~1.1 kHz with
                    // the sizzle above; fast decay ("pssh", not hiss).
                    cc = { m_cym_splash_hz, 14, 64, 1000.f, 19000.f, 0.55f,
                           0.006f, 0.70f, 0.018f, 0.25f, 0.0020f, 0.75f, 0.038f,
                           1.23f, 0.16f, 0.0015f, 0.06f };
                    ref_note = 76;
                    break;
                default: break;
            }
            // Body-brightness fix (REALISM_REVIEW item 1): Cymbal/Ride/RidBel
            // measured ~1-1.4 kHz body centroid vs ~6-7 kHz in the reference
            // samples — the flat-gain bank under the pink (−3 dB/oct) driver
            // reads dark/tonal.  hfTilt=1 counters the driver tilt exactly.
            // HHat-O ("do not break"), Gong (deliberately tonal) and Splash
            // keep the legacy flat bank (hfTilt stays 0).
            if (m_preset_idx == k_Cymbal || m_preset_idx == k_Ride ||
                m_preset_idx == k_RideBell) {
                // Over-whiten past 1.0: the thwack burst and swept one-pole park
                // extra red energy in the drive beyond the pink tilt itself
                // (measured: tilt 1.0 only moved the body 0.9 → 1.5 kHz; 2.0 →
                // 3.3 kHz).  Crash needs more than the rides — its anchor set
                // starts lower (343 Hz) so the red bottom weighs more.
                cc.hfTilt = (m_preset_idx == k_Cymbal) ? 3.0f : 2.0f;
            }
            if (cc.freqHz) {
                // Bank size = preset base x Partls density (user CPU/density
                // trade-off) x voice-pressure degradation: the 3rd/4th
                // simultaneous voice gets a smaller bank — in a dense stack
                // the density loss is masked, and rolls can no longer
                // overload the CPU.
                int active_cym = 0, busy_cost = 0;
                for (int i = 0; i < NUM_VOICES; ++i) {
                    if (i != (int)state.next_voice_idx &&
                        state.voices[i].is_active && state.voices[i].cymbal.active) {
                        ++active_cym;
                        busy_cost += kCymVoiceFixedLanes +
                                     (int)state.voices[i].cymbal.resCount;
                    }
                }
                float rscale = (float)m_cym_reso_scale * 0.01f;
                if (active_cym >= 3)      rscale *= 0.67f;
                else if (active_cym == 2) rscale *= 0.80f;
                int rcount = (int)((float)cc.resonators * rscale);
                // Cost ceiling: whatever the ladder above asks for, the new
                // voice may only take what is left of kCymCostBudget once its
                // own fixed cost is paid.  Already-ringing banks keep their
                // size; the newest (least scrutinised) voice absorbs the trim.
                const int room = kCymCostBudget - busy_cost - kCymVoiceFixedLanes;
                if (rcount > room) rcount = room;
                rcount = (rcount + 3) & ~3;
                if (rcount < kCymMinResonators) rcount = kCymMinResonators;
                if (rcount > (int)kCymbalMaxResonators) rcount = (int)kCymbalMaxResonators;
                cc.resonators = (uint16_t)rcount;
                // Note transposes the whole anchor spectrum like a smaller or
                // larger cymbal (2^(Δsemitones/12), anchored at the shipped
                // default note so the stock sound is unchanged) — previously
                // the note only reseeded the jitter, which read as "distortion".
                float pitch_ratio = exp2f((float)((int)note - ref_note) * (1.0f / 12.0f));
                // VlMllRes scales the stick hit ("tang"): shipped value is 0 for
                // all cymbal presets (=> x1.0 — REFERENCE-ANCHORED); positive
                // values boost the stick level AND sharpen the noise-driver
                // attack (punch without running the boosted click into the
                // output clamp); negative values soften toward a felt mallet.
                // Velocity still scales the hit on top inside cymbal_note_on.
                const float hit_mod = (float)m_params[k_paramVlMllRes] * 0.01f;
                cc.stickLevel = fminf(2.0f, cc.stickLevel * knob_exp2(1.2f * hit_mod));
                if (hit_mod > 0.0f) {
                    cc.lowAttackSec *= knob_exp2(-1.0f * hit_mod);   // up to 2x snappier
                    cc.thwackSec    *= 1.0f + 0.6f * hit_mod;    // longer ping = "tang"
                }
                // VlMllStf → stick STIFFNESS (was inert on cymbals): a harder
                // tip bites faster, the contact ping gets shorter and the
                // strike brighter; a soft felt tip rounds all three off.
                // Anchored at the shipped VlMllStf (0 on every cymbal preset)
                // so the calibrated cymbals render bit-identical.
                const float stif_d = (float)m_params[k_paramVlMllStf] * 0.01f - m_snare_vlstf_ref;
                if (stif_d > 0.001f || stif_d < -0.001f) {
                    cc.lowAttackSec *= knob_exp2(-1.2f * stif_d);
                    cc.thwackSec    *= fmaxf(0.40f, fminf(2.50f, knob_exp2(-0.8f * stif_d)));
                    cc.hfTilt        = fmaxf(0.0f, fminf(4.0f, cc.hfTilt + 0.8f * stif_d));
                }
                // ── Cymbal knob-design (REFERENCE-ANCHORED) ──────────────────
                // The dense-resonator port bypasses the modal bank, so Dkay,
                // Mterl, HitPos, Rel, Inharm and TubRad were ALL dead on the
                // cymbal family.  Map each to a natural property of the metal,
                // anchored at the shipped knob value so the calibrated cymbals
                // render bit-identical and only knob movement bites.
                // Depth pass (July 2026, HW: "parameters are too subtle").  Every
                // coefficient below was widened and its clamp opened; all remain
                // anchored at Δ=0, so the 40 shipped presets are untouched and
                // only the travel either side of the shipped value grew.
                {
                    // Dkay → overall ring decay length (body + sizzle together).
                    float cd_dk = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
                    float dscale = fmaxf(0.15f, fminf(6.0f, knob_exp2(3.0f * (cd_dk - m_modal_dkay_ref))));
                    // Rel → tail length, weighted toward the sizzle (high band).
                    // Dkay is the coarse overall length; Rel trims the tail on top
                    // (gentle on the body decaySec, full on the sizzle highDecaySec)
                    // so the two knobs give independent "how long / how sizzly".
                    float cd_rl = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel] * 0.05f));
                    float rscale = fmaxf(0.15f, fminf(6.0f, knob_exp2(2.4f * (cd_rl - m_modal_rel_ref))));
                    cc.decaySec     *= fmaxf(0.15f, fminf(6.0f, dscale * sqrtf(rscale)));
                    cc.highDecaySec *= fmaxf(0.15f, fminf(9.0f, dscale * rscale));
                    // Inharm → jitter spread (beating density / shimmer thickness).
                    float cd_ih = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramInharm] * 0.01f));
                    // Deliberately NOT widened with the rest of this pass.  Jitter
                    // is the one cymbal knob already past its useful depth: at 2.4
                    // and 3.0 the detune smears wide enough that the bank's high
                    // modes pile onto the fLo/Nyquist clamps, and the measured
                    // HF-band swing across the knob SHRANK (audit: ok -> weak).
                    // 2.0 is the setting that buys the most audible spread.
                    cc.jitterSemis = fmaxf(0.0f, fminf(6.0f,
                                     cc.jitterSemis * knob_exp2(2.0f * (cd_ih - m_modal_inharm_ref))));
                    // Mterl → metal brightness (hard bronze sustains highs; soft
                    // alloy reads dull).  Tilts the bank HF weight + the ceiling.
                    float cd_mn = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
                    float d_cmt = cd_mn - m_modal_mterl_ref;
                    if (d_cmt < -0.001f || d_cmt > 0.001f) {
                        cc.hfTilt = fmaxf(0.0f, fminf(5.0f, cc.hfTilt + d_cmt * 3.6f));
                        cc.maxHz  = fmaxf(2500.0f, fminf(20000.0f, cc.maxHz * knob_exp2(1.3f * d_cmt)));
                    }
                    // HitPos → strike locus: toward the BELL (high) = more stick
                    // ping and pitched ring, less wash; toward the EDGE (low) =
                    // more broadband wash, softer ping.
                    // fmaxf(-1.0f, …): anchored consumer, so it must follow the
                    // knob below centre now that HitPos is bipolar (pass 39).
                    float cd_hp = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramHitPos] * 0.01f));
                    float d_chp = cd_hp - m_modal_hitpos_ref;
                    if (d_chp < -0.001f || d_chp > 0.001f) {
                        cc.stickLevel     = fminf(3.0f, cc.stickLevel * knob_exp2(2.4f * d_chp));
                        cc.resonatorLevel = fmaxf(0.03f, cc.resonatorLevel * knob_exp2(1.4f * d_chp));
                        cc.noiseLevel     = fmaxf(0.0f, cc.noiseLevel * knob_exp2(-1.7f * d_chp));
                    }
                    // TubRad → instrument SIZE: transposes the whole anchor
                    // spectrum like a bigger (lower) or smaller (higher) cymbal,
                    // and adds breath under the ring.  Size is the clearest
                    // "tube radius" analogue and is plainly audible.  Anchored so
                    // the shipped cymbal pitch is the neutral point.
                    float cd_tn = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
                    float d_ctr = cd_tn - m_modal_tubrad_ref;
                    if (d_ctr < -0.001f || d_ctr > 0.001f) {
                        pitch_ratio *= fmaxf(0.35f, fminf(2.8f, knob_exp2(-1.2f * d_ctr)));
                        cc.shimmerLevel = fmaxf(0.0f, cc.shimmerLevel * knob_exp2(1.3f * d_ctr));
                    }
                    // MlltRes → RING PRESENCE (metal vs air).  Was completely
                    // inert on the cymbal family (audit: "NO EFFECT") because
                    // the dense-resonator port has no mallet exciter at all.
                    // Up = the bank speaks over the noise bed (a gong that
                    // *rings*); down = the wash dominates (all air, no metal).
                    float cd_mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
                    float d_cmr = cd_mr - m_modal_mltres_ref;
                    if (d_cmr < -0.001f || d_cmr > 0.001f) {
                        cc.resonatorLevel = fmaxf(0.03f, cc.resonatorLevel * knob_exp2(2.4f * d_cmr));
                        cc.shimmerLevel   = fmaxf(0.0f,  cc.shimmerLevel   * knob_exp2(1.2f * d_cmr));
                        cc.noiseLevel     = fmaxf(0.0f,  cc.noiseLevel     * knob_exp2(-0.9f * d_cmr));
                    }
                    // MlltStif → BEATER HARDNESS (static counterpart of the
                    // velocity-driven VlMllStf, which was the only stick control
                    // the family had).  Hard stick = a short bright contact ping
                    // and a faster bite; soft mallet = a slow, dark swell — the
                    // difference between a gong struck with a stick and one
                    // bloomed in with a felt beater.
                    float cd_st = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
                    float d_cst = cd_st - m_modal_stiff_ref;
                    if (d_cst < -0.001f || d_cst > 0.001f) {
                        cc.stickLevel   = fminf(3.0f, cc.stickLevel * knob_exp2(1.8f * d_cst));
                        cc.thwackSec   *= fmaxf(0.30f, fminf(3.00f, knob_exp2(-1.1f * d_cst)));
                        cc.lowAttackSec = fmaxf(0.0005f, cc.lowAttackSec * knob_exp2(-1.6f * d_cst));
                        cc.hfTilt       = fmaxf(0.0f, fminf(5.0f, cc.hfTilt + 1.1f * d_cst));
                    }
                }
                cymbal_note_on(v.cymbal, cc, v.current_velocity, ring_scale, pm_amt,
                               pitch_ratio, seed, cym_still_ringing);

                // ── Cymbal family level calibration ────────────────────────
                // HW (pass 29): "cymbal: sound is very quiet".  Measured over a
                // common 250 ms window (full-render RMS is not comparable —
                // render lengths run 1-20 s), the ENGINE_CYMBAL presets sat
                // 11-18 dB under the mean of every other preset in the unit:
                //
                //   Cymbal -18.3 dB   Ride -17.3 dB   HHat-O -17.9 dB
                //   RidBel -14.7 dB   Splash -11.3 dB   Gong +3.6 dB
                //
                // This is NOT a pass-26/29 regression — the same presets
                // measured the same before both (Cymbal 0.0080 -> 0.0099 RMS).
                // It became audible because pass 29's master change lifts
                // transient-dense presets ~3.7 dB but a decaying wash barely at
                // all, so everything else moved up around them.  So it is a
                // straight voicing miscalibration, corrected here.
                //
                // velocityGain is the ONLY uniform output scaler on this voice:
                // scaling the config levels instead would not be uniform,
                // because stickLevel feeds both the resonator drive and the
                // direct tap and would land on the thwack squared.
                //
                // Gong is already correctly placed and is left alone.  HHat-O
                // is equally quiet by measurement but is flagged
                // "HW-approved, do not break" in CLAUDE.md, so its level is
                // deliberately NOT touched here — raise it only on an explicit
                // listen.
                float cym_trim = 1.0f;
                switch (m_preset_idx) {
                    case k_Cymbal:   cym_trim = 3.2f; break;
                    case k_Ride:     cym_trim = 2.9f; break;
                    case k_RideBell: cym_trim = 2.1f; break;
                    case k_Splash:   cym_trim = 1.4f; break;
                    default: break;
                }
                v.cymbal.velocityGain *= cym_trim;
            }
        }
        // Ride/RidBel: like Gong/HHat-O, their per-preset NzRs left noise_env_hi
        // with a 3-8 ms T60 — the sustained sizzle a ride/crash needs was dead
        // before the hf/ring paths could use it.  Give both long shimmer beds.
        if (m_preset_idx == k_Ride || m_preset_idx == k_RideBell) {
            v.exciter.noise_env_hi.decay_rate = 0.000060f; // shorter so ring (not crash) dominates the body
            v.exciter.noise_env.decay_rate    = 0.000050f;
        }
        // Clap: 3-4 retriggered noise bursts (~55 Hz gate, depth fading in
        // ~15 ms) before the smooth "tcha" tail — single-burst noise read as
        // a click on hardware.  Phase starts at 3π/2 so frame 0 is full level.
        if (m_preset_idx == k_Clap) {
            v.noise_am_depth = 0.95f;
            v.noise_am_inc   = (M_TWOPI * 55.0f) * inverse_default_sample_rate;
            v.noise_am_decay = 0.99860f;                 // depth τ ≈ 15 ms
            v.noise_am_phase = M_3_PI_2;
        }
        // Shaker: grain pulses — the bead mass hits each shell wall in turn,
        // ~13 Hz double-pulse with the depth fading over ~100 ms.
        if (m_preset_idx == k_Shaker) {
            v.noise_am_depth = 0.92f;
            // 17 → 28 Hz (HW pass 41: "increase LFO speed for more shaking
            // effect").  A hand shaker's bead-to-wall rate under a fast wrist
            // is ~25-35 Hz; 17 Hz read as a slow rattle rather than shaking.
            // MlltStif still scales this (knob_exp2 below), so the old 17 Hz
            // is still reachable by turning it down.
            v.noise_am_inc   = (M_TWOPI * 28.0f) * inverse_default_sample_rate;
            // decay = 1.0: the rattle LFO does NOT fade — a longer Rel must give a
            // longer RATTLE, not a smooth decaying hiss (HW: "when sound is longer
            // there's no rattle at all").  The shaker rattles for its whole tail.
            v.noise_am_decay = 1.0f;
            v.noise_am_phase = M_3_PI_2;
            // Soft onset: a shaker swells over ~15 ms, it does not click.  The
            // noise burst otherwise reaches full level in <1 ms = the "hit" HW
            // reported.  Slow the noise-env attack so the rattle fades in.
            v.exciter.noise_env.attack_rate    = 0.004f;  // ~15 ms ramp
            v.exciter.noise_env_hi.attack_rate = 0.004f;
        }
        // ── PARAM RE-ROUTING (accepted template) ─────────────────────────────
        // ENGINE_NOISE (Clap/Shaker/HHat-C) leaves the whole exciter/resonator
        // knob bank inert (mallet is masked by NzMix≈1, modal mix is ~0).  Wire
        // the two most useful dead knobs to the grain/burst LFO, REFERENCE-
        // ANCHORED so the shipped sound is unchanged and only knob movement bites:
        //   MlltStif → AM rate  (grain/burst speed, ±2 oct around shipped)
        //   MlltRes  → AM depth (burst contrast, 0..~1)
        if (kPresetEngine[m_preset_idx] == ENGINE_NOISE && v.noise_am_inc > 0.0f) {
            float st = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
            v.noise_am_inc *= knob_exp2(2.0f * (st - m_modal_stiff_ref));
            float mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
            v.noise_am_depth = fmaxf(0.0f, fminf(0.99f,
                                     v.noise_am_depth * knob_exp2(1.5f * (mr - m_modal_mltres_ref))));
        }
        // Modal bank: re-initialize on every NoteOn so frequencies track the played note
        // and envelopes are velocity-scaled. LoadPreset called init_modal_modes with
        // current_velocity=0 (default) which zeroed all modal_env_X values, silencing
        // the entire modal synthesis path — Cymbal and Gong were running without it.
        {
            const ModalPresetConfig& mc = modal_preset_configs[m_preset_idx];
            if (mc.mode_count > 0) {
                float modal_mix_val = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_modal_mix);

                // ── Effective modal configuration (REFERENCE-ANCHOR pattern) ──
                // For modal engines, Model / Partls / Inharm / Mterl reshape the
                // bank RELATIVE to the preset's shipped knob values.  At the
                // shipped values every branch below is a no-op, so the calibrated
                // modal_preset_configs sound is bit-identical (no regression).
                float r2 = mc.ratio2, r3 = mc.ratio3, r4 = mc.ratio4;
                float r5 = mc.ratio5, r6 = mc.ratio6;
                float t1 = mc.t60_1_ms, t2 = mc.t60_2_ms, t3 = mc.t60_3_ms, t4 = mc.t60_4_ms;
                float e1 = mc.env1, e2 = mc.env2, e3 = mc.env3, e4 = mc.env4;
                float e5 = mc.env5, e6 = mc.env6;
                uint8_t count = mc.mode_count;
                const EngineType modal_ne = kPresetEngine[m_preset_idx];
                const bool is_modal_engine = (modal_ne != ENGINE_KS &&
                                              modal_ne != ENGINE_NOISE &&
                                              modal_ne != ENGINE_REMOVED);
                if (is_modal_engine) {
                    const uint8_t model_now = (m_model_a < k_lastModel) ? m_model_a : (uint8_t)k_String;
                    // (1) Model → modal ratio set.  Moving the Model knob off the
                    // shipped value swaps the calibrated ratios for the selected
                    // physical model's template (harmonic string, free bar,
                    // Bessel membrane, thick plate, ...).
                    if (model_now != m_modal_model_ref) {
                        const float* tr = kModelModalRatios[model_now];
                        r2 = tr[0]; r3 = tr[1]; r4 = tr[2]; r5 = tr[3]; r6 = tr[4];
                    }
                    // (2) Partls → mode count AND overtone richness, offset around
                    // the shipped count.  Partls 5-7 are ResA/ResB editor-selects:
                    // ignored.  The count change alone was inaudible (added modes
                    // are high/quiet), so ALSO scale the upper-mode envelopes:
                    // up = richer/brighter overtones, down = fewer/purer.
                    {
                        int pv = m_params[k_paramPartls];
                        if (pv >= 0 && pv <= 4 && pv != m_modal_partls_ref) {
                            int d = pv - m_modal_partls_ref;
                            int c = (int)count + d;
                            count = (uint8_t)((c < 2) ? 2 : ((c > 6) ? 6 : c));
                            // Widened for sound-design travel (HW: "effect too weak").
                            // 0.6→1.0 per step and now tilts mode 2 as well, so each
                            // Partls click is a clear richness/brightness change.
                            float rich = knob_exp2(1.0f * (float)d);   // ±per Partls step
                            rich = fmaxf(0.15f, fminf(6.0f, rich));
                            e2 *= fmaxf(0.5f, fminf(2.0f, rich));
                            e3 *= rich; e4 *= rich; e5 *= rich; e6 *= rich;
                        }
                    }
                    // Fallbacks for modes the calibrated row doesn't define
                    // (a raised count would otherwise put modes at 0 Hz / 0 gain).
                    if (count > 2) {
                        const float* tr = kModelModalRatios[model_now];
                        if (r3 <= 0.0f) r3 = tr[1];
                        if (r4 <= 0.0f) r4 = tr[2];
                        if (r5 <= 0.0f) r5 = tr[3];
                        if (r6 <= 0.0f) r6 = tr[4];
                        if (t2 <= 0.0f) t2 = t1 * 0.65f;
                        if (t3 <= 0.0f) t3 = t2 * 0.65f;
                        if (t4 <= 0.0f) t4 = t3 * 0.65f;
                        if (e3 <= 0.0f) e3 = e2 * 0.60f;
                        if (e4 <= 0.0f) e4 = e3 * 0.60f;
                        if (e5 <= 0.0f) e5 = e4 * 0.60f;
                        if (e6 <= 0.0f) e6 = e5 * 0.60f;
                    }
                    // (3) Inharm → overtone spread around the fundamental:
                    //   ratio' = 1 + (ratio − 1) × spread,  spread anchored ×1.0
                    // at the shipped Inharm.  Compresses toward harmonicity below
                    // the anchor, stretches the partials above it.
                    {
                        float inh = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramInharm] * 0.01f));
                        // Exponential, not linear (pass 37).  The old
                        // `1 + 2.4·d` was written when Inharm could only go UP
                        // from a floored 0, so its downward half was never
                        // reachable.  Under the bipolar range it is, and linear
                        // was doubly wrong there: `spread` hit the 0.05 clamp at
                        // d = −0.40, so **Inharm −40 … −100 all rendered
                        // identically** (measured on Handpan: ratios frozen at
                        // 1.000/1.011/1.054/1.099/1.103/1.168), and 0.05 is a
                        // DEGENERATE point — it collapses every partial onto the
                        // fundamental, which is a unison pile-up, not the
                        // "compressed toward harmonicity" the mapping intends.
                        // 2^(1.75·d) spans 0.30…3.36 over the full travel — the
                        // same top end as before, a live and musical bottom, and
                        // it cannot reach zero.  knob_exp2(0) is exactly 1.0, so
                        // the anchor and byte-identity hold.
                        float spread = knob_exp2(1.75f * (inh - m_modal_inharm_ref));
                        if (spread < 0.999f || spread > 1.001f) {
                            spread = fmaxf(0.05f, fminf(5.0f, spread));
                            if (r2 > 0.0f) r2 = 1.0f + (r2 - 1.0f) * spread;
                            if (r3 > 0.0f) r3 = 1.0f + (r3 - 1.0f) * spread;
                            if (r4 > 0.0f) r4 = 1.0f + (r4 - 1.0f) * spread;
                            if (r5 > 0.0f) r5 = 1.0f + (r5 - 1.0f) * spread;
                            if (r6 > 0.0f) r6 = 1.0f + (r6 - 1.0f) * spread;
                        }
                    }
                    // (4) Mterl → material damping of the upper modes.  Brighter
                    // material (metal) sustains its overtones; dull material
                    // (wood) damps them fast.  Mode 1 keeps the calibrated T60
                    // (overall ring length stays Dkay's job).
                    {
                        float mn = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
                        // Widened 1.5→2.5→3.4 (HW: "effect too weak", then "from
                        // subtle to live"): metal sustains its overtones, wood
                        // damps them — now a full timbre sweep across the knob.
                        float mat = knob_exp2(3.4f * (mn - m_modal_mterl_ref));
                        if (mat < 0.999f || mat > 1.001f) {
                            mat = fmaxf(0.08f, fminf(9.0f, mat));
                            t2 *= mat;
                            t3 *= mat;
                            t4 *= mat;  // modes 5/6 derive their decay from t4
                            // ALSO tilt the upper modes' INITIAL energy (gentler,
                            // mat^0.5).  Damping only the T60 was tail-only and
                            // inaudible on fundamental-dominated drums (e.g. Timpani);
                            // metal = brighter ONSET too, wood = duller onset.
                            float matenv = fmaxf(0.25f, fminf(3.2f, sqrtf(mat)));
                            // Also tilt mode 2 (gentler) so Mterl is audible on
                            // drums whose energy sits in the low modes, where the
                            // 3-6 tilt alone read as "weak" (audit).
                            e2 *= fmaxf(0.5f, fminf(2.2f, matenv));
                            e3 *= matenv; e4 *= matenv; e5 *= matenv; e6 *= matenv;
                        }
                    }
                    // (5) TubRad → body size.  A wider shell/cavity rings its
                    // WHOLE body longer (all modes, not just the fundamental) and
                    // feeds more energy into the boom oscillator where the preset
                    // has one.  Anchored at the shipped TubRad so the calibrated
                    // body is the neutral point.  Widened from t1-only (HW/audit:
                    // "TubRad no effect" on membranes — a mode-1-only tail change
                    // is inaudible on fundamental-dominated drums like Timpani).
                    {
                        float tn = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
                        float tub = knob_exp2(1.9f * (tn - m_modal_tubrad_ref));
                        if (tub < 0.999f || tub > 1.001f) {
                            tub = fmaxf(0.25f, fminf(3.5f, tub));
                            t1 *= tub; t2 *= tub; t3 *= tub; t4 *= tub;  // whole shell
                            // Kick presets route TubRad → boom base tune in the
                            // dedicated kick block below; don't also scale their
                            // boom_mix here (keeps all three kicks consistent).
                            if (m_preset_idx != k_Kick2 && m_preset_idx != k_808Sub &&
                                m_preset_idx != k_KickDrum)
                                v.boom_mix *= tub;
                        }
                    }
                }
                v.init_modal_modes(r2, r3, r4,
                                   t1, t2, t3, t4,
                                   modal_mix_val, e1, e2, e3, e4,
                                   count, r5, r6,
                                   e5, e6);
                // Dkay → modal T60 trim for BAR/MEMBRANE/SNARE/PLATE engines.
                // The calibrated modal_preset_configs T60 plays at the preset's shipped
                // Dkay (m_modal_dkay_ref), so the default sound always matches the tuning.
                // The knob then trims RELATIVE to that reference:
                //   t60_scale = 2^(3*(norm - ref))  →  ref→×1.0, lower Dkay→shorter ring.
                // knob_exp2 (guarded fasterpow2f): raw fasterpow2f returns ~0.96 at
                // the Δ=0 anchor, silently shortening every shipped ring — the guard
                // returns exactly 1.0 there; off-anchor the ±0.3% error is inaudible.
                // Applied as powf(decay, 1/scale): scale<1 → faster decay coefficient.
                {
                    const EngineType ne3 = kPresetEngine[m_preset_idx];
                    if (ne3 != ENGINE_KS && ne3 != ENGINE_NOISE && ne3 != ENGINE_REMOVED) {
                        float dkay_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
                        // Rel → modal ring-length trim (anchored): on modal engines Rel
                        // otherwise only gated the noise tail and felt dead.  Folded into
                        // t60_scale alongside Dkay (Dkay = coarse decay, Rel = ±~1 oct trim).
                        float rel_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel] * 0.05f));
                        // Widened again (HW: "from subtle to live"): Dkay 3.5→4.5
                        // (~±4.5 oct of ring), Rel 2.5→3.2 of extra trim on top.
                        float t60_scale = knob_exp2(4.5f * (dkay_norm - m_modal_dkay_ref)
                                              + 3.2f * (rel_norm - m_modal_rel_ref));
                        if (t60_scale < 0.999f || t60_scale > 1.001f) {
                            float exp_scale = 1.0f / t60_scale;
                            v.modal_decay_1 = powf(v.modal_decay_1, exp_scale);
                            v.modal_decay_2 = powf(v.modal_decay_2, exp_scale);
                            v.modal_decay_3 = powf(v.modal_decay_3, exp_scale);
                            v.modal_decay_4 = powf(v.modal_decay_4, exp_scale);
                            v.modal_decay_5 = powf(v.modal_decay_5, exp_scale);
                            v.modal_decay_6 = powf(v.modal_decay_6, exp_scale);
                        }

                        // Mallet stiffness → modal brightness tilt.  Stiffer mallet
                        // boosts higher modes (brighter strike), softer cuts them
                        // (rounder).  Mode 1 is never tilted.
                        //
                        // Stronger MlltStif travel (HW: "effect too weak") WITHOUT
                        // touching shipped presets.  v.exciter.mallet_stiffness folds
                        // in the baked VlMllStf (velocity) and rim-position brightness,
                        // which is PART of the calibrated sound — so widening
                        // (mallet_stiffness − ref) directly would retune every
                        // off-centre membrane preset.  Instead split it: keep the
                        // baked vel/rim part at the original ×1.4 and widen only the
                        // KNOB's deviation from the shipped value (×2.4).  At the
                        // shipped knob value the knob term is exactly 0 → bit-identical.
                        float stiff_knob = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
                        float tilt = (v.exciter.mallet_stiffness - stiff_knob) * 1.4f   // baked vel/rim (unchanged)
                                   + (stiff_knob - m_modal_stiff_ref)          * 3.4f;  // knob travel (widened)
                        if (tilt < -0.001f || tilt > 0.001f) {
                            v.modal_env_2 *= fmaxf(0.05f, fminf(6.0f, 1.0f + tilt * 1.0f));
                            v.modal_env_3 *= fmaxf(0.05f, fminf(6.0f, 1.0f + tilt * 2.0f));
                            v.modal_env_4 *= fmaxf(0.05f, fminf(6.0f, 1.0f + tilt * 3.0f));
                            v.modal_env_5 *= fmaxf(0.05f, fminf(6.0f, 1.0f + tilt * 4.0f));
                            v.modal_env_6 *= fmaxf(0.05f, fminf(6.0f, 1.0f + tilt * 5.0f));
                        }

                        // MlltRes → modal presence (anchored).  MlltRes was dead
                        // on modal engines, so map it to the modal bank's
                        // level/presence — a clearly audible timbre control.
                        // (The old crash-bank intensity mapping went away with
                        // the ENGINE_CYMBAL port; cymbal presets bypass this
                        // whole path.)
                        {
                            float mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
                            // Widened 1.6→2.6→3.4 (HW: "effect too weak", then
                            // "from subtle to live") so MlltRes is a full-range
                            // modal presence/level control on modal engines.
                            v.modal_mix *= fmaxf(0.05f, fminf(9.0f, knob_exp2(3.4f * (mr - m_modal_mltres_ref))));
                        }

                        // HitPos → strike-position excitation.  Hitting toward the
                        // rim/edge (high HitPos) couples energy into the higher
                        // modes and away from the fundamental; striking dead
                        // centre does the opposite.  Anchored at the shipped
                        // HitPos so the calibrated balance is the neutral point.
                        // fmaxf(-1.0f, …): anchored, follows the knob below
                        // centre under the bipolar range (pass 39).  This is
                        // the consumer that carries the negative half on the
                        // 25 presets that ship HitPos 0 — below centre the
                        // tilt runs the other way, weighting mode 1 up and the
                        // upper modes down (a deeper "struck dead centre").
                        float hit_off = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramHitPos] * 0.01f))
                                      - m_modal_hitpos_ref;
                        if (hit_off < -0.001f || hit_off > 0.001f) {
                            // Coefficients doubled after HW feedback ("HitPos has
                            // no effect"): the tilt must be plainly audible on
                            // membrane presets whose upper modes sit well below
                            // the fundamental's level.
                            // Widened ~1.6× again (HW still "too weak" after the
                            // first doubling), then ×1.5 more in the July-2026
                            // depth pass: rim vs centre is now a dramatic tilt.
                            // KNOWN, MEASURED, DELIBERATELY NOT CHASED (pass 39):
                            // these six are linear-into-a-clamp, so the far
                            // NEGATIVE corner of the now-bipolar range saturates
                            // on presets that ship HitPos high.  Kick2 (ref
                            // 0.36) saturates below HitPos −54, AcSnare (ref
                            // 0.46) similarly — plateau_probe reports −98..−59
                            // identical on both.  Presets shipping HitPos 0 (25
                            // of 41, the ones the bipolar range was FOR) only
                            // saturate below −90, i.e. the last 8 units, so the
                            // pathology this pass set out to fix is fixed.
                            // The obvious repair is the pass-37 treatment —
                            // knob_exp2 instead of 1 ± c·hit_off — but matching
                            // the current mid-range feel needs the coefficients
                            // re-derived (mode 1 would need ~4.3 to reproduce
                            // today's 0.225 at hit_off 0.5, not 1.55), and this
                            // curve has been re-tuned against HW listening
                            // three times.  Re-shaping it is a voicing pass with
                            // a listen attached, not a bug fix to slip into a
                            // range change.
                            v.modal_env_1 *= fmaxf(0.08f, fminf(2.4f, 1.0f - hit_off * 1.55f));
                            v.modal_env_2 *= fmaxf(0.05f, fminf(9.0f, 1.0f + hit_off * 1.35f));
                            v.modal_env_3 *= fmaxf(0.05f, fminf(9.0f, 1.0f + hit_off * 2.40f));
                            v.modal_env_4 *= fmaxf(0.05f, fminf(9.0f, 1.0f + hit_off * 3.60f));
                            v.modal_env_5 *= fmaxf(0.05f, fminf(9.0f, 1.0f + hit_off * 4.80f));
                            v.modal_env_6 *= fmaxf(0.05f, fminf(9.0f, 1.0f + hit_off * 6.00f));
                        }
                    }
                }
            }
        }

        // Taiko velocity split (6th HW pass): "boom at strong hits, woodblock
        // when soft".  Hard strikes drive the sub-octave boom; soft strikes
        // lift the bright 212 Hz mid (modal mode 4, the dominant "AAN" partial)
        // so soft hits read as a bright open "tak", hard hits as a boomy thud.
        if (m_preset_idx == k_Taiko) {
            float vel = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
            v.boom_mix *= (0.25f + 1.50f * vel * vel);      // soft ×0.3 … hard ×1.75
            v.modal_env_4 *= (1.70f - 1.10f * vel);         // soft ×1.6 … hard ×0.6
        }

        // ── Strike transient layer (membrane presets) ───────────────────────
        // The modal tail is measurably correct; the perceptual gap is the
        // broadband ATTACK the modal bank structurally cannot make (Taiko
        // stick-slap, ref early centroid ~1.9 kHz; Timpani felt-mallet contact).
        // Layer a short velocity-scaled band-passed noise burst over the modal
        // body (see processBlock).  Pure DSP, no samples — closes most of the
        // attack-brightness gap that incremental modal tuning could not.
        if (m_preset_idx == k_Taiko) {
            v.trans_env   = 1.0f;
            v.trans_decay = 0.99204f;   // T60 ≈ 18 ms
            v.trans_gain  = 2.90f;
            v.trans_a_lo  = 0.220f;     // ~2 kHz HP corner
            v.trans_a_hi  = 0.540f;     // ~6 kHz LP corner → bright stick slap
            v.trans_lp_lo = v.trans_lp_hi = 0.0f;
        } else if (m_preset_idx == k_Timpani) {
            v.trans_env   = 1.0f;
            v.trans_decay = 0.9881f;    // T60 ≈ 12 ms
            v.trans_gain  = 2.00f;
            v.trans_a_lo  = 0.075f;     // ~600 Hz HP corner
            v.trans_a_hi  = 0.300f;     // ~2.6 kHz LP corner → felt-mallet contact
            v.trans_lp_lo = v.trans_lp_hi = 0.0f;
        } else if (m_preset_idx == k_AcousticTom) {
            // Close-room tom ref has a bright stick-contact attack the modal
            // bank cannot make (measured render centroid 205 Hz vs ref 1521).
            v.trans_env   = 1.0f;
            v.trans_decay = 0.99539f;   // T60 ≈ 30 ms
            v.trans_gain  = 3.00f;
            v.trans_a_lo  = 0.130f;     // ~1.1 kHz HP corner
            v.trans_a_hi  = 0.420f;     // ~4.3 kHz LP corner → stick contact
            v.trans_lp_lo = v.trans_lp_hi = 0.0f;
        } else if (m_preset_idx == k_RackTom) {
            // DATA-DRIVEN from rock-rack-tom-1.wav.  The first cut put this
            // burst at 1.3-5 kHz on the assumption that "brighter stick" means
            // treble; measuring the reference's 0-30 ms window says otherwise —
            // it carries 12.0 % of its attack energy in 300 Hz-1 kHz and only
            // 2.7 % in 1-6 kHz, i.e. the contact of a stick on a tuned tom head
            // is mostly a MID thwack, not sizzle.  A 1.3 kHz HP corner filtered
            // out the very band that matters, so the band moved down here.
            //
            // The GAIN, though, must stay small — this is a limiter-pumping
            // trap, and chasing that 12 % with level walks straight into it.
            // Swept against the master stage: gain 24 measured WORSE in the
            // very band it was raised for (300 Hz-1 kHz: 1.95 % at gain 5,
            // 2.21 % at gain 0) because a burst that large pins the limiter,
            // which then ducks the whole hit and recovers into a swell — the
            // render peaked 60 ms AFTER the strike and its first 20 ms sat at
            // rms 0.25 against 0.48 with the burst switched off.  Gain 2.5 is
            // the last value that still decays monotonically.  Pass 30's rule
            // applies unchanged: a limited bus cannot give you level.
            v.trans_env   = 1.0f;
            v.trans_decay = 0.99539f;   // T60 ≈ 30 ms
            v.trans_gain  = 2.5f;
            v.trans_a_lo  = 0.030f;     // band ≈ 250 Hz …
            v.trans_a_hi  = 0.135f;     // … to 1.15 kHz — the measured thwack band
            v.trans_lp_lo = v.trans_lp_hi = 0.0f;
            // The swept boom is this preset's fundamental, and booms in this
            // engine are absolute Hz (which is why the kicks audit as "Note
            // inert").  Fold the note ratio into boom_tune — the sweep formula
            // already multiplies by it — so RackTom keeps tracking its Note
            // instead of joining that list.  MUST sit after PartialReset(),
            // which resets boom_tune to 1.0.
            v.boom_tune = exp2f(((float)note - 53.0f) * (1.0f / 12.0f));
            // (The pitch-envelope restore this glide depends on lives in the
            // shared boom restore block above, gated to 808Sub + RackTom.)
        }

        // ── Tom/membrane strike knobs (REFERENCE-ANCHORED) ───────────────────
        // VlMllRes/VlMllStf were inert on the tom family: the global mapping
        // only modulates the (masked) mallet exciter and noise attack.  Wire
        // them to the SLAP — the trans_* burst — with the same semantics as
        // the kernel/kick: VlMllRes = velocity-weighted slap PROMINENCE (adds
        // a hand/stick slap even on presets that ship without one), VlMllStf =
        // slap SHARPNESS (brighter + shorter when up, rounder + longer down).
        // Every tom ships VlMllRes/VlMllStf = 0 → deltas are exactly 0 →
        // no-ops → shipped presets stay byte-identical.
        if (kPresetEngine[m_preset_idx] == ENGINE_MEMBRANE &&
            m_preset_idx != k_Kick2 && m_preset_idx != k_808Sub &&
            m_preset_idx != k_KickDrum) {
            const float tvq  = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
            const float d_tvr = (float)m_params[k_paramVlMllRes] * 0.01f - m_snare_vlres_ref;
            const float d_tvs = (float)m_params[k_paramVlMllStf] * 0.01f - m_snare_vlstf_ref;
            if (d_tvr > 0.001f || d_tvr < -0.001f) {
                if (v.trans_env > 0.0005f) {
                    // Preset ships a stick/slap layer (e.g. AcTom): scale it.
                    v.trans_gain = fminf(8.0f, v.trans_gain *
                                   knob_exp2(3.0f * d_tvr * (0.4f + 0.6f * tvq)));
                } else if (d_tvr > 0.0f) {
                    // No shipped layer: raise a velocity-weighted hand-slap
                    // burst (~1-4.6 kHz band, T60 ≈ 21 ms).
                    v.trans_env   = fminf(2.0f, d_tvr * (0.8f + 1.9f * tvq));
                    v.trans_decay = 0.99340f;
                    v.trans_gain  = 3.20f;
                    v.trans_a_lo  = 0.120f;
                    v.trans_a_hi  = 0.450f;
                    v.trans_lp_lo = v.trans_lp_hi = 0.0f;
                }
            }
            if ((d_tvs > 0.001f || d_tvs < -0.001f) && v.trans_env > 0.0005f) {
                float tsnap = d_tvs * (0.5f + 0.5f * tvq);
                v.trans_a_lo = fmaxf(0.05f, fminf(0.45f, v.trans_a_lo * knob_exp2(1.2f * tsnap)));
                v.trans_a_hi = fmaxf(0.15f, fminf(0.80f, v.trans_a_hi * knob_exp2(0.8f * tsnap)));
                float one_m = (1.0f - v.trans_decay) * knob_exp2(1.2f * tsnap);
                v.trans_decay = fmaxf(0.98500f, fminf(0.99950f, 1.0f - one_m));
            }
        }

        // ── Kick "thump" (beater-impact punch) ───────────────────────────────
        // The kick family's boom gives the sub, but the mallet knobs did almost
        // nothing (the mallet click is a tiny high tick, not a mid punch), so
        // there was no way to dial in "thump".  Wire MlltRes (amount) + MlltStif
        // (snap/pitch) to a fast pitch-dropping mid sine punch (~500→150 Hz),
        // REFERENCE-ANCHORED: at the shipped knob values the thump is exactly
        // zero → the shipped kicks render bit-identical ("perfect boom" kept),
        // and turning the mallet knobs UP adds the punch the user expected.
        v.thump_env = 0.0f; //  Explicitly initializing v.thump_env = 0.0f; at the start of the block ensures the thump is cleanly silenced when the voice is reused or when thump is disabled.
        float mr = 0.0f;
        float st = 0.0f;
        float d_mr = 0.0f;
        float d_st = 0.0f;
        float thump_amt = 0.0f;
        float thump_hz = 0.0f;
        if (m_preset_idx == k_Kick2 || m_preset_idx == k_808Sub ||
            m_preset_idx == k_KickDrum) {
            mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes]  * 0.001f));
            st = fmaxf(0.01f,fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
            d_mr = mr - m_modal_mltres_ref;    // anchored: 0 at shipped value
            d_st = st - m_modal_stiff_ref;
            // Only positive deltas add thump (turning the knob UP = more punch).
            thump_amt = fmaxf(0.0f, 1.35f * d_mr + 0.85f * d_st);
            // VlMllRes → velocity-weighted thump PROMINENCE (HW: VlMllRes was
            // inert on kicks — the global mapping only touches the masked
            // mallet).  Same semantics as the kernel's hit-prominence knob:
            // raising it adds punch that grows with how hard you strike, so
            // accents land harder; it also works WITHOUT MlltRes dialed in.
            // Negative values scale down whatever thump the Mllt knobs set.
            const float kvq  = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
            const float d_kvr = (float)m_params[k_paramVlMllRes] * 0.01f - m_snare_vlres_ref;
            const float d_kvs = (float)m_params[k_paramVlMllStf] * 0.01f - m_snare_vlstf_ref;
            // Depth widened (HW: "still too subtle"): the ceiling below used to
            // clamp at 1.6, which full travel already reached at 1.63, so the
            // top of the knob was flat.  Measured knob audibility (RMS of the
            // difference between knob-0 and knob-max renders, relative to the
            // signal) went 808Sub -9.6 -> -6.6 dB and KickDrum -10.2 -> -5.6 dB.
            if (d_kvr > 0.001f)       thump_amt += d_kvr * (0.90f + 1.80f * kvq);
            else if (d_kvr < -0.001f) thump_amt *= knob_exp2(2.0f * d_kvr);
            // ── VlMllRes TRADES boom for punch, it does not just add punch ────
            // HW: "'thump' is not really increased."  The layer was being armed
            // correctly all along (measured: thump_env 1.6 at 115 Hz on all
            // three kicks) — the problem is that the kick bus is normalised
            // downstream, first by a hard clip (now removed) and then by the
            // master limiter, so ADDING a layer to an already-limited bus comes
            // straight back out and the knob measured 0.98-1.02x.
            //
            // Level is the one thing a limited bus cannot give you; BALANCE is
            // free.  So the knob now takes sub away as it adds punch, which is
            // also what the control means musically — a beater-forward kick has
            // less body, not more of everything.  Measured on the 100-400 Hz vs
            // 30-90 Hz band ratio (plain RMS is blind to this — the CLAUDE.md
            // "use a band metric on the kick" lesson), full travel moves the
            // ratio several-fold while total level barely moves, which is
            // exactly what "the knob does something" sounds like.
            if (d_kvr > 0.001f) {
                v.boom_mix = fmaxf(0.0f, v.boom_mix * (1.0f - 0.70f * d_kvr));
            } else if (d_kvr < -0.001f) {
                // Knob down = body-forward: give the sub back, up to +40 %.
                v.boom_mix = fminf(1.50f, v.boom_mix * (1.0f - 0.40f * d_kvr));
            }
            // VlMllStf contributes a knock of its own (stiff beater tip =
            // audible contact), so the knob is live standalone — without this
            // it only SHAPES a thump raised by the other knobs and audits NO
            // EFFECT when swept alone.  Depth widened and the DOWN direction
            // added (HW: VlMllStf measured -23 dB / -37 dB = inert on Kick2):
            // a soft beater is not "no knock", it is a rounder, weightier one,
            // so downward trades the knock for body instead of removing both.
            if (d_kvs > 0.001f)       thump_amt += d_kvs * 1.40f * (0.4f + 0.6f * kvq);
            else if (d_kvs < -0.001f) v.boom_mix = fminf(1.50f,
                                          v.boom_mix * (1.0f - 0.45f * d_kvs));
            // MlltStif BELOW the shipped value was dead on the kick family: the
            // thump is clamped at 0 from below, so on Kick2/KickDrum (which ship
            // MlltStif at 0.60/0.70) the whole lower half of the knob did
            // nothing.  A softer beater is a slower contact, so soften it the
            // way the physics does — stretch the boom's onset ramp, giving a
            // rounded felt-mallet "whoomp" instead of a struck attack.  Only the
            // downward branch (d_st < 0); Δ=0 is untouched → byte-identical.
            if (d_st < -0.001f) {
                v.boom_attack_inc = fmaxf(0.00008f,
                                    v.boom_attack_inc * knob_exp2(2.0f * d_st));
            }
            if (thump_amt > 0.001f) {
                // MlltStif also shifts the punch pitch (higher = snappier knock,
                // lower = rounder thud).  Bottom-of-drop ~70-260 Hz around 115 Hz
                // so the added energy sits in the 120-250 Hz "thump" band, below
                // the bright mallet click and above the boom's sub.
                // VlMllStf → velocity-weighted SNAP: shifts the punch pitch up
                // and shortens its ring as you hit harder (stiff beater tip).
                float snap = d_kvs * (0.4f + 0.6f * kvq);
                thump_hz = fmaxf(55.0f, fminf(340.0f,
                           115.0f * knob_exp2(1.8f * d_st + 1.4f * snap)));
                v.thump_inc   = (M_TWOPI * thump_hz) * inverse_default_sample_rate;
                v.thump_env   = fminf(2.8f, thump_amt);
                v.thump_amp0  = v.thump_env;
                // Base T60 ≈ 58 ms — a punchy attack, not a tone.  snap>0
                // widens (1-decay) → shorter/snappier; snap<0 rounder/longer.
                float one_minus_td = 0.00240f * knob_exp2(1.2f * snap);
                v.thump_decay = fmaxf(0.99000f, fminf(0.99920f, 1.0f - one_minus_td));
                v.thump_phase = 0.0f;
            }

            // ── Kick knob-design (REFERENCE-ANCHORED) ────────────────────────
            // 808Sub/KickDrum use the empty default modal config (mode_count 0),
            // so the shared modal routing above is skipped ENTIRELY and every
            // modal knob was dead; even on Kick2 the modal bank is inaudible
            // under the boom.  The kick's real voice IS the boom oscillator, so
            // route the dead knobs straight to the boom.  Each is a delta from
            // the shipped knob value → the shipped kicks render bit-identical.
            // Dkay (coarse) + Rel (fine) → boom decay LENGTH.  boom_decay is a
            // near-1 per-sample multiplier and T60 ∝ 1/(1−decay); scale (1−decay)
            // by 1/len so knob-up = longer boom.
            float dkay_n = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
            float rel_n  = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel]  * 0.05f));
            float klen   = knob_exp2(3.0f * (dkay_n - m_modal_dkay_ref)
                                + 2.2f * (rel_n  - m_modal_rel_ref));
            if (klen < 0.999f || klen > 1.001f) {
                float one_minus = (1.0f - v.boom_decay) / fmaxf(0.15f, fminf(6.0f, klen));
                v.boom_decay = fmaxf(0.99000f, fminf(0.99995f, 1.0f - one_minus));
            }
            // Mterl → boom body WEIGHT (drumhead density → how much sub).
            // Downward the curve now runs all the way to SILENCE at the knob
            // floor (quadratic in norm/ref, continuous with ×1 at the ref), so
            // MlltRes-up + Mterl-down gives a thump-only kick with almost no
            // boom (HW request).  Upward keeps the gentle 2^(1.2·Δ) lift.
            float kmn  = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
            float d_kmt = kmn - m_modal_mterl_ref;
            if (d_kmt < -0.001f || d_kmt > 0.001f) {
                float bw;
                if (d_kmt < 0.0f && m_modal_mterl_ref > 0.001f) {
                    float t = fmaxf(0.0f, kmn / m_modal_mterl_ref);   // 1 at ref → 0 at floor
                    bw = t * t;
                } else {
                    bw = knob_exp2(1.9f * d_kmt);
                }
                v.boom_mix = fmaxf(0.0f, fminf(1.50f, v.boom_mix * bw));
            }
            // TubRad → boom base TUNE (bigger shell = lower resting pitch).
            // KickDrum/808Sub recompute boom_inc every sample, so the tune must
            // ride in boom_tune (applied inside those sweep formulas); Kick2 has
            // a fixed boom_inc, so bake the tune in directly.
            float ktn  = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
            float d_ktr = ktn - m_modal_tubrad_ref;
            if (d_ktr < -0.001f || d_ktr > 0.001f) {
                v.boom_tune = fmaxf(0.40f, fminf(2.00f, knob_exp2(-1.0f * d_ktr)));
                if (m_preset_idx == k_Kick2) v.boom_inc *= v.boom_tune;
            }
            // Inharm → boom pitch-DROP depth ("808 dive").  Scales the pitch_env
            // sweep depth: strongest on 808Sub (the pitch-sweep kick, where the
            // boom_inc formula reads pitch_env_amt directly).  Kick2 (fixed boom)
            // and KickDrum (calibrated 90→55 Hz sweep) keep their "perfect" boom
            // untouched, so Inharm is deliberately light there.
            float kinh  = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramInharm] * 0.01f));
            float d_kih = kinh - m_modal_inharm_ref;
            // Exponential, not linear.  The old `max(0.05, 1 + 4.5·d)` was
            // written when Inharm could only go UP from a floored 0, so its
            // downward half was never reachable and never mattered.  Under the
            // bipolar range (pass 36) it does: linear, the factor hits its 0.05
            // floor at d = −0.21, so everything below Inharm −30 measured
            // IDENTICALLY (47.8 Hz start) and ~70 % of the new travel was dead.
            // `knob_exp2` spreads the whole range and cannot go negative —
            // 2^(2.5·d) reaches 0.18 at d = −1 and 5.7 at d = +1, keeping the
            // top end where the linear curve had it.  knob_exp2(0) is exactly
            // 1.0, so the anchor — and byte-identity — is preserved.
            if (d_kih < -0.001f || d_kih > 0.001f)
                v.pitch_env_amt = fmaxf(0.0f, v.pitch_env_amt * knob_exp2(2.5f * d_kih));

            // ── Velocity → BALANCE trade (Kick2 ONLY) ────────────────────────
            // HW: "velocity seems to increase the decay but not the hit."
            // Measured cause: Kick2's attack-to-tail ratio is 0.93 / 0.93 / 0.95
            // at velocity 127 / 64 / 30 — flat.  The master limiter pins the
            // level, so a harder strike cannot get LOUDER; it only stays above
            // the threshold longer, which is exactly what reads as "more decay,
            // same hit".  Pass 30's rule: a limited bus cannot give you level,
            // but balance is free.  So velocity now moves the BALANCE — a soft
            // hit gets rounder and boomier, a hard one stays tight — which is
            // what makes the difference audible as impact.
            //
            // SCOPED TO Kick2 deliberately.  This block is shared with 808Sub
            // and KickDrum, and both of those are HW-approved as they stand;
            // the user asked for Kick2 specifically and asked that "Kick"
            // (= KickDrum, preset 20) not be touched.  One `if` keeps that
            // promise — do not widen it without a listen on the other two.
            //
            // Anchored at FULL velocity: `current_velocity` is linear here (the
            // quadratic curve is BrshSnr-only), so vd == 0 at MIDI 127 and the
            // hardest hit is bit-for-bit what it was before.  Everything below
            // full velocity is a deliberate change — a render at velocity 100
            // sits at vd = 0.21.
            if (m_preset_idx == k_Kick2) {
                const float vd = fmaxf(0.0f, fminf(1.0f, 1.0f - v.current_velocity));
                v.boom_mix        = fminf(1.50f, v.boom_mix * (1.0f + 0.35f * vd));
                v.boom_attack_inc = fmaxf(0.00008f, v.boom_attack_inc * (1.0f - 0.50f * vd));
            }
            // HitPos → beater CLICK (harder beater = brighter contact tick).  The
            // kick doesn't otherwise use the trans_* burst; borrow it for a short
            // bright tick that only appears as HitPos rises above the shipped value.
            // fmaxf(-1.0f, …) for consistency with the other anchored HitPos
            // reads, but note the `d_khp > 0.001f` guard below: the beater
            // click only ever ADDS as HitPos rises, so the negative half is
            // inert here BY DESIGN, not by clamping.
            float khit  = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramHitPos] * 0.01f));
            float d_khp = khit - m_modal_hitpos_ref;
            if (d_khp > 0.001f) {
                v.trans_env   = fminf(2.0f, d_khp * 3.8f);
                v.trans_decay = 0.98500f;   // T60 ≈ 9 ms — a click, not a tone
                v.trans_gain  = 1.60f;
                v.trans_a_lo  = 0.20f;      // ~1.8 kHz HP corner
                v.trans_a_hi  = 0.55f;      // ~6 kHz LP corner → beater tick
                v.trans_lp_lo = v.trans_lp_hi = 0.0f;
            }
        }
}

    inline void NoteOff(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];

            // Find the voice playing this note that hasn't already been released
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;

                // SNARE: do NOT release the noise envelopes.  Real snare wires
                // ring freely once the stick leaves the head, so the envelopes
                // stay in ENV_DECAY (sustain 0) and die at the NzRs-governed
                // natural rate, calibrated against the reference samples
                // (~0.2-0.4 s buzz tails); the Rel-rate release choked the
                // buzz to ~26 ms T60.  NOTE: this is a VOICING choice now, not
                // a same-tick workaround — FastEnvelope::release() defers a
                // release that arrives during the attack (ENV_ATTACK_REL), so
                // every other engine can call it safely.  See the same-tick
                // gotcha in CLAUDE.md and T37.
                const EngineType ve = kPresetEngine[m_preset_idx];
                if (ve != ENGINE_SNARE) {
                    v.exciter.noise_env.release();
                    v.exciter.noise_env_hi.release();
                }
                // KS: release master_env so voice decays via Dkay release rate.
                // Modal engines: master_env holds at 1.0 — modal T60 controls ring.
                // NOISE engines: master_env also holds at 1.0; noise_env release
                // (controlled by Rel parameter) fades the output instead.  This
                // gives Clap/Shaker a proper "tschaa" tail rather than an instant cut.
                if (ve == ENGINE_KS) {
                    v.exciter.master_env.release();
                }
            }
        }
    }

    inline void GateOff() {
        // The internal Drumlogue sequencer releases the UI note
        NoteOff(m_ui_note);

        // Voice-allocation policy for REPEATED triggers of the same preset.
        // The Drumlogue fires gate_on+gate_off in the same tick, so whatever
        // next_voice_idx is left at here decides where the NEXT hit lands.
        //
        // SUSTAINED engines (PLATE = cymbal/gong/ride/hi-hat/bell, BAR = marimba/
        // vibe/...) must NOT reset: leaving next_voice_idx alone keeps the
        // round-robin (…1,2,3,0…) so a fast second hit lands on a DIFFERENT voice
        // and STACKS over the still-ringing first hit — essential for cymbal rolls
        // and overlapping swells (HW: "important for cymbals").
        //
        // MEMBRANE (kick/tom/conga/bongo), SNARE and NOISE now ALSO stack: HW
        // feedback wanted fast repeats to overlap (drum-roll tails, flams) rather
        // than choke one another to a single voice.  The old low-end-build-up
        // worry is bounded by the master limiter, and NUM_VOICES=4 caps the pile.
        // Heavy engines keep their restriction: ENGINE_CYMBAL is separately capped
        // by m_cym_poly (the Poly knob), and the Timpani/Taiko kernel runs its own
        // 2-kettle path.  Only ENGINE_KS stays mono — reusing one string slot
        // avoids the same-pitch beating that made overlapping plucks buzz.
        // ENGINE_KS used to be pinned here (`next_voice_idx = NUM_VOICES - 1`),
        // which made it hard mono.  Pass 41 moved that decision into NoteOn,
        // where the incoming note is actually known: a repluck of the SAME
        // note reuses its slot (no beating, and physically what a plectrum
        // does), a different note stacks.  GateOff cannot make that call — it
        // only sees the note being released — so it no longer tries.
    }

    inline void AllNoteOff() {
        // Panic button: aggressively release everything
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].is_releasing = true;
            state.voices[i].exciter.noise_env.release();
            state.voices[i].exciter.noise_env_hi.release();
            state.voices[i].exciter.master_env.release();
            // Cymbal voices have no release env by design (gate_off must not
            // choke the tail); panic uses the click-free output fade instead
            // (tau 8 ms -> voice fully off ~55 ms after AllNoteOff).
            state.voices[i].cymbal.fadeMul = cym_env_mul(0.008f, k_dsp_sample_rate);
        }
    }

    inline void PitchBend(uint16_t bend) {
        // MIDI pitch bend: 0-16383, centre = 8192.
        // Map to ±2 semitones (standard default bend-sensitivity range).
        // PitchBend is not in the audio hot loop, so we use powf() for accuracy.
        // fasterpowf(2.0f, 0.0f) ≈ 0.9714 (not 1.0) due to fasterlog2f(2.0f)≈1.057
        // approximation error cascading through fasterpow2f(0.0f), which would cause
        // every centre-bend to quietly detune the voice downward by ~50 cents.

        if (bend == pitch_centre) {
            m_pitch_bend_mult = 1.0f;
        } else {
            float semitones = (float)(bend - pitch_centre) * (2.0f / (float)pitch_centre);
            // A higher pitch requires a shorter delay line → negate the exponent.
            m_pitch_bend_mult = powf(2.0f, -semitones * 0.08333333333f);  // approx 1 / 12
        }

        // Apply immediately to every active voice.
        // Clamping to [2, DELAY_BUFFER_SIZE-2] prevents buffer overrun on low notes
        // bent upward (e.g. MIDI 0 at −2 st → delay ≈ 6585 samples > buffer).
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            if (!v.is_active) continue;
            apply_pitch_bend_to_voice(v);
        }
    }

    // ==============================================================================
    // 5. The Core Physics (Executed per-voice, per-sample)
    // ==============================================================================

    // Processes a single sample through the Waveguide
    inline float process_waveguide(WaveguideState& wg, float exciter_input) {
         auto schroeder_stage = [](float x, float* buf, uint8_t& idx, uint8_t len, float g) {
            float d = buf[idx];
            float v = x + g * d;
            float y = -g * v + d;
            buf[idx] = v;
            idx = (uint8_t)((idx + 1u) % len);
            return y;
         };
        // 1. Calculate the read pointer position for exact pitch
        float read_idx = (float)wg.write_ptr - wg.delay_length;

        // delay_length is clamped to [2, DELAY_BUFFER_SIZE-2] so read_idx ≥ −(DELAY_BUFFER_SIZE-2).
        // One addition of DELAY_BUFFER_SIZE always brings it into [2, DELAY_BUFFER_SIZE).
        // Use 'if' rather than 'while' — the loop can execute at most once and
        // the extra branch prediction overhead of 'while' is never justified.
        if (read_idx < 0.0f) {
            read_idx += (float)DELAY_BUFFER_SIZE;
        }

        // Explicitly mask BOTH indices to guarantee we never read out-of-bounds memory
        uint32_t idx_A = ((uint32_t)read_idx) & DELAY_MASK;
        uint32_t idx_B = (idx_A + 1) & DELAY_MASK;
        float frac = read_idx - (float)((uint32_t)read_idx);

        // Horner-form linear interpolation: 1 multiply + 2 adds instead of 2 multiplies + 1 add.
        float delay_out = wg.buffer[idx_A] + frac * (wg.buffer[idx_B] - wg.buffer[idx_A]);

        // 3a. Dispersion (Allpass Filter) — applied BEFORE the loss filter.
        // Physical order: AP models wave propagation (medium property); LP models
        // boundary absorption (reflection loss).  AP first ensures high-frequency
        // phase stretching acts on the full-amplitude signal, then LP applies loss.
        // With LP-first the AP acts on an already attenuated signal, reducing the
        // audible inharmonicity at high frequencies (wrong direction for stiff strings).
        //
        // model_ap_base: a per-model baseline dispersion coefficient that gives each
        // physical model (Beam, Square, Plate, etc.) a distinct inharmonic character
        // even when the user sets Inharm=0.  Summed with ap_coeff (from Inharm knob)
        // and clamped to [0, 0.99) to prevent allpass instability.
        float ap = fminf(0.99f, wg.ap_coeff + wg.model_ap_base);
        float ap_out = (ap * delay_out) + wg.ap_x1 - (ap * wg.ap_y1);
        wg.ap_x1 = delay_out;
        wg.ap_y1 = ap_out;

        // 3b. Loss Filter (1-pole Lowpass) — applied AFTER dispersion.
        // wg.lowpass_coeff was pre-calculated in setParameter()
        // Loss filter: bypassed for metallic rods (bypass_loop_lp=true) to preserve
        // high harmonics. T60 is then controlled entirely by loss_g_dc × feedback_gain.
        // Technique: pure-gain resonator per Smith, "Physical Audio Signal Processing" (2010).
        float filtered_out;
        if (!wg.bypass_loop_lp) {
            wg.z1 = (ap_out * wg.lowpass_coeff) + (wg.z1 * (1.0f - wg.lowpass_coeff));
            filtered_out = wg.z1;
            if (wg.double_lp) {
                wg.z2 = (wg.z1 * wg.lowpass_coeff) + (wg.z2 * (1.0f - wg.lowpass_coeff));
                filtered_out = wg.z2;
            }
        } else {
            filtered_out = ap_out;  // passthrough: hf=0, loss is pure gain only
        }
        if (wg.diffuser_mix > 0.0001f) {
            float y = filtered_out;
            y = schroeder_stage(y, wg.diffuser_buf1, wg.diffuser_i1, 13, wg.diffuser_g);
            y = schroeder_stage(y, wg.diffuser_buf2, wg.diffuser_i2, 19, wg.diffuser_g);
            y = schroeder_stage(y, wg.diffuser_buf3, wg.diffuser_i3, 29, wg.diffuser_g);
            y = schroeder_stage(y, wg.diffuser_buf4, wg.diffuser_i4, 41, wg.diffuser_g);
            filtered_out = (filtered_out * (1.0f - wg.diffuser_mix)) + (y * wg.diffuser_mix);
        }
        // 4. Feedback & Exciter Addition
        // wg.feedback_gain is our "Decay" time
        float hf = ap_out - filtered_out;
        float loss_shaped = (filtered_out * wg.loss_g_dc) + (hf * wg.loss_g_hf);
        float new_val = exciter_input + (loss_shaped * wg.feedback_gain * wg.phase_mult);

        // 5. Write back to the delay line and advance the pointer
        wg.buffer[wg.write_ptr] = new_val;
        wg.write_ptr = (wg.write_ptr + 1) & DELAY_MASK;

        // Return new_val (exciter + filtered feedback) rather than delay_out.
        // This matches the f84af87 behaviour: the exciter signal passes through
        // immediately on frame 0 so samples and mallet strikes are audible at
        // once, not after one full delay-line round-trip (~4 ms at note 60).
        // The fundamental pitch is still determined by delay_length; the change
        // only affects the output tap point, not the feedback loop stability.
        return new_val;
    }

    // Processes the Exciter (Generates the initial "strike" burst).
    // PCM sample playback removed (see NoteOn) — pure synthesis only.
    inline float process_exciter(ExciterState& ex) {
        float out = 0.0f;

        // Noise: computed but NOT fed into the waveguide here.
        // Storing in noise_out_sample separates percussion broadband texture
        // (snare buzz, cymbal wash) from the pitched resonator ring.
        // processBlock mixes it in parallel with the resonator output.
        // Exception: tube models (phase_mult=-1) also receive noise into the waveguide
        // to sustain the oscillation — that injection happens in processBlock.
        ex.noise_out_sample = 0.0f;
        float noise_env_low = ex.noise_env.process();
        float noise_env_high = ex.noise_env_hi.process();
        if (noise_env_low > 0.001f || noise_env_high > 0.001f) {
             float raw_noise = ex.noise_gen.process();
             float raw_noise_unf = raw_noise; // keep true unfiltered branch for high burst
             raw_noise = ex.noise_filter.process(raw_noise);

            // Dual-noise-burst architecture:
            //   - low band: body/snap tail (slow noise_env)
            //   - high band: fast click/hiss burst (snappy noise_env_hi)
            // BOTH bands derive from the SVF-coloured noise so NzFltr/NzFltFrq
            // govern the whole burst.  The old code split the high band from the
            // UNFILTERED source, so the dominant sizzle branch ignored the user
            // filter entirely (and the split-corner coupling made the cutoff act
            // in reverse).  noise_hi_lp_coeff is the per-preset body/sizzle
            // split corner; the hat path below still uses raw_noise_unf because
            // hat_filter is its own dedicated centroid control.
            ex.noise_lp_state += 0.15f * (raw_noise - ex.noise_lp_state);
            float low = ex.noise_lp_state;
            ex.noise_hi_lp_state += ex.noise_hi_lp_coeff * (raw_noise - ex.noise_hi_lp_state);
            float high = raw_noise - ex.noise_hi_lp_state;
            // noise_band_mix is clamped at its write sites (NoteOn / PartialReset),
            // so no per-sample clamp is needed here.
            const float mix = ex.noise_band_mix;
            float low_part = low * (1.0f - mix) * noise_env_low;
            float high_part = high * mix * 1.35f * noise_env_high;
            if (mix > 0.80f) {
                // Hi-hat family: dedicated BP biquad for centroid control near
                // 7 kHz.  Only run the SVF when the preset actually uses it.
                float hat_src = ex.use_hat_filter ? ex.hat_filter.process(raw_noise_unf)
                                                  : raw_noise;
                high_part = hat_src * mix * 1.35f * noise_env_high;
            }
            float noise_sum = (low_part + high_part) * ex.noise_decay_coeff;
            if (ex.snare_wire_mix > 0.001f) {
                // Phase-B crack burst: broadband onset before wire resonance engages.
                // snare_crack_gain (default 1.0, VlMllRes-controlled) scales the
                // snap: 0 = pure buzz, high = a cracky stick attack.
                float crack_burst = high * noise_env_high * (1.0f - ex.wire_onset_env) * 0.90f * ex.snare_crack_gain;
                noise_sum += crack_burst;

                // 3-band parallel wire resonators. Each band is an IIR resonator driven
                // by the gated noise input. Parallel topology (vs series) avoids inter-band
                // coupling that would tonalize the rattle.
                // Source: Cook, "Real Sound Synthesis for Interactive Applications" (2002).
                float wire_input = noise_sum * ex.wire_onset_env;

                float wa = wire_input + (ex.snare_wire_a1  * ex.snare_wire_z1)  - (ex.snare_wire_a2  * ex.snare_wire_z2);
                ex.snare_wire_z2 = ex.snare_wire_z1;
                ex.snare_wire_z1 = wa;

                float wb = wire_input + (ex.snare_wire_a1b * ex.snare_wire_z1b) - (ex.snare_wire_a2b * ex.snare_wire_z2b);
                ex.snare_wire_z2b = ex.snare_wire_z1b;
                ex.snare_wire_z1b = wb;

                float wc = wire_input + (ex.snare_wire_a1c * ex.snare_wire_z1c) - (ex.snare_wire_a2c * ex.snare_wire_z2c);
                ex.snare_wire_z2c = ex.snare_wire_z1c;
                ex.snare_wire_z1c = wc;

                ex.wire_onset_env = fminf(1.0f, ex.wire_onset_env + ex.wire_onset_attack);

                // Mix: A gives body crack, B gives mid buzz, C gives high sizzle.
                float wire_rattle = (0.50f * wa) + (0.80f * wb) + (0.60f * wc);
                noise_sum = (noise_sum * (1.0f - ex.snare_wire_mix)) + (wire_rattle * ex.snare_wire_mix * 0.40f);
            }
            ex.noise_out_sample = noise_sum;
        }

        // 3. The Modal Mallet Strike
        // Two cascaded 1-pole LPs shape the strike spectrum:
        //   LP1 (mallet_stiffness): controls attack sharpness — high = bright, low = round.
        //   LP2 (mallet_res_coeff): controls mallet body — high = bright, low = dark (MlltRes).
        // Gate: skip both LP updates (and the * 15 add) once the second pole has fully
        // decayed.  Without this gate the filters run for the full voice lifetime, leaking
        // CPU every sample and risking denormal (subnormal) values on non-FTZ hardware.
        // Threshold 1e-6f is well above the sub-normal range (~1.2e-38f) and inaudible.
        //
        // NzMix blend: mallet scales inversely with noise_decay_coeff so NzMix acts as a
        // true crossfade — NzMix=0 → full mallet (string/bar), NzMix=100 → silent mallet
        // (pure noise). At intermediate values both contribute proportionally.
        if (ex.current_frame == 0 || ex.mallet_lp2 > 1e-6f) {
            float mallet_impulse = (ex.current_frame == 0) ? 1.0f : 0.0f;
            ex.mallet_lp  = (mallet_impulse * ex.mallet_stiffness) + (ex.mallet_lp  * (1.0f - ex.mallet_stiffness));
            ex.mallet_lp2 = (ex.mallet_lp   * ex.mallet_res_coeff) + (ex.mallet_lp2 * (1.0f - ex.mallet_res_coeff));
            out += ex.mallet_lp2 * 15.0f * (1.0f - ex.noise_decay_coeff);
        }

        // CRITICAL FIX: Increment time AT THE VERY END so Frame 0 actually triggers
        ex.current_frame++;

        return out;
    }

    // ==============================================================================
    // 6. The Master Audio Loop (Called by Drumlogue OS)
    // ==============================================================================
    //
    // RENDER_STAGE: Incremental isolation for hardware silence debugging.
    // Set in config.mk:  UDEFS += -DRENDER_STAGE=1
    //
    //   Stage 1 — Raw exciter only (mallet impulse / PCM sample, no waveguide, no env, no FX)
    //             If silent: gate callbacks or voice activation are broken.
    //   Stage 2 — + Waveguide resonators
    //             If silent: delay_length or feedback_gain is 0 on ARM.
    //   Stage 3 — + master_env fade + squelch (Phase 18 fix)
    //             If silent: pre-advance fix not working on ARM; revert to exciter-only.
    //   Stage 4 — + Tone EQ + master filter + overdrive (full render, default)
    //             If silent: tone or FX path issue.
    // ==============================================================================

    inline void processBlock(float* __restrict main_out, size_t frames) {

        // Clear the output buffer — mandatory; without this the += accumulation
        // can corrupt with stale or NaN data from the previous block.
        for (size_t i = 0; i < frames * 2; ++i)
            main_out[i] = 0.0f;

#ifdef UNIT_TEST_DEBUG
        // Reset probes each block so callers that check them after a block with
        // no active voices (e.g. after Reset()) correctly observe 0, not the
        // stale value from the previous block.
        ut_exciter_out = 0.0f;
        ut_delay_read  = 0.0f;
        ut_voice_out   = 0.0f;
#endif

        // Hoist tone read outside all loops — avoids UI/audio-thread race.
        const float tone_val = state.tone;
        // Every preset ships Tone=0: skip the tilt-EQ one-pole entirely then
        // (its output path is already a no-op at 0; this also skips the LP
        // state update).  The LP re-converges within ~10 samples when the
        // knob moves off zero — inaudible under the knob's own change.
        const bool tone_active = (tone_val != 0.0f);

        // ── Dense modal-drum kernel path (Timpani/Taiko) ───────────────────
        // Renders the coupled resonator bank behind the approved standalone
        // references and runs its OWN master stage: Tone tilt → master filter
        // → Gain (re-anchored so the shipped row is transparent) → transparent
        // peak limiter.  The legacy soft-clip is bypassed on purpose: it
        // compresses the strike back into the body (crest ≈ 1) — the exact
        // "rough / synthy hit" the HW comparison flagged.  All other presets
        // take the unchanged legacy path below (bit-identical output).
        if (kernel_preset_active() && m_drum_kernel.IsActive()) {
            const float drive_rel = state.master_drive / m_drum_kernel.RefDrive();
            size_t done = 0;
            while (done < frames) {
                float mono[64];
                size_t todo = frames - done;
                if (todo > 64) todo = 64;
                bool audible = m_drum_kernel.Process(mono, (int)todo);
                if (audible) {
                    for (size_t i = 0; i < todo; ++i) {
                        float x = mono[i];
                        // Stage 4a tilt EQ (same curve as the voice path).
                        if (tone_active) {
                            m_kernel_tone_lp = (x * kToneLpMix) + (m_kernel_tone_lp * (1.0f - kToneLpMix));
                            if (tone_val < zeroThreshold) {
                                x = x + (m_kernel_tone_lp - x) * (-tone_val * kInvToneCutDivisor);
                            } else {
                                x += (x - m_kernel_tone_lp) * (tone_val * kInvToneBoostDivisor);
                            }
                        }
                        x = state.master_filter.process(x);   // LowCut/Resnc
                        x *= drive_rel;                        // Gain around anchor
                        float a = fabsf(x);                    // transparent limiter
                        if (a > 0.85f) {
                            a = 0.85f + 0.15f * fastertanhf((a - 0.85f) * 6.6666667f);
                            x = (x < 0.0f) ? -a : a;
                        }
                        x = fmaxf(-0.99f, fminf(0.99f, x));
                        main_out[(done + i) * 2]     = x;
                        main_out[(done + i) * 2 + 1] = x;
                    }
                } else {
                    // Kernel silent: keep the master filter state flowing so
                    // there is no step when the next hit lands.
                    for (size_t i = 0; i < todo; ++i) {
                        float x = state.master_filter.process(0.0f);
                        x = fmaxf(-0.99f, fminf(0.99f, x));
                        main_out[(done + i) * 2]     = x;
                        main_out[(done + i) * 2 + 1] = x;
                    }
                }
                done += todo;
            }
            return;
        }

        // Release a master drive deferred behind a preset-change fade as soon
        // as the last fading voice has retired.
        if (m_pending_drive >= 0.0f) {
            bool fading = false;
            for (int i = 0; i < NUM_VOICES; ++i)
                if (state.voices[i].is_active && state.voices[i].fade_mul < 1.0f) fading = true;
            if (!fading) { state.master_drive = m_pending_drive; m_pending_drive = -1.0f; }
        }

        // ── The voice bus is MONO; only main_out[i*2] carries it ──────────────
        // Every engine here is single-channel, and Stage 4b filters the LEFT
        // sample and writes the result to BOTH channels, so anything a voice
        // accumulates into main_out[i*2+1] is overwritten before it can be
        // heard.  The voice loops therefore write the left lane only — the
        // right one is filled once, at the end.  This is not a shortcut: the
        // stereo store cost an extra load-add-store per sample PER VOICE in the
        // hottest loop in the unit, and the cymbal soft-headroom pass below was
        // spending a divide per sample on a lane nobody reads.
        // Both early returns above stay correct: the kernel path writes both
        // channels itself, and the idle path returns on an all-zero buffer.
        bool any_voice_rendered = false;
        bool any_cymbal_rendered = false;
        for (int voice_idx = 0; voice_idx < NUM_VOICES; ++voice_idx) {
            VoiceState& voice = state.voices[voice_idx];
            if (!voice.is_active) continue;

            // Engine routing: the engine LATCHED AT NoteOn, never the live
            // kPresetEngine[m_preset_idx].  Reading it live meant a preset
            // change mid-tail re-routed a ringing voice into another engine's
            // per-sample code — which, for a cymbal voice (whose exciter is
            // never advanced and so still holds an unstarted envelope at
            // current_frame 0), fired a complete unplayed attack: a measured
            // 19x burst on Cymbal -> Clap.  See T38.
            const EngineType voice_engine = (EngineType)voice.engine;
            if (voice_engine == ENGINE_REMOVED) {
                voice.is_active = false;
                continue;
            }
            any_voice_rendered = true;
            if (voice_engine == ENGINE_CYMBAL) any_cymbal_rendered = true;

            // Pre-compute model-aware coupling clamps once per block.
            // feedback_gain is constant during audio rendering, so this runs once per voice
            // per processBlock() call instead of once per sample — saves ~128 fminf() calls.
            //
            // The clamp is K=0.8 for EVERY pair, coherent or not.  A permissive
            // K=2.5 for different-pitch (phase-incoherent) pairs was tried and
            // reverted: phase incoherence lowers the AVERAGE coupling energy but
            // not the worst-case beat alignment, which still needs C ≤ 1−G, and
            // K=2.5 violates that for every G < 1 (G + C = 2.5 − 1.5G > 1) —
            // long-decay presets (Timpani, Djambe) grew exponentially.  The two
            // arms of that test had therefore been identical since the revert,
            // so the pitch-ratio comparison that selected between them (a float
            // divide per active voice per block) is gone with them.
            //
            // Stability check (Timpani worst case: g=0.958, Ptls=2):
            //   safe_cpl = min(0.25, 0.042 × 0.8) = 0.034
            float v_safe_cpl_a = 0.0f, v_safe_cpl_b = 0.0f;
            if (m_active_partials >= 16) {
                const float half_depth = m_coupling_depth * 0.5f;
                v_safe_cpl_a = fminf(half_depth, (1.0f - voice.resA.feedback_gain) * 0.8f);
                v_safe_cpl_b = fminf(half_depth, (1.0f - voice.resB.feedback_gain) * 0.8f);
            }

            // ── ENGINE_CYMBAL: self-contained dense-resonator cymbal ───────
            // Bypasses the entire exciter/KS/modal/plate machinery; it owns its
            // own excitation, decay and lifetime.  Output goes through the same
            // tilt EQ + master gain as every other engine.
            if (voice_engine == ENGINE_CYMBAL) {
                for (size_t i = 0; i < frames; ++i) {
                    float cy = cymbal_process(voice.cymbal);
                    if (voice.fade_mul < 1.0f) {   // preset-change fade-out
                        cy *= voice.fade;
                        voice.fade *= voice.fade_mul;
                        if (voice.fade < 0.001f) { voice.is_active = false; voice.cymbal.active = false; }
                    }
                    if (tone_active) {
                        voice.tone_lp = (cy * kToneLpMix) + (voice.tone_lp * (1.0f - kToneLpMix));
                        if (tone_val < zeroThreshold) {
                            cy = cy + (voice.tone_lp - cy) * (-tone_val * kInvToneCutDivisor);
                        } else {
                            const float hp = cy - voice.tone_lp;
                            cy += hp * (tone_val * kInvToneBoostDivisor);
                        }
                    }
                    // Left only — see the mono-bus note above the voice loop.
                    main_out[i * 2] += cy * state.master_gain;
#ifdef UNIT_TEST_DEBUG
                    if (voice_idx == state.next_voice_idx) ut_voice_out = cy;
#endif
                    if (!voice.cymbal.active) { voice.is_active = false; break; }
                }
                continue;  // next voice
            }

            // Hoist per-preset constants out of the per-sample loop: the
            // noise-gain family selection cannot change during a block.
            // (Cymbal/Gong route to ENGINE_CYMBAL and never reach this path;
            // the values are kept for engine-routing experiments.)
            float base_parallel_noise_gain = 5.0f;
            if (m_preset_idx == k_Triangle) {
                base_parallel_noise_gain = 7.0f;
            } else if (m_preset_idx == k_Cymbal || m_preset_idx == k_Gong) {
                // Reduce harsh noise dominance so the modal ring is more audible
                base_parallel_noise_gain = 3.5f;
            }
            // Non-KS engines never feed the KS coupling taps: zero them once
            // per block (was two stores per sample) so stale values don't leak
            // if the preset changes while a voice is active.
            if (voice_engine != ENGINE_KS) {
                voice.resA_out_prev = 0.0f;
                voice.resB_out_prev = 0.0f;
            }

            for (size_t i = 0; i < frames; ++i) {

                // ── Stage 1: Raw exciter (always executes) ─────────────────
                // Mallet impulse and/or PCM sample — the most direct signal
                // possible.  If Stage 1 is silent, the voice is never activated
                // or unit_gate_on / unit_render are not being called.
                float exciter_sig = process_exciter(voice.exciter);
                if (voice.metal_fm_depth > 0.0f && voice.metal_fm_env > silence_threshold) {
                    float fm = fastersinfullf(voice.metal_fm_phase) * voice.metal_fm_depth * voice.metal_fm_env;
                    exciter_sig += fm;
                    // Sweep effect: instantaneous modulation rate is higher at onset,
                    // then relaxes as envelope decays.
                    float sweep = 1.0f + (2.4f * voice.metal_fm_env);
                    voice.metal_fm_phase += voice.metal_fm_inc * sweep;
                    if (voice.metal_fm_phase > (M_TWOPI)) voice.metal_fm_phase -= (M_TWOPI);
                    voice.metal_fm_env *= voice.metal_fm_decay;
                }
                if (voice.reed_nl_enabled) {
                    // Lightweight asymmetric waveshaper to emulate reed contact.
                    float x = exciter_sig * voice.reed_nl_drive;
                    float y = (x >= 0.0f) ? fastertanhf(x) : (0.6f * fastertanhf(1.6f * x));
                    // Blend strength follows drive so clarinet-like presets can
                    // push odd harmonics without globally over-distorting softer voices.
                    float nl_mix = fmaxf(0.35f, fminf(0.85f, 0.20f + (0.18f * voice.reed_nl_drive)));
                    exciter_sig = ((1.0f - nl_mix) * exciter_sig) + (nl_mix * y);
                }
                float voice_out   = exciter_sig * voice.current_velocity;

                // outA kept at 0 here so the debug probe below always compiles.
                float outA = 0.0f;

                // ── Stage 2: Waveguide resonators (ENGINE_KS only) ────────
                // Non-KS engines bypass the KS delay line entirely.  The modal
                // bank (below) is their primary tonal resonator.
                if (voice_engine == ENGINE_KS) {
                    if (voice.transient_frames_left > 0 && voice.transient_frames_total > 0) {
                        float t = (float)voice.transient_frames_left * voice.transient_inv_total;
                        float decay = t * t;
                        float lp_off = voice.transient_lp_jitter * decay;
                        float ap_off = voice.transient_ap_jitter * decay;
                        voice.resA.lowpass_coeff = fmaxf(0.01f, fminf(0.999f, voice.transient_lp_base_a + lp_off));
                        voice.resB.lowpass_coeff = fmaxf(0.01f, fminf(0.999f, voice.transient_lp_base_b + lp_off));
                        voice.resA.ap_coeff = fmaxf(-0.99f, fminf(0.99f, voice.transient_ap_base_a + ap_off));
                        voice.resB.ap_coeff = fmaxf(-0.99f, fminf(0.99f, voice.transient_ap_base_b + ap_off));
                        voice.transient_frames_left--;
                    } else {
                        voice.resA.lowpass_coeff = voice.transient_lp_base_a;
                        voice.resB.lowpass_coeff = voice.transient_lp_base_b;
                        voice.resA.ap_coeff = voice.transient_ap_base_a;
                        voice.resB.ap_coeff = voice.transient_ap_base_b;
                    }

                    float safe_cpl_a = v_safe_cpl_a;
                    float safe_cpl_b = v_safe_cpl_b;
                    if (voice.pitch_env_amt > 0.0f && voice.pitch_env > silence_threshold) {
                        float sweep_st = voice.pitch_env_amt * voice.pitch_env;
                        // EXACT exp2f, not fasterpowf — this is TUNING, and the
                        // approximation is catastrophic here.  `fasterpowf(2,p)`
                        // is `fasterpow2f(p * fasterlog2f(2))`, and BOTH halves
                        // are approximate: it returns 0.971348 at p = 0 instead
                        // of 1.0.  Since the sweep converges to p = 0, the delay
                        // line was left permanently 2.9 % short — the string
                        // settled a flat HALF SEMITONE SHARP of its own note and
                        // stayed there (measured on Koto: 261.75 → 269.44 Hz for
                        // the full 6 s render, against 261.75/0.971457 = 269.44
                        // predicted).  The error is −22 to −50 cents across the
                        // whole sweep, so the bend depth was wrong too.  This is
                        // the documented knob_exp2 trap at a site that had never
                        // executed: the pitch_env clobber (pass 35) kept this
                        // branch dead, so the tuning bug hid behind it until
                        // Koto was enabled in pass 38.  Costs one exp2f per
                        // sample for the ~240 ms the sweep runs, on KS presets
                        // with a non-zero amt — Koto alone today.
                        float sweep_scale = exp2f(-sweep_st * 0.08333333333f);
                        voice.resA.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 1),
                                                                     voice.base_delay_A * m_pitch_bend_mult * sweep_scale));
                        voice.resB.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 1),
                                                                     voice.base_delay_B * m_pitch_bend_mult * sweep_scale));
                        voice.pitch_env *= voice.pitch_env_decay;
                    }

                    float tube_noise_A = (voice.resA.phase_mult < 0.0f)
                                         ? voice.exciter.noise_out_sample : 0.0f;
                    float inputA = exciter_sig + tube_noise_A + (voice.resB_out_prev * safe_cpl_a);
                    outA = process_waveguide(voice.resA, inputA);
                    float outB = 0.0f;

                    bool resB_needed = (m_active_partials >= 16) &&
                                       (state.mix_ab > 0.001f ||
                                        m_coupling_depth > 0.001f ||
                                        fabsf(voice.resB_out_prev) > 0.00003f);
                    if (resB_needed) {
                        float tube_noise_B = (voice.resB.phase_mult < 0.0f)
                                             ? voice.exciter.noise_out_sample : 0.0f;
                        float inputB = exciter_sig + tube_noise_B + (voice.resA_out_prev * safe_cpl_b);
                        outB = process_waveguide(voice.resB, inputB);
                        voice.resA_out_prev = outA;
                        voice.resB_out_prev = outB;
                    } else {
                        voice.resA_out_prev = outA;
                        voice.resB_out_prev = 0.0f;
                    }
                    float resonator_out = ((outA * (1.0f - state.mix_ab)) + (outB * state.mix_ab));
                    voice_out = resonator_out * voice.current_velocity;
                } else {
                    // Non-KS engine: exciter provides the transient attack.
                    // Pitch sweep still applies for membrane presets (kick pitch drop).
                    if (voice.pitch_env_amt > 0.0f && voice.pitch_env > silence_threshold) {
                        voice.pitch_env *= voice.pitch_env_decay;
                    }
                    // voice_out is already exciter_sig * current_velocity from Stage 1.
                }

                // Parallel noise path: noise bypasses the waveguide and mixes directly
                // into the voice output.  This preserves the broadband character that the
                // resonator would otherwise pitch-filter away (snare buzz, cymbal wash,
                // hi-hat hiss, shaker rattle).  The ×5 base factor brings noise amplitude
                // into the same ballpark as the resonator output driven by the ×15 mallet;
                // per-preset overrides are folded into base_parallel_noise_gain above.
                float parallel_noise_gain = base_parallel_noise_gain;
                // Ring-coupled noise gate for ENGINE_PLATE: noise tracks the modal ring
                // decay so both die together — user reported noise and ring as "juxtaposed".
                // noise_ring_gate starts at 1.0 on NoteOn and decays with modal_decay_1.
                // Floor of 0.15 keeps a small amount of noise-texture even in long tails.
                if (voice_engine == ENGINE_PLATE && voice.modal_pilot_enabled) {
                    parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
                    voice.noise_ring_gate *= voice.modal_decay_1;
                }
                // Noise ⇄ ring cross-modulation: ring-modulate the wash by the
                // previous sample's modal output so the noise shimmers AT the
                // ring's partial frequencies (Risset cymbal technique) instead
                // of sitting beside it as a static overlay.
                //   gate = (1−d) + d·modal — at d=0.8 only 20% of the wash is
                // static; the rest is true bipolar ring-mod.  The earlier
                // (1 + d·modal) form kept a full-strength static carrier, which
                // HW still heard as "two sounds overlaid, not mixed".
                float rm_gate = 1.0f;
                if (voice.modal_rm_depth > 0.0f) {
                    rm_gate = (1.0f - voice.modal_rm_depth)
                            + voice.modal_rm_depth * voice.modal_out_prev;
                    parallel_noise_gain *= rm_gate;
                }
                // Enveloped-LFO noise gate (Clap multi-burst, Shaker grains):
                // amplitude dips at the LFO rate with a decaying depth, leaving
                // the plain noise tail once the modulation has faded.
                if (voice.noise_am_depth > silence_threshold) {
                    float am = 1.0f - voice.noise_am_depth *
                               (0.5f + 0.5f * fastersinfullf(voice.noise_am_phase));
                    parallel_noise_gain *= fmaxf(0.0f, am);
                    voice.noise_am_phase += voice.noise_am_inc;
                    if (voice.noise_am_phase > (M_TWOPI)) voice.noise_am_phase -= (M_TWOPI);
                    voice.noise_am_depth *= voice.noise_am_decay;
                }
                voice_out += voice.exciter.noise_out_sample * parallel_noise_gain * voice.current_velocity;
                // Structural high-band branch: simple high-pass (x - LP(x)) over
                // exciter noise, mixed post-resonator to reduce KS-loss coupling.
                if (voice.hf_branch_mix > 0.0f && voice.hf_branch_env > silence_threshold) {
                    voice.hf_branch_lp += 0.12f * (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    float hf = (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    float hf_out = hf * voice.hf_branch_env * voice.hf_branch_mix * 8.0f * rm_gate;
                    voice_out += hf_out * voice.current_velocity;
                    voice.hf_branch_env *= voice.hf_branch_decay;
                }
                // ── Strike transient layer (membrane presets) ─────────────────
                // Short bright band-passed noise burst = the stick-slap / mallet-
                // contact attack the modal bank cannot make.  band = LP(hi) − LP(lo)
                // is a one-pole difference bandpass; the burst envelope (trans_env)
                // decays in ~10-30 ms and the level scales with velocity, so hard
                // hits get a brighter, louder attack transient.
                if (voice.trans_env > silence_threshold) {
                    float wn = voice.exciter.noise_gen.process();
                    voice.trans_lp_lo += voice.trans_a_lo * (wn - voice.trans_lp_lo);
                    voice.trans_lp_hi += voice.trans_a_hi * (wn - voice.trans_lp_hi);
                    float band = voice.trans_lp_hi - voice.trans_lp_lo;
                    voice_out += band * voice.trans_env * voice.trans_gain * voice.current_velocity;
                    voice.trans_env *= voice.trans_decay;
                }
                // Kick "thump": a fast pitch-dropping mid sine punch layered
                // over the boom.  Pitch starts ~3.5× thump_inc and drops to
                // thump_inc as the (shared) amplitude env falls — a punchy
                // "dow" beater impact.  Level and pitch are dialled by the
                // mallet knobs in NoteOn (0 at shipped values → bit-identical).
                if (voice.thump_env > silence_threshold) {
                    float drop = (voice.thump_amp0 > 1e-6f) ? (voice.thump_env / voice.thump_amp0) : 0.0f;
                    // The pitch sweep is CUBED against the amplitude envelope,
                    // not tied to it 1:1 (HW pass 41: 'kick "thump" is more a
                    // "zip" sound').  Linear `drop` made the sweep last as long
                    // as the sound: 299 → 115 Hz, about 1.4 octaves, spread
                    // over ~20 ms — long enough to hear as a descending chirp
                    // rather than an impact.  Cubing collapses it to ~7 ms
                    // while the amplitude still runs its full ~60 ms, so the
                    // sweep becomes attack CHARACTER instead of a glide.  The
                    // width is pulled 1.6 → 1.0 (2.6× → 2.0×, i.e. a 230 Hz
                    // start) for the same reason.
                    float d3   = drop * drop * drop;
                    float swp  = 1.0f + 1.0f * d3;
                    float th   = fastersinfullf(voice.thump_phase) * voice.thump_env;
                    // Velocity drives the PUNCH superlinearly (HW: "velocity
                    // seems to increase the decay but not the hit").  The body
                    // is scaled linearly by velocity, so a harder hit mostly
                    // just rang longer; giving the transient a steeper curve
                    // makes a hard strike punch harder relative to the body,
                    // which is what reads as "hit".  vel^1.5 via v*sqrt(v).
                    const float vel_p = voice.current_velocity *
                                        sqrtf(voice.current_velocity);
                    voice_out += th * vel_p;
                    voice.thump_phase += voice.thump_inc * swp;
                    if (voice.thump_phase > (M_TWOPI)) voice.thump_phase -= (M_TWOPI);
                    voice.thump_env *= voice.thump_decay;
                }
                if (voice.boom_mix > 0.0f && voice.boom_env > silence_threshold) {
                    if (m_preset_idx == k_KickDrum) {
                        // kick sub sweep: 90→55 Hz over boom_decay envelope
                        // (× boom_tune: TubRad retunes the whole kick, 1.0 shipped)
                        float sweep_hz = (55.0f + (35.0f * voice.boom_env)) * voice.boom_tune;
                        voice.boom_inc = (M_TWOPI * sweep_hz) * inverse_default_sample_rate;
                    } else if (m_preset_idx == k_808Sub) {
                        // 808-style: pitch_env (τ≈21ms) sweeps 45+115=160Hz → 45Hz in ~100ms
                        // independently from boom amplitude (boom_decay=760ms T60)
                        float sweep_hz = (45.0f + voice.pitch_env_amt * voice.pitch_env) * voice.boom_tune;
                        voice.boom_inc = (M_TWOPI * sweep_hz) * inverse_default_sample_rate;
                    } else if (m_preset_idx == k_RackTom) {
                        // The tom's THUMP is a pitch bend, not a mode.  Tracking
                        // the dominant partial of rock-rack-tom-1.wav in 30 ms
                        // windows shows one partial sliding 160 → 110 Hz (~650
                        // cents) with τ ≈ 55 ms, not the static cluster a windowed
                        // FFT appears to show — see the modal config comment.
                        // 175 Hz is the resting pitch at the shipped Note; boom_tune
                        // carries the note ratio so the whole drum still transposes.
                        float sweep_hz = (174.61f + voice.pitch_env_amt * voice.pitch_env) * voice.boom_tune;
                        voice.boom_inc = (M_TWOPI * sweep_hz) * inverse_default_sample_rate;
                    } else if (m_preset_idx == k_AcSnare) {
                        // Head-tension drop: the stick stretches the batter head
                        // and it relaxes back over the first few ms, so the body
                        // pitch starts sharp and settles.  UNLIKE the three
                        // branches above, this consumer did not previously
                        // exist — the row's pitch_env data (amt 18 Hz,
                        // τ ≈ 14 ms) had no reader anywhere on ENGINE_SNARE, so
                        // restoring the fields alone would have been silent.
                        // 175 Hz is `asn_bm`, the shipped resting pitch, so the
                        // sweep converges to exactly the fixed boom_inc it
                        // replaces.  boom_tune is 1.0 on this preset (nothing
                        // writes it — TubRad's retune is kick-gated); it is kept
                        // so all four branches share one form.
                        float sweep_hz = (175.0f + voice.pitch_env_amt * voice.pitch_env) * voice.boom_tune;
                        voice.boom_inc = (M_TWOPI * sweep_hz) * inverse_default_sample_rate;
                    }
                    voice.boom_attack_env = fminf(1.0f, voice.boom_attack_env + voice.boom_attack_inc);
                    float boom = fastersinfullf(voice.boom_phase)
                               * voice.boom_env * voice.boom_mix * voice.boom_attack_env;
                    voice_out += boom * voice.current_velocity;
                    voice.boom_phase += voice.boom_inc;
                    if (voice.boom_phase > (M_TWOPI)) voice.boom_phase -= (M_TWOPI);
                    voice.boom_env *= voice.boom_decay;
                }
                if (voice.modal_pilot_enabled) {
                    // Update modes 1/2 (and optionally 3/4 for metallic presets).
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
                    float32x2_t k12 = {voice.modal_k_1, voice.modal_k_2};
                    float32x2_t y112 = {voice.modal_y1_1, voice.modal_y1_2};
                    float32x2_t y212 = {voice.modal_y2_1, voice.modal_y2_2};
                    float32x2_t yn12 = vsub_f32(vmul_f32(k12, y112), y212);
                    voice.modal_y2_1 = voice.modal_y1_1;
                    voice.modal_y2_2 = voice.modal_y1_2;
                    voice.modal_y1_1 = vget_lane_f32(yn12, 0);
                    voice.modal_y1_2 = vget_lane_f32(yn12, 1);
                    if (voice.modal_mode_count > 2) {
                        float32x2_t k34 = {voice.modal_k_3, voice.modal_k_4};
                        float32x2_t y134 = {voice.modal_y1_3, voice.modal_y1_4};
                        float32x2_t y234 = {voice.modal_y2_3, voice.modal_y2_4};
                        float32x2_t yn34 = vsub_f32(vmul_f32(k34, y134), y234);
                        voice.modal_y2_3 = voice.modal_y1_3;
                        voice.modal_y2_4 = voice.modal_y1_4;
                        voice.modal_y1_3 = vget_lane_f32(yn34, 0);
                        voice.modal_y1_4 = vget_lane_f32(yn34, 1);
                    }
#else
                    float yn1 = (voice.modal_k_1 * voice.modal_y1_1) - voice.modal_y2_1;
                    float yn2 = (voice.modal_k_2 * voice.modal_y1_2) - voice.modal_y2_2;
                    voice.modal_y2_1 = voice.modal_y1_1;
                    voice.modal_y2_2 = voice.modal_y1_2;
                    voice.modal_y1_1 = yn1;
                    voice.modal_y1_2 = yn2;
                    if (voice.modal_mode_count > 2) {
                        float yn3 = (voice.modal_k_3 * voice.modal_y1_3) - voice.modal_y2_3;
                        float yn4 = (voice.modal_k_4 * voice.modal_y1_4) - voice.modal_y2_4;
                        voice.modal_y2_3 = voice.modal_y1_3;
                        voice.modal_y2_4 = voice.modal_y1_4;
                        voice.modal_y1_3 = yn3;
                        voice.modal_y1_4 = yn4;
                    }
#endif
                     if (voice.modal_mode_count > 4) {
                         float y5n = (voice.modal_k_5 * voice.modal_y1_5) - voice.modal_y2_5;
                         voice.modal_y2_5 = voice.modal_y1_5;
                         voice.modal_y1_5 = y5n;
                         float y6n = (voice.modal_k_6 * voice.modal_y1_6) - voice.modal_y2_6;
                         voice.modal_y2_6 = voice.modal_y1_6;
                         voice.modal_y1_6 = y6n;
                     }
                    // Drift control: periodic soft normalization for long tails.
                    if ((voice.modal_norm_count++ & 127u) == 0u) {
                        float a1 = fmaxf(fabsf(voice.modal_y1_1), fabsf(voice.modal_y2_1));
                        float a2 = fmaxf(fabsf(voice.modal_y1_2), fabsf(voice.modal_y2_2));
                        if (a1 > 1.2f) {
                            float s = 1.0f / a1;
                            voice.modal_y1_1 *= s;
                            voice.modal_y2_1 *= s;
                        }
                        if (a2 > 1.2f) {
                            float s = 1.0f / a2;
                            voice.modal_y1_2 *= s;
                            voice.modal_y2_2 *= s;
                        }
                        if (voice.modal_mode_count > 2) {
                            float a3 = fmaxf(fabsf(voice.modal_y1_3), fabsf(voice.modal_y2_3));
                            float a4 = fmaxf(fabsf(voice.modal_y1_4), fabsf(voice.modal_y2_4));
                            if (a3 > 1.2f) {
                                float s = 1.0f / a3;
                                voice.modal_y1_3 *= s;
                                voice.modal_y2_3 *= s;
                            }
                            if (a4 > 1.2f) {
                                float s = 1.0f / a4;
                                voice.modal_y1_4 *= s;
                                voice.modal_y2_4 *= s;
                             }
                             if (voice.modal_mode_count > 4) {
                                 float a5 = fmaxf(fabsf(voice.modal_y1_5), fabsf(voice.modal_y2_5));
                                 float a6 = fmaxf(fabsf(voice.modal_y1_6), fabsf(voice.modal_y2_6));
                                 if (a5 > 1.2f) { float s = 1.0f / a5; voice.modal_y1_5 *= s; voice.modal_y2_5 *= s; }
                                 if (a6 > 1.2f) { float s = 1.0f / a6; voice.modal_y1_6 *= s; voice.modal_y2_6 *= s; }
                             }
                         }
                     }
                     float m1 = voice.modal_y1_1 * voice.modal_env_1;
                     float m2 = voice.modal_y1_2 * voice.modal_env_2;
                     float m3 = 0.0f, m4 = 0.0f, m5 = 0.0f, m6 = 0.0f;
                     voice.modal_env_1 *= voice.modal_decay_1;
                     voice.modal_env_2 *= voice.modal_decay_2;
                     if (voice.modal_mode_count > 2) {
                         m3 = voice.modal_y1_3 * voice.modal_env_3;
                         m4 = voice.modal_y1_4 * voice.modal_env_4;
                         voice.modal_env_3 *= voice.modal_decay_3;
                         voice.modal_env_4 *= voice.modal_decay_4;
                         if (voice.modal_mode_count > 4) {
                             m5 = voice.modal_y1_5 * voice.modal_env_5;
                             m6 = voice.modal_y1_6 * voice.modal_env_6;
                             voice.modal_env_5 *= voice.modal_decay_5;
                             voice.modal_env_6 *= voice.modal_decay_6;
                         }
                     }
                    float modal_mix_dyn = voice.modal_mix;
                    if (voice.metal_fm_env > silence_threshold) {
                        // Keep modal attack "opening" during FM chirp onset, then settle.
                        modal_mix_dyn *= (1.0f + (0.35f * voice.metal_fm_env));
                    }
                    // Non-KS engines use the modal bank as primary tonal resonator.
                    // Scale up mix to compensate for the absent KS resonator gain.
                    float modal_engine_gain = (voice_engine == ENGINE_KS) ? 1.0f : 5.0f;
                    float modal_sum = m1
                                    + (stage2_modal_amp_ratio_2 * m2)
                                    + (0.45f * m3)
                                    + (0.28f * m4)
                                    + (0.18f * m5)
                                    + (0.12f * m6);
                    // Feed the ring-mod coupling (1-sample delay; bounded so the
                    // noise gain factor stays well inside ±(1+depth)).
                    voice.modal_out_prev = fmaxf(-1.5f, fminf(1.5f, modal_sum));
                    voice_out += modal_sum * modal_mix_dyn * modal_engine_gain;
                     if (voice.modal_env_1 < silence_threshold &&
                         voice.modal_env_2 < silence_threshold &&
                         (voice.modal_mode_count <= 2 || (voice.modal_env_3 < silence_threshold && voice.modal_env_4 < silence_threshold &&
                                                          (voice.modal_mode_count <= 4 || (voice.modal_env_5 < silence_threshold && voice.modal_env_6 < silence_threshold))))) {
                         voice.modal_pilot_enabled = false;
                         voice.modal_mode_count = 0;
                    }
                }
                // ── Stage 3: master_env fade + squelch ────────────────────
                // If Stage 3 is silent but Stage 2 is not, the Phase 18
                // pre-advance fix is not working on this ARM binary — the
                // envelope is stuck at 0 on the first GateOff tick.
                voice.mag_env = (fabsf(voice_out) * alpha) + (voice.mag_env * limiter);
                float damper_fade = voice.exciter.master_env.process();
                voice_out *= damper_fade;
                if (voice.exciter.current_frame > kSquelchGuardSamples) {
                    // Original squelch: deactivate released voices
                    if (voice.is_releasing &&
                        (voice.mag_env < kSquelchThreshold ||
                         voice.exciter.master_env.state == ENV_IDLE)) {
                        voice.is_active = false;
                    }
                    // KS auto-decay squelch: reclaim voice slot when master_env
                    // (driven by Dkay) has fully decayed while gate is held.
                    if (!voice.is_releasing && voice_engine == ENGINE_KS &&
                        voice.exciter.master_env.state == ENV_IDLE) {
                        voice.is_active = false;
                    }
                    // Non-KS zombie-voice cleanup: reclaim slot when modal ring
                    // and exciter are both silent, even with gate still held.
                    if (!voice.is_releasing && voice_engine != ENGINE_KS &&
                        voice.mag_env < kSquelchThreshold) {
                        voice.is_active = false;
                    }
                }
                // ── Stage 4a: Tilt EQ ──────────────────────────────────────
                if (tone_active) {
                    voice.tone_lp = (voice_out * kToneLpMix) + (voice.tone_lp * (1.0f - kToneLpMix));
                    if (tone_val < zeroThreshold) {
                        voice_out = voice_out + (voice.tone_lp - voice_out) * (-tone_val * kInvToneCutDivisor);
                    } else {
                        float hp = voice_out - voice.tone_lp;
                        voice_out += hp * (tone_val * kInvToneBoostDivisor);
                    }
                }
                if (voice.onset_inc > 0.0f && voice.onset_env < 1.0f) {
                    voice.onset_env = fminf(1.0f, voice.onset_env + voice.onset_inc);
                    voice_out *= voice.onset_env;
                }
                // Preset-change fade-out.  fade_mul is exactly 1.0f in normal
                // play, so this branch is skipped and the output is unchanged
                // bit-for-bit; only a voice orphaned by a preset change ramps.
                if (voice.fade_mul < 1.0f) {
                    voice_out *= voice.fade;
                    voice.fade *= voice.fade_mul;
                    if (voice.fade < 0.001f) voice.is_active = false;
                }

                // Left only — see the mono-bus note above the voice loop.
                main_out[i * 2] += voice_out * state.master_gain;

#ifdef UNIT_TEST_DEBUG
                if (voice_idx == state.next_voice_idx) {
                    ut_exciter_out = exciter_sig;
                    ut_delay_read  = outA;
                    ut_voice_out   = voice_out;
                }
#endif
                // ── Stage 1/2: voice lifetime management ───────────────────
                // Without Stage 3 squelch, voices stay is_active=true forever.
                // Deactivate once the mallet has fully decayed, the noise
                // envelopes are idle, AND the voice is actually silent.
                // The mag_env guard is critical: without it, waveguide-sustain
                // presets (GtrStr, Koto, etc.) deactivate immediately after
                // NoteOff because mallet/noise decay long before the KS string
                // stops ringing, producing a 10Hz master-filter tail instead of
                // the correct multi-second sustain.
                if (voice.is_releasing &&
                        voice.exciter.mallet_lp2 < 1e-6f &&
                        voice.exciter.noise_env.state == ENV_IDLE &&
                        voice.exciter.noise_env_hi.state == ENV_IDLE &&
                        voice.mag_env < kSquelchThreshold) {
                    voice.is_active = false;
                }
            }
        }

        // ── Stage 1-3: pre-clip trim + hard-clip output ────────────────────
        // Stage 4 uses soft-clip + overdrive.  For debug stages the raw mallet
        // impulse (~3-4 × full-scale) must be clamped or the Drumlogue DAC
        // saturates on the first note and may engage hardware protection.
        // (Timpani/Taiko never reach here — the dense-kernel path above owns
        // its own master stage; the earlier per-preset pre-clip trim hack is
        // gone with it.)
        //
        // ENGINE_CYMBAL: stacked voices (rolls) can sum past the ±0.99
        // brickwall below, which reads as harsh noise.  CymbalKit-style soft
        // headroom first: a single voice passes ~unchanged, stacks are limited
        // gently instead of hard-clipping.  Other presets are untouched.
        // ── Idle-CPU guard ─────────────────────────────────────────────────
        // The OS calls render continuously; with no active voice the buffer is
        // all zeros, and after a short flush window the master SVF has settled
        // to zero too — skip the pre-clip and master-FX loops entirely.  The
        // flush window (240 blocks ≈ 320 ms at 64 frames) lets the filter tail
        // ring out after the last voice dies so there is no step on re-entry.
        if (any_voice_rendered) {
            m_idle_flush_blocks = 240;
        } else if (m_idle_flush_blocks > 0) {
            --m_idle_flush_blocks;
        } else {
            return;
        }

        // Keyed on a cymbal voice actually having RENDERED, not on the live
        // preset index: after a preset change the two disagree, and the soft
        // headroom belongs to the stacked cymbal voices, wherever they came from.
        if (any_cymbal_rendered) {
            for (size_t i = 0; i < frames; ++i) {
                const size_t l = i * 2;
                main_out[l] = main_out[l] / (1.0f + 0.35f * fabsf(main_out[l]));
            }
        }
        // NOTE: there is deliberately NO hard clip of the voice bus here.
        // A `clamp(main_out, +-0.99)` used to sit at this point, left over from
        // the debug render stages (where the raw mallet impulse, ~3-4x full
        // scale, could reach the DAC without a master stage behind it).  In the
        // shipping path Stage 4b ALWAYS runs and bounds the output to
        // kMasterLimCeil by construction, so the clamp protected nothing and
        // cost a great deal:
        //   - it is a hard clipper, and the kick presets sit on top of it
        //     permanently (Kick2's bus is pinned at 0.99 for most of the hit),
        //     which is a much larger distortion source than anything the
        //     master curve does;
        //   - because the bus was already pinned, ANY layer added underneath it
        //     was invisible.  That is why VlMllRes measured 0.99x on Kick2 and
        //     "thump is not really increased" on hardware: the thump layer was
        //     being armed correctly (verified: thump_env 1.6 at 115 Hz on all
        //     three kicks) and then clipped straight back off.
        // The master limiter below sees the true bus level instead and rides it
        // down smoothly, so added layers change the sound again.
        // ── Stage 4b: Master FX (filter + overdrive + peak limiter) ───────
        // The output stage used to be x / (1 + |x|), which divides EVERY
        // sample, not just the ones that need limiting: a unity-amplitude bus
        // left the unit at 0.50 and a 0.3 signal at 0.23.  That is where the
        // unit's ~6 dB of missing level went — measured across the 40 shipped
        // presets, peaks sat at 0.50-0.74 and NOTHING reached the 0.99 ceiling.
        // It also squashed the strike crest, which is the "rough / synthy hit"
        // the HW comparison flagged and the reason the Timpani/Taiko kernel was
        // given its own master stage.  Use that same transparent limiter here:
        // unity below kMasterLimThr, tanh knee above, asymptotic to 1.0.
        // Both channels carry the same signal (mix_r was already a copy of the
        // filtered left), so limit once and write it twice.
        // ── Why a gain ENVELOPE and not a waveshaper ───────────────────────
        // HW: "boom for kicks was declared perfect, but with long decays it
        // sounds a bit brittle or distorted."  Pass 29's stage applied the
        // curve below to every SAMPLE, which is a waveshaper: the gain changes
        // within a single cycle, and on a ~45-90 Hz kick boom that manufactures
        // high-order harmonics.  Measured on 808Sub at Dkay+Rel max, the 4th
        // harmonic sat at -21.5 dB under the pre-29 x/(1+|x|) master and at
        // -7.4 dB after — a 14 dB rise, worst on long decays because the
        // sustained part of the boom stays under compression longest.
        //
        // The presets deliberately drive this stage hard (808Sub arrives at
        // ~4.5x full scale), so softening the knee cannot help: the waveform
        // sits deep in the compressive region, nowhere near the corner.  That
        // was measured too — sweeping a smoothstep knee of 0 / 0.10 / 0.20 /
        // 0.30 / 0.44 moved H4 by 0.2 dB.  Do not re-try it.
        //
        // Instead the peak is tracked with an instant-attack, slow-release
        // follower and ONE gain is applied per cycle.  Because master_lim_env
        // only ever jumps up and decays down, it is >= |x| at every sample, so
        // out = x * (limit(env)/env) is still bounded by kMasterLimCeil BY
        // CONSTRUCTION — the brickwall stays pure safety — while the waveform
        // shape inside a cycle is preserved and the harmonics are not created.
        for (size_t i = 0; i < frames; ++i) {
            float x = state.master_filter.process(main_out[i * 2]);
            x *= state.master_drive;
            const float ax = fabsf(x);
            state.master_lim_env = (ax > state.master_lim_env)
                ? ax
                : state.master_lim_env + (ax - state.master_lim_env) * kMasterLimRelCoef;
            float a = state.master_lim_env;
            if (a > kMasterLimThr) {
                // Soft knee that asymptotes to kMasterLimCeil BY CONSTRUCTION:
                //     y = thr + span * over / (over + span),   over = a - thr
                // Unity slope at the knee, monotone, and strictly below `ceil`
                // for every finite input, so nothing is ever flat-topped and
                // the brickwall below is pure safety.  It is the same hyperbola
                // as the old x/(1+|x|), translated to start at the threshold.
                //
                // Two shapes were tried and rejected here, both measurable:
                // plain x/(1+|x|) compresses from zero up (a 0.5 bus left at
                // 0.333, -3.5 dB, and the strike crest with it), while
                // 0.85 + 0.15*fastertanhf(...) needs a clamp — fastertanhf is
                // a rational approximation asymptotic to ~1.168, not 1.0 — and
                // clamping it just relocates the brickwall: it pinned 23034
                // samples dead flat across the 40 presets, including an 11 ms
                // plateau on the Gong, where the old curve pinned none.
                const float over = a - kMasterLimThr;
                const float y =
                    kMasterLimThr + kMasterLimSpan * over / (over + kMasterLimSpan);
                x *= y / a;                 // one gain for the whole cycle
            }
            x = fmaxf(-kMasterLimCeil, fminf(kMasterLimCeil, x));
            main_out[i * 2]     = x;
            main_out[i * 2 + 1] = x;
        }
    }

    inline void GateOn(uint8_t velocity) {
        // [UT4: ZERO VELOCITY FIX]
        if (velocity == 0) {
            GateOff();
        } else {
            // Route internal Drumlogue sequencer to the UI Note parameter
            NoteOn(m_ui_note, velocity);
        }
    }

// ==============================================================================
// PRIVATE METHODS
// ==============================================================================
private:
    // True while the dense modal-drum kernel owns the audio path.
    inline bool kernel_preset_active() const {
        return m_preset_idx == k_Timpani || m_preset_idx == k_Taiko;
    }

    // Map the Brachetti sound-design params onto the dense-kernel modifiers.
    // Every mapping is a DELTA from the preset's shipped value (the same
    // reference-anchor pattern as the legacy modal bank), so the shipped rows
    // reproduce the approved standalone renders exactly and every knob still
    // sculpts around that point.  Curves reuse the strengthened Task-5 gains.
    inline void RefreshKernelMods() {
        if (!kernel_preset_active() || !m_drum_kernel.IsActive()) return;
        ModalDrumKernel::Mods md;

        // (Pitch is not a mod: the note travels with each NoteOn/Trigger and
        // retunes one kettle synchronously — see the kernel header.)

        // Dkay (0-200, ×0.005) + Rel (0-20, ×0.05): T60 multiplier.
        float dk = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
        float rl = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel]  * 0.05f));
        md.decay_mult = knob_exp2(4.5f * (dk - m_modal_dkay_ref) + 3.2f * (rl - m_modal_rel_ref));

        // Mterl (−10..30): brighter material lets HF modes ring longer.  Widened
        // 1.2→2.0→3.0 (audit/HW: Mterl "weak", then "too subtle" on the kettle)
        // for a full material sweep; still anchored → bit-identical.
        float mt = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
        md.hf_decay_tilt = 3.0f * (mt - m_modal_mterl_ref);

        // TubRad (0-20, ×0.05): kettle/shell SIZE.  A bigger body rings a little
        // longer AND darker (more low-mode weight); this is the "TubRad no effect
        // on the kernel drums" fix — the legacy modal-bank TubRad path is bypassed
        // for Timpani/Taiko.  Anchored at the shipped TubRad → bit-identical.
        float tr = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
        float d_tr = tr - m_modal_tubrad_ref;
        md.decay_mult    *= knob_exp2(1.9f * d_tr);   // bigger = longer sustain
        md.hf_decay_tilt -= 1.0f * d_tr;           // bigger = darker/rounder

        // Inharm (0-199, ×0.005): stretches the upper modes away from f0.
        float ih = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramInharm] * 0.01f));
        md.stretch = 2.4f * (ih - m_modal_inharm_ref);

        // Partls (index 0-4): density of the membrane fill.  Shipped index =
        // the full approved wedge; lower thins it toward the bare mode lines.
        int pt = (m_params[k_paramPartls] >= 0 && m_params[k_paramPartls] <= 4)
                 ? (int)m_params[k_paramPartls] : m_modal_partls_ref;
        md.density = fminf(1.0f, knob_exp2(0.7f * (float)(pt - m_modal_partls_ref)));

        // MlltStif (×0.002): impulse sharpness (shorter strike = brighter).
        float st = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
        md.exc_sharp = knob_exp2(3.0f * (st - m_modal_stiff_ref));

        // MlltRes (×0.001) + HitPos (×0.01, edge = clickier) + VlMllRes: the
        // knock ("wham") gain.  VlMllRes is the HIT-PROMINENCE knob (HW: "at
        // high values the hit should be prominent"): raising it BOOSTS the
        // knock and FLATTENS its velocity curve (lower exponent), so the hit
        // stays tall even on soft strikes; lowering it buries the knock and
        // steepens the accent response.
        float mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
        // fmaxf(-1.0f, …): anchored, follows the bipolar knob below centre.
        float hp = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramHitPos] * 0.01f));
        float vs = (float)m_params[k_paramVlMllStf] * 0.01f;
        float vr = (float)m_params[k_paramVlMllRes] * 0.01f;
        md.knock_mult = knob_exp2(3.4f * (mr - m_modal_mltres_ref) +
                              1.9f * (hp - m_modal_hitpos_ref) +
                              3.0f * (vr - m_kernel_vlres_ref));
        md.vel_knock_exp = fmaxf(0.4f, fminf(3.0f, 1.5f - 1.0f * (vr - m_kernel_vlres_ref)));

        // VlMllStf (±100 → ±1): velocity→sharpness amount.
        // Was `clamp(0.6 + 0.8*(vs - ref), 0, 1)` — the ONLY linear-into-a-clamp
        // mapping in this block, everything around it is knob_exp2, and it
        // saturated exactly the way the pass-37 modal spread did.  Timpani
        // ships VlMllStf 40, so 0.6 + 0.8*(vs − 0.4) left the clamp reachable
        // at vs < −0.35: measured dead spans −100..−40 and 80..100, i.e. ~40 %
        // of the knob (plateau_probe).  vel_sharp is a normalised 0..1 depth,
        // so it takes the same headroom scaling as the mallet stiffness above:
        // pivot at the shipped value, then divide the delta by the travel left
        // on that side so the knob reaches 0 and 1 exactly at its own ends and
        // never needs a clamp.  d == 0 at shipped → 0.6 exactly, as before, so
        // all 41 renders stay byte-identical.
        {
            float d = vs - m_kernel_vlstf_ref;
            md.vel_sharp = (d >= 0.0f)
                ? 0.6f + (d / fmaxf(1e-6f, 1.0f - m_kernel_vlstf_ref)) * 0.4f
                : 0.6f + (d / fmaxf(1e-6f, 1.0f + m_kernel_vlstf_ref)) * 0.6f;
        }

        // NzMix (0-100): noise-wedge level around the recipe.  The additive
        // term keeps the knob alive when the recipe ships with noise 0
        // (Timpani): opening it adds the taiko-style grain/air layer.
        float nz  = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzMix] * 0.01f));
        float dnz = nz - m_kernel_nzmix_ref;
        md.noise_mult = knob_exp2(3.0f * dnz);
        md.noise_add  = 0.30f * fmaxf(0.0f, dnz);

        // NzFltFrq (stored ÷10): wedge start-cutoff scale vs the shipped corner.
        float fq = fmaxf(20.0f, (float)m_params[k_paramNzFltFrq]);
        md.noise_bright = fq / m_kernel_nzfrq_ref;

        m_drum_kernel.SetMods(md);
    }

    inline void apply_pitch_bend_to_voice(VoiceState& v) {
        v.resA.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 2),
                                                  v.base_delay_A * m_pitch_bend_mult));
        v.resB.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 2),
                                                  v.base_delay_B * m_pitch_bend_mult));
    }


// ==============================================================================
// PRIVATE VARIABLES
// ==============================================================================
private:
    float m_master_cutoff = 16000.0f; // Master LP cutoff — default fully open

    // Functions from unit runtime (nullptr until Init() assigns them from the OS descriptor)
    unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr = nullptr;
    unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr = nullptr;
    unit_runtime_get_sample_ptr m_get_sample = nullptr;

    uint8_t m_ui_note = 60;
    // Idle-CPU guard: blocks of master-chain flush left to run after the last
    // active voice died.  While > 0 the pre-clip + master FX loops keep running
    // on the (all-zero) buffer so the master SVF settles; at 0 processBlock
    // returns right after the buffer clear — the drumlogue calls every unit's
    // render continuously, so a silent unit otherwise burns SVF + soft-clip
    // per sample forever.
    uint16_t m_idle_flush_blocks = 0;
    // Ex-sample-selection params, repurposed (PCM layering removed): global
    // cymbal performance/CPU controls.
    // Master drive queued behind a preset-change fade (-1 = nothing pending).
    float m_pending_drive = -1.0f;
    uint8_t m_poly = 4;            // global voice cap (1-4, Poly knob)
    uint8_t m_cym_poly = 4;        // cymbal-family cap (= m_poly; CPU is bounded
                                   // by kCymCostBudget, not by voice count)
    uint8_t m_cym_reso_scale = 40;  // cymbal resonator-bank scale (25-60 %),
                                    // driven by Partls on the cymbal presets
    float   m_vel_bias = 0.0f;      // Velocity knob, -1 (ghost) .. +1 (wham);
                                    // 0 = neutral, strikes play as sent
    uint8_t m_model_a = k_String;
    uint8_t m_model_b = k_String;
    bool    m_is_resonator_a = true; // default is res A
    bool    m_is_resonator_b = true; // "copy" of res A

    uint8_t m_active_partials = 32; // Default: 32 partials (Partls index 3, ResB active)
    float   m_coupling_depth  = 0.75f; // Coupling depth [0.0-1.0] from Partls UI index 0-4.
    // Stored separately from m_params[k_paramPartls] so that Partls=5/6
    // (ResA/ResB editor-select modes) never corrupt the coupling amount.
    float   m_pitch_bend_mult = 1.0f; // Delay-length multiplier from MIDI pitch bend (1.0 = centred).
};

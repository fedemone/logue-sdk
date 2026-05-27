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
static constexpr float    kInvToneBoostDivisor = 0.06666667f; // 1 / 15
static constexpr float    zeroThreshold = 0.0f;
static constexpr float    alpha = 0.01f;
static constexpr float    limiter = 0.99f;
static constexpr int      kSquelchGuardSamples = 1000; // ~20 ms
static constexpr float    kSquelchThreshold = 0.0001f; // -80 dB
static constexpr float    k_log_2_of_200 = 7.643856f;
static constexpr float    k_log_0001 = -6.907755279f; // logf(0.001f) — T60→decay coefficient
static constexpr float    stage2_modal_amp_ratio_2 = 0.6f;
static constexpr float    silence_threshold = 1e-5f;

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
class alignas(16) RipplerXWaveguide {
public:
    // ==============================================================================
    // PARAMETER INDEX ENUM (Strictly matches header.c)
    // ==============================================================================
    enum ParamIndex {
        k_paramProgram = 0,
        k_paramNote,        // 1
        k_paramBank,        // 2
        k_paramSample,      // 3
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
    enum ProgramIndex {
        k_Init = 0,         // 0
        k_Marimba,          // 1  -sample: marimba-hit-c4_C_minor.wav (524Hz +/- 50Hz)
        k_808Sub,           // 2
        k_AcSnare,          // 3  -sample: acoustic-snare.wav (1436Hz +/- 20Hz), snare heavy.wav (1287Hz +/- 80Hz)
        k_TubularBell,      // 4  -samples: tubular-bell-47849.wav (oscillates between 1500Hz and 280Hz then settles to 1230Hz)
        k_Timpani,          // 5  -sample: Orchestral-Timpani-C.wav (239Hz +/- 20Hz)
        k_Djambe,           // 6  -samples: Djambe-B3.wav (starts over 600Hz and settles to 215Hz), Djambe-A3.wav (starts over 1100Hz and settles to 747Hz +/- 10Hz)
        k_Taiko,            // 7  -sample: Taiko-Hit.wav (1582Hz +/- 50Hz)
        k_MarchSnare,       // 8  -sample: Marching-Snare-Drum-A#-minor (1750Hz +/- 100Hz)
        k_Koto,             // 9  -sample: Koto-B5.wav (starts as 700Hz and settles to 290Hz), Koto-B5.wav (starts as 650Hz goes up to 900Hz and settles 200Hz), Koto-Stab-F#.wav (750Hz +/- 100Hz)
        k_Vibraphone,       // 10  -sample: vibraphone_C_major.wav (goes up to 1398Hz then settles to 273Hz), vibraphone_C_major1.wav  (262Hz +/- 20Hz)
        k_Woodblock,        // 11  -sample: Woodblock.wav (3500Hz +/- 100Hz), Woodblock1.wav (858Hz +/- 30Hz)
        k_AcousticTom,      // 12  -sample: Tom1-001-CloseRoom.wav (428Hz +/- 50Hz), Tom2-004-CloseRoom.wav (288Hz + 50Hz)
        k_Cymbal,           // 13  -sample: cymbal-Crash16Inch.wav (starts as 2000Hz goes down to 650Hz and settles to 1000Hz - a lot of oscillations)
        k_Gong,             // 14  -sample: Chinese-Gong.wav, Gong-long-G#.wav (starts with 800Hz and settles to 1680Hz +/- 10Hz)
        k_Kalimba,          // 15  -sample: kalimba-e_E.wav (1398Hz +/- 50Hz)
        k_SteelPan,         // 16  -sample: steel-pan-Nova Drum Real C 432.wav (257Hz +/- 30Hz), steel-pan-PERCY-C4-SHort.wav (260Hz +/- 50Hz), steel-pan-yudin C3.wav (220Hz +/- 20Hz)
        k_Claves,           // 17  -sample: percussion-clave-like-hit-107112.mp3 (950Hz +/- 40Hz), wetclave.wav  (2629Hz +/- 20Hz)
        k_Cowbell,          // 18  -sample: Cowbell_2.wav (408Hz +/- 30Hz)
        k_Triangle,         // 19  -sample: Triangle-Bell_C#.wav (3753Hz +/- 100Hz), Triangle-Bell_F5.wav (795Hz +/- 100Hz)
        k_KickDrum,         // 20  -sample: KickA-Hard-012-CloseRoom.wav (1016Hz +/- 100Hz)
        k_Clap,             // 21  -sample: 07_Clap_05_SP.wav (1532 +/- 100Hz)
        k_Shaker,           // 22  -sample: MaracasPair.wav (11292 +/- 100Hz)
        k_Flute,            // 23  -sample: Flute-A2.wav (427Hz +/- 5Hz), Flute-D3.wav (577Hz +/- 5Hz)
        k_Clarinet,         // 24  -sample: Clarinet-A-minor.wav (120Hz +/- 10Hz), Clarinet-C-minor.wav (523Hz +/- 10Hz)
        k_PluckBass,        // 25  -sample:
        k_GlassBowl,        // 26  -sample: glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav (oscillates between 200Hz and 1550Hz), glass-singing-bowl_23042017-01-raw-71015.wav
        k_GuitarStr,        // 27 — reference: Karplus-Strong string at A4 for model validation
        k_HiHatClosed,      // 28  -sample: HatClosedLive3.wav (12597Hz +/- 100Hz), OpenHatBig.wav (11746Hz +/- 100Hz)
        k_HiHatOpen,        // 29  -sample: TightClosedHat.wav (11635Hz +/- 100Hz)
        k_Conga,            // 30  -sample: Bongo_Conga2.wav (286Hz +/- 10Hz)
        k_Handpan,          // 31  -sample: Tabla-Drum-Hit-D4_.wav (237Hz +/- 30Hz), percussion-one-shot-tabla-3_C_major.wav (323Hz +/- 30Hz)
        k_BellTree,         // 32  -sample:
        k_SlitDrum,         // 33  -sample:
        k_Ride,             // 34  -sample: cymbal-Ride18Inch.wav (start over 2000Hz and settles to 761Hz), CrashA-001-ClosedRoom.av (starts as 850Hz goes up to 1650Hz and settles to 645 - lot of oscillations in between)
        k_RideBell,         // 35  -sample: cymbal-RideBell20InchSabian.wav (starts over 2000Hz goes down to 1600Hz and settles to 867Hz)
        k_Bongo,            // 36  -sample: Bongo_Conga_Mute4.wav (430Hz +/- 100Hz)
        k_GlassBottle,      // 37  -sample: GlassBottle.wav (2636Hz +/- 200Hz)
        k_Tick,             // 38  -sample: one-tic-clock.wav,ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav (550Hz +/- 100Hz), high-church-clock-fx_100bpm.wav (strats over 800Hz and settles to 395Hz)
        k_NumPrograms       // 39 — marker (count)
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

static constexpr float kck_bm = (2.0f * M_PI * 58.0f) * inverse_default_sample_rate;
static constexpr float tak_bm = (2.0f * M_PI * 70.0f) * inverse_default_sample_rate;
static constexpr float tom_bm = (2.0f * M_PI * 110.0f) * inverse_default_sample_rate;
static constexpr float asn_bm = (2.0f * M_PI * 175.0f) * inverse_default_sample_rate;
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
struct ModalPresetConfig { float ratio2; float ratio3; float ratio4; float t60_1_ms; float t60_2_ms; float t60_3_ms; float t60_4_ms; float mix; float env1; float env2; float env3; float env4; uint8_t mode_count; float ratio5; float ratio6; };
// NOTE: Must be 'static' only (no const/constexpr).  On GCC 6.5, const/constexpr
// places these arrays in .rodata, which is counted by the Drumlogue firmware in its
// per-unit .text segment size check (~30 KB limit).  Plain 'static' puts them in
// .data, which is checked separately and has a much larger budget.
static ModalPresetConfig kDefaultModalPresetConfig{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0,0.0f,0.0f};
static ModalPresetConfig modal_preset_configs[k_NumPrograms] = {
/* k_Init */ kDefaultModalPresetConfig, /* k_Marimba */ {4.00f,10.0f,0.0f,250.0f,90.0f,0.0f,0.0f,0.18f,0.72f,0.50f,0.22f,0.0f,3,0,0.0f}, /* k_808Sub */ kDefaultModalPresetConfig,
/* k_AcSnare */ {1.59f,2.14f,2.30f,60.0f,45.0f,30.0f,20.0f,0.15f,0.65f,0.50f,0.35f,0.22f,4,0,0.0f}, /* k_TubularBell */ {2.756f,5.404f,0.0f,2000.0f,3000.0f,0.0f,0.0f,0.22f,0.18f,0.90f,0.55f,0.0f,3,0,0.0f},
/* k_Timpani */ {1.340f,1.664f,1.980f,900.0f,680.0f,500.0f,380.0f,0.32f,0.90f,0.75f,0.55f,0.38f,4,0,0.0f}, /* k_Djambe */ {1.59f,2.14f,2.30f,80.0f,55.0f,38.0f,26.0f,0.20f,0.65f,0.50f,0.35f,0.22f,4,0,0.0f},
/* k_Taiko */ {1.59f,2.14f,2.90f,350.0f,250.0f,180.0f,120.0f,0.28f,0.85f,0.70f,0.52f,0.36f,4,0,0.0f}, /* k_MarchSnare */ {1.59f,2.14f,2.30f,30.0f,22.0f,15.0f,10.0f,0.14f,0.60f,0.45f,0.30f,0.18f,4,0,0.0f},
/* k_Koto */ kDefaultModalPresetConfig, /* k_Vibraphone */ {4.00f,10.0f,0.0f,800.0f,300.0f,0.0f,0.0f,0.18f,0.80f,0.52f,0.26f,0.0f,3,0,0.0f},
/* k_Woodblock */ {STAGE2_MODAL_RATIO_2,0.0f,0.0f,STAGE2_MODAL_T60_1_MS,STAGE2_MODAL_T60_2_MS,0.0f,0.0f,STAGE2_MODAL_MIX,STAGE2_MODAL_ENV1,STAGE2_MODAL_ENV2,0.0f,0.0f,2,0,0.0f},
/* k_AcousticTom */ {1.59f,2.14f,2.30f,100.0f,70.0f,50.0f,35.0f,0.18f,0.65f,0.48f,0.32f,0.20f,4,0,0.0f}, /* k_Cymbal */ {2.92f,6.37f,11.75f,3000.0f,2000.0f,1500.0f,1000.0f,0.15f,0.90f,0.75f,0.55f,0.36f,6, 14.0f,19.0f},
/* k_Gong */ {1.479f,1.932f,2.332f,2000.0f,1500.0f,1100.0f,800.0f,0.20f,0.90f,0.75f,0.56f,0.40f,6, 2.549f,2.840f}, /* k_Kalimba */ {4.00f,10.0f,0.0f,300.0f,100.0f,0.0f,0.0f,0.15f,0.80f,0.50f,0.22f,0.0f,3,0,0.0f},
/* k_SteelPan */ {2.00f,3.00f,4.00f,1200.0f,900.0f,700.0f,500.0f,0.22f,0.90f,0.75f,0.55f,0.35f,4,0,0.0f},
/* k_Claves */ {2.756f,5.404f,0.0f,60.0f,25.0f,0.0f,0.0f,0.16f,0.70f,0.45f,0.0f,0.0f,3,0,0.0f}, /* k_Cowbell */ {1.41f,2.01f,2.56f,180.0f,130.0f,90.0f,65.0f,0.16f,0.75f,0.60f,0.45f,0.30f,4,0,0.0f},
/* k_Triangle */ {2.756f,5.404f,0.0f,6000.0f,5000.0f,3500.0f,0.0f,0.15f,0.80f,0.55f,0.30f,0.0f,3,0,0.0f},
/* k_KickDrum */ kDefaultModalPresetConfig, /* k_Clap */ kDefaultModalPresetConfig, /* k_Shaker */ kDefaultModalPresetConfig, /* k_Flute */ kDefaultModalPresetConfig, /* k_Clarinet */ kDefaultModalPresetConfig,
/* k_PluckBass */ kDefaultModalPresetConfig,
/* k_GlassBowl */ {2.09f,3.35f,4.77f,2000.0f,1600.0f,1200.0f,800.0f,0.20f,0.85f,0.70f,0.50f,0.35f,4,0,0.0f}, /* k_GuitarStr */ kDefaultModalPresetConfig,
/* k_HiHatClosed */ {1.479f,1.932f,2.332f,45.0f,28.0f,16.0f,10.0f,0.24f,0.80f,0.65f,0.48f,0.32f,4,0,0.0f}, /* k_HiHatOpen */ {1.479f,1.932f,2.332f,400.0f,300.0f,200.0f,140.0f,0.30f,0.85f,0.70f,0.55f,0.40f,4,0,0.0f},
/* k_Conga */ {1.59f,2.14f,2.30f,90.0f,65.0f,45.0f,30.0f,0.20f,0.70f,0.52f,0.35f,0.22f,4,0,0.0f}, /* k_Handpan */ {2.00f,3.00f,0.0f,900.0f,700.0f,0.0f,0.0f,0.20f,0.85f,0.65f,0.0f,0.0f,3,0,0.0f},
/* k_BellTree */ {2.01f,2.76f,0.0f,900.0f,700.0f,0.0f,0.0f,0.17f,0.80f,0.60f,0.0f,0.0f,3,0,0.0f},
/* k_SlitDrum */ kDefaultModalPresetConfig, /* k_Ride */ {1.479f,1.932f,2.332f,2400.0f,1800.0f,1300.0f,950.0f,0.14f,0.80f,0.65f,0.50f,0.35f,6, 2.549f,2.840f},
/* k_RideBell */ {2.01f,2.76f,3.56f,1500.0f,1200.0f,900.0f,700.0f,0.20f,0.85f,0.70f,0.55f,0.40f,4,0,0.0f},
/* k_Bongo */ {1.59f,2.14f,2.30f,50.0f,35.0f,25.0f,16.0f,0.18f,0.65f,0.48f,0.32f,0.20f,4,0.0f,0.0f}, /* k_GlassBottle */ kDefaultModalPresetConfig, /* k_Tick */ kDefaultModalPresetConfig
};

static float model_param_presets[k_NumPrograms][k_model_param_total]{
    /*               k_base_fm_hz, k_snare_wire_z1, k_snare_wire_z2, k_snare_wire_mix, k_snare_wire_a1, k_snare_wire_a2, k_wire_onset_env, k_wire_onset_attack, k_noise_lp_state, k_noise_band_mix, k_noise_hi_lp_state, k_noise_hi_lp_coeff, k_use_hat_filter, k_diffuser_mix, k_pitch_env, k_pitch_env_decay, k_pitch_env_amt, k_boom_inc, k_boom_env, k_boom_decay, k_boom_mix, k_boom_attack_env, k_boom_attack_inc, k_reed_nl_enabled, k_reed_nl_drive, k_snare_freq_b, k_snare_r_b, k_snare_freq_c, k_snare_r_c, k_modal_mix, k_onset_attack_ms */
    /* k_Init        */ { 850.00000f,    0.00000f,    0.00000f,    0.00000f,    1.69510f,    0.89300f,    1.00000f,    1.00000f,    0.00000f,    0.50000f,    0.00000f,    0.30000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Marimba     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_808Sub      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_AcSnare     */ {   0.00000f,    0.00000f,    0.00000f,    0.78000f,    1.76000f,    0.91800f,    0.00000f,    0.00260f,    0.00000f,    0.42000f,    0.00000f,    0.86000f, false,    0.00000f,    1.00000f,    0.99850f,   18.00000f, asn_bm,    1.00000f,    0.99920f,    0.12000f,    0.00000f,    0.00180f, false,    0.00000f, 4500.00000f,    0.86000f, 7200.00000f,    0.82000f,    0.15000f,    0.00000f},
    /* k_TubularBell */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    0.00000f},
    /* k_Timpani     */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.32000f,    2.00000f},
    /* k_Djambe      */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.04000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    3.50000f},
    /* k_Taiko       */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, tak_bm,    1.00000f,    0.99960f,    0.35000f,    0.00000f,    0.00220f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.28000f,    4.00000f},
    /* k_MarchSnare  */ {   0.00000f,    0.00000f,    0.00000f,    0.72000f,    1.74500f,    0.91200f,    0.00000f,    0.00280f,    0.00000f,    0.50000f,    0.00000f,    0.89000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f, 5200.00000f,    0.88000f, 8500.00000f,    0.84000f,    0.00000f,    0.00000f},
    /* k_Koto        */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Vibraphone  */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.00000f},
    /* k_Woodblock   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.08000f,    0.00000f},
    /* k_AcousticTom */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.05000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f, tom_bm,    1.00000f,    0.99945f,    0.05000f,    0.00000f,    0.00080f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    3.50000f},
    /* k_Cymbal      */ {3400.00000f,    1.00000f, 4500.00000f,    0.00000f,    0.00000f,    1.10000f,    0.00000f,    0.00000f,    0.00000f,    0.98000f,    0.00000f,    0.91000f, true,    0.30000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.15000f,    0.00000f},
    /* k_Gong        */ {1500.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.82000f,    0.00000f,    0.83000f, false,    0.24000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Kalimba     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.03000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.15000f,    0.00000f},
    /* k_SteelPan    */ { 200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    1.50000f},
    /* k_Claves      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    0.50000f},
    /* k_Cowbell     */ { 900.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.16000f,    0.00000f},
    /* k_Triangle    */ {1800.00000f,    0.00000f,    0.00000f,    0.00000f,    1.26000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.15000f,    0.00000f,    0.96000f, false,    0.16000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.40000f,    0.00000f},
    /* k_KickDrum    */ {   0.00000f,    0.00000f,    0.00000f,    0.03000f,    1.20000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.05000f, false,    0.00000f,    1.00000f,    0.99890f,    9.00000f, kck_bm,    1.00000f,    0.99940f,    0.43000f,    0.00000f,    0.00100f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    5.50000f},
    /* k_Clap        */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Shaker      */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Flute       */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.28000f,    0.00000f,    0.82000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Clarinet    */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.28000f,    0.00000f,    0.82000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, true,    3.70000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_PluckBass   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_GlassBowl   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.00000f},
    /* k_GuitarStr   */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_HiHatClosed */ {3400.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.86000f,    0.00000f,    0.39000f, true,    0.34000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.24000f,    0.50000f},
    /* k_HiHatOpen   */ {3600.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    1.00000f,    0.00000f,    0.93000f, true,    0.36000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.30000f,    0.00000f},
    /* k_Conga       */ { 400.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.22000f,    3.50000f},
    /* k_Handpan     */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    3.00000f},
    /* k_BellTree    */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.17000f,    0.00000f},
    /* k_SlitDrum    */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Ride        */ {1450.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.05000f, false,    0.32000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.14000f,    0.00000f},
    /* k_RideBell    */ {1200.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.05000f, false,    0.32000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.20000f,    0.00000f},
    /* k_Bongo       */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.18000f,    3.50000f},
    /* k_GlassBottle */ {   0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f},
    /* k_Tick        */ { 400.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.02000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f, false,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.00000f,    0.50000f}
};

inline static float preset_param(ProgramIndex program, ModelParamIndex param) {
  return model_param_presets[program][param];
}

SynthState state;

#ifdef UNIT_TEST_DEBUG
    // Expose private members for unit test introspection (test binary only).
    float m_coupling_depth_ut() const { return m_coupling_depth; }
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

        state.master_gain  = 1.0f;
        state.master_drive = 1.0f;
        state.mix_ab       = 0.5f; // Equal A/B mix
        state.tone         = 0.0f; // Neutral tilt EQ (LoadPreset restores the preset value)
        m_pitch_bend_mult  = 1.0f; // Clear any held bend so the next note plays in tune.

        // Always return to ResA edit context so LoadPreset (called next in Init)
        // applies preset data symmetrically to both resonators.
        m_is_resonator_a = true;
        m_is_resonator_b = true;

        // Force master filter back to safe Highpass mode and clear its accumulators
        state.master_filter.mode = 2;
        state.master_filter.set_coeffs(10.0f, 0.707f, default_sample_rate);
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
        m_preset_idx = idx;
        // Keep m_params[0] in sync so unit_get_param_value(0) returns the correct
        // preset index regardless of whether LoadPreset was called via setParameter
        // (Program knob) or directly via unit_load_preset().
        m_params[k_paramProgram] = idx;

        // Columns Map: NOTE keep the justification so it's easier to read!!
        // 0:Prgram | 1:Note | 2:Bank | 3:Sample | 4:MlltRes | 5:MlltStif | 6:VlMllR | 7:VlMllS
        // 8:Prtls  | 9:Model| 10:Dkay| 11:Mterl | 12:Tone   | 13:HitPos  | 14:Rel   | 15:Inharm
        // 16:LCut  | 17:TRad| 18:Gain| 19:NzMix | 20:NzRes  | 21:NzFltr  | 22:NzFrq | 23:Resnc
        // Column order matches the ParamIndex enum above.
        // Current parameters cover all core physical-modelling dimensions (exciter, resonator,
        // noise, master FX). Phase 12/13 in PROGRESS.md track future additions (TubRad, Tone, etc.).
        // Columns 15 (Inharm) and 16 (LowCut) store 1/10th of the effective value.
        // setParameter multiplies them back by 10 so the encoder travels 10× fewer steps.
        static const int32_t presets[k_NumPrograms][k_lastParamIndex] = {
            //  Prg  Nte  Bnk  Smp - MlRs MlSt VlRs VlSt - Ptls Mdl  Dky  Mtr - Ton  Hit  Rel  InHm - LwCt TbRd Gain NzMx - NzRs NzFl NzFq Rsnc
            //
            // COUPLING RULE (Phase 25: dynamic clamp in render loop):
            //   The render loop now dynamically clamps coupling injection to:
            //     safe_coupling ≤ (1 − feedback_gain) × 0.8
            //   This guarantees stability at ANY Partials/Decay combination.
            //   High Decay (feedback_gain→1) → coupling nearly zero (self-limiting).
            //   Low Decay (feedback_gain→0.85) → coupling up to ~0.12 (audible).
            //
            //   ResB is micro-detuned by +0.3% (~5 cents) in NoteOn to break
            //   mathematically perfect beating between matched resonators.
            //
            //   Hit (HitPos=mix_ab): only relevant when ResB is active (Ptls≥2, Mdl=3/5).
            //   Set Hit=0 for single-resonator presets so output is not halved.
            //
            //                            ÷10                           ÷10                                                              ÷10
            {   0,  60,   0,   0, 500, 470,   0,   0,   0,   0,  35,  10,   0,   0,  10,   0,   1,   3,   0,   0, 300,   0,1200, 707},        // 0:  InitDbg    — pure KS string, no coupling
            {   1,  72,   0,   1, 800, 130,   0,   0,   0,   6, 194,  -7,   0,   0,   5,  15,   1,   7,  20,   0, 300,   0,1200, 707},      // 1:  Marimba    — sample: C5/1.0s→Dkay184; B=0.0075→InHm15; centroid→Mterl-9; Note60→72
            {   2,  36,   0,   0, 150,   0,   0,   0,   0,   3, 180,  -6,  -5,   0,  15,   0,   1,   3,   0,   0, 300,   0,1200, 707},        // 2:  808 Sub    — final Stage-1: Dkay170/Mterl-6 to counter LP-loss-shortened tail without adding noise
            {   3,  38,   0,   1, 120, 280,   0,   0,   2,   5, 168,  -7,   0,  46,   9,   3,   0,   8,   7,  61, 740,   2, 480, 707},        // 3:  Ac Snare   — brighter wire path: higher NzMix/NzRes/NzFq to feed the new stronger snare-wire resonator
            {   4,  72,   0,   1, 900, 340,   0,   0,   0,   1, 200,  30,   0,   0,  20,   5,  20,  18,   0,   5, 300,   0,1500, 707},     // 4:  TblrBel    — c=0.98@524Hz (Mterl28+TubRad20); MlltStif100 (medium felt mallet, less overtone energy → measured T60 tracks fundamental ~7.5s)
            {   5,  40,   0,   1, 360, 300,   0,   0,   2,   3, 200,  10,   0,  36,  18,   8,   0,  -4,   4,   1, 420,   0, 380, 707},    // 5:  Timpani    — InHm9: slight spread on coupled membrane mode; NzRs300 longer noise tail. rescue pass: deeper boom (darker loss) with longer low-body sustain and broader impact
            {   6,  48,   0,   1, 600, 350,   0,   0,   1,   5, 152,   0,   0,  35,  12,   8,   5,  15,   5,   7, 450,   0, 500, 707},      // 6:  Djambe     — Dkay102/Mterl0: drier djembe body with wider noise cutoff
            {   7,  41,   0,   1, 250, 390,   0,   0,   1,   5, 200,  10,   0,  30,  15,   1,   1,  11,   5,   9, 550,   0, 130, 707},       // 7:  Taiko      — harder mallet + reduced noise tail NzMx14 + lower NzFq for thud character
            {   8,  65,   0,   1, 720, 500,   0,   0,   1,   5, 190,  20,   0,  50,   8,  16,  25,  19,   5,  73, 800,   2, 105, 707},    // 8:  MrchSnr    — NzMx=73/NzFq=105: low-freq dense noise raises flatness to match snare reference; Mterl=20/NzHi=0.86
            {   9,  60,   0,   1, 600, 335,   0,   0,   0,   0, 185,  12,   0,   0,  12,   3,   1,   7,   0,   0, 300,   0,1000, 707},       // 09: Koto       — InHm3 adds light inharmonic shimmer; no noise for cleaner pluck
            {  10,  72,   0,   1, 500, 300,   0,   0,   0,   1, 200,  28,   0,   0,  18,   1,   1,  13,   0,   0, 300,   0,1000, 707},     // 10: Vibrph     — Mterl28/TbRd13 (=GtrStr): coeff≈0.976→dc_gain≈0.998→T60≈3.9s@C5; within [1.13,16.6]s test bounds
            {  11,  48,   0,   1, 900, 500,   0,   0,   0,   2, 156,  24,   0,   0,   2,  10,   1,   3,   0,   5, 420,   0, 900, 707},       // 11: Wodblk     — NzMx5 light transient click; NzRs420 short burst
            {  12,  45,   0,   1, 450, 300,   0,   0,   2,   5, 200,   0,   0,  44,  11,   0,   0,   9,   5,   3, 360,   0, 520, 707},       // 12: Ac Tom     — Drumhead gain curve: Dkay=90→g=0.697→T60≈195ms@110Hz; boom_mix=0.24 body lift
            {  13,  65,   0,   1, 800, 450,   0,   0,   0,   4, 200,  28,   0,   0,  18,   8,   5,  15,   5,  40, 640,   2, 340, 707},     // 13: Cymbal     — note65(349Hz) mode6=991Hz; Dkay199 T60~11.3s; Mterl30→lossless LP; NzMx15→KS/modal dominant
            {  14,  50,   0,   1, 200,  20,   0,   0,   0,   4, 190,   1,   0,   0,  20,   8,   1,  21,  20,  19, 860,   0,  30, 707},       // 14: Gong       — reduce darkness and bias more upper partial noise onset
            {  15,  65,   0,   1, 700, 390,   0,   0,   0,   1, 192,   6,   0,   0,   5,   0,   1,   7,   3,  10, 260,   0, 720, 707},        // 15: Kalimba    — darker/lower-inharmonic tine with lower noise mix
            {  16,  60,   0,   1, 600,   0,   0,   0,   0,   4, 200,  18,   0,   0,  12,   0,   3,   9,   5,   0, 300,   0,1000, 707},        // 16: StelPan    — Mterl14 brighter pan + TbRd11 for steelpan inharmonic partial spread
            {  17,  79,   0,   1, 900, 480,   0,   0,   0,   2,  13,  -3,   0,   0,   1,   0,   1,   1,   0,  20, 260,   0, 800, 707},         // 17: Claves     — final Stage-1: InHm3 to reduce audible inharmonic beating while keeping wood attack
            {  18,  67,   0,   1, 800, 420,   0,   0,   0,   4, 175,  20,   0,   0,   4,  28,  20,   3,  30,   5, 300,   0,1000, 707},   // 18: Cowbell    — Dkay:55→175 (~2s metallic ring); InHm:1700→200 (moderate plate inharmonicity)
            {  19,  69,   0,   1, 900, 470,   0,   0,   0,   1, 190,  20,   0,   0,  15,  22, 120,  20,   0,   5, 300,   0,1500, 707},  // 19: Triangle   — note 77→69: A4 trigger gives best dual-ref mean (75.77 vs 80.58 at F5)
            {  20,  36,   0,   1, 380, 350,   0,   0,   2,   5, 195,  -5,   0,  38,   6,   4,   0,   3,   6,  15, 220,   0, 220, 707},       // 20: Kick Drum  — MlSt350 (from 120): rendered 0-5ms had only 24% hi vs ref 82%; stiffer mallet sharpens beater click. Drumhead gain curve: Dkay=55→g=0.601→T60≈175ms@65Hz; boom_mix=0.40 dominates after body ring; pitch sweep 9st; auto_tune: TbRd1→3, AtkMs5.5ms (63.09→59.10)
            {  21,  60,   0,   1, 500, 270,   0,   0,   2,   5,  15,   5,   0,  50,   3,   0,  10,   3,   5,  95, 600,   2, 600, 707},       // 21: Clap       — NzMx95: maximum noise content for hand-clap character
            {  22,  72,   0,   1, 100, 370,   0,   0,   2,   5,  12,  10,   0,  50,   2,   0,  20,   3,   3,  90, 900,   2, 800, 707},      // 22: Shaker     — Dkay12/Mterl10: dry rattle body; NzMx90 high noise content
            {  23,  72,   0,   1, 100, 132,   0,   0,   0,   7, 200,  22,   0,   0,  12,   0,   1,   3,   0,  15, 950,   0, 340, 707},      // 23: Flute      — Mterl22 silver flute tube (brighter); T60≈0.71s→measured≥0.26s test bound; NzMx15 breath noise
            {  24,  72,   0,   0,  50, 180,   0,   0,   0,   8, 195,   8,   0,   0,  12,   2,   1,  12,   0,  29, 940,   0,1500, 707},      // 24: Clarinet   — stronger reed odd-harmonic generation support via brighter tube/noise input
            {  25,  36,   0,   1, 600, 250,   0,   0,   0,   0, 105,  -6,   0,   0,  10,   0,   1,   3,  40,   0, 300,   0, 500, 707},      // 25: PlkBass    — final Stage-1: less drive + harder mallet / slightly longer decay for cleaner pluck body
            {  26,  76,   0,   1, 700,  50,   0,   0,   0,   4, 200,  30,   0,   0,  18,  10,  10,  18,   0,   0, 300,   0,1200, 707},    // 26: GlsBwl     — InHm10 glass bowl partials; no noise for pure bowl character
            // 27: Guitar String — Karplus-Strong reference for physical model validation.
            // A4 = 440 Hz (standard pitch reference).  Dkay=195 → g≈0.9953 → T_60≈3.3 s.
            // Single resonator (Partls=0, no coupling), no noise (NzMix=0), no sample (Smp=0).
            // Hit=0: full ResA output (HitPos=50 would halve the signal when ResB is disabled).
            // InHm=0: pure Karplus-Strong, no allpass inharmonicity — cleanest reference.
            // Expected: bright pluck attack, gradual spectral darkening, ~3-second sustain.
            // Validate: (1) pitch = 440 Hz with a tuner app; (2) audible at 3 s;
            //           (3) no flutter/beating (one clean tone per press).
            //  Prg  Nte  Bnk  Smp - MlRs MlSt VlRs VlSt - Ptls Mdl  Dky  Mtr - Ton  Hit  Rel  InHm - LwCt TbRd Gain NzMx - NzRs NzFl NzFq Rsnc
            {  27,  69,   0,   0, 800, 500,   0,   0,   0,   0, 200,  28,   0,   0,  15,   0,   1,  13,   0,   0, 300,   0,1200, 707},  // 28: Guitar String — KS reference, A4, T60≈3.3s
            // ── New kit voices ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
            //  Prg  Nte  Bnk  Smp - MlRs MlSt VlRs VlSt - Ptls Mdl  Dky  Mtr - Ton  Hit  Rel  InHm - LwCt TbRd Gain NzMx - NzRs NzFl NzFq Rsnc
            {  28,  79,   0,   1, 900, 500,   0,   0,   0,   4, 160,  14,   0,   0,   2,  15,   5,   3,   0,  58, 920,   2, 900, 707},      // 28: HHat-C  — Mterl12 (from 26): centroid was 15-16kHz vs ref 7kHz; darker LP reduces KS harmonics above ~10kHz. softer mallet MlSt420 + InHm14 metallic partial spread + NzMx35/NzFq600. rescue pass: tighter short chick with brighter/sparser metallic top
            {  29,  79,   0,   1, 900, 490,   0,   0,   0,   4, 210,  18,   0,   0,  14,  12,   5,  12,   0,  79,1000,   2,1500, 707},  // 29: HHat-O  — brighter upper band and noise weighting
            {  30,  62,   0,   1, 600, 425,   0,   0,   1,   5, 158,   3,   0,   0,  10,   8,   2,   9,   0,  15, 520,   0, 710, 707},      // 30: Conga   — softer MlSt365 + TbRd9 + NzMx15/NzFq650 for tighter conga snap
            {  31,  62,   0,   1, 700, 300,   0,   0,   0,   4, 190,  22,   0,   0,  20,   0,   5,  18,   0,   5, 300,   0,1000, 707},     // 31: Handpn  — Mterl22/TbRd20 nitrided steel; coeff≈0.960→dc_gain≈0.996→T60≈1.58s measured; within [1.42,20.8]s test bounds
            {  32,  84,   0,   1, 900, 420,   0,   0,   0,   1, 200,  20,   0,   0,   8,  10,  10,   3,   0,   0, 300,   0,1200, 707},     // 32: BelTre  — Beam, T60=1.0s@C6→Dkay193; Mterl20 very bright; InHm10 metallic partial spread
            {  33,  60,   0,   1, 700, 270,   0,   0,   0,   6, 177,   8,   0,   0,  10,   6,   2,   3,   0,   0, 300,   0, 800, 707},        // 33: SltDrm  — MarBar, T60=1.0s@C4→Dkay167; Mterl8 mid-bright wood; InHm6 (B≈0.003)
            {  34,  57,   0,   1, 900, 500,   0,   0,   0,   4, 190,  28,   0,   0,  18,   7,   5,  17,   0,  15, 700,   2, 600, 707},     // 34: Ride    — InHm6 plate spread; NzMx15/NzRs700 sizzle-only noise character
            {  35,  60,   0,   1, 900, 491,   0,   0,   0,   4, 200,  16,   0,   0,   8,  11,   5,   1,   0,  20, 600,   2, 700, 707},      // 35: RidBel  — InHm10 bell spread; TbRd3; NzFq700 higher sizzle
            {  36,  57,   0,   1, 650, 410,   0,   0,   0,   5, 162, -10,   0,   0,   8,   4,   2,  -1,   0,   0, 520,   0, 450, 707},       // 36: Bongo   — harder mallet MlSt410 for sharper bongo slap; InHm6 tonal cue
            {  37,  88,   0,   1, 100, 450,   0,   0,   0,   7, 200,   5,   0,   0,   5,   0,   2,  19,   0,  50, 110,   0, 390, 707},       // 37: GlsBotl — MlSt450 harder blow onset; Mterl5 brighter; NzMx45/NzRs150 short puff
            {  38,  49,   0,   1, 900, 500,   0,   0,   0,   4, 200,   7,   0,   0,   3,  14,   5,   1,   0,  24, 150,   2, 400, 707}       // 38: Tick    — InHm16 tight wood spread; NzMx24/NzRs150 crisp tick transient
        };

        if (idx >= k_NumPrograms) return;

        // Preset loading always targets ResA first, then ResB, regardless of the
        // current editor selection.  Save both flags and restore them afterwards so
        // a preset load never changes which resonator(s) the user is editing.
        bool saved_is_a = m_is_resonator_a;
        bool saved_is_b = m_is_resonator_b;
        m_is_resonator_a = true;
        m_is_resonator_b = false;

        // Apply parameters, SKIPPING INDEX 0 to prevent infinite recursion stack overflow!
        // Also skip Bank and Sample so the user's sample selection persists
        // across preset changes (user reported: sample resets to bank 0, sample 1
        // on every preset change).
        for (uint8_t param_id = 0; param_id < 24; ++param_id) {
            if (param_id == k_paramProgram) continue;
            if (param_id == k_paramBank)    continue;
            if (param_id == k_paramSample)  continue;

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

        // Restore both flags so the user's edit context survives preset loads.
        m_is_resonator_a = saved_is_a;
        m_is_resonator_b = saved_is_b;

        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
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
                                   modal_cfg.ratio5, modal_cfg.ratio6);
            }
        }
    }

    static inline const char * getPresetName(uint8_t idx) {
        static const char* const preset_names[] = {
            "InitDbg", "Marmba", "808Sub", "AcSnre",
            "TblrBel", "Timpni", "Djambe", "Taiko",
            "MrchSnr", "Koto",   "Vibrph",
            "Wodblk",  "Ac Tom", "Cymbal", "Gong",
            "Kalimba", "StelPan","Claves", "Cowbel",
            "Trngle",  "Kick",    "Clap",  "Shaker",
            "Flute",   "Clrint", "PlkBss", "GlsBwl",
            "GtrStr",
            "HHat-C",  "HHat-O", "Conga",  "Handpn",
            "BelTre",  "SltDrm",
            "Ride",    "RidBel",
            "Bongo",   "GlsBotl", "Tick"
        };
        if (idx < k_NumPrograms) return preset_names[idx];
        return "Unknown";
    }

    // ==============================================================================
    // 2. Parameter Binding (UI Thread)
    // ==============================================================================
    inline void setParameter(uint8_t index, int32_t value) {
        // CRITICAL UI FIX: Prevent OS out-of-bounds writes
        if (index >= 24) return;
        m_params[index] = value;

        switch(index) {
            case k_paramProgram:
                LoadPreset((uint8_t)value);
                break;

            case k_paramNote:
                m_ui_note = (uint8_t)fmaxf(1.0f, fminf(126.0f, value));
                break;

            case k_paramBank:
                m_sample_bank = value;
                break;

            case k_paramSample:
                m_sample_number = value;
                break;
            case k_paramMlltStif: {
                // Stored ÷10 (10-500 represents 100-5000). Divide by 500 (new max).
                float norm = fmaxf(0.01f, fminf(1.0f, (float)value * 0.002f));
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
                if (value <= 1999) {
                    // Stored 0-1999; effective range 0-19990 (×10). Divide by 2000 to normalise.
                    float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.0005f));
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
                // Stored 1-1999; effective range 10-19990 Hz (×10 scaling).
                m_master_cutoff = (float)value * 10.0f;
                // Divide by 1000: UI stores 707-4000, filter needs 0.707-4.0
                float res_val = fmaxf(0.707f, (float)m_params[k_paramResnc] * 0.001f);
                state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
                break;
            }
            case k_paramGain: {
                float norm = fmaxf(0.0f, (float)value * 0.01f);
                state.master_drive = 1.0f + (norm * 20.0f);
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
                // UI passes 707 to 4000. Divide by 1000 to get a Q factor of 0.707 to 4.0
                float res_val = fmaxf(0.707f, (float)value * 0.001f);
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
                     // Private split-band high cutoff (no extra UI): tied to NzFq,
                     // shifted upward for sizzle branch and converted to 1-pole coeff.
                     float hi_hz = fminf(20000.0f, fmaxf(300.0f, freq * 2.2f));
                     float alpha = fminf(0.95f, fmaxf(0.02f, (2.0f * M_PI * hi_hz) * inverse_default_sample_rate));
                     state.voices[i].exciter.noise_hi_lp_coeff = alpha;
                }
                break;
            }

            default:
                break;
        }
    }

    // Show A or B label depending on the resonator selected via the Partls selector.
    // IMPORTANT: always index into arrays with the function's `value` argument —
    // never with stored state — so scrolling through values shows the correct label.
    inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
        static const char* const bank_names[] = { "CH", "OH", "RS", "CP", "MISC", "USER", "EXP" };
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
        } else if (index == k_paramBank) {
            if (value >= 0 && value < 7) return bank_names[value];
        } else if (index == k_paramModel) {
            if (value >= 0 && value < 9)
                return m_is_resonator_a && m_is_resonator_b ? model_names_ab[value] :
                    m_is_resonator_a ? model_names_a[value] : model_names_b[value];
        } else if (index == k_paramPartls) {
            if (value == 5) return "-> ResA+B";
            if (value == 6) return "-> ResA";
            if (value == 7) return "-> ResB";
            if (value >= 0 && value < 5)
                return m_is_resonator_a && m_is_resonator_b ? partial_names_ab[value] :
                    m_is_resonator_a ? partial_names_a[value] : partial_names_b[value];
        } else if (index == k_paramNzFltr) {
            if (value >= 0 && value < 3) return nz_filter_names[value];
        } else if (index == k_paramMlltStif) {
            // Stored ÷10; show real ×10 value (100-5000)
            static char ms_buf[8];
            snprintf(ms_buf, sizeof(ms_buf), "%d", (int)(value * 10));
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
                int32_t khz_i = hz / 1000;
                int32_t khz_d = (hz % 1000) / 100;
                snprintf(nf_buf, sizeof(nf_buf), "%d.%dkHz", khz_i, khz_d);
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
        }

        // Unconditional failsafe to prevent OS screen crashes
        return "---";
    }

    // ==============================================================================
    // 4. Sequencer and MIDI Routing
    // ==============================================================================
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        state.next_voice_idx = (state.next_voice_idx + 1) % NUM_VOICES;
        VoiceState& v = state.voices[state.next_voice_idx];

        // Capture before is_active is overwritten: only a previously-active voice
        // has residual delay-line data that needs clearing on retrigger.
        // A fresh slot (never used since Reset()) is already zero — skip the work.
        const bool had_residual = v.is_active || v.is_releasing;

        // CRITICAL FIX 2: Ensure the voice actually turns on!
        v.is_active = true;
        v.is_releasing = false;

        // --- Sample Loading ---
        // First, clear any old sample data
        v.exciter.sample_ptr = nullptr;
        v.exciter.sample_frames = 0;

        // Then, try to load the new one just-in-time.
        // CRITICAL: m_sample_number == 0 means "no sample" (Smp=0 in the preset table).
        // Without this guard, the ternary (m_sample_number > 0) ? ... : 0 falls through
        // to actualIndex=0, which loads hardware bank 0 / sample 0 on EVERY preset —
        // even those explicitly set to Smp=0 (e.g. GtrStr).  The unit tests hide this
        // because mock_get_sample() always returns nullptr, masking the real hardware path.
        if (m_sample_number > 0 &&
            m_get_sample && m_get_num_sample_banks_ptr && m_get_num_samples_for_bank_ptr) {
            if (m_sample_bank < m_get_num_sample_banks_ptr()) {
                size_t actualIndex = (size_t)(m_sample_number - 1);  // 1-indexed: Smp=1→idx 0
                if (actualIndex < m_get_num_samples_for_bank_ptr(m_sample_bank)) {
                    const sample_wrapper_t* wrapper = m_get_sample(m_sample_bank, actualIndex);
                    if (wrapper && wrapper->sample_ptr) {
                        v.exciter.sample_ptr = wrapper->sample_ptr;
                        v.exciter.sample_frames = wrapper->frames;
                        v.exciter.channels = wrapper->channels;
                    }
                }
            }
        }
        // --- End Sample Loading ---

        v.current_note = note;
        v.current_velocity = (float)velocity * 0.007874015f;    // approx 1 / 127
        // --- 2D DRUMHEAD STRIKE PHYSICS ---
        // 1. Calculate the physical strike location once for the entire voice
        float hit_x = (float)m_params[k_paramHitPos] * 0.01f;
        float hit_y = (1.0f - v.current_velocity) * hit_x * 0.5f;

        // Use our fast-math approximation to find distance from center (0.0 to 1.0)
        float radius = sqrtsum2acc(hit_x, hit_y); //
        radius = fminf(1.0f, radius);

        // --- VELOCITY MODULATION ---
        // VlMllStf: harder hit → stiffer (brighter) mallet.
        // Override the global mallet_stiffness on this specific voice only,
        // so soft hits are round and hard hits are sharp without changing other voices.
        {
            float base_stiff = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.002f));
            float stif_mod   = (float)m_params[k_paramVlMllStf] * 0.01f; // -1.0 to +1.0
            // Add up to a 50% stiffness boost when striking at the absolute edge
            float rim_stiffness_boost = radius * 0.5f;
            v.exciter.mallet_stiffness = fmaxf(0.01f, fminf(1.0f,
                base_stiff + stif_mod * v.current_velocity + rim_stiffness_boost));
        }

        // VlMllRes: harder hit → faster noise attack (sharper transient).
        // Override the noise_env attack_rate on this voice so it responds to accents.
        {
            float base_nz     = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzRes] * 0.001f));
            float base_attack = 0.9f - (base_nz * 0.8f);
            float res_mod     = (float)m_params[k_paramVlMllRes] * 0.01f; // -1.0 to +1.0
            // Add up to a 10% speed boost to the attack rate for extreme rim hits
            float rim_snap_boost = radius * 0.1f;

            v.exciter.noise_env.attack_rate = fmaxf(0.01f, fminf(0.99f,
                base_attack + (res_mod * v.current_velocity * 0.5f) + rim_snap_boost)); //
            // High-band burst should stay snappier than low-band burst.
            v.exciter.noise_env_hi.attack_rate = fmaxf(0.05f, fminf(0.99f,
                v.exciter.noise_env.attack_rate * 1.25f));
            // Snare-family physical staging: body thud first, then wire buzz.
            // attack_rate=0.001: 21% noise at 5ms, 62% at 20ms, 91% at 50ms.
            // (0.01 was too fast: 91% noise already at 5ms, so no staging effect.)
            if (m_preset_idx == k_AcSnare || m_preset_idx == k_MarchSnare) {
                v.exciter.noise_env.attack_rate = 0.001f;
                v.exciter.noise_env_hi.attack_rate = 0.001f;
                // Band A: velocity-controlled centre (~2.8 kHz for AcSnare, ~3.5 kHz for MrchSnr)
                float vq = fmaxf(0.0f, fminf(1.0f, v.current_velocity));
                float r_a = 0.90f + (0.07f * vq);
                float freq_a = (m_preset_idx == k_MarchSnare) ? 3500.0f : 2800.0f;
                float w_a = (2.0f * M_PI * freq_a) * inverse_default_sample_rate;
                v.exciter.snare_wire_a1 = 2.0f * r_a * fastercosfullf(w_a);
                v.exciter.snare_wire_a2 = r_a * r_a;

                // Band B: per-preset centre freq + pole radius (fallback: 4.5 kHz, r=0.86)
                float freq_b = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_snare_freq_b);
                float r_b_base = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_snare_r_b);
                if (freq_b < 100.0f) freq_b = 4500.0f;
                if (r_b_base < 0.3f) r_b_base = 0.86f;
                float r_b = r_b_base + (0.05f * vq);
                float w_b = (2.0f * M_PI * freq_b) * inverse_default_sample_rate;
                v.exciter.snare_wire_a1b = 2.0f * r_b * fastercosfullf(w_b);
                v.exciter.snare_wire_a2b = r_b * r_b;

                // Band C: per-preset centre freq + pole radius (fallback: 7.2 kHz, r=0.82)
                float freq_c = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_snare_freq_c);
                float r_c_base = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_snare_r_c);
                if (freq_c < 100.0f) freq_c = 7200.0f;
                if (r_c_base < 0.3f) r_c_base = 0.82f;
                float r_c = r_c_base + (0.03f * vq);
                float w_c = (2.0f * M_PI * freq_c) * inverse_default_sample_rate;
                v.exciter.snare_wire_a1c = 2.0f * r_c * fastercosfullf(w_c);
                v.exciter.snare_wire_a2c = r_c * r_c;
            }
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
        // Master envelope: auto-decay from 1.0 toward 0.0 at decay_rate.
        // This ensures percussion sounds decay naturally even with gate held
        // (the Drumlogue trigger button behaviour).  NoteOff switches to the
        // faster release_rate for a clean tail-off.
        //
        // Direct assignment avoids the  trigger() + process()  pattern that
        // relied on value >= 0.99f after one multiply-add.  ARM -ffast-math
        // may emit an FMA whose rounding leaves value fractionally below 0.99f.
        //
        // decay_rate and release_rate are set by setParameter(k_paramDkay).
        v.exciter.master_env.sustain_level = 0.0f;
        v.exciter.master_env.value = 1.0f;
        v.exciter.master_env.state = ENV_DECAY;

        // Stage-1 transient complexity: short coefficient modulation window.
        // Deterministic per-hit micro-randomization from note/voice/velocity.
        float vel_norm = fmaxf(0.0f, fminf(1.0f, velocity  * 0.007874015f));    // approx 1 / 127
        uint32_t seed = (uint32_t)note * 1103515245u
                      ^ (uint32_t)state.next_voice_idx * 12345u
                      ^ (uint32_t)velocity * 2654435761u;
        float r = ((float)((seed >> 8) & 0xFFFFu) * 3.05180437934e-5f) - 1.0f; // [-1, +1] - approx 1 / 32767.5f
        v.transient_frames_total = (uint32_t)(default_sample_rate * 0.035f); // 35 ms
        v.transient_frames_left = v.transient_frames_total;
        v.transient_inv_total = (v.transient_frames_total > 0) ? (1.0f / (float)v.transient_frames_total) : 0.0f;
        v.transient_lp_jitter = fmaxf(-0.08f, fminf(0.08f, (0.05f * vel_norm) + (0.02f * r)));
        v.transient_ap_jitter = fmaxf(-0.03f, fminf(0.03f, (0.015f * vel_norm) - (0.01f * r)));

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
        // Bullet-1 step 3 start: dedicated metallic HF exciter emphasis.
        // For cymbal/gong/open-hat, keep a stronger independent high-band path
        // so upper shimmer is less tied to the KS loop loss behavior.
        if (m_preset_idx == k_Cymbal || m_preset_idx == k_Gong || m_preset_idx == k_HiHatOpen) {
          v.exciter.noise_band_mix = fmaxf(v.exciter.noise_band_mix, 0.92f);
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
            v.exciter.noise_env_hi.decay_rate = 0.000007f;  // ~3s high-band shimmer
            v.exciter.noise_env.decay_rate    = 0.0000035f; // ~6s low-band wash
            v.hf_branch_decay = 0.9998f;                    // T60~720ms upper shimmer
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
        {
            float atk_ms = preset_param(preset, k_onset_attack_ms);
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
          v.metal_fm_inc = (2.0f * M_PI * base_fm_hz) * inverse_default_sample_rate;
          v.metal_fm_env = 1.0f;
          v.metal_fm_decay = (m_preset_idx == k_HiHatClosed) ? 0.9955f : 0.9978f;
          // HHat-O: lower FM depth (0.16→0.06) so the chirp doesn't re-excite KS
          // harmonics above 5 kHz as strongly; SVF BP@8kHz noise can then dominate.
          v.metal_fm_depth = (m_preset_idx == k_HiHatClosed) ? 0.08f : (m_preset_idx == k_HiHatOpen) ? 0.06f
                                                                                                     : 0.16f;
          }
        // Modal bank: re-initialize on every NoteOn so frequencies track the played note
        // and envelopes are velocity-scaled. LoadPreset called init_modal_modes with
        // current_velocity=0 (default) which zeroed all modal_env_X values, silencing
        // the entire modal synthesis path — Cymbal and Gong were running without it.
        {
            const ModalPresetConfig& mc = modal_preset_configs[m_preset_idx];
            if (mc.mode_count > 0) {
                float modal_mix_val = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_modal_mix);
                v.init_modal_modes(mc.ratio2, mc.ratio3, mc.ratio4,
                                   mc.t60_1_ms, mc.t60_2_ms, mc.t60_3_ms, mc.t60_4_ms,
                                   modal_mix_val, mc.env1, mc.env2, mc.env3, mc.env4,
                                   mc.mode_count, mc.ratio5, mc.ratio6);
            }
        }
}

    inline void NoteOff(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];

            // Find the voice playing this note that hasn't already been released
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;

                v.exciter.noise_env.release();
                v.exciter.noise_env_hi.release();
                v.exciter.master_env.release();
            }
        }
    }

    inline void GateOff() {
        // The internal Drumlogue sequencer releases the UI note
        NoteOff(m_ui_note);

        // Reset the voice allocator so the next strike starts at Voice 0.
        // Because NoteOn pre-increments before use, setting to (NUM_VOICES - 1)
        // means the very next NoteOn wraps to index 0.
        //
        // Without this reset: round-robin cycles through voices 1,2,3,0,1,2,3,0,...
        // so each successive gate press uses a different slot.  When the note has a
        // long T_60 (e.g. GtrStr ~5 s), four different voice slots accumulate residual
        // energy at different phases, causing constructive/destructive interference
        // ("beating") and progressive amplitude variation across presses.
        //
        // With this reset: every gate press always starts at Voice 0.  Concurrent
        // notes within the same gate still allocate voices 0→1→2→3 correctly, because
        // each NoteOn call still increments before use.
        state.next_voice_idx = NUM_VOICES - 1;
    }

    inline void AllNoteOff() {
        // Panic button: aggressively release everything
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].is_releasing = true;
            state.voices[i].exciter.noise_env.release();
            state.voices[i].exciter.noise_env_hi.release();
            state.voices[i].exciter.master_env.release();
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
            if (m_preset_idx == k_AcousticTom) {
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

    // Processes the Exciter (Generates the initial "strike" or sample burst)
    inline float process_exciter(ExciterState& ex) {
        float out = 0.0f;

        if (ex.sample_ptr && ex.current_frame < ex.sample_frames) {
            size_t raw_idx = ex.current_frame * ex.channels;
            out = ex.sample_ptr[raw_idx];
        }

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
            //   - low band: filtered (body/snap tail)
            //   - high band: unfiltered (fast click/hiss burst)
            ex.noise_lp_state += 0.15f * (raw_noise - ex.noise_lp_state);
            float low = ex.noise_lp_state;
            ex.noise_hi_lp_state += ex.noise_hi_lp_coeff * (raw_noise_unf - ex.noise_hi_lp_state);
            float high = raw_noise_unf - ex.noise_hi_lp_state;
            float mix = fmaxf(0.0f, fminf(1.0f, ex.noise_band_mix));
            float low_part = low * (1.0f - mix) * noise_env_low;
            float high_part = high * mix * 1.35f * noise_env_high;
            if (mix > 0.80f) {
                // Hi-hat family: dedicated BP biquad for centroid control near 7 kHz.
                float hat_bp = ex.hat_filter.process(raw_noise_unf);
                high_part = (ex.use_hat_filter ? hat_bp : raw_noise) * mix * 1.35f * noise_env_high;
            }
            float noise_sum = (low_part + high_part) * ex.noise_decay_coeff;
            if (ex.snare_wire_mix > 0.001f) {
                // Phase-B crack burst: broadband onset before wire resonance engages.
                float crack_burst = high * noise_env_high * (1.0f - ex.wire_onset_env) * 0.90f;
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

        for (int voice_idx = 0; voice_idx < NUM_VOICES; ++voice_idx) {
            VoiceState& voice = state.voices[voice_idx];
            if (!voice.is_active) continue;

            // Pre-compute model-aware coupling clamps once per block.
            // feedback_gain is constant during audio rendering, so this runs once per voice
            // per processBlock() call instead of once per sample — saves ~128 fminf() calls.
            //
            // Different-frequency resonator pairs (membrane/drumhead, ResB tuned to the Bessel
            // (1,1) mode ratio 0.628× base pitch) are phase-incoherent: coupling energy from
            // ResB arrives at a different phase on every round trip, partially cancelling rather
            // than always constructively adding.  This allows a 3× more permissive clamp (K=2.5
            // vs K=0.8) so the Partials knob remains audible at high Decay values without
            // the exponential explosion that full coupling causes for same-frequency pairs.
            //
            // Stability check (Timpani worst case: g=0.958, Ptls=2, diff_freq):
            //   safe_cpl = min(0.25, 0.042 × 2.5) = min(0.25, 0.105) = 0.105
            // vs old value: min(0.25, 0.034) = 0.034 (below audibility).
            float v_safe_cpl_a = 0.0f, v_safe_cpl_b = 0.0f;
            if (m_active_partials >= 16) {
                float half_depth = m_coupling_depth * 0.5f;
                float delay_ratio_diff = (voice.resA.delay_length > 0.1f)
                    ? fabsf(1.0f - voice.resB.delay_length / voice.resA.delay_length)
                    : 0.0f;
                if (delay_ratio_diff > 0.05f) {
                    // Incoherent (different-pitch) pair.
                    // Phase incoherence reduces average coupling energy, but the worst-case
                    // beat alignment still satisfies G + C ≤ 1 only when C ≤ 1-G.
                    // K=2.5 violated this (G+C = 2.5 - 1.5G > 1 for all G < 1), causing
                    // exponential amplitude growth in long-decay presets (Timpani, Djambe).
                    // Use the same K=0.8 as same-pitch pairs to guarantee stability.
                    v_safe_cpl_a = fminf(half_depth, (1.0f - voice.resA.feedback_gain) * 0.8f);
                    v_safe_cpl_b = fminf(half_depth, (1.0f - voice.resB.feedback_gain) * 0.8f);
                } else {
                    // Coherent (same-pitch) pair: conservative stability clamp
                    v_safe_cpl_a = fminf(half_depth, (1.0f - voice.resA.feedback_gain) * 0.8f);
                    v_safe_cpl_b = fminf(half_depth, (1.0f - voice.resB.feedback_gain) * 0.8f);
                }
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
                    if (voice.metal_fm_phase > (2.0f * M_PI)) voice.metal_fm_phase -= (2.0f * M_PI);
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

                if (voice.transient_frames_left > 0 && voice.transient_frames_total > 0) {
                    float t = (float)voice.transient_frames_left * voice.transient_inv_total;
                    float decay = t * t; // smoother fade-out
                    float lp_off = voice.transient_lp_jitter * decay;
                    float ap_off = voice.transient_ap_jitter * decay;
                    voice.resA.lowpass_coeff = fmaxf(0.01f, fminf(0.999f, voice.transient_lp_base_a + lp_off));
                    voice.resB.lowpass_coeff = fmaxf(0.01f, fminf(0.999f, voice.transient_lp_base_b + lp_off));
                    // Keep AP modulation symmetric around 0 so transient jitter can push
                    // either toward positive or negative dispersion.
                    // NOTE: Waveguide allpass explicitly supports [-0.99, +0.99].
                    voice.resA.ap_coeff = fmaxf(-0.99f, fminf(0.99f, voice.transient_ap_base_a + ap_off));
                    voice.resB.ap_coeff = fmaxf(-0.99f, fminf(0.99f, voice.transient_ap_base_b + ap_off));
                    voice.transient_frames_left--;
                } else {
                    voice.resA.lowpass_coeff = voice.transient_lp_base_a;
                    voice.resB.lowpass_coeff = voice.transient_lp_base_b;
                    voice.resA.ap_coeff = voice.transient_ap_base_a;
                    voice.resB.ap_coeff = voice.transient_ap_base_b;
                }

                // ── Stage 2: Waveguide resonators ──────────────────────────
                // If Stage 2 is silent but Stage 1 is not, the waveguide has
                // zero delay_length or zero feedback_gain on this hardware.
                //
                // Model-aware coupling clamps are pre-computed once per block above.
                // Both diff-frequency and same-frequency pairs use K=0.8 to guarantee
                // G+C < 1 (coupled stability) across all Dkay settings.
                float safe_cpl_a = v_safe_cpl_a;
                float safe_cpl_b = v_safe_cpl_b;
                if (voice.pitch_env_amt > 0.0f && voice.pitch_env > silence_threshold) {
                    // Exponential (semitone-domain) sweep: more natural drum down-bend.
                    float sweep_st = voice.pitch_env_amt * voice.pitch_env;
                    float sweep_scale = fasterpowf(2.0f, -sweep_st * 0.08333333333f); // -st/12
                    voice.resA.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 1),
                                                                 voice.base_delay_A * m_pitch_bend_mult * sweep_scale));
                    voice.resB.delay_length = fmaxf(2.0f, fminf((float)(DELAY_BUFFER_SIZE - 1),
                                                                 voice.base_delay_B * m_pitch_bend_mult * sweep_scale));
                    voice.pitch_env *= voice.pitch_env_decay;
                }

                // Tube models (OpenTube=7, ClosedTube=8, phase_mult=-1) need noise fed
                // into the waveguide so breath continuously excites the tube resonance
                // (physically correct for flute/clarinet).  Percussion models do NOT get
                // noise in the waveguide — it would be pitch-filtered into a tonal ring.
                float tube_noise_A = (voice.resA.phase_mult < 0.0f)
                                     ? voice.exciter.noise_out_sample : 0.0f;
                float inputA = exciter_sig + tube_noise_A + (voice.resB_out_prev * safe_cpl_a);
                outA = process_waveguide(voice.resA, inputA);
                float outB = 0.0f;
                // --- ACTIVE PARTIAL COUNTING ---
                // Dynamically drop Resonator B to reclaim CPU cycles.
                // We only run the second delay line if:
                // 1. The user actually requested dual resonators (m_active_partials >= 16)
                // 2. AND (ResB is audible in the mix OR coupling is feeding it into ResA)
                // 3. OR ResB still has ringing energy left to decay (> -90 dB)

                bool resB_needed = (m_active_partials >= 16) &&
                                   (state.mix_ab > 0.001f ||
                                    m_coupling_depth > 0.001f ||
                                    fabsf(voice.resB_out_prev) > 0.00003f);

                if (resB_needed) {
                    float tube_noise_B = (voice.resB.phase_mult < 0.0f)
                                         ? voice.exciter.noise_out_sample : 0.0f;
                    float inputB = exciter_sig + tube_noise_B + (voice.resA_out_prev * safe_cpl_b);
                    outB = process_waveguide(voice.resB, inputB); //
                    voice.resA_out_prev = outA;
                    voice.resB_out_prev = outB;
                } else {
                    // ResB is bypassed. Keep its output at 0 to prevent coupling artifacts.
                    voice.resA_out_prev = outA;
                    voice.resB_out_prev = 0.0f;
                }
                float resonator_out = ((outA * (1.0f - state.mix_ab)) + (outB * state.mix_ab));
                // Structural mitigation for snare-family tonal ringing:
                // when wire mix is high, duck pitched resonator body so the broadband
                // wire/noise branch dominates like an actual snare drum.
                if (m_preset_idx == k_AcSnare || m_preset_idx == k_MarchSnare) {
                    float onset = 1.0f - fminf(1.0f, voice.exciter.wire_onset_env);
                    float wire_weight = fminf(1.0f, voice.exciter.snare_wire_mix + (0.35f * onset));
                    float ring_duck = 1.0f - (0.70f * wire_weight);
                    resonator_out *= ring_duck;
                }
                voice_out = resonator_out * voice.current_velocity;

                // Parallel noise path: noise bypasses the waveguide and mixes directly
                // into the voice output.  This preserves the broadband character that the
                // resonator would otherwise pitch-filter away (snare buzz, cymbal wash,
                // hi-hat hiss, shaker rattle).  The ×5 factor brings noise amplitude into
                // the same ballpark as the resonator output driven by the ×15 mallet.
                float parallel_noise_gain = 5.0f;
                // Structural mitigation: maintain an explicit high-band dominant path
                // for metallic/open-hat families so HF content survives KS loop losses.
                if (m_preset_idx == k_Cymbal || m_preset_idx == k_Gong ||
                    m_preset_idx == k_Triangle || m_preset_idx == k_HiHatOpen) {
                    parallel_noise_gain = 7.0f;
                }
                voice_out += voice.exciter.noise_out_sample * parallel_noise_gain * voice.current_velocity;
                // Structural high-band branch: simple high-pass (x - LP(x)) over
                // exciter noise, mixed post-resonator to reduce KS-loss coupling.
                if (voice.hf_branch_mix > 0.0f && voice.hf_branch_env > silence_threshold) {
                    voice.hf_branch_lp += 0.12f * (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    float hf = (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    voice_out += hf * voice.hf_branch_env * voice.hf_branch_mix * 8.0f * voice.current_velocity;
                    voice.hf_branch_env *= voice.hf_branch_decay;
                }
                if (voice.boom_mix > 0.0f && voice.boom_env > silence_threshold) {
                    if (m_preset_idx == k_KickDrum) {
                        // Step 4: kick FM-like sub sweep (about 90 -> 55 Hz, exponential via boom_env decay).
                        float sweep_hz = 55.0f + (35.0f * voice.boom_env);
                        voice.boom_inc = (2.0f * M_PI * sweep_hz) * inverse_default_sample_rate;
                    }
                    voice.boom_attack_env = fminf(1.0f, voice.boom_attack_env + voice.boom_attack_inc);
                    float boom = fastersinfullf(voice.boom_phase)
                               * voice.boom_env * voice.boom_mix * voice.boom_attack_env;
                    voice_out += boom * voice.current_velocity;
                    voice.boom_phase += voice.boom_inc;
                    if (voice.boom_phase > (2.0f * M_PI)) voice.boom_phase -= (2.0f * M_PI);
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
                    voice_out += (m1
                                + (stage2_modal_amp_ratio_2 * m2)
                                + (0.45f * m3)
                                + (0.28f * m4)
                                + (0.18f * m5)
                                + (0.12f * m6)) * modal_mix_dyn;
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
                    // Auto-decay squelch: deactivate voices whose master_env
                    // has naturally decayed to ENV_IDLE even while gate is held.
                    // This reclaims voice slots for percussion auto-decay.
                    if (!voice.is_releasing &&
                        voice.exciter.master_env.state == ENV_IDLE) {
                        voice.is_active = false;
                    }
                }
                // ── Stage 4a: Tilt EQ ──────────────────────────────────────
                voice.tone_lp = (voice_out * kToneLpMix) + (voice.tone_lp * (1.0f - kToneLpMix));
                if (tone_val < zeroThreshold) {
                     voice_out = voice_out + (voice.tone_lp - voice_out) * (-tone_val * kInvToneCutDivisor);
                 } else if (tone_val > zeroThreshold) {
                     float hp = voice_out - voice.tone_lp;
                     voice_out += hp * (tone_val * kInvToneBoostDivisor);
                 }
                if (voice.onset_inc > 0.0f && voice.onset_env < 1.0f) {
                    voice.onset_env = fminf(1.0f, voice.onset_env + voice.onset_inc);
                    voice_out *= voice.onset_env;
                }

                main_out[i * 2]     += voice_out * state.master_gain;
                main_out[i * 2 + 1] += voice_out * state.master_gain;

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

        // ── Stage 1-3: hard-clip output ────────────────────────────────────
        // Stage 4 uses soft-clip + overdrive.  For debug stages the raw mallet
        // impulse (~3-4 × full-scale) must be clamped or the Drumlogue DAC
        // saturates on the first note and may engage hardware protection.
        for (size_t i = 0; i < frames; ++i) {
            main_out[i * 2]     = fmaxf(-0.99f, fminf(0.99f, main_out[i * 2]));
            main_out[i * 2 + 1] = fmaxf(-0.99f, fminf(0.99f, main_out[i * 2 + 1]));
        }
        // ── Stage 4b: Master FX (filter + overdrive + brickwall) ──────────
        for (size_t i = 0; i < frames; ++i) {
            float mix_l = main_out[i * 2];
            float mix_r = main_out[i * 2 + 1];
            mix_l = state.master_filter.process(mix_l);
            mix_r = mix_l;
            mix_l *= state.master_drive;
            mix_r *= state.master_drive;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            float32x2_t v = {mix_l, mix_r};
            float32x2_t abs_v = vabs_f32(v);
            float32x2_t den = vadd_f32(abs_v, vdup_n_f32(1.0f));
            // Reciprocal estimate + one NR refinement (ARMv7-friendly).
            float32x2_t rec = vrecpe_f32(den);
            rec = vmul_f32(vrecps_f32(den, rec), rec);
            float32x2_t clipped = vmul_f32(v, rec);
            float clipped_l = vget_lane_f32(clipped, 0);
            float clipped_r = vget_lane_f32(clipped, 1);
#else
            float clipped_l = mix_l / (1.0f + fabsf(mix_l));
            float clipped_r = mix_r / (1.0f + fabsf(mix_r));
#endif
            main_out[i * 2]     = fmaxf(-0.99f, fminf(0.99f, clipped_l));
            main_out[i * 2 + 1] = fmaxf(-0.99f, fminf(0.99f, clipped_r));
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
    float m_master_cutoff = 10000.0f; // Default open filter

    // Functions from unit runtime (nullptr until Init() assigns them from the OS descriptor)
    unit_runtime_get_num_sample_banks_ptr m_get_num_sample_banks_ptr = nullptr;
    unit_runtime_get_num_samples_for_bank_ptr m_get_num_samples_for_bank_ptr = nullptr;
    unit_runtime_get_sample_ptr m_get_sample = nullptr;

    uint8_t m_ui_note = 60;
    uint8_t m_sample_bank = 0;
    uint8_t m_sample_number = 0;
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

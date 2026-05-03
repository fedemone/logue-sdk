#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__arm__) || defined(__aarch64__)
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

#ifdef ENABLE_PHASE_7_MODELS
FastTables g_tables;
#endif

// ==============================================================================
// CONSTANTS
// ==============================================================================
static constexpr float default_sample_rate = 48000.0f;
static constexpr uint16_t pitch_centre = 8192;
static constexpr float kToneLpMix = 0.3f;
static constexpr float kToneCutDivisor = 10.0f;
static constexpr float kToneBoostDivisor = 15.0f;
static constexpr float kInvToneCutDivisor = 0.1f;         // 1 / 10
static constexpr float kInvToneBoostDivisor = 0.06666667f; // 1 / 15
static constexpr float zeroThreshold = 0.0f;
static constexpr float alpha = 0.01f;
static constexpr float limiter = 0.99f;
static constexpr int kSquelchGuardSamples = 1000; // ~20 ms
static constexpr float kSquelchThreshold = 0.0001f; // -80 dB
static constexpr float k_log_2_of_200 = 7.643856f;
static constexpr float k_log_0001 = -6.907755279f; // logf(0.001f) — T60→decay coefficient
static constexpr float stage2_modal_amp_ratio_2 = 0.6f;
static constexpr float silence_threshold = 1e-5f;

#if ENABLE_STAGE2_MODAL_PILOT
// Stage-2 pilot defaults (override-able at compile time for quick sweeps).
#ifndef STAGE2_MODAL_RATIO_2
#define STAGE2_MODAL_RATIO_2 2.80f
#endif
#ifndef STAGE2_MODAL_ENV1
#define STAGE2_MODAL_ENV1 0.9f
#endif
#ifndef STAGE2_MODAL_ENV2
#define STAGE2_MODAL_ENV2 0.7f
#endif
#ifndef STAGE2_MODAL_T60_1_MS
#define STAGE2_MODAL_T60_1_MS 70.0f
#endif
#ifndef STAGE2_MODAL_T60_2_MS
#define STAGE2_MODAL_T60_2_MS 110.0f
#endif
#ifndef STAGE2_MODAL_DECAY1
#define STAGE2_MODAL_DECAY1 0.99905f
#endif
#ifndef STAGE2_MODAL_DECAY2
#define STAGE2_MODAL_DECAY2 0.99810f
#endif
#ifndef STAGE2_MODAL_MIX
#define STAGE2_MODAL_MIX 0.08f
#endif
#endif

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
        k_Marimba,          // 1 -sample: marimba-hit-c4_C_minor.wav (524Hz +/- 50Hz)
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

#ifdef ENABLE_PHASE_7_MODELS
        g_tables.generate(default_sample_rate); // Pre-calculate all tuning math
#endif

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
        // [UT1: MEMSET FIX] - Never memset C++ structs with default initializers!
        // memset(&state, 0, sizeof(SynthState)); <-- DELETED
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].is_active = false;
            state.voices[i].exciter.current_frame = 0;
            state.voices[i].resA.write_ptr = 0;
            state.voices[i].resB.write_ptr = 0;

            // Explicitly clear delay buffers to prevent NaN garbage
            for(int j = 0; j < DELAY_BUFFER_SIZE; ++j) {
                state.voices[i].resA.buffer[j] = 0.0f;
                state.voices[i].resB.buffer[j] = 0.0f;
            }

            // Clear filter state memory so a Reset() mid-play never causes a click
            // on the next NoteOn (stale z1/ap states corrupt the first output samples).
            state.voices[i].resA.z1    = 0.0f;
            state.voices[i].resA.ap_x1 = 0.0f;
            state.voices[i].resA.ap_y1 = 0.0f;
            state.voices[i].resB.z1    = 0.0f;
            state.voices[i].resB.ap_x1 = 0.0f;
            state.voices[i].resB.ap_y1 = 0.0f;

            // Re-apply safe defaults
            state.voices[i].resA.lowpass_coeff = 1.0f;
            state.voices[i].resB.lowpass_coeff = 1.0f;

            // Clear coupling, tone and energy-squelch memory
            state.voices[i].resA_out_prev = 0.0f;
            state.voices[i].resB_out_prev = 0.0f;
            state.voices[i].tone_lp = 0.0f;
            state.voices[i].mag_env = 0.0f;
            state.voices[i].base_delay_A = 0.0f;
            state.voices[i].base_delay_B = 0.0f;
            state.voices[i].transient_frames_left = 0;
            state.voices[i].transient_frames_total = 0;
            state.voices[i].transient_inv_total = 0.0f;
            state.voices[i].transient_lp_jitter = 0.0f;
            state.voices[i].transient_ap_jitter = 0.0f;
            state.voices[i].transient_lp_base_a = 1.0f;
            state.voices[i].transient_lp_base_b = 1.0f;
            state.voices[i].transient_ap_base_a = 0.0f;
            state.voices[i].transient_ap_base_b = 0.0f;
#if ENABLE_STAGE2_MODAL_PILOT
            state.voices[i].modal_pilot_enabled = false;
            state.voices[i].modal_k_1 = 0.0f;
            state.voices[i].modal_k_2 = 0.0f;
            state.voices[i].modal_k_3 = 0.0f;
            state.voices[i].modal_k_4 = 0.0f;
             state.voices[i].modal_k_5 = 0.0f;
             state.voices[i].modal_k_6 = 0.0f;
            state.voices[i].modal_y1_1 = 0.0f;
            state.voices[i].modal_y1_2 = 0.0f;
            state.voices[i].modal_y1_3 = 0.0f;
            state.voices[i].modal_y1_4 = 0.0f;
            state.voices[i].modal_y2_1 = 0.0f;
            state.voices[i].modal_y2_2 = 0.0f;
            state.voices[i].modal_y2_3 = 0.0f;
            state.voices[i].modal_y2_4 = 0.0f;
             state.voices[i].modal_y1_5 = 0.0f;
             state.voices[i].modal_y1_6 = 0.0f;
             state.voices[i].modal_y2_5 = 0.0f;
             state.voices[i].modal_y2_6 = 0.0f;
            state.voices[i].modal_norm_count = 0;
            state.voices[i].modal_env_1 = 0.0f;
            state.voices[i].modal_env_2 = 0.0f;
            state.voices[i].modal_env_3 = 0.0f;
            state.voices[i].modal_env_4 = 0.0f;
             state.voices[i].modal_env_5 = 0.0f;
             state.voices[i].modal_env_6 = 0.0f;
            state.voices[i].modal_decay_1 = 0.9990f;
            state.voices[i].modal_decay_2 = 0.9985f;
            state.voices[i].modal_decay_3 = 0.9980f;
            state.voices[i].modal_decay_4 = 0.9975f;
             state.voices[i].modal_decay_5 = 0.9970f;
             state.voices[i].modal_decay_6 = 0.9965f;
            state.voices[i].modal_mix = 0.0f;
            state.voices[i].modal_mode_count = 0;
            state.voices[i].pitch_env = 0.0f;
            state.voices[i].pitch_env_decay = 1.0f;
            state.voices[i].pitch_env_amt = 0.0f;
            state.voices[i].reed_nl_enabled = false;
            state.voices[i].reed_nl_drive = 1.0f;
#endif
            state.voices[i].exciter.noise_lp_state = 0.0f;
            state.voices[i].exciter.noise_band_mix = 0.5f;
             state.voices[i].exciter.noise_hi_lp_state = 0.0f;
             state.voices[i].exciter.noise_hi_lp_coeff = 0.30f;
            state.voices[i].exciter.snare_wire_z1 = 0.0f;
            state.voices[i].exciter.snare_wire_z2 = 0.0f;
            state.voices[i].exciter.snare_wire_mix = 0.0f;

#ifdef ENABLE_PHASE_6_FILTERS
            // Noise filter defaults to LP mode, fully open (12 kHz)
            state.voices[i].exciter.noise_filter.mode = 0;
            state.voices[i].exciter.noise_filter.set_coeffs(12000.0f, 0.707f, default_sample_rate);
            // Clear SVF delay states — set_coeffs() only updates f/q, not the
            // recursive lp/bp/hp accumulators.  Leaving them non-zero after a
            // patch change would cause a loud click on the next NoteOn.
            state.voices[i].exciter.noise_filter.lp = 0.0f;
            state.voices[i].exciter.noise_filter.bp = 0.0f;
            state.voices[i].exciter.noise_filter.hp = 0.0f;
#endif
        }

        state.master_gain = 1.0f;
        state.master_drive = 1.0f;
        state.mix_ab = 0.5f; // Equal A/B mix
        state.tone = 0.0f;   // Neutral tilt EQ (LoadPreset restores the preset value)
        m_pitch_bend_mult = 1.0f; // Clear any held bend so the next note plays in tune.

        // Always return to ResA edit context so LoadPreset (called next in Init)
        // applies preset data symmetrically to both resonators.
        m_is_resonator_a = true;
        m_is_resonator_b = true;

#ifdef ENABLE_PHASE_6_FILTERS
        // [UT1: MEMSET FIX] - Force the filter back to safe Highpass mode
        state.master_filter.mode = 2;
        state.master_filter.set_coeffs(10.0f, 0.707f, default_sample_rate);
#endif
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
            {   0,  60,   0,   0, 500, 470,   0,   0,   0,   0,  35,  10,   0,   0,  10,   0,   1,   3,   0,   0, 300,   0,1200, 707}, // 0:  InitDbg    — pure KS string, no coupling
            {   1,  72,   0,   1, 800, 130,   0,   0,   0,   6, 194,  -7,   0,   0,   5,  15,   1,   7,  20,   0, 300,   0,1200, 707}, // 1:  Marimba    — sample: C5/1.0s→Dkay184; B=0.0075→InHm15; centroid→Mterl-9; Note60→72
            {   2,  36,   0,   0, 150,   0,   0,   0,   0,   3, 180,  -6,  -5,   0,  15,   0,   1,   3,   0,   0, 300,   0,1200, 707}, // 2:  808 Sub    — final Stage-1: Dkay170/Mterl-6 to counter LP-loss-shortened tail without adding noise
            {   3,  38,   0,   1, 520, 280,   0,   0,   2,   5,  78,  -3,   0,  46,   9,   2,   0,   2,   7,  58, 640,   2, 180, 707}, // 3:  Ac Snare   — Dkay35: shorter KS body shifts timbre to snappier snare vs metallic gong; NzMx35 raises noise floor. rescue pass: stronger shell body + wire crack, darker body with wider snare burst
            {   4,  72,   0,   1, 900, 340,   0,   0,   0,   1, 200,  30,   0,   0,  20,   5,  20,  18,   0,   5, 300,   0,1500, 707}, // 4:  TblrBel    — c=0.98@524Hz (Mterl28+TubRad20); MlltStif100 (medium felt mallet, less overtone energy → measured T60 tracks fundamental ~7.5s)
            {   5,  40,   0,   1, 360, 300,   0,   0,   2,   3, 200,  -2,   0,  36,  18,  10,   0,  -4,   4,  16, 420,   0, 380, 707}, // 5:  Timpani    — InHm9: slight spread on coupled membrane mode; NzRs300 longer noise tail. rescue pass: deeper boom (darker loss) with longer low-body sustain and broader impact
            {   6,  48,   0,   1, 600, 350,   0,   0,   1,   5, 102,   0,   0,  35,  12,  10,   5,  15,   5,   7, 450,   0, 500, 707}, // 6:  Djambe     — Dkay102/Mterl0: drier djembe body with wider noise cutoff
            {   7,  41,   0,   1, 250, 390,   0,   0,   1,   5, 180,   4,   0,  30,  15,   3,   1,   7,   5,  14, 550,   0, 250, 707}, // 7:  Taiko      — harder mallet + reduced noise tail NzMx14 + lower NzFq for thud character
            {   8,  65,   0,   1, 720, 500,   0,   0,   1,   5, 130,  12,   0,  50,   8,  15,  25,  19,   5,  20, 650,   2, 400, 707}, // 8:  MrchSnr    — Mterl12: less bright shell; NzMx20/NzFq400 for snappier wire character
            {   9,  60,   0,   1, 600, 335,   0,   0,   0,   0, 185,  12,   0,   0,  12,   3,   1,   7,   0,   0, 300,   0,1000, 707}, // 09: Koto       — InHm3 adds light inharmonic shimmer; no noise for cleaner pluck
            {  10,  72,   0,   1, 500, 300,   0,   0,   0,   1, 200,   2,   0,   0,  18,   1,   1,   4,   0,   0, 300,   0,1000, 707}, // 10: Vibrph     — final Stage-1: max Dkay + brighter loss profile (Mterl2/TubRad10) to offset LP-loss under-decay
            {  11,  48,   0,   1, 900, 500,   0,   0,   0,   2, 156,  24,   0,   0,   2,  10,   1,   3,   0,   5, 420,   0, 900, 707}, // 11: Wodblk     — NzMx5 light transient click; NzRs420 short burst
            {  12,  45,   0,   1, 450, 300,   0,   0,   2,   5, 172,  -2,   0,  44,  11,   1,   0,   5,   5,   8, 360,   0, 520, 707}, // 12: Ac Tom     — softer mallet + InHm0 for cleaner membrane fundamental. rescue pass: fuller low body + clearer stick transient with reduced string-like ring
            {  13,  60,   0,   1, 800, 450,   0,   0,   0,   4, 182,  16,   0,   0,  18,   7,   5,   9,   5,   8, 600,   2, 400, 707}, // 13: Cymbal     — Dkay182/Mterl16/InHm7: balanced metallic with 6-mode bank + diffuser + noise bed
            {  14,  50,   0,   1, 200,  80,   0,   0,   0,   4, 190,  -4,   0,   0,  20,   7,   1,  17,  20,  10, 800,   0,  30, 707}, // 14: Gong       — softer attack MlSt80 + less dark Mterl-4 + more noise onset NzMx10
            {  15,  65,   0,   1, 700, 461,   0,   0,   0,   1, 190,  10,   0,   0,   5,   6,   1,   5,   3,   0, 300,   0,1000, 707}, // 15: Kalimba    — Mterl10 warmer bar + InHm6 natural tine spread + TbRd7
            {  16,  60,   0,   1, 600,   0,   0,   0,   0,   4, 200,  14,   0,   0,  12,   0,   3,   9,   5,   0, 300,   0,1000, 707}, // 16: StelPan    — Mterl14 brighter pan + TbRd11 for steelpan inharmonic partial spread
            {  17,  79,   0,   1, 900, 450,   0,   0,   0,   2,  13,  -1,   0,   0,   1,   2,   1,   1,   0,   0, 300,   0, 800, 707}, // 17: Claves     — final Stage-1: InHm3 to reduce audible inharmonic beating while keeping wood attack
            {  18,  67,   0,   1, 800, 420,   0,   0,   0,   4, 185,  20,   0,   0,   4, 200,  20,   3,  30,   0, 300,   0,1000, 707}, // 18: Cowbell    — Dkay:55→175 (~2s metallic ring); InHm:1700→200 (moderate plate inharmonicity)
            {  19,  84,   0,   1, 900, 440,   0,   0,   0,   1, 190,   2,   0,   0,  15,  58,  80,  15,   0,   0, 300,   0,1500, 707}, // 19: Triangle   — harder mallet MlSt440 for cleaner metallic ping onset
            {  20,  36,   0,   1, 380, 120,   0,   0,   2,   5, 188,  -5,   0,  38,   6,   3,   0,   1,   6,  10, 180,   0, 220, 707}, // 20: Kick Drum  — Dkay165/Mterl3: longer punch tail; softer MlSt110; NzMx8 subtle click layer. rescue pass: added thump/body with darker sustain and reduced high-click dominance
            {  21,  60,   0,   1, 500, 270,   0,   0,   2,   5,  15,   5,   0,  50,   3,   0,  10,   3,   5,  95, 600,   2, 600, 707}, // 21: Clap       — NzMx95: maximum noise content for hand-clap character
            {  22,  72,   0,   1, 100, 370,   0,   0,   2,   5,  12,  10,   0,  50,   2,   0,  20,   3,   3,  90, 900,   2, 800, 707}, // 22: Shaker     — Dkay12/Mterl10: dry rattle body; NzMx90 high noise content
            {  23,  72,   0,   1, 100, 132,   0,   0,   0,   7, 200,  -3,   0,   0,  12,   0,   1,   3,   0,  15, 950,   0, 400, 707}, // 23: Flute      — InHm0 pure tube; NzMx15 breath noise; NzFq400 4kHz breath shimmer
            {  24,  72,   0,   0,  50,  50,   0,   0,   0,   8, 185,  -4,   0,   0,  12,  10,   1,   7,   0,   8, 850,   0, 600, 707}, // 24: Clarinet   — Dkay185/Mterl-4: more even tube body; MlSt50 reed articulation; NzMx8 breath
            {  25,  36,   0,   1, 600, 250,   0,   0,   0,   0, 105,  -6,   0,   0,  10,   0,   1,   3,  40,   0, 300,   0, 500, 707}, // 25: PlkBass    — final Stage-1: less drive + harder mallet / slightly longer decay for cleaner pluck body
            {  26,  76,   0,   1, 700,  50,   0,   0,   0,   4, 200,  30,   0,   0,  18,  10,  10,  18,   0,   0, 300,   0,1200, 707}, // 26: GlsBwl     — InHm10 glass bowl partials; no noise for pure bowl character
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
            {  28,  79,   0,   1, 900, 500,   0,   0,   0,   4, 110,  26,   0,   0,   2,  16,   5,   3,   0,  48, 760,   2, 900, 707},  // 28: HHat-C  — softer mallet MlSt420 + InHm14 metallic partial spread + NzMx35/NzFq600. rescue pass: tighter short chick with brighter/sparser metallic top
            {  29,  79,   0,   1, 900, 520,   0,   0,   0,   4, 198,  26,   0,   0,  14,  11,   5,  20,   0,  60,1100,   2,1100, 707},  // 29: HHat-O  — Mterl27/TbRd21: balanced open hat ring; NzMx45/NzFq600 sizzle. rescue pass: longer shimmering wash with clearer high-band sizzle
            {  30,  62,   0,   1, 600, 365,   0,   0,   1,   5, 158,   3,   0,   0,  10,  10,   2,   7,   0,  15, 520,   0, 650, 707},  // 30: Conga   — softer MlSt365 + TbRd9 + NzMx15/NzFq650 for tighter conga snap
            {  31,  62,   0,   1, 700, 300,   0,   0,   0,   4, 190,   7,   0,   0,  20,   1,   5,  15,   0,   5, 300,   0,1000, 707},  // 31: Handpn  — MlSt300 softer strike; Mterl7 warmer plate; InHm1 near-harmonic handpan
            {  32,  84,   0,   1, 900, 420,   0,   0,   0,   1, 200,  20,   0,   0,   8,  10,  10,   3,   0,   0, 300,   0,1200, 707},  // 32: BelTre  — Beam, T60=1.0s@C6→Dkay193; Mterl20 very bright; InHm10 metallic partial spread
            {  33,  60,   0,   1, 700, 270,   0,   0,   0,   6, 177,   8,   0,   0,  10,   6,   2,   3,   0,   0, 300,   0, 800, 707},  // 33: SltDrm  — MarBar, T60=1.0s@C4→Dkay167; Mterl8 mid-bright wood; InHm6 (B≈0.003)
            {  34,  57,   0,   1, 900, 500,   0,   0,   0,   4, 190,  30,   0,   0,  18,   6,   5,  17,   0,  15, 700,   2, 600, 707},  // 34: Ride    — InHm6 plate spread; NzMx15/NzRs700 sizzle-only noise character
            {  35,  60,   0,   1, 900, 461,   0,   0,   0,   4, 194,  18,   0,   0,   8,  10,   5,   1,   0,  20, 600,   2, 700, 707},  // 35: RidBel  — InHm10 bell spread; TbRd3; NzFq700 higher sizzle
            {  36,  57,   0,   1, 650, 410,   0,   0,   0,   5, 152,  -2,   0,   0,   8,   6,   2,  -1,   0,   0, 520,   0, 450, 707},  // 36: Bongo   — harder mallet MlSt410 for sharper bongo slap; InHm6 tonal cue
            {  37,  88,   0,   1, 100, 450,   0,   0,   0,   7, 195,   5,   0,   0,   5,   0,   2,  13,   0,  45, 150,   0, 450, 707},  // 37: GlsBotl — MlSt450 harder blow onset; Mterl5 brighter; NzMx45/NzRs150 short puff
            {  38,  49,   0,   1, 900, 500,   0,   0,   0,   4, 140,  11,   0,   0,   3,  16,   5,   3,   0,  24, 150,   2, 400, 707}   // 38: Tick    — InHm16 tight wood spread; NzMx24/NzRs150 crisp tick transient
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
            if (param_id == k_paramBank) continue;
            if (param_id == k_paramSample) continue;

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
                    // resonators can be coupled or indipendent
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
#ifdef ENABLE_PHASE_7_MODELS
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
#endif
                break;
            }

            case k_paramDkay: {
                // 0.85 = instant dead thud. 0.999 = rings for ~5 seconds.
                // Stored ÷10 (0-200 represents 0-2000). Divide by 200 (new max).
                if (value <= 200) {
                    float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.005f));
                    float g = 0.85f + (norm * 0.149f);
                    // master_env gate: exponential 50ms (Decay=0) → 10s (Decay=200).
                    // Decay is the primary sustain control; Rel only gates the noise
                    // burst.  Without this, the master_env would kill the waveguide
                    // resonance at ~28 ms (default Rel) regardless of Decay setting.
                    float t_s = 0.05f * fasterpow2f(k_log_2_of_200 * norm); // 50ms..10s - was fasterpowf(200.0f, norm)
                    float master_rate = M_THREELN10 / (t_s * default_sample_rate);  // was 3.0f * M_LN10
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        if (m_is_resonator_a)
                            state.voices[i].resA.feedback_gain = g;
                        if (m_is_resonator_b)
                            state.voices[i].resB.feedback_gain = g;
#ifdef ENABLE_PHASE_5_EXCITERS
                        // Always update regardless of which resonator is selected —
                        // master_env is voice-level, not per-resonator.
                        state.voices[i].exciter.master_env.release_rate = master_rate;
                        // Auto-decay rate: 30% of release rate.  Ensures sounds
                        // decay naturally even while the gate is held (percussion
                        // on a drum machine should never sustain indefinitely).
                        // NoteOff switches to the faster release_rate for a clean tail.
                        state.voices[i].exciter.master_env.decay_rate = master_rate * 0.3f;
#endif
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
                for (int i = 0; i < NUM_VOICES; ++i) {
                    if (m_is_resonator_a) {
                        state.voices[i].resA.lowpass_coeff = coeff;
                        state.voices[i].transient_lp_base_a = coeff;
                    }
                    if (m_is_resonator_b) {
                        state.voices[i].resB.lowpass_coeff = coeff;
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
#ifdef ENABLE_PHASE_5_EXCITERS
                    state.voices[i].exciter.noise_env.release_rate = rel_rate;
                    // High band should decay faster than low-band body.
                    state.voices[i].exciter.noise_env_hi.release_rate = fminf(0.99f, rel_rate * 2.5f);
#endif
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
#ifdef ENABLE_PHASE_6_FILTERS
                // Stored 1-1999; effective range 10-19990 Hz (×10 scaling).
                m_master_cutoff = (float)value * 10.0f;
                // Divide by 1000: UI stores 707-4000, filter needs 0.707-4.0
                float res_val = fmaxf(0.707f, (float)m_params[k_paramResnc] * 0.001f);
                state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
#endif
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
#ifdef ENABLE_PHASE_5_EXCITERS
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.01f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_decay_coeff = norm;
                }
#endif
                break;
            }

            case k_paramNzRes: {
#ifdef ENABLE_PHASE_5_EXCITERS
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
#endif
                break;
            }
            case k_paramResnc: {
#ifdef ENABLE_PHASE_6_FILTERS
                // UI passes 707 to 4000. Divide by 1000 to get a Q factor of 0.707 to 4.0
                float res_val = fmaxf(0.707f, (float)value * 0.001f);
                state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
#endif
                break;
            }

            case k_paramNzFltr: {
#ifdef ENABLE_PHASE_6_FILTERS
                int mode = (int)fmaxf(0.0f, fminf(2.0f, (float)value));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_filter.mode = mode;
                }
#endif
                break;
            }

            case k_paramNzFltFrq: {
#ifdef ENABLE_PHASE_6_FILTERS
                // Stored ÷10 (2-2000 represents 20-20000 Hz). Multiply by 10 for real Hz.
                float freq = fmaxf(20.0f, fminf(20000.0f, (float)value * 10.0f));
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_filter.set_coeffs(freq, 0.707f, default_sample_rate);
                     // Private split-band high cutoff (no extra UI): tied to NzFq,
                     // shifted upward for sizzle branch and converted to 1-pole coeff.
                     float hi_hz = fminf(20000.0f, fmaxf(300.0f, freq * 2.2f));
                     float alpha = fminf(0.95f, fmaxf(0.02f, (2.0f * M_PI * hi_hz) / default_sample_rate));
                     state.voices[i].exciter.noise_hi_lp_coeff = alpha;
                }
#endif
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
        static const char* const nz_filter_names[] = {"LP", "BP", "HP"};

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
            // Stored ÷10; show real ×10 Hz/kHz value (20-20000 Hz)
            static char nf_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                snprintf(nf_buf, sizeof(nf_buf), "%d.%dkHz", hz / 1000, (hz % 1000) / 100);
            } else {
                snprintf(nf_buf, sizeof(nf_buf), "%dHz", hz);
            }
            return nf_buf;
        } else if (index == k_paramLowCut) {
            // value is 1-1999; effective Hz is value×10 (10-19990 Hz).
            static char lc_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                // Show as kHz with one decimal place: 1000→"1.0kHz", 15000→"15.0kHz"
                snprintf(lc_buf, sizeof(lc_buf), "%d.%dkHz", hz / 1000, (hz % 1000) / 100);
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
#ifdef ENABLE_PHASE_8_2D_DRUMHEAD
        // --- 2D DRUMHEAD STRIKE PHYSICS ---
        // 1. Calculate the physical strike location once for the entire voice
        float hit_x = (float)m_params[k_paramHitPos] * 0.01f;
        float hit_y = (1.0f - v.current_velocity) * hit_x * 0.5f;

        // Use our fast-math approximation to find distance from center (0.0 to 1.0)
        float radius = sqrtsum2acc(hit_x, hit_y); //
        radius = fminf(1.0f, radius);
#endif

        // --- VELOCITY MODULATION ---
        // VlMllStf: harder hit → stiffer (brighter) mallet.
        // Override the global mallet_stiffness on this specific voice only,
        // so soft hits are round and hard hits are sharp without changing other voices.
        {
            float base_stiff = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.002f));
            float stif_mod   = (float)m_params[k_paramVlMllStf] * 0.01f; // -1.0 to +1.0
#ifdef ENABLE_PHASE_8_2D_DRUMHEAD
            // Add up to a 50% stiffness boost when striking at the absolute edge
            float rim_stiffness_boost = radius * 0.5f;
#endif
            v.exciter.mallet_stiffness = fmaxf(0.01f, fminf(1.0f,
                base_stiff + stif_mod * v.current_velocity));
        }

        // VlMllRes: harder hit → faster noise attack (sharper transient).
        // Override the noise_env attack_rate on this voice so it responds to accents.
        {
            float base_nz     = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramNzRes] * 0.001f));
            float base_attack = 0.9f - (base_nz * 0.8f);
            float res_mod     = (float)m_params[k_paramVlMllRes] * 0.01f; // -1.0 to +1.0
#ifdef ENABLE_PHASE_8_2D_DRUMHEAD
        // Add up to a 10% speed boost to the attack rate for extreme rim hits
            float rim_snap_boost = radius * 0.1f;

            v.exciter.noise_env.attack_rate = fmaxf(0.01f, fminf(0.99f,
                base_attack + (res_mod * v.current_velocity * 0.5f) + rim_snap_boost)); //
#else
            v.exciter.noise_env.attack_rate = fmaxf(0.01f, fminf(0.99f,
                base_attack + res_mod * v.current_velocity * 0.5f));
#endif
            // High-band burst should stay snappier than low-band burst.
            v.exciter.noise_env_hi.attack_rate = fmaxf(0.05f, fminf(0.99f,
                v.exciter.noise_env.attack_rate * 1.25f));
        }

        // --- THE PHYSICS OF PITCH ---
#ifdef ENABLE_PHASE_7_MODELS
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
#ifndef ENABLE_PHASE_8_2D_DRUMHEAD
            // Membrane / Drumhead Logic:
            // A circular membrane's overtone ratios are determined by the zeros of the
            // Bessel function J_mn.  The dominant second mode (1,1) has ratio ≈ 1.5926
            // relative to the fundamental, so ResB should be at 1/1.5926 ≈ 0.628× the
            // fundamental delay.  The old value of 0.68 (ratio 1.47) was not a Bessel
            // zero and produced an off-character "wrong" shimmer.
            v.resB.delay_length = base_delay * 0.628f;
#else
            // --- 2D DRUMHEAD STRIKE PHYSICS ---
            // 4. Interpolate between Bessel modes based on strike radius
            // Center (r=0): Mode (1,1) -> ratio ~ 0.628
            // Edge   (r=1): Mode (2,1) -> ratio ~ 0.466
            const float mode_1_1 = 0.628f;
            const float mode_2_1 = 0.466f;
            float dynamic_ratio = mode_1_1 + radius * (mode_2_1 - mode_1_1);

            v.resB.delay_length = base_delay * dynamic_ratio;
#endif
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
#else
        // Legacy fallback calculation
        // 1. Convert MIDI Note to Frequency (Hz)
        float freq = 440.0f * fasterpowf(2.0f, ((float)note - 69.0f)* 0.08333333333f); // approx 1 / 12
        // [UT2: DELAY BOUNDS FIX]
        if (freq < 12.0f) freq = 12.0f;
        // 2. Convert Frequency to Delay Line Length (Samples)
        // Drumlogue sample rate is strictly 48000.0f
        float delay_len = default_sample_rate / freq;
        // 3. Assign to resonators (Apply fine-tuning offsets here later)
        v.resA.delay_length = delay_len;
        v.resB.delay_length = delay_len;
#endif
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
            float ca = v.resA.ap_coeff;                       // AP coefficient
            float lp_del_A = pa / (1.0f - pa);                // τ_LP: pa/(1-pa)
            float ap_del_A = (1.0f - ca) / (1.0f + ca);      // τ_AP: (1-c)/(1+c) ≤ 1
            v.resA.delay_length = fmaxf(2.0f, v.resA.delay_length - lp_del_A - ap_del_A);

            // ResB
            float pb = 1.0f - v.resB.lowpass_coeff;
            float cb = v.resB.ap_coeff;
            float lp_del_B = pb / (1.0f - pb);
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
        v.mag_env = 0.0f;

        // Reset phase
        v.exciter.current_frame = 0;
        v.exciter.mallet_lp  = 0.0f;
        v.exciter.mallet_lp2 = 0.0f;
        v.resA.ap_x1 = 0.0f;
        v.resA.ap_y1 = 0.0f;
        v.resB.ap_x1 = 0.0f;
        v.resB.ap_y1 = 0.0f;
        v.resA_out_prev = 0.0f;
        v.resB_out_prev = 0.0f;
        v.tone_lp = 0.0f;
        v.transient_frames_left = 0;
        v.transient_frames_total = 0;
        v.transient_inv_total = 0.0f;
        v.transient_lp_jitter = 0.0f;
        v.transient_ap_jitter = 0.0f;
        v.transient_lp_base_a = v.resA.lowpass_coeff;
        v.transient_lp_base_b = v.resB.lowpass_coeff;
        v.transient_ap_base_a = v.resA.ap_coeff;
        v.transient_ap_base_b = v.resB.ap_coeff;
#if ENABLE_STAGE2_MODAL_PILOT
        v.modal_pilot_enabled = false;
        v.modal_k_1 = 0.0f;
        v.modal_k_2 = 0.0f;
        v.modal_k_3 = 0.0f;
        v.modal_k_4 = 0.0f;
         v.modal_k_5 = 0.0f;
         v.modal_k_6 = 0.0f;
        v.modal_y1_1 = 0.0f;
        v.modal_y1_2 = 0.0f;
        v.modal_y1_3 = 0.0f;
        v.modal_y1_4 = 0.0f;
        v.modal_y2_1 = 0.0f;
        v.modal_y2_2 = 0.0f;
        v.modal_y2_3 = 0.0f;
        v.modal_y2_4 = 0.0f;
         v.modal_y1_5 = 0.0f;
         v.modal_y1_6 = 0.0f;
         v.modal_y2_5 = 0.0f;
         v.modal_y2_6 = 0.0f;
        v.modal_norm_count = 0;
        v.modal_env_1 = 0.0f;
        v.modal_env_2 = 0.0f;
        v.modal_env_3 = 0.0f;
        v.modal_env_4 = 0.0f;
         v.modal_env_5 = 0.0f;
         v.modal_env_6 = 0.0f;
        v.modal_decay_1 = 0.9990f;
        v.modal_decay_2 = 0.9985f;
        v.modal_decay_3 = 0.9980f;
        v.modal_decay_4 = 0.9975f;
         v.modal_decay_5 = 0.9970f;
         v.modal_decay_6 = 0.9965f;
        v.modal_mix = 0.0f;
        v.modal_mode_count = 0;
        v.pitch_env = 0.0f;
        v.pitch_env_decay = 1.0f;
        v.pitch_env_amt = 0.0f;
        v.reed_nl_enabled = false;
        v.reed_nl_drive = 1.0f;
#endif
        v.exciter.noise_lp_state = 0.0f;
         v.exciter.noise_band_mix = 0.5f;
         v.exciter.noise_hi_lp_state = 0.0f;
        v.exciter.snare_wire_z1 = 0.0f;
        v.exciter.snare_wire_z2 = 0.0f;
        v.exciter.snare_wire_mix = 0.0f;

        // Clear waveguide delay line, LP state, and write pointer.
        //
        // After write_ptr is reset to 0, the read position starts at
        // (0 - delay_length) mod DELAY_BUFFER_SIZE ≈ (4096 - delay_length).
        // The read pointer advances with the write pointer.  At sample delay_length,
        // the read pointer reaches position 0, which was just written by this note —
        // from that point forward every read is from freshly-computed data.
        // Only the tail window [4096 - ceil(delay_length) - 1 … 4095] is ever read
        // before new data covers it; clearing that window (typically 110-880 floats)
        // is correct and 10-37× cheaper than zeroing the full 16 KB buffer.
        //
        // Skip entirely on a fresh (never-triggered) slot: Reset() already zeroed it.
        v.resA.write_ptr = 0;
        v.resB.write_ptr = 0;
        v.resA.z1 = 0.0f;
        v.resB.z1 = 0.0f;
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

#ifdef ENABLE_PHASE_6_FILTERS
        // Clear noise SVF delay states so rapid re-triggering doesn't produce
        // a click from residual filter memory.  set_coeffs() (called once from
        // setParameter) only updates f/q and never zeroes lp/bp/hp.
        v.exciter.noise_filter.lp = 0.0f;
        v.exciter.noise_filter.bp = 0.0f;
        v.exciter.noise_filter.hp = 0.0f;
#endif

#ifdef ENABLE_PHASE_5_EXCITERS
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
#endif


        // Stage-1 transient complexity: short coefficient modulation window.
        // Deterministic per-hit micro-randomization from note/voice/velocity.
        float vel_norm = fmaxf(0.0f, fminf(1.0f, velocity  * 0.007874015f));    // approx 1 / 127
        uint32_t seed = (uint32_t)note * 1103515245u
                      ^ (uint32_t)state.next_voice_idx * 12345u
                      ^ (uint32_t)velocity * 2654435761u;
        float r = ((float)((seed >> 8) & 0xFFFFu) / 32767.5f) - 1.0f; // [-1, +1]
        v.transient_frames_total = (uint32_t)(default_sample_rate * 0.035f); // 35 ms
        v.transient_frames_left = v.transient_frames_total;
        v.transient_inv_total = (v.transient_frames_total > 0) ? (1.0f / (float)v.transient_frames_total) : 0.0f;
        v.transient_lp_jitter = fmaxf(-0.08f, fminf(0.08f, (0.05f * vel_norm) + (0.02f * r)));
        v.transient_ap_jitter = fmaxf(-0.03f, fminf(0.03f, (0.015f * vel_norm) - (0.01f * r)));

        // Stage-1 model-specific transient presets.
        // Simple profile map: percussion gets longer/stronger transient modulation.
        uint8_t model_profile = m_model_a;
        bool percussion_model = (model_profile == 2 || model_profile == 3 ||
                                 model_profile == 4 || model_profile == 5 ||
                                 model_profile == 6);
        bool tube_model = (model_profile == 7 || model_profile == 8);
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
        if (m_preset_idx == 3) { // AcSnare: add short resonant wire-like sizzle emphasis.
            v.exciter.snare_wire_mix = 0.55f;
        }
         // Metallic presets: enable light Schroeder diffusion in feedback loop
         // for pseudo-modal density at low CPU cost.
         bool metallic_diff = (m_preset_idx == 13 || m_preset_idx == 14 || m_preset_idx == 28 || m_preset_idx == 29);
         v.resA.diffuser_mix = metallic_diff ? 0.32f : 0.0f;
         v.resB.diffuser_mix = metallic_diff ? 0.32f : 0.0f;
         v.resA.diffuser_g = 0.45f;
         v.resB.diffuser_g = 0.45f;

#if ENABLE_STAGE2_MODAL_PILOT
        // Stage-2 pilot extensions (CPU-light):
        // - Modal bank for complex metallic presets (Wodblk/Gong/Cymbal)
        // - Kick pitch-envelope (delay-length sweep)
        // - Clarinet reed nonlinearity in exciter path
        auto init_modal_modes = [&](float ratio2, float ratio3, float ratio4,
                                    float t60_1_ms, float t60_2_ms, float t60_3_ms, float t60_4_ms,
                                    float mix, float env1, float env2, float env3, float env4,
                                    uint8_t mode_count) {
            float base_f = 440.0f * fasterpowf(2.0f, ((float)note - 69.0f) * 0.08333333333f); // approx 1/12
            if (base_f < 20.0f) base_f = 20.0f;
            float f1 = fminf(base_f, 0.45f * default_sample_rate);
            float f2 = fminf(base_f * ratio2, 0.45f * default_sample_rate);
            float f3 = fminf(base_f * ratio3, 0.45f * default_sample_rate);
            float f4 = fminf(base_f * ratio4, 0.45f * default_sample_rate);
            float f5 = fminf(base_f * ratio4 * 1.31f, 0.45f * default_sample_rate);
            float f6 = fminf(base_f * ratio4 * 1.62f, 0.45f * default_sample_rate);
            float w1 = (2.0f * M_PI * f1) / default_sample_rate;
            float w2 = (2.0f * M_PI * f2) / default_sample_rate;
            float w3 = (2.0f * M_PI * f3) / default_sample_rate;
            float w4 = (2.0f * M_PI * f4) / default_sample_rate;
            float w5 = (2.0f * M_PI * f5) / default_sample_rate;
            float w6 = (2.0f * M_PI * f6) / default_sample_rate;
            v.modal_pilot_enabled = true;
            v.modal_mode_count = mode_count;
            v.modal_k_1 = 2.0f * fastercosfullf(w1);
            v.modal_k_2 = 2.0f * fastercosfullf(w2);
            v.modal_k_3 = (mode_count > 2) ? 2.0f * fastercosfullf(w3) : 0.0f;
            v.modal_k_4 = (mode_count > 3) ? 2.0f * fastercosfullf(w4) : 0.0f;
            v.modal_k_5 = (mode_count > 4) ? 2.0f * fastercosfullf(w5) : 0.0f;
            v.modal_k_6 = (mode_count > 5) ? 2.0f * fastercosfullf(w6) : 0.0f;
            v.modal_y2_1 = 0.0f; v.modal_y1_1 = fastersinfullf(w1);
            v.modal_y2_2 = 0.0f; v.modal_y1_2 = fastersinfullf(w2);
            v.modal_y2_3 = 0.0f; v.modal_y1_3 = (mode_count > 2) ? fastersinfullf(w3) : 0.0f;
            v.modal_y2_4 = 0.0f; v.modal_y1_4 = (mode_count > 3) ? fastersinfullf(w4) : 0.0f;
            v.modal_y2_5 = 0.0f; v.modal_y1_5 = (mode_count > 4) ? fastersinfullf(w5) : 0.0f;
            v.modal_y2_6 = 0.0f; v.modal_y1_6 = (mode_count > 5) ? fastersinfullf(w6) : 0.0f;
            v.modal_norm_count = 0;
            v.modal_env_1 = env1 * v.current_velocity;
            v.modal_env_2 = env2 * v.current_velocity;
            v.modal_env_3 = (mode_count > 2) ? (env3 * v.current_velocity) : 0.0f;
            v.modal_env_4 = (mode_count > 3) ? (env4 * v.current_velocity) : 0.0f;
            v.modal_env_5 = (mode_count > 4) ? (0.22f * env4 * v.current_velocity) : 0.0f;
            v.modal_env_6 = (mode_count > 5) ? (0.16f * env4 * v.current_velocity) : 0.0f;
            float t60_1_s = 0.001f * t60_1_ms;
            float t60_2_s = 0.001f * t60_2_ms;
            float t60_3_s = 0.001f * t60_3_ms;
            float t60_4_s = 0.001f * t60_4_ms;
            v.modal_decay_1 = (t60_1_s > 0.0f) ? fasterexpf(k_log_0001 / (t60_1_s * default_sample_rate)) : STAGE2_MODAL_DECAY1;
            v.modal_decay_2 = (t60_2_s > 0.0f) ? fasterexpf(k_log_0001 / (t60_2_s * default_sample_rate)) : STAGE2_MODAL_DECAY2;
            v.modal_decay_3 = (mode_count > 2 && t60_3_s > 0.0f) ? fasterexpf(k_log_0001 / (t60_3_s * default_sample_rate)) : STAGE2_MODAL_DECAY2;
            v.modal_decay_4 = (mode_count > 3 && t60_4_s > 0.0f) ? fasterexpf(k_log_0001 / (t60_4_s * default_sample_rate)) : STAGE2_MODAL_DECAY2;
            // Modes 5 and 6 share T60_4's base but decay faster: T60_5 = 0.85×T60_4,
            // T60_6 = 0.70×T60_4.  Reusing decay_4 via power law avoids two extra expf
            // calls: exp(k / (r*T)) = exp(k/T)^(1/r) = decay_4^(1/r).
            v.modal_decay_5 = (mode_count > 4 && t60_4_s > 0.0f) ? fasterpowf(v.modal_decay_4, 1.0f / 0.85f) : STAGE2_MODAL_DECAY2;
            v.modal_decay_6 = (mode_count > 5 && t60_4_s > 0.0f) ? fasterpowf(v.modal_decay_4, 1.0f / 0.70f) : STAGE2_MODAL_DECAY2;
            v.modal_mix = mix;
        };

        uint8_t program = (uint8_t)m_params[k_paramProgram];
        if (program == k_Woodblock) {         // Wodblk pilot
            init_modal_modes(STAGE2_MODAL_RATIO_2, 0.0f, 0.0f,
                             STAGE2_MODAL_T60_1_MS, STAGE2_MODAL_T60_2_MS, 0.0f, 0.0f,
                             STAGE2_MODAL_MIX, STAGE2_MODAL_ENV1, STAGE2_MODAL_ENV2, 0.0f, 0.0f, 2);
        } else if (program == k_Cymbal) {  // Cymbal: 6-mode inharmonic shimmer bank
            init_modal_modes(2.45f, 3.91f, 5.62f,
                             140.0f, 260.0f, 420.0f, 620.0f,
                             0.11f, 0.75f, 0.60f, 0.46f, 0.34f, 6);
        } else if (program == k_Gong) {  // Gong: 6-mode dense metallic modes
            init_modal_modes(1.78f, 2.63f, 3.81f,
                             220.0f, 420.0f, 680.0f, 920.0f,
                             0.16f, 0.85f, 0.70f, 0.52f, 0.38f, 6);
        }

        if (program == k_KickDrum) {         // Kick: downward pitch sweep (portamento-like)
            v.pitch_env = 1.0f;
            v.pitch_env_decay = 0.9989f;
            v.pitch_env_amt = 9.0f; // semitone-domain sweep depth
        }
        if (program == k_Clarinet) {         // Clarinet: lightweight reed nonlinearity
            v.reed_nl_enabled = true;
            v.reed_nl_drive = 1.8f;
        }
#endif
    }

    inline void NoteOff(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];

            // Find the voice playing this note that hasn't already been released
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;

#ifdef ENABLE_PHASE_5_EXCITERS
                v.exciter.noise_env.release();
                v.exciter.noise_env_hi.release();
                v.exciter.master_env.release();
#endif
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
#ifdef ENABLE_PHASE_5_EXCITERS
            state.voices[i].exciter.noise_env.release();
            state.voices[i].exciter.noise_env_hi.release();
            state.voices[i].exciter.master_env.release();
#endif
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
            m_pitch_bend_mult = powf(2.0f, -semitones* 0.08333333333f); // approx 1 / 12
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

        // delay_length is clamped to [2, DELAY_BUFFER_SIZE-2] so read_idx ≥ −4094.
        // One addition of DELAY_BUFFER_SIZE always brings it into [2, 4096).
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
#ifdef ENABLE_PHASE_7_MODELS
        float ap = fminf(0.99f, wg.ap_coeff + wg.model_ap_base);
#else
        float ap = wg.ap_coeff;
#endif
        float ap_out = (ap * delay_out) + wg.ap_x1 - (ap * wg.ap_y1);
        wg.ap_x1 = delay_out;
        wg.ap_y1 = ap_out;

        // 3b. Loss Filter (1-pole Lowpass) — applied AFTER dispersion.
        // wg.lowpass_coeff was pre-calculated in setParameter()
        wg.z1 = (ap_out * wg.lowpass_coeff) + (wg.z1 * (1.0f - wg.lowpass_coeff));
        float filtered_out = wg.z1;
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
#ifdef ENABLE_PHASE_7_MODELS
        float new_val = exciter_input + (filtered_out * wg.feedback_gain * wg.phase_mult);
#else
        float new_val = exciter_input + (filtered_out * wg.feedback_gain);
#endif

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

#ifdef ENABLE_PHASE_5_EXCITERS
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
 #ifdef ENABLE_PHASE_6_FILTERS
             raw_noise = ex.noise_filter.process(raw_noise);
 #endif
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
            float noise_sum = (low_part + high_part) * ex.noise_decay_coeff;
            if (ex.snare_wire_mix > 0.001f) {
                // Very short resonant burst path (2nd-order): emphasizes snare-wire sizzle
                // without feeding broadband noise into pitch-tracked waveguides.
                // Poles at r=0.945, f≈3.5kHz (a1=2r·cos(w), a2=r²): wire sizzle in
                // the snare wire frequency range; previous 695 Hz was far too low.
                float wire = noise_sum + (1.6951f * ex.snare_wire_z1) - (0.8930f * ex.snare_wire_z2);
                ex.snare_wire_z2 = ex.snare_wire_z1;
                ex.snare_wire_z1 = wire;
                noise_sum = (noise_sum * (1.0f - ex.snare_wire_mix)) + (wire * ex.snare_wire_mix * 0.35f);
            }
            ex.noise_out_sample = noise_sum;
         }
 #endif

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
#ifndef RENDER_STAGE
#define RENDER_STAGE 4
#endif

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
#if ENABLE_STAGE2_MODAL_PILOT
                if (voice.reed_nl_enabled) {
                    // Lightweight asymmetric waveshaper to emulate reed contact.
                    float x = exciter_sig * voice.reed_nl_drive;
                    float y = (x >= 0.0f) ? fastertanhf(x) : (0.6f * fastertanhf(1.6f * x));
                    exciter_sig = (0.65f * exciter_sig) + (0.35f * y);
                }
#endif
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

#if RENDER_STAGE >= 2
                // ── Stage 2: Waveguide resonators ──────────────────────────
                // If Stage 2 is silent but Stage 1 is not, the waveguide has
                // zero delay_length or zero feedback_gain on this hardware.
                //
                // Model-aware coupling clamps are pre-computed once per block above.
                // Both diff-frequency and same-frequency pairs use K=0.8 to guarantee
                // G+C < 1 (coupled stability) across all Dkay settings.
                float safe_cpl_a = v_safe_cpl_a;
                float safe_cpl_b = v_safe_cpl_b;
#if ENABLE_STAGE2_MODAL_PILOT
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
#endif

                // Tube models (OpenTube=7, ClosedTube=8, phase_mult=-1) need noise fed
                // into the waveguide so breath continuously excites the tube resonance
                // (physically correct for flute/clarinet).  Percussion models do NOT get
                // noise in the waveguide — it would be pitch-filtered into a tonal ring.
#if defined(ENABLE_PHASE_5_EXCITERS) && defined(ENABLE_PHASE_7_MODELS)
                float tube_noise_A = (voice.resA.phase_mult < 0.0f)
                                     ? voice.exciter.noise_out_sample : 0.0f;
#else
                float tube_noise_A = 0.0f;
#endif
                float inputA = exciter_sig + tube_noise_A + (voice.resB_out_prev * safe_cpl_a);
                outA = process_waveguide(voice.resA, inputA);
                float outB = 0.0f;
#ifdef ENABLE_PHASE_8_2D_DRUMHEAD
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
#if defined(ENABLE_PHASE_5_EXCITERS) && defined(ENABLE_PHASE_7_MODELS)
                    float tube_noise_B = (voice.resB.phase_mult < 0.0f)
                                         ? voice.exciter.noise_out_sample : 0.0f;
#else
                    float tube_noise_B = 0.0f;
#endif
                    float inputB = exciter_sig + tube_noise_B + (voice.resA_out_prev * safe_cpl_b);
                    outB = process_waveguide(voice.resB, inputB); //
                    voice.resA_out_prev = outA;
                    voice.resB_out_prev = outB;
                } else {
                    // ResB is bypassed. Keep its output at 0 to prevent coupling artifacts.
                    voice.resA_out_prev = outA;
                    voice.resB_out_prev = 0.0f;
                }
#else
                if (m_active_partials >= 16) {
#if defined(ENABLE_PHASE_5_EXCITERS) && defined(ENABLE_PHASE_7_MODELS)
                    float tube_noise_B = (voice.resB.phase_mult < 0.0f)
                                         ? voice.exciter.noise_out_sample : 0.0f;
#else
                    float tube_noise_B = 0.0f;
#endif
                    float inputB = exciter_sig + tube_noise_B + (voice.resA_out_prev * safe_cpl_b);
                    outB = process_waveguide(voice.resB, inputB);
                    voice.resA_out_prev = outA;
                    voice.resB_out_prev = outB;
                } else {
                    voice.resA_out_prev = 0.0f;
                    voice.resB_out_prev = 0.0f;
                }
#endif
                voice_out = ((outA * (1.0f - state.mix_ab)) + (outB * state.mix_ab))
                            * voice.current_velocity;

#ifdef ENABLE_PHASE_5_EXCITERS
                // Parallel noise path: noise bypasses the waveguide and mixes directly
                // into the voice output.  This preserves the broadband character that the
                // resonator would otherwise pitch-filter away (snare buzz, cymbal wash,
                // hi-hat hiss, shaker rattle).  The ×5 factor brings noise amplitude into
                // the same ballpark as the resonator output driven by the ×15 mallet.
                voice_out += voice.exciter.noise_out_sample * 5.0f * voice.current_velocity;
#endif
#if ENABLE_STAGE2_MODAL_PILOT
                if (voice.modal_pilot_enabled) {
                    // Update modes 1/2 (and optionally 3/4 for metallic presets).
#if defined(__ARM_NEON) || defined(__aarch64__)
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
                     if (voice.modal_mode_count > 4) {
                         float y5n = (voice.modal_k_5 * voice.modal_y1_5) - voice.modal_y2_5;
                         voice.modal_y2_5 = voice.modal_y1_5;
                         voice.modal_y1_5 = y5n;
                         float y6n = (voice.modal_k_6 * voice.modal_y1_6) - voice.modal_y2_6;
                         voice.modal_y2_6 = voice.modal_y1_6;
                         voice.modal_y1_6 = y6n;
                     }
#else
                    float y1n = (voice.modal_k_1 * voice.modal_y1_1) - voice.modal_y2_1;
                    voice.modal_y2_1 = voice.modal_y1_1;
                    voice.modal_y1_1 = y1n;
                    float y2n = (voice.modal_k_2 * voice.modal_y1_2) - voice.modal_y2_2;
                    voice.modal_y2_2 = voice.modal_y1_2;
                    voice.modal_y1_2 = y2n;
                    if (voice.modal_mode_count > 2) {
                        float y3n = (voice.modal_k_3 * voice.modal_y1_3) - voice.modal_y2_3;
                        voice.modal_y2_3 = voice.modal_y1_3;
                        voice.modal_y1_3 = y3n;
                        float y4n = (voice.modal_k_4 * voice.modal_y1_4) - voice.modal_y2_4;
                        voice.modal_y2_4 = voice.modal_y1_4;
                        voice.modal_y1_4 = y4n;
                     }
                     if (voice.modal_mode_count > 4) {
                         float y5n = (voice.modal_k_5 * voice.modal_y1_5) - voice.modal_y2_5;
                         voice.modal_y2_5 = voice.modal_y1_5;
                         voice.modal_y1_5 = y5n;
                         float y6n = (voice.modal_k_6 * voice.modal_y1_6) - voice.modal_y2_6;
                         voice.modal_y2_6 = voice.modal_y1_6;
                         voice.modal_y1_6 = y6n;
                    }
#endif
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
                     voice_out += (m1
                                 + (stage2_modal_amp_ratio_2 * m2)
                                 + (0.45f * m3)
                                 + (0.28f * m4)
                                 + (0.18f * m5)
                                 + (0.12f * m6)) * voice.modal_mix;
                     if (voice.modal_env_1 < silence_threshold &&
                         voice.modal_env_2 < silence_threshold &&
                         (voice.modal_mode_count <= 2 || (voice.modal_env_3 < silence_threshold && voice.modal_env_4 < silence_threshold &&
                                                          (voice.modal_mode_count <= 4 || (voice.modal_env_5 < silence_threshold && voice.modal_env_6 < silence_threshold))))) {
                         voice.modal_pilot_enabled = false;
                         voice.modal_mode_count = 0;
                    }
                }
#endif
#endif // RENDER_STAGE >= 2

#if RENDER_STAGE >= 3
                // ── Stage 3: master_env fade + squelch ────────────────────
                // If Stage 3 is silent but Stage 2 is not, the Phase 18
                // pre-advance fix is not working on this ARM binary — the
                // envelope is stuck at 0 on the first GateOff tick.
#ifdef ENABLE_PHASE_5_EXCITERS
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
#endif // ENABLE_PHASE_5_EXCITERS
#endif // RENDER_STAGE >= 3

#if RENDER_STAGE >= 4
                // ── Stage 4a: Tilt EQ ──────────────────────────────────────
                voice.tone_lp = (voice_out * kToneLpMix) + (voice.tone_lp * (1.0f - kToneLpMix));
                if (tone_val < zeroThreshold) {
                     voice_out = voice_out + (voice.tone_lp - voice_out) * (-tone_val * kInvToneCutDivisor);
                 } else if (tone_val > zeroThreshold) {
                     float hp = voice_out - voice.tone_lp;
                     voice_out += hp * (tone_val * kInvToneBoostDivisor);
                 }
#endif // RENDER_STAGE >= 4

                main_out[i * 2]     += voice_out * state.master_gain;
                main_out[i * 2 + 1] += voice_out * state.master_gain;

#ifdef UNIT_TEST_DEBUG
                if (voice_idx == state.next_voice_idx) {
                    ut_exciter_out = exciter_sig;
                    ut_delay_read  = outA;
                    ut_voice_out   = voice_out;
                }
#endif

#if RENDER_STAGE < 3
                // ── Stage 1/2: voice lifetime management ───────────────────
                // Without Stage 3 squelch, voices stay is_active=true forever.
                // Deactivate once the mallet has fully decayed and (if Phase 5)
                // the noise envelope is also idle — keeps voice slots free for
                // re-triggering and prevents stale voices burning CPU.
#ifdef ENABLE_PHASE_5_EXCITERS
                if (voice.is_releasing &&
                        voice.exciter.mallet_lp2 < 1e-6f &&
                        voice.exciter.noise_env.state == ENV_IDLE &&
                        voice.exciter.noise_env_hi.state == ENV_IDLE) {
                    voice.is_active = false;
                }
#else
                if (voice.is_releasing && voice.exciter.mallet_lp2 < 1e-6f) {
                    voice.is_active = false;
                }
#endif
#endif // RENDER_STAGE < 3

            }
        }

#if RENDER_STAGE < 4
        // ── Stage 1-3: hard-clip output ────────────────────────────────────
        // Stage 4 uses soft-clip + overdrive.  For debug stages the raw mallet
        // impulse (~3-4 × full-scale) must be clamped or the Drumlogue DAC
        // saturates on the first note and may engage hardware protection.
        for (size_t i = 0; i < frames; ++i) {
            main_out[i * 2]     = fmaxf(-0.99f, fminf(0.99f, main_out[i * 2]));
            main_out[i * 2 + 1] = fmaxf(-0.99f, fminf(0.99f, main_out[i * 2 + 1]));
        }
#endif // RENDER_STAGE < 4

#if RENDER_STAGE >= 4
        // ── Stage 4b: Master FX (filter + overdrive + brickwall) ──────────
        for (size_t i = 0; i < frames; ++i) {
            float mix_l = main_out[i * 2];
            float mix_r = main_out[i * 2 + 1];
#ifdef ENABLE_PHASE_6_FILTERS
            mix_l = state.master_filter.process(mix_l);
            mix_r = mix_l;
#endif
            mix_l *= state.master_drive;
            mix_r *= state.master_drive;
#if defined(__ARM_NEON) || defined(__aarch64__)
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
#endif // RENDER_STAGE >= 4
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

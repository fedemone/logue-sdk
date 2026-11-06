#pragma once
// Brachetti Plate — synth engine of the brachetti_plate unit.
//
// Small metal percussion: Cowbell, Triangle, Bell Tree and Tick.  A voice
// is a mallet + noise exciter driving an inharmonic plate modal bank, with
// noise that follows the ring down, a metallic FM chirp, a high-band
// shimmer branch and (Tick) a dedicated hat filter.
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship
// with, each renders bit-identically to the same preset there;
// tools/brachetti_equiv checks it.
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

// 2^x for the REFERENCE-ANCHORED knob curves.  fasterpow2f is precise
// enough for a knob's response (±0.3 %), EXCEPT at the anchor:
// fasterpow2f(0) ≈ 0.9614, not 1.0, and every anchored mapping evaluates at
// exactly 0 for the shipped preset, where the factor must be exactly 1.
// So the anchor is guarded.  Never use this for tuning or decay
// coefficients: those need exact expf / cosf / exp2f (fasterexpf is badly
// wrong for |x| < 0.001; fastercosfullf shifts low mode frequencies).
inline float knob_exp2(float x) {
    return (x == 0.0f) ? 1.0f : fasterpow2f(x);
}

// Compile-time check on one column of LoadPreset's preset table: every row's
// value lies in [lo, hi].  Pins the columns LoadPreset does NOT load, so
// giving one preset its own Tone or Cutoff — which would be silently
// ignored — fails the build instead.
template <int R, int C>
constexpr bool preset_column_in(const int16_t (&t)[R][C], int col, int lo, int hi) {
    for (int r = 0; r < R; ++r)
        if (t[r][col] < lo || t[r][col] > hi) return false;
    return true;
}

// ==============================================================================
// CONSTANTS
// ==============================================================================
static constexpr float    default_sample_rate = 48000.0f;
static constexpr float    inverse_default_sample_rate = 2.0833333333e-5f; // 1 / 48000
static constexpr float    kToneLpMix = 0.3f;
static constexpr float    kInvToneCutDivisor = 0.1f;         // 1 / 10
static constexpr float    kInvToneBoostDivisor = 0.13333334f; // 1 / 7.5 (widened ×2: every preset ships Tone=0, so no regression)
static constexpr float    zeroThreshold = 0.0f;
static constexpr float    alpha = 0.01f;
static constexpr float    limiter = 0.99f;
static constexpr int      kSquelchGuardSamples = 1000; // ~20 ms
static constexpr float    kSquelchThreshold = 0.0001f; // -80 dB
static constexpr float    k_log_2_of_200 = 7.643856f;
static constexpr float    stage2_modal_amp_ratio_2 = 0.6f;
static constexpr float    silence_threshold = 1e-5f;
// Master peak limiter: unity below the threshold, a soft knee above it that
// is asymptotic to the ceiling (see Stage 4b in processBlock).  It is a gain
// envelope, so it holds the ceiling at any threshold; the threshold only
// trades loudness against how much gain riding happens.
static constexpr float    kMasterLimThr     = 0.75f;
static constexpr float    kMasterLimCeil    = 0.99f;        // == the brickwall
static constexpr float    kMasterLimSpan    = kMasterLimCeil - kMasterLimThr;
// Master limiter release, ms.  20 ms keeps the strike's crest with the
// fewest added harmonics on long low tones; longer releases only get
// quieter, and two hits 150 ms apart show no ducking.
static constexpr float    kMasterLimRelMs   = 20.0f;
// 1-exp(-1/tau) ~= 1/tau for tau this large (960 samples at 20 ms).
static constexpr float    kMasterLimRelCoef =
    1.0f / (kMasterLimRelMs * 0.001f * 48000.0f);

// Mixed into each voice's noise seed on every strike (see NoteOn).  The
// value is the original brachetti's for this engine, kept so the noise
// sequences — and so every render — stay exactly as they were.
static constexpr uint32_t kNoiseSeedStir = 0x26A9DBE5u;

// ==============================================================================
// MAIN CLASS
// ==============================================================================
class alignas(16) BrachettiSynth {
public:
    // ==============================================================================
    // PARAMETER INDEX ENUM (Strictly matches header.c)
    // ==============================================================================
    enum ParamIndex {
        k_paramProgram = 0,  // 0
        k_paramPitch,        // 1
        k_paramPoly,         // 2
        k_paramVelocity,     // 3
        k_paramMlltRes,      // 4
        k_paramMlltStif,     // 5
        k_paramVlMllRes,     // 6
        k_paramVlMllStf,     // 7
        k_paramPartls,       // 8
        k_paramModel,        // 9
        k_paramDkay,         // 10
        k_paramMterl,        // 11
        k_paramTone,         // 12
        k_paramHitPos,       // 13
        k_paramRel,          // 14
        k_paramInharm,       // 15
        k_paramCutoff,       // 16
        k_paramTubRad,       // 17
        k_paramGain,         // 18
        k_paramNzMix,        // 19
        k_paramNzRes,        // 20
        k_paramNzFltr,       // 21
        k_paramNzFltFrq,     // 22
        k_paramResnc,        // 23
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of every preset.
    static_assert(k_lastParamIndex == 24, "header.c declares 24 params");
    // Pitch knob reach, semitones either side of the preset's root note.
    // Must equal the min/max header.c declares for "Pitch".
    static constexpr int kPitchRangeSemis = 24;
    enum ProgramIndex {
        k_Cowbell = 0,      // 0  -sample: Cowbell_2.wav (408Hz +/- 30Hz)
        k_Triangle,         // 1  -sample: Triangle-Bell_C#.wav (3753Hz +/- 100Hz), Triangle-Bell_F5.wav (795Hz +/- 100Hz)
        k_BellTree,         // 2  -sample:
        k_Tick,             // 3  — a metallic closed-hat chick + a low "clack" mode
        k_NumPrograms       // 4 — marker (count)
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
    k_snare_wire_a2,
    k_noise_band_mix,
    k_noise_hi_lp_coeff,
    k_use_hat_filter,
    k_diffuser_mix,
    k_modal_mix,       // Modal bank mix (0.0 = off, overrides ModalPresetConfig.mix)
    k_onset_attack_ms, // Global onset ramp (ms); 0 = instant (no ramp applied)
    k_model_param_total
};

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

ModalPresetConfig modal_preset_configs[k_NumPrograms] = {
    /* k_Cowbell: plate ratios.  Ref Cowbell_2 is a 120 ms clank with a ~5.6 kHz
       early centroid; the 500 ms ring read as a mellow bell.  T60s shortened to
       the clank, upper modes lifted, and modes 5/6 (ratios 6.9/9.8 ≈ 2.7/3.8 kHz
       at the shipped note) supply the missing clank brightness. */ {2.00f, 2.68f, 4.30f, 180.0f, 140.0f, 100.0f, 70.0f, 0.22f, 0.70f, 0.72f, 0.60f, 0.48f, 6, 6.90f, 9.80f, 0.55f, 0.40f},
    /* k_Triangle: 6 modes.  A real triangle's audible spectrum is dominated by
       >5 kHz partials (ref centroid 8.5 kHz); modes 4-6 add the 4-8 kHz sheen.
       The ring is short (t60 ≈ 275 ms).  Dkay is a REFERENCE ANCHOR —
       LoadPreset captures the row's own Dkay (60, displayed 600) into
       m_modal_dkay_ref, so t60_scale is exactly 1.0 at the shipped value: to
       change the length, scale the T60s here, not the Dkay column. */
    {2.756f, 5.404f, 9.00f, 790.0f, 658.0f, 461.0f, 329.0f, 0.15f, 0.80f, 0.55f, 0.30f, 0.85f, 6, 13.1f, 17.9f, 0.80f, 0.70f},
    /* k_BellTree */ {2.01f, 2.76f, 0.0f, 900.0f, 700.0f, 0.0f, 0.0f, 0.17f, 0.80f, 0.60f, 0.0f, 0.0f, 3, 0, 0.0f},
    /* k_Tick: a metallic closed-hat chick + a low "clack" under it (mode 4
       at ratio 1.40, 50ms, strong) */
    {2.92f, 6.37f, 1.40f, 45.0f, 28.0f, 316.0f, 70.0f, 0.30f, 0.85f, 0.70f, 0.50f, 1.25f, 4, 0.0f, 0.0f}
};

float model_param_presets[k_NumPrograms][k_model_param_total]{
    /*               k_base_fm_hz, k_snare_wire_z1, k_snare_wire_z2, k_snare_wire_a2, k_noise_band_mix, k_noise_hi_lp_coeff, k_use_hat_filter, k_diffuser_mix, k_modal_mix, k_onset_attack_ms */
    /* k_Cowbell     */ { 900.00000f, 0.00000f, 0.00000f, 0.00000f, 0.45000f, 0.75000f, false, 0.00000f, 0.22000f, 0.00000f},
    /* k_Triangle    */ { 1800.00000f, 0.00000f, 0.00000f, 0.00000f, 0.15000f, 0.96000f, false, 0.16000f, 0.40000f, 0.00000f},
    /* k_BellTree    */ { 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, 0.00000f, false, 0.00000f, 0.17000f, 0.00000f},
    /* k_Tick        */ { 3400.00000f, 2.00000f, 6000.00000f, 0.80000f, 0.86000f, 0.39000f, true, 0.34000f, 0.24000f, 0.50000f}
};

// Preset → output trim: gain-stages each preset into the master limiter.
// Calibrated on the note's BODY, not its strike — the peak over [10 ms, 1 s]
// at velocity 127 on the preset's root, put on a body target (1.5) near the
// limiter threshold — so the limiter catches transients instead of holding
// whole notes at the ceiling.  Clamped to <= 1: it only gives back drive the
// master stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
// NOTE: plain member (no static/const/constexpr), like the tables above.
float kPresetOutTrim[k_NumPrograms] = {
    /* 0  Cowbel  */ 0.11212f,
    /* 1  Trngle  */ 0.25227f,
    /* 2  BelTre  */ 0.60091f,
    /* 3  Tick    */ 1.00000f};

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

    // ==============================================================================
    // 0. Lifecycle & Initialization
    // ==============================================================================

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        // 1. Hardware Sanity Checks
        // The Drumlogue is strictly 48kHz, stereo.
        // If Korg ever releases a 96kHz device, this prevents your delay math from breaking.
        if (desc->samplerate != (uint32_t)default_sample_rate) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;

        // 2. Performance knobs to their header.c defaults.  LoadPreset does
        // not write them (that is what keeps them in step with the host's
        // parameter store), so without this they would come up as 0 on a
        // fresh object (Cutoff 0, clamped to its 10 Hz minimum) and, on an
        // object Init() is called on again, as whatever the last session
        // left.  Then the fresh-object values Reset() no longer writes,
        // because they belong to knobs once a preset is loaded.
        seed_performance_params();
        init_fresh_voice_defaults();

        // 3. Clear all memory explicitly at boot
        Reset();

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

    // Called when the engine needs a hard flush.  RUNTIME state only — never a
    // parameter's value (the SDK: unit_reset() clears notes, envelopes and
    // delay lines, but parameter values must not be reset).  Every
    // knob-derived setting already holds its knob's value, so Reset() leaves
    // them alone; the fresh-object values a first boot needs come from
    // init_fresh_voice_defaults(), which only Init() calls.
    inline void Reset() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            // Clear voice active / pitch memory
            state.voices[i].is_active    = false;
            state.voices[i].is_releasing = false;

            // Reset all runtime modulation state: envelopes, transient shaper,
            // modal bank, pitch env, onset and the other per-voice layers.
            state.voices[i].PartialReset();

            // Noise filter: clear its MEMORY only.  Its mode and cutoff are the
            // NzFltr / NzFltFrq knobs and stay as they are.
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

        // Bus gain: as loud as the bus can take before the master limiter's soft
        // knee starts to narrow velocity sensitivity (a ghosted hit must stay well
        // under a neutral one).
        state.master_gain  = 2.3f;
        // The drive is the Gain knob's and stays.  What Reset() does own is a
        // drive and trim DEFERRED behind a preset-change fade: the fade waits
        // for the outgoing voices, and Reset() has just killed them, so
        // complete it now — exactly what processBlock would do as the last
        // fade retired.  The queued values are the INCOMING preset's (the one
        // actually loaded).
        if (m_pending_drive >= 0.0f) state.master_drive = m_pending_drive;
        if (m_pending_trim  >= 0.0f) m_out_trim         = m_pending_trim;
        m_pending_drive    = -1.0f;
        m_pending_trim     = -1.0f;
        // Master-stage state dies with the voices too: a limiter envelope left
        // high would ride the first strike after Resume() down for ~20 ms, and
        // a non-zero idle counter would keep the master chain spinning on an
        // empty buffer.
        state.master_lim_env = 0.0f;
        m_idle_flush_blocks  = 0;
        // Tone (state.tone) is a knob value and is left alone.

        // Master filter is a LOWPASS "Cutoff" (mode 0): the knob opens it as it
        // rises.  Its coefficients are the Cutoff/Resnc knobs and stay; only the
        // filter's memory is cleared.
        state.master_filter.mode = 0;
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

    // Tracks the raw UI integer for every parameter slot
    int32_t m_params[k_lastParamIndex] = {0};
    uint8_t m_preset_idx = 0;
    // REFERENCE ANCHORS: the knob values the current preset shipped with,
    // captured in LoadPreset.  Every knob mapping in NoteOn is a delta from
    // these, so each preset plays its calibrated sound at the shipped knob
    // values and only knob MOVEMENT alters it (the m_modal_ / m_snare_
    // names come from the original brachetti, where every family shared
    // these anchors).
    float   m_modal_dkay_ref   = 0.975f;  // Dkay, normalized 0..1
    float   m_modal_rel_ref    = 0.9f;    // Rel, normalized 0..1
    float   m_modal_stiff_ref  = 0.26f;   // MlltStif, normalized 0.01..1
    uint8_t m_modal_model_ref  = 0;       // Model index
    int32_t m_modal_partls_ref = 0;       // Partls (0-4)
    float   m_modal_inharm_ref = 0.0f;    // Inharm, normalized -1..1
    float   m_modal_mterl_ref  = 0.5f;    // Mterl, normalized 0..1
    float   m_modal_hitpos_ref = 0.26f;   // HitPos, normalized 0..1
    float   m_modal_tubrad_ref = 0.25f;   // TubRad, normalized 0..1
    float   m_modal_mltres_ref = 0.5f;    // MlltRes, normalized 0..1
    float   m_snare_vlstf_ref  = 0.0f;    // VlMllStf * 0.01

    // Called by unit_get_param_value so the OS knows what to draw on the screen.
    inline int32_t getParameterValue(uint8_t index) const {
        // CRITICAL UI FIX: Prevent OS out-of-bounds reads
        if (index >= k_lastParamIndex) return 0;
        return m_params[index];
    }

    inline uint8_t getPresetIndex() const {
        return m_preset_idx;
    }

    // The current preset's root note (its Note column) and the note the
    // sequencer path actually plays, root + Pitch.  Host tools use these to
    // turn an absolute note into a Pitch offset; nothing on the device reads
    // them.
    inline uint8_t PresetRootNote() const { return m_root_note; }
    inline uint8_t PlayedNote() const     { return m_ui_note; }

    // Called by unit_set_param_value(0, value) to load a new patch
    inline void LoadPreset(uint8_t idx) {
        // Reject an out-of-range preset BEFORE anything is written: the tables
        // below are all sized k_NumPrograms.
        if (idx >= k_NumPrograms) return;

        // A voice cannot survive a preset change: the rest of this function
        // rewrites the shared per-voice DSP coefficients underneath it.  Arm a
        // ~10 ms fade-out so it retires click-free under its OWN settings
        // instead of being cut dead or re-excited by the incoming preset.
        // Only on a REAL change — Init()'s LoadPreset(0) and re-selecting the
        // current preset must stay no-ops so shipped renders are unaffected.
        const bool preset_changed = (idx != m_preset_idx);
        const float prev_drive = state.master_drive;
        const float prev_trim  = m_out_trim;
        if (preset_changed) {
            const float fmul = env_mul(kPresetFadeTauSec, default_sample_rate);
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

        // One row per preset, one column per knob in ParamIndex order (the knob
        // values the preset ships with).
        // The Pitch column holds the preset's ROOT NOTE (MIDI), not a knob
        // value: the Pitch knob is an offset in semitones FROM it, so the
        // column is captured into m_root_note and never written to the knob.
        // Columns LoadPreset does NOT load are knobs the host owns:
        // Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc.
        // Their values below are never read.
        // int16_t: the stored range is [-10, 1999].
        static constexpr int16_t presets[k_NumPrograms][k_lastParamIndex] = {
            //  Prg Root  Ply  Vel MlRs MlSt VlRs VlSt Ptls  Mdl  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain NzMx NzRs NzFl NzFq Rsnc
            {   0,  67,   0,   1, 800,  42,   0,   0,   0,   4, 175,  20,   0,   0,   4,   3,1999,   3,  30,  40, 300,   0,1000,  71},   // 0: Cowbell   — short clank (modal cfg 180 ms) + strike noise (NzMx 40) toward the bright reference clank
            {   1,  69,   0,   1, 900,  47,   0,   0,   0,   1,  60,  20,   0,   0,  15,   2,1999,  20,   0,  10, 300,   0,1500,  71},   // 1: Triangle  — NzMx 10 feeds the raised hf_branch (NoteOn) for the >8 kHz metal sheen.  Dkay 60 is the ANCHOR of the short T60s in modal_preset_configs — change the two together or the knob just re-lengthens the ring.
            {   2,  84,   0,   1, 900,  42,   0,   0,   0,   1, 200,  20,   0,   0,   8,   1,1999,   3,   0,   0, 300,   0,1200,  71},   // 2: BelTre
            {   3,  79,   0,   1, 900,  50,   0,   0,   0,   4, 160,  14,   0,   0,   2,   2,1999,   3,   0,  58, 960,   2, 900,  71}    // 3: Tick      — a metallic closed-hat chick + clack mode (modal cfg)
        };

        static_assert(preset_column_in(presets, k_paramVlMllRes, 0, 0),
                      "VlMllRes is not loaded per preset: every row must ship 0");

        static_assert(preset_column_in(presets, k_paramTone, 0, 0),
                      "Tone is not loaded per preset: every row must ship 0");

        static_assert(preset_column_in(presets, k_paramCutoff, 1999, 1999),
                      "Cutoff is not loaded per preset: every row must ship 1999");

        static_assert(preset_column_in(presets, k_paramResnc, 71, 71),
                      "Resnc is not loaded per preset: every row must ship 71");

        static_assert(preset_column_in(presets, k_paramDkay, 0, 200),
                      "Dkay is 0..200: every consumer saturates at 200");

        static_assert(preset_column_in(presets, k_paramPartls, 0, 4),
                      "Partls is 0..4 in this unit");

        // Apply the row, skipping Program (recursion) and the knobs the host owns.
        // Pitch is skipped too: column 1 is this preset's ROOT, the knob an
        // offset from it, so the root moves and the offset stays.
        m_root_note = (uint8_t)presets[idx][k_paramPitch];
        update_played_note();
        for (uint8_t param_id = 0; param_id < k_lastParamIndex; ++param_id) {
            if (param_id == k_paramProgram) continue;
            if (param_id == k_paramPitch) continue;
            if (param_id == k_paramPoly) continue;
            if (param_id == k_paramVelocity) continue;
            if (param_id == k_paramVlMllRes) continue;
            if (param_id == k_paramTone) continue;
            if (param_id == k_paramCutoff) continue;
            if (param_id == k_paramResnc) continue;
            setParameter(param_id, presets[idx][param_id]);
        }

        // Capture the shipped knob values as the REFERENCE ANCHORS: every knob
        // mapping pivots on them, so the shipped preset plays its calibrated
        // sound and only knob MOVEMENT alters it.
        m_modal_dkay_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramDkay] * 0.005f));
        m_modal_stiff_ref = fmaxf(0.01f, fminf(1.0f, (float)presets[idx][k_paramMlltStif] * 0.02f));
        m_modal_model_ref  = (uint8_t)presets[idx][k_paramModel];
        m_modal_partls_ref = (presets[idx][k_paramPartls] <= 4) ? presets[idx][k_paramPartls] : 0;
        m_modal_inharm_ref = fmaxf(-1.0f, fminf(1.0f, (float)presets[idx][k_paramInharm] * 0.01f));
        m_modal_mterl_ref  = (fmaxf(-10.0f, fminf(30.0f, (float)presets[idx][k_paramMterl])) + 10.0f) * 0.025f;
        m_modal_hitpos_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramHitPos] * 0.01f));
        m_modal_tubrad_ref = fmaxf(0.0f, fminf(20.0f, (float)presets[idx][k_paramTubRad])) * 0.05f;
        m_modal_mltres_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramMlltRes] * 0.001f));
        m_modal_rel_ref    = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramRel] * 0.05f));
        m_snare_vlstf_ref  = (float)presets[idx][k_paramVlMllStf] * 0.01f;

        // Hold the OUTGOING master drive and output trim until the fades finish:
        // Gain is a master parameter, so the incoming preset's drive would
        // otherwise hit the outgoing tail.  Pre-scaling the voice cannot fix this —
        // the ±0.99 clamp sits between the voice and the drive.  The deferral
        // cannot go stale: the parameter loop above always writes k_paramGain,
        // whose case clears m_pending_drive, so the queue only holds the drive of
        // the preset now loading (Reset() completes a pending deferral itself).
        // The trim moves further than the drive (up to 22 dB between presets), so
        // it is deferred on the same terms.
        m_out_trim = kPresetOutTrim[idx];
        if (preset_changed &&
            (state.master_drive != prev_drive || m_out_trim != prev_trim)) {
            bool fading = false;
            for (int i = 0; i < NUM_VOICES; ++i)
                if (state.voices[i].is_active && state.voices[i].fade_mul < 1.0f) fading = true;
            if (fading) {
                if (state.master_drive != prev_drive) {
                    m_pending_drive  = state.master_drive;
                    state.master_drive = prev_drive;
                }
                if (m_out_trim != prev_trim) {
                    m_pending_trim = m_out_trim;
                    m_out_trim     = prev_trim;
                }
            }
        }

        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            // Skip a voice that is fading out from the PREVIOUS preset: these
            // writes would retune its state mid-fade, and
            // init_modal_modes below would re-strike its modal bank at full
            // amplitude.  It gets these values on its next NoteOn anyway.
            if (v.is_active && v.fade_mul < 1.0f) continue;
            // Both noise-shaper memories: the table holds 0 for every preset.
            v.exciter.noise_lp_state = 0.0f;
            v.exciter.noise_band_mix = preset_param(static_cast<ProgramIndex>(idx), k_noise_band_mix);
            v.exciter.noise_hi_lp_state = 0.0f;
            v.exciter.noise_hi_lp_coeff = preset_param(static_cast<ProgramIndex>(idx), k_noise_hi_lp_coeff);
            v.exciter.use_hat_filter = (bool)preset_param(static_cast<ProgramIndex>(idx), k_use_hat_filter);

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
            "Cowbel", "Trngle", "BelTre", "Tick"
        };
        static_assert(sizeof(preset_names) / sizeof(preset_names[0]) == k_NumPrograms,
                      "one name per preset");
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

            case k_paramPitch: {
                // Semitone offset from the preset's root note, bounded as the SDK asks and
                // stored clamped so the host reads back what plays.
                const int32_t p = (value < -kPitchRangeSemis) ? -kPitchRangeSemis
                                : (value >  kPitchRangeSemis) ?  kPitchRangeSemis
                                : value;
                m_params[index] = p;
                m_pitch_semis   = (int8_t)p;
                update_played_note();
                break;
            }

            case k_paramPoly:
                // Global voice cap 1-4: stacking voices round-robin within the
                // first m_poly slots.
                m_poly     = (uint8_t)((value < 1) ? 1 : ((value > 4) ? 4 : value));
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

            case k_paramPartls:
                // Mode-count offset around the preset's shipped count (0-4; NoteOn reads
                // it).
                m_params[index] = (value < 0) ? 0 : ((value > 4) ? 4 : value);
                break;

            case k_paramModel:
                // Physical model: swaps the modal ratio template (NoteOn) and picks
                // the default noise band balance.
                m_model = (uint8_t)value;
                break;

            case k_paramDkay: {
                // Stored ÷10 (0-200 represents 0-2000), clamped to 0..200.  Its decay
                // mappings live in NoteOn; what is set here is the master envelope's
                // release, which AllNoteOff fades a voice with.
                const int32_t dk = (value < 0) ? 0 : ((value > 200) ? 200 : value);
                m_params[index] = dk;
                apply_release_rate(dk);
                break;
            }

            case k_paramTone: {
                state.tone = fmaxf(-10.0f, fminf(30.0f, (float)value));
                break;
            }

            case k_paramRel: {
                float norm = fmaxf(0.0f, fminf(1.0f, (float)value * 0.05f));
                // Rel sets the noise envelopes' release (0 → fast snap, 20 → slow
                // noise tail); its decay mappings live in NoteOn.
                float rel_rate = 0.00005f + ((1.0f - norm) * 0.01f);
                for (int i = 0; i < NUM_VOICES; ++i) {
                    state.voices[i].exciter.noise_env.release_rate = rel_rate;
                    // High band should decay faster than low-band body.
                    state.voices[i].exciter.noise_env_hi.release_rate = fminf(0.99f, rel_rate * 2.5f);
                }
                break;
            }

            case k_paramCutoff: {
                // Master LP "Cutoff".  Stored 1-1999; effective 10-19990 Hz
                // (×10 scaling); set_coeffs caps it at 0.47·fs (22.56 kHz).
                // High values = open (full spectrum), low values = dark.
                apply_master_filter();
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
                apply_master_filter();
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

    }

    inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
        static const char* const model_names[] = {
            "String", "Beam", "Square", "Membrn", "Plate",
            "Drumhd", "Marimba", "OpnTube", "ClsTube"
        };
        static const char* const nz_filter_names[]  = {"LP", "BP", "HP"};

        if (index == k_paramProgram) {
            // value IS the preset index being browsed — use it directly.
            return getPresetName((uint8_t)value);
        } else if (index == k_paramModel) {
            if (value >= 0 && value < 9) return model_names[value];
        } else if (index == k_paramPartls) {
            // The mode count this position gives the CURRENT preset: the knob
            // offsets the preset's own count, clamped to the bank's 2..6.
            static char pt_buf[8];
            const int shipped = modal_preset_configs[m_preset_idx].mode_count;
            if (shipped == 0) return "-";   // this preset has no modal bank
            int c = shipped + ((value < 0) ? 0 : ((value > 4) ? 4 : (int)value)) - m_modal_partls_ref;
            c = (c < 2) ? 2 : ((c > 6) ? 6 : c);
            snprintf(pt_buf, sizeof(pt_buf), "%d md", c);
            return pt_buf;
        } else if (index == k_paramNzFltr) {
            if (value >= 0 && value < 3) return nz_filter_names[value];
        } else if (index == k_paramMlltStif) {
            // Stored ÷100; show the real ×100 value (0-5000, step 100).
            static char ms_buf[8];
            snprintf(ms_buf, sizeof(ms_buf), "%d", (int)(value * 100));
            return ms_buf;
        } else if (index == k_paramDkay) {
            // Stored ÷10; show the real ×10 value (0-2000).
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
        } else if (index == k_paramCutoff) {
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
            // Stored ÷10; show the real ×10 value (710-4000).
            static char rs_buf[8];
            snprintf(rs_buf, sizeof(rs_buf), "%d", (int)(value * 10));
            return rs_buf;
        } else if (index == k_paramPoly) {
            static char pl_buf[8];
            snprintf(pl_buf, sizeof(pl_buf), "%d", (value < 1) ? 1 : ((value > 4) ? 4 : (int)value));
            return pl_buf;
        }

        // Unconditional failsafe to prevent OS screen crashes
        return "---";
    }

    // ==============================================================================
    // 4. Sequencer and MIDI Routing
    // ==============================================================================

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
    // keeps every preset and every render byte-identical.
    //   knob < 0 → ghost notes: down to x0.19 (velocity 127 lands at ~24)
    //   knob > 0 → wham:        up to x5.28, clamped at kVelWhamMax, so any
    //                           stroke above ~velocity 25 saturates to a
    //                           full-force hit no matter what the step stores.
    // Velocity is the unit's whole dynamics axis — every strike-time mapping
    // reads it — so biasing it here, once, carries through all of it.
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
        // Global Poly cap: voices round-robin within the first m_poly slots, so
        // the knob truly sets (and the display truly shows) the polyphony.
        {
            const uint8_t cap = m_poly;
            // Every strike stacks — for a sustained plate the overlap IS the
            // sound (ringing bells), so there is no roll fusion.
            state.next_voice_idx = (uint8_t)((state.next_voice_idx + 1) % cap);
        }
        VoiceState& v = state.voices[state.next_voice_idx];

        // CRITICAL FIX 2: Ensure the voice actually turns on!
        v.is_active = true;
        v.is_releasing = false;

        v.current_note = note;
        v.current_velocity = vel_norm;   // MIDI velocity x the Velocity knob
        // --- 2D DRUMHEAD STRIKE PHYSICS ---
        // The strike location, once for the whole voice.  FLOORED AT 0: hit_x is a
        // distance from the centre of the head, consumed through a magnitude
        // (sqrtsum2acc), so a negative x would fold onto its positive mirror
        // (HitPos −50 sounding like +50).  0 already is dead centre.
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
            // Knob travel is scaled into the REMAINING headroom, not added into a
            // clamp: the delta, normalised by the knob travel left on that side, moves
            // the stiffness through the headroom left on that side.  So full VlMllStf
            // travel reaches the bound at full velocity whatever MlltStif / VlMllStf
            // the preset ships, the result is monotonic, and it cannot leave
            // [0.01, 1.0] (no clamp needed).  Anchored at the shipped VlMllStf: d == 0
            // for every shipped preset, and t0 is the calibrated stiffness.  A preset
            // shipping MlltStif 50 has no upward headroom — the mallet is already as
            // stiff as the exciter allows; move MlltStif first.
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

            // Velocity depth 1.0; VlMllRes 0 (every shipped preset) makes this a no-op.
            v.exciter.noise_env.attack_rate = fmaxf(0.01f, fminf(0.99f,
                base_attack + (res_mod * v.current_velocity * 1.0f) + rim_snap_boost)); //
            // High-band burst should stay snappier than low-band burst.
            v.exciter.noise_env_hi.attack_rate = fmaxf(0.05f, fminf(0.99f,
                v.exciter.noise_env.attack_rate * 1.25f));
        }

        v.PartialReset();

        // Stir the voice's noise seed on every strike, so repeated hits do not
        // replay the same noise (kNoiseSeedStir).
        v.exciter.noise_gen.seed ^= kNoiseSeedStir;

        // Clear noise SVF delay states so rapid re-triggering doesn't produce
        // a click from residual filter memory.  set_coeffs() (called once from
        // setParameter) only updates f/q and never zeroes lp/bp/hp.
        v.exciter.noise_filter.lp = 0.0f;
        v.exciter.noise_filter.bp = 0.0f;
        v.exciter.noise_filter.hp = 0.0f;

        // Trigger the envelopes when a note hits
        v.exciter.noise_env.trigger();
        v.exciter.noise_env_hi.trigger();
        // Master envelope gate: holds at 1.0 while the gate is held — the ring
        // length belongs to the voice's own decays, and the voice retires through
        // the magnitude squelch.  Only AllNoteOff releases it (at the Dkay-set
        // release_rate).  Assigned directly rather than through trigger(): ARM
        // -ffast-math may leave an FMA a hair under the 0.99 attack threshold.
        v.exciter.master_env.sustain_level = 1.0f;
        v.exciter.master_env.value = 1.0f;
        v.exciter.master_env.state = ENV_DECAY;

        // Default noise band balance by physical model (the per-preset value below
        // wins when the preset sets one): percussion models lean on the bright
        // band, tubes on the body band.
        {
            const uint8_t model_profile = m_model;
            const bool percussion_model = (model_profile == k_SquarePlate || model_profile == k_Membrane ||
                                           model_profile == k_Plate || model_profile == k_Drumhead ||
                                           model_profile == k_MarimbaBar);
            const bool tube_model = (model_profile == k_OpenTube || model_profile == k_ClosedTube);
            if (percussion_model)  v.exciter.noise_band_mix = 0.70f;
            else if (tube_model)   v.exciter.noise_band_mix = 0.35f;
            else                   v.exciter.noise_band_mix = 0.50f;
        }
        // Per-preset noise band balance takes precedence over the model-profile
        // defaults above.  Without this, every NoteOn clobbered the calibrated
        // k_noise_band_mix from model_param_presets (Tick ships 0.86; at the
        // profile's 0.70 its dedicated hat_filter path — gated on mix > 0.80 —
        // would never open).  A value of 0 in the table means
        // "no preset opinion": the model-profile default stands.
        {
            float preset_band_mix = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_noise_band_mix);
            if (preset_band_mix > 0.001f) {
                // Clamp at the write site: process_exciter reads this per sample
                // and relies on it staying inside [0, 1].
                v.exciter.noise_band_mix = fminf(1.0f, preset_band_mix);
            }
        }
        // Triangle: the missing >8 kHz metal sheen (ref centroid 8.5 kHz vs render
        // 2.4k) — push the post-resonator high band; sustained hi noise feeds it.
        if (m_preset_idx == k_Triangle) {
            v.hf_branch_mix = 0.35f;
            v.hf_branch_decay = 0.9996f;
            v.exciter.noise_env_hi.decay_rate = 0.000080f;  // keep sheen alive ~1.8s
        }
        // Metallic rods (Triangle, BellTree, Cowbell): a decoupled post-resonator
        // high band keeps the upper shimmer alive.
        if (m_preset_idx == k_Triangle || m_preset_idx == k_BellTree ||
            m_preset_idx == k_Cowbell) {
            v.hf_branch_env = fmaxf(v.hf_branch_env, 1.0f);
            v.hf_branch_decay = fmaxf(v.hf_branch_decay, 0.9992f);
            v.hf_branch_mix = fmaxf(v.hf_branch_mix, 0.22f);
        }
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
          v.metal_fm_inc = (M_TWOPI * base_fm_hz) * inverse_default_sample_rate;
          v.metal_fm_env = 1.0f;
          v.metal_fm_decay = 0.9978f;
          v.metal_fm_depth = 0.16f;
        }
        // Modal bank: re-initialised on every NoteOn so the mode frequencies track
        // the played note and the envelopes the velocity (LoadPreset's own init
        // runs at velocity 0).
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
                {
                    const uint8_t model_now = (m_model < k_lastModel) ? m_model : (uint8_t)k_String;
                    // (1) Model → modal ratio set.  Moving the Model knob off the
                    // shipped value swaps the calibrated ratios for the selected
                    // physical model's template (harmonic string, free bar,
                    // Bessel membrane, thick plate, ...).
                    if (model_now != m_modal_model_ref) {
                        const float* tr = kModelModalRatios[model_now];
                        r2 = tr[0]; r3 = tr[1]; r4 = tr[2]; r5 = tr[3]; r6 = tr[4];
                    }
                    // (2) Partls → mode count AND overtone richness, offset around
                    // the shipped count.  The count change alone was inaudible (added modes
                    // are high/quiet), so ALSO scale the upper-mode envelopes:
                    // up = richer/brighter overtones, down = fewer/purer.
                    {
                        int pv = m_params[k_paramPartls];
                        if (pv >= 0 && pv <= 4 && pv != m_modal_partls_ref) {
                            int d = pv - m_modal_partls_ref;
                            int c = (int)count + d;
                            count = (uint8_t)((c < 2) ? 2 : ((c > 6) ? 6 : c));
                            // 1.0 per step, tilting mode 2 as well, so each Partls click is a clear
                            // richness/brightness change.
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
                        // Exponential, not linear: 2^(1.75·d) spans 0.30…3.36 over the bipolar
                        // range and cannot reach zero (a linear spread hits its floor partway down
                        // and collapses every partial onto the fundamental).  knob_exp2(0) is
                        // exactly 1.0, so the anchor holds.
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
                        // Coefficient 3.4: metal sustains its overtones, wood damps them — a full
                        // timbre sweep across the knob.
                        float mat = knob_exp2(3.4f * (mn - m_modal_mterl_ref));
                        if (mat < 0.999f || mat > 1.001f) {
                            mat = fmaxf(0.08f, fminf(9.0f, mat));
                            t2 *= mat;
                            t3 *= mat;
                            t4 *= mat;  // modes 5/6 derive their decay from t4
                            // ALSO tilt the upper modes' INITIAL energy (gentler, mat^0.5): damping only
                            // the T60 is tail-only and inaudible on fundamental-dominated drums; metal =
                            // brighter onset too, wood = duller onset.
                            float matenv = fmaxf(0.25f, fminf(3.2f, sqrtf(mat)));
                            // Also tilt mode 2 (gentler) so Mterl is audible on
                            // drums whose energy sits in the low modes, where the
                            // 3-6 tilt alone reads as weak.
                            e2 *= fmaxf(0.5f, fminf(2.2f, matenv));
                            e3 *= matenv; e4 *= matenv; e5 *= matenv; e6 *= matenv;
                        }
                    }
                    // (5) TubRad → body size.  A wider body rings ALL its modes longer (a
                    // mode-1-only change is inaudible on fundamental-dominated instruments).
                    // Anchored at the shipped TubRad.
                    {
                        float tn = fmaxf(0.0f, fminf(20.0f, (float)m_params[k_paramTubRad])) * 0.05f;
                        float tub = knob_exp2(1.9f * (tn - m_modal_tubrad_ref));
                        if (tub < 0.999f || tub > 1.001f) {
                            tub = fmaxf(0.25f, fminf(3.5f, tub));
                            t1 *= tub; t2 *= tub; t3 *= tub; t4 *= tub;  // whole shell
                        }
                    }
                }
                v.init_modal_modes(r2, r3, r4,
                                   t1, t2, t3, t4,
                                   modal_mix_val, e1, e2, e3, e4,
                                   count, r5, r6,
                                   e5, e6);
                // Dkay → modal T60 trim.  The calibrated modal_preset_configs T60 plays at
                // the preset's shipped Dkay (m_modal_dkay_ref) and the knob trims relative
                // to it, with Rel as a finer trim on top; applied as powf(decay, 1/scale)
                // (scale < 1 → faster decay).  knob_exp2 returns exactly 1.0 at the anchor.
                {
                    {
                        float dkay_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
                        // Rel → modal ring-length trim (anchored): on modal engines Rel
                        // otherwise only gated the noise tail and felt dead.  Folded into
                        // t60_scale alongside Dkay (Dkay = coarse decay, Rel = ±~1 oct trim).
                        float rel_norm = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel] * 0.05f));
                        // Dkay 4.5 (~±4.5 octaves of ring), Rel 3.2 of extra trim on top.
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

                        // Mallet stiffness → modal brightness tilt: a stiffer mallet boosts the
                        // higher modes, a softer one cuts them (mode 1 is never tilted).
                        // mallet_stiffness already folds in the velocity (VlMllStf) and rim-position
                        // brightness, which are part of the calibrated sound, so that part keeps
                        // ×1.4 and only the KNOB's deviation from the shipped value is widened
                        // (×3.4) — exactly 0 at the shipped knob value.
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
                        {
                            float mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
                            // Coefficient 3.4: MlltRes is a full-range modal presence/level control.
                            v.modal_mix *= fmaxf(0.05f, fminf(9.0f, knob_exp2(3.4f * (mr - m_modal_mltres_ref))));
                        }

                        // HitPos → strike-position excitation: toward the rim couples energy into
                        // the higher modes and away from the fundamental, dead centre the opposite.
                        // Anchored at the shipped HitPos, and it follows the knob below centre too
                        // (bipolar), where it weights mode 1 up and the upper modes down.
                        float hit_off = fmaxf(-1.0f, fminf(1.0f, (float)m_params[k_paramHitPos] * 0.01f))
                                      - m_modal_hitpos_ref;
                        if (hit_off < -0.001f || hit_off > 0.001f) {
                            // Known limit: these six are linear-into-a-clamp, so the far negative
                            // corner of the bipolar range saturates on presets that ship HitPos high.
                            // Reshaping them (knob_exp2) means re-deriving the coefficients against a
                            // listen.
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

    }

    inline void NoteOff(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];

            // Find the voice playing this note that hasn't already been released
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;

                v.exciter.noise_env.release();
                v.exciter.noise_env_hi.release();
                // master_env holds at 1.0: the ring is the voice's own decay.
            }
        }
    }

    inline void GateOff() {
        // Release the note GateOn STRUCK, not whatever root + Pitch is now.
        // Pitch (or a Program change, which moves the root) can move between
        // a held gate's on and off — a motion-sequenced Pitch, a long gate
        // time — and NoteOff(m_ui_note) then matched nothing: the struck voice
        // was never released and rang out its whole tail.  The drumlogue's own
        // same-tick gate cannot move it, so every same-tick render is
        // unchanged.
        NoteOff(m_gate_note);

        // Voice allocation for repeated triggers is decided in NoteOn, where the
        // incoming note is known: round-robin within Poly.  GateOff only sees
        // the note being released, so it does not try.
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
        // Pitch bend: nothing on this engine reads it (each voice is tuned once,
        // at its strike).
        (void)bend;
    }

    // ==============================================================================
    // 5. The Core Physics (Executed per-voice, per-sample)
    // ==============================================================================

    // Processes the Exciter (Generates the initial "strike" burst).
    // PCM sample playback removed (see NoteOn) — pure synthesis only.
    inline float process_exciter(ExciterState& ex) {
        float out = 0.0f;

        // Noise: stored in noise_out_sample, which processBlock mixes in
        // PARALLEL with the resonator output — broadband texture stays
        // separate from the pitched ring.
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
        // true crossfade — NzMix=0 → full mallet, NzMix=100 → silent mallet
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

    inline void processBlock(float* __restrict main_out, size_t frames) {

        // Clear the output buffer — mandatory; without this the += accumulation
        // can corrupt with stale or NaN data from the previous block.
        for (size_t i = 0; i < frames * 2; ++i)
            main_out[i] = 0.0f;

        // Hoist tone read outside all loops — avoids UI/audio-thread race.
        const float tone_val = state.tone;
        // Every preset ships Tone=0: skip the tilt-EQ one-pole entirely then
        // (its output path is already a no-op at 0; this also skips the LP
        // state update).  The LP re-converges within ~10 samples when the
        // knob moves off zero — inaudible under the knob's own change.
        const bool tone_active = (tone_val != 0.0f);

        // Release a master drive deferred behind a preset-change fade as soon
        // as the last fading voice has retired.
        if (m_pending_drive >= 0.0f || m_pending_trim >= 0.0f) {
            bool fading = false;
            for (int i = 0; i < NUM_VOICES; ++i)
                if (state.voices[i].is_active && state.voices[i].fade_mul < 1.0f) fading = true;
            if (!fading) {
                if (m_pending_drive >= 0.0f) { state.master_drive = m_pending_drive; m_pending_drive = -1.0f; }
                if (m_pending_trim  >= 0.0f) { m_out_trim = m_pending_trim; m_pending_trim = -1.0f; }
            }
        }

        // ── The voice bus is MONO; only main_out[i*2] carries it ──────────────
        // Every voice is single-channel, and Stage 4b filters the LEFT sample
        // and writes the result to BOTH channels, so the voice loops write the
        // left lane only (a stereo store would cost a load-add-store per sample
        // per voice in the hottest loop of the unit).  The idle path below
        // returns on an all-zero buffer.
        bool any_voice_rendered = false;
        for (int voice_idx = 0; voice_idx < NUM_VOICES; ++voice_idx) {
            VoiceState& voice = state.voices[voice_idx];
            if (!voice.is_active) continue;

            any_voice_rendered = true;

            // Hoisted per-preset parallel-noise gain (constant within a block).
            float base_parallel_noise_gain = 5.0f;
            if (m_preset_idx == k_Triangle) {
                base_parallel_noise_gain = 7.0f;
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
                float voice_out   = exciter_sig * voice.current_velocity;

                // Parallel noise path: the noise mixes straight into the voice output,
                // keeping the broadband character a resonator would pitch-filter away.
                // The ×5 base factor brings it into the ballpark of the ×15 mallet's
                // output; per-preset overrides are folded into base_parallel_noise_gain
                // above.
                float parallel_noise_gain = base_parallel_noise_gain;
                // Ring-coupled noise gate for ENGINE_PLATE: noise tracks the modal ring
                // decay so both die together — user reported noise and ring as "juxtaposed".
                // noise_ring_gate starts at 1.0 on NoteOn and decays with modal_decay_1.
                // Floor of 0.15 keeps a small amount of noise-texture even in long tails.
                if (voice.modal_pilot_enabled) {
                    parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
                    voice.noise_ring_gate *= voice.modal_decay_1;
                }
                voice_out += voice.exciter.noise_out_sample * parallel_noise_gain * voice.current_velocity;
                // Structural high-band branch: a simple high-pass (x − LP(x)) over the
                // exciter noise, mixed post-resonator for a sustained metallic shimmer.
                if (voice.hf_branch_mix > 0.0f && voice.hf_branch_env > silence_threshold) {
                    voice.hf_branch_lp += 0.12f * (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    float hf = (voice.exciter.noise_out_sample - voice.hf_branch_lp);
                    float hf_out = hf * voice.hf_branch_env * voice.hf_branch_mix * 8.0f;
                    voice_out += hf_out * voice.current_velocity;
                    voice.hf_branch_env *= voice.hf_branch_decay;
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
                    // The modal bank is this engine's tonal resonator: x5 is the gain it was
                    // voiced at.
                    const float modal_engine_gain = 5.0f;
                    float modal_sum = m1
                                    + (stage2_modal_amp_ratio_2 * m2)
                                    + (0.45f * m3)
                                    + (0.28f * m4)
                                    + (0.18f * m5)
                                    + (0.12f * m6);
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
                    // Zombie-voice cleanup: reclaim the slot when the ring and
                    // the exciter are both silent, even with the gate held.
                    if (!voice.is_releasing &&
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

                // ── Stage 1/2: voice lifetime management ───────────────────
                // Without Stage 3 squelch, voices stay is_active=true forever.
                // Deactivate once the mallet has fully decayed, the noise
                // envelopes are idle, AND the voice is actually silent.
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

        // NOTE: there is deliberately NO hard clip of the voice bus here: Stage 4b
        // always runs and bounds the output to kMasterLimCeil by construction, and a
        // clamp in front of it would flatten the loudest presets and hide any layer
        // added underneath them.
        // ── Stage 4b: Master FX (filter + drive + peak limiter) ───────────
        // A transparent limiter: unity below kMasterLimThr, a soft knee above,
        // asymptotic to the ceiling.  Both channels carry the same signal, so limit
        // once and write it twice.
        // ── Why a gain ENVELOPE and not a waveshaper ───────────────────────
        // A curve applied to every sample changes the gain within a cycle and
        // manufactures harmonics on low tones; softening the knee cannot help when
        // the presets drive the stage hard.  So the peak is tracked by an
        // instant-attack, slow-release follower and ONE gain is applied per cycle:
        // master_lim_env only jumps up and decays down, so it is >= |x| at every
        // sample and out = x · limit(env)/env stays bounded by kMasterLimCeil BY
        // CONSTRUCTION (the brickwall is pure safety), while the waveform inside a
        // cycle is preserved.
        // One master-stage gain, hoisted: the Gain knob's drive times the preset's
        // output trim (neither changes inside a block).
        const float stage_gain = state.master_drive * m_out_trim;
        for (size_t i = 0; i < frames; ++i) {
            float x = state.master_filter.process(main_out[i * 2]);
            x *= stage_gain;
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
                // clamping it just relocates the brickwall: it pins thousands of
                // samples dead flat, where this curve pins none.
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
            // Route the internal Drumlogue sequencer to the preset's root note
            // plus the Pitch knob.  External MIDI (unit_note_on) bypasses this
            // and plays the note it carries, as it always has.
            m_gate_note = m_ui_note;   // what GateOff must release
            NoteOn(m_ui_note, velocity);
        }
    }

// ==============================================================================
// PRIVATE METHODS
// ==============================================================================
private:

// ==============================================================================
// PRIVATE VARIABLES
// ==============================================================================
private:
    float m_master_cutoff = 16000.0f; // Master LP cutoff — default fully open

    // The note the sequencer path plays (GateOn / GateOff) = the preset's ROOT
    // note (its Note column, captured by LoadPreset) + the Pitch knob.  Kept
    // derived rather than stored on its own so neither input can go stale
    // against it: LoadPreset moves the root, setParameter moves the offset,
    // and both re-derive through update_played_note().
    uint8_t m_root_note   = 36;   // set by LoadPreset (Init loads preset 0)
    int8_t  m_pitch_semis = 0;    // Pitch knob, ±kPitchRangeSemis
    uint8_t m_ui_note     = 36;
    uint8_t m_gate_note   = 36;   // the note the last GateOn struck

    inline void update_played_note() {
        const int n = (int)m_root_note + (int)m_pitch_semis;
        m_ui_note = (uint8_t)((n < 0) ? 0 : ((n > 127) ? 127 : n));
    }

    // Master lowpass from the Cutoff and Resnc knobs.  One place for both
    // setParameter cases, so they cannot drift.
    // Bit-identical to the two case bodies it replaces for every in-range
    // value; out-of-range values are bounded as the SDK asks.
    inline void apply_master_filter() {
        const int32_t lc = (m_params[k_paramCutoff] < 1) ? 1
                         : (m_params[k_paramCutoff] > 1999) ? 1999
                         : m_params[k_paramCutoff];
        m_master_cutoff = (float)lc * 10.0f;
        const float res_val =
            fmaxf(0.707f, 0.707f + ((float)m_params[k_paramResnc] - 71.0f) * 0.01f);
        state.master_filter.set_coeffs(m_master_cutoff, res_val, default_sample_rate);
    }

    // Master-envelope release from a Dkay value (0..200): 50 ms .. 10 s.  It
    // only ever runs after AllNoteOff — the gate holds the envelope at 1.0.
    inline void apply_release_rate(int32_t dk) {
        float norm = fmaxf(0.0f, fminf(1.0f, (float)dk * 0.005f));
        float t_s = fasterpow2f(k_log_2_of_200 * norm); // 50ms..10s
        float master_rate = M_THREELN10 * 20 * inverse_default_sample_rate / t_s;
        for (int i = 0; i < NUM_VOICES; ++i)
            state.voices[i].exciter.master_env.release_rate = master_rate;
    }

    // Fresh-object values for state a knob owns once a preset is loaded (the
    // noise filter: NzFltr / NzFltFrq).  Only Init() calls this, so Init() on a
    // USED object starts where a fresh one does.
    inline void init_fresh_voice_defaults() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            v.exciter.noise_filter.mode = 0;   // LP, fully open (12 kHz)
            v.exciter.noise_filter.set_coeffs(12000.0f, 0.707f, default_sample_rate);
        }
    }

    // The knobs LoadPreset does NOT write — performance and master controls the
    // host owns — back to their header.c defaults.  Called once from Init().
    // MUST track header.c's `.init` fields for these slots (C cannot assert
    // across the two files).
    inline void seed_performance_params() {
        static const int16_t kDefaults[][2] = {
            { k_paramPitch,    0    },
            { k_paramPoly,     4    },
            { k_paramVelocity, 0    },
            { k_paramVlMllRes, 0    },
            { k_paramTone,     0    },
            { k_paramCutoff,   1999 },
            { k_paramResnc,    71   },
        };
        for (const auto& d : kDefaults) setParameter((uint8_t)d[0], d[1]);
    }
    // Idle-CPU guard: blocks of master-chain flush left to run after the last
    // active voice died.  While > 0 the pre-clip + master FX loops keep running
    // on the (all-zero) buffer so the master SVF settles; at 0 processBlock
    // returns right after the buffer clear — the drumlogue calls every unit's
    // render continuously, so a silent unit otherwise burns SVF + soft-clip
    // per sample forever.
    uint16_t m_idle_flush_blocks = 0;
    // Master drive queued behind a preset-change fade (-1 = nothing pending).
    float m_pending_drive = -1.0f;
    // Per-preset output trim (kPresetOutTrim) and its preset-change deferral,
    // which mirrors m_pending_drive one-for-one: the trim is a master-stage
    // gain like the drive is, so letting the incoming preset's trim hit the
    // outgoing preset's fading tail would step that tail by up to 22 dB.
    float m_out_trim = 1.0f;
    float m_pending_trim = -1.0f;
    uint8_t m_poly = 4;            // global voice cap (1-4, Poly knob)
    float   m_vel_bias = 0.0f;      // Velocity knob, -1 (ghost) .. +1 (wham);
                                    // 0 = neutral, strikes play as sent
    uint8_t m_model = k_String;     // Model knob (physical model template)
};

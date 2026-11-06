#pragma once
// Brachetti Cymbal — synth engine of the brachetti_cymbal unit.
//
// One of the per-family units split out of the 40-preset `brachetti` unit
// (platform/drumlogue/brachetti, which stays as it is).  This unit holds the
// dense-resonator metal: Cymbal (crash), Gong, HHat-O, Ride, RidBel and Splash.
// In the original these six never used the shared voice engine — ENGINE_CYMBAL
// bypasses the exciter, waveguide, modal bank and noise path for a self-
// contained Stowell-style resonator cymbal (CymbalVoice, dsp_core.h, ported
// from cymbal_synthesis/) — so that is all this unit contains: CymbalVoice,
// the cymbal voice-allocation policy and CPU budget, the knob → cymbal mapping
// and the master stage.  At the knob values the presets ship with, every
// preset renders bit-identically to the original; tools/brachetti_equiv
// proves it.
//
// Removed knobs, all measured inert on all six presets in the original
// (tools/brachetti_equiv/dead_params.py): Model, NzMix, NzRes, NzFltr and
// NzFltFrq — the cymbal has its own noise drivers, configured per preset.
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

// 2^x for the REFERENCE-ANCHORED knob curves (NoteOn / LoadPreset).
// Their precision is not critical — ±0.3% on a knob's
// response curve is inaudible — so fasterpow2f is fine, EXCEPT at the anchor:
// fasterpow2f(0) ≈ 0.9614, NOT 1.0, which would silently break the
// byte-identical guarantee for every shipped preset (the anchor delta is
// exactly 0 there and the factor must be exactly 1).  Guard the anchor.
// NEVER use this for tuning or decay-coefficient math — see the CLAUDE.md
// fasterexpf / fastercosfullf gotchas (exact expf/cosf/exp2f there).
inline float knob_exp2(float x) {
    return (x == 0.0f) ? 1.0f : fasterpow2f(x);
}

// Compile-time check on one column of LoadPreset's preset table: every row's
// value lies in [lo, hi].  Used to pin the columns LoadPreset does NOT load
// (pass 48), so a future edit that gives one preset its own Tone or Cutoff —
// which would be silently ignored — fails the build instead.
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
static constexpr float    kToneLpMix = 0.3f;
static constexpr float    kInvToneCutDivisor = 0.1f;         // 1 / 10
static constexpr float    kInvToneBoostDivisor = 0.13333334f; // 1 / 7.5 (widened ×2: every preset ships Tone=0, so no regression)
static constexpr float    zeroThreshold = 0.0f;
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
        k_paramDkay,         // 9
        k_paramMterl,        // 10
        k_paramTone,         // 11
        k_paramHitPos,       // 12
        k_paramRel,          // 13
        k_paramInharm,       // 14
        k_paramCutoff,       // 15
        k_paramTubRad,       // 16
        k_paramGain,         // 17
        k_paramResnc,        // 18
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of every preset.
    static_assert(k_lastParamIndex == 19, "header.c declares 19 params");
    // Pitch knob reach, semitones either side of the preset's root note.
    // Must equal the min/max header.c declares for "Pitch".
    static constexpr int kPitchRangeSemis = 24;
    enum ProgramIndex {
        k_Cymbal = 0,       // 0  -sample: cymbal-Crash16Inch.wav (starts as 2000Hz goes down to 650Hz and settles to 1000Hz - a lot of oscillations)
        k_Gong,             // 1  -sample: Chinese-Gong.wav, Gong-long-G#.wav (starts with 800Hz and settles to 1680Hz +/- 10Hz)
        k_HiHatOpen,        // 2  -sample: TightClosedHat.wav (11635Hz +/- 100Hz)
        k_Ride,             // 3  -sample: cymbal-Ride18Inch.wav (start over 2000Hz and settles to 761Hz)
        k_RideBell,         // 4  -sample: cymbal-RideBell20InchSabian.wav (starts over 2000Hz, settles to 867Hz)
        k_Splash,           // 5  — small pitched splash cymbal (ENGINE_CYMBAL, splash anchor table)
        k_NumPrograms       // 6 — marker (count)
    };

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

// Preset → output trim.  The one number that gain-stages a preset into the
// master limiter; see "Pass 47" in CLAUDE.md and calib_probe.cpp, which
// generates this table.
//
// WHY IT EXISTS.  Stage 4b is a peak limiter with a 0.99 ceiling, and before
// this table the voice bus reached it between 0.5x and 116x full scale — a
// 47 dB spread across the library, with a MEASURED single-preset worst case of
// 13.4x for the BODY of a note (Cowbell), not for its strike.  Nothing
// downstream can be transparent across that, and what the limiter actually did
// with it was not limiting: with 20-40 dB of gain reduction standing on the
// whole note it held the output AT the ceiling for hundreds of milliseconds,
// so (measured, 10 ms frames, Marimba at its own note and velocity 127) the
// output envelope read 0, -8, -6, -4, -2, 0, 0, 0 ... dB — a duck into the
// strike and then a swell back to the wall that stayed there for half a
// second, where the voice itself decays -14, -14, -15, -16, -18, -24, -42 dB.
// The instrument's decay never happened, every new strike ducked whatever was
// still ringing, and stacked notes (which is where HW heard it) drove the
// residual that no smooth gain can explain to within 5-19 dB of the signal.
//
// HOW IT IS CALIBRATED.  On the note's BODY, not its strike: the peak over
// [10 ms, 1 s] at velocity 127 on the preset's own Note, put onto the BODY
// TARGET — near the limiter's kMasterLimThr.  A mallet transient limited by
// 15 dB for 2 ms is what percussion mastering does and is inaudible; a body
// held 20 dB down for 500 ms is the defect.  The strike keeps the headroom
// above the threshold and the limiter goes back to catching peaks.
//
// THE BODY TARGET IS THE CLIPPING/LOUDNESS DIAL, and it is the probe's only
// argument — `/tmp/calib_probe 1.5` regenerates this whole table from it, so
// re-trading the two is one command and a paste.  Measured over the library:
//
//     target      mean LUFS   stacked-note distortion   presets left at 1.0
//     1.0          -17.7            -22.3 dB                    8
//     1.5 (ships)  -16.1            -20.6 dB                   12
//     no trim      -13.3            -13.7 dB                   40
//
// This table is at 1.5, set on HW request to give a measured part of the
// clipping back for level.  Kick and DeepBs are COUPLED to the target — their
// decays were asked for by ear (1590 / 495 ms) and the limiter's grip on the
// tail moves the measured value — so changing it means re-converging those two
// against decay_probe.  See CLAUDE.md pass 47.
//
// It is clamped to <= 1 by construction: this only ever gives back drive the
// master stage could not use, so raising the target never pushes a preset past
// where it already sat.  At 1.5 twelve presets keep exactly 1.0 (Timpani and
// Taiko, which run the kernel's own master stage, plus Cymbal, Claves, Clap,
// HHat-O, Ride, RidBel, Tick, Splash and Wodblk) and render bit-identically.
//
// NOTE: must be 'static' only (no const/constexpr) — same .rodata rule as the
// tables above.
float kPresetOutTrim[k_NumPrograms] = {
    /* 0  Cymbal  */ 1.00000f,
    /* 1  Gong    */ 0.36908f,
    /* 2  HHat-O  */ 1.00000f,
    /* 3  Ride    */ 1.00000f,
    /* 4  RidBel  */ 1.00000f,
    /* 5  Splash  */ 1.00000f};

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

    // RUNTIME state only — never a parameter's value (the SDK: unit_reset()
    // must not reset parameter values).  T49 in the original.
    inline void Reset() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].is_active = false;
            // Clears the voice's fade, tilt-EQ memory and cymbal activity.
            state.voices[i].PartialReset();
        }

        // 1.5 -> 2.3: as loud as the bus can take before the master limiter's
        // dynamic-range guarantee (a ghosted hit must stay under 60% of a
        // neutral one) starts to erode.
        state.master_gain  = 2.3f;
        // The drive is the Gain knob's and stays.  What Reset() does own is a
        // drive and trim DEFERRED behind a preset-change fade: the fade waits
        // for the outgoing voices, and Reset() has just killed them, so
        // complete it now — exactly what processBlock would do as the last
        // fade retired.
        if (m_pending_drive >= 0.0f) state.master_drive = m_pending_drive;
        if (m_pending_trim  >= 0.0f) m_out_trim         = m_pending_trim;
        m_pending_drive    = -1.0f;
        m_pending_trim     = -1.0f;
        // Master-stage state dies with the voices too.
        state.master_lim_env = 0.0f;
        m_idle_flush_blocks  = 0;

        // Master filter is a LOWPASS "Cutoff" (mode 0); its coefficients are the
        // Cutoff/Resnc knobs and stay.  Only the filter's memory is cleared.
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
    // captured in LoadPreset.  Every cymbal knob mapping in NoteOn is a delta
    // from these, so the shipped preset plays its calibrated sound and only
    // knob MOVEMENT alters it.
    float   m_modal_dkay_ref   = 0.975f;
    float   m_modal_rel_ref    = 0.9f;
    float   m_modal_stiff_ref  = 0.26f;
    float   m_modal_inharm_ref = 0.0f;
    float   m_modal_mterl_ref  = 0.5f;
    float   m_modal_hitpos_ref = 0.26f;
    float   m_modal_tubrad_ref = 0.25f;
    float   m_modal_mltres_ref = 0.5f;
    float   m_snare_vlstf_ref  = 0.0f;

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

        // One row per preset, one column per knob in ParamIndex order (the knob
        // values the preset ships with).
        // The Pitch column holds the preset's ROOT NOTE (MIDI), not a knob
        // value: the Pitch knob is an offset in semitones FROM it, so the
        // column is captured into m_root_note and never written to the knob.
        // Columns LoadPreset does NOT load are knobs the host owns (pass 48):
        // Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc.
        // Their values below are never read (VlMllRes's is, once, as an anchor).
        // int16_t: the stored range is [-10, 1999].
        static constexpr int16_t presets[k_NumPrograms][k_lastParamIndex] = {
            //  Prg Root  Ply  Vel MlRs MlSt VlRs VlSt Ptls  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain Rsnc
            {   0,  65,   0,   1, 800,  45,   0,   0,   3, 200,  28,   0,   0,  18,   1,1999,  15,   5,  71},   // 0: Cymbal    — noise⇄ring cross-modulation (modal_rm_depth 0.70)
            {   1,  50,   0,   1, 200,   2,   0,   0,   3, 190,   1,   0,   0,  20,   1,1999,  20,  20,  71},   // 1: Gong      — NzMx 19→26 + FM depth 0.18 for more crash onset; rm_depth 0.60
            {   2,  79,   0,   1, 900,  49,   0,   0,   3, 200,  18,   0,   0,  18,   1,1999,  12,   0,  71},   // 2: HHat-O    — ring and noise now cross-modulated (rm_depth 0.50).  Dkay 210 -> 200 (pass 48): the range is 0..200 and every consumer already read 210 as 200
            {   3,  69,   0,   1, 900,  50,   0,   0,   3, 190,  28,   0,   0,  18,   1,1999,  17,   0,  71},   // 3: Ride      — thick-plate modal ratios replace near-harmonic set (was 'a string sound')
            {   4,  60,   0,   1, 900,  49,   0,   0,   3, 200,  16,   0,   0,   8,   1,1999,   1,   0,  71},   // 4: RidBel    — bell-partial ratios 1:2:3.01:4.7
            {   5,  76,   0,   1, 800,  45,   0,   0,   3, 200,  28,   0,   0,  18,   1,1999,  15,   5,  71}    // 5: Splash    — small pitched splash (ENGINE_CYMBAL)
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

        static_assert(preset_column_in(presets, k_paramPartls, 0, 7),
                      "Partls (the bank density) is 0..7");

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
        m_modal_inharm_ref = fmaxf(-1.0f, fminf(1.0f, (float)presets[idx][k_paramInharm] * 0.01f));
        m_modal_mterl_ref  = (fmaxf(-10.0f, fminf(30.0f, (float)presets[idx][k_paramMterl])) + 10.0f) * 0.025f;
        m_modal_hitpos_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramHitPos] * 0.01f));
        m_modal_tubrad_ref = fmaxf(0.0f, fminf(20.0f, (float)presets[idx][k_paramTubRad])) * 0.05f;
        m_modal_mltres_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramMlltRes] * 0.001f));
        m_modal_rel_ref    = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramRel] * 0.05f));
        m_snare_vlstf_ref  = (float)presets[idx][k_paramVlMllStf] * 0.01f;

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
        // preset now loading.  (Reset() has no such loop — it completes a
        // pending deferral itself.)
        //
        // The output trim is deferred on exactly the same terms — it is the
        // other half of the master-stage gain, and it moves further than the
        // drive does (up to 22 dB between two presets), so a fading tail that
        // took the incoming trim would step audibly.
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
    }

    static inline const char * getPresetName(uint8_t idx) {
        static const char* const preset_names[] = {
            "Cymbal", "Gong", "HHat-O", "Ride", "RidBel", "Splash"
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
                // Semitone offset from the preset's root note (the note
                // transposes the whole anchor spectrum — a smaller or larger
                // cymbal).  Bounded as the SDK asks, stored clamped.
                const int32_t p = (value < -kPitchRangeSemis) ? -kPitchRangeSemis
                                : (value >  kPitchRangeSemis) ?  kPitchRangeSemis
                                : value;
                m_params[index] = p;
                m_pitch_semis   = (int8_t)p;
                update_played_note();
                break;
            }

            case k_paramPoly:
                // Global voice cap 1-4.  The CPU is bounded on top of it by
                // kCymCostBudget, which in practice affords two simultaneous
                // cymbal voices (see the voice policy in NoteOn).
                m_poly = (uint8_t)((value < 1) ? 1 : ((value > 4) ? 4 : value));
                break;

            case k_paramVelocity:
                // Global performance control: bias every incoming strike (see
                // vel_bias_apply).  0 is neutral.
                m_vel_bias = fmaxf(-1.0f, fminf(1.0f, (float)value * 0.01f));
                break;

            case k_paramPartls:
                // Resonator-bank DENSITY: 25 + 5 x Partls % of each preset's bank
                // (the ex-"Rsntrs" control).  Shipped rows store 3 = 40 %.
                if (value >= 0 && value <= 7) m_cym_reso_scale = (uint8_t)(25 + 5 * value);
                break;

            case k_paramDkay: {
                // Stored ÷10 (0-200 represents 0-2000), CLAMPED to 0..200.
                const int32_t dk = (value < 0) ? 0 : ((value > 200) ? 200 : value);
                m_params[index] = dk;
                break;
            }

            case k_paramTone:
                state.tone = fmaxf(-10.0f, fminf(30.0f, (float)value));
                break;

            case k_paramCutoff:
            case k_paramResnc:
                // Master LP "Cutoff" (stored 1-1999 = 10-19990 Hz, high = open)
                // and its Q (stored ÷10, 71 = Q 0.707).
                apply_master_filter();
                break;

            case k_paramGain: {
                float norm = fmaxf(0.0f, (float)value * 0.01f);
                state.master_drive = 1.0f + (norm * 20.0f);
                m_pending_drive = -1.0f;   // an explicit Gain turn wins
                break;
            }

            default:
                // MlltRes, MlltStif, VlMllRes, VlMllStf, Mterl, HitPos, Rel,
                // Inharm, TubRad: read from m_params at the next strike.
                break;
        }
    }

    inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
        if (index == k_paramProgram) {
            // value IS the preset index being browsed — use it directly.
            return getPresetName((uint8_t)value);
        } else if (index == k_paramPartls) {
            // The resonator-bank density this position gives.
            static char cr_buf[8];
            int d = (value < 0) ? 0 : ((value > 7) ? 7 : (int)value);
            snprintf(cr_buf, sizeof(cr_buf), "Rs%d%%", 25 + 5 * d);
            return cr_buf;
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
        } else if (index == k_paramCutoff) {
            static char lc_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                snprintf(lc_buf, sizeof(lc_buf), "%d.%dkHz", (int)(hz / 1000), (int)((hz % 1000) / 100));
            } else {
                snprintf(lc_buf, sizeof(lc_buf), "%dHz", (int)hz);
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

    // The ceiling was the pre-pass-26 worst case — 2 voices at the largest bank
    // the Rsntrs knob could ask for (60 lanes) = 368 — because that was the
    // only cymbal CPU level this unit had field evidence for: it ran for 25
    // passes without an audio crash, and the first report of one came after
    // pass 26 doubled it.
    //
    // 440 (pass 46), for two reasons that are both about the case the old
    // number was sized against no longer existing:
    //   1. Repeated strikes used to cost TWO voices, ping-ponging between
    //      slots.  Since pass 45 a re-strike on the same note re-excites its
    //      own voice, so the gesture the budget was really protecting against
    //      now costs ONE voice — measured 29.0 -> 14.1 µs/block.
    //   2. The gong's bank went back to the density it was ported from (32 ->
    //      96 lanes), and at 368 that pushed a second DIFFERENTLY-PITCHED gong
    //      out of the budget entirely: 220 + 124 + 32 = 376 > 368, so distinct
    //      notes collapsed onto one voice.  That is the defect this whole
    //      branch started from, reintroduced through the back door.
    //
    // The raise is cheap because the bank is nearly free and the VOICE is what
    // costs: measured on the real processBlock, taking the gong from 32 to 96
    // lanes moves one voice 13,990 -> 14,345 ns/block (+2.5 %), and a second
    // full voice adds ~2,000 ns.  Two 96-lane gong voices measure ~16.4
    // µs/block against pass 30's 49.7 µs last-known-good level and 95.6 µs
    // crash level.  The 2-voice ceiling is unchanged for every preset in the
    // family — a third voice is still denied (gong 440 + 124 + 32 = 596 > 440;
    // crash at max density 360 + 124 + 32 = 516 > 440).
    static constexpr int kCymCostBudget = 440;

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
        //
        // HW AGAIN, after both of those: "multiple hits still do not stack
        // correctly."  They did not, and the ledger says why — 8 gong strikes
        // 300 ms apart allocate slot 0, slot 1, then 0, 1, 0, 1, 0, 1.  Two
        // voices at the default density cost 2 x (124 + 32) = 312 of the 368
        // lane budget, so the third strike can never afford a slot and steals;
        // with every voice inside the 0.6 s protect window it steals the
        // OLDEST, which is the one two strikes back.  Every strike from the
        // third on therefore lands on top of a ring, alternating, for ever.
        //
        // The missing rule is the one ENGINE_KS already has for a re-plucked
        // string, and it is the same physics: a gong is ONE piece of metal.
        // Striking it again does not produce a second gong, it puts more
        // energy into the one that is already ringing — which is precisely
        // what cymbal_note_on's retainRing path now does properly (the ring,
        // the driver envelopes and the noise bed all carry across the strike).
        // So a re-strike ON THE SAME NOTE re-excites its own voice, and only a
        // DIFFERENT note — a different-sized instrument — asks for a slot of
        // its own.  Repeated strikes then cost ONE voice instead of two, which
        // is also why the budget stops binding on exactly the gesture that
        // used to break it.
        {
            const int cap = (int)m_poly;
            const uint32_t protect =
                (uint32_t)(kCymStealProtectSec * default_sample_rate);
            int active = 0, freeIdx = -1, faded = -1, oldest = -1, same = -1;
            int busy_cost = 0;
            float wmag = 1e9f;
            uint32_t oldage = 0u;
            for (int i = 0; i < NUM_VOICES; ++i) {
                VoiceState& cv = state.voices[i];
                if (cv.is_active && cv.cymbal.active) {
                    ++active;
                    busy_cost += kCymVoiceFixedLanes + (int)cv.cymbal.resCount;
                    // fade_mul < 1 = a voice the PREVIOUS preset left ringing
                    // and that LoadPreset is fading out (pass 28).  It is not
                    // this preset's plate, so it must not be re-excited — that
                    // would cancel its fade and build the new preset's bank on
                    // top of the old one's ring.
                    if (same < 0 && cv.current_note == note &&
                        cv.fade_mul >= 1.0f) same = i;
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
            if (same >= 0)                      state.next_voice_idx = (uint8_t)same;
            else if (active < cap && affordable && freeIdx >= 0)
                                                state.next_voice_idx = (uint8_t)freeIdx;
            else if (faded >= 0)                state.next_voice_idx = (uint8_t)faded;
            else if (oldest >= 0)               state.next_voice_idx = (uint8_t)oldest;
            else if (freeIdx >= 0)              state.next_voice_idx = (uint8_t)freeIdx;
        }
        VoiceState& v = state.voices[state.next_voice_idx];

        // ENGINE_CYMBAL restrike continuity: a gong/crash that is still ringing
        // when its slot is re-struck (or stolen) must not have its resonator
        // bank zeroed — a struck gong keeps sounding through the next strike,
        // and the hard reset is what made stacked hits read as "the previous
        // one disappears".  Captured before PartialReset() clears the flag.
        const bool cym_still_ringing =
            v.cymbal.active;

        // CRITICAL FIX 2: Ensure the voice actually turns on!
        v.is_active = true;

        v.current_note = note;
        v.current_velocity = vel_norm;   // MIDI velocity x the Velocity knob
        v.PartialReset();

        // The strike seed: the bank's lane detune is drawn from it, so it folds
        // in the note, the voice slot and the RAW MIDI velocity.
        uint32_t seed = (uint32_t)note * 1103515245u
                      ^ (uint32_t)state.next_voice_idx * 12345u
                      ^ (uint32_t)velocity * 2654435761u;
        // ── ENGINE_CYMBAL strike (ported dense-resonator cymbal) ───────────────
        // Replaces the old plate crash-bank + FDN for the metallic cymbal family.
        // Per-preset config is explicit so each preset tunes independently.
        {
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
                    // hfTilt stays 0 here, and pass 46 put it back after pass
                    // 45 set it to 2.4.  Worth the space, because the mistake
                    // is instructive: pass 45 called a 190 Hz centroid with
                    // nothing above 3 kHz "objectively wrong" on the strength
                    // of two written notes in this repo, having no recording to
                    // check against (samples/ was gitignored — see
                    // samples/README.md).  The recording says otherwise.
                    // Chinese-Gong.wav, the reference this preset is named
                    // after, measures a 196 Hz power centroid with 0.0 % of its
                    // energy above 3 kHz — i.e. the render pass 45 "fixed" was
                    // already a close match for it, and the tilt moved the
                    // preset onto the OTHER gong in samples/ (Gong-long-G#.wav,
                    // centroid 2473 Hz, a different instrument).  On refcmp's
                    // metric the sustain was right before and wrong after:
                    // reference 815 Hz, pre-45 728 Hz, pass-45 3225 Hz.
                    //
                    // What IS off is the attack — reference 1147 Hz early
                    // against 437 rendered — and hfTilt cannot buy it.  A
                    // static per-lane gain moves both windows together, and
                    // nothing in the bank's decay separates them either (see
                    // the rejected hfDamp note in dsp_core.h).  Attack
                    // brightness has to come from the STRIKE — stickLevel,
                    // thwackSec, the shimmer band — not from a bank-wide tilt.
                    // Mterl still moves the tilt either way from here.
                    //
                    // 240 is NOT a bank size — it is the prototype's bank size
                    // divided by the shipped density.  `resonators` is the count
                    // at 100 % `Partls`, and the knob only spans 25-60 %, so the
                    // 80 this used to hold rendered 80 x 0.40 = 32 lanes against
                    // `cymbal_synthesis`'s 96.  The port has been running the
                    // gong at a THIRD of the density it was ported from since
                    // the port was written; 240 x 0.40 = 96 restores it at the
                    // shipped knob position.  Measured against the prototype:
                    // 300 Hz-1 kHz 5.9 % -> 9.6 % (prototype 15.0 %), power
                    // centroid 203 -> 222 Hz (prototype 216), refcmp `late`
                    // 423 -> 590 Hz (prototype 699).  Above `Partls` 4 the
                    // request exceeds kCymbalMaxResonators and flattens at 112 —
                    // a real plateau on the top three knob positions, accepted
                    // because raising the cap costs .bss on all four voices for
                    // a range nothing asks for.
                    cc = { m_cym_gong_hz, 16, 240, 150.f, 14000.f, 2.4f,
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
            // HHat-O ("do not break"), Gong and Splash keep the legacy flat
            // bank (hfTilt stays 0).  The gong's place in that list is now
            // backed by its recording rather than by the phrase "deliberately
            // tonal": Chinese-Gong.wav measures a 196 Hz power centroid with
            // 0.0 % above 3 kHz, so a flat bank is what it should have.  Read
            // the long note on the gong's config above before changing it.
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
                // HHat-O is equally quiet by measurement but is flagged
                // "HW-approved, do not break" in CLAUDE.md, so its level is
                // deliberately NOT touched here — raise it only on an explicit
                // listen.
                //
                // Gong 1.28, and it is entirely make-up gain for two changes
                // that each removed real level without changing the voicing:
                // the subsonic guard (energy that was inaudible as pitch but WAS
                // feeding the master limiter) and the denser bank (1/sqrt(N)
                // normalisation spreads the same energy over 96 lanes instead
                // of 32).  Together 1.4 dB.  1.28 lands rms 0.1737 and T60
                // 4.99 s against the pre-pass-45 build's 0.1820 and 4.98 s —
                // i.e. the same length, 0.4 dB down.  1.40 matches the level
                // exactly but stretches T60 to 5.22 s by pushing the limiter
                // harder, and length is the thing this preset can least afford.
                float cym_trim = 1.0f;
                switch (m_preset_idx) {
                    case k_Cymbal:   cym_trim = 3.2f; break;
                    case k_Ride:     cym_trim = 2.9f; break;
                    case k_RideBell: cym_trim = 2.1f; break;
                    case k_Gong:     cym_trim = 1.28f; break;
                    case k_Splash:   cym_trim = 1.4f; break;
                    default: break;
                }
                v.cymbal.velocityGain *= cym_trim;
            }
        }
}

    // A cymbal has no release by design: the Drumlogue fires gate on and off in
    // the same tick, so a gate-off must not choke the tail.  Note-offs do
    // nothing; AllNoteOff fades the voices out instead.
    inline void NoteOff(uint8_t note) {
        (void)note;
    }

    inline void GateOff() {
        // Release the note GateOn STRUCK, not whatever root + Pitch is now.
        // Pitch (or a Program change, which moves the root) can move between
        // a held gate's on and off — a motion-sequenced Pitch, a long gate
        // time — and NoteOff(m_ui_note) then matched nothing: the struck voice
        // was never released (measured: a Koto voice rang 33.9 s instead of
        // 2.9 s after Pitch +5 under a held gate).  The drumlogue's own
        // same-tick gate cannot move it, so every same-tick render is
        // unchanged.  T44f.
        NoteOff(m_gate_note);

    }

    inline void AllNoteOff() {
        // Panic: the cymbal has no release envelope by design (gate_off must not
        // choke the tail), so it uses the click-free output fade instead (tau
        // 8 ms -> voice fully off ~55 ms after AllNoteOff).
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].cymbal.fadeMul = cym_env_mul(0.008f, k_dsp_sample_rate);
        }
    }

    inline void PitchBend(uint16_t bend) {
        // Pitch bend moved only the original's Karplus-Strong delay lines;
        // a cymbal is transposed by its note (Pitch), not bent.
        (void)bend;
    }

    // ==============================================================================
    // 5. The Core Physics (Executed per-voice, per-sample)
    // ==============================================================================

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
        // The voices accumulate into the LEFT lane only; the master stage
        // filters it and writes the result to both channels.  The idle path
        // below returns on an all-zero buffer.
        bool any_voice_rendered = false;
        for (int voice_idx = 0; voice_idx < NUM_VOICES; ++voice_idx) {
            VoiceState& voice = state.voices[voice_idx];
            if (!voice.is_active) continue;

            any_voice_rendered = true;

            // Self-contained dense-resonator cymbal: owns its own excitation,
            // decay and lifetime.  Output goes through the tilt EQ + master gain.
            {
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
                    if (!voice.cymbal.active) { voice.is_active = false; break; }
                }
            }

        }

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

        // Stacked cymbal voices (rolls) can sum past the ±0.99 brickwall, which
        // reads as harsh noise.  CymbalKit-style soft headroom first: a single
        // voice passes ~unchanged, stacks are limited gently.
        if (any_voice_rendered) {
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
        // It also squashed the strike crest.  This is a transparent limiter:
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
        // One master-stage gain, hoisted: the Gain knob's drive times the
        // preset's calibrated output trim.  Neither can change inside a block
        // (setParameter runs on the UI thread and the deferred drive/trim are
        // released above), so this is also one multiply per sample instead of
        // two.
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
    uint8_t m_root_note   = 36;   // preset 0 (Kick2) until LoadPreset runs
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

    // The knobs LoadPreset does NOT write — performance and master controls
    // the host owns (pass 48) — back to their header.c defaults.  Called once
    // from Init().  MUST track header.c's `.init` fields for these slots; C
    // cannot assert across the two files, so they are listed here explicitly.
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
    uint8_t m_poly = 4;             // global voice cap (1-4, Poly knob)
    uint8_t m_cym_reso_scale = 40;  // cymbal resonator-bank scale (25-60 %),
                                    // driven by Partls
    float   m_vel_bias = 0.0f;      // Velocity knob, -1 (ghost) .. +1 (wham);
                                    // 0 = neutral, strikes play as sent
};

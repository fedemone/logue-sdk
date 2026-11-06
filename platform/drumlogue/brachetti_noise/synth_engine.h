#pragma once
// Brachetti Noise — synth engine of the brachetti_noise unit.
//
// One of the per-family units split out of the 40-preset `brachetti` unit
// (platform/drumlogue/brachetti, which stays as it is).  This unit holds
// the pure-noise voices: Clap, Shaker and the closed hi-hat (HHat-C).
// Engine: ENGINE_NOISE only — the code of every other engine in the original
// (Karplus-Strong waveguide, dense-resonator cymbal, Timpani/Taiko kernel and
// the other modal families' per-preset branches) is not here.  At the
// knob values the presets ship with, every preset renders bit-identically to
// the same preset in the original; tools/brachetti_equiv proves it.
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

// What the original's per-strike noise-seed stir reduces to on an engine
// whose Karplus-Strong delay line is never written (see NoteOn).
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
        k_paramPoly,         // 1
        k_paramVelocity,     // 2
        k_paramMlltRes,      // 3
        k_paramMlltStif,     // 4
        k_paramVlMllRes,     // 5
        k_paramVlMllStf,     // 6
        k_paramModel,        // 7
        k_paramTone,         // 8
        k_paramHitPos,       // 9
        k_paramRel,          // 10
        k_paramCutoff,       // 11
        k_paramGain,         // 12
        k_paramNzMix,        // 13
        k_paramNzRes,        // 14
        k_paramNzFltr,       // 15
        k_paramNzFltFrq,     // 16
        k_paramResnc,        // 17
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of every preset.
    static_assert(k_lastParamIndex == 18, "header.c declares 18 params");
    enum ProgramIndex {
        k_Clap = 0,         // 0  -sample: 07_Clap_05_SP.wav (1532 +/- 100Hz)
        k_Shaker,           // 1  — woodblock body + grain-pulse noise (enveloped-LFO AM)
        k_HiHatClosed,      // 2  — the pre-redesign Shaker noise voice ("a perfect closed hi-hat" per HW report)
        k_NumPrograms       // 3 — marker (count)
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
    k_noise_band_mix,
    k_noise_hi_lp_coeff,
    k_model_param_total
};

float model_param_presets[k_NumPrograms][k_model_param_total]{
    /*               k_noise_band_mix, k_noise_hi_lp_coeff */
    /* k_Clap        */ { 0.00000f, 0.00000f},
    /* k_Shaker: modal_mix 0.04→0 — the woodblock body was a struck "tok" at onset
       (HW: "too much hit sound, should not be there").  Shaker = pure rattling
       noise (AM-modulated), no pitched hit. */
    /* k_Shaker      */ { 0.00000f, 0.00000f},
    /* k_HiHatClosed */ { 0.00000f, 0.00000f}
};

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
    /* 0  Clap    */ 1.00000f,
    /* 1  Shaker  */ 0.30340f,
    /* 2  HHat-C  */ 0.86956f};

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
    // parameter's value (the SDK: unit_reset() deactivates notes, resets
    // envelopes, clears delay lines, "however, parameter values should not be
    // reset").  Every knob-derived setting is already the knob's value the
    // moment Reset() runs, so it leaves them all alone (pass 48, T49 in the
    // original); the fresh-object values a first boot needs come from
    // init_fresh_voice_defaults(), which only Init() calls.
    inline void Reset() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            // Clear voice active / pitch memory
            state.voices[i].is_active    = false;
            state.voices[i].is_releasing = false;

            // Reset all runtime modulation state: envelopes, transient shaper,
            // modal bank, pitch env, boom, metal FM, onset, snare wire, etc.
            state.voices[i].PartialReset();

            // Noise filter: clear its MEMORY only.  Its mode and cutoff are the
            // NzFltr / NzFltFrq knobs and stay as they are.
            state.voices[i].exciter.noise_filter.lp = 0.0f;
            state.voices[i].exciter.noise_filter.bp = 0.0f;
            state.voices[i].exciter.noise_filter.hp = 0.0f;
        }

        // 1.5 -> 2.3: as loud as the bus can take before the master limiter's
        // dynamic-range guarantee (T39c: a ghosted hit must stay under 60% of
        // a neutral one) starts to erode — the limiter's soft knee compresses
        // loud hits much harder than quiet ones, so pushing gain further in
        // narrows velocity sensitivity rather than adding audible level.
        state.master_gain  = 2.3f;  // was low overall; +~3.7dB, capped by T39c
        // The drive is the Gain knob's and stays.  What Reset() does own is a
        // drive and trim DEFERRED behind a preset-change fade: the fade waits
        // for the outgoing voices, and Reset() has just killed them, so
        // complete it now — exactly what processBlock would do as the last
        // fade retired.  The queued values are the INCOMING preset's (the one
        // actually loaded).  This used to drop the queue and force the drive
        // to 1.0, which threw the loaded preset's Gain away and, mid-fade,
        // left the OUTGOING preset's output trim installed for good.
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

        // Master filter is a LOWPASS "Cutoff" (mode 0), default fully open.
        // It was a highpass ("LowCut"), but every HW pass reported the knob as
        // reversed: raising it silenced the sound.  Players expect a drum-synth
        // master cutoff to OPEN as it rises — so it is now an LP with max = open.
        // The mode is fixed; the coefficients are the Cutoff/Resnc knobs and
        // stay (this used to hard-code 16 kHz / Q 0.707, below the knob's own
        // "open" of 19.99 kHz).  Only the filter's memory is cleared.
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
    // names are the original brachetti's, kept so its docs read as written).
    float   m_modal_stiff_ref  = 0.26f;   // MlltStif, normalized 0.01..1
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
        // ~10 ms fade-out so it retires click-free under its OWN latched engine
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
        // values the preset ships with), then the hidden voicing columns after the bar.
        // Root: the note the sequencer plays (this unit has no Pitch knob —
        // nothing in a noise voice depends on the note).
        // Dky: the preset's decay setting.  There is no Dkay knob here (it
        // moves nothing a noise voice plays); its one remaining effect is how
        // fast AllNoteOff fades a voice, which this column keeps exact.
        // Columns LoadPreset does NOT load are knobs the host owns (pass 48):
        // Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc.
        // Their values below are never read.
        // int16_t: the stored range is [-10, 1999].
        static constexpr int16_t presets[k_NumPrograms][k_lastParamIndex + 2] = {
            //  Prg  Ply  Vel MlRs MlSt VlRs VlSt  Mdl  Ton  Hit  Rel  Cut Gain NzMx NzRs NzFl NzFq Rsnc  |Root  Dky
            {   0,   0,   1, 500,  27,   0,   0,   5,   0,  50,  19,1999,   5,  95, 950,   1, 200,  71,  60,  15},   // 0: Clap      — multi-burst AM (~55Hz, NoteOn) + Rel 19 for the 'tcha' tail; NzFq 300→200 (BP 2kHz) toward ref centroid 2986Hz (was 4540)
            {   1,   0,   1, 500,  45,   0,   0,   6,   0,  50,  19,1999,   5,  95, 940,   1, 550,  71,  84,  30},   // 1: Shaker    — Rel 18→19 longer tail so the now-sustained 17Hz rattle (noise_am_decay=1.0) is audible by default; raise Rel for a longer rattle
            {   2,   0,   1, 100,  37,   0,   0,   5,   0,  50,  18,1999,   3,  90, 900,   2, 800,  71,  72,  12}    // 2: HHat-C    — the pre-redesign Shaker noise voice ('a perfect closed hi-hat' per HW)
        };

        static_assert(preset_column_in(presets, k_paramVlMllRes, 0, 0),
                      "VlMllRes is not loaded per preset: every row must ship 0");

        static_assert(preset_column_in(presets, k_paramTone, 0, 0),
                      "Tone is not loaded per preset: every row must ship 0");

        static_assert(preset_column_in(presets, k_paramCutoff, 1999, 1999),
                      "Cutoff is not loaded per preset: every row must ship 1999");

        static_assert(preset_column_in(presets, k_paramResnc, 71, 71),
                      "Resnc is not loaded per preset: every row must ship 71");

        // Apply the row, skipping Program (recursion) and the knobs the host owns.
        m_root_note = (uint8_t)presets[idx][k_lastParamIndex + 0];   // Root
        update_played_note();
        for (uint8_t param_id = 0; param_id < k_lastParamIndex; ++param_id) {
            if (param_id == k_paramProgram) continue;
            if (param_id == k_paramPoly) continue;
            if (param_id == k_paramVelocity) continue;
            if (param_id == k_paramVlMllRes) continue;
            if (param_id == k_paramTone) continue;
            if (param_id == k_paramCutoff) continue;
            if (param_id == k_paramResnc) continue;
            setParameter(param_id, presets[idx][param_id]);
        }
        apply_release_rate(presets[idx][k_lastParamIndex + 1]);   // Dky

        // Capture the shipped knob values as the REFERENCE ANCHORS: every knob
        // mapping pivots on them, so the shipped preset plays its calibrated
        // sound and only knob MOVEMENT alters it.
        m_modal_stiff_ref = fmaxf(0.01f, fminf(1.0f, (float)presets[idx][k_paramMlltStif] * 0.02f));
        m_modal_mltres_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramMlltRes] * 0.001f));
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

        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            // Skip a voice that is fading out from the PREVIOUS preset: these
            // writes would retune its wire/boom/pitch state mid-fade, and
            // init_modal_modes below would re-strike its modal bank at full
            // amplitude.  It gets these values on its next NoteOn anyway.
            if (v.is_active && v.fade_mul < 1.0f) continue;
            // Both noise-shaper memories: the table holds 0 for every preset.
            v.exciter.noise_lp_state = 0.0f;
            v.exciter.noise_band_mix = preset_param(static_cast<ProgramIndex>(idx), k_noise_band_mix);
            v.exciter.noise_hi_lp_state = 0.0f;
            v.exciter.noise_hi_lp_coeff = preset_param(static_cast<ProgramIndex>(idx), k_noise_hi_lp_coeff);
        }
    }

    static inline const char * getPresetName(uint8_t idx) {
        static const char* const preset_names[] = {
            "Clap", "Shaker", "HHat-C"
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

            case k_paramModel:
                // Physical model: swaps the modal ratio template (NoteOn) and picks
                // the default noise band balance.
                m_model = (uint8_t)value;
                break;

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
        } else if (index == k_paramNzFltr) {
            if (value >= 0 && value < 3) return nz_filter_names[value];
        } else if (index == k_paramMlltStif) {
            // Stored ÷100; show the real ×100 value (0-5000, step 100).
            static char ms_buf[8];
            snprintf(ms_buf, sizeof(ms_buf), "%d", (int)(value * 100));
            return ms_buf;
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

    // Roll window: strokes closer together than this belong to ONE physical
    // roll (pressed roll / buzz roll).  Shared by BOTH the voice-fusion test in
    // NoteOn and the snare wire-continuity restore below it — keep them on one
    // constant so the two mechanisms can never disagree about what a roll is.
    static constexpr float kRollFuseSec = 0.080f;

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
        // Global Poly cap: voices round-robin within the first m_poly slots, so
        // the knob truly sets (and the display truly shows) the polyphony.
        {
            const uint8_t cap = m_poly;
            // ROLL FUSION (HW: "pressed rolls feel less smooth, Djambe
            // especially muddy").  Strokes inside kRollFuseSec on the SAME note
            // REUSE the last voice (one continuous roll); anything slower, or on
            // a different note, stacks.
            const VoiceState& lastv = state.voices[state.next_voice_idx];
            const float fuse_sec = kRollFuseSec;
            const bool fuse = lastv.is_active &&
                              lastv.current_note == note &&
                              (lastv.exciter.current_frame <
                               (uint32_t)(fuse_sec * default_sample_rate));
            if (!fuse)
                state.next_voice_idx = (uint8_t)((state.next_voice_idx + 1) % cap);
        }
        VoiceState& v = state.voices[state.next_voice_idx];

        // CRITICAL FIX 2: Ensure the voice actually turns on!
        v.is_active = true;
        v.is_releasing = false;

        v.current_note = note;
        v.current_velocity = vel_norm;   // MIDI velocity x the Velocity knob
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
        }

        v.PartialReset();

        // Stir the voice's noise seed on every strike.  The original folded eight
        // words of the Karplus-Strong delay line into it; on this engine that line
        // is never written, so those words are always zero and the fold reduces to
        // this constant (computed exactly as the original's loop computes it).
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
        if (v.noise_am_inc > 0.0f) {
            float st = fmaxf(0.01f, fminf(1.0f, (float)m_params[k_paramMlltStif] * 0.02f));
            v.noise_am_inc *= knob_exp2(2.0f * (st - m_modal_stiff_ref));
            float mr = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramMlltRes] * 0.001f));
            v.noise_am_depth = fmaxf(0.0f, fminf(0.99f,
                                     v.noise_am_depth * knob_exp2(1.5f * (mr - m_modal_mltres_ref))));
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
        // was never released (measured: a Koto voice rang 33.9 s instead of
        // 2.9 s after Pitch +5 under a held gate).  The drumlogue's own
        // same-tick gate cannot move it, so every same-tick render is
        // unchanged.  T44f.
        NoteOff(m_gate_note);

        // Voice allocation for repeated triggers is decided in NoteOn, where the
        // incoming note is known: round-robin within Poly, with roll fusion for fast same-note strokes.  GateOff only sees
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
        // Pitch bend moved the Karplus-Strong delay lines in the original, and only
        // those: the modal bank, boom and noise never read it.  This engine has no
        // delay line, so a bend has nothing to move.
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
        // PARALLEL with the resonator output — broadband texture (snare buzz,
        // hat hiss, clap) stays separate from the pitched ring.
        ex.noise_out_sample = 0.0f;
        float noise_env_low = ex.noise_env.process();
        float noise_env_high = ex.noise_env_hi.process();
        if (noise_env_low > 0.001f || noise_env_high > 0.001f) {
             float raw_noise = ex.noise_gen.process();
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

            const float base_parallel_noise_gain = 5.0f;

            for (size_t i = 0; i < frames; ++i) {

                // ── Stage 1: Raw exciter (always executes) ─────────────────
                // Mallet impulse and/or PCM sample — the most direct signal
                // possible.  If Stage 1 is silent, the voice is never activated
                // or unit_gate_on / unit_render are not being called.
                float exciter_sig = process_exciter(voice.exciter);
                float voice_out   = exciter_sig * voice.current_velocity;

                // Parallel noise path: noise bypasses the waveguide and mixes directly
                // into the voice output.  This preserves the broadband character that the
                // resonator would otherwise pitch-filter away (snare buzz, cymbal wash,
                // hi-hat hiss, shaker rattle).  The ×5 base factor brings noise amplitude
                // into the same ballpark as the resonator output driven by the ×15 mallet;
                // per-preset overrides are folded into base_parallel_noise_gain above.
                float parallel_noise_gain = base_parallel_noise_gain;
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
    uint8_t m_ui_note     = 36;
    uint8_t m_gate_note   = 36;   // the note the last GateOn struck

    inline void update_played_note() { m_ui_note = m_root_note; }   // no Pitch knob

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

    // The knobs LoadPreset does NOT write — performance and master controls
    // the host owns (pass 48) — back to their header.c defaults.  Called once
    // from Init().  MUST track header.c's `.init` fields for these slots; C
    // cannot assert across the two files, so they are listed here explicitly.
    inline void seed_performance_params() {
        static const int16_t kDefaults[][2] = {
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

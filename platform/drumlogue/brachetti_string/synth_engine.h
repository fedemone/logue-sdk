#pragma once
// Brachetti String — synth engine of the brachetti_string unit.
//
// One of the per-family units split out of the 40-preset `brachetti` unit
// (platform/drumlogue/brachetti, which stays as it is).  This unit holds the
// Karplus-Strong string: Koto, the original's only ENGINE_KS preset — two
// waveguide resonators (ResA / ResB, edited together or apart through the
// Partls selector 5-7) with a pluck exciter, a noise exciter and a fixed
// harmonic-overtone modal layer.  The other families' engines and per-preset
// branches are not here.  At the knob values Koto ships with it renders
// bit-identically to the original; tools/brachetti_equiv proves it.
//
// With a single preset there is no Program knob: the unit IS the Koto voice,
// and every other parameter works as in the original.
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

FastTables g_tables;

// ==============================================================================
// CONSTANTS
// ==============================================================================
static constexpr float    default_sample_rate = 48000.0f;
static constexpr float    inverse_default_sample_rate = 2.0833333333e-5f; // 1 / 48000
static constexpr uint16_t pitch_centre = 8192;
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
        k_paramPitch = 0,    // 0
        k_paramPoly,         // 1
        k_paramVelocity,     // 2
        k_paramMlltRes,      // 3
        k_paramMlltStif,     // 4
        k_paramVlMllRes,     // 5
        k_paramVlMllStf,     // 6
        k_paramPartls,       // 7
        k_paramModel,        // 8
        k_paramDkay,         // 9
        k_paramMterl,        // 10
        k_paramTone,         // 11
        k_paramHitPos,       // 12
        k_paramRel,          // 13
        k_paramInharm,       // 14
        k_paramCutoff,       // 15
        k_paramTubRad,       // 16
        k_paramGain,         // 17
        k_paramNzMix,        // 18
        k_paramNzRes,        // 19
        k_paramNzFltr,       // 20
        k_paramNzFltFrq,     // 21
        k_paramResnc,        // 22
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of every preset.
    static_assert(k_lastParamIndex == 23, "header.c declares 23 params");
    // Pitch knob reach, semitones either side of the preset's root note.
    // Must equal the min/max header.c declares for "Pitch".
    static constexpr int kPitchRangeSemis = 24;
    enum ProgramIndex {
        k_Koto = 0,         // 0  -sample: Koto-B5.wav (starts as 700Hz and settles to 290Hz), Koto-Stab-F#.wav (750Hz +/- 100Hz)
        k_NumPrograms       // 1 — marker (count)
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
    k_pitch_env,
    k_pitch_env_decay,
    k_pitch_env_amt,
    k_modal_mix,       // Modal bank mix (0.0 = off, overrides ModalPresetConfig.mix)
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
    /* k_Koto: harmonic-overtone reinforcement on top of the KS string (mix 0.10
       in model_param_presets).  Strong 2nd/3rd partials + a slightly sharp 4.2
       shimmer are the missing koto colour reported from HW. */
    {2.005f, 3.012f, 4.215f, 1100.0f, 800.0f, 600.0f, 420.0f, 0.34f, 0.80f, 0.74f, 0.66f, 0.56f, 6, 5.42f, 6.81f, 0.46f, 0.36f}
};

float model_param_presets[k_NumPrograms][k_model_param_total]{
    /*               k_noise_band_mix, k_noise_hi_lp_coeff, k_pitch_env, k_pitch_env_decay, k_pitch_env_amt, k_modal_mix */
    /* k_Koto        */ { 0.00000f, 0.00000f, 1.00000f, 0.99900f, 1.50000f, 0.22000f}
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
    /* 0  Koto    */ 0.13641f};

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

        g_tables.generate(default_sample_rate); // Pre-calculate all tuning math

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

    // Called when the user changes programs or the engine needs a hard flush.
    // This prevents loud "pops" from old delay line data playing unexpectedly.
    // RUNTIME state only — never a parameter's value (pass 48).  The SDK:
    // unit_reset() deactivates notes, resets envelopes, clears delay lines,
    // "however, parameter values should not be reset".  This used to rewrite
    // seven knob-derived settings to fixed values — Gain's drive, the HitPos
    // A/B blend, the noise filter's mode and cutoff, the KS loop's loss and
    // phase (Mterl / TubRad / Model), the ResA/ResB edit selector, Tone and
    // the master filter — and nothing put them back until the next Program
    // change or a touch of that knob: Marimba came back 12 dB quiet, and
    // AcSnare / HHat-C / BrshSnr with the wrong noise colour.  The fresh-object
    // defaults those writes provided for the very first boot now live in
    // init_fresh_voice_defaults(), which only Init() calls.  T49.
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

            // Clear voice active / pitch memory
            state.voices[i].is_active    = false;
            state.voices[i].is_releasing = false;
            state.voices[i].base_delay_A = 0.0f;
            state.voices[i].base_delay_B = 0.0f;

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
        // HitPos's A/B blend (mix_ab) and Tone (state.tone) are knob values and
        // are left alone, as is the ResA/ResB edit selector (Partls 5-7): it is
        // the user's editing context, and resetting it while the panel still
        // showed "ResA" or "ResB" made the next Model/Dkay/Mterl edit land on
        // the other resonator too.  Init() sets the selector for a fresh boot.
        m_pitch_bend_mult  = 1.0f; // Clear any held bend so the next note plays in tune.

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

    // Tracks the raw UI integer for all 24 parameter slots (indices 0-23)
    int32_t m_params[k_lastParamIndex] = {0};
    uint8_t m_preset_idx = 0;
    // REFERENCE ANCHOR for the one knob mapping on this engine that pivots on
    // a shipped value: VlMllStf → mallet stiffness (NoteOn).  Captured in
    // LoadPreset.
    float   m_snare_vlstf_ref  = 0.0f;  // shipped VlMllStf * 0.01

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

    // The current preset's root note (its Note column) and the note the
    // sequencer path actually plays, root + Pitch.  Host tools use these to
    // turn an absolute note into a Pitch offset; nothing on the device reads
    // them.
    inline uint8_t PresetRootNote() const { return m_root_note; }
    inline uint8_t PlayedNote() const     { return m_ui_note; }

    // Called by unit_set_param_value(0, value) to load a new patch
    inline void LoadPreset(uint8_t idx) {
        // Koto is the only preset: only Init() (and an OS unit_load_preset(0))
        // ever loads it.
        if (idx >= k_NumPrograms) return;
        m_preset_idx = idx;

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
            // Root  Ply  Vel MlRs MlSt VlRs VlSt Ptls  Mdl  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain NzMx NzRs NzFl NzFq Rsnc
            {  60,   0,   1, 600,  42,   0,   0,   0,   0, 200,  20,   0,   0,  12,   0,1999,  18,   0,   0, 300,   0,1000,  71}    // 0: Koto      — + harmonic-overtone modal bank (mix 0.10)
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
                      "the shipped Partls is a partial count, not a selector");

        // Preset loading always targets ResA first, then ResB, regardless of the
        // current editor selection.  Save both flags and restore them afterwards so
        // a preset load never changes which resonator(s) the user is editing.
        bool saved_is_a = m_is_resonator_a;
        bool saved_is_b = m_is_resonator_b;
        m_is_resonator_a = true;
        m_is_resonator_b = false;

        // Apply the row, skipping the knobs the host owns (pass 48).  Column 0
        // (Pitch) is the ROOT note: the knob is an offset from it.
        m_root_note = (uint8_t)presets[idx][k_paramPitch];
        update_played_note();
        for (uint8_t param_id = 0; param_id < k_lastParamIndex; ++param_id) {
            if (param_id == k_paramPitch) continue;
            if (param_id == k_paramPoly) continue;
            if (param_id == k_paramVelocity) continue;
            if (param_id == k_paramVlMllRes) continue;
            if (param_id == k_paramTone) continue;
            if (param_id == k_paramCutoff) continue;
            if (param_id == k_paramResnc) continue;

            // Enforce ResA-only routing on every single parameter so Partls
            // cannot hijack the rest of the loop.
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

        // The shipped VlMllStf: the mallet-stiffness mapping in NoteOn pivots
        // on it, so the shipped value plays the calibrated pluck.
        m_snare_vlstf_ref  = (float)presets[idx][k_paramVlMllStf] * 0.01f;

        // Restore both flags so the user's edit context survives preset loads.
        m_is_resonator_a = saved_is_a;
        m_is_resonator_b = saved_is_b;

        // The preset's calibrated output trim (see kPresetOutTrim).
        m_out_trim = kPresetOutTrim[idx];

        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            v.exciter.noise_lp_state = 0.0f;
            v.exciter.noise_band_mix = preset_param(static_cast<ProgramIndex>(idx), k_noise_band_mix);
            v.exciter.noise_hi_lp_state = 0.0f;
            v.exciter.noise_hi_lp_coeff = preset_param(static_cast<ProgramIndex>(idx), k_noise_hi_lp_coeff);
            v.pitch_env = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env);
            v.pitch_env_decay = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env_decay);
            v.pitch_env_amt = preset_param(static_cast<ProgramIndex>(idx), k_pitch_env_amt);

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
            "Koto"
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
            case k_paramPitch: {
                // Semitone offset from the preset's root note (pass 48; was an
                // absolute MIDI note).  Bounded here as the SDK asks: patterns
                // saved before pass 48 still hold an ABSOLUTE note (36, 72...)
                // in this slot, and that has to land as a legal offset, not as
                // "root + 72".  Stored clamped so the host reads back what plays.
                const int32_t p = (value < -kPitchRangeSemis) ? -kPitchRangeSemis
                                : (value >  kPitchRangeSemis) ?  kPitchRangeSemis
                                : value;
                m_params[index] = p;
                m_pitch_semis   = (int8_t)p;
                update_played_note();
                break;
            }

            case k_paramPoly:
                // Global voice cap 1-4: a repluck of the SAME note reuses its
                // string's slot, a different note takes one of the first m_poly.
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

            // resonator A/B parameters
            case k_paramPartls: {
                if (value < 0) break;   // never index partial_counts[] negatively
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
                // Dkay's KS loop gain has a Drumhead curve of its own, so a
                // Model change must re-derive it (pass 48).  It did not: Koto
                // at Dkay 100 switched to Drumhead kept g 0.9245 where 0.7245
                // is due — invisible at the shipped Dkay 200 only because both
                // curves meet at 0.999 there.
                apply_ks_feedback_gain();
                break;
            }

            case k_paramDkay: {
                // 0.85 = instant dead thud. 0.999 = rings for ~5 seconds.
                // Stored ÷10 (0-200 represents 0-2000). Divide by 200 (new max).
                // CLAMPED, not skipped (pass 48).  This used to be
                // `if (value <= 200) { ... }`, so a value above 200 was ignored
                // outright and the last value <= 200 stuck (Koto at 150, then
                // 205, kept playing 150), while the header offered 201-210 —
                // ten positions every consumer saturates at 200 anyway.  The
                // header range is 0..200 now and HHat-O ships 200 (it shipped
                // 210, which every consumer already read as 200).
                {
                    const int32_t dk = (value < 0) ? 0 : ((value > 200) ? 200 : value);
                    m_params[index] = dk;
                    float norm = fmaxf(0.0f, fminf(1.0f, (float)dk * 0.005f));
                    // The KS loop gain depends on the MODEL as well (Drumhead
                    // has its own curve), so it lives in apply_ks_feedback_gain(),
                    // which the Model case calls too.  Each resonator keeps its
                    // own Dkay, because Partls 6/7 edit them separately.
                    if (m_is_resonator_a) m_dkay_norm_a = norm;
                    if (m_is_resonator_b) m_dkay_norm_b = norm;
                    apply_ks_feedback_gain();
                    // master_env gate: exponential 50ms (Decay=0) → 10s (Decay=200).
                    // Decay is the primary sustain control; Rel only gates the noise
                    // burst.  Without this, the master_env would kill the waveguide
                    // resonance at ~28 ms (default Rel) regardless of Decay setting.
                    float t_s = fasterpow2f(k_log_2_of_200 * norm); // 50ms..10s - was fasterpowf(200.0f, norm)
                    float master_rate = M_THREELN10 * 20 * inverse_default_sample_rate / t_s;  // was 3.0f * M_LN10
                    for (int i = 0; i < NUM_VOICES; ++i) {
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

        if (index == k_paramModel) {
            if (value >= 0 && value < 9)
                return m_is_resonator_a && m_is_resonator_b ? model_names_ab[value] :
                    m_is_resonator_a ? model_names_a[value] : model_names_b[value];
        } else if (index == k_paramPartls) {
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
        } else if (index == k_paramPoly) {
            // The string honours the full Poly range: a repluck of the same
            // note reuses its slot, different notes stack up to the cap.
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
        // Global Poly cap: voices round-robin within the first m_poly slots.
        {
            const uint8_t cap = m_poly;
            // One slot per STRING, not one per INSTRUMENT (HW pass 41: "Koto:
            // stacking notes feels unnatural").  A koto has thirteen strings and
            // they ring together, so a DIFFERENT note takes its own voice; a
            // repluck of the SAME note reuses its slot — the plectrum stops the
            // string before exciting it again, and two overlapping plucks of one
            // pitch would beat.  No time window: a string is stopped by being
            // replucked however long you waited.
            const VoiceState& lastv = state.voices[state.next_voice_idx];
            const bool ks_same_string = lastv.is_active && lastv.current_note == note;
            if (!ks_same_string)
                state.next_voice_idx = (uint8_t)((state.next_voice_idx + 1) % cap);
        }
        VoiceState& v = state.voices[state.next_voice_idx];

        // Capture before is_active is overwritten: only a previously-active voice
        // has residual delay-line data that needs clearing on retrigger.
        // A fresh slot (never used since Reset()) is already zero — skip the work.
        const bool had_residual = v.is_active || v.is_releasing;

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
        // Master envelope gate.  decay_rate and release_rate are set by
        // setParameter(k_paramDkay).  The string AUTO-DECAYS from 1.0 toward 0
        // while the gate is held (Dkay sets how fast); NoteOff switches to the
        // faster release_rate.  Assigned directly rather than through trigger():
        // ARM -ffast-math may leave an FMA a hair under the 0.99 attack threshold.
        v.exciter.master_env.sustain_level = 0.0f;
        v.exciter.master_env.value = 1.0f;
        v.exciter.master_env.state = ENV_DECAY;

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
        // PartialReset() clears boom helper fields; restore them from preset params
        // on every NoteOn, without preset-specific branching.
        const ProgramIndex preset = static_cast<ProgramIndex>(m_preset_idx);
        // ── Pitch-envelope restore (MUST be after PartialReset) ──────────────
        // PartialReset() zeroes pitch_env / _decay / _amt and only LoadPreset
        // writes them, so without this the first NoteOn would clear them for
        // good.  The KS branch sweeps the delay line with it: Koto's pluck bend
        // (pass 38), starting 1.5 semitones sharp.
        v.pitch_env       = preset_param(preset, k_pitch_env);
        v.pitch_env_decay = preset_param(preset, k_pitch_env_decay);
        v.pitch_env_amt   = preset_param(preset, k_pitch_env_amt);
        // Modal bank: re-initialize on every NoteOn so frequencies track the played note
        // and envelopes are velocity-scaled. LoadPreset called init_modal_modes with
        // current_velocity=0 (default) which zeroed all modal_env_X values, silencing
        // the entire modal synthesis path — Cymbal and Gong were running without it.
        {
            const ModalPresetConfig& mc = modal_preset_configs[m_preset_idx];
            if (mc.mode_count > 0) {
                float modal_mix_val = preset_param(static_cast<ProgramIndex>(m_preset_idx), k_modal_mix);
                // The string's modal layer plays its calibrated overtones as they
                // are: the knob → modal-bank mappings (Model, Partls, Inharm,
                // Mterl, TubRad, Dkay, MlltStif, MlltRes, HitPos) belong to the
                // modal-only engines — on the string those knobs shape the
                // waveguide instead.
                v.init_modal_modes(mc.ratio2, mc.ratio3, mc.ratio4,
                                   mc.t60_1_ms, mc.t60_2_ms, mc.t60_3_ms, mc.t60_4_ms,
                                   modal_mix_val, mc.env1, mc.env2, mc.env3, mc.env4,
                                   mc.mode_count, mc.ratio5, mc.ratio6,
                                   mc.env5, mc.env6);
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
                // The string releases its master envelope so the voice decays at
                // the Dkay release rate.
                v.exciter.master_env.release();
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
        wg.z1 = (ap_out * wg.lowpass_coeff) + (wg.z1 * (1.0f - wg.lowpass_coeff));
        float filtered_out = wg.z1;
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
        // Every engine here is single-channel, and Stage 4b filters the LEFT
        // sample and writes the result to BOTH channels, so anything a voice
        // accumulates into main_out[i*2+1] is overwritten before it can be
        // heard.  The voice loops therefore write the left lane only — the
        // right one is filled once, at the end.  This is not a shortcut: the
        // stereo store cost an extra load-add-store per sample PER VOICE in the
        // hottest loop in the unit, and the cymbal soft-headroom pass below was
        // spending a divide per sample on a lane nobody reads.
        // The idle path below returns on an all-zero buffer.
        bool any_voice_rendered = false;
        for (int voice_idx = 0; voice_idx < NUM_VOICES; ++voice_idx) {
            VoiceState& voice = state.voices[voice_idx];
            if (!voice.is_active) continue;

            any_voice_rendered = true;

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

            const float base_parallel_noise_gain = 5.0f;
            for (size_t i = 0; i < frames; ++i) {

                // ── Stage 1: Raw exciter (always executes) ─────────────────
                // Mallet impulse and/or PCM sample — the most direct signal
                // possible.  If Stage 1 is silent, the voice is never activated
                // or unit_gate_on / unit_render are not being called.
                float exciter_sig = process_exciter(voice.exciter);
                float voice_out   = exciter_sig * voice.current_velocity;

                // ── Stage 2: the waveguide resonators ─────────────────────
                {
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
                    float outA = process_waveguide(voice.resA, inputA);
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
                }

                // Parallel noise path: noise bypasses the waveguide and mixes directly
                // into the voice output.  This preserves the broadband character that the
                // resonator would otherwise pitch-filter away (snare buzz, cymbal wash,
                // hi-hat hiss, shaker rattle).  The ×5 base factor brings noise amplitude
                // into the same ballpark as the resonator output driven by the ×15 mallet;
                // per-preset overrides are folded into base_parallel_noise_gain above.
                float parallel_noise_gain = base_parallel_noise_gain;
                voice_out += voice.exciter.noise_out_sample * parallel_noise_gain * voice.current_velocity;
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
                    // The modal bank is an overtone layer on top of the string: x1
                    // (the modal-only engines ran it at x5).
                    const float modal_engine_gain = 1.0f;
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
                    if (!voice.is_releasing &&
                        voice.exciter.master_env.state == ENV_IDLE) {
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

    // The note the sequencer path plays (GateOn / GateOff) = the preset's ROOT
    // note (its Note column, captured by LoadPreset) + the Pitch knob.  Kept
    // derived rather than stored on its own so neither input can go stale
    // against it: LoadPreset moves the root, setParameter moves the offset,
    // and both re-derive through update_played_note().
    uint8_t m_root_note   = 60;   // Koto's root, set by LoadPreset
    int8_t  m_pitch_semis = 0;    // Pitch knob, ±kPitchRangeSemis
    uint8_t m_ui_note     = 36;
    uint8_t m_gate_note   = 36;   // the note the last GateOn struck
    // Each resonator's own Dkay, normalised 0..1 — the KS gain needs it again
    // whenever Model changes, and m_params holds only the last one written.
    float   m_dkay_norm_a = 1.0f;
    float   m_dkay_norm_b = 1.0f;
    // KS feedback gain for one resonator, from ITS OWN model and Dkay.
    // Drumhead (model 5): percussion skins need a shorter minimum decay than
    // strings/bars. Formula [0.45, 0.999] lets Dkay=55 give T60≈175ms@65Hz —
    // natural kick/tom body ring before boom takes over.  Membrane (3) and all
    // other models keep [0.85, 0.999].
    static inline float ks_feedback_gain(uint8_t model, float dkay_norm) {
        return (model == k_Drumhead) ? (0.45f + dkay_norm * 0.549f)
                                     : (0.85f + dkay_norm * 0.149f);
    }
    // Writes both resonators every time.  The old Dkay case chose ONE curve
    // for whichever resonators were selected — Drumhead if either was — so
    // editing ResA+B with only one of them on Drumhead gave the other the
    // wrong curve.  ENGINE_KS is the only reader of feedback_gain.
    inline void apply_ks_feedback_gain() {
        const float ga = ks_feedback_gain(m_model_a, m_dkay_norm_a);
        const float gb = ks_feedback_gain(m_model_b, m_dkay_norm_b);
        for (int i = 0; i < NUM_VOICES; ++i) {
            state.voices[i].resA.feedback_gain = ga;
            state.voices[i].resB.feedback_gain = gb;
        }
    }

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

    // Fresh-object values for state a knob owns once a preset is loaded — the
    // KS loop's loss/phase (Mterl / TubRad / Model), the noise filter (NzFltr /
    // NzFltFrq) — and for the ResA/ResB edit selector.  Reset() used to write
    // these on EVERY reset, which clobbered the knobs; now only Init() does,
    // so that Init() on a USED object (every host tool reuses one) starts
    // where a fresh one does.  The coefficients are the member defaults and
    // LoadPreset(0) rewrites them anyway; the selector is the one that
    // matters, because LoadPreset deliberately preserves it — without this a
    // re-Init() would keep a stale "ResB only".  Same values and order as the
    // old Reset() writes, so a boot is bit-for-bit what it was.  T49b.
    inline void init_fresh_voice_defaults() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            VoiceState& v = state.voices[i];
            v.resA.lowpass_coeff = 1.0f;  v.resB.lowpass_coeff = 1.0f;
            v.resA.loss_g_dc     = 1.0f;  v.resB.loss_g_dc     = 1.0f;
            v.resA.loss_g_hf     = 1.0f;  v.resB.loss_g_hf     = 1.0f;
            v.resA.phase_mult    = 1.0f;  v.resB.phase_mult    = 1.0f;
            v.exciter.noise_filter.mode = 0;   // LP, fully open (12 kHz)
            v.exciter.noise_filter.set_coeffs(12000.0f, 0.707f, default_sample_rate);
        }
        m_is_resonator_a = true;   // a fresh boot edits ResA + ResB together
        m_is_resonator_b = true;
    }

    // The knobs LoadPreset does NOT write — performance and master controls
    // the host owns (pass 48) — back to their header.c defaults.  Called once
    // from Init().  MUST track header.c's `.init` fields for these slots; C
    // cannot assert across the two files, so they are listed here explicitly.
    inline void seed_performance_params() {
        static const int16_t kDefaults[][2] = {
            { k_paramPitch,    0    },
            { k_paramPoly,  4    },
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

#pragma once
// Brachetti Kernel — synth engine of the brachetti_kernel unit.
//
// Timpani and Taiko on the dense coupled-resonator ModalDrumKernel
// (modal_drum_kernel.h: ~280 measured + fill modes, a recorded knock, a
// noise wedge, two kettles) with its own master stage.  This file maps
// the knobs onto the kernel and runs that master stage.
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship
// with, each renders bit-identically to the same preset there;
// tools/brachetti_equiv checks it.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <float_math.h>
#include "../common/runtime.h" // Drumlogue OS functions
#include "unit.h"
#include "filter.h"
#include "modal_drum_kernel.h" // dense coupled-resonator kernel (Timpani/Taiko)

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
static constexpr float    kToneLpMix = 0.3f;
static constexpr float    kInvToneCutDivisor = 0.1f;         // 1 / 10
static constexpr float    kInvToneBoostDivisor = 0.13333334f; // 1 / 7.5
static constexpr float    zeroThreshold = 0.0f;

// The master state the kernel path uses: Tone, the master low-pass and the
// Gain drive.
struct KernelMaster {
    float   master_drive = 1.0f;   // ← non-zero
    float   tone         = 0.0f;   // Tilt EQ amount, cached from k_paramTone [-10, 30]
    FastSVF master_filter;
};

// ==============================================================================
// MAIN CLASS
// ==============================================================================
class alignas(16) BrachettiSynth {
public:
    enum ParamIndex {
        k_paramProgram = 0,  // 0
        k_paramPitch,        // 1
        k_paramVelocity,     // 2
        k_paramMlltRes,      // 3
        k_paramMlltStif,     // 4
        k_paramVlMllRes,     // 5
        k_paramVlMllStf,     // 6
        k_paramPartls,       // 7
        k_paramDkay,         // 8
        k_paramMterl,        // 9
        k_paramTone,         // 10
        k_paramHitPos,       // 11
        k_paramRel,          // 12
        k_paramInharm,       // 13
        k_paramCutoff,       // 14
        k_paramTubRad,       // 15
        k_paramGain,         // 16
        k_paramNzMix,        // 17
        k_paramNzFltFrq,     // 18
        k_paramResnc,        // 19
        k_lastParamIndex    // marker
    };
    // Tripwire: header.c declares `.num_params` and lays out exactly this many
    // slots, and the preset table below has one column per slot.  Adding a
    // parameter to this enum without adding its row to header.c (or vice versa)
    // silently shifts every column of every preset.
    static_assert(k_lastParamIndex == 20, "header.c declares 20 params");
    // Pitch knob reach, semitones either side of the preset's root note.
    // Must equal the min/max header.c declares for "Pitch".
    static constexpr int kPitchRangeSemis = 24;
    enum ProgramIndex {
        k_Timpani = 0,      // 0  -sample: Orchestral-Timpani-C.wav (239Hz +/- 20Hz)
        k_Taiko,            // 1  -sample: Taiko-Hit.wav (1582Hz +/- 50Hz)
        k_NumPrograms       // 2 — marker (count)
    };

    KernelMaster state;

    inline int8_t Init(const unit_runtime_desc_t * desc) {
        // The Drumlogue is strictly 48 kHz stereo.
        if (desc->samplerate != (uint32_t)default_sample_rate) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;
        // The knobs LoadPreset does not write go to their header.c defaults.
        seed_performance_params();
        Reset();
        LoadPreset(0);
        return k_unit_err_none;
    }

    inline void Teardown() {}

    // RUNTIME state only — never a parameter's value (the SDK: unit_reset()
    // must not reset parameter values).  Both kettles' rings, the tilt EQ's
    // memory and the master filter's memory.
    inline void Reset() {
        m_drum_kernel.Flush();   // dense-kernel ring dies
        m_kernel_tone_lp = 0.0f;
        // Master filter is a LOWPASS "Cutoff" (mode 0); its coefficients are the
        // Cutoff/Resnc knobs and stay.  Only the filter's memory is cleared.
        state.master_filter.mode = 0;
        state.master_filter.lp = 0.0f;
        state.master_filter.bp = 0.0f;
        state.master_filter.hp = 0.0f;
    }

    inline void Resume() {}
    inline void Suspend() { AllNoteOff(); Reset(); }

    // ==============================================================================
    // 1. UI State & Preset Management
    // ==============================================================================
    int32_t m_params[k_lastParamIndex] = {0};
    uint8_t m_preset_idx = 0;

    // REFERENCE ANCHORS: the knob values the current preset shipped with,
    // captured in LoadPreset.  Every knob → kernel mapping is a delta from
    // these, so the shipped preset plays the approved render exactly.
    float   m_modal_dkay_ref   = 0.975f;
    float   m_modal_rel_ref    = 0.9f;
    float   m_modal_stiff_ref  = 0.26f;
    int32_t m_modal_partls_ref = 0;
    float   m_modal_inharm_ref = 0.0f;
    float   m_modal_mterl_ref  = 0.5f;
    float   m_modal_hitpos_ref = 0.26f;
    float   m_modal_tubrad_ref = 0.25f;
    float   m_modal_mltres_ref = 0.5f;
    float   m_kernel_vlstf_ref = 0.0f;
    float   m_kernel_vlres_ref = 0.0f;
    float   m_kernel_nzmix_ref = 0.0f;
    float   m_kernel_nzfrq_ref = 380.0f;

    // Single mono instance: one physical membrane, shared across hits.
    ModalDrumKernel m_drum_kernel;
    float   m_kernel_tone_lp   = 0.0f;   // Stage-4a tilt state for the kernel path

    inline int32_t getParameterValue(uint8_t index) const {
        if (index >= k_lastParamIndex) return 0;   // OS out-of-bounds guard
        return m_params[index];
    }
    inline uint8_t getPresetIndex() const { return m_preset_idx; }
    // The preset's root note and the note the sequencer path plays (root +
    // Pitch).  Host tools use these; nothing on the device reads them.
    inline uint8_t PresetRootNote() const { return m_root_note; }
    inline uint8_t PlayedNote() const     { return m_ui_note; }

    inline void LoadPreset(uint8_t idx) {
        if (idx >= k_NumPrograms) return;   // the tables are sized k_NumPrograms
        m_preset_idx = idx;
        // Keep m_params[0] in sync whichever way the preset was loaded.
        m_params[k_paramProgram] = idx;

        // One row per preset, one column per knob in ParamIndex order (the knob
        // values the preset ships with).
        // The Pitch column holds the preset's ROOT NOTE (MIDI), not a knob
        // value: the Pitch knob is an offset in semitones FROM it, so the
        // column is captured into m_root_note and never written to the knob.
        // Columns LoadPreset does NOT load are knobs the host owns:
        // Pitch, Velocity, VlMllRes, Tone, Cutoff, Resnc.
        // Their values below are never read (VlMllRes's is, once, as an anchor).
        // int16_t: the stored range is [-10, 1999].
        static constexpr int16_t presets[k_NumPrograms][k_lastParamIndex] = {
            //  Prg Root  Vel MlRs MlSt VlRs VlSt Ptls  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain NzMx NzFq Rsnc
            {   0,  52,   1, 360,  30,   0,  40,   2, 150,  10,   0,  36,  18,   1,1999,   0,   4,   1, 380,  71},   // 0: Timpani   — dense-kernel voice.  Note 52 (E3): the kettle's dominant sustained partial measures 165.5 Hz ≈ E3 (the 110 Hz A2 principal sits a fifth below) — the display now names the pitch you hear; 52 is also the kernel recipe root, so the shipped row plays the approved render at ratio 1
            {   1,  41,   1, 250,  45,   0,   0,   1, 120,  10,   0,  30,  15,   0,1999,  16,   5,  52, 800,  71}    // 1: Taiko     — bright open "TAAAN": data-driven inharmonic modes + bright 1472Hz partial (ratio 16.86, the "AAN" vowel) from Taiko-Hit.wav. Port retune: modal_mix 0.60 + brighter crack (NzMix 36→52, NzFltFrq 360→800 ≈8kHz) + leaner boom for a brighter, longer ring
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
        m_modal_partls_ref = (presets[idx][k_paramPartls] <= 4) ? presets[idx][k_paramPartls] : 0;
        m_modal_inharm_ref = fmaxf(-1.0f, fminf(1.0f, (float)presets[idx][k_paramInharm] * 0.01f));
        m_modal_mterl_ref  = (fmaxf(-10.0f, fminf(30.0f, (float)presets[idx][k_paramMterl])) + 10.0f) * 0.025f;
        m_modal_hitpos_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramHitPos] * 0.01f));
        m_modal_tubrad_ref = fmaxf(0.0f, fminf(20.0f, (float)presets[idx][k_paramTubRad])) * 0.05f;
        m_modal_mltres_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramMlltRes] * 0.001f));
        m_modal_rel_ref    = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramRel] * 0.05f));
        // ── Dense modal-drum kernel (Timpani/Taiko): bind the recipe behind
        // the approved standalone renders (105_timp_wedge / 110_taiko_wedge)
        // and capture the remaining shipped-value anchors.  ref_drive is the
        // master_drive the shipped Gain column produces; the kernel master
        // stage divides it back out so the shipped preset is transparent.
        m_kernel_vlstf_ref = (float)presets[idx][k_paramVlMllStf] * 0.01f;
        m_kernel_vlres_ref = (float)presets[idx][k_paramVlMllRes] * 0.01f;
        m_kernel_nzmix_ref = fmaxf(0.0f, fminf(1.0f, (float)presets[idx][k_paramNzMix] * 0.01f));
        // Stored unit (÷10 Hz), floored at the header minimum 2 — NOT at 20,
        // which is a Hz figure and does not belong to this unit.
        m_kernel_nzfrq_ref = fmaxf(2.0f, fminf(2000.0f, (float)presets[idx][k_paramNzFltFrq]));
        float ref_drive = 1.0f + fmaxf(0.0f, (float)presets[idx][k_paramGain] * 0.01f) * 20.0f;
        m_drum_kernel.Configure((idx == k_Timpani) ? &kTimpaniRecipe : &kTaikoRecipe, ref_drive);
        m_kernel_tone_lp = 0.0f;
        RefreshKernelMods();
    }

    static inline const char * getPresetName(uint8_t idx) {
        static const char* const preset_names[] = { "Timpni", "Taiko" };
        static_assert(sizeof(preset_names) / sizeof(preset_names[0]) == k_NumPrograms,
                      "one name per preset");
        if (idx < k_NumPrograms) return preset_names[idx];
        return "Unknown";
    }

    // ==============================================================================
    // 2. Parameter Binding (UI Thread)
    // ==============================================================================
    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= k_lastParamIndex) return;   // OS out-of-bounds guard
        m_params[index] = value;

        switch (index) {
            case k_paramProgram:
                LoadPreset((uint8_t)value);
                break;

            case k_paramPitch: {
                // Semitone offset from the preset's root note, bounded as the SDK
                // asks and stored clamped so the host reads back what plays.
                const int32_t p = (value < -kPitchRangeSemis) ? -kPitchRangeSemis
                                : (value >  kPitchRangeSemis) ?  kPitchRangeSemis
                                : value;
                m_params[index] = p;
                m_pitch_semis   = (int8_t)p;
                update_played_note();
                break;
            }

            case k_paramVelocity:
                // Global performance control: bias every incoming strike (see
                // vel_bias_apply).  0 is neutral.
                m_vel_bias = fmaxf(-1.0f, fminf(1.0f, (float)value * 0.01f));
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
                apply_master_filter();
                break;

            case k_paramGain: {
                float norm = fmaxf(0.0f, (float)value * 0.01f);
                state.master_drive = 1.0f + (norm * 20.0f);
                break;
            }

            default:
                break;
        }

        // Re-derive the kernel modifiers whenever any mapped knob changes.  Cheap
        // scalar math here; mods that move decay poles amortize inside
        // ModalDrumKernel::Process().  Pitch is NOT a mod: it rides each NoteOn
        // and retunes a kettle there.  (Program routes through LoadPreset, which
        // configures the kernel itself.)
        if (index != k_paramProgram && m_drum_kernel.IsActive()) {
            RefreshKernelMods();
        }
    }

    inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
        if (index == k_paramProgram) {
            return getPresetName((uint8_t)value);
        } else if (index == k_paramPartls) {
            // Partls is the density of the membrane fill around the measured
            // mode lines: show the fraction of the fill this position keeps
            // (the shipped position keeps all of it).
            static char pt_buf[8];
            const int pt = (value < 0) ? 0 : ((value > 4) ? 4 : (int)value);
            const float d = fminf(1.0f, knob_exp2(0.7f * (float)(pt - m_modal_partls_ref)));
            snprintf(pt_buf, sizeof(pt_buf), "%d%%", (int)(d * 100.0f + 0.5f));
            return pt_buf;
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
        } else if (index == k_paramNzFltFrq || index == k_paramCutoff) {
            static char hz_buf[10];
            int32_t hz = value * 10;
            if (hz >= 1000) {
                snprintf(hz_buf, sizeof(hz_buf), "%d.%dkHz", (int)(hz / 1000), (int)((hz % 1000) / 100));
            } else {
                snprintf(hz_buf, sizeof(hz_buf), "%dHz", (int)hz);
            }
            return hz_buf;
        } else if (index == k_paramResnc) {
            // Stored ÷10; show the real ×10 value (710-4000).
            static char rs_buf[8];
            snprintf(rs_buf, sizeof(rs_buf), "%d", (int)(value * 10));
            return rs_buf;
        }
        return "---";   // unconditional failsafe
    }

    // ==============================================================================
    // 3. Sequencer and MIDI Routing
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
    // keeps every preset and every render byte-identical.
    //   knob < 0 → ghost notes: down to x0.19 (velocity 127 lands at ~24)
    //   knob > 0 → wham:        up to x5.28, clamped at kVelWhamMax, so any
    //                           stroke above ~velocity 25 saturates to a
    //                           full-force hit no matter what the step stores.
    // Velocity is the unit's whole dynamics axis — it drives the strike impulse
    // and the knock — so biasing it here, once, carries through all of it.
    inline float vel_bias_apply(float vel_norm) const {
        if (m_vel_bias == 0.0f) return vel_norm;
        const float scaled = vel_norm * knob_exp2(2.4f * m_vel_bias);
        const float ceil_v = (m_vel_bias > 0.0f)
                             ? (1.0f + (kVelWhamMax - 1.0f) * m_vel_bias)
                             : 1.0f;
        return fmaxf(kVelGhostMin, fminf(ceil_v, scaled));
    }

    // Two kettles: a repeat of a kettle's note retriggers that drum in place
    // (energy accumulates — a roll); a new note takes the other kettle, retuned
    // synchronously, so the first ring is untouched.
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // The Velocity knob biases the strike ONCE, here.
        const float vel_norm = vel_bias_apply((float)velocity * 0.007874015f);
        float ratio = exp2f(((float)note - m_drum_kernel.RootNote()) * 0.0833333333f);
        m_drum_kernel.Trigger(note, ratio, vel_norm);
    }

    // The drums ring out under their measured decays: a note-off or gate-off
    // does nothing.
    inline void NoteOff(uint8_t note) { (void)note; }
    inline void GateOff() {}
    // AllNoteOff does not cut the kernel's ring either; Reset() / Suspend() is
    // what silences it.
    inline void AllNoteOff() {}
    // Pitch bend: nothing reads it (a new note retunes a kettle instead).
    inline void PitchBend(uint16_t bend) { (void)bend; }

    inline void GateOn(uint8_t velocity) {
        if (velocity == 0) {
            GateOff();
        } else {
            // The sequencer plays the preset's root note plus the Pitch knob.
            NoteOn(m_ui_note, velocity);
        }
    }

    // ==============================================================================
    // 4. The Master Audio Loop
    // ==============================================================================
    // The kernel renders the coupled resonator bank and this runs its OWN
    // master stage: Tone tilt → master filter → Gain (re-anchored so the
    // shipped row is transparent) → transparent peak limiter.  A soft clip
    // would compress the strike back into the body, so there is none.
    inline void processBlock(float* __restrict main_out, size_t frames) {
        // Hoist tone read outside the loop.  Every preset ships Tone=0: the
        // tilt EQ is skipped entirely then.
        const float tone_val = state.tone;
        const bool tone_active = (tone_val != 0.0f);

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
    }

// ==============================================================================
// PRIVATE METHODS
// ==============================================================================
private:
    // Map the Brachetti sound-design params onto the dense-kernel modifiers.
    // Every mapping is a DELTA from the preset's shipped value (the
    // REFERENCE-ANCHOR pattern), so the shipped rows play exactly as voiced
    // and every knob sculpts around that point.
    inline void RefreshKernelMods() {
        if (!m_drum_kernel.IsActive()) return;
        ModalDrumKernel::Mods md;

        // (Pitch is not a mod: the note travels with each NoteOn/Trigger and
        // retunes one kettle synchronously — see the kernel header.)

        // Dkay (0-200, ×0.005) + Rel (0-20, ×0.05): T60 multiplier.
        float dk = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramDkay] * 0.005f));
        float rl = fmaxf(0.0f, fminf(1.0f, (float)m_params[k_paramRel]  * 0.05f));
        md.decay_mult = knob_exp2(4.5f * (dk - m_modal_dkay_ref) + 3.2f * (rl - m_modal_rel_ref));

        // Mterl (−10..30): brighter material lets HF modes ring longer
        // (coefficient 3.0, a full material sweep); anchored at the shipped value.
        float mt = (fmaxf(-10.0f, fminf(30.0f, (float)m_params[k_paramMterl])) + 10.0f) * 0.025f;
        md.hf_decay_tilt = 3.0f * (mt - m_modal_mterl_ref);

        // TubRad (0-20, ×0.05): kettle/shell SIZE.  A bigger body rings a little
        // longer AND darker (more low-mode weight).  Anchored at the shipped
        // TubRad → bit-identical.
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
        // knock ("wham") gain.  VlMllRes is the HIT-PROMINENCE knob: raising it
        // BOOSTS the knock and FLATTENS its velocity curve (lower exponent), so the
        // hit stays tall even on soft strikes; lowering it buries the knock and
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

        // VlMllStf (±100 → ±1): velocity→sharpness depth, a normalised 0..1
        // amount.  Pivoted at the shipped value, with the delta divided by the knob
        // travel left on that side, so the knob reaches 0 and 1 exactly at its own
        // ends and needs no clamp; d == 0 at the shipped value gives 0.6 exactly.
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

        // NzFltFrq (stored ÷10, 2..2000): the noise wedge's start cutoff,
        // log-proportional either side of the shipped value and mapped so the
        // knob's ends land exactly on the coefficient's bounds (kNoiseCoefMin/Max)
        // and the shipped value on the recipe's own coefficient (factor 1.0).
        {
            const float fq  = fmaxf(2.0f, fminf(2000.0f, (float)m_params[k_paramNzFltFrq]));
            const float ref = m_kernel_nzfrq_ref;
            const float c0  = m_drum_kernel.NoiseCoefHi();
            if (fq == ref || c0 <= 0.0f) {
                md.noise_bright = 1.0f;
            } else if (fq > ref) {
                md.noise_bright = powf(ModalDrumKernel::kNoiseCoefMax / c0,
                                       logf(fq / ref) / logf(2000.0f / ref));
            } else {
                md.noise_bright = powf(ModalDrumKernel::kNoiseCoefMin / c0,
                                       logf(ref / fq) / logf(ref / 2.0f));
            }
        }

        m_drum_kernel.SetMods(md);
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

    // The knobs LoadPreset does NOT write — performance and master controls
    // the host owns — back to their header.c defaults.  Called once from
    // Init().  MUST track header.c's `.init` fields for these slots.
    inline void seed_performance_params() {
        static const int16_t kDefaults[][2] = {
            { k_paramPitch,    0    },
            { k_paramVelocity, 0    },
            { k_paramVlMllRes, 0    },
            { k_paramTone,     0    },
            { k_paramCutoff,   1999 },
            { k_paramResnc,    71   },
        };
        for (const auto& d : kDefaults) setParameter((uint8_t)d[0], d[1]);
    }

    float   m_master_cutoff = 16000.0f; // Master LP cutoff
    uint8_t m_root_note   = 52;   // preset 0 (Timpani) until LoadPreset runs
    int8_t  m_pitch_semis = 0;    // Pitch knob, ±kPitchRangeSemis
    uint8_t m_ui_note     = 52;
    float   m_vel_bias    = 0.0f; // Velocity knob, -1 (ghost) .. +1 (wham)
};

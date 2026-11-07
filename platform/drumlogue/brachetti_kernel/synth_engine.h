#pragma once
// Brachetti Kernel — synth engine of the brachetti_kernel unit.
//
// Timpani and Taiko on the dense coupled-resonator drum kernel
// (modal_drum_kernel.h: ~280 measured and fill modes, a recorded knock, a
// noise wedge, two kettles) with its own master stage.
//
//   presets.h            the knob layout and the presets
//   modal_drum_kernel.h  the drum (tables in modal_drum_data.h)
//   this file            the knobs → the kernel's modifiers, and the master stage
//
// The knob helpers and the master knobs (Tone, Cutoff / Resnc, Gain) are the
// ones every Brachetti unit shares (../brachetti_common).
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship with,
// each renders bit-identically to the same preset there; tools/brachetti_equiv
// checks it.
#include <cstddef>
#include <cstdint>

#include "../common/runtime.h"
#include "unit.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/master_stage.h"
#include "modal_drum_kernel.h"
#include "presets.h"

class alignas(16) BrachettiSynth {
public:
    static constexpr int k_NumPrograms = kNumPresets;

    // ==========================================================================
    // Lifecycle
    // ==========================================================================
    inline int8_t Init(const unit_runtime_desc_t* desc) {
        // The drumlogue is 48 kHz stereo; anything else is not this unit's host.
        if (desc->samplerate != (uint32_t)brc::kSampleRate) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;
        // The knobs LoadPreset does not write start at their header.c defaults.
        seed_performance_params();
        Reset();
        LoadPreset(0);
        return k_unit_err_none;
    }

    inline void Teardown() {}   // static memory only: nothing to free

    // A hard flush of RUNTIME state — never a parameter's value: both kettles'
    // rings, the tilt EQ's memory and the master filter's memory.
    inline void Reset() {
        m_kernel.Flush();
        m_tone_lp = 0.0f;
        m_master.clear_filter();
    }

    inline void Resume() {}
    inline void Suspend() { AllNoteOff(); Reset(); }

    // ==========================================================================
    // Presets
    // ==========================================================================
    static inline const char* getPresetName(uint8_t idx) { return preset_name(idx); }
    inline uint8_t getPresetIndex() const { return m_preset_idx; }

    // The current preset's root note and the note the sequencer plays (root +
    // Pitch).  Host tools use these; nothing on the device reads them.
    inline uint8_t PresetRootNote() const { return m_note.root; }
    inline uint8_t PlayedNote() const     { return m_note.note; }

    inline void LoadPreset(uint8_t idx) {
        if (idx >= kNumPresets) return;   // before anything is written
        m_preset_idx = idx;
        m_params[k_paramProgram] = idx;
        const int16_t* row = kPresetKnobs[idx];
        m_note.set_root((uint8_t)row[k_paramPitch]);   // the Pitch column is the root
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (loads_with_preset(p)) setParameter(p, row[p]);
        capture_anchors(row);

        // Bind the recipe behind the approved standalone render.  ref_drive is
        // the master drive the shipped Gain column produces; the master stage
        // divides it back out, so the shipped preset is transparent.
        const float ref_drive = 1.0f + fmaxf(0.0f, (float)row[k_paramGain] * 0.01f) * 20.0f;
        m_kernel.Configure(kRecipes[idx], ref_drive);
        m_tone_lp = 0.0f;
        refresh_kernel_mods();
    }

    // ==========================================================================
    // Knobs
    // ==========================================================================
    inline int32_t getParameterValue(uint8_t index) const {
        return (index < k_lastParamIndex) ? m_params[index] : 0;
    }

    inline void setParameter(uint8_t index, int32_t value) {
        if (index >= k_lastParamIndex) return;
        m_params[index] = value;
        switch (index) {
        case k_paramProgram:  LoadPreset((uint8_t)value); return;   // configures the kernel itself
        // Semitones from the preset's root; rides each NoteOn, which retunes a
        // kettle there.  Stored clamped.
        case k_paramPitch:    m_params[index] = m_note.set_pitch(value); break;
        case k_paramVelocity: m_velocity.set(value); break;
        // Stored ÷10 (0-200 = 0-2000), stored clamped.
        case k_paramDkay:     m_params[index] = brc::knob::clamp_int(value, 0, 200); break;
        case k_paramTone:     m_master.set_tone(value); break;
        case k_paramCutoff:
        case k_paramResnc:    m_master.set_filter(m_params[k_paramCutoff], m_params[k_paramResnc]); break;
        case k_paramGain:     m_master.set_gain(value); break;
        default: break;
        }
        // Every other knob is a kernel modifier: re-derive them all (cheap
        // scalar math; the mods that move decay poles are amortised inside
        // ModalDrumKernel::Process).
        refresh_kernel_mods();
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char partls_buf[8], stiff_buf[8], dkay_buf[8], nzfrq_buf[10], cutoff_buf[10], resnc_buf[8];
        switch (index) {
        case k_paramProgram:  return preset_name((uint8_t)value);   // the preset being browsed
        case k_paramPartls:   // the fraction of the membrane fill this position keeps
            snprintf(partls_buf, sizeof(partls_buf), "%d%%", (int)(fill_density(brc::knob::clamp_int(value, 0, 4)) * 100.0f + 0.5f));
            return partls_buf;
        case k_paramMlltStif: return brc::format_int(stiff_buf, sizeof(stiff_buf), (int)(value * 100));
        case k_paramDkay:     return brc::format_int(dkay_buf, sizeof(dkay_buf), (int)(value * 10));
        case k_paramNzFltFrq: return brc::format_hz(nzfrq_buf, sizeof(nzfrq_buf), value);
        case k_paramCutoff:   return brc::format_hz(cutoff_buf, sizeof(cutoff_buf), value);
        case k_paramResnc:    return brc::format_int(resnc_buf, sizeof(resnc_buf), (int)(value * 10));
        default:              return "---";
        }
    }

    // ==========================================================================
    // Notes
    // ==========================================================================
    // Two kettles: a repeat of a kettle's note retriggers that drum in place
    // (energy accumulates — a roll); a new note takes the other kettle,
    // retuned synchronously, so the first ring is untouched.
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // The Velocity knob biases the strike once, here: it drives the
        // strike impulse and the knock.
        const float vel = m_velocity.apply(brc::midi_velocity(velocity));
        const float ratio = exp2f(((float)note - m_kernel.RootNote()) * 0.0833333333f);
        m_kernel.Trigger(note, ratio, vel);
    }

    // The drums ring out under their measured decays: note-offs, gate-offs and
    // AllNoteOff do nothing (Reset / Suspend silence them).
    inline void NoteOff(uint8_t note) { (void)note; }
    inline void GateOff() {}
    inline void AllNoteOff() {}
    // A new note retunes a kettle instead of bending it.
    inline void PitchBend(uint16_t bend) { (void)bend; }

    inline void GateOn(uint8_t velocity) {
        if (velocity == 0) { GateOff(); return; }
        NoteOn(m_note.strike(), velocity);
    }

    // ==========================================================================
    // Audio
    // ==========================================================================
    // The kernel renders the kettles, then this unit's OWN master stage: Tone
    // tilt → master filter → Gain (re-anchored, so the shipped row is
    // transparent) → transparent peak limiter.  A soft clip would compress the
    // strike back into the body, so there is none.
    inline void processBlock(float* __restrict out, size_t frames) {
        const float tone = m_master.tone;   // read once per block
        const float drive_rel = m_master.drive / m_kernel.RefDrive();
        for (size_t done = 0; done < frames; ) {
            float mono[kChunk];
            const size_t todo = (frames - done > kChunk) ? kChunk : (frames - done);
            if (m_kernel.Process(mono, (int)todo)) {
                for (size_t i = 0; i < todo; ++i) write_stereo(out, done + i, master(mono[i], tone, drive_rel));
            } else {
                // The kernel is silent: keep the master filter flowing so there
                // is no step when the next hit lands.
                for (size_t i = 0; i < todo; ++i) write_stereo(out, done + i, clip(m_master.filter.process(0.0f)));
            }
            done += todo;
        }
    }

private:
    static constexpr size_t kChunk = 64;   // the kernel renders at most this many frames per call

    // ── Master stage ────────────────────────────────────────────────────────
    float master(float x, float tone, float drive_rel) {
        if (tone != 0.0f) x = brc::tone_tilt(x, m_tone_lp, tone);   // every preset ships Tone 0
        x = m_master.filter.process(x);
        x *= drive_rel;   // Gain around the shipped value
        // Unity below 0.85, a tanh knee above it.
        return clip(ModalDrumKernel::TanhKnee(x, 0.85f, 0.15f, 6.6666667f));
    }
    static float clip(float x) { return brc::clampf(x, -0.99f, 0.99f); }
    static void write_stereo(float* out, size_t frame, float x) {
        out[frame * 2]     = x;
        out[frame * 2 + 1] = x;
    }

    // ── Knobs → kernel modifiers (REFERENCE-ANCHORED) ───────────────────────
    // Every mapping is a DELTA from the preset's shipped value, so the shipped
    // rows play exactly as voiced and every knob sculpts around that point.
    // (Pitch is not a mod: the note travels with each NoteOn and retunes one
    // kettle synchronously.)
    void refresh_kernel_mods() {
        if (!m_kernel.IsActive()) return;
        ModalDrumKernel::Mods md;
        ring_mods(md);
        strike_mods(md);
        noise_mods(md);
        m_kernel.SetMods(md);
    }

    // The ring: how long, how bright, how stretched and how dense.
    void ring_mods(ModalDrumKernel::Mods& md) const {
        namespace knob = brc::knob;
        // Dkay + Rel: the T60 multiplier.
        const float dk = knob::decay(m_params[k_paramDkay]);
        const float rl = knob::release(m_params[k_paramRel]);
        md.decay_mult = brc::knob_exp2(4.5f * (dk - m_anchor.decay) + 3.2f * (rl - m_anchor.release));
        // Mterl: a brighter material lets the HF modes ring longer (3.0 is a
        // full material sweep).
        md.hf_decay_tilt = 3.0f * (knob::material(m_params[k_paramMterl]) - m_anchor.material);
        // TubRad: the kettle / shell SIZE.  A bigger body rings a little
        // longer AND darker (more low-mode weight).
        const float d_tr = knob::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius;
        md.decay_mult    *= brc::knob_exp2(1.9f * d_tr);
        md.hf_decay_tilt -= 1.0f * d_tr;
        // Inharm: stretches the upper modes away from the fundamental.
        md.stretch = 2.4f * (knob::inharm(m_params[k_paramInharm]) - m_anchor.inharm);
        // Partls: the density of the membrane fill.
        md.density = fill_density(m_params[k_paramPartls]);
    }

    // Partls (0-4) → the fraction of the membrane fill kept: the shipped
    // position keeps all of it (the approved wedge); lower thins it toward
    // the bare mode lines.  Out of range reads as the shipped position.
    float fill_density(int32_t partls) const {
        const int pt = (partls >= 0 && partls <= 4) ? (int)partls : m_anchor.partls;
        return fminf(1.0f, brc::knob_exp2(0.7f * (float)(pt - m_anchor.partls)));
    }

    // The strike: impulse sharpness and the knock.
    void strike_mods(ModalDrumKernel::Mods& md) const {
        namespace knob = brc::knob;
        // MlltStif: impulse sharpness (a shorter strike is brighter).
        md.exc_sharp = brc::knob_exp2(3.0f * (knob::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff));
        // MlltRes + HitPos (toward the edge is clickier) + VlMllRes: the knock
        // ("wham") gain.  VlMllRes is the HIT-PROMINENCE knob: raising it
        // boosts the knock AND flattens its velocity curve (a lower exponent),
        // so the hit stays tall on soft strikes; lowering it buries the knock
        // and steepens the accent response.
        const float mr = knob::mallet_res(m_params[k_paramMlltRes]);
        const float hp = knob::hit_pos(m_params[k_paramHitPos]);
        const float vr = knob::percent(m_params[k_paramVlMllRes]);
        md.knock_mult = brc::knob_exp2(3.4f * (mr - m_anchor.mallet_res) +
                                       1.9f * (hp - m_anchor.hit_pos) +
                                       3.0f * (vr - m_anchor.vel_res));
        md.vel_knock_exp = brc::clampf(1.5f - 1.0f * (vr - m_anchor.vel_res), 0.4f, 3.0f);
        // VlMllStf: the velocity → sharpness depth, 0..1.  The delta is divided
        // by the knob travel left on its side, so the knob reaches 0 and 1
        // exactly at its own ends and the shipped value gives 0.6 exactly.
        const float d = knob::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff;
        md.vel_sharp = (d >= 0.0f)
            ? 0.6f + (d / fmaxf(1e-6f, 1.0f - m_anchor.vel_stiff)) * 0.4f
            : 0.6f + (d / fmaxf(1e-6f, 1.0f + m_anchor.vel_stiff)) * 0.6f;
    }

    // The noise wedge: level and start cutoff.
    void noise_mods(ModalDrumKernel::Mods& md) const {
        // NzMix: the wedge level around the recipe's.  The additive term keeps
        // the knob alive when the recipe ships no noise (Timpani): opening it
        // adds the taiko-style grain / air layer.
        const float dnz = brc::knob::noise_mix(m_params[k_paramNzMix]) - m_noise_mix_anchor;
        md.noise_mult = brc::knob_exp2(3.0f * dnz);
        md.noise_add  = 0.30f * fmaxf(0.0f, dnz);
        md.noise_bright = noise_brightness(noise_freq(m_params[k_paramNzFltFrq]));
    }

    // NzFltFrq (stored ÷10, floored at the header minimum 2 — not at 20, which
    // is a Hz figure).
    static float noise_freq(int32_t v) { return brc::clampf((float)v, 2.0f, 2000.0f); }

    // NzFltFrq → the wedge's start-cutoff multiplier: log-proportional either
    // side of the shipped value, so the knob's ends land exactly on the
    // coefficient's bounds (kNoiseCoefMin / Max) and the shipped value on the
    // recipe's own coefficient (factor 1.0).
    float noise_brightness(float fq) const {
        const float ref = m_noise_freq_anchor;
        const float c0  = m_kernel.NoiseCoefHi();
        if (fq == ref || c0 <= 0.0f) return 1.0f;
        if (fq > ref)
            return powf(ModalDrumKernel::kNoiseCoefMax / c0, logf(fq / ref) / logf(2000.0f / ref));
        return powf(ModalDrumKernel::kNoiseCoefMin / c0, logf(ref / fq) / logf(ref / 2.0f));
    }

    // The shipped knob values every mapping pivots on.
    void capture_anchors(const int16_t* row) {
        namespace knob = brc::knob;
        m_anchor.decay        = knob::decay(row[k_paramDkay]);
        m_anchor.mallet_stiff = knob::mallet_stiff(row[k_paramMlltStif]);
        m_anchor.partls       = (row[k_paramPartls] <= 4) ? row[k_paramPartls] : 0;
        m_anchor.inharm       = knob::inharm(row[k_paramInharm]);
        m_anchor.material     = knob::material(row[k_paramMterl]);
        m_anchor.hit_pos      = knob::hit_pos(row[k_paramHitPos]);
        m_anchor.tube_radius  = knob::tube_radius(row[k_paramTubRad]);
        m_anchor.mallet_res   = knob::mallet_res(row[k_paramMlltRes]);
        m_anchor.release      = knob::release(row[k_paramRel]);
        m_anchor.vel_stiff    = knob::percent(row[k_paramVlMllStf]);
        m_anchor.vel_res      = knob::percent(row[k_paramVlMllRes]);
        m_noise_mix_anchor    = knob::noise_mix(row[k_paramNzMix]);
        m_noise_freq_anchor   = noise_freq(row[k_paramNzFltFrq]);
    }

    // The knobs the host owns keep their value across Program changes:
    // Pitch (its column is the root), Velocity, VlMllRes, Tone, Cutoff, Resnc.
    static bool loads_with_preset(uint8_t p) {
        switch (p) {
        case k_paramProgram: case k_paramPitch: case k_paramVelocity: case k_paramVlMllRes:
        case k_paramTone: case k_paramCutoff: case k_paramResnc:
            return false;
        default:
            return true;
        }
    }

    // The knobs LoadPreset does NOT write, to their header.c defaults (Init).
    // MUST track header.c's `.init` fields for these slots.
    void seed_performance_params() {
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

    // One mono instance: one physical drum (two kettles), shared across hits.
    ModalDrumKernel     m_kernel;
    float               m_tone_lp = 0.0f;   // Tone tilt EQ memory
    brc::MasterControls m_master;
    int32_t             m_params[k_lastParamIndex] = {0};   // raw knob values, as the host set them
    uint8_t             m_preset_idx = 0;
    brc::Anchors        m_anchor;
    float               m_noise_mix_anchor  = 0.0f;     // NzMix
    float               m_noise_freq_anchor = 380.0f;   // NzFltFrq, stored units
    brc::VelocityBias   m_velocity;
    brc::SequencerNote  m_note;
};

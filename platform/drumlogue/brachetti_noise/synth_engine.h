#pragma once
// Brachetti Noise — synth engine of the brachetti_noise unit.
//
// The pure-noise voices: Clap, Shaker and the closed hi-hat (HHat-C).  A
// voice is a shaped, filtered noise burst (two bands, two envelopes) with
// an enveloped-LFO amplitude gate (Clap's multi-burst, Shaker's grains)
// and a faint mallet.
//
//   presets.h   the knob layout, the presets and their voicing
//   voice.h     one voice
//   this file   presets and knobs → voices, voices → the master stage
//
// The exciter, the master stage and the knob helpers are the ones every
// Brachetti unit shares (../brachetti_common); this unit has its own knob
// layout, so it does not use the modal units' engine.
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship
// with, each renders bit-identically to the same preset there;
// tools/brachetti_equiv checks it.
#include <cstddef>
#include <cstdint>

#include "../common/runtime.h"
#include "unit.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, kSampleRate, knob_exp2, phase_inc
//   exciter.h        Exciter, noise_filter_name
//   knobs.h          Anchors, SequencerNote, VelocityBias, format_hz,
//                    format_int, is_host_knob, knob::mallet_res,
//                    knob::mallet_stiff, knob::percent, knob::poly,
//                    midi_velocity, seed_host_knobs
//   master_stage.h   MasterStage, tone_tilt
//   models.h         k_String, model_name
//   voice_base.h     any_fading, fade_out_all, fuses_with, kNumVoices,
//                    kRollFuseSec, next_voice, set_all_exciters
//   voice_layers.h   NoiseGate
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/exciter.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/models.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
#include "presets.h"
#include "voice.h"

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
        // The knobs LoadPreset does not write start at their header.c
        // defaults, and so does the state the noise-filter knobs own.
        brc::seed_host_knobs(*this, kHostKnobs);
        for (VoiceState& v : m_voices) v.exciter.init_noise_filter();
        Reset();
        LoadPreset(0);
        return k_unit_err_none;
    }

    inline void Teardown() {}   // static memory only: nothing to free

    // A hard flush of RUNTIME state — never a parameter's value.
    inline void Reset() {
        for (VoiceState& v : m_voices) {
            v.is_active    = false;
            v.is_releasing = false;
            v.reset_for_strike();
            v.exciter.clear_noise_filter();   // memory only: NzFltr / NzFltFrq stay
        }
        m_master.reset();
    }

    inline void Resume() {}
    inline void Suspend() { AllNoteOff(); Reset(); }

    // ==========================================================================
    // Presets
    // ==========================================================================
    static inline const char* getPresetName(uint8_t idx) { return preset_name(idx); }
    inline uint8_t getPresetIndex() const { return m_preset_idx; }

    // The note the sequencer plays (the preset's root; there is no Pitch
    // knob).  Host tools use these; nothing on the device reads them.
    inline uint8_t PresetRootNote() const { return m_note.root; }
    inline uint8_t PlayedNote() const     { return m_note.note; }

    inline void LoadPreset(uint8_t idx) {
        if (idx >= kNumPresets) return;   // before anything is written

        // A voice cannot survive a preset change: it fades out (~10 ms) under
        // its own settings.  Only on a REAL change.
        const bool changed = (idx != m_preset_idx);
        const float prev_drive = m_master.drive;
        const float prev_trim  = m_master.out_trim;
        if (changed) brc::fade_out_all(m_voices);

        m_preset_idx = idx;
        m_params[k_paramProgram] = idx;
        const int16_t* row = kPresetKnobs[idx];
        m_note.set_root((uint8_t)row[kRootColumn]);
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (p != k_paramProgram && !brc::is_host_knob(kHostKnobs, p)) setParameter(p, row[p]);
        brc::set_all_exciters(m_voices, &brc::Exciter::set_release_time, row[kDecayColumn]);

        // REFERENCE ANCHORS for the gate's knob mappings and the strike.
        m_anchor.mallet_stiff = brc::knob::mallet_stiff(row[k_paramMlltStif]);
        m_anchor.mallet_res   = brc::knob::mallet_res(row[k_paramMlltRes]);
        m_anchor.vel_stiff    = brc::knob::percent(row[k_paramVlMllStf]);

        m_master.out_trim = kOutTrim[idx];
        if (changed) m_master.hold_while_fading(prev_drive, prev_trim, brc::any_fading(m_voices));

        // The noise split corner is set only here, into every voice that is
        // not fading out.
        for (VoiceState& v : m_voices) {
            if (v.fading()) continue;
            v.exciter.noise_lp_state    = 0.0f;
            v.exciter.noise_band_mix    = voicing().noise_band_mix;
            v.exciter.noise_hi_lp_state = 0.0f;
            v.exciter.noise_hi_lp_coeff = voicing().noise_split;
        }
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
        case k_paramProgram:  LoadPreset((uint8_t)value); break;
        case k_paramPoly:     m_poly = brc::knob::poly(value); break;
        case k_paramVelocity: m_velocity.set(value); break;
        case k_paramMlltRes:  brc::set_all_exciters(m_voices, &brc::Exciter::set_mallet_res, value); break;
        case k_paramMlltStif: brc::set_all_exciters(m_voices, &brc::Exciter::set_mallet_stiff, value); break;
        case k_paramModel:    m_model = (uint8_t)value; break;
        case k_paramTone:     m_master.set_tone(value); break;
        case k_paramRel:      brc::set_all_exciters(m_voices, &brc::Exciter::set_release, value); break;
        case k_paramCutoff:
        case k_paramResnc:    m_master.set_filter(m_params[k_paramCutoff], m_params[k_paramResnc]); break;
        case k_paramGain:     m_master.set_gain(value); break;
        case k_paramNzMix:    brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_mix, value); break;
        case k_paramNzRes:    brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_res, value); break;
        case k_paramNzFltr:   brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_filter_mode, value); break;
        case k_paramNzFltFrq: brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_filter_freq, value); break;
        default: break;   // VlMllRes, VlMllStf, HitPos: read at the strike
        }
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char stiff_buf[8], nzfrq_buf[10], cutoff_buf[10], resnc_buf[8], poly_buf[8];
        const char* s = nullptr;
        switch (index) {
        case k_paramProgram:  return preset_name((uint8_t)value);   // the preset being browsed
        case k_paramModel:    s = brc::model_name(value); break;
        case k_paramNzFltr:   s = brc::noise_filter_name(value); break;
        case k_paramMlltStif: return brc::format_int(stiff_buf, sizeof(stiff_buf), (int)(value * 100));
        case k_paramNzFltFrq: return brc::format_hz(nzfrq_buf, sizeof(nzfrq_buf), value);
        case k_paramCutoff:   return brc::format_hz(cutoff_buf, sizeof(cutoff_buf), value);
        case k_paramResnc:    return brc::format_int(resnc_buf, sizeof(resnc_buf), (int)(value * 10));
        case k_paramPoly:     return brc::format_int(poly_buf, sizeof(poly_buf), brc::knob::poly(value));
        default: break;
        }
        return s ? s : "---";
    }

    // ==========================================================================
    // Notes
    // ==========================================================================
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // Strokes on the same note inside kRollFuseSec are one roll.
        if (!brc::fuses_with(m_voices[m_last_voice], note, brc::kRollFuseSec))
            m_last_voice = brc::next_voice(m_last_voice, m_poly);
        VoiceState& v = m_voices[m_last_voice];
        v.start(note, m_velocity.apply(brc::midi_velocity(velocity)));
        v.exciter.shape_strike(v.current_velocity, m_params[k_paramHitPos],
                               m_params[k_paramMlltStif], m_params[k_paramVlMllStf],
                               m_anchor.vel_stiff, m_params[k_paramNzRes],
                               m_params[k_paramVlMllRes]);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        gate_noise(v);
    }

    // Releases the noise into its Rel tail.
    inline void NoteOff(uint8_t note) {
        for (VoiceState& v : m_voices) {
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;
                v.exciter.release_noise();
            }
        }
    }

    inline void GateOn(uint8_t velocity) {
        if (velocity == 0) { GateOff(); return; }
        NoteOn(m_note.strike(), velocity);
    }
    inline void GateOff() { NoteOff(m_note.gate_note); }

    // Panic: everything fades out at the preset's decay setting.
    inline void AllNoteOff() {
        for (VoiceState& v : m_voices) {
            v.is_releasing = true;
            v.exciter.release_noise();
            v.exciter.master_env.release();
        }
    }

    inline void PitchBend(uint16_t bend) { (void)bend; }   // nothing depends on pitch

    // ==========================================================================
    // Audio
    // ==========================================================================
    inline void processBlock(float* __restrict out, size_t frames) {
        brc::MasterStage::clear(out, frames);
        const float tone = m_master.tone;   // read once per block
        m_master.release_held(m_voices);

        bool any_voice = false;
        for (VoiceState& v : m_voices) {
            if (!v.is_active) continue;
            any_voice = true;
            render_voice(v, out, frames, tone);
        }
        if (!m_master.should_run(any_voice)) return;
        m_master.render(out, frames);
    }

private:
    // The noise level beside the exciter's mallet (×5 brings it into the
    // ballpark of the ×15 mallet).
    static constexpr float kNoiseGain = 5.0f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }



    // The gate that makes a clap or a shaker out of the noise burst, with the
    // mallet knobs on it (REFERENCE-ANCHORED): MlltStif sets the burst / grain
    // RATE (±2 octaves), MlltRes their contrast (the gate's depth).
    void gate_noise(VoiceState& v) const {
        brc::NoiseGate& g = v.gate;
        if (m_preset_idx == k_Clap) {
            // A clap is several hands landing a few ms apart: ~55 Hz bursts
            // whose contrast fades over ~15 ms into the plain "tcha" tail.
            g.depth = 0.95f;
            g.inc   = brc::phase_inc(55.0f);
            g.decay = 0.99860f;
            g.phase = M_3_PI_2;   // full level on frame 0
        }
        if (m_preset_idx == k_Shaker) {
            // A hand shaker's bead-to-wall rate under a fast wrist is
            // ~25-35 Hz; slower reads as a slow rattle rather than shaking.
            // The rattle does NOT fade: a longer Rel gives a longer RATTLE,
            // not a smooth hiss.  And a shaker swells over ~15 ms — at full
            // level in < 1 ms the burst reads as a hit.
            g.depth = 0.92f;
            g.inc   = brc::phase_inc(28.0f);
            g.decay = 1.0f;
            g.phase = M_3_PI_2;
            v.exciter.noise_env.attack_rate    = 0.004f;
            v.exciter.noise_env_hi.attack_rate = 0.004f;
        }
        if (g.inc > 0.0f) {
            g.inc *= brc::knob_exp2(2.0f * (brc::knob::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff));
            g.depth = brc::clampf(
                g.depth * brc::knob_exp2(1.5f * (brc::knob::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res)),
                0.0f, 0.99f);
        }
    }

    // One voice for one block, added into the mono bus (out[i * 2]).
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            const float exciter_sig = v.exciter.process();
            float x = exciter_sig * v.current_velocity;
            float noise_gain = kNoiseGain;
            if (v.gate.active()) noise_gain *= v.gate.gain();
            x += v.exciter.noise_out_sample * noise_gain * v.current_velocity;
            x = v.follow_and_gate(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            x = v.fade_out(x);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            v.retire_when_done();
        }
    }


    VoiceState         m_voices[brc::kNumVoices];
    uint8_t            m_last_voice = 0;   // the slot the last strike took
    brc::MasterStage   m_master;
    int32_t            m_params[k_lastParamIndex] = {0};   // raw knob values, as the host set them
    uint8_t            m_preset_idx = 0;
    brc::Anchors       m_anchor;
    brc::VelocityBias  m_velocity;
    brc::SequencerNote m_note;
    uint8_t            m_poly  = 4;              // Poly knob: voice cap 1-4
    uint8_t            m_model = brc::k_String;  // Model knob
};

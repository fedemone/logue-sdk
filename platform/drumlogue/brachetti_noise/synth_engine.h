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
//   common_knobs.h   CommonKnobs, common_knob_text, is_common_knob
//   dsp_math.h       clampf, kSampleRate, knob_exp2, phase_inc
//   exciter.h        Exciter, noise_filter_name
//   knobs.h          Anchors, format_hz, format_int, is_host_knob,
//                    kKnobTextSize, knob::mallet_res, knob::mallet_stiff,
//                    knob::percent, knob_text, midi_velocity, seed_host_knobs
//   master_stage.h   MasterStage, tone_tilt
//   models.h         k_String
//   voice_base.h     any_fading, fade_out_all, fuses_with, kNumVoices,
//                    kRollFuseSec, next_voice, set_all_exciters
//   voice_layers.h   NoiseGate, punch_amount
#include "../brachetti_common/common_knobs.h"
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
    inline uint8_t PresetRootNote() const { return m_common.note.root; }
    inline uint8_t PlayedNote() const     { return m_common.note.note; }

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
        m_common.note.set_root((uint8_t)row[kRootColumn]);
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (p != k_paramProgram && !brc::is_host_knob(kHostKnobs, p)) setParameter(p, row[p]);
        brc::set_all_exciters(m_voices, &brc::Exciter::set_release_time, row[kDecayColumn]);

        // REFERENCE ANCHORS for the gate's knob mappings and the strike.
        m_anchor.mallet_stiff = brc::knob::mallet_stiff(row[k_paramGrnRate]);
        m_anchor.mallet_res   = brc::knob::mallet_res(row[k_paramGrnDepth]);
        m_anchor.vel_stiff    = brc::knob::percent(kShippedVelKnob);
        m_model               = kShippedModel[idx];   // the noise band balance's default

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
        if (index == k_paramProgram) { LoadPreset((uint8_t)value); return; }
        if (index == k_paramNoPitch) return;   // a blank slot
        if (brc::is_common_knob(index)) {
            m_params[index] = m_common.set(index, value, m_params, m_master);
            return;
        }
        switch (index) {
        // The grain knobs are the exciter's mallet knobs too (the mallet is
        // mostly crossfaded out under NzMix).
        case k_paramGrnDepth: brc::set_all_exciters(m_voices, &brc::Exciter::set_mallet_res, value); break;
        case k_paramGrnRate:  brc::set_all_exciters(m_voices, &brc::Exciter::set_mallet_stiff, value); break;
        case k_paramRel:      brc::set_all_exciters(m_voices, &brc::Exciter::set_release, value); break;
        case k_paramNzMix:    brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_mix, value); break;
        case k_paramNzRes:    brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_res, value); break;
        case k_paramNzFltr:   brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_filter_mode, value); break;
        case k_paramNzFltFrq: brc::set_all_exciters(m_voices, &brc::Exciter::set_noise_filter_freq, value); break;
        default: break;   // VlMllRes, VlMllStf, HitPos: read at the strike
        }
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        if (index == k_paramProgram) return preset_name((uint8_t)value);   // the preset being browsed
        if (brc::is_common_knob(index)) {
            const char* c = brc::common_knob_text(index, value);
            return c ? c : "---";
        }
        const char* s = nullptr;
        switch (index) {
        case k_paramNzFltr:   s = brc::noise_filter_name(value); break;
        case k_paramGrnRate:  return brc::format_int(brc::knob_text(index), brc::kKnobTextSize, (int)(value * 100));
        case k_paramNzFltFrq: return brc::format_hz(brc::knob_text(index), brc::kKnobTextSize, value);
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
            m_last_voice = brc::next_voice(m_last_voice, m_common.poly);
        VoiceState& v = m_voices[m_last_voice];
        v.start(note, m_common.velocity.apply(brc::midi_velocity(velocity)));
        v.exciter.shape_strike(v.current_velocity, kShippedHitPos,
                               m_params[k_paramGrnRate], kShippedVelKnob,
                               m_anchor.vel_stiff, m_params[k_paramNzRes],
                               kShippedVelKnob);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        gate_noise(v);
        shape_noise(v);
        v.punch.trigger(brc::punch_amount(m_params[k_paramPunch]));
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
        NoteOn(m_common.note.strike(), velocity);
    }
    inline void GateOff() { NoteOff(m_common.note.gate_note); }

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
        if (voicing().gate_hz > 0.0f) {
            g.depth = voicing().gate_depth;
            g.inc   = brc::phase_inc(voicing().gate_hz);
            g.decay = voicing().gate_decay;
            g.phase = M_3_PI_2;   // full level on frame 0
        }
        if (voicing().attack_rate > 0.0f) {
            v.exciter.noise_env.attack_rate    = voicing().attack_rate;
            v.exciter.noise_env_hi.attack_rate = voicing().attack_rate;
        }
        // GrnDpth above its shipped value also ADDS contrast (up to +0.9), so
        // it reaches the presets that ship a shallow gate or none: a preset
        // without one gets a 30 Hz grain that never fades (GrnRate sets its
        // rate, GrnFade lets it fade).
        const float d_depth = brc::knob::mallet_res(m_params[k_paramGrnDepth]) - m_anchor.mallet_res;
        if (!(g.inc > 0.0f) && d_depth > 0.001f) {
            g.depth = 0.0f;
            g.inc   = brc::phase_inc(30.0f);
            g.decay = 1.0f;
            g.phase = M_3_PI_2;
        }
        if (g.inc > 0.0f) {
            g.inc *= brc::knob_exp2(2.0f * (brc::knob::mallet_stiff(m_params[k_paramGrnRate]) - m_anchor.mallet_stiff));
            g.depth = brc::clampf(g.depth * brc::knob_exp2(1.5f * d_depth) + fmaxf(0.0f, d_depth) * 1.8f,
                                  0.0f, 0.99f);
        }
    }

    // ── This family's own knobs: GrnFade, Swell, Band, VelTone ──────────────
    // Each is bipolar around 0, the value every preset ships, where it is the
    // identity: the presets play as voiced.
    static float bipolar(int32_t v) { return brc::clampf((float)v * 0.01f, -1.0f, 1.0f); }

    void shape_noise(VoiceState& v) const {
        // GrnFade: how fast the grain contrast fades into the plain hiss.
        // (1 − decay) scales ×1/8 .. ×8, and above 0 a fade is ADDED (τ down
        // to ~40 ms), so the shakers whose grains never fade can fade too.
        const float fade = bipolar(m_params[k_paramGrnFade]);
        if (fade != 0.0f && v.gate.inc > 0.0f) {
            const float one_minus = (1.0f - v.gate.decay) * brc::knob_exp2(3.0f * fade)
                                  + fmaxf(0.0f, fade) * 0.0005f;
            v.gate.decay = brc::clampf(1.0f - one_minus, 0.99f, 1.0f);
        }
        // Swell: the noise's rise time ×1/32 .. ×32, up to ~70 ms (both
        // bands: the attack rate is a one-pole coefficient, time ∝ 1 / rate).
        const float swell = bipolar(m_params[k_paramSwell]);
        if (swell != 0.0f) {
            const float k = brc::knob_exp2(-5.0f * swell);
            v.exciter.noise_env.attack_rate    = brc::clampf(v.exciter.noise_env.attack_rate * k, 0.0003f, 0.99f);
            v.exciter.noise_env_hi.attack_rate = brc::clampf(v.exciter.noise_env_hi.attack_rate * k, 0.0003f, 0.99f);
        }
        // Band: the body / click balance, scaled into the headroom on its
        // side (−1 all body, +1 all click).
        const float band = bipolar(m_params[k_paramBand]);
        if (band != 0.0f) {
            const float m = v.exciter.noise_band_mix;
            v.exciter.noise_band_mix = brc::clampf(m + band * ((band > 0.0f) ? (1.0f - m) : m), 0.0f, 1.0f);
        }
        // VelTone: velocity → the noise filter's cutoff, up to ±2 octaves
        // between a full stroke and a ghost, pivoting on velocity 0.75.  The
        // voice's filter is retuned only while it is in use (and once more
        // when it stops, back to the knob's value).
        const float vt = bipolar(m_params[k_paramVelTone]);
        if (vt != 0.0f || v.tone_moved) {
            if (vt == 0.0f) {
                v.exciter.set_noise_filter_freq(m_params[k_paramNzFltFrq]);
            } else {
                const float vel = brc::clampf(v.current_velocity, 0.0f, 1.0f);
                const float hz = brc::clampf((float)m_params[k_paramNzFltFrq] * 10.0f
                                             * brc::knob_exp2(4.0f * vt * (vel - 0.75f)), 20.0f, 20000.0f);
                v.exciter.noise_filter.set_coeffs(hz, 0.707f, brc::kSampleRate);
            }
            v.tone_moved = (vt != 0.0f);
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
            x = v.punch.apply(x);
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
    brc::CommonKnobs   m_common;                 // Poly, Velocity (no Pitch)
    uint8_t            m_model = brc::k_String;  // the preset's noise band default (kShippedModel)
};

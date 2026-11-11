#pragma once
// Brachetti Tom — synth engine of the brachetti_tom unit.
//
// Toms and hand drums: Djambe, Ac Tom, DeepBs, Conga, Handpan, Bongo and
// RackTom.  A voice is a mallet + noise exciter, a membrane modal bank, a
// boom oscillator (Ac Tom, DeepBs, Bongo, RackTom — RackTom's glides) and a
// stick / hand-slap transient.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice and its layers
//   this file   the strike, the tom's knob mappings and the per-sample voice
//
// Knobs, preset loading, note routing and the master stage are the modal
// engine every Brachetti modal unit shares: ../brachetti_common/modal_synth.h.
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship
// with, each renders bit-identically to the same preset there;
// tools/brachetti_equiv checks it.
#include <cstddef>
#include <cstdint>

#include "../common/runtime.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, knob_exp2
//   knobs.h          knob::percent, off_anchor
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalSynth
//   voice_base.h     kRollFuseSec
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"
#include "../brachetti_common/voice_base.h"
#include "presets.h"
#include "voice.h"

class alignas(16) BrachettiSynth : public brc::ModalSynth<BrachettiSynth, VoiceState> {
public:
    // ── The presets (presets.h), as the modal engine reads them ─────────────
    static constexpr int k_NumPrograms = kNumPresets;
    static const char* getPresetName(uint8_t idx) { return preset_name(idx); }
    static const char* preset_name(uint8_t idx) { return ::preset_name(idx); }
    static const int16_t* preset_knobs(uint8_t idx) { return kPresetKnobs[idx]; }
    static const brc::ModalPresetConfig& modal_config(uint8_t idx) { return kModalConfigs[idx]; }
    static float out_trim(uint8_t idx) { return kOutTrim[idx]; }

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        VoiceState& v = allocate_voice(note, roll_window());
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        // Only RackTom's boom glides with the pitch envelope; a strike resets
        // it, so it is restored here (adding a preset here changes its sound).
        if (m_preset_idx == k_RackTom)
            v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        v.onset.trigger(voicing().onset_ms);
        if (voicing().chirp_hz > 0.0f) v.chirp.trigger(voicing().chirp_hz);
        // A wider shell (TubRad) also feeds more energy into the boom.
        v.boom.mix *= strike_modal_bank(v, voicing().modal_mix);
        trigger_slap(v, note);
        dial_slap(v);
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            float exciter_sig = v.exciter.process();
            if (v.chirp.active()) exciter_sig += v.chirp.process();
            float x = exciter_sig * v.current_velocity;
            v.pitch.process();
            x += v.exciter.noise_out_sample * kNoiseGain * v.current_velocity;
            if (v.slap.active()) x += v.slap.process(v.exciter.noise_gen) * v.current_velocity;
            if (v.boom.active()) {
                // RackTom's THUMP is a pitch bend, not a mode: one partial
                // sliding 160 → 110 Hz (τ ≈ 55 ms) in rock-rack-tom-1.wav, at
                // rest on 175 Hz at the shipped note; boom.tune carries the
                // note ratio, so the whole drum still transposes.
                if (m_preset_idx == k_RackTom)
                    v.boom.set_freq((174.61f + v.pitch.amount * v.pitch.env) * v.boom.tune);
                x += v.boom.process() * v.current_velocity;
            }
            if (v.modal.active) {
                const float modes = v.modal.process();
                x += modes * v.chirp.open_modal(v.modal.mix) * kModalGain;
            }
            x = v.follow_and_gate(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            x = v.onset.apply(x);
            x = v.fade_out(x);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            v.retire_when_done();
        }
    }

    // ── LoadPreset's per-voice coefficients ─────────────────────────────────
    // The noise split corner is set only here.
    void load_voicing_into(VoiceState& v) {
        v.exciter.noise_lp_state    = 0.0f;
        v.exciter.noise_band_mix    = voicing().noise_band_mix;
        v.exciter.noise_hi_lp_state = 0.0f;
        v.exciter.noise_hi_lp_coeff = voicing().noise_split;
        v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        load_modal_bank(v);
    }

private:
    // The noise burst runs in PARALLEL with the body, keeping the broadband
    // character a resonator would pitch-filter away; ×5 brings it into the
    // ballpark of the ×15 mallet.  The modal bank was voiced at ×5.
    static constexpr float kNoiseGain = 5.0f;
    static constexpr float kModalGain = 5.0f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }

    // Roll fusion: strokes on the same note inside this window are one
    // pressed roll.  DeepBs keeps ONE body out to 300 ms: overlapping bodies
    // of the longest membrane here turn the low end to porridge — and a real
    // bass drum cannot do that, the beater damps the head it just struck.
    float roll_window() const {
        return (m_preset_idx == k_Taiko2) ? 0.300f : brc::kRollFuseSec;
    }

    // The broadband attack the modal bank cannot make.
    void trigger_slap(VoiceState& v, uint8_t note) const {
        if (m_preset_idx == k_AcousticTom) {
            // The close-room reference's bright stick contact (~1.5 kHz
            // centroid): ~1.1-4.3 kHz, T60 ≈ 30 ms.
            v.slap.trigger(1.0f, 0.99539f, 3.00f, 0.130f, 0.420f);
        } else if (m_preset_idx == k_RackTom) {
            // Measured from rock-rack-tom-1.wav: a stick on a tuned tom head
            // is mostly a MID thwack (12 % of the 0-30 ms energy in 300 Hz-
            // 1 kHz, under 3 % in 1-6 kHz), so the band is ~250 Hz-1.15 kHz.
            // The gain must stay small: a burst big enough to chase that 12 %
            // pins the master limiter, which ducks the hit and recovers into a
            // swell; 2.5 is the last value that still decays monotonically.
            v.slap.trigger(1.0f, 0.99539f, 2.5f, 0.030f, 0.135f);
            // The glide is absolute Hz: fold the note ratio into boom.tune so
            // RackTom tracks its note.  (After the strike's reset, which sets
            // tune back to 1.0.)
            v.boom.tune = exp2f(((float)note - 53.0f) * (1.0f / 12.0f));
        }
    }

    // VlMllRes and VlMllStf on the SLAP (REFERENCE-ANCHORED; every tom ships
    // both at 0).  VlMllRes = velocity-weighted slap PROMINENCE: it scales a
    // shipped slap, or raises a hand-slap burst (~1-4.6 kHz, T60 ≈ 21 ms) on a
    // preset without one.  VlMllStf = slap SHARPNESS: brighter and shorter
    // up, rounder and longer down.
    void dial_slap(VoiceState& v) const {
        const float vel   = brc::clampf(v.current_velocity, 0.0f, 1.0f);
        const float d_vr  = brc::knob::percent(m_params[k_paramVlMllRes]) - m_anchor.vel_res;
        const float d_vs  = brc::knob::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff;
        if (brc::off_anchor(d_vr)) {
            if (v.slap.env > 0.0005f) {
                v.slap.gain = fminf(8.0f, v.slap.gain * brc::knob_exp2(3.0f * d_vr * (0.4f + 0.6f * vel)));
            } else if (d_vr > 0.0f) {
                v.slap.trigger(fminf(2.0f, d_vr * (0.8f + 1.9f * vel)), 0.99340f, 3.20f, 0.120f, 0.450f);
            }
        }
        if (brc::off_anchor(d_vs) && v.slap.env > 0.0005f) {
            const float snap = d_vs * (0.5f + 0.5f * vel);
            v.slap.a_lo = brc::clampf(v.slap.a_lo * brc::knob_exp2(1.2f * snap), 0.05f, 0.45f);
            v.slap.a_hi = brc::clampf(v.slap.a_hi * brc::knob_exp2(0.8f * snap), 0.15f, 0.80f);
            const float one_minus = (1.0f - v.slap.decay) * brc::knob_exp2(1.2f * snap);
            v.slap.decay = brc::clampf(1.0f - one_minus, 0.98500f, 0.99950f);
        }
    }
};

#pragma once
// Brachetti Plate — synth engine of the brachetti_plate unit.
//
// Small metal percussion: Cowbell, Triangle, Bell Tree and Tick.  A voice
// is a mallet + noise exciter driving an inharmonic plate modal bank, with
// noise that follows the ring down, a metallic FM chirp, a high-band
// shimmer branch and (Tick) a dedicated hat filter.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice and its layers
//   this file   the strike and the per-sample voice
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
//   dsp_math.h       clampf, kSampleRate, knob_exp2, or_default
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalSynth, kFamilyKnob
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"
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

    // Chirp sits in VlMllRes' slot (presets.h); VlMllRes then plays at 0,
    // the value every plate shipped (the default fixed_knob).
    static constexpr uint8_t role_at(uint8_t slot) {
        return (slot == k_paramChirp) ? brc::kFamilyKnob : slot;
    }

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        // Every strike stacks: for a sustained plate the overlap IS the sound
        // (ringing bells), so there is no roll fusion.
        VoiceState& v = allocate_voice(note, 0.0f);
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        // Tick ships a band mix of 0.86: above 0.8 its hat filter takes over
        // the click band (see VoiceState::excite).
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        if (voicing().shimmer_mix > 0.0f) v.shimmer.trigger(voicing().shimmer_mix, voicing().shimmer_decay);
        if (voicing().click_decay > 0.0f) v.exciter.noise_env_hi.decay_rate = voicing().click_decay;
        tune_hat(v);
        v.onset.trigger(voicing().onset_ms);
        v.chirp.trigger(voicing().chirp_hz);   // every plate preset but BelTre chirps
        dial_chirp(v);
        strike_modal_bank(v, voicing().modal_mix);
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        const float noise_gain = voicing().noise_gain;
        for (size_t i = 0; i < frames; ++i) {
            float exciter_sig = v.excite();
            if (v.chirp.active()) exciter_sig += v.chirp.process();
            float x = exciter_sig * v.current_velocity;
            const float gain = v.ring_gated(noise_gain);
            x += v.exciter.noise_out_sample * gain * v.current_velocity;
            if (v.shimmer.active()) x += v.shimmer.process(v.exciter.noise_out_sample) * v.current_velocity;
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
    // The noise split corner and the hat switch are set only here.
    void load_voicing_into(VoiceState& v) {
        v.exciter.noise_lp_state    = 0.0f;
        v.exciter.noise_band_mix    = voicing().noise_band_mix;
        v.exciter.noise_hi_lp_state = 0.0f;
        v.exciter.noise_hi_lp_coeff = voicing().noise_split;
        v.use_hat                   = voicing().hat;
        load_modal_bank(v);
    }

private:
    static constexpr float kModalGain = 5.0f;   // the bank's voicing level

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }

    // Chirp: the metallic FM chirp on the strike (bipolar around 0, every
    // preset's value, the identity there).  Its FM depth scales ×1/8 .. ×8
    // and, above 0, its sweep lasts up to 4× longer; BelTre, which ships
    // none, gets one at 1.5 kHz.  The chirp also lifts the bank while it
    // sounds (MetalChirp::open_modal).
    void dial_chirp(VoiceState& v) const {
        const float k = brc::clampf((float)m_params[k_paramChirp] * 0.01f, -1.0f, 1.0f);
        if (k == 0.0f) return;
        if (!(voicing().chirp_hz > 0.0f)) {
            if (k <= 0.0f) return;
            v.chirp.trigger(1500.0f);
        }
        v.chirp.depth *= brc::knob_exp2(3.0f * k);
        if (k > 0.0f) v.chirp.decay = 1.0f - (1.0f - v.chirp.decay) / (1.0f + 3.0f * k);
    }

    // The preset's hat-filter tuning, on a voice that uses the filter.
    void tune_hat(VoiceState& v) const {
        if (!v.use_hat || !(voicing().hat_hz > 0.0f)) return;
        const float q = brc::or_default(voicing().hat_q, 0.1f, 1.1f);
        v.hat.mode = (int)roundf(voicing().hat_mode);
        v.hat.set_coeffs(voicing().hat_hz, q, brc::kSampleRate);
    }
};

#pragma once
// Brachetti Bar — synth engine of the brachetti_bar unit.
//
// Mallet and struck-bar instruments: Marimba, Tubular Bell, Vibraphone,
// Woodblock, Kalimba, Steel Pan, Claves, Glass Bowl, Slit Drum and Glass
// Bottle.  A voice is a mallet + noise exciter driving a free-bar modal bank
// (up to six modes), plus a metallic FM chirp on Steel Pan.
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

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        // Every strike stacks: for a sustained bar the overlap IS the sound
        // (a marimba roll), so there is no roll fusion.
        VoiceState& v = allocate_voice(note, 0.0f);
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.onset.trigger(voicing().onset_ms);
        if (voicing().chirp_hz > 0.0f) v.chirp.trigger(voicing().chirp_hz);
        strike_modal_bank(v, voicing().modal_mix);
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            float exciter_sig = v.exciter.process();
            if (v.chirp.active()) exciter_sig += v.chirp.process();
            float x = exciter_sig * v.current_velocity;
            x += v.exciter.noise_out_sample * kNoiseGain * v.current_velocity;
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
        load_modal_bank(v);
    }

private:
    // The noise burst runs in PARALLEL with the bank, keeping the broadband
    // character a resonator would pitch-filter away; ×5 brings it into the
    // ballpark of the ×15 mallet.  The bank itself was voiced at ×5.
    static constexpr float kNoiseGain = 5.0f;
    static constexpr float kModalGain = 5.0f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }
};

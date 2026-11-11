#pragma once
// Brachetti World — synth engine of the brachetti_world unit.
//
// The instruments no other Brachetti family engine can make: the cuica, the
// berimbau, the balafon, the riq, the pandeiro, the shekere and the
// rainstick.  A voice is the shared mallet + noise exciter and modal bank,
// plus the layers of voice.h a preset's table turns on.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice and its layers
//   this file   the strike (each layer set up from the table) and the voice
//
// Knobs, preset loading, note routing and the master stage are the modal
// engine every Brachetti modal unit shares: ../brachetti_common/modal_synth.h.
// Unlike the other family units this one was not split from the original
// brachetti: its presets were voiced after the Pelle e Legno samples.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../common/runtime.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, kLn0001, kSampleRate, phase_inc
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig, kMaxModes
//   modal_synth.h    ModalSynth
//   voice_base.h     kNumVoices
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"
#include "../brachetti_common/voice_base.h"
#include "presets.h"
#include "voice.h"

// The wires' delay lines, one per voice slot: 16 KB that would otherwise sit in
// the synth object's .data as zeros.  Namespace-scope `static`: private to
// this unit, and in .bss.
static float s_wire_lines[brc::kNumVoices][world::kWireLen];

// The modal bank's output weight per mode (as ModalBank::process sums them):
// the buzz reads the bank's level at the strike through it.
static const float kModeWeight[brc::kMaxModes] = {1.0f, 0.6f, 0.45f, 0.28f, 0.18f, 0.12f};

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
        // Every strike stacks (a ringing balafon bar, a rainstick turned over
        // again): no roll fusion.
        VoiceState& v = allocate_voice(note, 0.0f);
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.onset.trigger(voicing().onset_ms);
        strike_modal_bank(v, voicing().modal_mix);
        // The pitched layers follow the played note from the preset's root.
        const float tune = exp2f(((float)note - (float)kPresetKnobs[m_preset_idx][k_paramPitch])
                                 * (1.0f / 12.0f));
        const uint32_t seed = v.exciter.noise_gen.seed;
        if (voicing().fr_level > 0.0f) strike_friction(v.friction, tune);
        if (voicing().wire_level > 0.0f) strike_wire(v, tune);
        if (voicing().buzz_level > 0.0f) strike_buzz(v);
        if (voicing().jg_level > 0.0f) strike_jingles(v.jingles, seed);
        if (voicing().pt_level > 0.0f) strike_particles(v.particles, seed);
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        const float exc = voicing().exciter_gain;
        for (size_t i = 0; i < frames; ++i) {
            const float vel = v.current_velocity;
            const float exciter_sig = v.exciter.process();
            float x = (exciter_sig + v.exciter.noise_out_sample * kNoiseGain) * exc * vel;
            if (v.modal.active) {
                const float modes = v.modal.process();
                x += modes * v.modal.mix * kModalGain;
                if (v.buzz.active()) x += v.buzz.process(modes, v.exciter.noise_gen) * vel;
            }
            float layers = 0.0f;
            if (v.friction.active())  layers += v.friction.process(v.exciter.noise_gen);
            if (v.wire.active())      layers += v.wire.process();
            if (v.jingles.active())   layers += v.jingles.process(v.exciter.noise_gen);
            if (v.particles.active()) layers += v.particles.process(v.exciter.noise_gen);
            x += layers * vel;
            const bool sounding = v.layers_active();
            x = v.follow_and_gate(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            x = v.onset.apply(x);
            x = v.fade_out(x);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            v.retire_when_done();
            // A layer can build up from silence (a rainstick's pebbles start
            // slowly), so the level squelch must not retire a voice whose
            // layers still play — unless it is fading out or AllNoteOff has
            // closed it.
            if (sounding && v.fade_mul >= 1.0f && v.exciter.master_env.state != ENV_IDLE)
                v.is_active = true;
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
    // The exciter's burst beside the body, and the bank, as the other modal
    // units voice them.
    static constexpr float kNoiseGain = 5.0f;
    static constexpr float kModalGain = 5.0f;
    // The cuica's two head formants' Q.
    static constexpr float kFormant1Q = 3.0f;
    static constexpr float kFormant2Q = 4.0f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }

    static float ms_to_samples(float ms) { return ms * 0.001f * brc::kSampleRate; }

    // A resonance of quality q at hz: its pole radius exp(−π·hz / (q·fs)).
    static float pole_for_q(float hz, float q) {
        return expf(-3.14159265f * hz / (q * brc::kSampleRate));
    }

    void strike_friction(world::Friction& f, float tune) const {
        const Voicing& c = voicing();
        f.level   = c.fr_level;
        f.hz      = c.fr_hz * tune;
        f.rise    = c.fr_rise;
        f.fall    = c.fr_fall;
        f.up.trigger(0.0f, c.fr_rise_ms);
        f.down.trigger(c.fr_fall_hold_ms, c.fr_fall_ms);
        f.att_inc = 1.0f / ms_to_samples(20.0f);
        f.hold    = (uint32_t)ms_to_samples(c.fr_hold_ms);
        f.decay   = world::t60_mul(c.fr_t60_ms);
        f.noise   = c.fr_noise;
        const float r1 = pole_for_q(c.fr_formant1_hz, kFormant1Q);
        const float r2 = pole_for_q(c.fr_formant2_hz, kFormant2Q);
        f.f1.tune(c.fr_formant1_hz, r1);
        f.f2.tune(c.fr_formant2_hz, r2);
        f.g1 = 1.0f - r1;
        f.g2 = 1.0f - r2;
    }

    void strike_wire(VoiceState& v, float tune) {
        const Voicing& c = voicing();
        world::Wire& w = v.wire;
        w.buf = s_wire_lines[&v - m_voices];
        memset(w.buf, 0, sizeof(float) * world::kWireLen);
        w.level = c.wire_level;
        w.delay = brc::clampf(brc::kSampleRate / (c.wire_hz * tune), 2.0f, (float)(world::kWireLen - 2));
        w.loop_gain = expf(brc::kLn0001 * w.delay / (c.wire_t60_ms * 0.001f * brc::kSampleRate));
        w.damp = c.wire_damp;
        w.direct = c.wire_direct;
        // The stick: a short Hann-windowed noise burst one period long at most.
        const int len = (int)w.delay;
        int burst = (int)ms_to_samples(c.wire_burst_ms);
        if (burst > len) burst = len;
        if (burst < 2) burst = 2;
        for (int i = 0; i < burst; ++i) {
            const float h = 0.5f - 0.5f * cosf(6.2831853f * (float)i / (float)(burst - 1));
            w.buf[i] = v.exciter.noise_gen.process() * h;
        }
        w.w = len;
        w.gourd_hz   = c.gourd_hz;
        w.gourd_up   = c.gourd_up_hz;
        w.gourd_down = c.gourd_down_hz;
        w.gourd_r    = pole_for_q(c.gourd_hz, c.gourd_q);
        w.gourd_g    = 1.0f - w.gourd_r;
        w.up.trigger(0.0f, c.gourd_up_ms);
        w.down.trigger(c.gourd_down_hold_ms, c.gourd_down_ms);
        w.gourd.tune(c.gourd_hz, w.gourd_r);
    }

    void strike_buzz(VoiceState& v) const {
        const Voicing& c = voicing();
        world::Buzz& b = v.buzz;
        // The bank's level at the strike.
        float ref = 0.0f;
        for (int i = 0; i < brc::kMaxModes; ++i) ref += v.modal.env[i] * kModeWeight[i];
        b.ref    = fmaxf(ref, 1e-4f);
        b.level  = c.buzz_level;
        b.thresh = c.buzz_thresh;
        const float r = pole_for_q(c.buzz_hz, c.buzz_q);
        b.res.tune(c.buzz_hz, r);
        b.g = 1.0f - r;
    }

    void strike_jingles(world::Jingles& j, uint32_t seed) const {
        const Voicing& c = voicing();
        j.level = c.jg_level;
        j.lcg = seed | 1u;
        const float lo = logf(c.jg_lo_hz), span = logf(c.jg_hi_hz) - lo;
        j.g = 0.15f;
        for (int p = 0; p < world::kJinglePairs; ++p) {
            for (int k = 0; k < world::kJingleModes; ++k) {
                const float hz = expf(lo + span * j.draw());
                j.mode[p][k].tune(hz, world::pole_radius(c.jg_t60_ms * (0.6f + 0.8f * j.draw())));
                // an impulse rings a two-pole at ~1/sin(w): scale it back to ~unity
                j.gain[p][k] = (0.2f + 0.8f * j.draw()) * sinf(brc::phase_inc(hz));
            }
            j.amp[p]  = 1.0f;
            j.next[p] = (uint32_t)(j.draw() * ms_to_samples(3.0f));
        }
        j.clash_mul   = expf(-1.0f / ms_to_samples(c.jg_clash_ms));
        j.bounce_lo   = (uint32_t)ms_to_samples(c.jg_bounce_lo_ms);
        j.bounce_span = (uint32_t)ms_to_samples(c.jg_bounce_hi_ms - c.jg_bounce_lo_ms) + 1u;
        j.spread      = (uint32_t)ms_to_samples(c.jg_spread_ms);
    }

    void strike_particles(world::Particles& p, uint32_t seed) const {
        const Voicing& c = voicing();
        p.level     = c.pt_level;
        p.lcg       = seed | 1u;
        p.rate      = c.pt_rate / brc::kSampleRate;
        p.att_inc   = 1.0f / fmaxf(1.0f, ms_to_samples(c.pt_attack_ms));
        p.hold      = (uint32_t)ms_to_samples(c.pt_attack_ms + c.pt_hold_ms);
        p.decay     = world::t60_mul(c.pt_t60_ms);
        p.click_mul = expf(-1.0f / ms_to_samples(c.pt_click_ms));
        const float r1 = pole_for_q(c.pt_f1_hz, c.pt_q1);
        const float r2 = pole_for_q(c.pt_f2_hz, c.pt_q2);
        p.r1.tune(c.pt_f1_hz, r1);
        p.r2.tune(c.pt_f2_hz, r2);
        p.g1 = 1.0f - r1;
        p.g2 = 1.0f - r2;
        p.mix2 = c.pt_mix2;
        p.direct = c.pt_direct;
    }
};

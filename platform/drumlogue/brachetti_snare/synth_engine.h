#pragma once
// Brachetti Snare — synth engine of the brachetti_snare unit.
//
// The snare drums: AcSnre, MrchSnr, BrshSnr and RimShot.  A voice is a
// mallet + noise exciter, a short membrane modal head, three parallel
// snare-wire resonators fed by the noise (with a crack burst on the
// onset), AcSnare's swept body boom, and BrshSnr's swirl AM.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice, its layers and the wires
//   this file   the strike, the snare's knob mappings and the per-sample voice
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
        VoiceState& v = allocate_voice(note, brc::kRollFuseSec);
        // Buzz-roll continuity: on a fast restrike (a press or buzz roll) the
        // wires are still rattling when the next stroke lands.  Their motion
        // is taken here, before the strike's reset, and given back below —
        // without the crack — so a roll reads as one continuous buzz, not a
        // row of cracks.  The same window as the roll fusion that keeps the
        // restrike on this ringing slot.
        const bool wires_in_motion =
            v.is_active && (v.exciter.current_frame < (uint32_t)(brc::kRollFuseSec * brc::kSampleRate));
        const SnareWires::Motion motion = v.wires.motion();

        v.start(note, strike_velocity(velocity));
        if (m_preset_idx == k_BrushSnare) brush_dynamics(v);
        shape_exciter(v);
        stage_noise_attack(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        // Only AcSnare's boom sweeps with the pitch envelope; a strike resets
        // it, so it is restored here (adding a preset here changes its sound).
        if (m_preset_idx == k_AcSnare)
            v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        strike_wires(v, wires_in_motion, motion);
        const float onset_scale = brush_texture(v);
        v.onset.trigger(voicing().onset_ms * onset_scale);
        // A wider shell (TubRad) also feeds more energy into the boom.
        v.boom.mix *= strike_modal_bank(v, voicing().modal_mix);
    }

    // SNARE: NoteOff does not release the noise.  Real snare wires ring
    // freely once the stick leaves the head, so they die at their own
    // NzRes-governed rate (calibrated against the references); a release at
    // the Rel rate chokes the buzz to ~26 ms.
    void NoteOff(uint8_t note) {
        for (VoiceState& v : m_voices)
            if (v.is_active && !v.is_releasing && v.current_note == note) v.is_releasing = true;
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            const float exciter_sig = v.excite();
            float x = exciter_sig * v.current_velocity;
            v.pitch.process();
            float noise_gain = kNoiseGain;
            if (v.swirl.active()) noise_gain *= v.swirl.gain();
            x += v.exciter.noise_out_sample * noise_gain * v.current_velocity;
            if (v.boom.active()) {
                // AcSnare's head-tension drop: the stick stretches the batter
                // head and it relaxes back over the first few ms, so the body
                // starts sharp and settles onto 175 Hz.
                if (m_preset_idx == k_AcSnare)
                    v.boom.set_freq((175.0f + v.pitch.amount * v.pitch.env) * v.boom.tune);
                x += v.boom.process() * v.current_velocity;
            }
            if (v.modal.active) {
                const float modes = v.modal.process();
                x += modes * v.modal.mix * kModalGain;
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
        v.wires.band[0].z1   = 0.0f;
        v.wires.band[0].z2   = 0.0f;
        v.wires.mix          = voicing().wire_mix;
        v.wires.band[0].a1   = voicing().wire_a1;
        v.wires.band[0].a2   = voicing().wire_a2;
        v.wires.onset_env    = voicing().wire_onset_env;
        v.wires.onset_attack = voicing().wire_onset_attack;
        v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        load_modal_bank(v);
    }

private:
    // The noise burst runs in PARALLEL with the head, keeping the broadband
    // character a resonator would pitch-filter away; ×5 brings it into the
    // ballpark of the ×15 mallet.  The modal head was voiced at ×5.
    static constexpr float kNoiseGain = 5.0f;
    static constexpr float kModalGain = 5.0f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }

    // BrshSnr's dynamics: a brush stroke cannot "crack" — even a hard hit is
    // a soft, drawn-out sweep — so its ceiling stays capped (≈0.72 at full
    // velocity), and a QUADRATIC curve with a low floor keeps the whole lower
    // half of the range a quiet crawl; only accents build presence.
    static void brush_dynamics(VoiceState& v) {
        const float vv = v.current_velocity;
        v.current_velocity = 0.08f + 0.64f * vv * vv;
    }

    // BrshSnr's 0.08..0.72 velocity back to 0..1, so its character mappings
    // get the whole stroke rather than a slice of it.
    static float brush_stroke(const VoiceState& v) {
        return brc::clampf((v.current_velocity - 0.08f) * 1.5625f, 0.0f, 1.0f);
    }

    // When the noise arrives.  A struck snare's crack and buzz must land
    // together (~90 % in 4 ms; a slower staging reads as noise sprayed over
    // the click).  A brush is many small straws sweeping the head — a sweep,
    // not a hit: measured from the brush references, a SOFT stroke turns on
    // almost at once (a steady wash), a HARD one builds more slowly to a
    // later peak (~22 ms), so the attack SLOWS as velocity rises — the
    // reverse of a struck drum.
    void stage_noise_attack(VoiceState& v) const {
        float attack;
        if (m_preset_idx == k_BrushSnare) attack = 0.0060f - 0.0028f * brush_stroke(v);
        else                              attack = 0.012f;
        v.exciter.noise_env.attack_rate    = attack;
        v.exciter.noise_env_hi.attack_rate = attack;
    }

    // The wires, rebuilt on every strike (the strike's reset cleared them),
    // and the knobs that shape them.  The BUZZ, not the ~10 % modal head, is a
    // snare's defining voice, so the exciter knobs drive the wires, each as a
    // delta from its shipped value (REFERENCE-ANCHORED):
    //   Rel      → buzz TAIL length (with Dkay as the coarse control on BrshSnr,
    //              whose main voice is the swish; the other snares keep Dkay
    //              on the head)
    //   MlltRes  → buzz AMOUNT (wire vs plain filtered noise)
    //   MlltStif → buzz BRIGHTNESS (all three bands)
    //   VlMllStf → buzz TIGHTNESS (pole radius: loose ↔ zingy)
    //   VlMllRes → the CRACK (the broadband burst before the wires)
    //   TubRad   → body depth: band A only (the drum's body tone)
    void strike_wires(VoiceState& v, bool in_motion, const SnareWires::Motion& motion) const {
        SnareWires& w = v.wires;
        w.mix          = brc::clampf(voicing().wire_mix, 0.0f, 1.0f);
        w.onset_env    = voicing().wire_onset_env;
        w.onset_attack = voicing().wire_onset_attack;
        if (in_motion) {
            w.onset_env = 1.0f;   // the wires are already rattling: no crack phase
            w.resume(motion);
        }

        const float vel = brc::clampf(v.current_velocity, 0.0f, 1.0f);
        const float dkay_mult = (m_preset_idx == k_BrushSnare)
            ? brc::knob_exp2(-3.0f * (brc::knob::decay(m_params[k_paramDkay]) - m_anchor.decay))
            : 1.0f;
        const float decay_mult = brc::knob_exp2(-4.0f * (brc::knob::release(m_params[k_paramRel]) - m_anchor.release))
                                 * dkay_mult;   // longer Rel / Dkay = longer buzz
        const float mix_mult = brc::knob_exp2(3.0f * (brc::knob::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res));
        const float bright = brc::clampf(
            brc::knob_exp2(1.6f * (brc::knob::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff)),
            0.33f, 3.0f);   // ±1.6 octaves
        const float tight = brc::clampf((brc::knob::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff) * 0.12f,
                                        -0.12f, 0.12f);
        w.crack_gain = brc::clampf(
            brc::knob_exp2(4.0f * (brc::knob::percent(m_params[k_paramVlMllRes]) - m_anchor.vel_res)), 0.0f, 9.0f);
        // TubRad: a bigger shell drops band A for a deeper "thunk", a smaller
        // one lifts it.  ASYMMETRIC on purpose: deepening reads right however
        // far it goes, while far above the shipped pitch the band sounds
        // toy-like, so upward runs at half rate and caps at ×1.15.
        const float d_tube = brc::knob::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius;
        const float body = (d_tube >= 0.0f) ? fmaxf(0.28f, brc::knob_exp2(-1.9f * d_tube))
                                            : fminf(1.15f, brc::knob_exp2(-0.5f * d_tube));

        // Velocity → buzz: soft hits are mostly head tone with a short, loose
        // rattle; hard hits press the wires into the head for a tighter,
        // brighter, longer buzz (anchored at full velocity).  The brush's
        // rattle is a small detail that must SUSTAIN even on a soft crawl,
        // so there the wires do not fade on soft hits.  The buzz decay is
        // recomputed from NzRes each time (it is shared voice state).
        const float base_decay = 0.0001f + ((1.0f - brc::knob::noise_res(m_params[k_paramNzRes])) * 0.005f);
        if (m_preset_idx == k_BrushSnare) {
            w.mix = 0.20f + 0.06f * (1.0f - vel);
            v.exciter.noise_env.decay_rate = base_decay * (0.80f + 0.20f * vel);
        } else {
            w.mix *= (0.60f + 0.40f * vel);
            v.exciter.noise_env.decay_rate = base_decay * (2.0f - vel);   // up to 2× faster when soft
        }
        w.mix = brc::clampf(w.mix * mix_mult, 0.0f, 1.0f);
        v.exciter.noise_env.decay_rate *= decay_mult;

        // Band A: a velocity-dependent centre per snare (brushes lower and
        // softer, with a low-Q diffuse rattle — a chuff, not a ring).
        float r_a = (m_preset_idx == k_BrushSnare) ? (0.83f + (0.03f * vel)) : (0.90f + (0.07f * vel));
        r_a = brc::clampf(r_a + tight, 0.5f, 0.995f);
        w.band[0].tune(fminf(20000.0f, band_a_hz() * bright * body), r_a);
        // Bands B and C: the preset's centre and pole radius.
        w.band[1].tune(fminf(20000.0f, or_default(voicing().band_b_hz, 100.0f, 4500.0f) * bright),
                       brc::clampf(or_default(voicing().band_b_r, 0.3f, 0.86f) + (0.05f * vel) + tight, 0.5f, 0.995f));
        w.band[2].tune(fminf(20000.0f, or_default(voicing().band_c_hz, 100.0f, 7200.0f) * bright),
                       brc::clampf(or_default(voicing().band_c_r, 0.3f, 0.82f) + (0.03f * vel) + tight, 0.5f, 0.995f));
    }

    float band_a_hz() const {
        switch (m_preset_idx) {
        case k_MarchSnare: return 3500.0f;
        case k_BrushSnare: return 2200.0f;   // brushes read lower and softer
        case k_RimShot:    return 3200.0f;
        default:           return 2800.0f;
        }
    }

    static float or_default(float value, float below, float fallback) {
        return (value < below) ? fallback : value;
    }

    // BrshSnr's texture: a soft breathy "chuff", not a snappy "shack".  A
    // faint ~5 Hz flutter models the straws' contact; the click band decays
    // with the body instead of snapping in ~6 ms (the biggest "shack" →
    // "chuff" step); the split leans to the breathy body band (a soft stroke
    // is almost pure air, only an accent brings the bright band up); and the
    // stroke ARRIVES slowly — a soft brush stroke has no contact transient.
    // Returns the onset-time scale (1 for the struck snares).
    float brush_texture(VoiceState& v) const {
        if (m_preset_idx != k_BrushSnare) return 1.0f;
        v.swirl.depth = 0.15f;
        v.swirl.inc   = brc::phase_inc(5.0f);
        v.swirl.decay = 1.0f;          // the flutter persists
        v.swirl.phase = M_3_PI_2;      // full level on frame 0
        const float stroke = brush_stroke(v);
        v.exciter.noise_env_hi.decay_rate = v.exciter.noise_env.decay_rate;
        v.exciter.noise_band_mix = 0.08f + 0.19f * stroke;
        return 1.0f + 8.0f * (1.0f - stroke);
    }
};

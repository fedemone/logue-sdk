#pragma once
// Brachetti Kick — synth engine of the brachetti_kick unit.
//
// The boom-oscillator kick drums: Kick2 (a kettledrum-bodied kick), 808Sub
// (a TR-808-style pitch-diving sub) and Kick (an acoustic bass drum).  A
// voice is a mallet + noise exciter, a boom oscillator with its own pitch
// sweep, a beater "thump" layer, a beater click and, on Kick2, a membrane
// modal bank.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice and its layers
//   this file   the strike, the kick's knob mappings and the per-sample voice
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
//   dsp_math.h       clampf, knob_exp2, phase_inc
//   knobs.h          knob::decay, knob::hit_pos, knob::inharm,
//                    knob::mallet_res, knob::mallet_stiff, knob::material,
//                    knob::percent, knob::release, knob::tube_radius,
//                    off_anchor, off_unity
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalSynth, kFamilyKnob
//   voice_base.h     kRollFuseSec
//   voice_layers.h   punch_amount
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/modal_synth.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
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

    // ── The kick's own knobs in two standard slots (presets.h) ──────────────
    static constexpr uint8_t role_at(uint8_t slot) {
        return (slot == k_paramDiveTm || slot == k_paramPunch) ? brc::kFamilyKnob : slot;
    }
    static int32_t fixed_knob(uint8_t idx, uint8_t role) {
        return (role == k_paramPartls) ? kShippedPartls[idx] : kShippedModel[idx];
    }

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        // Strokes on the same note inside kRollFuseSec are one pressed roll.
        VoiceState& v = allocate_voice(note, brc::kRollFuseSec);
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        // Only 808Sub's boom sweeps with the pitch envelope; a strike resets
        // it, so it is restored here (adding a preset here changes its sound).
        if (m_preset_idx == k_808Sub)
            v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        v.onset.trigger(attack_ms());
        if (voicing().chirp_hz > 0.0f) v.chirp.trigger(voicing().chirp_hz);
        // The modal body is Kick2's (TubRad stretches it, and retunes the boom
        // in shape_boom).
        strike_modal_bank(v, voicing().modal_mix);

        // The kick's real voice is the boom (808Sub and Kick have no modal
        // bank, and Kick2's sits under the boom), so the knobs mostly go there.
        dial_thump(v);
        shape_boom(v);
        balance_kick2(v);
        beater_click(v);
        shape_attack(v);
        shape_dive(v);
        // The boom is the body: it keeps a 10 ms hit, then drops (τ 25 ms).
        v.punch.trigger(brc::punch_amount(m_params[k_paramPunch]), 10.0f, 25.0f);
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
            if (v.click.active()) x += v.click.process(v.exciter.noise_gen) * v.current_velocity;
            if (v.thump.active()) x += v.thump.process(v.current_velocity);
            if (v.boom.active()) {
                sweep_boom(v);
                x += v.boom.process() * v.current_velocity;
            }
            if (v.modal.active) {
                const float modes = v.modal.process();
                x += modes * v.chirp.open_modal(v.modal.mix) * kModalGain;
            }
            x = v.follow_and_gate(x);
            x = v.punch.apply(x);
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

    // The swept booms set their frequency every sample (× tune: TubRad
    // retunes the whole kick, 1.0 as shipped).  Kick2's boom is fixed.
    void sweep_boom(VoiceState& v) const {
        if (m_preset_idx == k_KickDrum) {
            // Kick: 90 → 55 Hz down the boom's own envelope, or down the pitch
            // envelope once Dive / DiveTm have moved (shape_dive).
            if (v.own_dive) v.boom.set_freq((55.0f + v.pitch.amount * v.pitch.env) * v.boom.tune);
            else            v.boom.set_freq((55.0f + (35.0f * v.boom.env)) * v.boom.tune);
        } else if (m_preset_idx == k_Kick2) {
            // Kick2's boom is fixed; it dives only when Dive is turned up.
            if (v.own_dive) v.boom.set_freq((58.0f + v.pitch.amount * v.pitch.env) * v.boom.tune);
        } else if (m_preset_idx == k_808Sub) {
            // 808Sub: the pitch envelope (τ ≈ 21 ms) dives 160 → 45 Hz in
            // ~100 ms, independently of the boom's level.
            v.boom.set_freq((45.0f + v.pitch.amount * v.pitch.env) * v.boom.tune);
        }
    }

    // ── The kick's knob mappings (REFERENCE-ANCHORED) ───────────────────────
    // The code names the knobs by their modal-engine ROLE (k_paramMlltRes …);
    // the drumlogue shows the kick's names for them: MlltRes = Thump,
    // MlltStif = Snap, VlMllRes = VelThmp, VlMllStf = VelSnap, Mterl = Weight,
    // HitPos = Click, Inharm = Dive, TubRad = Tune (header.c).
    // Each is a function of the knob's delta from the value the preset
    // shipped with: 0 at the shipped value, where the kick plays as voiced.

    // MlltRes (amount) and MlltStif (snap / pitch) dial in the THUMP, a fast
    // pitch-dropping mid punch over the boom; only turning them UP adds it.
    // VlMllRes adds velocity-weighted punch and TRADES boom for it: the master
    // limiter normalises a kick, so an added layer comes straight back out —
    // level is the one thing a limited bus cannot give, balance is free, and a
    // beater-forward kick has less body, not more of everything (judge it on
    // the 100-400 Hz vs 30-90 Hz balance, not on RMS).  VlMllStf adds a knock
    // of its own; down, a soft beater trades knock for body.  MlltStif below
    // the shipped value stretches the boom's onset (a felt-mallet "whoomp"),
    // where the thump, clamped at 0, has nothing to give.
    void dial_thump(VoiceState& v) const {
        const float d_mr = brc::knob::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res;
        const float d_st = brc::knob::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff;
        float thump_amt = fmaxf(0.0f, 1.35f * d_mr + 0.85f * d_st);

        const float vel   = brc::clampf(v.current_velocity, 0.0f, 1.0f);
        const float d_kvr = brc::knob::percent(m_params[k_paramVlMllRes]) - m_anchor.vel_res;
        const float d_kvs = brc::knob::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff;
        // The thump ceiling (2.8, below) sits above what full travel reaches,
        // so the top of the knob stays live.
        if (d_kvr > 0.001f)       thump_amt += d_kvr * (0.90f + 1.80f * vel);
        else if (d_kvr < -0.001f) thump_amt *= brc::knob_exp2(2.0f * d_kvr);
        if (d_kvr > 0.001f) {
            v.boom.mix = fmaxf(0.0f, v.boom.mix * (1.0f - 0.70f * d_kvr));
        } else if (d_kvr < -0.001f) {
            v.boom.mix = fminf(1.50f, v.boom.mix * (1.0f - 0.40f * d_kvr));   // body back, up to +40 %
        }
        if (d_kvs > 0.001f)       thump_amt += d_kvs * 1.40f * (0.4f + 0.6f * vel);
        else if (d_kvs < -0.001f) v.boom.mix = fminf(1.50f, v.boom.mix * (1.0f - 0.45f * d_kvs));
        if (d_st < -0.001f)
            v.boom.attack_inc = fmaxf(0.00008f, v.boom.attack_inc * brc::knob_exp2(2.0f * d_st));

        if (thump_amt > 0.001f) {
            // MlltStif and the velocity-weighted VlMllStf snap set the punch
            // pitch: the bottom of the drop sits ~70-260 Hz around 115 Hz, in
            // the 120-250 Hz "thump" band between the click and the sub, and
            // a snappier hit is higher and shorter.  Base T60 ≈ 58 ms.
            const float snap = d_kvs * (0.4f + 0.6f * vel);
            const float thump_hz = brc::clampf(115.0f * brc::knob_exp2(1.8f * d_st + 1.4f * snap),
                                               55.0f, 340.0f);
            v.thump.inc   = brc::phase_inc(thump_hz);
            v.thump.env   = fminf(2.8f, thump_amt);
            v.thump.amp0  = v.thump.env;
            const float one_minus = 0.00240f * brc::knob_exp2(1.2f * snap);
            v.thump.decay = brc::clampf(1.0f - one_minus, 0.99000f, 0.99920f);
            v.thump.phase = 0.0f;
        }
    }

    // The boom's knobs: Dkay (coarse) and Rel (fine) set its LENGTH, Mterl its
    // WEIGHT, TubRad its base TUNE, Inharm the depth of 808Sub's dive.
    void shape_boom(VoiceState& v) const {
        // Length: T60 ∝ 1/(1−decay), so (1−decay) is divided by the length.
        const float len = brc::knob_exp2(
            3.0f * (brc::knob::decay(m_params[k_paramDkay]) - m_anchor.decay)
            + 2.2f * (brc::knob::release(m_params[k_paramRel]) - m_anchor.release));
        if (brc::off_unity(len)) {
            const float one_minus = (1.0f - v.boom.decay) / brc::clampf(len, 0.15f, 6.0f);
            v.boom.decay = brc::clampf(1.0f - one_minus, 0.99000f, 0.99995f);
        }

        // Weight (drumhead density → how much sub).  Downward it runs all the
        // way to SILENCE at the knob floor (quadratic, continuous with ×1 at
        // the anchor), so MlltRes up + Mterl down is a thump-only kick;
        // upward a gentle 2^(1.9·Δ) lift.
        const float mat = brc::knob::material(m_params[k_paramMterl]);
        const float d_mat = mat - m_anchor.material;
        if (brc::off_anchor(d_mat)) {
            float weight;
            if (d_mat < 0.0f && m_anchor.material > 0.001f) {
                const float t = fmaxf(0.0f, mat / m_anchor.material);   // 1 at the anchor → 0 at the floor
                weight = t * t;
            } else {
                weight = brc::knob_exp2(1.9f * d_mat);
            }
            v.boom.mix = brc::clampf(v.boom.mix * weight, 0.0f, 1.50f);
        }

        // Tune (bigger shell = lower).  The swept booms read boom.tune every
        // sample; Kick2's fixed boom has it baked into its increment.
        const float d_tube = brc::knob::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius;
        if (brc::off_anchor(d_tube)) {
            v.boom.tune = brc::clampf(brc::knob_exp2(-1.0f * d_tube), 0.40f, 2.00f);
            if (m_preset_idx == k_Kick2) v.boom.inc *= v.boom.tune;
        }

        // Dive depth: scales the pitch envelope, strongest on 808Sub, the one
        // kick whose boom reads it.  Exponential: Inharm is bipolar, and a
        // linear factor would bottom out early and leave most of its downward
        // travel dead.
        const float d_inh = brc::knob::inharm(m_params[k_paramInharm]) - m_anchor.inharm;
        if (brc::off_anchor(d_inh))
            v.pitch.amount = fmaxf(0.0f, v.pitch.amount * brc::knob_exp2(2.5f * d_inh));
    }

    // Velocity → BALANCE, Kick2 only.  Without it Kick2's attack-to-tail ratio
    // is flat across velocity (0.93 / 0.93 / 0.95 at 127 / 64 / 30): the
    // limiter pins the level, so a harder strike only stays above the
    // threshold longer — "more decay, same hit".  Balance is free: a soft hit
    // gets rounder and boomier, a hard one stays tight.  Anchored at full
    // velocity (the calibrated hit).  808Sub and Kick are approved on hardware
    // as they stand — do not widen it without a listen on both.
    void balance_kick2(VoiceState& v) const {
        if (m_preset_idx != k_Kick2) return;
        const float vd = brc::clampf(1.0f - v.current_velocity, 0.0f, 1.0f);
        v.boom.mix        = fminf(1.50f, v.boom.mix * (1.0f + 0.35f * vd));
        v.boom.attack_inc = fmaxf(0.00008f, v.boom.attack_inc * (1.0f - 0.50f * vd));
    }

    // ── The kick's own knobs (in the Partls and Model slots) ─────────────────
    // Each is bipolar around 0, the value every preset ships: 0 is the
    // identity, so the presets play as voiced.

    // Punch: the kick's transient.  It drives the shared shaper (brc::Punch:
    // up, the body drops after a 10 ms hit; down, a softened attack) and the
    // kick's own attack: up punchier (a shorter onset ramp, a faster boom
    // rise), down softer — the onset stretches up to ×5.7 and, where a preset
    // has none (808Sub), up to 30 ms; the boom rises up to ×5.7 slower.
    // attack_knob() is that second half's direction: −Punch.
    float attack_knob() const { return -brc::punch_amount(m_params[k_paramPunch]); }
    float attack_ms() const {
        const float a = attack_knob();
        if (a == 0.0f) return voicing().onset_ms;
        return voicing().onset_ms * brc::knob_exp2(2.5f * a) + fmaxf(0.0f, a) * 30.0f;
    }
    void shape_attack(VoiceState& v) const {
        const float a = attack_knob();
        if (a != 0.0f)
            v.boom.attack_inc = brc::clampf(v.boom.attack_inc * brc::knob_exp2(-2.5f * a), 0.00008f, 1.0f);
    }

    // The pitch dive: Dive (the Inharm role, its depth) and DiveTm (its
    // time, ×1/4 .. ×4).  808Sub dives on its pitch envelope already (Dive's
    // depth is applied in shape_boom); here DiveTm stretches it.  Kick sweeps
    // 90 → 55 Hz down its boom's envelope: once either knob moves it gets an
    // envelope of its own with that time and a depth of 35 Hz × Dive's
    // factor.  Kick2's boom is a fixed 58 Hz: turning Dive UP gives it a dive
    // (up to +87 Hz at the top of the knob), DiveTm its time.
    void shape_dive(VoiceState& v) const {
        const float t = brc::knob_exp2(2.0f * brc::clampf((float)m_params[k_paramDiveTm] * 0.01f, -1.0f, 1.0f));
        const float d_inh = brc::knob::inharm(m_params[k_paramInharm]) - m_anchor.inharm;
        if (m_preset_idx == k_808Sub) {
            if (brc::off_unity(t)) v.pitch.decay = 1.0f - (1.0f - v.pitch.decay) / t;
        } else if (m_preset_idx == k_KickDrum) {
            if (!brc::off_unity(t) && !brc::off_anchor(d_inh)) return;
            v.own_dive = true;
            v.pitch.set(1.0f, 1.0f - (1.0f - v.boom.decay) / t,
                        35.0f * brc::knob_exp2(2.5f * d_inh));
        } else if (m_preset_idx == k_Kick2) {
            if (d_inh <= 0.001f) return;
            v.own_dive = true;
            v.pitch.set(1.0f, 1.0f - 0.0006f / t, 87.0f * fminf(1.0f, d_inh));
        }
    }

    // HitPos above the shipped value adds a beater CLICK (a harder beater =
    // a brighter contact tick, T60 ≈ 9 ms, ~1.8-6 kHz).  It only ever adds:
    // the lower half of the knob is inert here by design.
    void beater_click(VoiceState& v) const {
        const float d_hit = brc::knob::hit_pos(m_params[k_paramHitPos]) - m_anchor.hit_pos;
        if (d_hit > 0.001f)
            v.click.trigger(fminf(2.0f, d_hit * 3.8f), 0.98500f, 1.60f, 0.20f, 0.55f);
    }
};

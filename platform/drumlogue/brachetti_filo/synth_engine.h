#pragma once
// Brachetti Filo — synth engine of the brachetti_filo unit.
//
// The wires: BrcSnare's eight snares (AcSnre .. CajSlap) with their wires
// driven by the head as well as by the noise burst, and BrcWorld's two
// berimbaus with their steel wire made stiff.  A snare voice is BrcSnare's
// (a mallet + noise exciter, a short membrane modal head, three parallel
// snare-wire resonators fed by the noise with a crack burst on the onset,
// AcSnare's swept body boom, BrshSnr's swirl AM) plus the head drive; a
// berimbau voice is BrcWorld's wire under its gourd, plus the stiffness.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice, its layers, the wires and the berimbau's wire
//   this file   the strikes, the knob mappings and the per-sample voices
//
// Knobs, preset loading, note routing and the master stage are the modal
// engine every Brachetti modal unit shares: ../brachetti_common/modal_synth.h.
//
// Copied from brachetti_snare and brachetti_world so that they stay as they
// are (bit-identical to the original brachetti, and to their own renders):
// with the new layers off (head_follow 0, bw_stages 0) every preset renders
// bit-identically to the same preset there; tools/brachetti_equiv checks it
// (README.md, "Verification").
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../common/runtime.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, kLn0001, kSampleRate, knob_exp2, or_default,
//                    phase_inc
//   knobs.h          knob::decay, knob::mallet_res, knob::mallet_stiff,
//                    knob::noise_res, knob::percent, knob::release,
//                    knob::tube_radius
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalSynth, kFamilyKnob
//   voice_base.h     kNumVoices, kRollFuseSec
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

// The berimbau wires' delay lines, one per voice slot (16 KB, as BrcWorld's):
// namespace-scope `static`, private to this unit and in .bss.
static float s_wire_lines[brc::kNumVoices][filo::kWireLen];

class alignas(16) BrachettiSynth : public brc::ModalSynth<BrachettiSynth, VoiceState> {
public:
    // ── The presets (presets.h), as the modal engine reads them ─────────────
    static constexpr int k_NumPrograms = kNumPresets;
    static const char* getPresetName(uint8_t idx) { return preset_name(idx); }
    static const char* preset_name(uint8_t idx) { return ::preset_name(idx); }
    static const int16_t* preset_knobs(uint8_t idx) { return kPresetKnobs[idx]; }
    static const brc::ModalPresetConfig& modal_config(uint8_t idx) { return kModalConfigs[idx]; }
#ifdef BRC_FILO_AS_COPIED
    static float out_trim(uint8_t idx) { return kCopiedTrim[idx]; }
#else
    static float out_trim(uint8_t idx) { return kOutTrim[idx]; }
#endif

    // Punch sits in Partls' slot (presets.h); Partls plays at each preset's
    // shipped value.
    static constexpr uint8_t role_at(uint8_t slot) {
        return (slot == k_paramPunch) ? brc::kFamilyKnob : slot;
    }
    static int32_t fixed_knob(uint8_t idx, uint8_t role) { (void)role; return kShippedPartls[idx]; }

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        if (is_berimbau()) { berimbau_note_on(note, velocity); return; }
        VoiceState& v = allocate_voice(note, brc::kRollFuseSec);
        v.berimbau = false;
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
        if (kind() == kKindBrush) brush_dynamics(v);
        shape_exciter(v);
        stage_noise_attack(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.boom.load(voicing().boom_inc, voicing().boom_env, voicing().boom_decay,
                    voicing().boom_mix, voicing().boom_attack_env, voicing().boom_attack_inc);
        // Only AcSnare's boom sweeps with the pitch envelope; a strike resets
        // it, so it is restored here (adding a preset here changes its sound).
        if (kind() == kKindAcoustic)
            v.pitch.set(voicing().pitch_env, voicing().pitch_decay, voicing().pitch_amount);
        strike_wires(v, wires_in_motion, motion);
        strike_head_drive(v);
        const float onset_scale = brush_texture(v);
        v.onset.trigger(voicing().onset_ms * onset_scale);
        // A wider shell (TubRad) also feeds more energy into the boom.
        v.boom.mix *= strike_modal_bank(v, voicing().modal_mix);
        v.punch.trigger(brc::punch_amount(m_params[k_paramPunch]));
    }

    // SNARE: NoteOff does not release the noise.  Real snare wires ring
    // freely once the stick leaves the head, so they die at their own
    // NzRes-governed rate (calibrated against the references); a release at
    // the Rel rate chokes the buzz to ~26 ms.
    void NoteOff(uint8_t note) {
        if (is_berimbau()) { Base::NoteOff(note); return; }   // BrcWorld's: the noise releases
        for (VoiceState& v : m_voices)
            if (v.is_active && !v.is_releasing && v.current_note == note) v.is_releasing = true;
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        if (v.berimbau) { render_berimbau(v, out, frames, tone); return; }
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            const float exciter_sig = v.excite();
            float x = exciter_sig * v.current_velocity;
            v.pitch.process();
            float noise_gain = kNoiseGain;
            if (v.swirl.active()) noise_gain *= v.swirl.gain();
            x += v.exciter.noise_out_sample * noise_gain * v.current_velocity;
            float head = 0.0f;   // the head's motion, for the wires on the head
            if (v.boom.active()) {
                // AcSnare's head-tension drop: the stick stretches the batter
                // head and it relaxes back over the first few ms, so the body
                // starts sharp and settles onto 175 Hz.
                if (kind() == kKindAcoustic)
                    v.boom.set_freq((175.0f + v.pitch.amount * v.pitch.env) * v.boom.tune);
                const float boom = v.boom.process() * v.current_velocity;
                x += boom;
                head += boom;
            }
            if (v.modal.active) {
                const float modes = v.modal.process() * v.modal.mix * kModalGain;
                x += modes;
                head += modes;
            }
            if (v.head.active()) v.head.follow_head(head);
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
        if (is_berimbau()) {   // BrcWorld's
            v.exciter.noise_lp_state    = 0.0f;
            v.exciter.noise_band_mix    = voicing().noise_band_mix;
            v.exciter.noise_hi_lp_state = 0.0f;
            v.exciter.noise_hi_lp_coeff = voicing().noise_split;
            load_modal_bank(v);
            return;
        }
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
    using Base = brc::ModalSynth<BrachettiSynth, VoiceState>;

    bool is_berimbau() const { return voicing().bw_level > 0.0f; }

    // Which of BrcSnare's per-preset branches a snare takes: BrcSnare tests
    // the preset's index (AcSnare's swept boom, MrchSnr's and RimShot's band
    // A, BrshSnr's brush); here the preset table says it (snare_kind), so a
    // preset the voicing search appends takes the branches of the one it was
    // copied from.  The four classic snares carry their own, the rest 0.
    enum SnareKind : int { kKindGeneric = 0, kKindAcoustic, kKindMarch, kKindBrush, kKindRim };
    int kind() const { return (int)voicing().snare_kind; }

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
        if (kind() == kKindBrush) attack = 0.0060f - 0.0028f * brush_stroke(v);
        else                              attack = 0.012f;
        v.exciter.noise_env.attack_rate    = attack;
        v.exciter.noise_env_hi.attack_rate = attack;
    }

    // The wires, rebuilt on every strike (the strike's reset cleared them),
    // (the code names the knobs by their modal-engine ROLE; the drumlogue
    // shows MlltRes = Buzz, MlltStif = BuzzTon, VlMllRes = Crack,
    // VlMllStf = Tight, Rel = BuzzLen — header.c)
    // and the knobs that shape them.  The BUZZ, not the ~10 % modal head, is a
    // snare's defining voice, so the exciter knobs drive the wires, each as a
    // delta from its shipped value (REFERENCE-ANCHORED):
    //   Rel      → buzz TAIL length (with Dkay as the coarse control on BrshSnr,
    //              whose main voice is the swish; the other snares keep Dkay
    //              on the head)
    //   MlltRes  → buzz AMOUNT (wire vs plain filtered noise)
    //   MlltStif → buzz BRIGHTNESS (all three bands)
    //   VlMllStf → buzz TIGHTNESS (pole radius: loose ↔ zingy)
    //   VlMllRes → the CRACK (the broadband burst before the wires; up also
    //              lengthens it, and adds one where a preset has none)
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
        const float dkay_mult = (kind() == kKindBrush)
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
        const float d_crack = brc::knob::percent(m_params[k_paramVlMllRes]) - m_anchor.vel_res;
        w.crack_gain = brc::clampf(brc::knob_exp2(4.0f * d_crack), 0.0f, 9.0f);
        // Above its shipped value Crack also LENGTHENS the crack phase (the
        // wires fade in up to 4× slower) and gives one to the presets that
        // ship none (wires in from frame 0): a crack you can hear on every
        // snare, not only a louder first few ms.  Not while the wires are
        // already rattling (a buzz roll has no crack).
        if (d_crack > 0.001f && !in_motion) {
            w.onset_env    = fminf(w.onset_env, 1.0f - fminf(1.0f, d_crack));
            w.onset_attack = (w.onset_attack > 0.0f ? w.onset_attack : 0.0026f) / (1.0f + 3.0f * d_crack);
        }
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
        if (kind() == kKindBrush) {
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
        float r_a = (kind() == kKindBrush) ? (0.83f + (0.03f * vel)) : (0.90f + (0.07f * vel));
        r_a = brc::clampf(r_a + tight, 0.5f, 0.995f);
        w.band[0].tune(fminf(20000.0f, band_a_hz() * bright * body), r_a);
        // Bands B and C: the preset's centre and pole radius.
        w.band[1].tune(fminf(20000.0f, brc::or_default(voicing().band_b_hz, 100.0f, 4500.0f) * bright),
                       brc::clampf(brc::or_default(voicing().band_b_r, 0.3f, 0.86f) + (0.05f * vel) + tight, 0.5f, 0.995f));
        w.band[2].tune(fminf(20000.0f, brc::or_default(voicing().band_c_hz, 100.0f, 7200.0f) * bright),
                       brc::clampf(brc::or_default(voicing().band_c_r, 0.3f, 0.82f) + (0.03f * vel) + tight, 0.5f, 0.995f));
    }

    float band_a_hz() const {
        switch (kind()) {
        case kKindMarch: return 3500.0f;
        case kKindBrush: return 2200.0f;   // brushes read lower and softer
        case kKindRim:   return 3200.0f;
        default:           return 2800.0f;
        }
    }


    // BrshSnr's texture: a soft breathy "chuff", not a snappy "shack".  A
    // faint ~5 Hz flutter models the straws' contact; the click band decays
    // with the body instead of snapping in ~6 ms (the biggest "shack" →
    // "chuff" step); the split leans to the breathy body band (a soft stroke
    // is almost pure air, only an accent brings the bright band up); and the
    // stroke ARRIVES slowly — a soft brush stroke has no contact transient.
    // Returns the onset-time scale (1 for the struck snares).
    float brush_texture(VoiceState& v) const {
        if (kind() != kKindBrush) return 1.0f;
        v.swirl.depth = 0.15f;
        v.swirl.inc   = brc::phase_inc(5.0f);
        v.swirl.decay = 1.0f;          // the flutter persists
        v.swirl.phase = M_3_PI_2;      // full level on frame 0
        const float stroke = brush_stroke(v);
        v.exciter.noise_env_hi.decay_rate = v.exciter.noise_env.decay_rate;
        v.exciter.noise_band_mix = 0.08f + 0.19f * stroke;
        return 1.0f + 8.0f * (1.0f - stroke);
    }

    // ── The wires on the head ───────────────────────────────────────────────
    // Off (follow 0) on a preset that ships head_follow 0: the voice is then
    // BrcSnare's.  Two of the wires' knobs reach it, anchored (exactly 1 at
    // the shipped value): Buzz (MlltRes) scales the drive as it scales the
    // rattle, BuzzLen (Rel) the follower's release as it does the buzz tail.
    void strike_head_drive(VoiceState& v) const {
        HeadDrive& h = v.head;
        const Voicing& c = voicing();
        h.follow = 0.0f;
#ifdef BRC_FILO_AS_COPIED   // the copy check (README.md, "Verification")
        return;
#endif
        if (!(c.head_follow > 0.0f)) return;
        const float amount = brc::knob_exp2(3.0f * (brc::knob::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res));
        const float length = brc::knob_exp2(4.0f * (brc::knob::release(m_params[k_paramRel]) - m_anchor.release));
        h.follow  = c.head_follow * amount;
        h.crackle = brc::clampf(c.head_crackle, 0.0f, 1.0f);
        h.att     = 1.0f - expf(-1.0f / ms_to_samples(kHeadAttackMs));
        h.rel     = 1.0f - expf(-1.0f / fmaxf(1.0f, ms_to_samples(brc::or_default(c.head_rel_ms, 0.001f, 20.0f) * length)));
        h.noise.seed = (v.exciter.noise_gen.seed ^ 0x9E3779B9u) | 1u;
    }

    // ── The berimbau: BrcWorld's strike and voice ───────────────────────────
    // ../brachetti_world/synth_engine.h, as it is (its NoteOn, render_voice,
    // layer_knobs and strike_wire), so a berimbau with bw_stages 0 renders as
    // BrcWorld's.  The knobs keep their BrcWorld meaning on it (Crack is
    // BrcWorld's Rattle, in the same slot); Tight also scales the stiffness.
    static constexpr float kHeadAttackMs = 1.0f;
    // The stiffness all-passes' coefficient stays below this (stable at any
    // value under 1; the half-period guard in strike_wire keeps the pitch).
    static constexpr float kMaxStiff = 0.97f;

    void berimbau_note_on(uint8_t note, uint8_t velocity) {
        VoiceState& v = allocate_voice(note, 0.0f);
        v.start(note, strike_velocity(velocity));
        v.berimbau = true;
        // The noise envelopes' decay as BrcWorld's (only NzRes sets it there;
        // a snare strike on this slot may have left its own).  shape_exciter
        // then sets the attacks, as BrcWorld's strike does.
        v.exciter.set_noise_res(knob(k_paramNzRes));
        // shape_exciter with VlMllRes at 0, as BrcWorld plays it: Crack's
        // slot is its Rattle there, which only the wire reads.
        v.exciter.shape_strike(v.current_velocity, knob(k_paramHitPos),
                               knob(k_paramMlltStif), knob(k_paramVlMllStf),
                               m_anchor.vel_stiff, knob(k_paramNzRes), 0);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.onset.trigger(voicing().onset_ms);
        strike_modal_bank(v, voicing().modal_mix);
        const float tune = exp2f(((float)note - (float)kPresetKnobs[m_preset_idx][k_paramPitch])
                                 * (1.0f / 12.0f));
        const WireKnobs k = wire_knobs();
        strike_wire(v, tune, k);
        v.punch.trigger(brc::punch_amount(m_params[k_paramPunch]));
    }

    void render_berimbau(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        const float exc = voicing().exc_gain;
        for (size_t i = 0; i < frames; ++i) {
            const float vel = v.current_velocity;
            const float exciter_sig = v.exciter.process();
            float x = (exciter_sig + v.exciter.noise_out_sample * kNoiseGain) * exc * vel;
            if (v.modal.active) {
                const float modes = v.modal.process();
                x += modes * v.modal.mix * kModalGain;
            }
            float layers = 0.0f;
            if (v.wire.active()) layers += v.wire.process();
            x += layers * vel;
            const bool sounding = v.wire.active();
            x = v.follow_and_gate(x);
            x = v.punch.apply(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            x = v.onset.apply(x);
            x = v.fade_out(x);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            v.retire_when_done();
            if (sounding && v.fade_mul >= 1.0f && v.exciter.master_env.state != ENV_IDLE)
                v.is_active = true;
        }
    }

    // BrcWorld's layer knobs, the ones its wire reads (presence, length,
    // bend, body, brightness, density, rattle), plus Tight on the stiffness.
    struct WireKnobs {
        float length, bend, body, bright, density, presence;
        float rattle, rattle_add;
        float stiff;   // Tight: the stiffness × ¼ .. × 4 (on 1 − the all-passes' coefficient)
    };

    WireKnobs wire_knobs() const {
        namespace kn = brc::knob;
        WireKnobs k;
        k.length   = brc::clampf(brc::knob_exp2(4.5f * (kn::decay(m_params[k_paramDkay]) - m_anchor.decay)
                                                + 3.2f * (kn::release(knob(k_paramRel)) - m_anchor.release)),
                                 0.05f, 12.0f);
        k.bend     = brc::clampf(brc::knob_exp2(2.0f * (kn::inharm(m_params[k_paramInharm]) - m_anchor.inharm)),
                                 0.05f, 4.0f);
        k.body     = brc::clampf(brc::knob_exp2(-1.0f * (kn::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius)),
                                 0.5f, 2.0f);
        k.bright   = brc::clampf(brc::knob_exp2(1.5f * (kn::material(m_params[k_paramMterl]) - m_anchor.material)),
                                 0.25f, 4.0f);
        k.density  = brc::clampf(brc::knob_exp2(2.0f * (kn::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff)),
                                 0.1f, 8.0f);
        k.presence = brc::clampf(brc::knob_exp2(3.4f * (kn::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res)),
                                 0.05f, 9.0f);
        const float rattle = brc::clampf((float)m_params[k_paramVlMllRes] * 0.01f, -1.0f, 1.0f);
        k.rattle     = brc::knob_exp2(2.0f * rattle);
        k.rattle_add = fmaxf(0.0f, rattle);
        k.stiff = brc::knob_exp2(2.0f * brc::clampf(kn::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff,
                                                    -1.0f, 1.0f));
        return k;
    }

    static float body_hz(float hz, const WireKnobs& k) { return fminf(hz * k.body, 20000.0f); }
    static float ms_to_samples(float ms) { return ms * 0.001f * brc::kSampleRate; }
    static float pole_for_q(float hz, float q) {
        return expf(-3.14159265f * hz / (q * brc::kSampleRate));
    }

    // A one-pole low-pass a / (1 − b·z^−1)'s phase delay at ω, in samples,
    // b = 1 − a: atan2(b·sin ω, 1 − b·cos ω) / ω.
    static float lp_delay(float w, float a) {
        const float b = 1.0f - a;
        return atan2f(b * sinf(w), 1.0f - b * cosf(w)) / w;
    }

    // A first-order all-pass's phase delay at ω, in samples:
    // (ω + 2·atan2(a·sin ω, 1 − a·cos ω)) / ω.
    static float ap1_delay(float w, float a) {
        return (w + 2.0f * atanf(a * sinf(w) / (1.0f - a * cosf(w)))) / w;
    }

    void strike_wire(VoiceState& v, float tune, const WireKnobs& k) {
        const Voicing& c = voicing();
        filo::Wire& w = v.wire;
        w.buf = s_wire_lines[&v - m_voices];
        memset(w.buf, 0, sizeof(float) * filo::kWireLen);
        w.level = c.bw_level * k.presence;
        w.delay = brc::clampf(brc::kSampleRate / (c.bw_hz * tune), 2.0f, (float)(filo::kWireLen - 2));
        w.loop_gain = expf(brc::kLn0001 * w.delay / (c.bw_t60_ms * k.length * 0.001f * brc::kSampleRate));
        w.damp = brc::clampf(c.bw_damp * k.bright, 0.02f, 1.0f);
        w.direct = c.bw_direct * k.rattle + 0.6f * k.rattle_add;   // the wire's own dry sound beside the gourd
        // The stiffness: the all-passes' delay at the fundamental comes off
        // the line, so the fundamental stays where it was and the partials
        // above it go sharp.
        w.stages = (int)brc::clampf(c.bw_stages + 0.5f, 0.0f, (float)filo::kMaxStiffStages);
#ifdef BRC_FILO_AS_COPIED   // the copy check (README.md, "Verification")
        w.stages = 0;
#endif
        if (w.stages > 0) {
            // Tight: a first-order all-pass's low-frequency delay, and with it
            // the stretch, grows as 1 / (1 − a), so the knob scales 1 − a
            // (left alone at the shipped value, where the factor is 1).
            float a = c.bw_stiff;
            if (k.stiff != 1.0f) a = 1.0f - (1.0f - a) / k.stiff;
            a = brc::clampf(a, 0.0f, kMaxStiff);
            // The all-passes may take at most half the period (a high note,
            // a stiff wire): beyond that the line could not keep the pitch.
            const float w0 = brc::phase_inc(c.bw_hz * tune);
            const float period = w.delay;
            for (int i = 0; i < 8 && (float)w.stages * ap1_delay(w0, a) > 0.5f * period; ++i)
                a = 1.0f - (1.0f - a) * 1.5f;
            w.stiff = brc::clampf(a, 0.0f, kMaxStiff);
            // The line gives back the all-passes' delay at the fundamental,
            // and the loss filter's, which BrcWorld's wire leaves in (it plays
            // 8 cents flat at the root, a third of a tone three octaves up).
            w.delay = brc::clampf(w.delay - (float)w.stages * ap1_delay(w0, w.stiff)
                                          - lp_delay(w0, w.damp),
                                  2.0f, (float)(filo::kWireLen - 2));
        }
        // The stick: a short Hann-windowed noise burst one period long at most.
        const int len = (int)w.delay;
        int burst = (int)ms_to_samples(c.bw_burst_ms / k.density);   // a harder stick, a shorter burst
        if (burst > len) burst = len;
        if (burst < 2) burst = 2;
        for (int i = 0; i < burst; ++i) {
            const float h = 0.5f - 0.5f * cosf(6.2831853f * (float)i / (float)(burst - 1));
            w.buf[i] = v.exciter.noise_gen.process() * h;
        }
        w.w = len;
        w.gourd_hz   = body_hz(c.gourd_hz, k);
        w.gourd_up   = c.gourd_up_hz * k.body * k.bend;
        w.gourd_down = c.gourd_down_hz * k.body * k.bend;
        w.gourd_r    = pole_for_q(w.gourd_hz, c.gourd_q);
        w.gourd_g    = 1.0f - w.gourd_r;
        w.up.trigger(0.0f, c.gourd_up_ms * k.length);
        w.down.trigger(c.gourd_down_hold_ms * k.length, c.gourd_down_ms * k.length);
        w.gourd.tune(w.gourd_hz, w.gourd_r);
    }
};

#pragma once
// Brachetti Pizzico — synth engine of the brachetti_pizzico unit.
//
// Plucked strings from a delay loop: BrcLippa's Karplus-Strong loop with a
// Schroeder all-pass inside it (voice.h), its strike as BrcLippa's
// strike_loop, verbatim.  A voice is the shared mallet + noise exciter and
// modal bank, plus the loop.
//
//   presets.h   the presets and their voicing
//   voice.h     one voice and the loop
//   this file   the strike (the loop tuned and set up from the table) and the voice
//
// Knobs, preset loading, note routing and the master stage are the modal
// engine every Brachetti modal unit shares: ../../brachetti_common/modal_synth.h.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../common/runtime.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, kLn0001, kSampleRate, knob_exp2, or_default,
//                    phase_inc
//   knobs.h          name_at
//   master_stage.h   MasterStage, tone_tilt
//   modal_bank.h     ModalPresetConfig
//   modal_synth.h    ModalSynth, kFamilyKnob
//   voice_base.h     kNumVoices
//   voice_layers.h   punch_amount
#include "../../brachetti_common/dsp_math.h"
#include "../../brachetti_common/knobs.h"
#include "../../brachetti_common/master_stage.h"
#include "../../brachetti_common/modal_bank.h"
#include "../../brachetti_common/modal_synth.h"
#include "../../brachetti_common/voice_base.h"
#include "../../brachetti_common/voice_layers.h"
#include "presets.h"
#include "voice.h"

// The loops' delay lines, one pair per voice slot: 48 KB that would otherwise
// sit in the synth object's .data as zeros.  Namespace-scope `static`: private
// to this unit, and in .bss.
static float s_loop_lines[brc::kNumVoices][loop::kLoopLen];
static float s_ap_lines[brc::kNumVoices][loop::kApLen];

class alignas(16) BrachettiSynth : public brc::ModalSynth<BrachettiSynth, VoiceState> {
public:
    // ── The presets (presets.h), as the modal engine reads them ─────────────
    static constexpr int k_NumPrograms = kNumPresets;
    static const char* getPresetName(uint8_t idx) { return preset_name(idx); }
    static const char* preset_name(uint8_t idx) { return brc::name_at(kPresetNames, idx, "Unknown"); }   // = presets.h preset_name (spelled out: no ::, so the class also compiles inside Brachetti's namespace)
    static const int16_t* preset_knobs(uint8_t idx) { return kPresetKnobs[idx]; }
    static const brc::ModalPresetConfig& modal_config(uint8_t idx) { return kModalConfigs[idx]; }
    static float out_trim(uint8_t idx) { return kOutTrim[idx]; }

    // Bend, BendTm, Buzz, Blend and BuzzTon sit in VlMllRes', Partls',
    // Model's, NzRes' and NzFltr's slots, Punch in Rel's (presets.h).
    // VlMllRes then plays at 0 (the default fixed_knob), Rel at each preset's
    // shipped value, the others at the fixed values.
    static constexpr uint8_t role_at(uint8_t slot) {
        return (slot == k_paramBend || slot == k_paramBendTm || slot == k_paramBuzz ||
                slot == k_paramPunch || slot == k_paramBlend || slot == k_paramBuzzTone)
                   ? brc::kFamilyKnob : slot;
    }
    static int32_t fixed_knob(uint8_t idx, uint8_t role) {
        switch (role) {
        case Param::k_paramRel:    return kShippedRel[idx];
        case Param::k_paramPartls: return kFixedPartls;
        case Param::k_paramModel:  return kFixedModel;
        case Param::k_paramNzRes:  return kFixedNzRes;
        case Param::k_paramNzFltr: return kFixedNzFltr;
        default:                   return 0;   // VlMllRes
        }
    }

    // ── The strike ──────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        // Every strike stacks: a fast double stroke rings two heads' worth,
        // as a real drum's second stroke does not stop the first.
        VoiceState& v = allocate_voice(note, 0.0f);
        // A slot struck again while its loop still rings keeps the loop's
        // motion (loop::Drum::Motion): no click where the old ring would be cut.
        const bool ringing = v.is_active && v.drum.active() && v.fade_mul >= 1.0f;
        const loop::Drum::Motion motion = v.drum.motion();
        v.start(note, strike_velocity(velocity));
        shape_exciter(v);
        v.reset_for_strike();
        v.exciter.fire();
        v.exciter.set_band_mix(m_model, voicing().noise_band_mix);
        v.onset.trigger(voicing().onset_ms);
        strike_modal_bank(v, voicing().modal_mix);
        // The loop follows the played note from the preset's root.
        const float tune = exp2f(((float)note - (float)kPresetKnobs[m_preset_idx][k_paramPitch])
                                 * (1.0f / 12.0f));
        if (voicing().lp_level > 0.0f) {
            strike_loop(v, tune, layer_knobs(), ringing);
            if (ringing) v.drum.resume(motion, kRestrikeSlew);
        }
        v.punch.trigger(brc::punch_amount(m_params[k_paramPunch]));
    }

    // ── One voice, one block, into the mono bus (out[i * 2]) ────────────────
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        const float exc = voicing().exciter_gain;
        for (size_t i = 0; i < frames; ++i) {
            const float vel = v.current_velocity;
            // The exciter runs even where its gain is 0: the loop's excitation
            // noise comes from its generator.
            const float exciter_sig = v.exciter.process();
            float x = (exciter_sig + v.exciter.noise_out_sample * kNoiseGain) * exc * vel;
            if (v.modal.active) x += v.modal.process() * v.modal.mix * kModalGain;
            if (v.drum.active()) x += v.drum.process(v.exciter.noise_gen);   // velocity is in its excitation
            const bool sounding = v.layers_active();
            x = v.follow_and_gate(x);
            x = v.punch.apply(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            x = v.onset.apply(x);
            x = v.fade_out(x);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            v.retire_when_done();
            // The loop rings on long after the exciter: the level squelch
            // must not retire a voice whose loop still plays — unless it is
            // fading out or AllNoteOff has closed it.
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
    // The DC blocker inside the loop (Hz): the excitation's noise has some DC,
    // and a positive loop holds DC as its lowest "resonance".
    static constexpr float kDcHz = 8.0f;
    // A trip round the loop never gains more than this: stable whatever the
    // knobs do (the loss and DC filters never exceed unity).
    static constexpr float kMaxLoopGain = 0.9995f;
    // A strike into a ringing loop slews the glide over ~1 ms (one-pole).
    static constexpr float kRestrikeSlew = 0.0206f;   // 1 − e^(−1 / 48)
    // The loss filter is designed from the T60 here (Hz) and at the note.
    static constexpr float kHfHz = 3000.0f;
    // ... and its pole kept off 1 (where the filter would pass nothing).
    static constexpr float kMaxLossPole = 0.98f;
    // What Bend and Buzz add, at their tops, to a preset that ships none.
    static constexpr float kBendAdd   = 0.3f;
    static constexpr float kSnaresAdd = 0.5f;

    const Voicing& voicing() const { return kVoicing[m_preset_idx]; }

    // ── The knobs on the loop ───────────────────────────────────────────────
    // As on BrcWorld's layers: each knob, REFERENCE-ANCHORED, becomes a factor
    // (or an offset) that is exactly 1 (0) at the preset's shipped value, so a
    // preset plays as voiced until a knob moves.  The standard knobs keep the
    // meaning they have on the bank; three are the loop's own:
    //   Dkay      length    the loop's T60 (4.5 octaves per unit, as the bank's)
    //   Mterl     bright    the highs' T60 against the note's: brighter, ringing highs
    //   Inharm    inharm    the all-pass gain, an offset: the inharmonicity
    //   TubRad    apdelay   the all-pass delay (relabelled ApDelay)
    //   MlltStif  stick     a stiffer stick: a shorter excitation cycle
    //   MlltRes   presence  the loop's level (as it is the bank's)
    //   HitPos    position  the strike position: towards the rim, brighter
    //   NzMix     noise     the excitation's noise (as it is the burst's)
    //   NzFltFrq  noise_hz  the excitation noise's corner (as it is the burst's)
    //   VlMllStf  vel_stick velocity → a stiffer stick (as it is the mallet's)
    //   Bend, BendTm, Buzz (bipolar, 0 shipped): × ¼ .. × 4
    //   BuzzTon   (bipolar, 0 shipped): the buzz's band, ± 1 octave
    //   Blend     (0..100 %, 0 shipped): added to the preset's own blend, up to 0.5
    struct LayerKnobs {
        float length, bright, inharm, apdelay, stick, vel_stick, presence, position, noise, noise_hz;
        float bend, bend_add, bend_time, snares, snares_add, snare_tone, blend_add;
    };

    LayerKnobs layer_knobs() const {
        namespace kn = brc::knob;
        const int16_t* row = kPresetKnobs[m_preset_idx];
        LayerKnobs k;
        k.length   = brc::clampf(brc::knob_exp2(4.5f * (kn::decay(m_params[k_paramDkay]) - m_anchor.decay)),
                                 0.05f, 12.0f);
        k.bright   = brc::clampf(brc::knob_exp2(3.0f * (kn::material(m_params[k_paramMterl]) - m_anchor.material)),
                                 0.25f, 4.0f);
        k.inharm   = 0.9f * (kn::inharm(m_params[k_paramInharm]) - m_anchor.inharm);
        k.apdelay  = brc::clampf(brc::knob_exp2(3.0f * (kn::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius)),
                                 0.25f, 4.0f);
        k.stick    = brc::clampf(brc::knob_exp2(2.0f * (kn::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff)),
                                 0.25f, 4.0f);
        // per unit of velocity: applied at the strike, × the voice's velocity
        k.vel_stick = 2.0f * (kn::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff);
        k.presence = brc::clampf(brc::knob_exp2(3.4f * (kn::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res)),
                                 0.05f, 9.0f);
        k.position = 0.5f * (kn::hit_pos(m_params[k_paramHitPos]) - m_anchor.hit_pos);
        k.noise    = brc::clampf(brc::knob_exp2(3.0f * (kn::noise_mix(m_params[k_paramNzMix])
                                                        - kn::noise_mix(row[k_paramNzMix]))),
                                 0.0f, 8.0f);
        k.noise_hz = kn::noise_freq(m_params[k_paramNzFltFrq]) / kn::noise_freq(row[k_paramNzFltFrq]);
        const float bend   = brc::clampf((float)m_params[k_paramBend] * 0.01f, -1.0f, 1.0f);
        const float bendtm = brc::clampf((float)m_params[k_paramBendTm] * 0.01f, -1.0f, 1.0f);
        const float snares = brc::clampf((float)m_params[k_paramBuzz] * 0.01f, -1.0f, 1.0f);
        k.bend       = brc::knob_exp2(2.0f * bend);
        k.bend_add   = kBendAdd * fmaxf(0.0f, bend);
        k.bend_time  = brc::knob_exp2(2.0f * bendtm);
        k.snares     = brc::knob_exp2(2.0f * snares);
        k.snares_add = kSnaresAdd * fmaxf(0.0f, snares);
        k.snare_tone = brc::knob_exp2(brc::clampf((float)m_params[k_paramBuzzTone] * 0.01f, -1.0f, 1.0f));
        k.blend_add  = 0.5f * brc::clampf((float)m_params[k_paramBlend] * 0.01f, 0.0f, 1.0f);
        return k;
    }

    static float ms_to_samples(float ms) { return ms * 0.001f * brc::kSampleRate; }
    static float pole_for_q(float hz, float q) { return expf(-3.14159265f * hz / (q * brc::kSampleRate)); }

    // ── The loop's phase delays at ω (radians per sample), in samples ───────
    // Schroeder all-pass H = (−g + z^−M) / (1 − g·z^−M):
    //   arg H = −ωM − 2·atan2(g·sin ωM, 1 − g·cos ωM)
    static float ap_delay(float w, float m, float g) {
        const float p = w * m;
        return (p + 2.0f * atan2f(g * sinf(p), 1.0f - g * cosf(p))) / w;
    }
    // One-pole low-pass a / (1 − b·z^−1), b = 1 − a: arg = −atan2(b·sin ω, 1 − b·cos ω)
    static float lp_delay(float w, float a) {
        const float b = 1.0f - a;
        return atan2f(b * sinf(w), 1.0f - b * cosf(w)) / w;
    }
    // The DC blocker x − lp(x): a phase LEAD of (π − ω)/2 − atan2(b·sin ω, 1 − b·cos ω).
    static float hp_lead(float w, float a) {
        const float b = 1.0f - a;
        return (0.5f * (3.14159265f - w) - atan2f(b * sinf(w), 1.0f - b * cosf(w))) / w;
    }
    // The loss filter from two decay times, after Välimäki, Huopaniemi,
    // Karjalainen & Jánosy (1996): the pole b of a / (1 − b·z^−1) that makes
    // a trip round the loop lose as much more at kHfHz than at the note (ω₀)
    // as a T60 of t60_hf there, against t60 at the note, needs.  Per trip the
    // two attenuations are A = exp(ln 0.001 · period / T60); their ratio r
    // fixes |H(ω₁)|² / |H(ω₀)|² = r², a quadratic in b:
    //   b² − 2B·b + 1 = 0,  B = (cos ω₀ − r²·cos ω₁) / (1 − r²),  b = B − √(B² − 1).
    // A one-pole cannot lose more than (1 − cos ω₀) / (1 − cos ω₁) in power
    // between the two: a darker request is held at the darkest it can do.
    // Unlike a fixed coefficient, the highs then die in the same TIME at every
    // pitch: a low drum's loop makes few trips a second, and a fixed per-trip
    // loss would leave its highs ringing for seconds.
    static float loss_pole(float w0, float period, float t60, float t60_hf) {
        const float w1 = brc::phase_inc(kHfHz);
        if (w1 <= 1.05f * w0 || t60_hf >= t60) return 0.0f;   // no loss filter
        const float c0 = cosf(w0), c1 = cosf(w1);
        const float r = expf(brc::kLn0001 * period * (1.0f / t60_hf - 1.0f / t60));
        const float r2 = fmaxf(r * r, 1.0001f * (1.0f - c0) / (1.0f - c1));
        if (r2 >= 1.0f) return 0.0f;
        const float B = (c0 - r2 * c1) / (1.0f - r2);
        return brc::clampf(B - sqrtf(fmaxf(B * B - 1.0f, 0.0f)), 0.0f, kMaxLossPole);
    }

    static float lp_mag(float w, float a) {
        const float b = 1.0f - a;
        return a / sqrtf(1.0f - 2.0f * b * cosf(w) + b * b);
    }
    static float hp_mag(float w, float a) {
        const float b = 1.0f - a;
        return b * sqrtf((2.0f - 2.0f * cosf(w)) / (1.0f - 2.0f * b * cosf(w) + b * b));
    }

    void strike_loop(VoiceState& v, float tune, const LayerKnobs& k, bool ringing) {
        const Voicing& c = voicing();
        const float vel = v.current_velocity;
        loop::Drum& d = v.drum;
        const int slot = (int)(&v - m_voices);
        d.buf = s_loop_lines[slot];
        d.ap  = s_ap_lines[slot];
        if (!ringing) {   // a ringing loop keeps its lines: the strike adds to them
            memset(d.buf, 0, sizeof(float) * loop::kLoopLen);
            memset(d.ap, 0, sizeof(float) * loop::kApLen);
        }
        d.level = c.lp_level * k.presence;
        d.lcg   = v.exciter.noise_gen.seed | 1u;

        // The glide: a full-velocity strike starts bend above the note.  A
        // negative bend (the Bend knob can't make one) would start below it.
        d.bend     = brc::clampf((c.bend * k.bend + k.bend_add) * vel, 0.0f, 2.0f);
        d.bend_env = (d.bend > 0.0f) ? 1.0f : 0.0f;
        d.bend_mul = expf(-1.0f / fmaxf(1.0f, ms_to_samples(c.bend_ms * k.bend_time)));

        // The loop, tuned so its first resonance is the note: one period of
        // phase is the delay line plus the phase delays of the all-pass, the
        // loss filter and the DC blocker at that frequency.
        const float hz     = brc::clampf(c.lp_hz * tune, 30.0f, 0.4f * brc::kSampleRate);
        const float w0     = brc::phase_inc(hz);
        const float period = brc::kSampleRate / hz;
        const float t60    = ms_to_samples(c.lp_t60_ms * k.length);
        const float t60_hf = t60 * brc::clampf(c.lp_hf * k.bright, 0.005f, 1.0f);
        d.damp = 1.0f - loss_pole(w0, period, t60, t60_hf);
        d.dc_a = 1.0f - expf(-brc::phase_inc(kDcHz));
        d.g    = brc::clampf(c.ap_gain + k.inharm, -0.95f, 0.95f);
        float m = brc::clampf(c.ap_ratio * k.apdelay * period, 0.0f, (float)(loop::kApLen - 2));
        const float fixed = period - lp_delay(w0, d.damp) + hp_lead(w0, d.dc_a);
        float n = fixed - ((m > 0.0f) ? ap_delay(w0, m, d.g) : 0.0f);
        for (int i = 0; i < 8 && n < 2.0f; ++i) {   // the all-pass ate the period: shorten it
            m *= 0.7f;
            n = fixed - ap_delay(w0, m, d.g);
        }
        d.m0 = m;
        d.n0 = brc::clampf(n, 2.0f, (float)(loop::kLoopLen - 2));

        // The loss per trip, for the T60 at the note, the filters' own loss
        // there given back.
        const float trip = expf(brc::kLn0001 * period / fmaxf(t60, period));
        d.loop_gain = fminf(trip / (lp_mag(w0, d.damp) * hp_mag(w0, d.dc_a)), kMaxLoopGain);
        d.flip = brc::clampf(c.blend + k.blend_add, 0.0f, 0.5f);

        // The excitation: one sine cycle exc_ratio periods long (a stiffer
        // stick, a shorter one), the noise under its window, and the strike
        // position's comb.
        const float stick = k.stick * brc::knob_exp2(k.vel_stick * vel);
        const float len = brc::clampf(c.exc_ratio * period / stick, 2.0f, (float)loop::kLoopLen);
        d.exc_len = (uint32_t)len;
        const float rot = 6.2831853f / (float)d.exc_len;
        d.rot_c = cosf(rot);
        d.rot_s = sinf(rot);
        d.half_c = cosf(0.5f * rot);
        d.half_s = sinf(0.5f * rot);
        d.tone    = vel;
        d.noise   = c.exc_noise * k.noise * vel;
        d.noise_a = 1.0f - expf(-brc::phase_inc(brc::clampf(c.exc_noise_hz * k.noise_hz, 20.0f, 20000.0f)));
        const float pos = brc::clampf(c.exc_pos + k.position, 0.0f, 0.5f);
        d.exc_d = (uint32_t)(pos * period);
        d.hit   = (d.exc_d > 0u) ? 1.0f : 0.0f;

        // The snares, on the head's motion (a preset without them gets the
        // article's: 3 kHz, Q 2, when the Buzz knob adds them).
        d.sn_level   = c.sn_level * k.snares + k.snares_add;
        d.sn_crackle = brc::clampf(c.sn_crackle, 0.0f, 1.0f);
        if (d.sn_level > 0.0f) {
            const float shz = fminf(brc::or_default(c.sn_hz, 1.0f, 3000.0f) * k.snare_tone, 20000.0f);
            const float r = pole_for_q(shz, brc::or_default(c.sn_q, 0.01f, 2.0f));
            d.sn_bp.tune(shz, r);
            d.sn_g   = 1.0f - r;
            d.sn_att = 1.0f - expf(-1.0f / fmaxf(1.0f, ms_to_samples(brc::or_default(c.sn_att_ms, 0.001f, 1.0f))));
            d.sn_rel = 1.0f - expf(-1.0f / fmaxf(1.0f, ms_to_samples(brc::or_default(c.sn_rel_ms, 0.001f, 30.0f))));
        }
    }
};

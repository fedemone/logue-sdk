#pragma once
// Brachetti Cymbal — synth engine of the brachetti_cymbal unit.
//
// The dense-resonator metal: Cymbal (crash), Gong, HHat-O, Ride, RidBel and
// Splash.  A voice is one CymbalVoice: a large bank of exponentially detuned
// two-pole ringers driven by band-split noise and a stick impulse.
//
//   presets.h   the knob layout, the presets and how each one strikes
//   voice.h     one voice
//   this file   the voice policy and CPU budget, the knob mappings, and the
//               voices → the master stage
//
// The master stage and the knob helpers are the ones every Brachetti unit
// shares (../brachetti_common); this unit has its own knob layout and its own
// voice, so it does not use the modal units' engine.
//
// Split from the 40-preset brachetti unit (platform/drumlogue/brachetti,
// which documents its history).  At the knob values the presets ship with,
// each renders bit-identically to the same preset there; tools/brachetti_equiv
// checks it.
#include <cstddef>
#include <cstdint>

#include "../common/runtime.h"
#include "unit.h"

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, env_mul, kSampleRate, knob_exp2
//   knobs.h          Anchors, SequencerNote, VelocityBias, format_hz,
//                    format_int, format_percent, is_host_knob, kKnobTextSize,
//                    knob::clamp_int, knob::decay, knob::hit_pos,
//                    knob::inharm, knob::mallet_res, knob::mallet_stiff,
//                    knob::material, knob::percent, knob::poly, knob::release,
//                    knob::tube_radius, knob_text, midi_velocity, off_anchor,
//                    seed_host_knobs
//   master_stage.h   MasterStage, soft_saturate_bus, tone_tilt
//   voice_base.h     any_fading, fade_out_all, kNumVoices
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/voice_base.h"
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
        // The knobs LoadPreset does not write start at their header.c defaults.
        brc::seed_host_knobs(*this, kHostKnobs);
        Reset();
        LoadPreset(0);
        return k_unit_err_none;
    }

    inline void Teardown() {}   // static memory only: nothing to free

    // A hard flush of RUNTIME state — never a parameter's value.
    inline void Reset() {
        for (VoiceState& v : m_voices) {
            v.is_active = false;
            v.reset_for_strike();
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

    // The current preset's root note and the note the sequencer plays (root +
    // Pitch).  Host tools use these; nothing on the device reads them.
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
        m_note.set_root((uint8_t)row[k_paramPitch]);   // the Pitch column is the root
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (p != k_paramProgram && !brc::is_host_knob(kHostKnobs, p)) setParameter(p, row[p]);

        // REFERENCE ANCHORS: the shipped knob values every mapping in
        // shape_strike pivots on.
        namespace knob = brc::knob;
        m_anchor.decay        = knob::decay(row[k_paramDkay]);
        m_anchor.mallet_stiff = knob::mallet_stiff(row[k_paramMlltStif]);
        m_anchor.inharm       = knob::inharm(row[k_paramInharm]);
        m_anchor.material     = knob::material(row[k_paramMterl]);
        m_anchor.hit_pos      = knob::hit_pos(row[k_paramHitPos]);
        m_anchor.tube_radius  = knob::tube_radius(row[k_paramTubRad]);
        m_anchor.mallet_res   = knob::mallet_res(row[k_paramMlltRes]);
        m_anchor.release      = knob::release(row[k_paramRel]);
        m_anchor.vel_stiff    = knob::percent(row[k_paramVlMllStf]);

        m_master.out_trim = kOutTrim[idx];
        if (changed) m_master.hold_while_fading(prev_drive, prev_trim, brc::any_fading(m_voices));
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
        // Semitones from the preset's root: transposes the whole anchor
        // spectrum (a smaller or larger cymbal).  Stored clamped.
        case k_paramPitch:    m_params[index] = m_note.set_pitch(value); break;
        // The voice cap.  kCostBudget bounds the CPU on top of it, which in
        // practice affords two simultaneous cymbal voices.
        case k_paramPoly:     m_poly = brc::knob::poly(value); break;
        case k_paramVelocity: m_velocity.set(value); break;
        // Resonator-bank DENSITY: 25 + 5 × Partls % of the preset's bank.
        // Shipped rows store 3 = 40 %.
        case k_paramPartls:
            if (value >= 0 && value <= 7) m_bank_percent = (uint8_t)(25 + 5 * value);
            break;
        // Stored ÷10 (0-200 = 0-2000), stored clamped.
        case k_paramDkay:     m_params[index] = brc::knob::clamp_int(value, 0, 200); break;
        case k_paramTone:     m_master.set_tone(value); break;
        case k_paramCutoff:
        case k_paramResnc:    m_master.set_filter(m_params[k_paramCutoff], m_params[k_paramResnc]); break;
        case k_paramGain:     m_master.set_gain(value); break;
        // MlltRes, MlltStif, VlMllRes, VlMllStf, Mterl, HitPos, Rel, Inharm,
        // TubRad: read at the next strike.
        default: break;
        }
    }

    inline const char* getParameterStrValue(uint8_t index, int32_t value) const {
        switch (index) {
        case k_paramProgram:  return preset_name((uint8_t)value);   // the preset being browsed
        case k_paramPartls:   // the bank density this position gives
            return brc::format_percent(brc::knob_text(index), brc::kKnobTextSize,
                                       (int)(25 + 5 * brc::knob::clamp_int(value, 0, 7)), "Rs");
        case k_paramMlltStif: return brc::format_int(brc::knob_text(index), brc::kKnobTextSize, (int)(value * 100));
        case k_paramDkay:     return brc::format_int(brc::knob_text(index), brc::kKnobTextSize, (int)(value * 10));
        case k_paramCutoff:   return brc::format_hz(brc::knob_text(index), brc::kKnobTextSize, value);
        case k_paramResnc:    return brc::format_int(brc::knob_text(index), brc::kKnobTextSize, (int)(value * 10));
        case k_paramPoly:     return brc::format_int(brc::knob_text(index), brc::kKnobTextSize, brc::knob::poly(value));
        default:              return "---";
        }
    }

    // ==========================================================================
    // Notes
    // ==========================================================================
    inline void NoteOn(uint8_t note, uint8_t velocity) {
        // The Velocity knob biases the strike once, here: it drives the
        // stick, the noise drivers and the ring.
        const float vel = m_velocity.apply(brc::midi_velocity(velocity));
        m_last_voice = pick_voice(note);
        VoiceState& v = m_voices[m_last_voice];

        // A cymbal still ringing when its slot is struck again (or stolen)
        // keeps its ring: struck metal keeps sounding through the next strike.
        // Read before reset_for_strike() clears it.
        const bool still_ringing = v.cymbal.active;
        v.is_active        = true;
        v.current_note     = note;
        v.current_velocity = vel;
        v.reset_for_strike();

        // The bank's lane detune is drawn from this seed, so it folds in the
        // note, the slot and the RAW MIDI velocity.
        const uint32_t seed = (uint32_t)note * 1103515245u
                            ^ (uint32_t)m_last_voice * 12345u
                            ^ (uint32_t)velocity * 2654435761u;

        const Strike& s = kStrikes[m_preset_idx];
        CymbalConfig cc = s.config;
        cc.resonators = bank_size(m_last_voice, cc.resonators);
        // Pitch transposes the whole anchor spectrum (2^(Δ/12) from the
        // preset's root, so the shipped note plays the shipped sound).
        float pitch_ratio = exp2f((float)((int)note - s.ref_note) * (1.0f / 12.0f));
        shape_strike(cc, pitch_ratio);
        v.cymbal.strike(cc, v.current_velocity, s.ring_scale, pitch_ratio, seed, still_ringing);
        v.cymbal.velocityGain *= s.level;
    }

    // A cymbal has no release by design: the drumlogue fires gate on and off
    // in the same tick, so a gate-off must not choke the tail.  AllNoteOff
    // fades the voices out instead.
    inline void NoteOff(uint8_t note) { (void)note; }

    inline void GateOn(uint8_t velocity) {
        if (velocity == 0) { GateOff(); return; }
        NoteOn(m_note.strike(), velocity);
    }
    inline void GateOff() { NoteOff(m_note.gate_note); }

    // Panic: a click-free output fade (τ 8 ms: the voice is off ~55 ms later).
    inline void AllNoteOff() {
        for (VoiceState& v : m_voices) v.cymbal.fadeMul = brc::env_mul(0.008f, brc::kSampleRate);
    }

    // A cymbal is transposed by its note (Pitch), not bent.
    inline void PitchBend(uint16_t bend) { (void)bend; }

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
        if (any_voice) brc::soft_saturate_bus(out, frames, 0.35f);   // stacked voices: soft headroom first
        m_master.render(out, frames);
    }

private:
    // ── Voice policy and CPU budget ─────────────────────────────────────────
    // A voice younger than this is off-limits as a steal candidate.  It covers
    // more than the gong's 0.25 s driver attack on purpose: magEnv is a 10 ms
    // average of a dense inharmonic wash, so within the first half-second it
    // fluctuates enough to rank a fresher (louder) voice below an older one.
    // Inside the window the age ordering is the reliable signal, so the OLDEST
    // voice is stolen; past it the faintest tail is genuinely the faintest.
    static constexpr float kStealProtectSec = 0.60f;

    // The budget is counted in resonator-lane equivalents.  A voice costs far
    // more than its resonators: its FIXED per-sample work — pink noise, the two
    // driver one-poles, the PM, the rumble filter, the level follower — is
    // worth ~124 lanes on its own (79 % of a 32-lane voice), so every ringing
    // voice charges kVoiceFixedLanes PLUS its bank.
    static constexpr int kVoiceFixedLanes = 124;

    // The ceiling admits two full voices and denies a third (gong 440 + 124 +
    // 32 = 596 > 440; crash at full density 360 + 124 + 32 = 516 > 440), so
    // two DIFFERENTLY pitched gongs at their full 96-lane bank always get a
    // voice each; repeated strikes on one note cost a single voice.  The bank
    // is nearly free and the VOICE is what costs: measured on the real
    // processBlock, taking the gong from 32 to 96 lanes moves one voice 13,990
    // → 14,345 ns/block (+2.5 %), and a second full voice adds ~2,000 ns.  Two
    // 96-lane gongs measure ~16.4 µs/block, against ~50 µs/block known to run
    // on the drumlogue and ~96 µs known to overload it.
    static constexpr int kCostBudget = 440;

    // The smallest bank that still reads as a cymbal rather than a chord.
    static constexpr int kMinResonators = 32;

    static bool ringing(const VoiceState& v) { return v.is_active && v.cymbal.active; }
    static int  cost(const VoiceState& v)    { return kVoiceFixedLanes + (int)v.cymbal.resCount; }

    // The slot a strike takes.  Poly caps the simultaneous voices: below the
    // cap a silent slot is used, at the cap a ringing voice is stolen.
    //  * A re-strike ON THE SAME NOTE re-excites its own voice: a gong is one
    //    piece of metal, and striking it again adds energy to the ring already
    //    there.  Only a different note (a different-sized instrument) asks for
    //    a slot of its own.  A voice the previous preset left fading is not
    //    this preset's plate and is never re-excited.
    //  * A new voice is only ADDED if the cheapest bank it could get still
    //    fits kCostBudget; otherwise a slot is reused, so the cost cannot grow.
    //  * Stealing never takes the voice just struck: a freshly struck gong
    //    swells for 0.25 s, so it is the quietest voice while it is about to be
    //    the loudest.  Voices younger than kStealProtectSec are off-limits; if
    //    every voice is young, the OLDEST goes.
    uint8_t pick_voice(uint8_t note) const {
        const uint32_t protect = (uint32_t)(kStealProtectSec * brc::kSampleRate);
        int active = 0, free_slot = -1, faded = -1, oldest = -1, same = -1;
        int busy_cost = 0;
        float faded_mag = 1e9f;
        uint32_t oldest_age = 0u;
        for (int i = 0; i < brc::kNumVoices; ++i) {
            const VoiceState& v = m_voices[i];
            if (!ringing(v)) {
                if (free_slot < 0) free_slot = i;
                continue;
            }
            ++active;
            busy_cost += cost(v);
            if (same < 0 && v.current_note == note && v.fade_mul >= 1.0f) same = i;
            if (v.cymbal.sampleIndex >= protect && v.cymbal.magEnv < faded_mag) {
                faded_mag = v.cymbal.magEnv;
                faded = i;
            }
            if (oldest < 0 || v.cymbal.sampleIndex >= oldest_age) {
                oldest_age = v.cymbal.sampleIndex;
                oldest = i;
            }
        }
        const bool affordable = (busy_cost + kVoiceFixedLanes + kMinResonators) <= kCostBudget;
        if (same >= 0) return (uint8_t)same;
        if (active < (int)m_poly && affordable && free_slot >= 0) return (uint8_t)free_slot;
        if (faded >= 0)     return (uint8_t)faded;
        if (oldest >= 0)    return (uint8_t)oldest;
        if (free_slot >= 0) return (uint8_t)free_slot;
        return m_last_voice;
    }

    // The bank for the voice in `slot`: the preset's bank × the Partls density
    // × a voice-pressure step (the 3rd and 4th simultaneous voices get smaller
    // banks — in a dense stack the loss is masked, and rolls cannot overload
    // the CPU), then capped by what the budget has left once the new voice's
    // fixed cost is paid.  Banks already ringing keep their size; the newest
    // voice absorbs the trim.
    uint16_t bank_size(uint8_t slot, uint16_t preset_bank) const {
        int others = 0, busy_cost = 0;
        for (int i = 0; i < brc::kNumVoices; ++i) {
            if (i == (int)slot || !ringing(m_voices[i])) continue;
            ++others;
            busy_cost += cost(m_voices[i]);
        }
        float density = (float)m_bank_percent * 0.01f;
        if (others >= 3)      density *= 0.67f;
        else if (others == 2) density *= 0.80f;
        int count = (int)((float)preset_bank * density);
        const int room = kCostBudget - busy_cost - kVoiceFixedLanes;
        if (count > room) count = room;
        count = (count + 3) & ~3;
        if (count < kMinResonators) count = kMinResonators;
        if (count > (int)kCymbalMaxResonators) count = (int)kCymbalMaxResonators;
        return (uint16_t)count;
    }

    // ── The knobs on the strike (REFERENCE-ANCHORED) ────────────────────────
    // The cymbal has no mallet exciter and no modal bank, so each knob maps to
    // a natural property of the metal or the stick instead, anchored at the
    // shipped value: the calibrated cymbals render unchanged and only knob
    // MOVEMENT bites.  The depths are wide on purpose (a subtle cymbal knob
    // reads as a dead one); each mapping is exactly neutral at Δ = 0.
    void shape_strike(CymbalConfig& cc, float& pitch_ratio) const {
        stick_hit(cc);
        stick_stiffness(cc);
        ring_length(cc);
        jitter_spread(cc);
        metal_brightness(cc);
        strike_locus(cc);
        instrument_size(cc, pitch_ratio);
        ring_presence(cc);
        beater_hardness(cc);
    }

    // VlMllRes → the stick hit ("tang").  Positive values boost the stick and
    // sharpen the noise-driver attack (punch without running the boosted
    // click into the output clamp); negative values soften toward a felt
    // mallet.  Velocity still scales the hit on top.
    void stick_hit(CymbalConfig& cc) const {
        const float hit = brc::knob::percent(m_params[k_paramVlMllRes]);
        cc.stickLevel = fminf(2.0f, cc.stickLevel * brc::knob_exp2(1.2f * hit));
        if (hit > 0.0f) {
            cc.lowAttackSec *= brc::knob_exp2(-1.0f * hit);   // up to 2× snappier
            cc.thwackSec    *= 1.0f + 0.6f * hit;              // a longer ping = "tang"
        }
    }

    // VlMllStf → stick stiffness: a harder tip bites faster, the contact ping
    // gets shorter and the strike brighter; a soft felt tip rounds all three off.
    void stick_stiffness(CymbalConfig& cc) const {
        const float d = brc::knob::percent(m_params[k_paramVlMllStf]) - m_anchor.vel_stiff;
        if (!brc::off_anchor(d)) return;
        cc.lowAttackSec *= brc::knob_exp2(-1.2f * d);
        cc.thwackSec    *= brc::clampf(brc::knob_exp2(-0.8f * d), 0.40f, 2.50f);
        cc.hfTilt        = brc::clampf(cc.hfTilt + 0.8f * d, 0.0f, 4.0f);
    }

    // Dkay → the overall ring length (body and sizzle together); Rel trims the
    // tail on top, gently on the body and fully on the sizzle, so the two give
    // independent "how long" and "how sizzly".
    void ring_length(CymbalConfig& cc) const {
        const float dkay = brc::clampf(
            brc::knob_exp2(3.0f * (brc::knob::decay(m_params[k_paramDkay]) - m_anchor.decay)), 0.15f, 6.0f);
        const float rel = brc::clampf(
            brc::knob_exp2(2.4f * (brc::knob::release(m_params[k_paramRel]) - m_anchor.release)), 0.15f, 6.0f);
        cc.decaySec     *= brc::clampf(dkay * sqrtf(rel), 0.15f, 6.0f);
        cc.highDecaySec *= brc::clampf(dkay * rel, 0.15f, 9.0f);
    }

    // Inharm → the jitter spread (beating density, shimmer thickness).  Not as
    // wide as the others: at depths of 2.4 and 3.0 the detune smears the high
    // modes onto the fLo / Nyquist clamps and the measured HF swing across the
    // knob SHRINKS; 2.0 buys the most audible spread.
    void jitter_spread(CymbalConfig& cc) const {
        const float d = brc::knob::inharm(m_params[k_paramInharm]) - m_anchor.inharm;
        cc.jitterSemis = brc::clampf(cc.jitterSemis * brc::knob_exp2(2.0f * d), 0.0f, 6.0f);
    }

    // Mterl → metal brightness (hard bronze sustains its highs, a soft alloy
    // reads dull): tilts the bank's HF weight and moves the ceiling.
    void metal_brightness(CymbalConfig& cc) const {
        const float d = brc::knob::material(m_params[k_paramMterl]) - m_anchor.material;
        if (!brc::off_anchor(d)) return;
        cc.hfTilt = brc::clampf(cc.hfTilt + d * 3.6f, 0.0f, 5.0f);
        cc.maxHz  = brc::clampf(cc.maxHz * brc::knob_exp2(1.3f * d), 2500.0f, 20000.0f);
    }

    // HitPos → the strike locus: toward the BELL (up) more stick ping and
    // pitched ring, less wash; toward the EDGE (down) more broadband wash and
    // a softer ping.  Bipolar, so the mapping follows the knob below centre.
    void strike_locus(CymbalConfig& cc) const {
        const float d = brc::knob::hit_pos(m_params[k_paramHitPos]) - m_anchor.hit_pos;
        if (!brc::off_anchor(d)) return;
        cc.stickLevel     = fminf(3.0f, cc.stickLevel * brc::knob_exp2(2.4f * d));
        cc.resonatorLevel = fmaxf(0.03f, cc.resonatorLevel * brc::knob_exp2(1.4f * d));
        cc.noiseLevel     = fmaxf(0.0f, cc.noiseLevel * brc::knob_exp2(-1.7f * d));
    }

    // TubRad → instrument SIZE: transposes the whole anchor spectrum like a
    // bigger (lower) or smaller (higher) cymbal, and adds breath under the ring.
    void instrument_size(CymbalConfig& cc, float& pitch_ratio) const {
        const float d = brc::knob::tube_radius(m_params[k_paramTubRad]) - m_anchor.tube_radius;
        if (!brc::off_anchor(d)) return;
        pitch_ratio    *= brc::clampf(brc::knob_exp2(-1.2f * d), 0.35f, 2.8f);
        cc.shimmerLevel = fmaxf(0.0f, cc.shimmerLevel * brc::knob_exp2(1.3f * d));
    }

    // MlltRes → ring presence (metal vs air): up, the bank speaks over the
    // noise bed (a gong that RINGS); down, the wash dominates.
    void ring_presence(CymbalConfig& cc) const {
        const float d = brc::knob::mallet_res(m_params[k_paramMlltRes]) - m_anchor.mallet_res;
        if (!brc::off_anchor(d)) return;
        cc.resonatorLevel = fmaxf(0.03f, cc.resonatorLevel * brc::knob_exp2(2.4f * d));
        cc.shimmerLevel   = fmaxf(0.0f,  cc.shimmerLevel   * brc::knob_exp2(1.2f * d));
        cc.noiseLevel     = fmaxf(0.0f,  cc.noiseLevel     * brc::knob_exp2(-0.9f * d));
    }

    // MlltStif → beater hardness, the static counterpart of VlMllStf: a hard
    // stick gives a short bright contact ping and a faster bite, a soft mallet
    // a slow dark swell — a gong struck with a stick against one bloomed in
    // with a felt beater.
    void beater_hardness(CymbalConfig& cc) const {
        const float d = brc::knob::mallet_stiff(m_params[k_paramMlltStif]) - m_anchor.mallet_stiff;
        if (!brc::off_anchor(d)) return;
        cc.stickLevel   = fminf(3.0f, cc.stickLevel * brc::knob_exp2(1.8f * d));
        cc.thwackSec   *= brc::clampf(brc::knob_exp2(-1.1f * d), 0.30f, 3.00f);
        cc.lowAttackSec = fmaxf(0.0005f, cc.lowAttackSec * brc::knob_exp2(-1.6f * d));
        cc.hfTilt       = brc::clampf(cc.hfTilt + 1.1f * d, 0.0f, 5.0f);
    }

    // ── Rendering ───────────────────────────────────────────────────────────
    // One voice for one block, added into the mono bus (out[i * 2]).
    void render_voice(VoiceState& v, float* __restrict out, size_t frames, float tone) {
        const bool tone_active = (tone != 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            float x = v.cymbal.process();
            x = v.fade_out(x);
            if (tone_active) x = brc::tone_tilt(x, v.tone_lp, tone);
            out[i * 2] += x * brc::MasterStage::kBusGain;
            if (!v.cymbal.active) { v.is_active = false; break; }
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
    uint8_t            m_poly         = 4;    // Poly knob: voice cap 1-4
    uint8_t            m_bank_percent = 40;   // Partls: bank density, 25-60 %
};

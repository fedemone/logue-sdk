#pragma once
// Brachetti common library — the engine the six modal units share.
//
// Kick, Tom, Snare, Bar, Plate and World are one engine with different family
// layers: the same 24 knobs, the same preset loading, the same note routing
// and block loop.  That engine lives here; a unit derives from it (CRTP:
// static dispatch, no virtual calls) and supplies only its family:
//
//   class BrachettiSynth : public brc::ModalSynth<BrachettiSynth, VoiceState> {
//       // data (presets.h)
//       static constexpr int    k_NumPrograms;              // the preset count
//       static const char*      preset_name(uint8_t idx);
//       static const int16_t*   preset_knobs(uint8_t idx);   // one row per preset
//       static const ModalPresetConfig& modal_config(uint8_t idx);
//       static float            out_trim(uint8_t idx);
//       // the family
//       void NoteOn(uint8_t note, uint8_t velocity);          // the strike recipe
//       void render_voice(VoiceState&, float* bus, size_t frames, float tone);
//       void load_voicing_into(VoiceState& v);                // LoadPreset, per voice
//   };
//
// Provides: ModalLayout (the 24-knob layout: the common pages, then the
// standard dedicated set), kFamilyKnob, kModalHostKnobs (the knobs the host
// owns, with their defaults), ModalSynth.
//
// The knobs are handled in three tiers: the COMMON knobs (pages 1-2,
// common_knobs.h) the same on every unit; the standard DEDICATED knobs
// (pages 3-6), each a ROLE the shared modal engine has a setter or a strike
// mapping for; and the FAMILY knobs a unit puts in a standard slot instead
// (role_at returns kFamilyKnob there), which only the unit reads.  A unit that
// gives a standard role's slot away supplies that knob's per-preset value
// (fixed_knob), so its presets play exactly as before.
//
// A unit may also define NoteOff (GateOff routes through it).  Its voice type
// derives from VoiceBase and defines reset_for_strike(); it may define
// on_unit_reset() for state only Reset() clears.
#include <cstddef>
#include <cstdint>
#include "unit.h"
#include "common_knobs.h"
#include "dsp_math.h"
#include "exciter.h"
#include "knobs.h"
#include "master_stage.h"
#include "modal_bank.h"
#include "voice_base.h"

namespace brc {

// The 24 knobs, slot by slot.  MUST match each unit's header.c: the preset
// tables have one column per slot, and a parameter added here without its
// row in header.c (or vice versa) silently shifts every column of every
// preset.
//
// Pages 1-2 are the COMMON knobs (common_knobs.h: the same on all nine
// units).  Pages 3-6 are the family's DEDICATED knobs; the standard set below
// is what the shared modal engine drives, and the enumerator of each is also
// its ROLE.  A unit may give a standard slot to a knob of its own (a ROLE
// change, see role_at in ModalSynth) and may relabel any dedicated knob in
// its header.c to say what it does in that family.
struct ModalLayout : CommonLayout {
    enum Param : uint8_t {
        // page 3: the strike
        k_paramMlltRes = k_firstDedicated,  // 8
        k_paramMlltStif,     // 9
        k_paramVlMllRes,     // 10
        k_paramVlMllStf,     // 11
        // page 4: the bank and its length
        k_paramPartls,       // 12
        k_paramModel,        // 13
        k_paramDkay,         // 14
        k_paramRel,          // 15
        // page 5: the body
        k_paramMterl,        // 16
        k_paramHitPos,       // 17
        k_paramInharm,       // 18
        k_paramTubRad,       // 19
        // page 6: the noise burst
        k_paramNzMix,        // 20
        k_paramNzRes,        // 21
        k_paramNzFltr,       // 22
        k_paramNzFltFrq,     // 23
        k_lastParamIndex     // marker
    };
};
static_assert(ModalLayout::k_lastParamIndex == 24, "the modal units' header.c declares 24 params");

// A dedicated slot whose knob is the family's own (no standard role).
constexpr uint8_t kFamilyKnob = 0xFF;

// The knobs the host owns on the modal units: the common ones, plus VlMllRes
// where a unit keeps it (see ModalSynth::is_host_owned).
static const HostKnob kModalHostKnobs[] = {
    BRC_COMMON_HOST_KNOBS,
    { ModalLayout::k_paramVlMllRes, 0 },
};

template <class Unit, class Voice>
class ModalSynth : public ModalLayout {
public:
    // ==========================================================================
    // Lifecycle
    // ==========================================================================
    int8_t Init(const unit_runtime_desc_t* desc) {
        // The drumlogue is 48 kHz stereo; anything else is not this unit's host.
        if (desc->samplerate != (uint32_t)kSampleRate) return k_unit_err_samplerate;
        if (desc->output_channels != 2) return k_unit_err_geometry;

        // The knobs LoadPreset does not write start at their header.c defaults
        // (on a fresh object they would read 0; on an object Init() runs on
        // again, whatever the last session left), and so does the state the
        // noise-filter knobs own once a preset is loaded.
        seed_host_knobs(*this, kModalHostKnobs);
        for (Voice& v : m_voices) v.exciter.init_noise_filter();
        Reset();
        LoadPreset(0);
        return k_unit_err_none;
    }

    void Teardown() {}   // static memory only: nothing to free

    // A hard flush of RUNTIME state — never a parameter's value (the SDK:
    // unit_reset() clears notes, envelopes and delay lines, but parameter
    // values must not be reset).
    void Reset() {
        for (Voice& v : m_voices) {
            v.is_active    = false;
            v.is_releasing = false;
            v.reset_for_strike();
            v.exciter.clear_noise_filter();   // memory only: NzFltr / NzFltFrq stay
            v.on_unit_reset();
        }
        m_master.reset();
    }

    void Resume() {}
    void Suspend() { AllNoteOff(); Reset(); }

    // ==========================================================================
    // Presets
    // ==========================================================================
    uint8_t getPresetIndex() const { return m_preset_idx; }

    // The current preset's root note and the note the sequencer plays (root +
    // Pitch).  Host tools use these; nothing on the device reads them.
    uint8_t PresetRootNote() const { return m_common.note.root; }
    uint8_t PlayedNote() const     { return m_common.note.note; }

    void LoadPreset(uint8_t idx) {
        if (idx >= Unit::k_NumPrograms) return;   // before anything is written

        // A voice cannot survive a preset change: it fades out (~10 ms) under
        // its own settings.  Only on a REAL change: Init()'s LoadPreset(0) and
        // re-selecting the current preset leave ringing voices alone.
        const bool changed = (idx != m_preset_idx);
        const float prev_drive = m_master.drive;
        const float prev_trim  = m_master.out_trim;
        if (changed) fade_out_all(m_voices);

        m_preset_idx = idx;
        m_params[k_paramProgram] = idx;   // whether set by the knob or by unit_load_preset
        const int16_t* row = Unit::preset_knobs(idx);
        m_common.note.set_root((uint8_t)row[k_paramPitch]);
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (loads_with_preset(p)) setParameter(p, row[p]);
        // A standard knob the unit gave its slot away from still has a value:
        // the preset's own, which the unit supplies (fixed_knob).
        for (uint8_t r = k_firstDedicated; r < k_lastParamIndex; ++r)
            if (slot_of(r) == kNoSlot) apply_role(r, Unit::fixed_knob(idx, r));
        capture_anchors(row);

        m_master.out_trim = Unit::out_trim(idx);
        if (changed) m_master.hold_while_fading(prev_drive, prev_trim, any_fading(m_voices));

        // The preset's per-voice coefficients, into every voice that is not
        // fading out.  So re-selecting the preset already playing re-strikes
        // a ringing voice's layers, as the original unit does.
        for (Voice& v : m_voices)
            if (!v.fading()) unit().load_voicing_into(v);
    }

    // ==========================================================================
    // Knobs
    // ==========================================================================
    int32_t getParameterValue(uint8_t index) const {
        return (index < k_lastParamIndex) ? m_params[index] : 0;
    }

    void setParameter(uint8_t index, int32_t value) {
        if (index >= k_lastParamIndex) return;
        m_params[index] = value;
        if (index == k_paramProgram) { LoadPreset((uint8_t)value); return; }
        if (is_common_knob(index)) {
            m_params[index] = m_common.set(index, value, m_params, m_master);
            return;
        }
        // A dedicated knob: the shared engine's setter for its role; a
        // family knob (kFamilyKnob) is read by the unit at the strike.
        const uint8_t role = Unit::role_at(index);
        if (role != kFamilyKnob) m_params[index] = apply_role(role, value);
    }

    const char* getParameterStrValue(uint8_t index, int32_t value) const {
        if (index == k_paramProgram) return Unit::preset_name((uint8_t)value);   // the preset being browsed
        if (is_common_knob(index)) {
            const char* c = common_knob_text(index, value);
            return c ? c : "---";
        }
        const char* s = nullptr;
        switch (Unit::role_at(index)) {
        case k_paramModel:    s = model_name(value); break;
        case k_paramPartls:
            return format_mode_count(knob_text(index), kKnobTextSize,
                                     Unit::modal_config(m_preset_idx).mode_count, value, m_anchor.partls);
        case k_paramNzFltr:   s = noise_filter_name(value); break;
        case k_paramMlltStif: return format_int(knob_text(index), kKnobTextSize, (int)(value * 100));
        case k_paramDkay:     return format_int(knob_text(index), kKnobTextSize, (int)(value * 10));
        case k_paramNzFltFrq: return format_hz(knob_text(index), kKnobTextSize, value);
        case kFamilyKnob:     s = Unit::family_knob_text(index, value); break;
        default: break;
        }
        return s ? s : "---";
    }

    // ── What a unit may redefine (CRTP: its own static of the same name hides
    // these defaults) ──────────────────────────────────────────────────────
    // The standard role of a dedicated slot, or kFamilyKnob for a knob of the
    // unit's own.  The default is the standard layout: each slot its own role.
    static constexpr uint8_t role_at(uint8_t slot) { return slot; }
    // The value of a standard knob whose slot the unit gave away, for preset
    // idx (it then plays as the preset always did).
    static int32_t fixed_knob(uint8_t idx, uint8_t role) { (void)idx; (void)role; return 0; }
    // A family knob's display string (strings-type knobs only).
    static const char* family_knob_text(uint8_t slot, int32_t value) { (void)slot; (void)value; return nullptr; }

    // ==========================================================================
    // Notes (the unit supplies NoteOn: its family's strike)
    // ==========================================================================
    // Releases the noise; a struck body rings on under its own decay.
    void NoteOff(uint8_t note) {
        for (Voice& v : m_voices) {
            if (v.is_active && !v.is_releasing && v.current_note == note) {
                v.is_releasing = true;
                v.exciter.release_noise();
            }
        }
    }

    // The sequencer: the preset's root + Pitch.  External MIDI (NoteOn)
    // plays the note it carries.
    void GateOn(uint8_t velocity) {
        if (velocity == 0) { GateOff(); return; }
        unit().NoteOn(m_common.note.strike(), velocity);
    }
    void GateOff() { unit().NoteOff(m_common.note.gate_note); }

    // Panic: everything fades out under the Dkay-set release.
    void AllNoteOff() {
        for (Voice& v : m_voices) {
            v.is_releasing = true;
            v.exciter.release_noise();
            v.exciter.master_env.release();
        }
    }

    // Nothing reads pitch bend: each voice is tuned once, at its strike.
    void PitchBend(uint16_t bend) { (void)bend; }

    // ==========================================================================
    // Audio
    // ==========================================================================
    void processBlock(float* __restrict out, size_t frames) {
        MasterStage::clear(out, frames);
        const float tone = m_master.tone;   // read once per block
        m_master.release_held(m_voices);

        bool any_voice = false;
        for (Voice& v : m_voices) {
            if (!v.is_active) continue;
            any_voice = true;
            unit().render_voice(v, out, frames, tone);
        }
        if (!m_master.should_run(any_voice)) return;
        m_master.render(out, frames);
    }

protected:
    // ── Building blocks for a unit's NoteOn ─────────────────────────────────
    // MIDI velocity × the Velocity knob.
    float strike_velocity(uint8_t midi) const { return m_common.velocity.apply(midi_velocity(midi)); }

    // Round-robin within Poly.  With fuse_sec > 0, a stroke on the same note
    // within fuse_sec of the last one re-strikes that voice instead (roll
    // fusion: one continuous pressed roll, not a pile of whole bodies).
    Voice& allocate_voice(uint8_t note, float fuse_sec) {
        if (!(fuse_sec > 0.0f) || !fuses_with(m_voices[m_last_voice], note, fuse_sec))
            m_last_voice = next_voice(m_last_voice, m_common.poly);
        return m_voices[m_last_voice];
    }

    // Velocity and strike position (HitPos) shape the exciter before it fires.
    void shape_exciter(Voice& v) const {
        v.exciter.shape_strike(v.current_velocity, knob(k_paramHitPos),
                               knob(k_paramMlltStif), knob(k_paramVlMllStf),
                               m_anchor.vel_stiff, knob(k_paramNzRes),
                               knob(k_paramVlMllRes));
    }

    ModalKnobs modal_knobs() const {
        return {knob(k_paramModel), knob(k_paramPartls), knob(k_paramInharm),
                knob(k_paramMterl), knob(k_paramTubRad), knob(k_paramDkay),
                knob(k_paramRel), knob(k_paramMlltStif), knob(k_paramMlltRes),
                knob(k_paramHitPos)};
    }

    // A standard knob's raw value: its slot's, or the preset's own where the
    // unit gave the slot to a family knob.
    int32_t knob(uint8_t role) const {
        const uint8_t s = slot_of(role);
        return (s != kNoSlot) ? m_params[s] : Unit::fixed_knob(m_preset_idx, role);
    }

    // Strike the preset's modal bank, reshaped by the knobs.  The bank is
    // re-struck on every NoteOn, so its modes track the played note and its
    // amplitudes the velocity.  Returns the TubRad body-size factor applied
    // (1.0 when none, or when the preset has no bank).
    float strike_modal_bank(Voice& v, float modal_mix) {
        const ModalPresetConfig& cfg = Unit::modal_config(m_preset_idx);
        if (cfg.mode_count == 0) return 1.0f;
        const ModalKnobs k = modal_knobs();
        ModalShape shape = ModalShape::from(cfg, modal_mix);
        const float body = shape_modal(shape, k, m_anchor, m_model);
        v.modal.init(v.current_note, v.current_velocity, shape);
        strike_modal(v.modal, k, m_anchor, v.exciter.mallet_stiffness);
        return body;
    }

    // LoadPreset's own strike of a voice's bank, at the preset's calibrated
    // mix (it runs at the voice's last velocity: 0 on a fresh voice).
    void load_modal_bank(Voice& v) {
        const ModalPresetConfig& cfg = Unit::modal_config(m_preset_idx);
        if (cfg.mode_count > 0)
            v.modal.init(v.current_note, v.current_velocity, ModalShape::from(cfg, cfg.mix));
    }

    Voice             m_voices[kNumVoices];
    uint8_t           m_last_voice = 0;   // the slot the last strike took
    MasterStage       m_master;
    int32_t           m_params[k_lastParamIndex] = {0};   // raw knob values, as the host set them
    uint8_t           m_preset_idx = 0;
    Anchors           m_anchor;
    CommonKnobs       m_common;             // Pitch, Poly, Velocity
    uint8_t           m_model = k_String;   // Model knob

private:
    Unit& unit() { return static_cast<Unit&>(*this); }

    static constexpr uint8_t kNoSlot = 0xFF;

    // The slot a standard role sits in, or kNoSlot.
    static constexpr uint8_t slot_of(uint8_t role) {
        for (uint8_t s = k_firstDedicated; s < k_lastParamIndex; ++s)
            if (Unit::role_at(s) == role) return s;
        return kNoSlot;
    }

    // The host owns the common knobs it owns on every unit, and VlMllRes
    // where the unit keeps it (a family knob in its slot loads with the
    // preset, like any sound-design knob).
    static bool is_host_owned(uint8_t p) {
        return is_host_knob(kCommonHostKnobs, p) || (p == slot_of(k_paramVlMllRes));
    }

    // Every knob but Program and the host-owned ones loads with a preset.
    static bool loads_with_preset(uint8_t p) { return p != k_paramProgram && !is_host_owned(p); }

    // A standard knob's setter.  Returns the value to store.
    int32_t apply_role(uint8_t role, int32_t value) {
        switch (role) {
        case k_paramMlltRes:  set_exciters(&Exciter::set_mallet_res, value); break;
        case k_paramMlltStif: set_exciters(&Exciter::set_mallet_stiff, value); break;
        case k_paramPartls:   return knob::clamp_int(value, 0, 4);
        case k_paramModel:    m_model = (uint8_t)value; break;
        case k_paramDkay:
            // Its ring-length mappings are read at the strike; what is set
            // here is how fast AllNoteOff fades a voice.
            value = knob::clamp_int(value, 0, 200);
            set_exciters(&Exciter::set_release_time, value);
            break;
        case k_paramRel:      set_exciters(&Exciter::set_release, value); break;
        case k_paramNzMix:    set_exciters(&Exciter::set_noise_mix, value); break;
        case k_paramNzRes:    set_exciters(&Exciter::set_noise_res, value); break;
        case k_paramNzFltr:   set_exciters(&Exciter::set_noise_filter_mode, value); break;
        case k_paramNzFltFrq: set_exciters(&Exciter::set_noise_filter_freq, value); break;
        default: break;   // VlMllRes, VlMllStf, Mterl, HitPos, Inharm, TubRad: read at the strike
        }
        return value;
    }

    // A standard knob's shipped value: the preset row's, or the unit's fixed
    // value where the slot is a family knob's.
    static int32_t shipped(const int16_t* row, uint8_t idx, uint8_t role) {
        const uint8_t s = slot_of(role);
        return (s != kNoSlot) ? row[s] : Unit::fixed_knob(idx, role);
    }

    // The shipped knob values every mapping pivots on (REFERENCE ANCHORS).
    void capture_anchors(const int16_t* row) {
        const uint8_t i = m_preset_idx;
        m_anchor.decay        = knob::decay(shipped(row, i, k_paramDkay));
        m_anchor.mallet_stiff = knob::mallet_stiff(shipped(row, i, k_paramMlltStif));
        m_anchor.model        = (uint8_t)shipped(row, i, k_paramModel);
        const int32_t partls  = shipped(row, i, k_paramPartls);
        m_anchor.partls       = (partls <= 4) ? partls : 0;
        m_anchor.inharm       = knob::inharm(shipped(row, i, k_paramInharm));
        m_anchor.material     = knob::material(shipped(row, i, k_paramMterl));
        m_anchor.hit_pos      = knob::hit_pos(shipped(row, i, k_paramHitPos));
        m_anchor.tube_radius  = knob::tube_radius(shipped(row, i, k_paramTubRad));
        m_anchor.mallet_res   = knob::mallet_res(shipped(row, i, k_paramMlltRes));
        m_anchor.release      = knob::release(shipped(row, i, k_paramRel));
        m_anchor.vel_stiff    = knob::percent(shipped(row, i, k_paramVlMllStf));
        m_anchor.vel_res      = knob::percent(shipped(row, i, k_paramVlMllRes));
    }

    void set_exciters(void (Exciter::*set)(int32_t), int32_t value) {
        set_all_exciters(m_voices, set, value);
    }
};

}  // namespace brc

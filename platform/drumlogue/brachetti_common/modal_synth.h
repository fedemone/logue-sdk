#pragma once
// Brachetti common library — the engine the five modal units share.
//
// Kick, Tom, Snare, Bar and Plate are one engine with different family
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
// Provides: ModalLayout (the 24-knob layout), kModalHostKnobs (the knobs the
// host owns, with their defaults), ModalSynth.
//
// A unit may also define NoteOff (GateOff routes through it).  Its voice type
// derives from VoiceBase and defines reset_for_strike(); it may define
// on_unit_reset() for state only Reset() clears.
#include <cstddef>
#include <cstdint>
#include "unit.h"
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
struct ModalLayout {
    enum Param : uint8_t {
        k_paramProgram = 0,  // 0
        k_paramPitch,        // 1
        k_paramPoly,         // 2
        k_paramVelocity,     // 3
        k_paramMlltRes,      // 4
        k_paramMlltStif,     // 5
        k_paramVlMllRes,     // 6
        k_paramVlMllStf,     // 7
        k_paramPartls,       // 8
        k_paramModel,        // 9
        k_paramDkay,         // 10
        k_paramMterl,        // 11
        k_paramTone,         // 12
        k_paramHitPos,       // 13
        k_paramRel,          // 14
        k_paramInharm,       // 15
        k_paramCutoff,       // 16
        k_paramTubRad,       // 17
        k_paramGain,         // 18
        k_paramNzMix,        // 19
        k_paramNzRes,        // 20
        k_paramNzFltr,       // 21
        k_paramNzFltFrq,     // 22
        k_paramResnc,        // 23
        k_lastParamIndex     // marker
    };
};
static_assert(ModalLayout::k_lastParamIndex == 24, "the modal units' header.c declares 24 params");

// The knobs the host owns on the modal units, with their header.c defaults
// (see HostKnob in knobs.h).  Pitch's preset column is the preset's root.
static const HostKnob kModalHostKnobs[] = {
    { ModalLayout::k_paramPitch,    0    },
    { ModalLayout::k_paramPoly,     4    },
    { ModalLayout::k_paramVelocity, 0    },
    { ModalLayout::k_paramVlMllRes, 0    },
    { ModalLayout::k_paramTone,     0    },
    { ModalLayout::k_paramCutoff,   1999 },
    { ModalLayout::k_paramResnc,    71   },
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
    uint8_t PresetRootNote() const { return m_note.root; }
    uint8_t PlayedNote() const     { return m_note.note; }

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
        m_note.set_root((uint8_t)row[k_paramPitch]);
        for (uint8_t p = 0; p < k_lastParamIndex; ++p)
            if (loads_with_preset(p)) setParameter(p, row[p]);
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
        switch (index) {
        case k_paramProgram:  LoadPreset((uint8_t)value); break;
        case k_paramPitch:    m_params[index] = m_note.set_pitch(value); break;
        case k_paramPoly:     m_poly = knob::poly(value); break;
        case k_paramVelocity: m_velocity.set(value); break;
        case k_paramMlltRes:  set_exciters(&Exciter::set_mallet_res, value); break;
        case k_paramMlltStif: set_exciters(&Exciter::set_mallet_stiff, value); break;
        case k_paramPartls:   m_params[index] = knob::clamp_int(value, 0, 4); break;
        case k_paramModel:    m_model = (uint8_t)value; break;
        case k_paramDkay:
            // Its ring-length mappings are read at the strike; what is set
            // here is how fast AllNoteOff fades a voice.
            m_params[index] = knob::clamp_int(value, 0, 200);
            set_exciters(&Exciter::set_release_time, m_params[index]);
            break;
        case k_paramTone:     m_master.set_tone(value); break;
        case k_paramRel:      set_exciters(&Exciter::set_release, value); break;
        case k_paramCutoff:
        case k_paramResnc:    m_master.set_filter(m_params[k_paramCutoff], m_params[k_paramResnc]); break;
        case k_paramGain:     m_master.set_gain(value); break;
        case k_paramNzMix:    set_exciters(&Exciter::set_noise_mix, value); break;
        case k_paramNzRes:    set_exciters(&Exciter::set_noise_res, value); break;
        case k_paramNzFltr:   set_exciters(&Exciter::set_noise_filter_mode, value); break;
        case k_paramNzFltFrq: set_exciters(&Exciter::set_noise_filter_freq, value); break;
        default: break;   // VlMllRes, VlMllStf, Mterl, HitPos, Inharm, TubRad: read at the strike
        }
    }

    const char* getParameterStrValue(uint8_t index, int32_t value) const {
        static char partls_buf[8], stiff_buf[8], dkay_buf[8], nzfrq_buf[10],
                    cutoff_buf[10], resnc_buf[8], poly_buf[8];
        const char* s = nullptr;
        switch (index) {
        case k_paramProgram:  return Unit::preset_name((uint8_t)value);   // the preset being browsed
        case k_paramModel:    s = model_name(value); break;
        case k_paramPartls:
            return format_mode_count(partls_buf, sizeof(partls_buf),
                                     Unit::modal_config(m_preset_idx).mode_count, value, m_anchor.partls);
        case k_paramNzFltr:   s = noise_filter_name(value); break;
        case k_paramMlltStif: return format_int(stiff_buf, sizeof(stiff_buf), (int)(value * 100));
        case k_paramDkay:     return format_int(dkay_buf, sizeof(dkay_buf), (int)(value * 10));
        case k_paramNzFltFrq: return format_hz(nzfrq_buf, sizeof(nzfrq_buf), value);
        case k_paramCutoff:   return format_hz(cutoff_buf, sizeof(cutoff_buf), value);
        case k_paramResnc:    return format_int(resnc_buf, sizeof(resnc_buf), (int)(value * 10));
        case k_paramPoly:     return format_int(poly_buf, sizeof(poly_buf), knob::poly(value));
        default: break;
        }
        return s ? s : "---";
    }

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
        unit().NoteOn(m_note.strike(), velocity);
    }
    void GateOff() { unit().NoteOff(m_note.gate_note); }

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
    float strike_velocity(uint8_t midi) const { return m_velocity.apply(midi_velocity(midi)); }

    // Round-robin within Poly.  With fuse_sec > 0, a stroke on the same note
    // within fuse_sec of the last one re-strikes that voice instead (roll
    // fusion: one continuous pressed roll, not a pile of whole bodies).
    Voice& allocate_voice(uint8_t note, float fuse_sec) {
        if (!(fuse_sec > 0.0f) || !fuses_with(m_voices[m_last_voice], note, fuse_sec))
            m_last_voice = next_voice(m_last_voice, m_poly);
        return m_voices[m_last_voice];
    }

    // Velocity and strike position (HitPos) shape the exciter before it fires.
    void shape_exciter(Voice& v) const {
        v.exciter.shape_strike(v.current_velocity, m_params[k_paramHitPos],
                               m_params[k_paramMlltStif], m_params[k_paramVlMllStf],
                               m_anchor.vel_stiff, m_params[k_paramNzRes],
                               m_params[k_paramVlMllRes]);
    }

    ModalKnobs modal_knobs() const {
        return {m_params[k_paramModel], m_params[k_paramPartls], m_params[k_paramInharm],
                m_params[k_paramMterl], m_params[k_paramTubRad], m_params[k_paramDkay],
                m_params[k_paramRel], m_params[k_paramMlltStif], m_params[k_paramMlltRes],
                m_params[k_paramHitPos]};
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
    VelocityBias      m_velocity;
    SequencerNote     m_note;
    uint8_t           m_poly  = 4;          // Poly knob: voice cap 1-4
    uint8_t           m_model = k_String;   // Model knob

private:
    Unit& unit() { return static_cast<Unit&>(*this); }

    // Every knob but Program and the host-owned ones loads with a preset.
    static bool loads_with_preset(uint8_t p) {
        return p != k_paramProgram && !is_host_knob(kModalHostKnobs, p);
    }

    // The shipped knob values every mapping pivots on (REFERENCE ANCHORS).
    void capture_anchors(const int16_t* row) {
        m_anchor.decay        = knob::decay(row[k_paramDkay]);
        m_anchor.mallet_stiff = knob::mallet_stiff(row[k_paramMlltStif]);
        m_anchor.model        = (uint8_t)row[k_paramModel];
        m_anchor.partls       = (row[k_paramPartls] <= 4) ? row[k_paramPartls] : 0;
        m_anchor.inharm       = knob::inharm(row[k_paramInharm]);
        m_anchor.material     = knob::material(row[k_paramMterl]);
        m_anchor.hit_pos      = knob::hit_pos(row[k_paramHitPos]);
        m_anchor.tube_radius  = knob::tube_radius(row[k_paramTubRad]);
        m_anchor.mallet_res   = knob::mallet_res(row[k_paramMlltRes]);
        m_anchor.release      = knob::release(row[k_paramRel]);
        m_anchor.vel_stiff    = knob::percent(row[k_paramVlMllStf]);
        m_anchor.vel_res      = knob::percent(row[k_paramVlMllRes]);
    }

    void set_exciters(void (Exciter::*set)(int32_t), int32_t value) {
        set_all_exciters(m_voices, set, value);
    }
};

}  // namespace brc

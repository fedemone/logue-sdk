#pragma once
// Brachetti common library — Program changes that let the ringing sound ring.
//
// A family engine cannot keep a voice through a Program change: its voices
// read the current instrument's tables, anchors and master trim while they
// ring, so a change fades them out in ~10 ms (fade_out_all).  RingOver gives a
// unit TWO copies of its engine.  New strikes and the knobs go to the current
// copy; on a Program change while the current copy is still sounding, the
// other copy takes the new instrument (with the panel's knobs) and becomes
// current, and the first rings out untouched, under its own instrument,
// knobs and master stage, until it falls silent.  A change while nothing
// rings, or to the instrument already playing, is the engine's own
// LoadPreset, exactly as before.  So at most two instruments sound at once:
// a third change while both still ring fades the older one, as a single
// engine would.
//
//   static brc::RingOver<BrachettiSynth> s_synth;   // unit.cc
//
// It forwards the engine's unit-facing calls.  A held gate is released on
// the copy it struck; a MIDI note-off, all-notes-off, reset and suspend go to
// both.  An engine whose voices use file-scope buffers (delay lines) gives
// each copy its own with SetBank(0 / 1), called before Init.
//
// "Sounding" is measured on the output: a copy sounds while its last
// rendered block had a non-zero sample, or it was struck since.  An engine's
// output is exactly zero once its voices have retired and its master stage
// has stopped, so the tail (master filter included) is never cut.
//
// It is also where the panel's knob steps are applied (knob_steps.h): give
// it the unit's header with SetHeader before Init, and Cutoff and NzFltFrq
// step 0.1 kHz on the panel while the engine keeps its 10 Hz values.
//
// Provides: RingOver.
#include <cstddef>
#include <cstdint>
#include "unit.h"
#include "knob_steps.h"
#include "knobs.h"

namespace brc {

// SetBank(b) where the engine has it (file-scope buffers), else nothing.
template <class S>
inline auto ring_over_set_bank(S& s, uint8_t b, int) -> decltype(s.SetBank(b), void()) { s.SetBank(b); }
template <class S>
inline void ring_over_set_bank(S&, uint8_t, long) {}

template <class S>
class RingOver {
public:
    static constexpr int k_NumPrograms = S::k_NumPrograms;
    static constexpr size_t kChunk = 64;   // frames per render of the copy ringing out

    // The unit's knobs as the panel shows them (before Init).  Without it the
    // values pass through as they are.
    void SetHeader(const unit_header_t* h) {
        for (uint8_t i = 0; i < kMaxKnobs; ++i)
            m_step[i] = (h && i < h->num_params) ? knob_step(h->params[i]) : nullptr;
    }

    int8_t Init(const unit_runtime_desc_t* desc) {
        for (uint8_t i = 0; i < 2; ++i) {
            ring_over_set_bank(m_eng[i], i, 0);
            const int8_t err = m_eng[i].Init(desc);
            if (err != k_unit_err_none) return err;
            m_sounding[i] = false;
        }
        m_cur = 0;
        m_gate = 0;
        return k_unit_err_none;
    }

    void Teardown() { m_eng[0].Teardown(); m_eng[1].Teardown(); }
    void Reset()    { both_silent(); m_eng[0].Reset(); m_eng[1].Reset(); }
    void Resume()   { m_eng[0].Resume(); m_eng[1].Resume(); }
    void Suspend()  { both_silent(); m_eng[0].Suspend(); m_eng[1].Suspend(); }

    // ── Presets ─────────────────────────────────────────────────────────────
    static const char* getPresetName(uint8_t idx) { return S::getPresetName(idx); }
    uint8_t getPresetIndex() const { return cur().getPresetIndex(); }

    void LoadPreset(uint8_t idx) {
        if (idx >= S::k_NumPrograms) return;
        S& a = m_eng[m_cur];
        if (idx == a.getPresetIndex() || !m_sounding[m_cur]) { a.LoadPreset(idx); return; }
        // The current copy still sounds: the other one takes the instrument.
        const uint8_t n = m_cur ^ 1;
        S& b = m_eng[n];
        for (uint8_t p = 1; p < kMaxKnobs; ++p) b.setParameter(p, a.getParameterValue(p));
        b.LoadPreset(idx);   // the instrument's own knobs over the copied ones
        m_cur = n;
    }

    // True while either copy still sounds.
    bool Sounding() const { return m_sounding[0] || m_sounding[1]; }

    // ── Knobs ───────────────────────────────────────────────────────────────
    void setParameter(uint8_t index, int32_t value) {
        if (index == 0 && value >= 0 && value < S::k_NumPrograms) { LoadPreset((uint8_t)value); return; }
        m_eng[m_cur].setParameter(index, to_engine(index, value));
    }
    int32_t getParameterValue(uint8_t index) const {
        const int32_t e = cur().getParameterValue(index);
        return (index < kMaxKnobs && m_step[index]) ? step_to_header(*m_step[index], e) : e;
    }
    const char* getParameterStrValue(uint8_t index, int32_t value) const {
        return cur().getParameterStrValue(index, to_engine(index, value));
    }

    // ── Notes ───────────────────────────────────────────────────────────────
    void NoteOn(uint8_t note, uint8_t velocity) {
        m_eng[m_cur].NoteOn(note, velocity);
        m_sounding[m_cur] = true;
    }
    void NoteOff(uint8_t note) { m_eng[0].NoteOff(note); m_eng[1].NoteOff(note); }
    void GateOn(uint8_t velocity) {
        if (velocity == 0) { GateOff(); return; }
        m_gate = m_cur;
        m_eng[m_cur].GateOn(velocity);
        m_sounding[m_cur] = true;
    }
    void GateOff()    { m_eng[m_gate].GateOff(); }
    void AllNoteOff() { m_eng[0].AllNoteOff(); m_eng[1].AllNoteOff(); }
    void PitchBend(uint16_t bend) { m_eng[0].PitchBend(bend); m_eng[1].PitchBend(bend); }

    // ── Audio ───────────────────────────────────────────────────────────────
    void processBlock(float* __restrict out, size_t frames) {
        const uint8_t c = m_cur, o = c ^ 1;
        m_eng[c].processBlock(out, frames);
        m_sounding[c] = any_nonzero(out, frames);
        if (!m_sounding[o]) return;
        bool heard = false;
        float tmp[kChunk * 2];
        for (size_t done = 0; done < frames; done += kChunk) {
            const size_t n = (frames - done < kChunk) ? frames - done : kChunk;
            m_eng[o].processBlock(tmp, n);
            if (any_nonzero(tmp, n)) heard = true;
            for (size_t i = 0; i < 2 * n; ++i) out[2 * done + i] += tmp[i];
        }
        m_sounding[o] = heard;
    }

private:
    const S& cur() const { return m_eng[m_cur]; }
    int32_t to_engine(uint8_t index, int32_t v) const {
        return (index < kMaxKnobs && m_step[index]) ? step_to_engine(*m_step[index], v) : v;
    }
    void both_silent() { m_sounding[0] = m_sounding[1] = false; }
    static bool any_nonzero(const float* buf, size_t frames) {
        for (size_t i = 0; i < 2 * frames; ++i)
            if (buf[i] != 0.0f) return true;
        return false;
    }

    S       m_eng[2];
    bool    m_sounding[2] = {false, false};
    uint8_t m_cur  = 0;   // the copy new strikes and the knobs go to
    uint8_t m_gate = 0;   // the copy the last gate struck (its GateOff goes there)
    const KnobStep* m_step[kMaxKnobs] = {};   // SetHeader: the stepped knobs
};

}  // namespace brc

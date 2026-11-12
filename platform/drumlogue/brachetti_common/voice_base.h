#pragma once
// Brachetti common library — what every exciter-driven voice has, and its
// life cycle: allocation, the per-sample gate and squelch, the preset-change
// fade and retirement.
//
// Provides: VoiceBase; next_voice (round robin), fuses_with (roll fusion),
// any_fading, fade_out_all, set_all_exciters (one knob into every exciter);
// kNumVoices, kRollFuseSec, kPresetFadeTauSec, kSquelchGuardSamples,
// kSquelchThreshold.
//
// A unit's voice is a VoiceBase plus the layers the unit uses (a modal bank,
// a boom, ...).  Per sample, a unit's voice renders in this order:
//
//   x  = exciter + noise burst + the unit's layers
//   x  = follow_and_gate(x)       level follower, master envelope, squelch
//   x  = punch.apply(x)           the Punch knob's transient shaper (idle at 0)
//   x  = tone_tilt(x)             if the Tone knob is off 0 (master_stage.h)
//   x  = (the unit's onset ramp, if it has one)
//   x  = fade_out(x)              only while a preset change fades it
//   bus += x * MasterStage::kBusGain
//   retire_when_done()
#include <cstddef>
#include <cstdint>
#include "dsp_math.h"
#include "exciter.h"
#include "voice_layers.h"

namespace brc {

constexpr int kNumVoices = 4;   // the CPU budget: at most four voices ring at once

// A ringing voice is retired once its level stays under -80 dB, but never in
// the first ~20 ms of a strike, while the exciter is still building it up.
constexpr size_t kSquelchGuardSamples = 1000;
constexpr float  kSquelchThreshold    = 0.0001f;

// A preset change fades the outgoing voices over ~10 ms (6.9 tau reaches
// -60 dB): click-free, short enough not to overlap the new preset audibly.
constexpr float kPresetFadeTauSec = 0.0015f;

// Strokes on the same note closer together than this are ONE physical roll
// (a pressed roll): the units with roll fusion strike them on one voice.
constexpr float kRollFuseSec = 0.080f;

struct VoiceBase {
    bool    is_active        = false;
    bool    is_releasing     = false;
    uint8_t current_note     = 60;
    float   current_velocity = 0.0f;   // MIDI velocity × the Velocity knob

    // Preset-change fade.  A voice cannot survive a preset change — the new
    // preset rewrites the per-voice coefficients — so it fades out and is
    // reclaimed.  fade_mul is exactly 1.0f in normal play, where the fade is
    // skipped entirely.
    float fade     = 1.0f;
    float fade_mul = 1.0f;   // < 1 only while fading out

    Exciter exciter;

    float tone_lp = 0.0f;    // Tone tilt EQ memory
    float mag_env = 0.0f;    // level follower for the squelch (α = 0.01, τ ≈ 2 ms)
    Punch punch;             // the transient shaper: the unit triggers it at the strike

    void start(uint8_t note, float velocity) {
        is_active        = true;
        is_releasing     = false;
        current_note     = note;
        current_velocity = velocity;
    }

    // The base part of a voice's reset on every strike.
    void reset_base() {
        mag_env  = 0.0f;
        fade     = 1.0f;     // a re-struck slot is no longer fading
        fade_mul = 1.0f;
        exciter.restart();
        tone_lp  = 0.0f;
        punch.reset();
    }

    bool fading() const { return is_active && fade_mul < 1.0f; }

    // State only Reset() clears (a unit's voice may define its own).
    void on_unit_reset() {}

    // ── Per sample ──────────────────────────────────────────────────────────
    // Level follower, the master envelope (1.0 until AllNoteOff), and the
    // squelch: a released voice retires when it is silent or its envelope has
    // closed; a held one when ring and exciter have both gone silent (the
    // gate may stay held long after the sound has died).
    float follow_and_gate(float x) {
        mag_env = (fabsf(x) * 0.01f) + (mag_env * 0.99f);
        x *= exciter.master_env.process();
        if (exciter.current_frame > kSquelchGuardSamples) {
            if (is_releasing &&
                (mag_env < kSquelchThreshold || exciter.master_env.state == ENV_IDLE)) {
                is_active = false;
            }
            if (!is_releasing && mag_env < kSquelchThreshold) {
                is_active = false;
            }
        }
        return x;
    }

    float fade_out(float x) {
        if (fade_mul < 1.0f) {
            x *= fade;
            fade *= fade_mul;
            if (fade < 0.001f) is_active = false;
        }
        return x;
    }

    // A released voice whose mallet, noise and level have all died.
    void retire_when_done() {
        if (is_releasing && exciter.finished() && mag_env < kSquelchThreshold) {
            is_active = false;
        }
    }
};

// ── Voice set helpers ───────────────────────────────────────────────────────
// Round-robin within the first `cap` slots, so the Poly knob truly sets (and
// the display truly shows) the polyphony.
inline uint8_t next_voice(uint8_t last, uint8_t cap) {
    return (uint8_t)((last + 1) % cap);
}

// Roll fusion: a stroke on the same note within fuse_sec of the last one
// re-strikes that voice — one continuous pressed roll, not a pile of whole
// bodies each with its own attack.
template <class Voice>
inline bool fuses_with(const Voice& last, uint8_t note, float fuse_sec) {
    return last.is_active && last.current_note == note &&
           (last.exciter.current_frame < (uint32_t)(fuse_sec * kSampleRate));
}

template <class Voice, int N>
inline bool any_fading(const Voice (&voices)[N]) {
    bool fading = false;
    for (int i = 0; i < N; ++i)
        if (voices[i].is_active && voices[i].fade_mul < 1.0f) fading = true;
    return fading;
}

// One knob into every voice's exciter: set_all_exciters(m_voices,
// &Exciter::set_mallet_res, value).
template <class Voice, int N>
inline void set_all_exciters(Voice (&voices)[N], void (Exciter::*set)(int32_t), int32_t value) {
    for (Voice& v : voices) (v.exciter.*set)(value);
}

// Arm the preset-change fade on every ringing voice (an existing fade is not
// restarted).
template <class Voice, int N>
inline void fade_out_all(Voice (&voices)[N]) {
    const float fmul = env_mul(kPresetFadeTauSec, kSampleRate);
    for (int i = 0; i < N; ++i) {
        Voice& v = voices[i];
        if (!v.is_active) continue;
        if (v.fade_mul >= 1.0f) v.fade_mul = fmul;
    }
}

}  // namespace brc

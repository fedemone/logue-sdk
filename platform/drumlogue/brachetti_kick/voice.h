#pragma once
// Brachetti Kick — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the boom that is a kick's real voice, the pitch envelope its
// swept booms read, the thump (beater punch), the beater click, Kick2's modal
// body, a metallic chirp and the onset ramp.
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

// ── Thump ───────────────────────────────────────────────────────────────────
// A fast pitch-dropping mid sine punch over the boom: the boom gives the sub,
// the thump the beater-impact PUNCH.  Off unless the mallet knobs dial it in
// (see BrachettiSynth::dial_thump; 0 at the shipped values).
struct Thump {
    float env   = 0.0f;   // amplitude envelope, starts at the dialled depth
    float amp0  = 1.0f;   // the start level, which the pitch drop is measured from
    float phase = 0.0f;
    float inc   = 0.0f;   // bottom of the drop
    float decay = 1.0f;

    void reset() { env = 0.0f; amp0 = 1.0f; phase = 0.0f; inc = 0.0f; decay = 1.0f; }
    bool active() const { return env > brc::kSilence; }

    float process(float velocity) {
        // The pitch sweep is CUBED against the amplitude envelope: tied 1:1 it
        // lasts as long as the sound and reads as a descending "zip"; cubed it
        // collapses to ~7 ms while the level runs its ~60 ms, so the sweep is
        // attack CHARACTER (a 2× start, ≈ 230 Hz).
        const float drop = (amp0 > 1e-6f) ? (env / amp0) : 0.0f;
        const float sweep = 1.0f + 1.0f * (drop * drop * drop);
        const float th = fastersinfullf(phase) * env;
        // Velocity drives the punch superlinearly (v^1.5): the body scales
        // linearly, so a hard strike punches harder relative to the body
        // instead of mostly ringing longer.
        const float out = th * (velocity * sqrtf(velocity));
        phase += inc * sweep;
        brc::wrap_phase(phase);
        env *= decay;
        return out;
    }
};

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;   // Kick2's body
    brc::MetalChirp chirp;
    brc::Onset      onset;
    brc::PitchEnv   pitch;
    brc::Boom       boom;
    Thump           thump;
    brc::Transient  click;   // the beater click HitPos raises

    void reset_for_strike() {
        reset_base();
        modal.reset();
        chirp.reset();
        onset.reset();
        pitch.reset();
        boom.reset();
        thump.reset();
        click.reset();
    }
};

#pragma once
// Brachetti Tom — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the membrane modal bank it strikes, the boom (Ac Tom, DeepBs,
// Bongo and RackTom, whose boom glides), the pitch envelope RackTom's glide
// reads, the stick / hand-slap transient, a metallic chirp and the onset ramp.

// From brachetti_common (its README.md indexes every helper):
//   modal_bank.h     ModalBank, kMaxModes
//   voice_base.h     VoiceBase
//   voice_layers.h   Boom, GlideCurve, MetalChirp, Onset, PitchEnv, Transient
#include "../../brachetti_common/modal_bank.h"
#include "../../brachetti_common/voice_base.h"
#include "../../brachetti_common/voice_layers.h"

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;
    brc::MetalChirp chirp;
    brc::Onset      onset;
    brc::PitchEnv   pitch;
    brc::Boom       boom;
    brc::Transient  slap;   // the stick or hand slap
    brc::GlideCurve curve;  // an S-shaped bend for the tracked boom (glide_ms > 0)
    // Bank glide (Voicing::bank_glide): each mode's frequency at the strike
    // (radians per sample), the tracked boom's frequency now and at the strike.
    float bank_w[brc::kMaxModes] = {};
    float glide_hz  = 0.0f;
    float glide_hz0 = 0.0f;

    void reset_for_strike() {
        reset_base();
        modal.reset();
        chirp.reset();
        onset.reset();
        pitch.reset();
        boom.reset();
        slap.reset();
        curve.reset();
        glide_hz  = 0.0f;
        glide_hz0 = 0.0f;
    }
};

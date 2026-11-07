#pragma once
// Brachetti Bar — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the free-bar modal bank it strikes, the Steel Pan's metallic
// chirp and the onset ramp.

// From brachetti_common (its README.md indexes every helper):
//   modal_bank.h     ModalBank
//   voice_base.h     VoiceBase
//   voice_layers.h   MetalChirp, Onset
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;
    brc::MetalChirp chirp;
    brc::Onset      onset;

    // Every layer back to silence for a new strike.  The settings the knobs
    // own (mallet, noise envelopes, noise filter) stay.
    void reset_for_strike() {
        reset_base();
        modal.reset();
        chirp.reset();
        onset.reset();
    }
};

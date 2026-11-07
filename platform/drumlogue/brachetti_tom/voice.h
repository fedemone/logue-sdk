#pragma once
// Brachetti Tom — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the membrane modal bank it strikes, the boom (Ac Tom, DeepBs,
// Bongo and RackTom, whose boom glides), the pitch envelope RackTom's glide
// reads, the stick / hand-slap transient, a metallic chirp and the onset ramp.
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;
    brc::MetalChirp chirp;
    brc::Onset      onset;
    brc::PitchEnv   pitch;
    brc::Boom       boom;
    brc::Transient  slap;   // the stick or hand slap

    void reset_for_strike() {
        reset_base();
        modal.reset();
        chirp.reset();
        onset.reset();
        pitch.reset();
        boom.reset();
        slap.reset();
    }
};

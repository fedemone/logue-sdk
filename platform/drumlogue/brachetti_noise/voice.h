#pragma once
// Brachetti Noise — one voice: the exciter (a faint mallet + the noise
// burst, from VoiceBase) and the gate that chops the noise into Clap's bursts
// and Shaker's grains.
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

struct VoiceState : brc::VoiceBase {
    brc::NoiseGate gate;

    void reset_for_strike() {
        reset_base();
        gate.reset();
    }
};

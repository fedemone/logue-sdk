#pragma once
// Brachetti Plate — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the inharmonic plate modal bank it strikes, a metallic chirp,
// the onset ramp, and the plate's own layers:
//  * the hat filter, which takes over the click band in the hi-hat region
//    (noise band mix above 0.8: Tick);
//  * the shimmer, a high-pass of the noise burst mixed after the bank;
//  * the ring gate, which pulls the noise down with the bank's fundamental
//    so noise and ring die together instead of sounding side by side.
#include "../brachetti_common/filter.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

// The post-resonator high band.
struct Shimmer {
    float env   = 0.0f;
    float decay = 1.0f;
    float mix   = 0.0f;   // 0 = off
    float lp    = 0.0f;

    void reset() { env = 0.0f; decay = 1.0f; mix = 0.0f; lp = 0.0f; }
    void trigger(float mix_, float decay_) { env = 1.0f; decay = decay_; mix = mix_; }
    bool active() const { return mix > 0.0f && env > brc::kSilence; }

    // noise − LP(noise), enveloped.
    float process(float noise) {
        lp += 0.12f * (noise - lp);
        const float hf = noise - lp;
        const float out = hf * env * mix * 8.0f;
        env *= decay;
        return out;
    }
};

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;
    brc::MetalChirp chirp;
    brc::Onset      onset;
    Shimmer         shimmer;
    float           ring_gate = 1.0f;   // follows modal.decay[0] down from 1.0
    // The hat filter and whether this voice uses it.  use_hat is set by
    // LoadPreset, on the voices that are not fading out — so a slot that was
    // fading during a switch to Tick plays its next Tick strike without the
    // filter until the next LoadPreset (as the original unit does).
    FastSVF         hat;
    bool            use_hat = false;

    void reset_for_strike() {
        reset_base();
        modal.reset();
        chirp.reset();
        onset.reset();
        shimmer.reset();
        ring_gate = 1.0f;
    }

    // Reset(): the hat filter back to a band-pass at 7 kHz, memory cleared.
    void on_unit_reset() {
        hat.mode = 1;
        hat.set_coeffs(7000.0f, 1.1f, brc::kSampleRate);
        hat.lp = 0.0f;
        hat.bp = 0.0f;
        hat.hp = 0.0f;
    }

    // The exciter, with the hat filter on the click band in the hi-hat
    // region.  It filters the noise BEFORE the NzFltr filter, so it is a
    // centroid control of its own.
    float excite() {
        brc::NoiseBurst b;
        if (exciter.noise_burst(b)) {
            const float mix = exciter.noise_band_mix;
            if (mix > 0.80f) {
                const float src = use_hat ? hat.process(b.raw) : b.filtered;
                b.high_part = src * mix * 1.35f * b.env_high;
            }
            exciter.noise_out_sample = exciter.noise_sum(b);
        }
        return exciter.mallet();
    }

    // The noise level beside the bank: pulled down with the ring (floored at
    // 0.15, so a little noise texture stays in long tails).
    float ring_gated(float noise_gain) {
        if (modal.active) {
            noise_gain *= fmaxf(0.15f, ring_gate);
            ring_gate *= modal.decay[0];
        }
        return noise_gain;
    }
};

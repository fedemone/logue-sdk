#pragma once
// Brachetti Snare — one voice: the exciter (mallet + noise burst, from
// VoiceBase) with the snare wires on its noise, a short membrane modal head,
// AcSnare's swept body boom and the pitch envelope it reads, BrshSnr's swirl
// (a gate on the noise) and the onset ramp.
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"

// ── The snare wires ─────────────────────────────────────────────────────────
// Three parallel two-pole resonators driven by the noise burst: band A the
// body crack, B the mid buzz, C the high sizzle (parallel, not in series:
// coupled bands would tonalise the rattle — Cook, "Real Sound Synthesis for
// Interactive Applications", 2002).  Before the wires engage, a broadband
// CRACK burst (VlMllRes scales it) carries the strike; the wires fade in
// behind it (onset_env).
struct SnareWires {
    struct Band {
        float a1, a2;   // 2·r·cos(w), r²
        float z1, z2;

        // A resonator at freq_hz with pole radius r.
        void tune(float freq_hz, float r) {
            a1 = 2.0f * r * fastercosfullf(brc::phase_inc(freq_hz));
            a2 = r * r;
        }
        float ring(float input) {
            const float w = input + (a1 * z1) - (a2 * z2);
            z2 = z1;
            z1 = w;
            return w;
        }
    };

    float onset_env    = 1.0f;   // 0 = all crack .. 1 = wires fully in
    float onset_attack = 1.0f;   // its step per sample
    float mix          = 0.0f;   // wire rattle vs plain filtered noise
    float crack_gain   = 1.0f;   // VlMllRes
    Band  band[3] = {{1.6951f, 0.8930f, 0.0f, 0.0f},    // A ~2.8 kHz
                     {1.620f,  0.942f,  0.0f, 0.0f},    // B ~4.5 kHz
                     {1.120f,  0.908f,  0.0f, 0.0f}};   // C ~7.2 kHz

    // A strike's reset: wires at rest, band tuning back to its defaults.  The
    // onset state is the strike's to set.
    void reset() {
        mix = 0.0f;
        crack_gain = 1.0f;
        band[0] = {1.6951f, 0.8930f, 0.0f, 0.0f};
        band[1] = {1.620f,  0.942f,  0.0f, 0.0f};
        band[2] = {1.120f,  0.908f,  0.0f, 0.0f};
    }

    // The wires' motion, carried across a fast restrike (a buzz roll).
    struct Motion { float z[3][2]; };
    Motion motion() const {
        Motion m;
        for (int i = 0; i < 3; ++i) { m.z[i][0] = band[i].z1; m.z[i][1] = band[i].z2; }
        return m;
    }
    void resume(const Motion& m) {
        for (int i = 0; i < 3; ++i) { band[i].z1 = m.z[i][0]; band[i].z2 = m.z[i][1]; }
    }

    bool engaged() const { return mix > 0.001f; }

    // The noise burst through the wires: crack, then the three bands mixed
    // in under the plain noise.
    float process(float noise, float high, float env_high) {
        noise += high * env_high * (1.0f - onset_env) * 0.90f * crack_gain;
        const float input = noise * onset_env;
        const float wa = band[0].ring(input);
        const float wb = band[1].ring(input);
        const float wc = band[2].ring(input);
        onset_env = fminf(1.0f, onset_env + onset_attack);
        const float rattle = (0.50f * wa) + (0.80f * wb) + (0.60f * wc);
        return (noise * (1.0f - mix)) + (rattle * mix * 0.40f);
    }
};

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;   // the head
    brc::Onset      onset;
    brc::PitchEnv   pitch;
    brc::Boom       boom;
    brc::NoiseGate  swirl;   // BrshSnr's brush flutter
    SnareWires      wires;

    void reset_for_strike() {
        reset_base();
        modal.reset();
        onset.reset();
        pitch.reset();
        boom.reset();
        swirl.reset();
        wires.reset();
    }

    // The exciter with the wires on its noise burst.
    float excite() {
        brc::NoiseBurst b;
        if (exciter.noise_burst(b)) {
            float noise = exciter.noise_sum(b);
            if (wires.engaged()) noise = wires.process(noise, b.high, b.env_high);
            exciter.noise_out_sample = noise;
        }
        return exciter.mallet();
    }
};

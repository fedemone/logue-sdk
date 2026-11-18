#pragma once
// Brachetti Filo — one voice: the exciter (mallet + noise burst, from
// VoiceBase) with the snare wires on its noise, a short membrane modal head,
// AcSnare's swept body boom and the pitch envelope it reads, BrshSnr's swirl
// (a gate on the noise) and the onset ramp — BrcSnare's voice, as it is —
// plus the two things this family adds:
//
//   * the wires on the HEAD: a follower on the head's motion drives the
//     wire bands with noise (white, or the sparse crackle of mymembrane~), so
//     the wires rattle as long as, and as hard as, the head moves, after the
//     noise burst has died (the snares of Miguel Moreno's "Realistic drums
//     with karplus-strong synthesis" and of his mymembrane~);
//   * the berimbau's wire (BrcWorld's Wire, as it is) made STIFF: a cascade of
//     first-order all-passes in its loop sharpens the upper partials as a
//     steel wire's are (Van Duyne & Smith 1994).
//
// Each is off at 0 in the preset table, where the voice is BrcSnare's or
// BrcWorld's to the bit.

#include "../common/attributes.h"   // force_inline

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       kSilence
//   exciter.h        NoiseBurst
//   modal_bank.h     ModalBank
//   noise.h          FastNoise, crackle
//   small_filters.h  Resonator2, one_pole_lp
//   voice_base.h     VoiceBase
//   voice_layers.h   Boom, GlideCurve, NoiseGate, Onset, PitchEnv
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/exciter.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/noise.h"
#include "../brachetti_common/small_filters.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
#include <cmath>
#include <cstdint>

// ── The snare wires ─────────────────────────────────────────────────────────
// Three parallel two-pole resonators driven by the noise burst: band A the
// body crack, B the mid buzz, C the high sizzle (parallel, not in series:
// coupled bands would tonalise the rattle — Cook, "Real Sound Synthesis for
// Interactive Applications", 2002).  Before the wires engage, a broadband
// CRACK burst (VlMllRes scales it) carries the strike; the wires fade in
// behind it (onset_env).
struct SnareWires {
    using Band = brc::Resonator2;   // {a1, a2, z1, z2}: small_filters.h

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

    // The same, with the head's drive added to the bands' input (BrcFilo's
    // wires on the head; process() is kept separate so a preset without the
    // drive runs BrcSnare's arithmetic exactly).
    force_inline float process_driven(float noise, float high, float env_high, float drive) {
        noise += high * env_high * (1.0f - onset_env) * 0.90f * crack_gain;
        const float input = noise * onset_env + drive;
        const float wa = band[0].ring(input);
        const float wb = band[1].ring(input);
        const float wc = band[2].ring(input);
        onset_env = fminf(1.0f, onset_env + onset_attack);
        const float rattle = (0.50f * wa) + (0.80f * wb) + (0.60f * wc);
        return (noise * (1.0f - mix)) + (rattle * mix * 0.40f);
    }
};

// ── The wires on the head ───────────────────────────────────────────────────
// A follower on the head (the modal bank and the boom, before the master
// envelope) sets the level of a noise that drives the wire bands: the wires
// buzz while, and as hard as, the head moves.  The noise is white blended
// towards brc::crackle (mymembrane~'s noise~ to the 50th power: a sparse
// rattle).  Its own generator, so the noise burst's sequence is untouched.
struct HeadDrive {
    float follow  = 0.0f;   // 0 = off: the voice is BrcSnare's
    float crackle = 0.0f;   // 0 = white .. 1 = all crackle
    float att = 0.0f, rel = 0.0f;   // the follower's one-pole coefficients
    float env = 0.0f;
    FastNoise noise;

    bool active() const { return follow > 0.0f; }

    void reset() { env = 0.0f; }

    // This sample's drive, from the follower as the last sample left it.
    force_inline float sample() {
        float n = noise.process();
        if (crackle > 0.0f) n += crackle * (brc::crackle(n) - n);
        return follow * n * env;
    }

    force_inline void follow_head(float head) {
        const float a = fabsf(head);
        env += ((a > env) ? att : rel) * (a - env);
    }
};

namespace filo {

// ── The berimbau's wire ─────────────────────────────────────────────────────
// BrcWorld's Wire (../brachetti_world/voice.h), as it is, plus a cascade of
// `stages` first-order all-passes in the loop (coefficient `stiff`): their
// delay falls with frequency, so the upper partials come out sharp, as a
// steel wire's do (Van Duyne & Smith, "A Simplified Approach to Modeling
// Dispersion Caused by Stiffness in Strings and Plates", ICMC 1994).  At
// stages 0 the loop is BrcWorld's exactly.
constexpr int kWireLen = 1024;   // ≥ one period down to ~47 Hz
constexpr int kMaxStiffStages = 4;

struct Wire {
    float level = 0.0f;           // 0 = off
    float* buf = nullptr;         // this voice's delay line (synth_engine.h owns it)
    float delay = 0.0f;           // period in samples
    float loop_gain = 0.0f, damp = 0.5f, lp = 0.0f;
    int   w = 0;
    float direct = 0.0f;          // the wire beside the gourd
    float gourd_hz = 0.0f, gourd_up = 0.0f, gourd_down = 0.0f, gourd_r = 0.0f, gourd_g = 0.0f;
    brc::GlideCurve up, down;     // the gourd swung away from the belly, then back part way
    brc::Resonator2 gourd;
    uint32_t n = 0;
    float env = 1.0f;             // falls below silence → the layer stops
    int   stages = 0;             // the stiffness all-passes, 0 = none
    float stiff = 0.0f;           // their coefficient
    float ap_z[kMaxStiffStages] = {};

    void reset() { float* b = buf; *this = Wire(); buf = b; }

    bool active() const { return level > 0.0f && buf != nullptr; }

    force_inline float process() {
        // read with linear interpolation one period back
        float rp = (float)w - delay;
        if (rp < 0.0f) rp += (float)kWireLen;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1 == kWireLen) ? 0 : i0 + 1;
        const float fr = rp - (float)i0;
        const float x = buf[i0] + fr * (buf[i1] - buf[i0]);
        lp += damp * (x - lp);                 // the loss filter: highs die first
        if (stages > 0) {
            // the stiffness: v = u + a·v[n−1], y = −a·v + v[n−1], per stage
            float u = lp * loop_gain;
            for (int s = 0; s < stages; ++s) {
                const float v = u + stiff * ap_z[s];
                u = ap_z[s] - stiff * v;
                ap_z[s] = v;
            }
            buf[w] = u;
        } else {
            buf[w] = lp * loop_gain;
        }
        if (++w == kWireLen) w = 0;
        if ((n & 31u) == 0u) {                 // the gourd moves at control rate
            const float fc = fmaxf(40.0f, gourd_hz + gourd_up * (1.0f - up.env) - gourd_down * (1.0f - down.env));
            gourd.tune(fc, gourd_r);
        }
        up.process();
        down.process();
        ++n;
        env = brc::one_pole_lp(fabsf(x), 0.001f, env);
        if (n > 4800u && env < brc::kSilence) level = 0.0f;
        return (direct * x + gourd.ring(x * gourd_g)) * level;
    }
};

}  // namespace filo

struct VoiceState : brc::VoiceBase {
    brc::ModalBank  modal;   // the head
    brc::Onset      onset;
    brc::PitchEnv   pitch;
    brc::Boom       boom;
    brc::NoiseGate  swirl;   // BrshSnr's brush flutter
    SnareWires      wires;
    HeadDrive       head;    // the wires on the head (off on BrcSnare's presets)
    filo::Wire      wire;    // the berimbau
    bool            berimbau = false;   // struck as a berimbau: its render path

    void reset_for_strike() {
        reset_base();
        modal.reset();
        onset.reset();
        pitch.reset();
        boom.reset();
        swirl.reset();
        wires.reset();
        head.reset();
        wire.reset();
    }

    // The exciter with the wires on its noise burst.
    // Per sample, from the render loop: force_inline (the SDK's attributes.h)
    // because GCC's size heuristics may otherwise leave it out of line, which
    // costs a call every sample (arm_cpu.sh shows it).
    force_inline float excite() {
        brc::NoiseBurst b;
        if (head.active() && wires.engaged()) {
            // The wires on the head: driven through the burst and after it.
            const float drive = head.sample();
            if (exciter.noise_burst(b))
                exciter.noise_out_sample = wires.process_driven(exciter.noise_sum(b), b.high, b.env_high, drive);
            else
                exciter.noise_out_sample = wires.process_driven(0.0f, 0.0f, 0.0f, drive);
            return exciter.mallet();
        }
        if (exciter.noise_burst(b)) {
            float noise = exciter.noise_sum(b);
            if (wires.engaged()) noise = wires.process(noise, b.high, b.env_high);
            exciter.noise_out_sample = noise;
        }
        return exciter.mallet();
    }
};

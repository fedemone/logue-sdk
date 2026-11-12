#pragma once
// Brachetti common library — voice layers shared by more than one family.
// A unit's voice includes the ones it uses; each is plain state plus its
// per-sample step, idle (a no-op) until a strike triggers it.
//
//   MetalChirp  metallic FM chirp on the strike          kick tom bar plate
//   Onset       attack ramp over the first milliseconds  kick tom snare bar plate
//   PitchEnv    pitch envelope the boom sweeps read      kick tom snare
//   Boom        low sine body ("boom") with its own env  kick tom snare
//   Transient   band-passed noise burst: click / slap    kick tom
//   NoiseGate   enveloped-LFO gate on the noise          snare noise
//   GlideCurve  held, then S-shaped 1 → 0 bend envelope   tom world
//   Punch       the transient shaper (the Punch knob)    all nine
#include <cmath>
#include <cstdint>
#include "dsp_math.h"
#include "noise.h"
#include "small_filters.h"

namespace brc {

// ── Metallic FM chirp ───────────────────────────────────────────────────────
// A short sine sweep added to the exciter: its rate is highest at the strike
// and relaxes as the envelope decays, a recognisable metallic "ping".  While
// it sounds it also opens the modal bank's level (modal_mix).
struct MetalChirp {
    float phase = 0.0f;
    float inc   = 0.0f;
    float env   = 0.0f;
    float decay = 1.0f;
    float depth = 0.0f;

    void reset() { phase = 0.0f; inc = 0.0f; env = 0.0f; decay = 1.0f; depth = 0.0f; }

    void trigger(float base_hz) {
        phase = 0.0f;
        inc   = phase_inc(base_hz);
        env   = 1.0f;
        decay = 0.9978f;
        depth = 0.16f;
    }

    bool active() const { return depth > 0.0f && env > kSilence; }

    float process() {
        const float fm = fastersinfullf(phase) * depth * env;
        phase += inc * (1.0f + (2.4f * env));
        wrap_phase(phase);
        env *= decay;
        return fm;
    }

    // The modal bank's level while the chirp sounds.
    float open_modal(float modal_mix) const {
        return (env > kSilence) ? modal_mix * (1.0f + (0.35f * env)) : modal_mix;
    }
};

// ── Onset ramp ──────────────────────────────────────────────────────────────
// A linear fade-in over attack_ms (0 = none), applied to the whole voice.
struct Onset {
    float env = 1.0f;
    float inc = 0.0f;

    void reset() { env = 1.0f; inc = 0.0f; }

    void trigger(float attack_ms) {
        if (attack_ms > 0.001f) {
            env = 0.0f;
            inc = 1000.0f / (attack_ms * kSampleRate);
        } else {
            env = 1.0f;
            inc = 0.0f;
        }
    }

    float apply(float x) {
        if (inc > 0.0f && env < 1.0f) {
            env = fminf(1.0f, env + inc);
            x *= env;
        }
        return x;
    }
};

// ── Pitch envelope ──────────────────────────────────────────────────────────
// Decays per sample; the swept booms read it (sweep = base + amount · env).
struct PitchEnv {
    float env    = 0.0f;
    float decay  = 1.0f;
    float amount = 0.0f;

    void reset() { env = 0.0f; decay = 1.0f; amount = 0.0f; }
    void set(float e, float d, float a) { env = e; decay = d; amount = a; }
    void process() {
        if (amount > 0.0f && env > kSilence) env *= decay;
    }
};

// ── Boom ────────────────────────────────────────────────────────────────────
// A low sine body under the strike, with its own decay and a short attack
// ramp.  A fixed boom keeps the inc it was loaded with; a swept one has its
// frequency set every sample by the unit (set_freq) before process().  tune
// is a base-tune multiplier for those sweeps (1.0 = as shipped).
struct Boom {
    float phase      = 0.0f;
    float inc        = 0.0f;
    float env        = 0.0f;
    float decay      = 1.0f;
    float mix        = 0.0f;
    float attack_env = 1.0f;
    float attack_inc = 1.0f;
    float tune       = 1.0f;

    void reset() {
        phase = 0.0f; inc = 0.0f; env = 0.0f; decay = 1.0f; mix = 0.0f;
        attack_env = 1.0f; attack_inc = 1.0f; tune = 1.0f;
    }

    void load(float inc_, float env_, float decay_, float mix_, float attack_env_, float attack_inc_) {
        inc = inc_; env = env_; decay = decay_; mix = mix_;
        attack_env = attack_env_; attack_inc = attack_inc_;
    }

    bool active() const { return mix > 0.0f && env > kSilence; }

    void set_freq(float hz) { inc = phase_inc(hz); }

    float process() {
        attack_env = fminf(1.0f, attack_env + attack_inc);
        const float b = fastersinfullf(phase) * env * mix * attack_env;
        phase += inc;
        wrap_phase(phase);
        env *= decay;
        return b;
    }
};

// ── Strike transient ────────────────────────────────────────────────────────
// The broadband attack a modal bank cannot make — a beater click, a stick or
// hand slap: white noise through a difference-of-one-poles band-pass
// (LP(a_hi) − LP(a_lo)), decaying in ~10-30 ms.  It draws from the voice's
// own noise generator.
struct Transient {
    float env   = 0.0f;   // 0 = layer off
    float decay = 1.0f;
    float gain  = 0.0f;
    float a_lo  = 0.0f;   // low corner one-pole coefficient
    float a_hi  = 0.0f;   // high corner one-pole coefficient
    float lp_lo = 0.0f;
    float lp_hi = 0.0f;

    void reset() {
        env = 0.0f; decay = 1.0f; gain = 0.0f; a_lo = 0.0f; a_hi = 0.0f;
        lp_lo = lp_hi = 0.0f;
    }

    void trigger(float env_, float decay_, float gain_, float a_lo_, float a_hi_) {
        env = env_; decay = decay_; gain = gain_; a_lo = a_lo_; a_hi = a_hi_;
        lp_lo = lp_hi = 0.0f;
    }

    bool active() const { return env > kSilence; }

    float process(FastNoise& noise) {
        const float wn = noise.process();
        const float lo = one_pole_lp(wn, a_lo, lp_lo);
        const float band = one_pole_lp(wn, a_hi, lp_hi) - lo;
        const float out = band * env * gain;
        env *= decay;
        return out;
    }
};

// ── Noise gate ──────────────────────────────────────────────────────────────
// An enveloped LFO on the noise level: gate = 1 − depth·(0.5 + 0.5·sin), with
// the depth decaying each sample so the modulation fades into the plain tail
// (decay 1.0 = it never fades).  Clap bursts, shaker grains, a brush swirl.
struct NoiseGate {
    float phase = 0.0f;
    float inc   = 0.0f;
    float depth = 0.0f;
    float decay = 1.0f;

    void reset() { phase = 0.0f; inc = 0.0f; depth = 0.0f; decay = 1.0f; }

    bool active() const { return depth > kSilence; }

    // This sample's gain on the noise.
    float gain() {
        const float am = 1.0f - depth * (0.5f + 0.5f * fastersinfullf(phase));
        phase += inc;
        wrap_phase(phase);
        depth *= decay;
        return fmaxf(0.0f, am);
    }
};

// ── S-curve glide ───────────────────────────────────────────────────────────
// A bend a hand or an arm makes: held at 1 for hold samples, then an S-shaped
// (smoothstep) fall to 0 over len samples.  An exponential envelope starts
// moving at once and moves fastest at the start; a squeezed talking drum or a
// pressed bayan holds, then eases in and out.  A unit reads env as its bend
// amount (freq = base ± depth · env).
struct GlideCurve {
    uint32_t n       = 0;
    uint32_t hold    = 0;
    uint32_t len     = 0;      // 0 = off
    float    inv_len = 0.0f;
    float    env     = 0.0f;

    void reset() { n = 0; hold = 0; len = 0; inv_len = 0.0f; env = 0.0f; }

    void trigger(float hold_ms, float len_ms) {
        n       = 0;
        hold    = (uint32_t)(hold_ms * 0.001f * kSampleRate);
        len     = (uint32_t)(len_ms * 0.001f * kSampleRate);
        if (len < 1) len = 1;
        inv_len = 1.0f / (float)len;
        env     = 1.0f;
    }

    bool active() const { return len > 0; }

    float process() {
        if (n > hold) {
            const uint32_t k = n - hold;
            if (k >= len) {
                env = 0.0f;
            } else {
                const float u = (float)k * inv_len;
                env = 1.0f - u * u * (3.0f - 2.0f * u);
            }
        }
        if (n <= hold + len) ++n;
        return env;
    }
};

// ── Punch: the transient shaper ─────────────────────────────────────────────
// A gain curve over the voice, started by the strike — a drum synth knows the
// exact sample its transient starts on, so there is nothing to detect.  One
// bipolar knob (−1 .. +1, 0 = off):
//   + PUNCH: the attack passes untouched for hold_ms, then the body is pulled
//     down (τ = body_ms) to knob_exp2(−2.5·amount): −15 dB at the top.  The
//     attack is not boosted: every unit ends in a peak limiter that already
//     takes 5-25 dB off its hard strikes, and would take a boost straight back
//     off.  Lowering the body is the contrast the limiter cannot undo; the
//     part gets quieter (the drumlogue's part level gives it back).
//   − SOFTER: the voice fades in from 1 − 0.95·|amount| over τ = 2-20 ms, a
//     rounder strike under a full body.
// At 0 trigger() leaves it inactive and apply() returns x untouched, so a
// preset renders byte-identically.  One multiply per sample while active.
struct Punch {
    float    gain   = 1.0f;
    float    target = 1.0f;
    float    coef   = 0.0f;   // one-pole step toward target
    uint32_t hold   = 0;      // samples left before the gain starts to move
    bool     active = false;

    void reset() { gain = 1.0f; target = 1.0f; coef = 0.0f; hold = 0; active = false; }

    // amount: the knob, −1 .. +1.  hold_ms / body_ms: how long the attack
    // stays untouched and how fast the body then drops (a unit picks them for
    // its family; the defaults suit a stick or hand strike).
    void trigger(float amount, float hold_ms = 5.0f, float body_ms = 15.0f) {
        amount = clampf(amount, -1.0f, 1.0f);
        if (amount == 0.0f) { reset(); return; }
        active = true;
        if (amount > 0.0f) {
            gain   = 1.0f;
            target = knob_exp2(-2.5f * amount);
            hold   = (uint32_t)(hold_ms * 0.001f * kSampleRate);
            coef   = 1.0f - expf(-1.0f / (body_ms * 0.001f * kSampleRate));
        } else {
            gain   = 1.0f + 0.95f * amount;
            target = 1.0f;
            hold   = 0;
            coef   = 1.0f - expf(-1.0f / ((2.0f - 18.0f * amount) * 0.001f * kSampleRate));
        }
    }

    float apply(float x) {
        if (!active) return x;
        if (hold > 0) {
            --hold;
        } else {
            gain += (target - gain) * coef;
            // A softened attack is done once it is back at full level.
            if (target == 1.0f && gain > 0.9999f) { active = false; gain = 1.0f; return x; }
        }
        return x * gain;
    }
};

// The Punch knob's raw value (−100 .. 100) → the shaper's amount.
inline float punch_amount(int32_t knob) { return clampf((float)knob * 0.01f, -1.0f, 1.0f); }

}  // namespace brc

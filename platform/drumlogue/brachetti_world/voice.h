#pragma once
// Brachetti World — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the modal bank it strikes, and the five layers this family
// adds for instruments no other family engine can make.  Each layer is idle
// until a preset's table turns it on (level 0 = off):
//
//   Friction   a stick-slip voice: a band-limited sawtooth whose pitch the
//              thumb bends, through two head formants        (cuica)
//   Wire       a struck steel wire (a Karplus-Strong loop) under a gourd
//              resonance that sweeps as the gourd swings     (berimbau)
//   Buzz       a mirliton or snare string rattling against the vibrating
//              body: noise gated by the bank's own motion    (balafon)
//   Jingles    pairs of thin brass discs clashing and bouncing
//              for a few tens of ms                          (riq, pandeiro)
//   Particles  random bead / seed / pebble collisions through a
//              shell's two resonances                        (shekere, rainstick)

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       kLn0001, kSampleRate, kSilence
//   modal_bank.h     ModalBank
//   noise.h          FastNoise
//   small_filters.h  Resonator2, one_pole_lp
//   voice_base.h     VoiceBase
//   voice_layers.h   GlideCurve, Onset
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/noise.h"
#include "../brachetti_common/small_filters.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
#include <cmath>
#include <cstdint>
#include "../common/attributes.h"

namespace world {

// A resonator's peak gain is about 1/(1−r); scaling its input by (1−r) keeps
// every resonator here near unity at its peak, whatever its Q.
inline float pole_radius(float t60_ms) {
    return expf(brc::kLn0001 / (t60_ms * 0.001f * brc::kSampleRate));
}

// The per-sample multiplier of an envelope that falls 60 dB in t60_ms.
inline float t60_mul(float t60_ms) { return pole_radius(t60_ms); }

// ── Friction ────────────────────────────────────────────────────────────────
struct Friction {
    float level = 0.0f;           // 0 = off
    float phase = 0.0f;           // 0..1
    float hz = 0.0f, rise = 0.0f, fall = 0.0f;   // base, + rise·, − fall· (fractions)
    float env = 0.0f, att_inc = 0.0f, decay = 1.0f, noise = 0.0f;
    uint32_t n = 0, hold = 0;
    brc::GlideCurve up, down;     // the thumb pressing, then releasing
    brc::Resonator2 f1, f2;
    float g1 = 0.0f, g2 = 0.0f;   // formant input gains, (1 − r)
    float noise_lp = 0.0f;

    void reset() { *this = Friction(); }

    bool active() const { return level > 0.0f; }

    force_inline float process(FastNoise& rnd) {
        // the pitch: up eases 0 → rise, down eases 0 → fall
        const float f = hz * (1.0f + rise * (1.0f - up.process()) - fall * (1.0f - down.process()));
        const float dt = f * (1.0f / brc::kSampleRate);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        // sawtooth with a polyBLEP correction at the wrap: the slip
        float s = 2.0f * phase - 1.0f;
        if (phase < dt) {
            const float t = phase / dt;
            s -= t + t - t * t - 1.0f;
        } else if (phase > 1.0f - dt) {
            const float t = (phase - 1.0f) / dt;
            s -= t * t + t + t + 1.0f;
        }
        // the stroke's envelope: a 20 ms onset, held, then its decay
        if (env < 1.0f && n <= hold) env = fminf(1.0f, env + att_inc);
        else if (n > hold) env *= decay;
        ++n;
        const float x = s * env;
        const float rub = brc::one_pole_lp(rnd.process(), 0.33f, noise_lp) * env * noise;
        const float y = 0.8f * x + 0.6f * f1.ring(x * g1) + 0.4f * f2.ring(x * g2) + rub;
        if (n > hold && env < brc::kSilence) level = 0.0f;
        return y * level;
    }
};

// ── Wire ────────────────────────────────────────────────────────────────────
constexpr int kWireLen = 1024;   // ≥ one period down to ~47 Hz

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
        buf[w] = lp * loop_gain;
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

// ── Buzz ────────────────────────────────────────────────────────────────────
struct Buzz {
    float level = 0.0f;           // 0 = off
    float ref = 1.0f;             // the bank's level at the strike
    float thresh = 0.25f;         // the part of it that the mirliton rides out
    brc::Resonator2 res;
    float g = 0.0f;

    void reset() { *this = Buzz(); }

    bool active() const { return level > 0.0f; }

    // body: this sample of the modal bank
    force_inline float process(float body, FastNoise& rnd) {
        const float d = fabsf(body) / ref - thresh;
        if (d <= 0.0f) return 0.0f;
        const float x = rnd.process() * d;
        return (res.ring(x * g) + 0.5f * x) * level;
    }
};

// ── Jingles ─────────────────────────────────────────────────────────────────
constexpr int kJinglePairs = 5;
constexpr int kJingleModes = 3;

struct Jingles {
    float level = 0.0f;           // 0 = off
    brc::Resonator2 mode[kJinglePairs][kJingleModes];
    float gain[kJinglePairs][kJingleModes] = {};   // each mode's share of a strike
    float g = 0.0f;               // the noise clash's level beside the modes
    uint32_t next[kJinglePairs] = {};
    float amp[kJinglePairs] = {};
    float clash[kJinglePairs] = {};
    float clash_mul = 0.0f;
    uint32_t bounce_lo = 0, bounce_span = 1, spread = 0;
    uint32_t n = 0;
    uint32_t lcg = 1u;

    void reset() { *this = Jingles(); }

    bool active() const { return level > 0.0f; }

    // a uniform draw in [0, 1)
    float draw() {
        lcg = lcg * 1664525u + 1013904223u;
        return (float)(lcg >> 8) * (1.0f / 16777216.0f);
    }

    force_inline float process(FastNoise& rnd) {
        float y = 0.0f;
        bool alive = false;
        for (int p = 0; p < kJinglePairs; ++p) {
            float strike = 0.0f;   // the discs meet: an impulse into their modes
            if (amp[p] > 0.05f) {
                alive = true;
                if (n == next[p]) {
                    strike = amp[p];
                    clash[p] = amp[p];
                    amp[p] *= 0.35f + 0.25f * draw();
                    next[p] = n + bounce_lo + (uint32_t)(draw() * (float)bounce_span);
                    if (next[p] > spread) amp[p] = 0.0f;
                }
            }
            float rattle = 0.0f;   // and scrape: a short noise burst beside them
            if (clash[p] > 1e-4f) {
                rattle = rnd.process() * clash[p];
                clash[p] *= clash_mul;
                alive = true;
            }
            for (int k = 0; k < kJingleModes; ++k) y += mode[p][k].ring(strike * gain[p][k]);
            y += g * rattle;
        }
        ++n;
        if (!alive && n > spread + 9600u) level = 0.0f;
        return y * level;
    }
};

// ── Particles ───────────────────────────────────────────────────────────────
struct Particles {
    float level = 0.0f;           // 0 = off
    float rate = 0.0f;            // collisions per sample at the envelope's top
    float env = 0.0f, att_inc = 0.0f, decay = 1.0f;
    uint32_t n = 0, hold = 0;
    float click = 0.0f, click_mul = 0.0f;
    brc::Resonator2 r1, r2;
    float g1 = 0.0f, g2 = 0.0f, mix2 = 0.5f, direct = 0.3f;
    uint32_t lcg = 1u;

    void reset() { *this = Particles(); }

    bool active() const { return level > 0.0f; }

    force_inline float process(FastNoise& rnd) {
        if (n < hold) env = fminf(1.0f, env + att_inc);
        else env *= decay;
        ++n;
        lcg = lcg * 1664525u + 1013904223u;
        const float u = (float)(lcg >> 8) * (1.0f / 16777216.0f);
        if (u < rate * env) {                    // a collision
            lcg = lcg * 1664525u + 1013904223u;
            click += env * (0.2f + 0.8f * (float)(lcg >> 8) * (1.0f / 16777216.0f));
        }
        const float x = rnd.process() * click;
        click *= click_mul;
        if (n > hold && env < brc::kSilence && click < brc::kSilence) level = 0.0f;
        return (r1.ring(x * g1) + mix2 * r2.ring(x * g2) + direct * x) * level;
    }
};

}  // namespace world

struct VoiceState : brc::VoiceBase {
    brc::ModalBank   modal;
    brc::Onset       onset;
    world::Friction  friction;
    world::Wire      wire;
    world::Buzz      buzz;
    world::Jingles   jingles;
    world::Particles particles;

    bool layers_active() const {
        return friction.active() || wire.active() || buzz.active() || jingles.active() || particles.active();
    }

    // Every layer back to silence for a new strike.  The settings the knobs
    // own (mallet, noise envelopes, noise filter) stay.
    void reset_for_strike() {
        reset_base();
        modal.reset();
        onset.reset();
        friction.reset();
        wire.reset();
        buzz.reset();
        jingles.reset();
        particles.reset();
    }
};

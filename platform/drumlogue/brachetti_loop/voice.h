#pragma once
// Brachetti Loop — one voice: the exciter (mallet + noise burst, from
// VoiceBase), the modal bank it can strike, and the family's own layer, the
// LOOP DRUM: a Karplus-Strong delay loop made into a drum head by an all-pass
// filter inside it.
//
// The design follows Miguel Moreno's "Realistic drums with karplus-strong
// synthesis" (Mike Moreno DSP, 13 March 2022,
// https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/,
// the Pure Data instrument [mymembrane~] in https://github.com/MikeMorenoDSP/pd-mkmr):
//
//   * a delay line looped through a one-pole low-pass (damping) and a
//     feedback gain: the Karplus-Strong loop (Karplus & Strong 1983, with
//     Jaffe & Smith's 1983 extensions as J. O. Smith's "Physical Audio Signal
//     Processing" describes them);
//   * a Schroeder all-pass comb (a feedback comb and a feedforward comb in
//     one delay) inside the loop: its frequency-dependent delay moves the
//     loop's resonances off the harmonic series, and the ratio of its delay
//     to the loop's sets how: a string becomes a drum;
//   * a pitch envelope that shortens BOTH delays at the strike and lets them
//     relax: the head's tension rising with a hard stroke, and a chirp that
//     sharpens the attack;
//   * the excitation: one sine cycle one period long, plus white noise
//     filtered and enveloped by the same window;
//   * the snares: an envelope follower on the drum's output sets the level of
//     band-passed white noise (3 kHz in the article).
//
// The article gives the structure, not code or numbers: the tuning formula
// (the loop length that puts the first resonance exactly on the note, through
// the phase delays of the all-pass, the loss filter and the DC blocker), the
// DC blocker in the loop, the loss filter designed from two decay times
// (after Välimäki et al. 1996), the loss compensation, the strike-position
// comb (HitPos) and Karplus & Strong's own "drum" sign-flip blend are this
// unit's.
// README.md, "The loop drum", has all of it.

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       kSilence
//   modal_bank.h     ModalBank
//   noise.h          FastNoise
//   small_filters.h  Resonator2, one_pole_hp, one_pole_lp
//   voice_base.h     VoiceBase
//   voice_layers.h   Onset
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/noise.h"
#include "../brachetti_common/small_filters.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
#include <cmath>
#include <cstdint>
#include "../common/attributes.h"

namespace loop {

constexpr int kLoopLen = 2048;   // ≥ one period down to ~23.4 Hz
constexpr int kApLen   = 1024;   // the all-pass's delay line

// One read of a circular delay line, `delay` samples (fractional) behind the
// write index w: linear interpolation.
force_inline float read_delay(const float* buf, int len, int w, float delay) {
    float rp = (float)w - delay;
    if (rp < 0.0f) rp += (float)len;
    const int i0 = (int)rp;
    const int i1 = (i0 + 1 == len) ? 0 : i0 + 1;
    const float fr = rp - (float)i0;
    return buf[i0] + fr * (buf[i1] - buf[i0]);
}

struct Drum {
    float level = 0.0f;           // 0 = off
    float* buf = nullptr;         // the loop's delay line   (synth_engine.h owns them)
    float* ap  = nullptr;         // the all-pass's delay line
    int   w = 0, aw = 0;          // their write indices

    // The loop, at rest (the glide shortens both delays from these).
    float n0 = 0.0f;              // the loop delay, samples
    float m0 = 0.0f;              // the all-pass delay, samples
    float g = 0.0f;               // the all-pass gain, −0.95..0.95
    float loop_gain = 0.0f;       // per trip round the loop (< 1)
    float damp = 0.5f, lp = 0.0f; // the loss filter: highs die first
    float dc_a = 0.0f, dc = 0.0f; // the DC blocker in the loop
    float flip = 0.0f;            // Karplus & Strong's drum blend: P(sign flip), 0 = off
    uint32_t lcg = 1u;

    // The glide: both delays × 1 / (1 + bend · env), env falling from 1.
    float bend = 0.0f, bend_env = 0.0f, bend_mul = 0.0f;

    // The excitation: a sine cycle (sin_s, sin_c rotate it) over exc_len
    // samples, minus hit× itself exc_d samples later (the strike position's
    // comb), plus noise low-passed and under the cycle's Hann window
    // (win_s, win_c rotate it at half the rate).
    uint32_t n = 0, exc_len = 0, exc_d = 0;
    float rot_c = 1.0f, rot_s = 0.0f;        // cos, sin of 2π / exc_len
    float half_c = 1.0f, half_s = 0.0f;      // cos, sin of π / exc_len
    float sin_c = 1.0f, sin_s = 0.0f;        // the cycle
    float del_c = 1.0f, del_s = 0.0f;        // the comb's copy
    float win_c = 1.0f, win_s = 0.0f;        // the window, as sin² of the half rate
    float tone = 0.0f, hit = 0.0f, noise = 0.0f, noise_a = 1.0f, noise_lp = 0.0f;

    // The snares: a follower on the head's output → the level of band-passed noise.
    float sn_level = 0.0f, sn_env = 0.0f, sn_att = 0.0f, sn_rel = 0.0f, sn_g = 0.0f;
    brc::Resonator2 sn_bp;

    float env = 1.0f;             // falls below silence → the layer stops

    void reset() {
        float* b = buf;
        float* a = ap;
        *this = Drum();
        buf = b;
        ap = a;
    }

    bool active() const { return level > 0.0f && buf != nullptr; }

    // The excitation's next sample (already velocity-scaled by tone / noise).
    force_inline float excite(FastNoise& rnd) {
        if (n >= exc_len + exc_d) return 0.0f;
        float e = 0.0f;
        if (n < exc_len) {
            // window: sin²(πn/len) = the Hann window, rotated at half the rate
            const float win = win_s * win_s;
            noise_lp += noise_a * (rnd.process() - noise_lp);
            e = tone * sin_s + noise * noise_lp * win;
            const float s = sin_s * rot_c + sin_c * rot_s;
            sin_c = sin_c * rot_c - sin_s * rot_s;
            sin_s = s;
            const float h = win_s * half_c + win_c * half_s;
            win_c = win_c * half_c - win_s * half_s;
            win_s = h;
        }
        if (n >= exc_d && exc_d > 0u) {   // the comb: the same cycle, exc_d later
            e -= hit * tone * del_s;
            const float s = del_s * rot_c + del_c * rot_s;
            del_c = del_c * rot_c - del_s * rot_s;
            del_s = s;
        }
        return e;
    }

    force_inline float process(FastNoise& rnd) {
        const float e = excite(rnd);
        ++n;
        // The glide: the head's tension relaxing after the stroke.
        float k = 1.0f;
        if (bend_env > 1e-4f) {
            k = 1.0f / (1.0f + bend * bend_env);
            bend_env *= bend_mul;
        }
        // Once round the loop: the delay, the loss filter, the DC blocker,
        // the gain (sign-flipped at random: the K-S drum blend) ...
        const float x = read_delay(buf, kLoopLen, w, n0 * k);
        lp += damp * (x - lp);
        float u = brc::one_pole_hp(lp, dc_a, dc) * loop_gain;
        if (flip > 0.0f) {
            lcg = lcg * 1664525u + 1013904223u;
            if ((float)(lcg >> 8) * (1.0f / 16777216.0f) < flip) u = -u;
        }
        // ... and the all-pass: v[n] = u + g·v[n−M], y = −g·v + v[n−M].
        const float vd = read_delay(ap, kApLen, aw, m0 * k);
        const float v = u + g * vd;
        ap[aw] = v;
        if (++aw == kApLen) aw = 0;
        const float y = e + vd - g * v;
        buf[w] = y;
        if (++w == kLoopLen) w = 0;
        // The snares ride the head's motion.
        float out = y;
        if (sn_level > 0.0f) {
            const float a = fabsf(y);
            sn_env += ((a > sn_env) ? sn_att : sn_rel) * (a - sn_env);
            out += sn_level * sn_bp.ring(rnd.process() * sn_env * sn_g);
        }
        env = brc::one_pole_lp(fabsf(y), 0.001f, env);
        if (n > 4800u && env < brc::kSilence) level = 0.0f;
        return out * level;
    }
};

}  // namespace loop

struct VoiceState : brc::VoiceBase {
    brc::ModalBank modal;
    brc::Onset     onset;
    loop::Drum     drum;

    bool layers_active() const { return drum.active(); }

    // Every layer back to silence for a new strike.  The settings the knobs
    // own (mallet, noise envelopes, noise filter) stay.
    void reset_for_strike() {
        reset_base();
        modal.reset();
        onset.reset();
        drum.reset();
    }
};

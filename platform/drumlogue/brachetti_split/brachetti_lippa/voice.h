#pragma once
// Brachetti Lippa — one voice: the exciter (mallet + noise burst, from
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
//   dsp_math.h       clampf, kSilence
//   modal_bank.h     ModalBank
//   noise.h          FastNoise, crackle
//   small_filters.h  Resonator2, one_pole_hp, one_pole_lp
//   voice_base.h     VoiceBase
//   voice_layers.h   Onset
#include "../../brachetti_common/dsp_math.h"
#include "../../brachetti_common/modal_bank.h"
#include "../../brachetti_common/noise.h"
#include "../../brachetti_common/small_filters.h"
#include "../../brachetti_common/voice_base.h"
#include "../../brachetti_common/voice_layers.h"
#include <cmath>
#include <cstdint>
#include "../../common/attributes.h"

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
    // On a strike into a ringing loop the glide's factor slews (k_slew > 0)
    // from where the old ring had it: a jump in the delays would jump the
    // read position inside the ringing waveform.  0 on a fresh strike.
    float k_now = 1.0f, k_slew = 0.0f;

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

    // The snares: a follower on the head's output → the level of band-passed
    // noise; sn_crackle blends the white noise towards a sparse crackle.
    float sn_level = 0.0f, sn_env = 0.0f, sn_att = 0.0f, sn_rel = 0.0f, sn_g = 0.0f;
    float sn_crackle = 0.0f;
    brc::Resonator2 sn_bp;

    float env = 1.0f;             // falls below silence → the layer stops

    void reset() {
        float* b = buf;
        float* a = ap;
        *this = Drum();
        buf = b;
        ap = a;
    }

    // What a ringing loop keeps when it is struck again: its delay lines (left
    // as they are), their write positions and its filters' states.  The new
    // strike then adds to the head's motion instead of cutting it off, which
    // would click (mymembrane~ ramps its output to 0 over 1 ms and clears its
    // delays instead).
    struct Motion { int w, aw; float lp, dc, env, sn_env, sn_z1, sn_z2, k; };
    Motion motion() const { return {w, aw, lp, dc, env, sn_env, sn_bp.z1, sn_bp.z2, k_now}; }
    void resume(const Motion& m, float slew) {
        w = m.w; aw = m.aw; lp = m.lp; dc = m.dc; env = m.env;
        sn_env = m.sn_env; sn_bp.z1 = m.sn_z1; sn_bp.z2 = m.sn_z2;
        k_now = m.k;
        k_slew = slew;
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
        if (k_slew > 0.0f) {   // only after a strike into a ringing loop
            k_now += k_slew * (k - k_now);
            k = k_now;
            if (n > 2400u) k_slew = 0.0f;   // 50 ms: settled to e^−50
        } else {
            k_now = k;
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
            float wn = rnd.process();
            if (sn_crackle > 0.0f) wn += sn_crackle * (brc::crackle(wn) - wn);
            out += sn_level * sn_bp.ring(wn * sn_env * sn_g);
        }
        env = brc::one_pole_lp(fabsf(y), 0.001f, env);
        if (n > 4800u && env < brc::kSilence) level = 0.0f;
        return out * level;
    }
};

}  // namespace loop

// ── mymembrane~: Miguel Moreno's drum, ported ───────────────────────────────
// instruments/mymembrane~.pd in pd-mkmr (Miguel Moreno, BSD-3-Clause; the
// notice is in README.md), object by object, with Pd's own formulas for the
// objects it uses (noise~, hip~, lop~, bp~, biquad~ with its lowpass~
// coefficients, vd~'s 4-point interpolation and its one-block minimum delay in
// a feedback loop, env~, the hv.tanh rational tanh).  Checked against Pd 0.54's
// renders of the patch with its help and demo settings: waveform correlation
// 0.94-1.00 over the first 200 ms, the same strongest partial, decay and
// periodicity (README.md, "mymembrane~ in BrcLippa").
//
//   loop:  delay(f, chirp) → hip~ 20 → × feedback → tanh → + excitation
//          → all-pass(apf, chirp; gain) → lop~ f·2^(6·damping)/2 → × boost → delay
//   out:   tanh(loop + the excitation's noise) + snares
//   excitation: a sine cycle one period long, + noise~ (seed 16 at every strike)
//          through a low-pass at f·2^(3v)/3 (Q 0.4) under a one-period window
//   chirp: the delay's frequency × (1 + (2+v)·e¹⁰), the all-pass's × (1 + (1+v)·e²⁰),
//          e a ramp 1 → 0 over 80 ms; the loop × (1 + 0.7·√v·e²⁰)
//   snares: noise^50 → hip~ 100 → bp~ 3000 Q 1, × the RMS (env~ 1024/512, line~ 1 ms)
//          of hip~ 100 of the drum, × the noise level
namespace mm {

constexpr float kSR = 48000.0f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kMinDelay = 65.0f;   // vd~ in a feedback loop: at least one block
// A restrike's old ring at or above this level (out_env, the drum's follower)
// gets no attack boost: Membrane::resume.
constexpr float kRingNoBoost = 0.1f;

struct PdNoise {   // noise~
    uint32_t val = 16u;
    force_inline float next() {
        const float o = (float)((int32_t)(val & 0x7fffffffu) - 0x40000000) * (1.0f / 1073741824.0f);
        val = val * 435898247u + 382842987u;
        return o;
    }
};
struct Hip {   // hip~
    float coef = 0.0f, last = 0.0f;
    void set(float hz) { coef = brc::clampf(1.0f - hz * kTwoPi / kSR, 0.0f, 1.0f); }
    force_inline float run(float x) {
        const float nw = x + coef * last;
        const float y = 0.5f * (1.0f + coef) * (nw - last);
        last = nw;
        return y;
    }
};
struct Lop {   // lop~
    float coef = 1.0f, last = 0.0f;
    void set(float hz) { coef = brc::clampf(hz * kTwoPi / kSR, 0.0f, 1.0f); }
    force_inline float run(float x) { last = coef * x + (1.0f - coef) * last; return last; }
};
struct Bp {   // bp~
    float c1 = 0.0f, c2 = 0.0f, g = 0.0f, y1 = 0.0f, y2 = 0.0f;
    static float qcos(float f) {
        if (f >= -(0.5f * 3.14159f) && f <= 0.5f * 3.14159f) {
            const float h = f * f;
            return ((h * h * h * (-1.0f / 720.0f) + h * h * (1.0f / 24.0f)) - h * 0.5f) + 1.0f;
        }
        return 0.0f;
    }
    void set(float hz, float q) {
        const float om = hz * (2.0f * 3.14159f) / kSR;
        float omr = (q < 0.001f) ? 1.0f : om / q;
        if (omr > 1.0f) omr = 1.0f;
        const float r = 1.0f - omr;
        c1 = 2.0f * qcos(om) * r;
        c2 = -r * r;
        g = 2.0f * omr * (omr + r * om);
    }
    force_inline float run(float x) {
        const float y = g * x + c1 * y1 + c2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};
struct Biquad {   // biquad~, as the patch's lowpass~ sets it
    float fb1 = 0.0f, fb2 = 0.0f, ff1 = 0.0f, ff2 = 0.0f, ff3 = 0.0f, w1 = 0.0f, w2 = 0.0f;
    void lowpass(float hz, float q) {
        hz = brc::clampf(hz, 20.0f, 22050.0f);
        const float om = hz * 6.28319f / kSR, s = sinf(om), c = cosf(om);
        const float alpha = s / (2.0f * fmaxf(q, 5.96046e-08f));
        const float a0 = 1.0f + alpha, a1 = -2.0f * c, a2 = 1.0f - alpha, b1 = 1.0f - c, b0 = b1 * 0.5f;
        fb1 = -a1 / a0; fb2 = -a2 / a0; ff1 = b0 / a0; ff2 = b1 / a0; ff3 = b0 / a0;
        w1 = w2 = 0.0f;
    }
    force_inline float run(float x) {
        const float w = x + fb1 * w1 + fb2 * w2;
        const float y = ff1 * w + ff2 * w1 + ff3 * w2;
        w2 = w1;
        w1 = w;
        return y;
    }
};
force_inline float hv_tanh(float x) {   // the patch's hv.tanh
    x = brc::clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
// vd~'s 4-point interpolation, d samples back from the write index w
force_inline float read4(const float* v, int mask, int w, float d) {
    const int id = (int)d;
    const float fr = d - (float)id;
    const float a = v[(w - id) & mask], b = v[(w - id - 1) & mask];
    const float c = v[(w - id - 2) & mask], dd = v[(w - id - 3) & mask];
    const float cmb = c - b;
    return b + fr * (cmb - 0.1666667f * (1.0f - fr) * ((dd - a - 3.0f * cmb) * fr + (dd + 2.0f * a - 3.0f * b)));
}

struct Membrane {
    float level = 0.0f;             // 0 = off
    float* buf = nullptr;           // the loop's delay line (2048, BrcLippa's)
    float* ap  = nullptr;           // the all-pass's (1024)
    int   w = 0, aw = 0;
    float f = 100.0f, apf = 100.0f; // the delay's and the all-pass's frequencies, Hz
    float fbk = 0.9f, apg = 0.0f, flip = 0.0f;
    uint32_t lcg = 1u;
    float v = 1.0f, velgain = 1.0f;
    float exc_len = 1.0f;           // the sine cycle and the noise window, samples
    float chirp_len = 3840.0f;      // the chirp's ramp, samples (80 ms)
    float depth_l = 3.0f, depth_a = 2.0f, boost = 0.7f;
    float noise = 1.0f, snares = 0.0f;
    Biquad nlp;
    Hip hip20, hip_sn, hip_env;
    Lop lp;
    Bp bp;
    PdNoise nz, sn;
    // env~ 1024/512: the Hann-weighted mean square in two half-window sums
    float env_lo = 0.0f, env_hi = 0.0f, env_prev = 0.0f, env_c = 1.0f, env_s = 0.0f;
    float env = 0.0f, env_step = 0.0f;
    int   env_p = 0, env_ramp = 0;
    float rot_c = 1.0f, rot_s = 0.0f;   // cos, sin of 2π / 1024
    // the delays after the chirp, samples: kSR / f and kSR / apf within vd~'s
    // limits, computed once at the strike
    float dl_rest = 480.0f, da_rest = 480.0f;
    // a strike into a ringing loop slews the delays from where the ring had them
    float dl_now = 0.0f, da_now = 0.0f, slew = 0.0f;
    uint32_t n = 0;
    float out_env = 1.0f;

    void reset() {
        float* b = buf;
        float* a = ap;
        *this = Membrane();
        buf = b;
        ap = a;
    }

    bool active() const { return level > 0.0f && buf != nullptr; }

    struct Motion {
        int w, aw; float hip20, lp, dl, da, out_env, env, bp1, bp2, hsn, henv;
    };
    Motion motion() const {
        return {w, aw, hip20.last, lp.last, dl_now, da_now, out_env, env, bp.y1, bp.y2, hip_sn.last, hip_env.last};
    }
    void resume(const Motion& m, float s) {
        w = m.w; aw = m.aw; hip20.last = m.hip20; lp.last = m.lp; dl_now = m.dl; da_now = m.da;
        out_env = m.out_env; env = m.env; bp.y1 = m.bp1; bp.y2 = m.bp2; hip_sn.last = m.hsn; hip_env.last = m.henv;
        slew = s;
        // The attack's loop boost (up to × 1.7 for ~10 ms) is meant for the new
        // excitation; here the loop also holds the old ring, and boosting a loud
        // ring drives it into the tanh: a click (MmFloor restruck 30-150 ms after
        // a strike: 2-3× the high frequencies of a clean restrike,
        // restrike_check.py).  So the boost shrinks with the ring's level, gone
        // at kRingNoBoost (−20 dB): a quiet tail still gets the attack a fresh
        // strike gets.
        boost *= brc::clampf(1.0f - m.out_env * (1.0f / kRingNoBoost), 0.0f, 1.0f);
    }

    force_inline float process() {
        const float nf = (float)n;
        // the excitation: a sine cycle (cos~ of the ramp − 0.25) and the noise
        // under its window (in 0.25 ms, out over 0.25 ms after the period)
        float exc = 0.0f, direct = 0.0f;
        if (nf < exc_len + 12.0f) {
            float win = fminf(1.0f, nf * (1.0f / 12.0f));
            if (nf > exc_len) win = fmaxf(0.0f, 1.0f - (nf - exc_len) * (1.0f / 12.0f));
            const float n2 = nlp.run(nz.next()) * win * 2.0f * noise;
            const float sine = (nf < exc_len) ? sinf(kTwoPi * nf / exc_len) * 0.5f : 0.0f;
            exc = (sine + n2) * velgain;
            direct = n2 * 2.0f * velgain;
        }
        // the chirp
        float dl = dl_rest, da = da_rest, bst = 1.0f;
        if (nf < chirp_len) {
            const float r = 1.0f - nf / chirp_len;
            const float r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;
            const float e10 = r8 * r2, e20 = e10 * e10;
            dl = kSR / (f * (1.0f + depth_l * e10));
            da = kSR / (apf * (1.0f + depth_a * e20));
            bst = 1.0f + e20 * boost;
            dl = brc::clampf(dl, kMinDelay, 2040.0f);
            da = brc::clampf(da, kMinDelay, 1016.0f);
        }
        if (slew > 0.0f) {   // only after a strike into a ringing loop
            dl_now += slew * (dl - dl_now);
            da_now += slew * (da - da_now);
            dl = dl_now;
            da = da_now;
            if (n > 2400u) slew = 0.0f;
        } else {
            dl_now = dl;
            da_now = da;
        }
        ++n;
        // the loop
        float back = hv_tanh(hip20.run(read4(buf, 2047, w, dl)) * fbk);
        if (flip > 0.0f) {   // Karplus & Strong's drum blend (BrcLippa's Blend knob)
            lcg = lcg * 1664525u + 1013904223u;
            if ((float)(lcg >> 8) * (1.0f / 16777216.0f) < flip) back = -back;
        }
        const float u = exc + back;
        const float wd = read4(ap, 1023, aw, da);
        const float wv = u + apg * wd;
        ap[aw] = wv;
        aw = (aw + 1) & 1023;
        const float y = lp.run(wd - apg * wv) * bst;
        buf[w] = y;
        w = (w + 1) & 2047;
        const float drum = hv_tanh(y + direct);
        float out = drum;
        if (snares > 0.0f) {
            const float s = sn.next();
            const float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8, s32 = s16 * s16;
            const float snr = bp.run(hip_sn.run(s32 * s16 * s2));
            const float h = hip_env.run(drum);
            const float hh = h * h;
            env_lo += (1.0f - env_c) * hh;   // this sample in the next window's older half
            env_hi += (1.0f + env_c) * hh;   // and in this window's newer half
            const float c = env_c * rot_c - env_s * rot_s;
            env_s = env_s * rot_c + env_c * rot_s;
            env_c = c;
            if (++env_p == 512) {
                const float ms = (env_prev + env_hi) * (1.0f / 1024.0f);
                env_prev = env_lo;
                env_lo = env_hi = 0.0f;
                env_c = 1.0f; env_s = 0.0f; env_p = 0;
                env_step = (sqrtf(fmaxf(ms, 0.0f)) - env) * (1.0f / 48.0f);   // line~ over 1 ms
                env_ramp = 48;
            }
            if (env_ramp > 0) { env += env_step; --env_ramp; }
            out += snr * env * snares;
        }
        out_env = brc::one_pole_lp(fabsf(drum), 0.001f, out_env);
        if (n > 4800u && out_env < brc::kSilence) level = 0.0f;
        return out * level;
    }
};

}  // namespace mm

struct VoiceState : brc::VoiceBase {
    brc::ModalBank modal;
    brc::Onset     onset;
    loop::Drum     drum;
    mm::Membrane   mm;      // mymembrane~ (a preset uses one or the other)

    bool layers_active() const { return drum.active() || mm.active(); }

    // Every layer back to silence for a new strike.  The settings the knobs
    // own (mallet, noise envelopes, noise filter) stay.
    void reset_for_strike() {
        reset_base();
        modal.reset();
        onset.reset();
        drum.reset();
        mm.reset();
    }
};

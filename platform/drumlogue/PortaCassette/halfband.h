#pragma once
/*
 * halfband.h — oversampling for PortaCassette's waveshapers
 *
 * A memoryless waveshaper run at 48 kHz folds every harmonic above 24 kHz back
 * into the audio band as an inharmonic tone: a 10 kHz hi-hat partial's 5th
 * harmonic lands at 1.9 kHz.  Tape cannot do that — its harmonics of treble lie
 * above its own bandwidth — so the shapers run oversampled between polyphase
 * IIR halfband upsamplers and their matching downsamplers.
 *
 * Each 2x stage is two parallel chains of first-order allpass sections in
 * z^-2 (Laurent de Soras' HIIR layout), H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2,
 * with elliptic halfband coefficients:
 *
 *   stage A, 48 <-> 96 kHz, 4 coefficients: flat to 20 kHz (ripple < 2e-6
 *     dB), at least 64 dB of rejection from 28 kHz.
 *   stage B, 96 <-> 192 kHz, 2 coefficients: flat to 20 kHz, at least 70 dB
 *     of rejection from 76 kHz -- everything that could fold below 20 kHz.
 *     (What folds into 20-48 kHz, stage A removes on the way down.)
 *
 * 64-70 dB puts what is left of an alias under the tape's own noise floor;
 * six and four coefficients bought 96 and 132 dB for twice the work.
 *
 * 2x is stage A alone; 4x is A then B.  The response is not linear-phase,
 * which is irrelevant inside a tape model whose wow & flutter already moves
 * the delay by tens of samples.
 *
 * As in biquad.h, the state lives in d-registers with L in lane 0 and R in
 * lane 1, so both channels run down one dependency chain.
 */

#include <arm_neon.h>

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline, optimize("Ofast")))
#endif

#define HB_A_COEFS 4
#define HB_B_COEFS 2

/* Path 0 takes the even-indexed coefficients, path 1 the odd ones. */
static const float kHbACoefs[HB_A_COEFS] = {
    0.089698028237028396f, 0.31091938742016656f,
    0.57735026918962573f,  0.85066977141837596f,
};
static const float kHbBCoefs[HB_B_COEFS] = {
    0.12662043569874445f,  0.56579571523312822f,
};

template <int N> struct hb_chain_t { float32x2_t x[N], y[N]; };

typedef struct { hb_chain_t<HB_A_COEFS> up, down; } hb2x_state_t;
typedef struct { hb_chain_t<HB_A_COEFS> up_a, down_a;
                 hb_chain_t<HB_B_COEFS> up_b, down_b; } hb4x_state_t;

template <int N> fast_inline void hb_chain_reset(hb_chain_t<N>* c) {
    for (int i = 0; i < N; ++i) c->x[i] = c->y[i] = vdup_n_f32(0.0f);
}

fast_inline void hb2x_reset(hb2x_state_t* s) {
    hb_chain_reset(&s->up);
    hb_chain_reset(&s->down);
}

fast_inline void hb4x_reset(hb4x_state_t* s) {
    hb_chain_reset(&s->up_a);  hb_chain_reset(&s->down_a);
    hb_chain_reset(&s->up_b);  hb_chain_reset(&s->down_b);
}

/* Run the two allpass paths, one {L,R} sample each, with the chain's state
 * and coefficients in registers.  Each section is
 * y[n] = a*(x[n] - y[n-1]) + x[n-1] at the lower rate.  `k` holds the
 * coefficients two to a d-register, so each multiply-accumulate takes its
 * coefficient from a lane rather than rebuilding it from an immediate. */
template <int N>
fast_inline void hb_paths(float32x2_t* x, float32x2_t* y, const float32x2_t* k,
                          float32x2_t* s0, float32x2_t* s1) {
    float32x2_t a = *s0, b = *s1;
#pragma GCC unroll 8
    for (int i = 0; i < N; i += 2) {
        const float32x2_t ta = vmla_lane_f32(x[i],     vsub_f32(a, y[i]),     k[i / 2], 0);
        const float32x2_t tb = vmla_lane_f32(x[i + 1], vsub_f32(b, y[i + 1]), k[i / 2], 1);
        x[i] = a;      y[i] = ta;      a = ta;
        x[i + 1] = b;  y[i + 1] = tb;  b = tb;
    }
    *s0 = a;
    *s1 = b;
}

/* A chain's state copied into locals for the length of a quad. */
template <int N> struct hb_regs_t {
    float32x2_t x[N], y[N], k[N / 2];
    fast_inline void load(const hb_chain_t<N>* c, const float* coefs) {
#pragma GCC unroll 8
        for (int i = 0; i < N; ++i) { x[i] = c->x[i]; y[i] = c->y[i]; }
#pragma GCC unroll 4
        for (int i = 0; i < N / 2; ++i) k[i] = vld1_f32(coefs + 2 * i);
    }
    fast_inline void store(hb_chain_t<N>* c) const {
#pragma GCC unroll 8
        for (int i = 0; i < N; ++i) { c->x[i] = x[i]; c->y[i] = y[i]; }
    }
    /* One frame in, two frames at twice the rate out (earlier one first). */
    fast_inline void up(float32x2_t in, float32x2_t* out0, float32x2_t* out1) {
        float32x2_t a = in, b = in;
        hb_paths<N>(x, y, k, &a, &b);
        *out0 = a;
        *out1 = b;
    }
    /* Two frames in (earlier one first), one frame at half the rate out. */
    fast_inline float32x2_t down(float32x2_t in0, float32x2_t in1) {
        float32x2_t a = in1, b = in0;
        hb_paths<N>(x, y, k, &a, &b);
        return vmul_n_f32(vadd_f32(a, b), 0.5f);
    }
};

/* {l0,l1,l2,l3},{r0,r1,r2,r3} -> four {l,r} frames, and back. */
fast_inline void hb_unpack(float32x4_t l, float32x4_t r, float32x2_t f[4]) {
    const float32x4x2_t p = vzipq_f32(l, r);
    f[0] = vget_low_f32(p.val[0]);  f[1] = vget_high_f32(p.val[0]);
    f[2] = vget_low_f32(p.val[1]);  f[3] = vget_high_f32(p.val[1]);
}
fast_inline void hb_pack(const float32x2_t f[4], float32x4_t* l, float32x4_t* r) {
    const float32x4x2_t o = vuzpq_f32(vcombine_f32(f[0], f[1]), vcombine_f32(f[2], f[3]));
    *l = o.val[0];
    *r = o.val[1];
}

/*
 * Apply a memoryless shaper to four consecutive stereo frames at 2x.
 * `shape` takes and returns a float32x4_t holding two oversampled {L,R}
 * frames, which is fine for anything with no memory and no channel coupling.
 *
 * The quad goes through in three passes -- every upsampling step, then the
 * shaper on all of it, then every downsampling step -- so only one chain's
 * state is live at a time and it stays in registers for the whole quad.
 */
template <typename Shaper>
fast_inline void hb2x_process4_stereo(hb2x_state_t* s, float32x4_t* l, float32x4_t* r,
                                      Shaper shape) {
    float32x2_t f[4], u[8];
    hb_unpack(*l, *r, f);
    {
        hb_regs_t<HB_A_COEFS> c;
        c.load(&s->up, kHbACoefs);
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i) c.up(f[i], &u[2 * i], &u[2 * i + 1]);
        c.store(&s->up);
    }
#pragma GCC unroll 4
    for (int i = 0; i < 4; ++i) {
        const float32x4_t v = shape(vcombine_f32(u[2 * i], u[2 * i + 1]));
        u[2 * i] = vget_low_f32(v);
        u[2 * i + 1] = vget_high_f32(v);
    }
    {
        hb_regs_t<HB_A_COEFS> c;
        c.load(&s->down, kHbACoefs);
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i) f[i] = c.down(u[2 * i], u[2 * i + 1]);
        c.store(&s->down);
    }
    hb_pack(f, l, r);
}

/* The same at 4x: stage A then stage B on the way up, B then A on the way
 * down. */
template <typename Shaper>
fast_inline void hb4x_process4_stereo(hb4x_state_t* s, float32x4_t* l, float32x4_t* r,
                                      Shaper shape) {
    float32x2_t f[4], m[8], u[16];
    hb_unpack(*l, *r, f);
    {
        hb_regs_t<HB_A_COEFS> c;
        c.load(&s->up_a, kHbACoefs);
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i) c.up(f[i], &m[2 * i], &m[2 * i + 1]);
        c.store(&s->up_a);
    }
    {
        hb_regs_t<HB_B_COEFS> c;
        c.load(&s->up_b, kHbBCoefs);
#pragma GCC unroll 8
        for (int i = 0; i < 8; ++i) c.up(m[i], &u[2 * i], &u[2 * i + 1]);
        c.store(&s->up_b);
    }
#pragma GCC unroll 8
    for (int i = 0; i < 8; ++i) {
        const float32x4_t v = shape(vcombine_f32(u[2 * i], u[2 * i + 1]));
        u[2 * i] = vget_low_f32(v);
        u[2 * i + 1] = vget_high_f32(v);
    }
    {
        hb_regs_t<HB_B_COEFS> c;
        c.load(&s->down_b, kHbBCoefs);
#pragma GCC unroll 8
        for (int i = 0; i < 8; ++i) m[i] = c.down(u[2 * i], u[2 * i + 1]);
        c.store(&s->down_b);
    }
    {
        hb_regs_t<HB_A_COEFS> c;
        c.load(&s->down_a, kHbACoefs);
#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i) f[i] = c.down(m[2 * i], m[2 * i + 1]);
        c.store(&s->down_a);
    }
    hb_pack(f, l, r);
}

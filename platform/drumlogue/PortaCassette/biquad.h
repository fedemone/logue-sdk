#pragma once
/*
 * biquad.h — biquad filters for PortaCassette
 *
 * Transposed Direct Form II, one section covering both channels.  The four
 * lanes of a NEON vector are four consecutive **time** samples, so the state has
 * to advance serially through them; an earlier float32x4_t-state design ran four
 * independent parallel histories and got the frequency response wrong.
 *
 * The state lives in d-registers with L in lane 0 and R in lane 1.  The
 * drumlogue's Cortex-A7 has a 64-bit NEON datapath, so a float32x2_t op costs
 * what a scalar VFP op costs and a float32x4_t op costs two; packing {L,R} into
 * one d-register runs both channels down a single dependency chain for the price
 * of one, roughly halving the cost of every stereo filter in the chain.
 */

#include <arm_neon.h>

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline, optimize("Ofast")))
#endif

typedef struct { float32x2_t z1, z2; } biquad_state2_t;   /* {L,R} in one d-reg */
typedef struct { float b0, b1, b2, a1, a2; } biquad_coeffs_t;

fast_inline void biquad_reset2(biquad_state2_t* s) {
    s->z1 = s->z2 = vdup_n_f32(0.0f);
}

/* One TDF2 step on a {L,R} pair held in a single d-register. */
fast_inline float32x2_t biquad_step2(float32x2_t x, const biquad_coeffs_t* c,
                                     float32x2_t* z1, float32x2_t* z2) {
    const float32x2_t y = vmla_n_f32(*z1, x, c->b0);
    *z1 = vmls_n_f32(vmla_n_f32(*z2, x, c->b1), y, c->a1);
    *z2 = vmls_n_f32(vmul_n_f32(x, c->b2), y, c->a2);
    return y;
}

/* Process 4 consecutive stereo frames in place. */
fast_inline void biquad_process4_stereo(biquad_state2_t* s,
                                        const biquad_coeffs_t* c,
                                        float32x4_t* l, float32x4_t* r) {
    float32x2_t z1 = s->z1, z2 = s->z2;

    /* {l0,l1,l2,l3},{r0,r1,r2,r3} -> {l0,r0,l1,r1},{l2,r2,l3,r3} */
    const float32x4x2_t p = vzipq_f32(*l, *r);

    const float32x2_t y0 = biquad_step2(vget_low_f32(p.val[0]),  c, &z1, &z2);
    const float32x2_t y1 = biquad_step2(vget_high_f32(p.val[0]), c, &z1, &z2);
    const float32x2_t y2 = biquad_step2(vget_low_f32(p.val[1]),  c, &z1, &z2);
    const float32x2_t y3 = biquad_step2(vget_high_f32(p.val[1]), c, &z1, &z2);

    s->z1 = z1;
    s->z2 = z2;

    const float32x4x2_t o = vuzpq_f32(vcombine_f32(y0, y1), vcombine_f32(y2, y3));
    *l = o.val[0];
    *r = o.val[1];
}

#pragma once
/*
 * File: crossover.h
 *
 * Linkwitz-Riley 24dB/oct crossover filters
 * Perfect for multiband compression - zero phase shift at crossover
 *
 * Based on SHARC Audio Elements biquad_filter.c architecture
 *
 * FIXED:
 * - Separate L and R filter states (previously shared, causing R channel = L)
 * - last_freq is per-instance (previously static, causing constant reinit)
 */

#include "constants.h"
#include <arm_neon.h>
#include <math.h>

// Crossover filter state — separate L and R biquad histories
typedef struct {
    // Left channel states (4 lanes = 4 consecutive time samples of left)
    float32x4_t l_lpf_z1,  l_lpf_z2;    // LPF stage 1, left
    float32x4_t l_lpf2_z1, l_lpf2_z2;   // LPF stage 2, left
    float32x4_t l_hpf_z1,  l_hpf_z2;    // HPF stage 1, left
    float32x4_t l_hpf2_z1, l_hpf2_z2;   // HPF stage 2, left

    // Right channel states (independent from left)
    float32x4_t r_lpf_z1,  r_lpf_z2;
    float32x4_t r_lpf2_z1, r_lpf2_z2;
    float32x4_t r_hpf_z1,  r_hpf_z2;
    float32x4_t r_hpf2_z1, r_hpf2_z2;

    float lpf_coeffs[5];   // Biquad coefficients (same for L and R)
    float hpf_coeffs[5];
    float last_freq;       // Per-instance: avoid unnecessary reinit
} crossover_t;

// Initialize crossover at given frequency
fast_inline void crossover_init(crossover_t* xover, float freq_hz, float sample_rate) {
    float w0 = 2.0f * M_PI * freq_hz / sample_rate;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float Q = 0.707f;  // Butterworth Q for Linkwitz-Riley

    float alpha = sin_w0 / (2.0f * Q);

    // Low-pass coefficients
    float b0 = (1.0f - cos_w0) / 2.0f;
    float b1 = 1.0f - cos_w0;
    float b2 = b0;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha;

    xover->lpf_coeffs[0] = b0 / a0;
    xover->lpf_coeffs[1] = b1 / a0;
    xover->lpf_coeffs[2] = b2 / a0;
    xover->lpf_coeffs[3] = a1 / a0;
    xover->lpf_coeffs[4] = a2 / a0;

    // High-pass coefficients
    b0 = (1.0f + cos_w0) / 2.0f;
    b1 = -(1.0f + cos_w0);
    b2 = b0;

    xover->hpf_coeffs[0] = b0 / a0;
    xover->hpf_coeffs[1] = b1 / a0;
    xover->hpf_coeffs[2] = b2 / a0;
    xover->hpf_coeffs[3] = a1 / a0;
    xover->hpf_coeffs[4] = a2 / a0;

    // Reset all states (L and R)
    float32x4_t zero = vdupq_n_f32(0.0f);
    xover->l_lpf_z1  = xover->l_lpf_z2  = zero;
    xover->l_lpf2_z1 = xover->l_lpf2_z2 = zero;
    xover->l_hpf_z1  = xover->l_hpf_z2  = zero;
    xover->l_hpf2_z1 = xover->l_hpf2_z2 = zero;
    xover->r_lpf_z1  = xover->r_lpf_z2  = zero;
    xover->r_lpf2_z1 = xover->r_lpf2_z2 = zero;
    xover->r_hpf_z1  = xover->r_hpf_z2  = zero;
    xover->r_hpf2_z1 = xover->r_hpf2_z2 = zero;

    xover->last_freq = freq_hz;
}

// Process one block (4 consecutive time samples) through a biquad (Direct Form I)
fast_inline float32x4_t biquad_process(float32x4_t in,
                                       float32x4_t* z1,
                                       float32x4_t* z2,
                                       const float* coeffs) {
    float32x4_t b0 = vdupq_n_f32(coeffs[0]);
    float32x4_t b1 = vdupq_n_f32(coeffs[1]);
    float32x4_t b2 = vdupq_n_f32(coeffs[2]);
    float32x4_t a1 = vdupq_n_f32(coeffs[3]);
    float32x4_t a2 = vdupq_n_f32(coeffs[4]);

    float32x4_t out = vaddq_f32(vmulq_f32(in, b0), *z1);
    *z1 = vaddq_f32(vmlaq_f32(*z2, in, b1), vmulq_f32(out, a1));
    *z2 = vaddq_f32(vmulq_f32(in, b2), vmulq_f32(out, a2));

    return out;
}

// Process stereo through crossover — L and R use fully independent filter states
fast_inline void crossover_process(crossover_t* xover,
                                   float32x4_t in_l,
                                   float32x4_t in_r,
                                   float32x4_t* low_l,
                                   float32x4_t* low_r,
                                   float32x4_t* mid_l,
                                   float32x4_t* mid_r,
                                   float32x4_t* high_l,
                                   float32x4_t* high_r,
                                   float crossover_freq,
                                   float sample_rate) {

    // Reinit only when frequency actually changes (per-instance, not static)
    if (fabsf(crossover_freq - xover->last_freq) > 1.0f) {
        crossover_init(xover, crossover_freq, sample_rate);
    }

    // Left channel — 24dB/oct LPF and HPF (two biquads in series each)
    float32x4_t lpf1_l  = biquad_process(in_l,  &xover->l_lpf_z1,  &xover->l_lpf_z2,  xover->lpf_coeffs);
    float32x4_t lpf2_l  = biquad_process(lpf1_l, &xover->l_lpf2_z1, &xover->l_lpf2_z2, xover->lpf_coeffs);
    float32x4_t hpf1_l  = biquad_process(in_l,  &xover->l_hpf_z1,  &xover->l_hpf_z2,  xover->hpf_coeffs);
    float32x4_t hpf2_l  = biquad_process(hpf1_l, &xover->l_hpf2_z1, &xover->l_hpf2_z2, xover->hpf_coeffs);

    // Right channel — completely separate states, no crosstalk from left
    float32x4_t lpf1_r  = biquad_process(in_r,  &xover->r_lpf_z1,  &xover->r_lpf_z2,  xover->lpf_coeffs);
    float32x4_t lpf2_r  = biquad_process(lpf1_r, &xover->r_lpf2_z1, &xover->r_lpf2_z2, xover->lpf_coeffs);
    float32x4_t hpf1_r  = biquad_process(in_r,  &xover->r_hpf_z1,  &xover->r_hpf_z2,  xover->hpf_coeffs);
    float32x4_t hpf2_r  = biquad_process(hpf1_r, &xover->r_hpf2_z1, &xover->r_hpf2_z2, xover->hpf_coeffs);

    *low_l  = lpf2_l;
    *low_r  = lpf2_r;
    *high_l = hpf2_l;
    *high_r = hpf2_r;
    // Mid = all-pass complement (input minus both band outputs)
    *mid_l  = vsubq_f32(vsubq_f32(in_l, lpf2_l), hpf2_l);
    *mid_r  = vsubq_f32(vsubq_f32(in_r, lpf2_r), hpf2_r);
}

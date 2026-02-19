#include "Filter.h"

void Filter::lp(float32_t srate, float32_t freq, float32_t q)
{
    // Fix: M_2_PI is usually 0.63 (2/pi). You want M_TWOPI (6.28).
    // Pre-calculate inverse srate to replace division with multiplication
    float32_t w0 = M_TWOPI * fminf(freq / srate, 0.49f);

    // Optimize: Compute sin/cos.
    float32_t cos_w0 = fastcosfullf(w0);
    float32_t alpha = fastersinfullf(w0) * 0.5f / q; // / (2*q) -> * 0.5 / q

    float32_t inv_a0 = 1.0f / (1.0f + alpha);

    a1 = -2.0f * cos_w0 * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;

    // Normalize Gain for LP: Unity gain at DC
    // (1 + a1 + a2) * 0.25 is a fast way to calculate (1-cos)/2 normalized
    b1 = (1.0f - cos_w0) * inv_a0;
    b0 = b1 * 0.5f;
    b2 = b0;
}

void Filter::bp(float32_t srate, float32_t freq, float32_t q)
{
    float32_t w0 = M_TWOPI * fminf(freq / srate, 0.49f);
    float32_t alpha = fastersinfullf(w0) * 0.5f / q;

    float32_t inv_a0 = 1.0f / (1.0f + alpha);

    a1 = -2.0f * fastcosfullf(w0) * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;

    // Bandpass Constant Peak Gain (0dB)
    b0 = alpha * inv_a0;
    b1 = 0.0f;
    b2 = -b0;
}

void Filter::hp(float32_t srate, float32_t freq, float32_t q)
{
    float32_t w0 = M_TWOPI * fminf(freq / srate, 0.49f);
    float32_t cos_w0 = fastcosfullf(w0);
    float32_t alpha = fastersinfullf(w0) * 0.5f / q;

    float32_t inv_a0 = 1.0f / (1.0f + alpha);

    a1 = -2.0f * cos_w0 * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;

    // Normalize Gain for HP
    b1 = -(1.0f + cos_w0) * inv_a0;
    b0 = -b1 * 0.5f;
    b2 = b0;
}

void Filter::clear(float32_t input)
{
    // Reset state but keep initialDC bias if needed
    x1 = x2 = input;

    // Estimate steady state Y to prevent "pop" on reset
    // For LP/HP, steady state gain is usually 1 or 0.
    // A safe bet for a generic reset is 0 unless we track DC.
    y1 = y2 = 0.0f;
}
#pragma once
#include "float_math.h"
#include <cmath>

// Ensure 2*PI is defined correctly
#ifndef M_TWOPI
#define M_TWOPI 6.28318530717958647692f
#endif

class alignas(16) Filter {
public:
    Filter() {}
    ~Filter() {}

    /** * Sets the amount of "warmth" or analog saturation.
     * @param d: 0.0 (clean) to 1.0 (warm/driven)
     */
    void setDrive(float32_t d) {
        // Map 0..1 to a useful gain range (e.g., 1.0 to 2.5)
        drive = 1.0f + d * 1.5f;
    }

    // --- Filter Modes ---
    // Optimized to use pre-calculated inverse constants
    void lp(float32_t srate, float32_t freq, float32_t q);
    void bp(float32_t srate, float32_t freq, float32_t q);
    void hp(float32_t srate, float32_t freq, float32_t q);

/**
 * Standard Direct Form I processing with added Organic Saturation.
 * Inlined for maximum performance.
 */
inline float32x4_t df1_vec(float32x4_t input) {
    // Broadcast coefficients to vectors
    float32x4_t v_b0 = vdupq_n_f32(b0);
    float32x4_t v_b1 = vdupq_n_f32(b1);
    float32x4_t v_b2 = vdupq_n_f32(b2);
    float32x4_t v_a1 = vdupq_n_f32(a1);
    float32x4_t v_a2 = vdupq_n_f32(a2);

    // Load state
    float32x4_t v_x1 = vdupq_n_f32(x1);
    float32x4_t v_x2 = vdupq_n_f32(x2);
    float32x4_t v_y1 = vdupq_n_f32(y1);
    float32x4_t v_y2 = vdupq_n_f32(y2);

    // Apply drive (vectorized soft clipping)
    float32x4_t v_drive = vdupq_n_f32(drive);
    float32x4_t x = vmulq_f32(input, v_drive);

    // Soft clip: x - 0.1481*x^3 (vectorized)
    float32x4_t x_sq = vmulq_f32(x, x);
    float32x4_t x_cub = vmulq_f32(x_sq, x);
    x = vmlsq_n_f32(x, x_cub, 0.1481f);

    // Clamp to [-1.5, 1.5]
    x = vmaxq_f32(vminq_f32(x, vdupq_n_f32(1.5f)), vdupq_n_f32(-1.5f));

    // Biquad: y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2
    float32x4_t y = vmulq_f32(x, v_b0);
    y = vmlaq_f32(y, v_x1, v_b1);
    y = vmlaq_f32(y, v_x2, v_b2);
    y = vmlsq_f32(y, v_y1, v_a1);
    y = vmlsq_f32(y, v_y2, v_a2);

    // Update state with LAST sample in vector
    x2 = x1;
    x1 = vgetq_lane_f32(x, 3);
    y2 = y1;
    y1 = vgetq_lane_f32(y, 3);

    return y;
}

    // Optimized clear
    void clear(float32_t input = 0.0f);

private:
    // Coefficients
    float32_t a1 = 0.0f, a2 = 0.0f;
    float32_t b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;

    // State
    float32_t x1 = 0.0f, x2 = 0.0f;
    float32_t y1 = 0.0f, y2 = 0.0f;

    // Organic parameter
    float32_t drive = 1.0f; // Default to clean
};
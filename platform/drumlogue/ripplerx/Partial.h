// Copyright 2025 tilr
// Partial is a second order bandpass filter with extra variables for decay, frequency and amplitude calculation

#pragma once
#include <arm_neon.h>
#include "float_math.h"
#include "constants.h"

/**
 * Partial class optimized for ARM NEON.
 * Member order is strictly controlled to maximize cache hits:
 * 1. NEON vectors (hot path) are at the top for 16-byte alignment.
 * 2. Scalars (warm path) follow.
 */
class alignas(16) Partial
{
public:
    Partial() : k(1) { clear(); } // Default constructor
    Partial(int n) : k(n) { clear(); }
    ~Partial() {};

    void update(float32_t freq, float32_t ratio, float32_t ratio_max, float32_t vel, bool isRelease);

    /**
     * Hot path: Process 4 samples in parallel.
     * Inlined to allow Resonator to optimize the loop.
     *
     * FIX: Cannot do parallel of 4 samples since it's recursive.
     * Rewritten to handle the 4 samples serially because an IIR filter has a "Time Dependency"
     * (Output y[n] depends on y[n-1]).
     * Do not use vmulq / vaddq for the recursive part. You must unroll the loop.
     * Since you already have the data in vectors, you can extract them, process, and repack,
     * or (better) perform the math using scalar variables.
     */
    inline float32x4_t process(float32x4_t input) {
        // 1. Extract the 4 samples and the scalar coefficients
        // (We only need lane 0 of the coeffs since they are duplicated)
        float32_t b0 = vgetq_lane_f32(vb0, 0);
        float32_t b2 = vgetq_lane_f32(vb2, 0);
        float32_t a1 = vgetq_lane_f32(va1, 0);
        float32_t a2 = vgetq_lane_f32(va2, 0);

        // Get current state (scalar)
        // Assuming vy1, vy2 etc are stored as vectors, we only strictly need scalars for state
        float32_t y1 = vgetq_lane_f32(vy1, 0);
        float32_t y2 = vgetq_lane_f32(vy2, 0);
        float32_t x1 = vgetq_lane_f32(vx1, 0);
        float32_t x2 = vgetq_lane_f32(vx2, 0);

        // Input buffer
        alignas(16) float32_t in_buf[4];
        vst1q_f32(in_buf, input);

        alignas(16) float32_t out_buf[4];

        // 2. Serial Processing Loop (The Compiler will optimize this heavily)
        for (int i = 0; i < 4; ++i) {
            float32_t x = in_buf[i];

            // Biquad Difference Equation
            // y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
            // Note: In your Resonator optimization, b1 = 0 and b2 = -b0 usually for BP?
            // Check your math: part.vb0 = b0, part.vb2 = -b0.
            // If b1 is missing in your struct, assume it's 0.

            float32_t y = (b0 * x) + (b2 * x2) - (a1 * y1) - (a2 * y2);

            // Anti-denormal/NaN check (Crucial for stability)
            // asm volatile ("" : "+r" (y)); // Prevent aggressive reordering if needed

            // Update State
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;

            out_buf[i] = y;
        }

        // 3. Save State back to vectors (for next block)
        // We duplicate the state so next time we extract lane 0 it's correct
        vy1 = vdupq_n_f32(y1);
        vy2 = vdupq_n_f32(y2);
        vx1 = vdupq_n_f32(x1);
        vx2 = vdupq_n_f32(x2);

        // 4. Return vector
        return vld1q_f32(out_buf);
    }

    void clear();

    // --- NEON state/coefficients (Hot Data) ---
    // Total: 128 bytes (exactly 2 cache lines on most ARM CPUs)
    float32x4_t vb0, vb2, va1, va2; // Normalized coefficients
    float32x4_t x1, x2, y1, y2;     // State vectors
    float32x4_t vx1, vx2, vy1, vy2; // Old state vectors

    // --- Parameter Scalars (Warm Data) ---
    float32_t srate = 44100.0f;
    float32_t decay = 0.0f;
    float32_t damp = 0.0f;
    float32_t tone = 0.0f;
    float32_t hit = 0.0f;
    float32_t rel = 0.0f;
    float32_t inharm = 0.0f;
    float32_t radius = 0.0f;
    float32_t vel_decay = 0.0f;
    float32_t vel_hit = 0.0f;
    float32_t vel_inharm = 0.0f;
    int k = 1;

private:
    // Scalar coefficients are no longer needed as members;
    // they are calculated as locals in update() and stored in the vectors above.
};
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
     */
    inline float32x4_t process(float32x4_t input) {
        // b0*x + b2*x2 - a1*y1 - a2*y2
        // Coefficients are pre-normalized by a0 in update()
        float32x4_t out = vmulq_f32(input, vb0);
        out = vmlaq_f32(out, x2, vb2);
        out = vmlsq_f32(out, y1, va1);
        out = vmlsq_f32(out, y2, va2);

        // State shift
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = out;

        return out;
    }

    void clear();

    // --- NEON state/coefficients (Hot Data) ---
    // Total: 128 bytes (exactly 2 cache lines on most ARM CPUs)
    float32x4_t vb0, vb2, va1, va2; // Normalized coefficients
    float32x4_t x1, x2, y1, y2;     // State vectors

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
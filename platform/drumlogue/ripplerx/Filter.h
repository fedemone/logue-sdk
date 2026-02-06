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
    inline float32_t df1(float32_t input) {
        // 1. Organic Drive: Boost input slightly into the clipper
        float32_t x = input * drive;

        // 2. Soft Clipper (Fast Tanh approximation)
        // This adds "warmth" and prevents harsh digital clipping at high resonance
        if (x < -1.5f) x = -1.0f;
        else if (x > 1.5f) x = 1.0f;
        else x = x - (0.1481f * x * x * x); // Cubic approximation: x - 4/27 * x^3

        // 3. Standard Biquad Difference Equation
        // y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
        float32_t y = (b0 * x) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2);

        // Anti-denormal (optional but good for reverb tails/silence)
        // y += 1.0e-18f;

        // Shift state
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;

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
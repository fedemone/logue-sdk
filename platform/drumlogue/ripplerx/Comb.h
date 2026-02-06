// Copyright (C) 2025 tilr
// Comb stereoizer
#pragma once
#include "float_math.h"
#include "constants.h"
#include <arm_neon.h>

class alignas(16) Comb
{
public:
    Comb() : pos(0), buf_size(0) {}
    ~Comb() = default;

    void init(float32_t srate)
    {
        pos = 0;
        // 20ms delay is standard for Haas effect stereoizing
        buf_size = (int)(0.02f * srate);
        if (buf_size > kMaxBufSize) buf_size = kMaxBufSize;

        // Clear buffer using NEON
        float32x4_t zero = vdupq_n_f32(0.0f);
        // Ensure we clear up to the 4-sample boundary
        for(int i = 0; i < kMaxBufSize; ++i) {
            buf[i] = zero;
        }

        // Stereoizer coefficients:
        // We want to add: [ +del*C, -del*C, +del*C, -del*C ]
        // This widens L vs R by pushing them in opposite phases.
        const float c = 0.165f;
        float32_t coefs[4] = {c, -c, c, -c};
        vStereoizer = vld1q_f32(coefs);
    }

    /**
     * Processes 2 stereo frames (4 samples: L1, R1, L2, R2).
     * Adds a delayed, phase-inverted version of the signal to itself.
     */
    inline float32x4_t process(float32x4_t input) {
        // 1. Fetch delayed signal
        // We read 4 samples at once. This is the "Comb" part.
        float32x4_t delayed = buf[pos];

        // 2. Store current input into buffer for future use
        // We store the raw input (or you could store the sum, but raw is cleaner for delays)
        buf[pos] = input;

        // 3. Pointer Increment (Branchless-ish)
        pos++;
        if (pos >= buf_size) pos = 0;

        // 4. Apply Stereo Width Effect
        // L_out = L_in + (Delay_L * 0.165)
        // R_out = R_in + (Delay_R * -0.165)
        // This keeps the center phantom image solid but widens the sides.
        float32x4_t effect = vmulq_f32(delayed, vStereoizer);

        // 5. Mix
        return vaddq_f32(input, effect);
    }

private:
    int pos = 0;
    int buf_size = 0;

    // Constants
    float32x4_t vStereoizer;

    // Buffer: Stores full float32x4_t vectors.
    // This allows us to delay L1, R1, L2, R2 exactly as they came in.
    // Size = kMaxBufSize vectors (not scalars)
    float32x4_t buf[kMaxBufSize];
};
#pragma once
#include "float_math.h"
#include "Filter.h"
#include <arm_neon.h>

class alignas(16) Mallet
{
public:
    Mallet() {}
    ~Mallet() {}

    /**
     * Prepares the mallet strike.
     * @param srate: Sample rate
     * @param freq: Stiffness/Tone of the mallet (Filter cutoff)
     */
    void trigger(float32_t srate, float32_t freq);

    /**
     * Generates the excitation signal (4 samples).
     * Returns filtered noise with exponential decay.
     */
    inline float32x4_t process() {
        if (elapsed <= 0) {
            return vdupq_n_f32(0.0f);
        }

        // 1. Generate Fast Noise (Linear Congruential Generator)
        // We use 4 independent seeds to generate 4 white noise samples in parallel
        // seed = seed * 1664525 + 1013904223
        uint32x4_t v_next = vmlaq_n_u32(vdupq_n_u32(1013904223), v_seed, 1664525);
        v_seed = v_next;

        // Convert UINT to Float [-1.0, 1.0]
        // Standard trick: construct IEEE float (exponent 1.0) and subtract 1.0
        // (x >> 9) | 0x3F800000 -> [1.0, 2.0)
        uint32x4_t v_bits = vorrq_u32(vshrq_n_u32(v_next, 9), vdupq_n_u32(0x3F800000));
        // [1.0, 2.0) - 1.5 -> [-0.5, 0.5). * 2.0 -> [-1.0, 1.0)
        float32x4_t noise = vsubq_f32(vreinterpretq_f32_u32(v_bits), vdupq_n_f32(1.5f));
        noise = vmulq_n_f32(noise, 2.0f);

        // 2. Apply Amplitude Envelope
        // output = noise * current_amp
        float32x4_t output = vmulq_f32(noise, v_amp_state);

        // 3. Update Envelope State (Exponential Decay)
        // state *= coef (for next block)
        v_amp_state = vmulq_f32(v_amp_state, v_decay_coef);

        // 4. Filter the noise (Stiffness)
        // Optimized: Use serial vectorized filter
        output = filter.df1_vec(output);

        // 5. Update Timer
        elapsed -= 4;

        return output;
    }

    void clear();

private:
    int elapsed = 0;

    // Envelope State
    float32x4_t v_amp_state = vdupq_n_f32(0.0f);
    float32x4_t v_decay_coef = vdupq_n_f32(0.0f);

    // RNG State
    uint32x4_t v_seed = {12345, 67890, 54321, 98765};

    // Components
    Filter filter;
};
#pragma once
#include <arm_neon.h>
#include "WFLCG.hh"
#include "float_math.h"
#include "Filter.h"
#include "Envelope.h"
#include "constants.h"

class Noise {
public:
    Noise() {}

    void init(float32_t _srate, int filterMode, float32_t _freq, float32_t _q,
              float32_t att, float32_t dec, float32_t sus, float32_t rel,
              float32_t _vel_freq, float32_t _vel_q);

    inline float32x4_t process() {
        // 1. Single check for envelope state
        if (!env.getState()) return vdupq_n_f32(0.0f);

        env.process();
        float32_t env_val = env.getEnv();

        // 2. True Mono Full-Rate Noise Generation
        // We need 2 independent samples for the 2 frames (L/R pairs) processed in this vector.
        // This ensures L=R (Mono) and full 48kHz bandwidth (no downsampling).
        float32_t r0 = rng.getFloat();
        float32_t r1 = rng.getFloat();

        float32_t s0 = (r0 - 1.5f) * 2.0f;
        float32_t s1 = (r1 - 1.5f) * 2.0f;

        // 3. Scalar Filtering & Broadcast
        float32_t y0 = filter_active ? filter.df1(s0) : s0;
        float32_t y1 = filter_active ? filter.df1(s1) : s1;

        // Construct [y0, y0, y1, y1] for [L0, R0, L1, R1]
        float32x2_t frame0 = vdup_n_f32(y0);
        float32x2_t frame1 = vdup_n_f32(y1);
        float32x4_t out_sample = vcombine_f32(frame0, frame1);

        // 4. Check if envelope just finished
        if (!env.getState()) {
            filter.clear(0.0f);
        }

        return vmulq_n_f32(out_sample, env_val);
    }

    void initFilter();

    void attack(float32_t velocity);

    void release();

    void clear();

    // Public members moved to end for better padding/alignment
    float32_t vel_freq = 0.0f;
    float32_t vel_q = 0.0f;
    float32_t srate = 44100.0f;
    float32_t vel = 0.0f;
    float32_t q = 0.707f;
    bool filter_active = false;

private:
    Filter filter{};
    Envelope env{};
    WFLCG rng{};
    int fmode = 0;
    float32_t freq = 0.0f;
};
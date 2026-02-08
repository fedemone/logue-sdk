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

    inline float32_t process() {
        // 1. Single check for envelope state
        bool active = env.getState();
        if (!active) return 0.0f;

        env.process();
        float32_t env_val = env.getEnv();

        // 2. Faster Noise Generation
        // Direct conversion of bits to float can be faster than * 2.0 - 3.0
        // But assuming getFloat() is optimized, we keep the math lean.
        float32_t raw_sample = rng.getFloat();
        float32_t sample = raw_sample * 2.0f - 3.0f;

        // 3. Conditional execution (The compiler will likely use 'vsel' or 'vbit' on ARM)
        // This avoids the pipeline flush of a branch
        float32_t filtered = filter.df1(sample);
        float32_t out_sample = filter_active ? filtered : sample;


        // 4. Check if envelope just finished
        if (!env.getState()) {
            filter.clear(0.0f);
        }

        return out_sample * env_val;
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
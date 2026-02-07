#pragma once
#include <stdint.h>
#include <math.h>
#include "float_math.h"

// States
enum EnvState {
    Off = 0,
    Attack,
    Decay,
    Sustain,
    Release
};

class alignas(16) Envelope {
public:
    Envelope() {}
    ~Envelope() {}

    void init(float32_t srate, float32_t a, float32_t d, float32_t s, float32_t r,
              float32_t tensionA, float32_t tensionD, float32_t tensionR);

    // Inline control methods for speed
    inline void attack(float32_t _scale = 1.0f) {
        scale = _scale;
        recalcCoefs();
        state = Attack;
    }

    inline void release() {
        // Calculate release curve starting from current envelope value to prevent clicks
        // If current env is lower than sustain, use current; otherwise use sustain/max
        float32_t start_level = env;

        calcCoefs(start_level, 0.0f, start_level, rel, tr, -1.0f, rb, rc);
        state = Release;
    }

    inline void reset() {
        state = Off;
        env = 0.0f;
    }

    // Main process function - inlined for performance
    inline int process() {
        switch (state) {
            case Attack:
                env = ab + env * ac;
                if (env >= scale) {
                    env = scale;
                    state = Decay;
                }
                break;

            case Decay:
                env = db + env * dc;
                if (env <= sus_level) {
                    env = sus_level;
                    state = Sustain;
                }
                break;

            case Sustain:
                return state; // Value is constant, do nothing

            case Release:
                env = rb + env * rc;
                // Clamp to 0 to stop
                if (env <= 1e-5f) {
                    env = 0.0f;
                    state = Off;
                }
                break;

            case Off:
                return 0;
        }
        return state;
    }

    inline float32_t getEnv() const { return env; }
    inline int getState() const { return state; }

private:
    void calcCoefs(float32_t targetB1, float32_t targetB2, float32_t targetC, float32_t rate,
                   float32_t tension, float32_t mult, float32_t& result_b, float32_t& result_c);

    void recalcCoefs();

    // Inline helper to cubic curve mapping
    inline float32_t normalizeTension(float32_t t) {
        t += 1.0f;
        return t == 1.0f ? 100.0f : (t > 1.0f ? 3.001f - t : 0.001f + t);
    }

    // Parameters (Time in samples)
    float32_t att = 0.0f;
    float32_t dec = 0.0f;
    float32_t sus = 0.0f;     // Sustain Gain (0.0 - 1.0)
    float32_t sus_level = 0.0f; // Pre-calculated sustain level (scale * sus)
    float32_t rel = 0.0f;

    // Tensions
    float32_t ta = 0.0f;
    float32_t td = 0.0f;
    float32_t tr = 0.0f;

    // Coefficients
    float32_t ab = 0.0f, ac = 0.0f; // Attack coeffs
    float32_t db = 0.0f, dc = 0.0f; // Decay coeffs
    float32_t rb = 0.0f, rc = 0.0f; // Release coeffs

    // Runtime state
    float32_t env = 0.0f;
    float32_t scale = 1.0f;
    int state = Off;
};
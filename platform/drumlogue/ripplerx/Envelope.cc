#include "Envelope.h"
#include <algorithm> // for std::max, std::min

void Envelope::init(float32_t srate, float32_t a, float32_t d, float32_t s, float32_t r,
                    float32_t tensionA, float32_t tensionD, float32_t tensionR) {
    // Convert ms to samples (rate * 0.001)
    float32_t ms_to_samples = srate * 0.001f;

    att = fmaxf(a, 1.0f) * ms_to_samples;
    dec = fmaxf(d, 1.0f) * ms_to_samples;
    rel = fmaxf(r, 1.0f) * ms_to_samples;

    // Sustain is usually dB or linear. Original code treated 's' as dB if < 0?
    // Assuming 's' is in dB range [-inf, 0], mapping it to linear [0, 1]
    // fasterpowf(10, s/20) is standard dB to linear.
    sus = fasterpowf(10.0f, fminf(s, 0.0f) * 0.05f); // * 0.05 is / 20

    ta = normalizeTension(tensionA);
    // Invert tension logic for decay/release as they go downwards
    td = normalizeTension(-1.0f * tensionD);
    tr = normalizeTension(-1.0f * tensionR);
}

void Envelope::calcCoefs(float32_t targetB1, float32_t targetB2, float32_t targetC, float32_t rate,
    float32_t tension, float32_t mult, float32_t& result_b, float32_t& result_c)
{
    // Prevent division by zero
    if (rate < 1.0f) rate = 1.0f;
    float32_t inv_rate = 1.0f / rate;

    float32_t t;

    // OPTIMIZATION: Replace pow(x, 3.0) with x*x*x
    if (tension > 1.0f) {  // Slow-start shape (Logarithmic-like)
        float32_t base = tension - 1.0f;
        t = base * base * base;

        // Curve Logic
        float32_t val = (targetC + t) / t;
        result_c = e_expf(fasterlogf(val) * inv_rate);
        result_b = (targetB1 - mult * t) * (1.0f - result_c);

    } else {              // Fast-start shape (Exponential-like)
        t = tension * tension * tension;

        // Curve Logic
        float32_t val = (targetC + t) / t;
        // Optimization: -log(x) is log(1/x), but here just negate result
        result_c = e_expf(-fasterlogf(val) * inv_rate);
        result_b = (targetB2 + mult * t) * (1.0f - result_c);
    }
}

void Envelope::recalcCoefs()
{
    // Pre-calculate the absolute sustain level to avoid multiply in process()
    sus_level = sus * scale;

    // Calculate attack coefficients
    // Target: goes from 0 to scale
    calcCoefs(0.0f, scale, scale, att, ta, 1.0f, ab, ac);

    // Calculate decay coefficients
    // Target: goes from scale down to sustain level
    // Note: (1.0-sus)*scale simplifies to scale - sus_level
    calcCoefs(1.0f, sus_level, scale - sus_level, dec, td, -1.0f, db, dc);
}
#pragma once

/**
 * @file svf_filter.h
 * @brief Chamberlin state-variable filter with low/band/high morph.
 *
 * Direct port of copych/ESP32-S3_FM_Drum_Synth FMDrums/svf_morph.h (MIT).
 * Changes: Arduino/IRAM_ATTR removed, std math, 48 kHz sample rate, the
 * fast_sin() LUT replaced by float_math's fastersinfullf().
 */

#include <cmath>
#include "constants.h"
#include "float_math.h"

static inline float svf_clamp(float in, float lo, float hi) {
    if (in > hi) return hi;
    if (in < lo) return lo;
    return in;
}

class SvfFilter {
public:
    void init(float sampleRate) {
        sr_    = sampleRate;
        fc_    = 16000.0f;
        res_   = 0.0f;
        drive_ = 0.0f;
        preDrive_ = 0.5f;
        fcMax_ = sr_ / 2.75625f;
        morph_ = 0.33f;
        reset();
        updateParams();
    }

    void reset() { low_ = band_ = high_ = 0.0f; }

    void setFreqHz(float f)    { fc_  = svf_clamp(f, 1.0f, fcMax_); updateParams(); }
    void setResonance(float r) { res_ = svf_clamp(r, 0.0f, 1.0f);   updateParams(); }
    void setMorph(float m)     { morph_ = svf_clamp(m, 0.0f, 1.0f); }

    float getFreqHz()    const { return fc_; }
    float getResonance() const { return res_; }
    float getMorph()     const { return morph_; }

    fast_inline void process(float in) {
        float notch = in - damp_ * band_;
        float hp = notch - low_;
        float bp = svf_clamp(band_ + freq_ * hp, -bandLimit_, bandLimit_);
        bp -= drive_ * bp * fabsf(bp);          // nonlinear drive
        float lp = low_ + freq_ * bp;
        low_  = lp;
        band_ = bp;
        high_ = hp;
        outLow_ = lp; outBand_ = bp; outHigh_ = hp;
    }

    fast_inline float processMorph(float in) {
        process(in);
        float mixed;
        if (morph_ <= 0.5f) {
            float t = morph_ * 2.0f;
            mixed = outLow_ * (1.0f - t) + outBand_ * t;
        } else {
            float t = (morph_ - 0.5f) * 2.0f;
            mixed = outBand_ * (1.0f - t) + outHigh_ * t;
        }
        return mixed * 1.2f;                     // empirical output gain
    }

private:
    void updateParams() {
        float omega = PI_F * fc_ * INV_SAMPLE_RATE;
        freq_ = 2.0f * fastersinfullf(omega);
        damp_ = svf_clamp(2.0f * (1.0f - powf(res_, 0.25f)),
                          0.0f,
                          svf_clamp(2.0f / freq_ - 0.5f * freq_, 0.0f, 2.0f));
        bandLimit_ = 1.5f + 2.0f * res_;
        drive_ = preDrive_ * preDrive_ * (1.0f + 2.0f * res_) * 0.3f;
    }

    float sr_ = 48000.0f, fc_ = 16000.0f, res_ = 0.0f;
    float freq_ = 0.25f, damp_ = 0.0f;
    float preDrive_ = 0.5f, drive_ = 0.0f;
    float fcMax_ = 16000.0f, morph_ = 0.33f, bandLimit_ = 1.5f;
    float low_ = 0.0f, band_ = 0.0f, high_ = 0.0f;
    float outLow_ = 0.0f, outBand_ = 0.0f, outHigh_ = 0.0f;
};

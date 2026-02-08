// Copyright 2025 tilr
// Port of FairlyChildish limiter for Reaper

// Copyright 2006, Thomas Scott Stillwell
// All rights reserved.
//
//Redistribution and use in source and binary forms, with or without modification, are permitted
//provided that the following conditions are met:
//
//Redistributions of source code must retain the above copyright notice, this list of conditions
//and the following disclaimer.
//
//Redistributions in binary form must reproduce the above copyright notice, this list of conditions
//and the following disclaimer in the documentation and/or other materials provided with the distribution.
//
//The name of Thomas Scott Stillwell may not be used to endorse or
//promote products derived from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY e_expffRESS OR
//IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
//FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
//BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
//THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include "float_math.h"
#include <arm_neon.h>
#include <cstdio>


class alignas(16) Limiter
{
public:
    Limiter() {}
    ~Limiter() {}

    void init(float32_t srate, float32_t _thresh = 0.0f, float32_t _bias = 70.0f,
              float32_t rms_win = 100.0f, float32_t makeup = 0.0f)
    {
        // Pre-calculate constants to avoid math in the hot path
        threshv = e_expff(_thresh * M_DBTOLOG);

        // Logarithmic threshold for fast comparison
        // We compare log values directly to avoid exp/log calls in the loop
        log_thresh = _thresh;

        bias = 80.0f * _bias / 100.0f;
        makeupv = e_expff(makeup * M_DBTOLOG);

        // RMS Window: Convert to coefficient
        // 1.0 - exp(-1 / (time * srate))
        float32_t rms_t = rms_win * 0.000001f; // us to seconds
        rmscoef = e_expff(-1.0f / (rms_t * srate));

        // Attack/Release coefficients
        atcoef = e_expff(-1.0f / (0.0002f * srate));
        relcoef = e_expff(-1.0f / (0.3f * srate));

        // Initialize state vectors
        vRunAve = vdupq_n_f32(0.0f);
        vRunDb = vdupq_n_f32(0.0f);

        // Pre-load constants into NEON vectors
        vRmsCoef = vdupq_n_f32(rmscoef);
        vOneMinusRms = vdupq_n_f32(1.0f - rmscoef);
        vAttCoef = vdupq_n_f32(atcoef);
        vRelCoef = vdupq_n_f32(relcoef);
        vThreshV = vdupq_n_f32(threshv);
        vBias = vdupq_n_f32(bias);
        vMakeup = vdupq_n_f32(makeupv);
        vRatioConst = vdupq_n_f32(19.0f); // Ratio 20.0 - 1.0
        vInvBias = vdupq_n_f32(1.0f / bias); // Pre-calculate division

        // Constants for log/exp math
        vLog2e = vdupq_n_f32(1.44269504f);
        vDbToLog = vdupq_n_f32(0.11512925f); // M_DBTOLOG
    }

    // Process 4 samples (stereo pair x 2 frames, or 4 mono)
    inline float32x4_t process(float32x4_t input)
    {
        // printf("[DEBUG] Limiter input: %.4f\n", vgetq_lane_f32(input, 0));
        // CRITICAL: Check for NaN/Inf in input to prevent crash propagation.
        // If input contains invalid values, return zeros to protect hardware audio.
        // Check all 4 lanes for validity
        float32_t l0 = vgetq_lane_f32(input, 0);
        float32_t l1 = vgetq_lane_f32(input, 1);
        float32_t l2 = vgetq_lane_f32(input, 2);
        float32_t l3 = vgetq_lane_f32(input, 3);

        if (!isfinite(l0) || !isfinite(l1) || !isfinite(l2) || !isfinite(l3) ||
            fabs(l0) > 1e10f || fabs(l1) > 1e10f || fabs(l2) > 1e10f || fabs(l3) > 1e10f) {
             return vdupq_n_f32(0.0f);
        }

        // 1. RMS Detection (Square Law)
        float32x4_t sq = vmulq_f32(input, input);

        // 2. Running Average (One-pole filter)
        // runave = sq + rmscoef * (runave - sq)
        // Optimized: runave = runave * rmscoef + sq * (1 - rmscoef)
        vRunAve = vmlaq_f32(vmulq_f32(vRunAve, vRmsCoef), sq, vOneMinusRms);

        // 3. Convert RMS to Decibels (Approximate)
        // We use the bit-trick log2 approximation directly on vectors
        // det = sqrt(runave) -> log(sqrt(x)) = 0.5 * log(x)
        float32x4_t det_log = v_fast_log2(vRunAve); // Returns log2(runave)

        // Convert log2 to dB: dB = 10 * log10(x)
        // We know: log2(x) * 3.0103 = dB
        // Since we missed the sqrt, we multiply by 0.5 * 3.0103 ~= 1.505
        float32x4_t db_val = vmulq_n_f32(det_log, 1.50515f);

        // 4. Threshold Check
        // overdb = max(0, db_val - log_thresh)
        float32x4_t overdb = vmaxq_f32(vdupq_n_f32(0.0f), vsubq_f32(db_val, vdupq_n_f32(log_thresh)));

        // 5. Attack / Release Smoothing (Branchless)
        // mask = overdb > rundb
        uint32x4_t mask = vcgtq_f32(overdb, vRunDb);

        // coef = mask ? atcoef : relcoef
        float32x4_t coef = vbslq_f32(mask, vAttCoef, vRelCoef);

        // rundb = overdb + coef * (rundb - overdb)
        float32x4_t diff = vsubq_f32(vRunDb, overdb);
        vRunDb = vaddq_f32(overdb, vmulq_f32(diff, coef));

        // 6. Gain Reduction Calculation
        // Ratio logic: cratio = 1.0 + (ratio-1) * sqrt(rundb / bias)
        float32x4_t ratio_term = vmulq_f32(vRunDb, vInvBias);
        //adds an epsilon to the RMS calculation to Prevent rsqrt(0)
        ratio_term = vaddq_f32(ratio_term, vdupq_n_f32(1.0e-9f));

        // rsqrte is "Reciprocal Square Root Estimate" - very fast
        // sqrt(x) = x * rsqrt(x) roughly
        // float32x4_t sqrt_term = vmulq_f32(ratio_term, vrsqrteq_f32(ratio_term));
        // Fix: Prevent 0 * Inf = NaN when ratio_term is 0. Use epsilon for rsqrt input.
        float32x4_t ratio_safe = vmaxq_f32(ratio_term, vdupq_n_f32(1.0e-9f));
        float32x4_t sqrt_term = vmulq_f32(ratio_term, vrsqrteq_f32(ratio_safe));


        float32x4_t cratio = vmlaq_f32(vdupq_n_f32(1.0f), vRatioConst, sqrt_term);

        // gr = -rundb * (cratio - 1) / cratio
        // Simplify: (cratio - 1) / cratio = 1 - (1/cratio)
        float32x4_t inv_cratio = vrecpeq_f32(cratio); // Fast reciprocal estimate
        float32x4_t gain_factor = vsubq_f32(vdupq_n_f32(1.0f), inv_cratio);

        float32x4_t gr_db = vmulq_f32(vRunDb, gain_factor); // Positive dB reduction

        // 7. Convert dB back to Linear Gain
        // gain = exp(-gr_db * 0.1151) * makeup
        // exp(x) = 2^(x * log2(e))
        float32x4_t db_scaled = vmulq_f32(gr_db, vDbToLog); // * 0.115 * log2e
        float32x4_t gain_linear = v_fast_exp2(vnegq_f32(db_scaled));

        // Apply Gain
        return vmulq_f32(input, vmulq_f32(gain_linear, vMakeup));
    }

private:
    // Helper: Fast Vector Log2 (Bit manipulation)
    inline float32x4_t v_fast_log2(float32x4_t x) {
        // Input x must be > 0. IEEE-754 hack.
        // int i = as_int(x);
        // return (i - 127 << 23) * (1.0 / 2^23);
        int32x4_t i = vreinterpretq_s32_f32(x);
        i = vsubq_s32(i, vdupq_n_s32(1065353216)); // Sub 127<<23
        float32x4_t f = vcvtq_f32_s32(i);
        return vmulq_n_f32(f, 1.1920928955e-7f); // Mul by 1/2^23
    }

    // Helper: Fast Vector Exp2 (Schraudolph Bit manipulation)
    inline float32x4_t v_fast_exp2(float32x4_t x) {
        // result = 2^x -> i = (int)(x * 8388608.0f) + 1065353216
        int32x4_t i = vcvtq_s32_f32(vmlaq_n_f32(vdupq_n_f32(1065353216.0f), x, 8388608.0f));
        return vreinterpretq_f32_s32(i);
    }

    // Parameters in vectors for SIMD
    float32x4_t vRunAve, vRunDb;
    float32x4_t vRmsCoef, vOneMinusRms;
    float32x4_t vAttCoef, vRelCoef;
    float32x4_t vThreshV, vBias, vMakeup;
    float32x4_t vRatioConst, vInvBias;
    float32x4_t vLog2e, vDbToLog;

    float32_t log_thresh;
    float32_t threshv, bias, makeupv;
    float32_t rmscoef, atcoef, relcoef;
};
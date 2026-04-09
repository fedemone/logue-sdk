#pragma once
#include <cmath>
#include "float_math.h"

constexpr float q_limit = 0.05f;
constexpr float kStabilitySafetyMargin = 0.98f;
constexpr float kWaveFoldingThreshold = 1.2f;
constexpr float kWaveFoldingMarker = 2 * kWaveFoldingThreshold;

enum filter_mode {
    mode_low = 0,
    mode_band,
    mode_high,
    mode_notch,
    mode_last   // marker
};

// ==========================================================
// Fast Polynomial Tanh Approximation
// ==========================================================
inline float fast_tanh(float x) {
    // Clamp first so the cubic stays in its valid range (|x| <= sqrt(3) ≈ 1.73).
    // BUG-FIX: x^2 must be computed from the clamped value. Using the raw x makes
    // the polynomial return large negative outputs for |x| > 1.73, which flips the
    // sign of filter integrator increments and causes NaN within a few samples.
    float cx = fmaxf(-1.0f, fminf(1.0f, x));
    // Multiply by 1.5f so the output scales to a full [-1.0, 1.0] range
    // instead of stopping at 0.666. This gives you maximum audio headroom.
    return cx * (1.0f - cx * cx * 0.33333f) * 1.5f;
}


// ==========================================================
// FILTER 1: THE SHERMAN WAVEFOLDER
// ==========================================================
struct MorphingFilter {
    filter_mode mode = mode_low; // Lowpass, Bandpass, Highpass, Notch

    // Morphing Parameters
    float drive = 0.0f;           // 0.0 (Clean) to 5.0 (Screaming)
    float sherman_asym = 0.0f;    // 0.0 (Symmetrical) to 1.0 (Asymmetrical)
    float lfo_res_mod = 0.0f;     // How much LFO3 rips the resonance apart

    // Euler SVF State
    float low = 0.0f;
    float band = 0.0f;
    float f = 0.0f;
    float q = 0.0f;

    inline void set_coeffs(float hz, float reso_q, float sample_rate) {
        // 1. Bottom Clamp: Prevent negative hz from audio-rate FM crashes
        hz = fmaxf(10.0f, hz);

        // 2. Top Clamp: Just below Nyquist to keep f < 2 and the SVF stable
        hz = fminf(hz, sample_rate * 0.49f);

        // Calculate frequency coefficient
        // (Using standard Chamberlin approx: 2 * sin(pi * f / fs))
        f = 2.0f * fastersinfullf(M_PI * hz / sample_rate);

        // Inverse Q for damping
        q = 1.0f / reso_q;

        // 3. Euler-forward stability guard: f^2 + 2*f*q < 4.
        // At near-Nyquist with any resonance, f alone can approach 2.0 — the linear
        // (no-drive) SVF path has no integrator saturation to limit feedback, so it
        // explodes immediately. Clamp f to the max safe value for the current q.
        float f_max = fasterSqrt_15bits(q * q + 4.0f) - q;
        if (f > f_max * kStabilitySafetyMargin) f = f_max * kStabilitySafetyMargin;
    }

    inline float process(float in, float lfo_val) {
        float drive_sig = in * (1.0f + drive);

        // Dynamic Resonance (Damping decreases as LFO pushes)
        float current_q = q * (1.0f - (lfo_val * lfo_res_mod * 0.5f));
        current_q = fmaxf(q_limit, current_q); // Prevent total self-oscillation collapse

        // STAGE 3: Sherman Asymmetrical Wavefolding (Pre-Filter)
        if (sherman_asym > 0.0f) {
            drive_sig += sherman_asym;
            if (drive_sig > kWaveFoldingMarker) drive_sig = kWaveFoldingMarker - drive_sig;
            else if (drive_sig < -kWaveFoldingMarker) drive_sig = -kWaveFoldingMarker - drive_sig;
        }

        // --- Euler-Forward SVF Core ---

        // Highpass calculation
        float high = drive_sig - low - current_q * band;

        // STAGE 2: Moog Saturation (Inside the Integrators)
        if (drive > 0.0f && sherman_asym <= 0.0f) {
            band += f * fast_tanh(high);
            low  += f * fast_tanh(band);
        } else {
            // STAGE 1: Clean
            band += f * high;
            low  += f * band;
        }

        // Notch calculation
        float notch = high + low;

        // Output Routing
        if (mode == mode_high) return high;
        if (mode == mode_band) return band;
        if (mode == mode_notch) return notch;
        return low; // mode 0
    }
};

// ==========================================================
// FILTER 2: THE POLIVOKS EMULATION
// ==========================================================
class PolivoksFilter {
public:
    filter_mode mode = mode_low; // Lowpass, Bandpass, Highpass, Notch
    float drive = 0.0f;

    // Internal State Variables
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    float f = 0.0f;
    float q = 0.0f;

    inline void set_coeffs(float hz, float reso_q, float sample_rate) {
        // 1. Mandatory Bottom Clamp: Protects against negative frequencies from audio-rate FM
        hz = fmaxf(10.0f, hz);

        // 2. Mandatory Top Clamp: Maintains Euler-forward stability bounds
        hz = fminf(hz, sample_rate * 0.45f);

        // Calculate frequency coefficient
        f = 2.0f * fastersinfullf(M_PI * hz / sample_rate);

        // Invert and scale Q to replicate the aggressive Polivoks resonance slope
        q = 1.0f / fmaxf(q_limit, reso_q);

        // 3. Euler-forward stability guard (same condition as MorphingFilter)
        float f_max = fasterSqrt_15bits(q * q + 4.0f) - q;
        if (f > f_max * kStabilitySafetyMargin) f = f_max * kStabilitySafetyMargin;
    }

    inline float process(float in) {
        float drive_sig = in * (1.0f + drive);

        float res_fb = q * ic1eq;
        float high = drive_sig - ic2eq - fast_tanh(res_fb);

        ic1eq += f * high;
        ic2eq += f * fast_tanh(ic1eq);

        // Derive Notch by summing Highpass and Lowpass
        float notch = high + ic2eq;

        if (mode == mode_band) return ic1eq; // Bandpass
        if (mode == mode_high) return high;  // Highpass
        if (mode == mode_notch) return notch; // Notch
        return ic2eq;                // Lowpass
    }
};

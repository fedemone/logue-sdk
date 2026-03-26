#pragma once
#include <cmath>
#include "float_math.h"

constexpr float q_limit = 0.05f;

// ==========================================================
// Fast Polynomial Tanh Approximation
// ==========================================================
inline float fast_tanh(float x) {
    float x2 = x * x;
    float clamped_x = fmaxf(-1.5f, fminf(1.5f, x));
    return clamped_x - (clamped_x * x2) * 0.33333f;
}


// ==========================================================
// FILTER 1: THE SHERMAN WAVEFOLDER
// ==========================================================
struct MorphingFilter {
    int mode = 0; // 0 = LP, 1 = BP, 2 = HP

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
        f = 2.0f * sinf(M_PI * hz / sample_rate);

        // Inverse Q for damping
        q = 1.0f / reso_q;
    }

    inline float process(float in, float lfo_val) {
        float drive_sig = in * (1.0f + drive);

        // Dynamic Resonance (Damping decreases as LFO pushes)
        float current_q = q * (1.0f - (lfo_val * lfo_res_mod * 0.5f));
        current_q = fmaxf(0.05f, current_q); // Prevent total self-oscillation collapse

        // STAGE 3: Sherman Asymmetrical Wavefolding (Pre-Filter)
        if (sherman_asym > 0.0f) {
            drive_sig += sherman_asym;
            if (drive_sig > 1.2f) drive_sig = 2.4f - drive_sig;
            else if (drive_sig < -1.2f) drive_sig = -2.4f - drive_sig;
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
        if (mode == 1) return high;
        if (mode == 2) return band;
        if (mode == 3) return notch;
        return low; // mode 0
    }
};

// ==========================================================
// FILTER 2: THE POLIVOKS EMULATION
// ==========================================================
class PolivoksFilter {
public:
    int mode = 0; // 0=Lowpass, 1=Bandpass, 2=Highpass
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
        f = 2.0f * sinf(M_PI * hz / sample_rate);

        // Invert and scale Q to replicate the aggressive Polivoks resonance slope
        q = 1.0f / fmaxf(0.05f, reso_q);
    }

    inline float process(float in) {
        float drive_sig = in * (1.0f + drive);

        float res_fb = q * ic1eq;
        float high = drive_sig - ic2eq - fast_tanh(res_fb);

        ic1eq += f * high;
        ic2eq += f * fast_tanh(ic1eq);

        // Derive Notch by summing Highpass and Lowpass
        float notch = high + ic2eq;

        if (mode == 1) return ic1eq; // Bandpass
        if (mode == 2) return high;  // Highpass
        if (mode == 3) return notch; // Notch
        return ic2eq;                // Lowpass
    }
};

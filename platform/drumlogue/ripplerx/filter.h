#pragma once
#include <cmath>
#include "float_math.h" // For our fast math approximations

/**
 * Fast Chamberlin State Variable Filter (SVF)
 * Provides 12dB/octave Lowpass, Highpass, and Bandpass outputs.
 */
struct FastSVF {
    // Filter State (Memory)
    float lp = 0.0f;
    float bp = 0.0f;
    float hp = 0.0f;

    // Fast-math coefficients (Calculated in UI thread)
    float f = 0.0f;
    float q = 0.0f;

    // CRITICAL FIX: Mode 2 = Highpass. This prevents the 10Hz parameter from muting the synth!
    int mode = 2;

    // Called by the UI Thread (setParameter) to keep the Audio Thread fast
    inline void set_coeffs(float cutoff_hz, float resonance, float srate) {
        // Clamp cutoff to prevent SVF explosion near Nyquist (srate / 2)
        float safe_cutoff = fminf(cutoff_hz, srate * 0.45f);

        // Chamberlin tuning formula: f = 2 * sin(pi * cutoff / srate)
        // NOTE: fastercosfullf takes RADIANS (not a [0,1]-normalised fraction).
        // The previous formula 2*fastercosfullf(0.25 - x) computed 2*cos(0.25_rad),
        // which gives f ≈ 1.91–1.97 for all audio frequencies — always near-Nyquist
        // and catastrophically unstable. Use sinf() directly: it is only called from
        // the UI thread (setParameter), so the extra precision cost is negligible.
        f = 2.0f * sinf(M_PI * safe_cutoff / srate);

        // Resonance (Q factor). Lower value = higher resonance peak.
        // Clamp to [0.5, 10.0]: 0.5 allows the UI minimum of 0.707 (Butterworth flat
        // response) to pass through correctly. Old clamp of 1.0 silently prevented
        // Butterworth Q and made the minimum labelled value have no effect.
        float safe_res = fmaxf(0.5f, fminf(resonance, 10.0f));
        q = 1.0f / safe_res;

        // Chamberlin SVF stability: eigenvalues exit the unit circle when
        // f >= sqrt(4 + q²) - q, causing exponential divergence to Inf.
        // On x86 strict IEEE 754: Inf * 0 = NaN, contaminating the waveguide.
        // On ARM -ffast-math: 0 * Inf = 0, so hardware is spared.
        // Init preset: NzFltFrq=1200→12000Hz→f=1.414, Q=0.707→q=√2,
        // stability requires f < √6-√2 ≈ 1.035 but f=1.414 → UNSTABLE.
        float f_stable_max = sqrtf(4.0f + q * q) - q;
        f = fminf(f, f_stable_max * 0.999f);
    }

    // Called by the Audio Thread (processBlock)
    inline float process(float input) {
        // The core SVF math (requires zero division or trig!)
        hp = input - lp - (q * bp);
        bp += f * hp;
        lp += f * bp;

        if (mode == 0) return lp;
        if (mode == 1) return bp;
        return hp;
    }
};
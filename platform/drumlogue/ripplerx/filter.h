#pragma once
#include <cmath>
#include "float_math.h" // For our fast math approximations

/**
 * Fast TPT (topology-preserving transform / Zavalishin) State Variable Filter.
 * Provides 12dB/octave Lowpass, Highpass, and Bandpass outputs.
 *
 * Replaces the previous Chamberlin SVF.  The Chamberlin recursion is only
 * accurate below ~fs/6 and its stability limit f < √(4+q²)−q caps the usable
 * cutoff at ~8.2 kHz (Q=0.707) at 48 kHz: every cutoff request above that was
 * clamped onto the stability boundary, freezing the filter into a high-Q
 * ~8 kHz resonator whose output got LOUDER as the cutoff rose — the
 * "cutoff works in reverse" hardware report.  The TPT structure is the
 * bilinear-transform discretisation of the analog SVF: unconditionally
 * stable and frequency-accurate all the way to Nyquist.
 * Reference: V. Zavalishin, "The Art of VA Filter Design", ch. 4.
 */
struct FastSVF {
    // Filter State (Memory).
    // lp and bp double as the two TPT integrator states (s2 and s1): external
    // code that zeroes lp/bp/hp to clear filter memory keeps working unchanged.
    // Use DMIs (no user constructor) so that globals containing FastSVF get
    // constant-initialized into .data rather than .bss.  The drumlogue unit
    // loader has a very small BSS budget; .data is fine because non-zero
    // members (mode=2) prevent the all-zeros BSS optimisation.
    float lp   = 0.0f;   // integrator state s2 (≈ lowpass memory)
    float bp   = 0.0f;   // integrator state s1 (≈ bandpass memory)
    float hp   = 0.0f;   // last highpass output (kept for external zeroing)

    // TPT coefficients (Calculated in UI thread)
    float a1   = 0.0f;
    float a2   = 0.0f;
    float a3   = 0.0f;
    float q    = 0.0f;   // damping k = 1/Q

    // CRITICAL FIX: Mode 2 = Highpass. This prevents the 10Hz parameter from muting the synth!
    int mode   = 2; // 0=LP, 1=BP, 2=HP  (non-zero → forces .data placement)

    // Called by the UI Thread (setParameter) to keep the Audio Thread fast
    inline void set_coeffs(float cutoff_hz, float resonance, float srate) {
        // Clamp cutoff below Nyquist: tan(π·fc/fs) → ∞ at fs/2.
        float safe_cutoff = fmaxf(1.0f, fminf(cutoff_hz, srate * 0.47f));

        // Bilinear prewarp: g = tan(π fc / fs).  UI thread only, so the
        // transcendental cost is irrelevant.
        float g = tanf((float)M_PI * safe_cutoff / srate);

        // Resonance (Q factor). q here is the DAMPING k = 1/Q.
        // Clamp to [0.5, 10.0]: 0.5 allows the UI minimum of 0.707 (Butterworth flat
        // response) to pass through correctly. Old clamp of 1.0 silently prevented
        // Butterworth Q and made the minimum labelled value have no effect.
        float safe_res = fmaxf(0.5f, fminf(resonance, 10.0f));
        q = 1.0f / safe_res;

        // TPT one-sample-feedback coefficients.  Unconditionally stable for any
        // g > 0 — no stability clamp needed (the Chamberlin clamp was the root
        // cause of the reversed-cutoff behaviour above ~8 kHz).
        a1 = 1.0f / (1.0f + g * (g + q));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Called by the Audio Thread (processBlock)
    inline float process(float input) {
        // Zavalishin TPT SVF core: 2 multiplies-per-integrator, no trig.
        float v3 = input - lp;            // lp holds s2
        float v1 = a1 * bp + a2 * v3;     // bandpass output (bp holds s1)
        float v2 = lp + a2 * bp + a3 * v3; // lowpass output
        bp = 2.0f * v1 - bp;              // s1 update
        lp = 2.0f * v2 - lp;              // s2 update
        hp = input - q * v1 - v2;

        if (mode == 0) return v2;
        if (mode == 1) return v1;
        return hp;
    }
};

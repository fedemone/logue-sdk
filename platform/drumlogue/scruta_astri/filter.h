#pragma once
#include <cmath>
#include "float_math.h"

constexpr float q_limit = 0.05f;
constexpr float kStabilitySafetyMargin = 0.98f;

// Euler-forward stability constant for a loop whose integrators run through
// fast_tanh().  The linear condition is f^2 + 2fq < 4, which is what the
// MorphingFilter's clean path obeys.  fast_tanh() has a small-signal gain of
// 1.5 (see the *1.5f below), and the Polivoks loop puts it on both integrators
// and on the resonance feedback, so its real condition is 1.5f^2 + 3fq < 4 --
// the same shape with 4 replaced by 8/3.  Using 4 there is 2.25x too loose,
// which is why that filter used to howl at zero input whenever the cutoff was
// high and the resonance low.
constexpr float kTanhLoopGuard = 8.0f / 3.0f;

// The MorphingFilter's saturated path is a different loop: fast_tanh() is on
// both integrators but the damping term stays linear, which works out to
// 2.25f^2 + 3fq < 4.  That solves to exactly 2/3 of the linear f_max, so the
// same sqrt is reused and scaled.  Only the drive>0, sherman_asym<=0 branch
// runs through the tanh -- the clean and wavefolder branches integrate
// linearly and the unscaled guard is right for them.
constexpr float kMorphTanhGuardScale = 2.0f / 3.0f;

// Self-oscillation, van der Pol style.  Damping is scaled by
//
//     1 - howl * (1 + kHowlExcess) * max(0, 1 - state^2 * kHowlSoften)
//
// which is negative while the filter is quiet -- energy goes in and the
// oscillation grows -- and climbs back through zero as the state grows, so it
// settles at a fixed amplitude instead of running away into a clamp.  Simply
// inverting the damping does not do this: fast_tanh() saturates the feedback
// at a constant rather than reducing it, so the states ramp until something
// else stops them.
//
// kHowlExcess sets where it takes off: oscillation starts once
// howl > 1 / (1 + kHowlExcess), so 1.0 puts the takeoff at half travel and
// leaves the rest of the knob as an amplitude ramp.  kHowlSoften sets how loud
// it gets, roughly sqrt((1 - 1/(2*howl)) / kHowlSoften) at the state.
constexpr float kHowlExcess = 1.0f;
constexpr float kHowlSoften = 0.62f;

// Ceilings on the integrator states.  The damping law above is what actually
// sets the amplitude; these are insurance so no combination of howl, drive and
// audio-rate cutoff modulation can send the states to infinity.  The
// MorphingFilter's is looser because its wavefolder legitimately drives the
// states harder than the Polivoks loop ever does.
constexpr float kPolivoksStateLimit = 4.0f;
constexpr float kMorphStateLimit = 8.0f;
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

    // 0.0 = provably stable, decays to silence on zero input.
    // 1.0 = negative damping, so it self-oscillates on a bounded limit cycle.
    // Driven from F1Res in synth.h, the same way F2Res drives the Polivoks.
    float howl = 0.0f;

    // NOTE: set_coeffs() picks its stability guard from `drive` and
    // `sherman_asym`, because those decide whether the integrators run through
    // fast_tanh.  Set them before calling it, not after.

    // Euler SVF State
    float low = 0.0f;
    float band = 0.0f;
    float f = 0.0f;
    float q = 0.0f;

    inline void set_coeffs(float hz, float reso_q, float sample_rate) {
        // 1. Bottom Clamp: Prevent negative hz from audio-rate FM crashes
        hz = fmaxf(10.0f, hz);

        // 2. Top Clamp: Just below Nyquist to keep f < 2 and the SVF stable
        // Chamberlin SVFs explode above fs/4.
        // Limit max frequency to ~12kHz (0.25f) to guarantee stability.
        hz = fminf(hz, sample_rate * 0.25f);

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

        // The saturated branch needs a tighter limit: fast_tanh's 1.5 small-signal
        // gain on both integrators makes the real condition 2.25f^2 + 3fq < 4.  Used
        // unscaled, this filter self-oscillated at zero input whenever drive was up
        // and the cutoff was high -- which is any patch with CMOS or F1Res above 0.
        if (drive > 0.0f && sherman_asym <= 0.0f) f_max *= kMorphTanhGuardScale;

        if (f > f_max * kStabilitySafetyMargin) f = f_max * kStabilitySafetyMargin;
    }

    inline float process(float in, float lfo_val) {
        float drive_sig = in * (1.0f + drive);

        // Dynamic Resonance (Damping decreases as LFO pushes)
        float current_q = q * (1.0f - (lfo_val * lfo_res_mod * 0.5f));
        current_q = fmaxf(q_limit, current_q); // Prevent total self-oscillation collapse

        // Amplitude-limited negative damping -- same law as the Polivoks, sensed
        // on `band` because that is this topology's resonant state.  Applied after
        // the clamp above: that clamp exists to stop the LFO collapsing the damping
        // by accident, which is not what howl is doing.
        if (howl > 0.0f) {
            const float soften = fmaxf(0.0f, 1.0f - band * band * kHowlSoften);
            current_q *= (1.0f - howl * (1.0f + kHowlExcess) * soften);
        }

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

        // Insurance for the self-oscillating and wavefolder paths; on the stable
        // ones the states never come near this.
        band = fmaxf(-kMorphStateLimit, fminf(kMorphStateLimit, band));
        low  = fmaxf(-kMorphStateLimit, fminf(kMorphStateLimit, low));

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

    // 0.0 = provably stable, decays to silence on zero input.
    // 1.0 = negative damping, so it self-oscillates and sits on a tanh-bounded
    //       limit cycle.  Driven from F2Res in synth.h: the howl is opt-in,
    //       not the resting state of the filter.
    float howl = 0.0f;

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

        // 3. Euler-forward stability guard for the tanh-integrator loop.  See
        // kTanhLoopGuard: this is the real limit, not the linear one.  Below it
        // the filter always decays; self-oscillation comes from howl instead of
        // from an accidentally over-wide coefficient, so it can be switched off.
        float f_max = fasterSqrt_15bits(q * q + kTanhLoopGuard) - q;
        if (f > f_max * kStabilitySafetyMargin) f = f_max * kStabilitySafetyMargin;
    }

    inline float process(float in) {
        float drive_sig = in * (1.0f + drive);

        // Amplitude-limited negative damping -- see kHowlExcess / kHowlSoften.
        // At howl 0 this is exactly q and the filter is the stable one above.
        float damp = q;
        if (howl > 0.0f) {
            const float soften = fmaxf(0.0f, 1.0f - ic1eq * ic1eq * kHowlSoften);
            damp = q * (1.0f - howl * (1.0f + kHowlExcess) * soften);
        }
        float res_fb = damp * ic1eq;
        float high = drive_sig - ic2eq - fast_tanh(res_fb);

        ic1eq += f * high;
        ic2eq += f * fast_tanh(ic1eq);

        // Insurance for the self-oscillating path only -- on the stable path the
        // states never come near this.
        ic1eq = fmaxf(-kPolivoksStateLimit, fminf(kPolivoksStateLimit, ic1eq));
        ic2eq = fmaxf(-kPolivoksStateLimit, fminf(kPolivoksStateLimit, ic2eq));

        // Derive Notch by summing Highpass and Lowpass
        float notch = high + ic2eq;

        if (mode == mode_band) return ic1eq; // Bandpass
        if (mode == mode_high) return high;  // Highpass
        if (mode == mode_notch) return notch; // Notch
        return ic2eq;                // Lowpass
    }
};

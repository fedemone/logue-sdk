#pragma once
// Brachetti common library — the one- and two-pole filters the voices are
// built from (the state-variable filter is filter.h).
//
//   one_pole_lp           s += a·(x − s): smoothing, envelope followers,
//                         noise band splits
//   one_pole_hp           x − one_pole_lp(x): DC blockers, high bands
//   one_pole_coeff_fast   a one-pole coefficient for a cutoff, swept at
//                         control rate (fast exp)
//   Resonator2            a two-pole resonator ringing at a frequency
//
// Every helper is the exact arithmetic the units used inline before, so
// replacing an inline step with it changes no output.
#include <cmath>
#include "dsp_math.h"

namespace brc {

// One step of a one-pole low-pass: state += a·(x − state); returns the state.
// a in (0, 1]: 1 passes x through, small values smooth harder.
inline float one_pole_lp(float x, float a, float& state) {
    state += a * (x - state);
    return state;
}

// One step of a one-pole high-pass: x minus its one-pole low-pass (the state
// is the low-pass).  Two in cascade make a 12 dB/oct DC blocker.
inline float one_pole_hp(float x, float a, float& state) {
    state += a * (x - state);
    return x - state;
}

// The one-pole low-pass coefficient for cutoff_hz (clamped to 10 Hz .. 0.45·fs):
// 1 − e^(−2π·fc/fs).  fastexpf is fine here: its argument is always negative
// (where it is accurate), and the coefficient only follows a cutoff that an
// envelope is already sweeping.  Use exact expf for a fixed tuning.
inline float one_pole_coeff_fast(float cutoff_hz) {
    cutoff_hz = clampf(cutoff_hz, 10.0f, kSampleRate * 0.45f);
    return 1.0f - fastexpf(-phase_inc(cutoff_hz));
}

// A two-pole resonator: w = x + a1·z1 − a2·z2, with a1 = 2·r·cos(ω) and
// a2 = r² for a pole radius r at frequency ω.  Plain data, so a table can
// hold one ({a1, a2, z1, z2}).
struct Resonator2 {
    float a1, a2;   // 2·r·cos(ω), r²
    float z1, z2;   // the last two outputs

    // Tune to freq_hz with pole radius r (the state is kept, so a ringing
    // resonator retunes without a click).  fastercosfullf: its error moves
    // the pitch slightly, which is part of the voicing it was tuned with.
    void tune(float freq_hz, float r) {
        a1 = 2.0f * r * fastercosfullf(phase_inc(freq_hz));
        a2 = r * r;
    }

    float ring(float x) {
        const float w = x + (a1 * z1) - (a2 * z2);
        z2 = z1;
        z1 = w;
        return w;
    }
};

}  // namespace brc

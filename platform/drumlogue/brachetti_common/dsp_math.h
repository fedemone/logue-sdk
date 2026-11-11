#pragma once
// Brachetti common library — constants and small math helpers every unit uses.
//
// Provides:
//   kSampleRate, kInvSampleRate, kLn0001, kSilence   constants
//   clampf            clamp to [lo, hi] (fmaxf(lo, fminf(hi, x)))
//   or_default        a table value, or its fallback when it is left unset
//   knob_exp2         2^x for anchored knob curves, exactly 1 at 0
//   env_mul           per-sample multiplier of a geometric envelope (exact expf)
//   phase_inc         radians per sample of an oscillator at a frequency
//   wrap_phase        keep a phase inside one turn
//   preset_column_in  compile-time check on a preset-table column
//
// The library is header-only C++14 (the drumlogue SDK builds units with
// -std=gnu++14): plain structs with default member initialisers, no
// constructors, no virtual functions, no dynamic memory.  That keeps each
// unit's global synth object constant-initialised (it is loaded from the ELF
// instead of being built at run time).  See README.md in this folder.
#include <cmath>
#include <cstdint>
#include "float_math.h"

namespace brc {

constexpr float kSampleRate    = 48000.0f;   // the drumlogue runs at 48 kHz only
constexpr float kInvSampleRate = 1.0f / 48000.0f;
constexpr float kLn0001        = -6.907755279f;   // ln(0.001): a T60 is a -60 dB decay
constexpr float kSilence       = 1e-5f;           // below this a layer counts as finished

inline float clampf(float x, float lo, float hi) { return fmaxf(lo, fminf(hi, x)); }

// A table value below `unset` means "not given": use the fallback.  (Voicing
// tables leave a column 0 where the default applies.)
inline float or_default(float value, float unset, float fallback) {
    return (value < unset) ? fallback : value;
}

// 2^x for the REFERENCE-ANCHORED knob curves.  fasterpow2f is precise enough
// for a knob's response (±0.3 %), EXCEPT at the anchor: fasterpow2f(0) ≈
// 0.9614, not 1.0, and every anchored mapping evaluates at exactly 0 for the
// shipped preset, where the factor must be exactly 1.  So the anchor is
// guarded.  Never use this for tuning or decay coefficients: those need exact
// expf / cosf / exp2f (fasterexpf is badly wrong for |x| < 0.001;
// fastercosfullf shifts low mode frequencies).
inline float knob_exp2(float x) {
    return (x == 0.0f) ? 1.0f : fasterpow2f(x);
}

// Per-sample multiplier of a geometric envelope with time constant tau_sec.
// Exact expf: the argument is ~-1e-4..-1e-2, where float_math.h's fast*
// variants are catastrophically wrong.
inline float env_mul(float tau_sec, float sample_rate) {
    if (tau_sec < 0.00002f) return 0.0f;
    return expf(-1.0f / (tau_sec * sample_rate));
}

// Phase increment (radians per sample) of an oscillator at hz.
constexpr float phase_inc(float hz) { return (M_TWOPI * hz) * kInvSampleRate; }

// Keep an oscillator phase inside one turn.
inline void wrap_phase(float& phase) {
    if (phase > (M_TWOPI)) phase -= (M_TWOPI);
}

// Compile-time check on one column of a unit's preset table: every row's
// value lies in [lo, hi].  Used to pin the columns LoadPreset does NOT load,
// so giving one preset its own Tone or Cutoff — which would be silently
// ignored — fails the build instead.
template <int R, int C>
constexpr bool preset_column_in(const int16_t (&t)[R][C], int col, int lo, int hi) {
    for (int r = 0; r < R; ++r)
        if (t[r][col] < lo || t[r][col] > hi) return false;
    return true;
}

}  // namespace brc

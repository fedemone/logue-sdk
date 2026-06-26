#pragma once

/**
 * @file constants.h
 * @brief Core constants for the EffeESP32 drumlogue unit.
 *
 * Lean subset tailored to the FM drum engine ported from
 * copych/ESP32-S3_FM_Drum_Synth.  Original firmware ran at 44100 Hz; the
 * drumlogue runtime is fixed at 48000 Hz, so all sample-rate dependent
 * coefficients are recomputed here.
 */

#ifdef __cplusplus
#include <cstdint>
#include <cmath>
#else
#include <stdint.h>
#include <math.h>
#endif

#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline))
#endif

// ----------------------------------------------------------------------------
// Audio
// ----------------------------------------------------------------------------
constexpr int   NEON_LANES      = 4;
constexpr float SAMPLE_RATE     = 48000.0f;
constexpr float INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;
constexpr float NYQUIST_FREQ    = SAMPLE_RATE * 0.5f;

#ifndef PI
constexpr float PI              = 3.14159265358979f;
#endif
constexpr float TWO_PI          = 2.0f * PI;
constexpr float PI_F            = PI;

// ----------------------------------------------------------------------------
// Mixing gains (carrier sum normalisation, mirrors copych FmVoice6.h)
// ----------------------------------------------------------------------------
constexpr float ONE_DIV_SQRT2 = 0.707106781f;
constexpr float ONE_DIV_SQRT3 = 0.577350269f;
constexpr float ONE_DIV_SQRT5 = 0.447213595f;

// ----------------------------------------------------------------------------
// MIDI / tuning
// ----------------------------------------------------------------------------
constexpr float A4_FREQ        = 440.0f;
constexpr int   A4_MIDI        = 69;
constexpr float MIDI_NORM      = 1.0f / 127.0f;
constexpr float SEMITONE_RATIO = 1.0594630943592953f;

// ----------------------------------------------------------------------------
// Voice pool / output
// ----------------------------------------------------------------------------
constexpr int   MAX_VOICES     = 8;     // polyphony of the drum allocator
constexpr float MASTER_GAIN    = 0.5f;  // output headroom (single hits ~0.4..0.9)

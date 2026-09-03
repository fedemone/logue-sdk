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
// Voice-mix gain feeding the output stage (common/output_stage.h).  This is a
// loudness trim, not a peak trim: the soft knee behind it is bounded by 0.995
// for any finite input, so raising this buys RMS instead of clipping.
// Calibrated with platform/drumlogue/tools/level_meter -- see that README for
// the measured before/after table.
#ifndef MASTER_GAIN_OVERRIDE
constexpr float MASTER_GAIN    = 2.51f;  // 0.5f +14dB (1.41f measured -13.6 LUFS)
#else
constexpr float MASTER_GAIN    = MASTER_GAIN_OVERRIDE;
#endif

// Bus limiter (common/output_stage.h, dl::PeakLimiter).  The voice mix is
// polyphonic, so its level is proportional to how many voices are sounding;
// without a gain stage that tracks that, every extra voice drives the soft knee
// behind it further into waveshaping, which on dense inharmonic material
// (cymbals, gongs) is heard as harshness rather than as loudness.  The limiter
// rides the gain instead, so stacking changes the level and not the spectrum.
//
// LIMIT_CEILING is also the knee's threshold, so the knee is exactly unity
// under the limiter and only catches the little that leaks past it.
// Measured with platform/drumlogue/tools/level_meter: 0.90 -> -12.10 LUFS mean,
// 0.95 -> -11.75, 0.97 -> -11.63, and at 0.95 the loudest instrument still peaks
// at -0.24 dBFS.  0.97 buys 0.12 LU more and leaves the knee behind it only
// 0.025 of span to work in, so 0.95 is the better trade.
#ifndef LIMIT_CEILING_OVERRIDE
constexpr float LIMIT_CEILING  = 0.95f;
#else
constexpr float LIMIT_CEILING  = LIMIT_CEILING_OVERRIDE;
#endif
// Release.  Must stay well above the period of the lowest content the engine
// makes (SubKick's 24 Hz fundamental is 42 ms), or the follower modulates the
// waveform and becomes a distortion source itself.  Measured with
// tools/level_meter/harmonics.py on the Kick's 49.8 Hz decay: at 120 ms the
// energy above 250 Hz sits at -71.6 dB against the fundamental, at 20 ms it
// rises to -39.3 dB -- 32 dB of manufactured harmonics to buy 0.84 LU, which is
// the wrong side of that trade.
#ifndef LIMIT_RELEASE_S_OVERRIDE
constexpr float LIMIT_RELEASE_S = 0.12f;
#else
constexpr float LIMIT_RELEASE_S = LIMIT_RELEASE_S_OVERRIDE;
#endif

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
// Voice-mix gain feeding the output stage (common/output_stage.h).
//
// This is a HEADROOM trim, not a loudness trim, and that is the whole point of
// its value.  It was 2.51, chosen to push the mean to the -9 LUFS house target
// through the old memoryless knee.  At that setting a single hit at velocity
// 127 arrives 6.4 dB (Crash1), 12 dB (the slot-51 hat) or 17 dB (Cymbal1) past
// the output ceiling -- so the stage has to hold it there until the envelope
// itself has fallen that far, which on a 6 s cymbal is a full second of
// perfectly flat output before any decay is audible.  Measured fall over the
// first second of a Crash1 hit: 6.50 dB unprocessed, 0.51 dB delivered.  That
// is the "continuous noise with no envelope" failure, and no limiter tuning
// avoids it: a faster release tracks the envelope more closely and flattens it
// harder (measured 0.10 dB delivered at a 40 ms release).
//
// The only lever is drive.  Measured with tools/level_meter and
// tools/stack_meter, fall delivered out of the natural fall over the first
// second, single hit at velocity 127:
//
//   MASTER_GAIN   mean LUFS   Crash1        slot-51 hat   Cymbal1 (Splash slot)
//   2.51           -11.75     0.51 / 6.50   2.66 / 8.08   16.3 / 32.6
//   1.41           -14.32     4.82 / 6.50   2.73 / 8.08   21.3 / 32.6
//   1.00           -16.79     6.47 / 6.50   3.57 / 8.08   24.2 / 32.6
//   0.71 (now)     -19.48     6.47 / 6.50   5.48 / 8.08   27.2 / 32.6
//   0.50           -22.41     6.47 / 6.50   8.08 / 8.08   30.3 / 32.6
//
// 0.71 leaves the limiter as what it should be -- a safety net for stacking --
// instead of the thing that shapes every single hit.  The level it gives up was
// never real: see tools/level_meter/README.md on why unit loudness here is not
// worth chasing, and that on hardware the difference is a quarter turn of the
// synth track's volume knob.
#ifndef MASTER_GAIN_OVERRIDE
constexpr float MASTER_GAIN    = 0.71f;
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

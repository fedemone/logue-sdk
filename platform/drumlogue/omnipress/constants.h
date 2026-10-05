#pragma once
/**
 * @file constants.h
 * @brief Central constants for OmniPress Master Compressor
 *
 * Single source of truth for all magic numbers
 * Version 1.0
 */

#include <cstdint>
#include "arm_neon.h"
#ifndef fast_inline
#define fast_inline inline __attribute__((always_inline, optimize("Ofast")))
#endif
// ============================================================================
// DSP Constants
// ============================================================================

// Sample rate (fixed for drumlogue)
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float INV_SAMPLE_RATE = 1.0f / SAMPLE_RATE;

// Smoothing constants
constexpr int   SMOOTH_FRAMES = 48;        // 1ms at 48kHz
constexpr float SMOOTH_COEFF_MIN = 0.001f;
constexpr float SMOOTH_COEFF_MAX = 0.5f;

// Filter constants
constexpr float FILTER_Q_BESSEL = 0.5f;      // Bessel response
constexpr float FILTER_Q_BUTTERWORTH = 0.707f; // Butterworth response
constexpr float FILTER_Q_LINKWITZ_RILEY = 0.5f; // Linkwitz-Riley (cascaded)

// ============================================================================
// Parameter Ranges (as per header.c)
// ============================================================================

// Page 1: Core Dynamics
constexpr int THRESH_MIN = -600;      // -60.0 dB
constexpr int THRESH_MAX = 0;          // 0 dB
constexpr int THRESH_DEFAULT = -100;   // -10.0 dB

constexpr int RATIO_MIN = 10;          // 1.0:1
constexpr int RATIO_MAX = 200;         // 20.0:1
constexpr int SLOPE_DEFAULT = 40;      // 4.0:1

constexpr int ATTACK_MIN = 1;           // 0.1 ms
constexpr int ATTACK_MAX = 1000;        // 100.0 ms
constexpr int ATTACK_DEFAULT = 50;      // 5.0 ms

constexpr int RELEASE_MIN = 10;         // 10 ms
constexpr int RELEASE_MAX = 2000;       // 2000 ms
constexpr int RELEASE_DEFAULT = 200;    // 200 ms

// Page 2: Character & Output
constexpr int MAKEUP_MIN = 0;           // 0 dB
constexpr int MAKEUP_MAX = 240;         // 24.0 dB
constexpr int MAKEUP_DEFAULT = 0;       // 0 dB

constexpr int DRIVE_MIN = 0;            // 0%
constexpr int DRIVE_MAX = 100;          // 100%
constexpr int DRIVE_DEFAULT = 0;        // 0%

constexpr int MIX_MIN = -100;           // 100% dry
constexpr int MIX_MAX = 100;            // 100% wet
constexpr int MIX_DEFAULT = 100;        // 100% wet

constexpr int SC_HPF_MIN = 20;          // 20 Hz
constexpr int SC_HPF_MAX = 500;         // 500 Hz
constexpr int SC_HPF_DEFAULT = 20;      // 20 Hz

// Page 3: Mode Selection
constexpr int COMP_MODE_MIN = 0;
constexpr int COMP_MODE_MAX = 2;
constexpr int COMP_MODE_DEFAULT = 0;    // Standard mode

constexpr int ATTENUATION_MIN = -300;
constexpr int ATTENUATION_MAX = 0;
constexpr int ATTENUATION_DEFAULT = ATTENUATION_MIN;    // Standard mode

constexpr int GAIN_MIN = 0;
constexpr int GAIN_MAX = 300;
constexpr int GAIN_DEFAULT = GAIN_MAX;    // Standard mode

constexpr int BAND_THRESH_MIN = -600;   // -60.0 dB
constexpr int BAND_THRESH_MAX = 0;      // 0 dB
constexpr int BAND_THRESH_DEFAULT = -200; // -20.0 dB

constexpr int BAND_RATIO_MIN = 10;      // 1.0:1
constexpr int BAND_RATIO_MAX = 200;     // 20.0:1
constexpr int BAND_RATIO_DEFAULT = 40;  // 4.0:1

// Page 4: Distressor Mode
constexpr int DSTR_MODE_MIN = 0;
constexpr int DSTR_MODE_MAX = 3;
constexpr int DSTR_MODE_DEFAULT = 0;    // No harmonics

// ============================================================================
// Compression Mode IDs
// ============================================================================
typedef enum {
    COMP_MODE_STANDARD = 0,
    COMP_MODE_DISTRESSOR  ,
    COMP_MODE_MULTIBAND   ,
    COMP_MODE_TOTAL
} CompMode;

// ============================================================================
// Distressor Mode IDs
// ============================================================================
typedef enum {
    DIST_MODE_CLEAN,
    DIST_MODE_DIST2,
    DIST_MODE_DIST3,
    DIST_MODE_BOTH ,
    DRIVE_MODE_SOFT_CLIP,
    DRIVE_MODE_HARD_CLIP,
    DRIVE_MODE_TRIANGLE,
    DRIVE_MODE_SINE,
    DRIVE_MODE_SUBOCTAVE,
    DIST_MODE_TOTAL,
}   DistressorMode;

// ============================================================================
// Detection Mode IDs
// ============================================================================
typedef enum {
    DETECT_MODE_PEAK,
    DETECT_MODE_RMS,
    DETECT_MODE_BLEND,
    DETECT_MODE_TOTAL,
} DetectionMode;

// ============================================================================
// Knee Type IDs
// ============================================================================

typedef enum {
    KNEE_HARD,
    KNEE_SOFT,
    KNEE_MEDIUM,
    KNEE_TOTAL,
} KneeType;

// ============================================================================
// Distressor Ratio IDs
// ============================================================================
typedef enum {
    DIST_RATIO_1_1,    // Warm mode
    DIST_RATIO_2_1,
    DIST_RATIO_3_1,
    DIST_RATIO_4_1,
    DIST_RATIO_6_1,
    DIST_RATIO_10_1,   // Opto mode
    DIST_RATIO_20_1,
    DIST_RATIO_NUKE ,  // Brick-wall
    DIST_RATIO_TOTAL, // marker
} DistressorRatio;

// ============================================================================
// Multiband Crossover Frequencies
// ============================================================================

// Xover Lo (ID 19) and Xover Hi (ID 23) are 0..100 knobs, logarithmic, each
// spanning four octaves: 62.5 Hz..1 kHz and 1 kHz..16 kHz.  The ranges meet
// only at 1 kHz, so the low split can never land above the high one.
constexpr float XOVER_LOW_HZ_MIN    = 62.5f;
constexpr float XOVER_HIGH_HZ_MIN   = 1000.0f;
constexpr float XOVER_KNOB_LOG_SPAN = 0.027725887f;   // ln(16) / 100
constexpr int   XOVER_LOW_KNOB_DEFAULT  = 50;         // 250 Hz
constexpr int   XOVER_HIGH_KNOB_DEFAULT = 33;         // 2.50 kHz

// SoloMute (ID 15): one band soloed or muted at a time
typedef enum {
    SOLO_MUTE_OFF,
    SOLO_LOW, SOLO_MID, SOLO_HIGH,
    MUTE_LOW, MUTE_MID, MUTE_HIGH,
    SOLO_MUTE_TOTAL,
} SoloMute;

constexpr float XOVER_LOW_FREQ_DEFAULT = 250.0f;   // Low/Mid crossover
constexpr float XOVER_HIGH_FREQ_DEFAULT = 2500.0f; // Mid/High crossover
constexpr float XOVER_MIN_FREQ = 20.0f;
constexpr float XOVER_MAX_FREQ = 20000.0f;

// ============================================================================
// Envelope Detector Constants
// ============================================================================

constexpr float ENV_ATTACK_DEFAULT_MS = 10.0f;
constexpr float ENV_RELEASE_DEFAULT_MS = 100.0f;
constexpr float ENV_HOLD_MS = 10.0f;                // Peak hold time
constexpr float ENV_RMS_WINDOW_MS = 50.0f;          // RMS window size

// Level-detector ballistics for Standard and Distressor.  The detector only
// finds the level -- near-instant attack, then the 10 ms hold and a 10 ms
// release -- and ATTACK/RELEASE are applied once, by the gain smoother behind
// it, which is how Multiband has always worked.  Both stages used to claim the
// user's times: the detector honoured them and the smoother, built from
// fasterexpf (which cannot return more than 0.9713) and in Standard written
// as y += c*(x - y) instead of y = x + c*(y - x), quietly did nothing.
constexpr float DETECTOR_ATTACK_MS  = 0.05f;
constexpr float DETECTOR_RELEASE_MS = ENV_HOLD_MS;

// ============================================================================
// Gain Computer Constants
// ============================================================================

constexpr float KNEE_WIDTH_DEFAULT = 6.0f;          // 6dB soft knee
constexpr float KNEE_WIDTH_MIN = 0.0f;
constexpr float KNEE_WIDTH_MAX = 20.0f;

// ============================================================================
// Sidechain HPF Constants
// ============================================================================

constexpr float SC_HPF_Q = 0.5f;                    // Bessel response

// ============================================================================
// Makeup Gain Limits
// ============================================================================

constexpr float MAKEUP_GAIN_MAX_LINEAR = 15.85f;    // 24 dB in linear
constexpr float MAKEUP_GAIN_MIN_LINEAR = 1.0f;      // 0 dB

// ============================================================================
// Input guard
// ============================================================================
// Largest magnitude MasterFX::Process() accepts on any input channel: +24 dBFS,
// far above anything a drum machine's bus carries, and low enough that the
// detector climbs out of whatever it saw within about a second at typical
// RELEASE settings.  Without a ceiling the detector takes any finite value at
// face value: measured on the ARM build, one 1e20 sample on the bus silenced
// Distressor (Dist2, DRIVE 67, WET) for 9 s, which on stage is a crash.
constexpr float INPUT_CEILING = 16.0f;

// ============================================================================
// DRIVE Slam Region (Distressor mode only)
// ============================================================================
// Past DRIVE_SLAM_KNEE the Distressor's drive stops being a character control
// and starts being a destruction control.  See drive_slam.h for why a bounded
// shaper needs more than extra gain to keep changing above this point.

constexpr float DRIVE_SLAM_KNEE = 0.60f;   // knob position where the slam starts

// Bias ahead of the shaper at full slam, as a fraction of the drive stage's
// input envelope, before the per-family share below.  0.45 of a mean-rectified
// envelope is about 0.29 of a sine's peak, which moves the duty cycle to
// roughly 60/40 -- an audible second harmonic, and never far enough to bias
// the waveform clear of the clipping point altogether.
constexpr float DRIVE_SLAM_BIAS = 0.45f;
constexpr float DRIVE_SLAM_BIAS_ATK_MS = 15.0f;   // let transients through first
constexpr float DRIVE_SLAM_BIAS_REL_MS = 250.0f;  // then bloom, and hold

// DC blocker that follows the biased clipping. 0.9985 puts the corner near
// 11 Hz, low enough to leave kick fundamentals alone.
constexpr float DRIVE_SLAM_DC_POLE = 0.9985f;

// ----------------------------------------------------------------------------
// Per-family voicing
// ----------------------------------------------------------------------------
// The nine DstrDist settings reach three structurally different stages, and
// the same slam settings do not suit all three.
//
//   SAT   the harmonic saturators and the soft/hard clippers.  Bounded and
//         monotone, so they need a great deal of gain to travel from their
//         knee to the square-wave regime, take the full bias, and then need
//         trimming because a square is a lot louder than the sine that made
//         it.
//   FOLD  the triangle and sine folders.  Their transfer curve is periodic:
//         gain adds folds linearly and they are already past 100% THD at the
//         top of the knob, so they need no extra gain at all, only a little
//         bias to break the fold pattern's symmetry -- and no trim, because
//         folding does not raise the level the way clipping does.
//   TUBE  the Overlord fall-through at DstrDist = None.  Two cascaded stages
//         that compound, plus its own grid-current bias tracker, so it needs
//         less of everything than a bare saturator.
typedef enum {
    SLAM_FAMILY_TUBE,
    SLAM_FAMILY_SAT,
    SLAM_FAMILY_FOLD,
    SLAM_FAMILY_TOTAL,
} SlamFamily;

typedef struct {
    float gain_max;    // extra pre-gain at DRIVE = 100 (1.0 = none)
    float bias_scale;  // share of DRIVE_SLAM_BIAS this family takes
    float trim;        // output trim at full slam
} slam_voicing_t;

// No family takes a trim any more: every drive stage is level-matched
// continuously behind its shaper (DRIVE_LEVEL_* below), and a fixed trim in
// front of that could not change the level anyway.  The field stays so a
// voicing can still ask for one.
static const slam_voicing_t drive_slam_voicing[SLAM_FAMILY_TOTAL] = {
    /* TUBE */ {  6.0f, 0.50f, 1.00f },
    /* SAT  */ { 24.0f, 1.00f, 1.00f },
    /* FOLD */ {  1.0f, 0.15f, 1.00f },
};

// ----------------------------------------------------------------------------
// Drive stage level matching (the Overlord tube, and every DstrDist shaper)
// ----------------------------------------------------------------------------
// Every shaper here is bounded, so once it saturates its output level is the
// shaper's own ceiling, whatever went in: DRIVE was a volume knob.  Measured
// on a -20 dBFS bus, the Overlord tube that Standard's DRIVE reaches added
// +18.7 dB by DRIVE 100; Dist2 added 16 dB by DRIVE 60 and at SLAM 67 turned
// the whole bus into a full-scale square pinned on the output limiter.  No
// fixed makeup law can fix that, because how far a shaper saturates depends
// on the programme: the history here is 1/g (driving harder made it quieter),
// then 1/sqrt(g) (still quieter), then none (louder).
//
// So each drive stage measures its own input and output and applies their
// ratio (level_match_process in filters.h).  The ratio of the two sides'
// average power over ~half a second sets the level, since that is what
// loudness follows; a fast peak-power ratio (1 ms attack, 200 ms release)
// rides above it as a ceiling, so turning DRIVE up cannot swell the bus for
// longer than a couple of ms.  Both sides of each estimate share their ballistics,
// so what is matched is the stage's gain, not the programme's rhythm.  DRIVE
// then changes the character and not the level.
constexpr float DRIVE_LEVEL_ATTACK_MS  = 0.02f;   // the fast ceiling: effectively instant

constexpr float DRIVE_LEVEL_RELEASE_MS = 200.0f;
constexpr float DRIVE_LEVEL_SLOW_MS    = 500.0f;  // symmetric: the average-power match
constexpr float DRIVE_LEVEL_FLOOR      = 1e-10f;  // mean square, -100 dBFS: hold below
// The floor has to reach below the stages' own small-signal gain, or quiet
// material -- a reverb tail, still linear in the shaper -- is handed back
// louder than it went in: at -40 dB, DRIVE 100 left a -60 dBFS tone 13.6 dB
// hot.  The SAT pre-gain tops out at x960 (+59.6 dB) and the tube's two
// stages at about the same, so -70 dB clears both.
constexpr float DRIVE_LEVEL_GAIN_MIN   = 3.16e-4f; // -70 dB
constexpr float DRIVE_LEVEL_GAIN_MAX   = 4.0f;    // +12 dB

// The DstrDist shapers also need a DC blocker behind them at every DRIVE
// setting, not only in the slam region: Dist2 and Both are asymmetric by
// design, and on the reported chain Dist2 parked up to +0.22 of DC on the
// master bus between DRIVE 1 and 60.  Same corner as the slam's blocker.  (The
// tube has its own, inside overlord_process.)
constexpr float DIST_DC_POLE = DRIVE_SLAM_DC_POLE;

// ============================================================================
// NEON Vector Constants
// ============================================================================

constexpr int NEON_LANES = 4;                       // 4 floats per vector
constexpr int VECTOR_ALIGN = 16;                    // 16-byte alignment

// ============================================================================
// DSP State Constants
// ============================================================================

constexpr float EPSILON = 1e-12f;                   // Prevent denormals
constexpr float DB_COEFF = 8.6858896f;              // 20 / ln(10)
constexpr float INV_DB_COEFF = 0.11512925f;         // ln(10) / 20

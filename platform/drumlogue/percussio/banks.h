#pragma once
/**
 * @file banks.h
 * @brief The additive data from the source page, transcribed verbatim.
 *
 * Two kinds of table, matching the two additive instruments on the page:
 *
 *   kRatio -- partial number / amplitude pairs, for `add-partials`.  These were
 *             found by ear ("with a strong fundamental and weak random low
 *             non-integer partials, I found a steel drum sound").  The
 *             fundamental the page plays each at is in its comment below, and
 *             is what the matching preset ships on the Freq knob.
 *
 *   kHertz -- frequency / amplitude pairs, for `add-freqs`.  These are FFT peak
 *             measurements of three recordings (a tubular bell, a small gong and
 *             a Turkish cymbal) taken in SND at three transform lengths.  They
 *             are absolute frequencies, so note 60 plays them as measured and
 *             other notes transpose the whole table.
 *
 * The numbers are exactly as printed on the page, including its quirks:
 *
 *   - The first entry of several measured tables (0.24 Hz at amplitude 3.34,
 *     0.91 at 0.54, 3.6 at 0.199/0.110) is the transform's DC bin, not a
 *     partial.  It is kept because it is part of the published data; the voice
 *     runs a DC blocker, and bank normalisation ignores everything under 20 Hz,
 *     so it contributes a slow breath rather than a headroom-eating offset.
 *   - The 16384-point cymbal table ends "4324 .342 4556 .808 .4587 .467".  That
 *     stray decimal point is a typo for 4587 Hz -- a 0.4587 Hz partial at
 *     amplitude 0.467 next to a 4556 Hz one is not a measurement -- and 4587 is
 *     what is stored.
 *   - The 262144-point cymbal table lists 2920 Hz before 2196 Hz.  Order is
 *     preserved, because the Partial knob takes the first N entries and the
 *     page's own ordering is what it takes.
 */

#include <cstdint>

namespace banks {

enum Kind : uint8_t {
  kRatio = 0,  // entries are partial numbers, multiplied by the fundamental
  kHertz = 1,  // entries are frequencies in Hz at note 60
};

/** Largest bank; the small gong has 28 pairs. */
enum { kMaxPartials = 32 };

struct Bank {
  const float* f;    // partial number or Hz
  const float* a;    // amplitude
  uint8_t n;         // pairs
  uint8_t kind;      // Kind
  const char* name;  // shown on the Bank parameter
};

// ---- add-partials: found by ear -----------------------------------------

// (setf steel-drum '(1 1 2 .2 2.6 .1 3.2 .1 5.6 .1 8.2 .1 2.9 .1 3 .1 4.2 .1 6.6 .1))
// (with-sound () (add-partials 0 .5 300 .5 steel-drum))   ; also 350 and 390
static const float kSteelF[] = {1.0f, 2.0f, 2.6f, 3.2f, 5.6f, 8.2f, 2.9f, 3.0f, 4.2f, 6.6f};
static const float kSteelA[] = {1.0f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};

// (setf low-bell '(1 .5 1.3 .1 1.6 .1 1.9 .1 2.2 .1))
// (with-sound () (add-partials 0 2 300 .5 low-bell :amp-env '(0 1 10 1 90 .05 100 0)))
static const float kClockF[] = {1.0f, 1.3f, 1.6f, 1.9f, 2.2f};
static const float kClockA[] = {0.5f, 0.1f, 0.1f, 0.1f, 0.1f};

// (setf odd-chime '(1 .3 3 .2 5 .1 7 .1 9 .1))   ; played at 1300 and 2078 Hz
static const float kOddF[] = {1.0f, 3.0f, 5.0f, 7.0f, 9.0f};
static const float kOddA[] = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f};

// (setf even-chime '(1 .3 2 .2 4 .1 6 .1 8 .1))
static const float kEvenF[] = {1.0f, 2.0f, 4.0f, 6.0f, 8.0f};
static const float kEvenA[] = {0.3f, 0.2f, 0.1f, 0.1f, 0.1f};

// ---- add-freqs: FFT peaks measured in SND -------------------------------

// Tubular bell, size 16384 FFT.
static const float kBell16F[] = {3.6f,   161.9f, 316.4f, 520.6f, 770.8f, 974.7f, 1062.0f, 1163.0f,
                                 1266.0f, 1391.0f, 1537.0f, 1752.0f, 1899.0f, 2141.0f, 2555.0f,
                                 2988.0f, 3191.0f, 3438.0f, 3870.0f, 3957.0f, 4389.0f, 4870.0f,
                                 5357.0f};
static const float kBell16A[] = {0.199f, 0.016f, 0.038f, 0.269f, 0.476f, 0.007f, 1.003f, 0.010f,
                                 0.010f, 0.980f, 0.006f, 1.000f, 0.012f, 0.134f, 0.345f,
                                 0.124f, 0.010f, 0.047f, 0.009f, 0.009f, 0.014f, 0.037f,
                                 0.013f};

// Tubular bell, size 65536 FFT.  The page prefers this one: "using peak data
// measured using the length 65536 FFT made a better sound".
static const float kBell65F[] = {0.91f,  162.0f,  317.0f,  521.0f,  771.0f,  943.0f,  1062.5f,
                                 1182.0f, 1266.0f, 1391.0f, 1511.0f, 1633.0f, 1752.0f, 1899.0f,
                                 2142.0f, 2377.0f, 2555.0f, 2988.0f, 3437.0f, 3813.0f, 4868.0f};
static const float kBell65A[] = {0.540f, 0.017f, 0.035f, 0.409f, 0.782f, 0.005f, 1.003f,
                                 0.005f, 0.018f, 0.670f, 0.005f, 0.006f, 0.662f, 0.009f,
                                 0.057f, 0.005f, 0.118f, 0.019f, 0.015f, 0.006f, 0.004f};

// Tubular bell, size 262144 FFT.
static const float kBell26F[] = {0.24f, 520.85f, 770.95f, 1062.6f, 1391.0f, 1752.3f};
static const float kBell26A[] = {3.34f, 0.342f, 0.540f, 0.624f, 0.324f, 0.234f};

// Small gong, size 16384 FFT.
static const float kGongF[] = {3.6f,   273.2f, 371.7f, 547.6f, 644.6f, 717.3f, 780.0f,
                               991.5f, 1082.0f, 1150.0f, 1203.0f, 1257.0f, 1357.0f, 1908.0f,
                               2014.0f, 2089.0f, 2167.0f, 2551.0f, 2638.0f, 2875.0f, 2917.0f,
                               3184.0f, 3532.0f, 3592.0f, 3834.0f, 4280.0f, 4630.0f, 4697.0f};
static const float kGongA[] = {0.110f, 1.101f, 0.176f, 0.352f, 0.246f, 1.075f, 0.074f,
                               0.165f, 0.244f, 0.108f, 0.093f, 0.104f, 1.390f, 0.128f,
                               0.091f, 0.134f, 0.067f, 0.074f, 0.0153f, 0.104f, 0.068f,
                               0.101f, 0.073f, 0.092f, 0.070f, 0.069f, 0.062f, 0.071f};

// Turkish cymbal, size 262144 FFT.
static const float kCym26F[] = {0.24f,  100.41f, 194.65f, 309.34f, 399.6f, 503.7f, 667.0f,
                                922.0f, 1037.0f, 1449.0f, 1524.0f, 1699.0f, 1871.0f, 2047.6f,
                                2920.0f, 2196.0f, 2269.0f, 2415.0f, 2720.0f, 2867.0f, 3540.0f,
                                3644.0f, 3733.0f, 4430.0f};
static const float kCym26A[] = {3.002f, 0.163f, 0.156f, 0.357f, 0.265f, 0.271f, 0.276f,
                                0.160f, 0.144f, 0.092f, 0.105f, 0.117f, 0.256f, 0.125f,
                                0.121f, 0.190f, 0.157f, 0.163f, 0.117f, 0.225f, 0.162f,
                                0.112f, 0.091f, 0.116f};

// Turkish cymbal, size 16384 FFT.
static const float kCym16F[] = {307.0f,  398.0f,  533.0f,  641.0f,  689.0f,  1449.0f, 1513.0f,
                                2032.0f, 3543.0f, 3648.0f, 3709.0f, 4324.0f, 4556.0f, 4587.0f};
static const float kCym16A[] = {0.573f, 0.619f, 1.005f, 0.329f, 0.482f, 0.349f, 0.319f,
                                0.390f, 0.502f, 0.445f, 0.310f, 0.342f, 0.808f, 0.467f};

// (with-sound () (add-noise 0 .5 .0005 '(100 1 200 .5 400 .2) :amp-env ... :r .99))
static const float kNsyBassF[] = {100.0f, 200.0f, 400.0f};
static const float kNsyBassA[] = {1.0f, 0.5f, 0.2f};

enum BankId : uint8_t {
  kBankSteel = 0,
  kBankClock,
  kBankOdd,
  kBankEven,
  kBankBell16,
  kBankBell65,
  kBankBell26,
  kBankGong,
  kBankCym26,
  kBankCym16,
  kBankNsyBass,
  kNumBanks
};

#define PCS_BANK(f, a, kind, nm) \
  { f, a, (uint8_t)(sizeof(f) / sizeof(float)), kind, nm }

static const Bank kBanks[kNumBanks] = {
    PCS_BANK(kSteelF, kSteelA, kRatio, "Steel"),
    PCS_BANK(kClockF, kClockA, kRatio, "ClkBell"),
    PCS_BANK(kOddF, kOddA, kRatio, "OddChm"),
    PCS_BANK(kEvenF, kEvenA, kRatio, "EvnChm"),
    PCS_BANK(kBell16F, kBell16A, kHertz, "TubBl16"),
    PCS_BANK(kBell65F, kBell65A, kHertz, "TubBl65"),
    PCS_BANK(kBell26F, kBell26A, kHertz, "TubBl26"),
    PCS_BANK(kGongF, kGongA, kHertz, "Gong"),
    PCS_BANK(kCym26F, kCym26A, kHertz, "TCym26"),
    PCS_BANK(kCym16F, kCym16A, kHertz, "TCym16"),
    PCS_BANK(kNsyBassF, kNsyBassA, kHertz, "NsyBass"),
};

#undef PCS_BANK

}  // namespace banks

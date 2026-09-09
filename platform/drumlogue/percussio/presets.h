#pragma once
/**
 * @file presets.h
 * @brief Parameter map and the source page's sound recipes, one preset each.
 *
 * Every preset below is a `with-sound` call from
 * https://ccrma.stanford.edu/~sdill/220A-project/drums.html, transcribed.  The
 * comment on each row is the original call.  Where a preset departs from it,
 * the row says so.
 *
 * Three global conversions apply to all of them:
 *
 *  1. FREQUENCY -> NOTE.  CLM takes a frequency argument; a drumlogue synth
 *     takes a note.  Note 60 plays the page's value and other notes transpose
 *     it, so a preset sounds as published when the sequencer is at C4 and stays
 *     playable off it.  This is not concert pitch: "Marimba" is 400 Hz at note
 *     60, not 261.6.
 *
 *  2. DURATION -> Decay.  The CLM duration argument becomes the Decay knob.
 *     Two envelopes on the page finish before the duration does
 *     ((0 1 80 0 100 0) on the second cymbal); those are folded into Decay,
 *     which is exact.
 *
 *  3. AMPLITUDE -> Level.  The page's amplitudes are mostly gain compensation
 *     for un-normalised resonators (.4 for a one-pole against .0007 for a
 *     narrow two-pole).  clm::TwoPole is peak-normalised here, so that
 *     compensation is neither needed nor wanted, and those rows carry a plain
 *     musical level instead.  Where the page's amplitude is a musical choice --
 *     the .2/.3/.4/.6 spread across the one-pole and one-zero sounds, .5 vs 1
 *     on the FM instruments -- it is kept as written.  Amplitudes above 1
 *     (the page's dance basses ask for 5) clamp at 100%.
 */

#include <cstdint>

#include "banks.h"
#include "voice.h"

namespace pcs {

/*===========================================================================*/
/* Parameters                                                                */
/*===========================================================================*/

enum ParamIndex : uint8_t {
  // Page 1 -- voice
  k_method = 0,
  k_model,
  k_decay,
  k_level,
  // Page 2 -- envelope
  k_attack,
  k_hold,
  k_curve,
  k_tune,
  // Page 3 -- resonator, tone and punch
  k_coef,
  k_reso,
  k_freq,
  k_punch,
  // Page 4 -- FM
  k_ratio,
  k_index1,
  k_index2,
  k_moddcy,
  // Page 5 -- additive
  k_bank,
  k_partial,
  k_nseamp,
  k_rand,
  // Page 6 -- Karplus-Strong and granular
  k_blend,
  k_length,
  k_density,
  k_grain,
  k_num_params
};

/**
 * CLM `:base` values offered by the Curve parameter.  The page uses 10 (the
 * subtractive default), 100, 1000, 100000 and 1000000, and linear for
 * everything additive and FM; the sub-1 entries are the other side of the same
 * control, and give the ordinary fast-then-tail percussive decay that a base
 * above 1 cannot produce.
 */
static const float kCurveBase[] = {0.01f, 0.1f, 0.5f, 1.0f,     3.0f,     10.0f,
                                   100.0f, 1000.0f, 10000.0f, 100000.0f, 1000000.0f};
static const char* const kCurveName[] = {"0.01", "0.1", "0.5", "Lin", "3",  "10",
                                         "100",  "1k",  "10k", "100k", "1M"};
enum { kNumCurves = (int)(sizeof(kCurveBase) / sizeof(float)) };

static const char* const kMethodName[kNumMethods] = {"Subtr", "Addit", "FM", "KarplS", "Granul"};

/**
 * Model strings, indexed by method.  Unused slots are empty.
 *
 * The two entries the page does not have are the constant-Q resonators:
 * `TwoPolQ` is subtract-pp and `TunedNsQ` is add-noise, both with `Reso` read
 * as a Q instead of a pole radius, so the sound keeps its character when it is
 * transposed.  At note 60 with no punch each is bit-identical to the model
 * above it, which is why they are variants of those models rather than a knob
 * of their own -- and the parameter budget is full anyway.
 */
static const char* const kModelName[kNumMethods][5] = {
    {"OnePole", "TwoPole", "OneZero", "TwoPolQ", ""},  // subtract-op / -pp / -oz
    {"Pure", "NseFlr", "NsyFrq", "TunedNs", "TunedNsQ"},  // add-freqs / :noise-amp / -noisy / -noise
    {"Dec .2", "Dec 0", "Rise", "", ""},   // the three fm mod-env shapes on the page
    {"Random", "Sine", "Const", "", ""},   // drum-ks initial wavetable
    {"Bell", "BellRev", "Cymbal", "CymRev", ""},  // grani source and direction
};

static const uint8_t kNumModels[kNumMethods] = {kNumSubtractModels, kNumAdditiveModels, kNumFmModels,
                                                kNumKsModels, kNumGrainModels};

/**
 * CLM's default sample rate in 1998 was 22050 Hz, and the page never overrides
 * it except to play back its 44100 Hz reference recordings.  A Karplus-Strong
 * wavetable length is in samples, so p = 4000 there is a 5.5 Hz loop, not the
 * 12 Hz one the same number gives at 48 kHz.  The preset rows below carry the
 * page's p scaled by this factor, which preserves the timbre the page names;
 * the unscaled number is in each row's comment.
 */
static constexpr float kKsRateScale = 48000.0f / 22050.0f;

/*===========================================================================*/
/* Presets                                                                   */
/*===========================================================================*/

struct Preset {
  const char* name;
  int16_t p[k_num_params];
};

// Column order, for reading the rows below:
//  Meth Modl Decay Level | Atk Hold Curv Tune | Coef Reso Freq Pnch
//  Ratio Idx1 Idx2 MDcy  | Bank Part NsAm Rand | Blnd Len Dens Grn
//
// Level/Attack/Hold/NsAm/Pnch are tenths of a percent (400 = 40.0%), Reso and
// Blend hundredths (9900 = 99.00%), Ratio thousandths (1400 = 1.400),
// Index1/Index2 hundredths (250 = 2.50).

static const Preset kPresets[] = {
    // ---- Subtractive: drum-subtract.ins --------------------------------
    // (subtract-op 0 .2 .4 '(0 0 5 1 100 0) :b1 0.9 :amp-env-base 10)
    {"BrshSnr1", {0, 0, 200, 400, 50, 50, 5, 0, 90, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-op 0 .5 .6 '(0 0 5 1 100 0) :b1 0.9 :amp-env-base 1000)
    {"BrshSnr2", {0, 0, 500, 600, 50, 50, 7, 0, 90, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-op 0 1 .6 '(0 0 2 1 100 0) :b1 0.9 :amp-env-base 100000)
    {"BrshSnr3", {0, 0, 1000, 600, 20, 20, 9, 0, 90, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-oz 0 .25 .4 '(0 0 5 1 100 0) :a1 -0.5 :amp-env-base 10)
    {"BrshSnr4", {0, 2, 250, 400, 50, 50, 5, 0, -50, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-oz 0 .5 .6 '(0 0 5 1 100 0) :a1 -0.5 :amp-env-base 1000)
    {"BrshSnr5", {0, 2, 500, 600, 50, 50, 7, 0, -50, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-op 0 .2 .3 '(0 1 100 0) :b1 0.2 :amp-env-base 100)
    {"SnareOP", {0, 0, 200, 300, 0, 0, 6, 0, 20, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 5000,
                 1741, 20, 100}},
    // (subtract-oz 0 .2 .3 '(0 1 100 0) :a1 -0.2 :amp-env-base 100)
    {"SnareOZ", {0, 2, 200, 300, 0, 0, 6, 0, -20, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // (subtract-op 0 .2 .3 '(0 1 100 0) :b1 -0.2 :amp-env-base 100)  -- low-pass
    {"SnareLoP", {0, 0, 200, 300, 0, 0, 6, 0, -20, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-oz 0 .2 .3 '(0 1 100 0) :a1 0.2 :amp-env-base 100)   -- low-pass
    {"SnareLoZ", {0, 2, 200, 300, 0, 0, 6, 0, 20, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-pp 0 .2 .2 '(0 1 100 0) :r 0.2 :frequency 400)
    {"SnarePP", {0, 1, 200, 200, 0, 0, 5, 0, 0, 2000, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 5000,
                 1741, 20, 100}},
    // (subtract-pp 0 .5 .02 '(0 0 5 1 100 0) :r .9 :frequency 100 :amp-env-base 1000)
    {"NsyBass1", {0, 1, 500, 600, 50, 50, 7, 0, 0, 9000, 100, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-pp 0 .5 .002 '(0 1 10 1 100 0) :r .99 :frequency 100 :amp-env-base 1000)
    {"NsyBass2", {0, 1, 500, 600, 0, 100, 7, 0, 0, 9900, 100, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-pp 0 .5 .0007 '(0 1 10 1 100 0) :r .999 :frequency 100 :amp-env-base 1000)
    {"NsyBass3", {0, 1, 500, 600, 0, 100, 7, 0, 0, 9990, 100, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-op 0 .2 .2 '(0 0 30 1 50 1 100 0) :b1 0.5)
    {"RattleP", {0, 0, 200, 200, 300, 500, 5, 0, 50, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // (subtract-oz 0 .2 .2 '(0 0 30 1 50 1 100 0) :a1 -0.5)
    {"RattleZ", {0, 2, 200, 200, 300, 500, 5, 0, -50, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // (subtract-pp 0 1 .0004 '(0 1 70 1 100 0) :amp-env-base 1000 :frequency 400 :r .9999)
    // Reso maxes at 99.99%, which is the page's r exactly.
    {"BlwBottl", {0, 1, 1000, 600, 0, 700, 7, 0, 0, 9999, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (subtract-pp 0 .4 .1 '(0 1 100 0) :r 0.7 :frequency 4000 :amp-env-base 1000000)
    {"FireCrkr", {0, 1, 400, 600, 0, 0, 10, 0, 0, 7000, 4000, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},

    // ---- Additive: drum-add.ins ----------------------------------------
    // (add-partials 0 .5 300 .5 steel-drum)   -- default amp-env (0 0 5 1 10 1 100 0)
    {"SteelDrm", {1, 0, 500, 500, 50, 100, 3, 0, 0, 9900, 300, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-partials 0 2 300 .5 low-bell :amp-env '(0 1 10 1 90 .05 100 0))
    // The 90%/.05 breakpoint is approximated by a 0.1-base decay from 10%,
    // which passes .03 where the page passes .05.
    {"ClockBel", {1, 0, 2000, 500, 0, 100, 1, 0, 0, 9900, 300, 0, 1400, 0, 100, 50, 1, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-partials 0 1 1300 .5 odd-chime :amp-env '(0 1 10 1 100 0))
    {"OddChime", {1, 0, 1000, 500, 0, 100, 3, 0, 0, 9900, 1300, 0, 1400, 0, 100, 50, 2, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-partials 0 1 1300 .5 even-chime :amp-env '(0 1 10 1 100 0))
    {"EvnChime", {1, 0, 1000, 500, 0, 100, 3, 0, 0, 9900, 1300, 0, 1400, 0, 100, 50, 3, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 2 .1 bell :amp-env '(0 1 10 1 100 0))   -- 16384-point FFT
    {"TubBell1", {1, 0, 2000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 4, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 2 .1 bell ...)   -- 65536-point FFT, the page's favourite
    {"TubBell2", {1, 0, 2000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 5, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 2 .1 bell ...)   -- 262144-point FFT
    {"TubBell3", {1, 0, 2000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 6, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 1 .1 gong :amp-env '(0 0 1 1 10 1 100 0))
    {"SmallGng", {1, 0, 1000, 500, 10, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 7, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 1 .1 cymbal :amp-env '(0 1 10 1 100 0))   -- 262144-point FFT
    {"TurkCym1", {1, 0, 1000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 8, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-freqs 0 1 .1 cymbal ...)   -- 16384-point FFT
    {"TurkCym2", {1, 0, 1000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 9, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (add-noisy-freqs 0 1 .1 cymbal 1 :amp-env '(0 1 10 1 100 0))
    {"NsyTCym", {1, 2, 1000, 500, 0, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 8, 100, 1000, 0,
                 5000, 1741, 20, 100}},
    // (add-noise 0 .5 .0005 '(100 1 200 .5 400 .2) :amp-env '(0 0 1 1 10 1 100 0) :r .99)
    {"NsyBsDrm", {1, 3, 500, 500, 10, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 10, 100, 0, 0,
                  5000, 1741, 20, 100}},

    // ---- FM: drum-fm.ins -----------------------------------------------
    // (fm 0 10 1 200 1.4 :car-env '(0 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0) :mod-index2 1)
    {"ChwnBell", {2, 0, 10000, 1000, 0, 500, 3, 0, 0, 9900, 200, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 .2 1 200 1.4 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0)
    //     :mod-index2 .2)
    {"WoodDrum", {2, 1, 200, 1000, 200, 500, 3, 0, 0, 9900, 200, 0, 1400, 0, 20, 12, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 .2 1 200 0.6875 ...)   -- Ratio is thousandths, so 0.6875 stores as 0.688
    {"WoodDrm2", {2, 1, 200, 1000, 200, 500, 3, 0, 0, 9900, 200, 0, 688, 0, 20, 12, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 1 .5 1000 2.005 :car-env '(0 1 50 .2 100 0) :mod-env '(0 1 25 .2 100 0) :mod-index2 1)
    {"MetlChim", {2, 0, 1000, 500, 0, 500, 3, 0, 0, 9900, 1000, 0, 2005, 0, 100, 25, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 .2 1 400 2.4 :car-env '(0 0.8 10 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0)
    //     :mod-index2 .2)
    {"Marimba", {2, 0, 200, 1000, 100, 500, 3, 0, 0, 9900, 400, 0, 2400, 0, 20, 50, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // (fm 0 .5 5 50 1.4 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0)
    //     :mod-index2 .2)   -- amplitude 5 clamps to 100%
    {"DnceBas1", {2, 1, 500, 1000, 200, 500, 3, 0, 0, 9900, 50, 0, 1400, 0, 20, 12, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm i .4 5 80 0.6875 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0)
    //     :mod-index2 2.5)
    {"DnceBas2", {2, 1, 400, 1000, 200, 500, 3, 0, 0, 9900, 80, 0, 688, 0, 250, 12, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 .3 .5 200 1.4 :car-env '(0 1 10 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0)
    //     :mod-index1 2 :mod-index2 1)
    // DEVIATION: the carrier envelope starts at 0.8 here, not 1.0 -- the start
    // level follows the attack breakpoint (see Voice::buildAmpEnv), and this is
    // the one page envelope where the two disagree.
    {"FMBrshSn", {2, 0, 300, 500, 100, 500, 3, 0, 0, 9900, 200, 0, 1400, 200, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (fm 0 1 1 40 2.2 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 0 12 1 100 0) :mod-index2 2)
    {"ShetMetl", {2, 2, 1000, 1000, 200, 500, 3, 0, 0, 9900, 40, 0, 2200, 0, 200, 12, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},

    // ---- Karplus-Strong: drum-ks.ins -----------------------------------
    // Lengths are the page's p scaled by 48000/22050 (see kKsRateScale).
    // (drum-ks 0 .5 :p 800 :b 0.5)         -- default duration 2, amp-env (0 1 100 1)
    {"KSSnare1", {3, 0, 2000, 500, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (drum-ks 0 .5 :p 1000 :b 0.6)
    {"KSSnare2", {3, 0, 2000, 500, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  6000, 2177, 20, 100}},
    // (drum-ks 0 .5 :p 4000 :b 1 :amp-env '(0 1 90 1 100 0))
    {"KSCymbl1", {3, 0, 2000, 500, 0, 900, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  10000, 8707, 20, 100}},
    // (drum-ks 0 .5 :p 2000 :b 1 :amp-env '(0 1 80 0 100 0))
    // The envelope reaches zero at 80% of a 2 s duration, so Decay is 1600 ms.
    {"KSCymbl2", {3, 0, 1600, 500, 0, 0, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  10000, 4354, 20, 100}},
    // (drum-ks 0 .5 :p 100 :b .98 :amp-env '(0 1 90 1 100 0))
    {"MetPlnk1", {3, 0, 2000, 500, 0, 900, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  9800, 218, 20, 100}},
    // (drum-ks 0 .5 :p 20 :b .997)
    {"MetPlnk2", {3, 0, 2000, 500, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  9970, 44, 20, 100}},
    // (drum-ks 0 .5 :p 50 :b .99)
    {"MetPlnk3", {3, 0, 2000, 500, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  9900, 109, 20, 100}},
    // (drum-ks 0 .5 :p 150 :b .99 :amp-env '(0 1 90 1 100 0))
    {"MetPlnk4", {3, 0, 2000, 500, 0, 900, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  9900, 327, 20, 100}},
    // (drum-ks 0 .5 :p 25 :b 1)
    {"MetPlnk5", {3, 0, 2000, 500, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  10000, 54, 20, 100}},
    // (drum-ks 0 .5 :p 40 :b 0 :amp-env '(0 1 90 1 100 0))
    {"Pluck1", {3, 0, 2000, 500, 0, 900, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 0,
                87, 20, 100}},
    // (drum-ks 0 1 :p 25 :b 0)
    {"Pluck2", {3, 0, 2000, 1000, 0, 1000, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 0,
                54, 20, 100}},
    // (drum-ks 0 .5 :p 200 :b 0 :amp-env '(0 1 50 1 100 0) :duration 2)
    {"Pluck3", {3, 0, 2000, 500, 0, 500, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 0,
                435, 20, 100}},
    // (drum-ks 0 .5 :p 400 :b 0 :amp-env '(0 1 50 1 100 0) :duration 2)
    {"Pluck4", {3, 0, 2000, 500, 0, 500, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0, 0,
                871, 20, 100}},

    // ---- Granular: drum-grani.ins --------------------------------------
    // The page grains recordings of a Turkish cymbal and a tubular bell.  Those
    // two sounds are also in this unit as measured additive banks, so the grain
    // sources here are Percussio's own renders of them -- taking the cymbal
    // apart and putting it back together again, as the page puts it, on a unit
    // that ships no samples.
    // (grani 0 2 20 "turkish-cymbal-1.snd" :grain-envelope '(0 1 100 0)
    //        :amp-envelope '(0 0 10 1 100 0))
    {"BrknCym1", {4, 2, 2000, 500, 100, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (grani 0 4 5 "turkish-cymbal-1.snd" ... :grain-density 20 :reverse t)
    {"BrknCym2", {4, 3, 4000, 500, 100, 100, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // (grani 0 2 10 "tubular-bell.snd" :grain-envelope '(0 1 100 0)
    //        :amp-envelope '(0 1 50 1 100 0) :grain-density 4 :reverse t)
    {"RhytBel1", {4, 1, 2000, 500, 0, 500, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 4, 100}},
    // (grani 2 2 10 "tubular-bell.snd" ... :grain-density 8 :reverse t)
    {"RhytBel2", {4, 0, 2000, 500, 0, 500, 3, 0, 0, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 8, 100}},

    // ---- Percussion: the page's engines, tuned for a drum machine -------
    // Nine sounds that are not on the page.  They exist to show what the
    // three controls the page has no equivalent for actually do: Punch, the
    // Q models, and Coef read as a tone tilt on the engines that ignore it.
    // A kick: subtract-pp held at Q, 14 semitones of pitch drop over 24 ms,
    // and Coef reading as a tone tilt because a two-pole never spends it.
    {"PunchKik", {0, 3, 400, 900, 0, 0, 1, 0, -40, 9950, 60, 600, 1400, 0, 100, 6, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // The same idea an octave and a half up.  Play it across the pad: the Q
    // model is what keeps the top of the range from turning into a whistle.
    {"PunchTom", {0, 3, 350, 650, 0, 0, 1, 0, -20, 9960, 180, 350, 1400, 0, 100, 10, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // subtract-op with the envelope the page never uses: no attack segment, no
    // plateau, and a 0.01 base, which is a hit that decays rather than a sizzle
    // that stops.  The punch moves the randh rate, since a one-pole has no
    // frequency of its own.
    {"TightSnr", {0, 0, 150, 750, 0, 0, 0, 0, 55, 9900, 400, 250, 1400, 0, 100, 8, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // 60 ms of one-zero high-pass.  Nothing new here but the length.
    {"TightHat", {0, 2, 60, 700, 0, 0, 1, 0, -90, 9900, 400, 0, 1400, 0, 100, 50, 0, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // FM at ratio 1, which is the harmonic case, with the punch on the carrier
    // and the index falling into it.
    {"SubKick", {2, 1, 600, 1000, 0, 200, 1, 0, -30, 9900, 45, 700, 1000, 0, 150, 5, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // 19 semitones in 10 ms at ratio 3.5: the punch is the whole sound.
    {"ZapPerc", {2, 0, 250, 600, 0, 300, 1, 0, 20, 9900, 300, 800, 3500, 0, 400, 4, 0, 100, 0, 0,
                 5000, 1741, 20, 100}},
    // NsyBsDrm's bank through the constant-Q resonators, with a pitch drop.
    // All eleven partials move together, so the bank stays in tune with itself.
    {"QBassDrm", {1, 4, 400, 700, 0, 50, 1, 0, -25, 9900, 400, 450, 1400, 0, 100, 8, 10, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // The 16384-point tubular bell, tilted bright and given a short pitch drop
    // -- the strike transient a struck bar has and a bank of steady sinusoids
    // does not.
    {"ClickBel", {1, 0, 1200, 700, 0, 20, 1, 0, 45, 9900, 400, 150, 1400, 0, 100, 3, 4, 100, 0, 0,
                  5000, 1741, 20, 100}},
    // The gong with the top taken off it.
    {"DarkGong", {1, 0, 2500, 900, 0, 100, 2, 0, -65, 9900, 400, 0, 1400, 0, 100, 50, 7, 100, 0, 0,
                  5000, 1741, 20, 100}},
};

enum { kNumPresets = (int)(sizeof(kPresets) / sizeof(Preset)) };

}  // namespace pcs

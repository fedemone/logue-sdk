#pragma once
// Brachetti Cymbal — the knob layout, the presets and how each one strikes.
#include <cstdint>
#include "../brachetti_common/dsp_math.h"
#include "voice.h"

// ── The knobs, slot by slot ─────────────────────────────────────────────────
// MUST match header.c slot for slot: the preset table below has one column per
// slot, and a parameter added here without its row in header.c (or vice
// versa) silently shifts every column of every preset.  (No Model or Nz*
// knobs: a cymbal has its own noise drivers and no modal body.  Partls is the
// resonator-bank density.)
enum ParamIndex : uint8_t {
    k_paramProgram = 0,  // 0
    k_paramPitch,        // 1
    k_paramPoly,         // 2
    k_paramVelocity,     // 3
    k_paramMlltRes,      // 4
    k_paramMlltStif,     // 5
    k_paramVlMllRes,     // 6
    k_paramVlMllStf,     // 7
    k_paramPartls,       // 8
    k_paramDkay,         // 9
    k_paramMterl,        // 10
    k_paramTone,         // 11
    k_paramHitPos,       // 12
    k_paramRel,          // 13
    k_paramInharm,       // 14
    k_paramCutoff,       // 15
    k_paramTubRad,       // 16
    k_paramGain,         // 17
    k_paramResnc,        // 18
    k_lastParamIndex     // marker
};
static_assert(k_lastParamIndex == 19, "header.c declares 19 params");

// ── The presets ─────────────────────────────────────────────────────────────
// (The comments name each preset's reference recording.)
enum Preset : uint8_t {
    k_Cymbal = 0,       // 0  cymbal-Crash16Inch.wav (starts at 2 kHz, dips to 650 Hz, settles at 1 kHz)
    k_Gong,             // 1  Chinese-Gong.wav, Gong-long-G#.wav (starts at 800 Hz, settles at 1680 Hz)
    k_HiHatOpen,        // 2  TightClosedHat.wav (11.6 kHz)
    k_Ride,             // 3  cymbal-Ride18Inch.wav (starts above 2 kHz, settles at 761 Hz)
    k_RideBell,         // 4  cymbal-RideBell20InchSabian.wav (starts above 2 kHz, settles at 867 Hz)
    k_Splash,           // 5  a small pitched splash cymbal
    kNumPresets         // 6  count
};

inline const char* preset_name(uint8_t idx) {
    static const char* const names[kNumPresets] = {
        "Cymbal", "Gong", "HHat-O", "Ride", "RidBel", "Splash"
    };
    return (idx < kNumPresets) ? names[idx] : "Unknown";
}

// One row per preset, one column per knob in slot order: the knob values the
// preset ships with.  The Pitch column holds the preset's ROOT NOTE (MIDI),
// not a knob value: the Pitch knob is an offset in semitones from it.  The
// columns of the knobs the host owns (Poly, Velocity, VlMllRes, Tone, Cutoff,
// Resnc) are never loaded; the static_asserts below pin them so a preset
// cannot quietly ship its own.  int16_t: the stored range is [-10, 1999].
static constexpr int16_t kPresetKnobs[kNumPresets][k_lastParamIndex] = {
    //  Prg Root  Ply  Vel MlRs MlSt VlRs VlSt Ptls  Dky  Mtr  Ton  Hit  Rel InHm  Cut TbRd Gain Rsnc
    {   0,  65,   0,   1, 800,  45,   0,   0,   3, 200,  28,   0,   0,  18,   1,1999,  15,   5,  71},   // 0: Cymbal
    {   1,  50,   0,   1, 200,   2,   0,   0,   3, 190,   1,   0,   0,  20,   1,1999,  20,  20,  71},   // 1: Gong
    {   2,  79,   0,   1, 900,  49,   0,   0,   3, 200,  18,   0,   0,  18,   1,1999,  12,   0,  71},   // 2: HHat-O
    {   3,  69,   0,   1, 900,  50,   0,   0,   3, 190,  28,   0,   0,  18,   1,1999,  17,   0,  71},   // 3: Ride
    {   4,  60,   0,   1, 900,  49,   0,   0,   3, 200,  16,   0,   0,   8,   1,1999,   1,   0,  71},   // 4: RidBel
    {   5,  76,   0,   1, 800,  45,   0,   0,   3, 200,  28,   0,   0,  18,   1,1999,  15,   5,  71}    // 5: Splash
};
static_assert(brc::preset_column_in(kPresetKnobs, k_paramVlMllRes, 0, 0),
              "VlMllRes is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramTone, 0, 0),
              "Tone is not loaded per preset: every row must ship 0");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramCutoff, 1999, 1999),
              "Cutoff is not loaded per preset: every row must ship 1999");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramResnc, 71, 71),
              "Resnc is not loaded per preset: every row must ship 71");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramDkay, 0, 200),
              "Dkay is 0..200: every consumer saturates at 200");
static_assert(brc::preset_column_in(kPresetKnobs, k_paramPartls, 0, 7),
              "Partls (the bank density) is 0..7");

// ── Inharmonic anchor tables (ported from cymbal_synthesis) ─────────────────
static const float kCrashHz[16] = {
    343.f, 421.f, 512.f, 731.f, 973.f, 1289.f, 1627.f, 2143.f,
    2879.f, 3659.f, 4721.f, 6121.f, 7841.f, 10061.f, 13109.f, 16987.f };
static const float kRideHz[16] = {
    481.f, 603.f, 759.f, 1013.f, 1349.f, 1811.f, 2399.f, 3191.f,
    4211.f, 5603.f, 7421.f, 9829.f, 12113.f, 14591.f, 16879.f, 18193.f };
static const float kSplashHz[14] = {
    1123.f, 1409.f, 1801.f, 2251.f, 2749.f, 3407.f, 4211.f, 5233.f,
    6473.f, 7963.f, 9787.f, 12043.f, 14813.f, 17041.f };
static const float kGongHz[16] = {
    162.f, 219.f, 287.f, 361.f, 452.f, 579.f, 733.f, 911.f,
    1139.f, 1451.f, 1847.f, 2381.f, 3097.f, 4181.f, 5741.f, 7919.f };
// Hi-hat: everything above ~3.2 kHz with heavy jitter reads as a bright
// metallic noise continuum — a closed-hat "tick" sizzle, no low pitched body.
static const float kHiHatHz[16] = {
    3271.f, 3907.f, 4523.f, 5219.f, 6007.f, 6907.f, 7949.f, 9133.f,
    10501.f, 12073.f, 13879.f, 15959.f, 17041.f, 18353.f, 19141.f, 19709.f };

// ── How each preset strikes ─────────────────────────────────────────────────
struct Strike {
    CymbalConfig config;      // before the knobs move it
    int          ref_note;    // the root the config is voiced at (Pitch transposes from it)
    float        ring_scale;  // shortens / lengthens the whole ring
    float        level;       // output make-up gain (see below)
};

// Config columns:
//   anchors, count, resonators at 100 % Partls | minHz, maxHz, jitter (semitones)
//   lowAttack, decay, highAttack, highDecay (s) | thwack (s), stick, noise,
//   resonator, shimmer, direct noise, phase-mod depth | hfTilt
//
// Bank sizes are trimmed from the prototype's 112/104/96 for the CPU budget;
// the level is independent of the count (the bank is normalised by 1/sqrt(N)).
//
// hfTilt (body brightness): under the pink (−3 dB/oct) driver a flat-gain
// bank makes Cymbal / Ride / RidBel read dark and tonal (~1-1.4 kHz body
// centroid against ~6-7 kHz in the reference recordings).  1.0 counters the
// driver tilt exactly, but the thwack burst and the swept one-pole park extra
// red energy in the drive (measured: tilt 1.0 only moved the body 0.9 →
// 1.5 kHz; 2.0 → 3.3 kHz), so they are over-whitened; the crash most, its
// anchors start lower (343 Hz) so the red bottom weighs more.  HHat-O and
// Splash keep the flat bank: they are approved on hardware as they are.
//
// level: measured over a common 250 ms window, the cymbals sit well under a
// struck drum at the same velocity (Cymbal -18.3 dB, Ride -17.3, HHat-O
// -17.9, RidBel -14.7, Splash -11.3, Gong +3.6, against the mean of the
// original 40-preset brachetti kit), so each gets a straight make-up gain on
// velocityGain, the voice's ONLY uniform output scaler (stickLevel feeds both
// the bank drive and the direct tap, so scaling it would land on the thwack
// squared).  HHat-O is equally quiet by measurement, but its level is
// approved on hardware as it is: raise it only on an explicit listen.
static const Strike kStrikes[kNumPresets] = {
    /* k_Cymbal */
    {{kCrashHz, 16, 96, 300.f, 20000.f, 1.8f,
      0.018f, 0.44f, 0.040f, 0.30f, 0.0035f, 0.85f, 0.045f,
      0.154f, 0.30f, 0.020f, 0.10f, 3.0f},
     65, 1.0f, 3.2f},
    /* k_Gong */
    // hfTilt stays 0: the reference recording (Chinese-Gong.wav) measures a
    // 196 Hz power centroid with 0.0 % of its energy above 3 kHz, which the
    // flat bank already matches (sustain centroid: reference 815 Hz, flat
    // bank 728 Hz, tilt 2.4 → 3225 Hz — a different, much brighter gong).
    // What a flat bank misses is the attack (reference 1147 Hz early against
    // 437 rendered), and hfTilt cannot buy it: a static per-lane gain moves
    // both windows together, and nothing in the bank's decay separates them
    // either (see the rejected hfDamp note in voice.h).  Attack brightness
    // has to come from the STRIKE: stickLevel, thwack, the shimmer band.
    //
    // 240 is NOT a bank size: it is the count at 100 % Partls, and the knob
    // spans 25-60 %, so 240 × 0.40 = 96 lanes at the shipped position — the
    // density of the prototype (fewer lanes thin the 300 Hz-1 kHz body).
    // Above Partls 4 the request exceeds kCymbalMaxResonators and flattens
    // at 112: a plateau on the top three positions, accepted because raising
    // the cap costs memory on every voice.
    //
    // level 1.28 is make-up for the subsonic drive guard and the dense bank,
    // which together take 1.4 dB.  1.40 would restore the level exactly, but
    // it pushes the limiter harder and stretches the T60 from 4.99 s to
    // 5.22 s, and length is what this preset can least afford.
    {{kGongHz, 16, 240, 150.f, 14000.f, 2.4f,
      0.25f, 1.70f, 0.50f, 1.20f, 0.020f, 0.32f, 0.035f,
      0.126f, 0.22f, 0.010f, 0.15f, 0.0f},
     50, 1.0f, 1.28f},
    /* k_HiHatOpen */
    // A long CLOSED-hat sizzle ("tick", not "tong"): all anchors above
    // 3.2 kHz with heavy jitter, a short stick tick, a decay longer than a
    // closed chick but far shorter than a crash.  Resonator level 6.5: the
    // pink drive has ~10 dB less energy at these anchors, so the bank needs a
    // much hotter tap than the low-anchor presets.
    {{kHiHatHz, 16, 64, 2800.f, 20000.f, 1.5f,
      0.006f, 0.55f, 0.012f, 0.30f, 0.0015f, 0.60f, 0.045f,
      6.50f, 0.55f, 0.010f, 0.05f, 0.0f},
     79, 0.55f, 1.0f},
    /* k_Ride */
    {{kRideHz, 16, 88, 450.f, 18000.f, 1.2f,
      0.015f, 0.55f, 0.035f, 0.34f, 0.0030f, 0.75f, 0.040f,
      0.52f, 0.34f, 0.016f, 0.08f, 2.0f},
     69, 1.0f, 2.9f},
    /* k_RideBell */
    // The Ride with a stronger, longer stick ping: the bell "tang".
    {{kRideHz, 16, 88, 450.f, 18000.f, 1.2f,
      0.015f, 0.55f, 0.035f, 0.34f, 0.0045f, 0.95f, 0.040f,
      0.52f, 0.34f, 0.016f, 0.08f, 2.0f},
     60, 1.0f, 2.1f},
    /* k_Splash */
    // The prototype's splash: a pitched body from ~1.1 kHz with the sizzle
    // above it and a fast decay ("pssh", not hiss).
    {{kSplashHz, 14, 64, 1000.f, 19000.f, 0.55f,
      0.006f, 0.70f, 0.018f, 0.25f, 0.0020f, 0.75f, 0.038f,
      1.23f, 0.16f, 0.0015f, 0.06f, 0.0f},
     76, 1.0f, 1.4f},
};

// Output trim: gain-stages each preset into the master limiter.  Calibrated
// on the note's BODY, not its strike — the peak over [10 ms, 1 s] at velocity
// 127 on the preset's root, put on a body target (1.5) near the limiter
// threshold — so the limiter catches transients instead of holding whole
// notes at the ceiling.  Clamped to <= 1: it only gives back drive the master
// stage could not use.  Changing a preset's decay or level means
// recalibrating its trim (brachetti/calib_probe.cpp does it).
static const float kOutTrim[kNumPresets] = {
    /* 0  Cymbal  */ 1.00000f,
    /* 1  Gong    */ 0.36908f,
    /* 2  HHat-O  */ 1.00000f,
    /* 3  Ride    */ 1.00000f,
    /* 4  RidBel  */ 1.00000f,
    /* 5  Splash  */ 1.00000f
};

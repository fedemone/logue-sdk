#pragma once
// Brachetti Cymbal — one voice: a dense-resonator cymbal (Dan Stowell-style,
// ported from the cymbal_synthesis prototype).  A large bank of
// exponentially detuned two-pole ringers is driven by band-split noise and a
// stick impulse, with phase modulation gated by the strike envelope for the
// nonlinear shimmer.
//
//   noise ─┬─ low-pass (swept by the low envelope)  ─┐
//          └─ high-pass × shimmer envelope            ├─ drive ─ subsonic guard ─ bank ─┐
//   stick ("thwack") burst ── phase modulation ───────┘                                 │
//   out = bank + direct noise + direct stick ─ rumble filter ─ level ─ fades ─ squelch ─┘
//
// No release by design: the drumlogue fires gate on and off in the same tick,
// so a gate-off must not choke the tail.  A voice ends when its tail falls
// below audibility (or its hard lifetime runs out).
//
// Plain data with default member initialisers (no constructor), so the
// global synth object stays constant-initialised.  The prototype's 32 KB per
// voice comb section is dropped: the bank and the phase modulation already
// give the density the prototype's FDN added.
#include <cstddef>
#include <cstdint>
#include <cmath>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

// From brachetti_common (its README.md indexes every helper):
//   dsp_math.h       clampf, env_mul, kInvSampleRate, kLn0001, kSampleRate,
//                    phase_inc
//   float_math.h     fasterpowf, fastexpf, fastsinf
//   noise.h          PinkFilter, lcg_unit
//   small_filters.h  one_pole_coeff_fast, one_pole_hp, one_pole_lp
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/float_math.h"
#include "../brachetti_common/noise.h"
#include "../brachetti_common/small_filters.h"

enum { kCymbalMaxResonators = 112 };
static_assert(kCymbalMaxResonators % 4 == 0, "kCymbalMaxResonators must be a multiple of 4 for NEON loop safety");

// Corner of the output rumble filter (two cascaded one-poles), an octave and
// a half under the gong's 150 Hz fLo — the lowest anchor in the family — so
// it is inaudible on the instrument and removes only subsonic energy (see
// CymbalVoice::output).
static constexpr float kCymRumbleCoef = brc::phase_inc(40.0f);

// Corner of the two-pole subsonic guard on the resonator DRIVE (see
// CymbalVoice::guard_drive).  It belongs well under the gong's lowest mode:
// the gong's reference recording (Chinese-Gong.wav) has 0.0 % of its energy
// below 60 Hz and 68.8 % in ONE mode at 91.5 Hz.  A corner up at the gong's
// 150 Hz fLo costs it 6 dB on its lowest lane and thins its body; at 55 Hz
// the guard still takes 25 Hz down 15 dB and DC to nothing, and the gong's
// power centroid (201 Hz) sits on the reference's 198.
static constexpr float kCymDriveHpCoef = brc::phase_inc(55.0f);

// Control-rate divider for the driver coefficients and the PM LFOs: 8 samples
// = 6 kHz.  The fastest thing updated there is HHat-O's 12 ms high-band
// attack (576 samples), so a stride of 8 still resolves it into ~72 steps.
// The one-pole coefficients are held piecewise constant (the filter output
// stays continuous, so a stepped cutoff cannot click), while the PM sum IS a
// multiplier and so is linearly interpolated back to sample rate.
enum { kCymCtrlStride = 8 };

// The three phase-modulation LFO rates (Hz before the strike's rate scaling).
// Namespace-scope `static`: a static local of a member function would be
// STB_GNU_UNIQUE (see brachetti_common/knobs.h, name_at).
static const float kPmRates[3] = { 37.0f, 71.0f, 113.0f };

// One preset's strike, as NoteOn hands it to CymbalVoice::strike (by value;
// freqHz points at a preset's anchor table).
struct CymbalConfig {
    const float* freqHz;      // inharmonic anchor frequencies
    uint16_t     freqCount;
    uint16_t     resonators;  // the bank at 100 % density (clamped to kCymbalMaxResonators)
    float minHz, maxHz, jitterSemis;
    float lowAttackSec, decaySec, highAttackSec, highDecaySec;
    float thwackSec, stickLevel, noiseLevel, resonatorLevel, shimmerLevel;
    float directNoiseLevel, phaseModDepth;
    // HF gain tilt (0 = flat bank).  At 1.0 each ringer's tap gain is
    // weighted by sqrt(f / 2 kHz), which exactly counters the pink driver's
    // −3 dB/oct amplitude tilt so the wash reads white-bright like the
    // reference cymbals (~6-7 kHz body centroid) instead of ~1.3 kHz dark.
    float hfTilt;
};

// TRIED AND REJECTED, so it is not re-attempted: a per-lane `hfDamp` giving
// higher lanes a shorter T60 (lane decay scaled by (1 kHz / f)^hfDamp).  The
// theory was that the bank's ONE shared `r` is why a static `hfTilt` cannot
// brighten the strike without also brightening the sustain — real metal
// damps high modes faster.  Measured, it moves the late centroid the WRONG
// WAY (3225 -> 3375 -> 3666 Hz as hfDamp goes 0 -> 0.3 -> 1.0) because these
// resonators are CONTINUOUSLY DRIVEN, not impulse-excited: lowering a lane's
// `r` widens its bandwidth and raises its `b0`, so the lane passes more of
// the broadband driver than it loses in peak gain.  At `hfTilt = 0` it darkens
// the tail by 8 % (728 -> 672 Hz), which does not pay for a `powf`+`expf` per
// lane per strike.  The sustain's brightness comes from the tilted tap being
// fed by a full-band driver, so nothing about the BANK's decay can separate
// attack from sustain.

struct CymbalVoice {
    bool     active       = false;
    uint32_t rng          = 1u;
    uint32_t sampleIndex  = 0u;
    uint32_t endSample    = 0u;   // hard lifetime bound
    float    velocity     = 0.0f;
    float    directNoiseLevel = 0.0f;

    // Per-strike constants, folded once in strike() so process() is
    // branch-light and free of libm.
    float velocityGain  = 0.0f;   // output level (harder = louder)
    float resonatorNorm = 0.0f;   // resonatorLevel / sqrt(count)
    float noiseGain     = 0.0f;
    float whiteBlend    = 0.0f;   // velocity-dependent drive whiteness
    float shimmerScale  = 0.0f;
    float maxCutoff     = 0.0f;
    float pmDepthBase   = 0.0f;
    float pmRateBase    = 0.0f;
    float thwackGain    = 0.0f;
    uint32_t thwackSamples = 0u;
    float thwackTauInv  = 0.0f;

    // Geometric (one-pole) envelopes: value = (1 - atk) · dec.
    float lowAtk = 0.0f, lowAtkMul = 0.0f, lowDec = 0.0f, lowDecMul = 0.0f;
    float hiAtk  = 0.0f, hiAtkMul  = 0.0f, hiDec  = 0.0f, hiDecMul  = 0.0f;
    float strikeAtk = 0.0f, strikeAtkMul = 0.0f, strikeDec = 0.0f, strikeDecMul = 0.0f;

    // The resonator bank, structure-of-arrays (NEON-friendly; the count is
    // padded to a multiple of 4 with silent lanes).
    uint16_t resCount = 0u;
    float resB0[kCymbalMaxResonators]   = {};
    float resA1[kCymbalMaxResonators]   = {};
    float resA2[kCymbalMaxResonators]   = {};
    float resY1[kCymbalMaxResonators]   = {};
    float resY2[kCymbalMaxResonators]   = {};
    float resGain[kCymbalMaxResonators] = {};

    float lpState = 0.0f, hpLowState = 0.0f, dcState = 0.0f, dcState2 = 0.0f;
    float driveHp1 = 0.0f, driveHp2 = 0.0f;
    brc::PinkFilter pinkNoise;
    float pmPhase[3]   = {};

    // Control-rate cache: the two driver one-poles' coefficients (each a
    // fastexpf) and the three PM LFOs (a fastsinf each) were ~57 % of a
    // voice's fixed per-sample cost, for quantities whose fastest time
    // constant is 12 ms.
    uint16_t ctrlCount = 0u;                  // samples left until the next update
    float    lpCoeff = 0.0f, hpCoeff = 0.0f;  // cached one-pole coefficients
    float    pmVal   = 0.0f, pmStep  = 0.0f;  // PM sum, linearly interpolated

    // Panic fade: fadeMul stays 1.0 in normal play; AllNoteOff sets it < 1 for
    // a click-free fade-out (the only way to stop a cymbal early).
    float fade = 1.0f, fadeMul = 1.0f;

    // Output level follower for the CPU squelch: a voice whose tail has dropped
    // below audibility releases its bank instead of grinding through it until
    // endSample (full banks running through long gong tails overload the
    // drumlogue).
    float magEnv = 0.0f;

    // The seed the ringing bank was built from.  A re-strike must rebuild the
    // SAME frequencies, or every lane it keeps ringing is retuned under its own
    // vibration (NoteOn's seed folds in the MIDI velocity, so two strikes of
    // different force would hand the same plate two different detune sets).
    uint32_t bankSeed = 0u;

    // ── The strike ──────────────────────────────────────────────────────────
    // ring_scale shortens / lengthens the whole ring (open hat vs crash);
    // pitch_ratio transposes the whole anchor spectrum (a smaller or larger
    // cymbal, not a filter).
    //
    // retain_ring: the voice is still ringing and is struck again (or its
    // slot stolen).  Metal does not reset: the old vibration, the driver
    // envelopes and the noise bed carry across the strike, so restrikes ADD
    // to what is sounding.  A fresh voice always starts from silence.
    void strike(const CymbalConfig& cfg, float velocity_, float ring_scale,
                float pitch_ratio, uint32_t seed, bool retain_ring) {
        const float sr = brc::kSampleRate;
        // Up to 2.0: the Velocity knob's "wham" can hand a strike hotter than
        // MIDI 127 (up to 1.30); every formula below is monotone and bounded
        // past 1.0.  2.0 only guards against garbage.
        velocity = brc::clampf(velocity_, 0.01f, 2.0f);
        // Where the two noise drivers are RIGHT NOW (0 on a fresh voice).
        const float low_held  = retain_ring ? (1.0f - lowAtk) * lowDec : 0.0f;
        const float high_held = retain_ring ? (1.0f - hiAtk)  * hiDec  : 0.0f;
        if (!retain_ring) bankSeed = seed ? seed : 0x12345678u;
        rng = bankSeed;
        sampleIndex = 0u;
        active = true;
        fade = 1.0f;
        fadeMul = 1.0f;
        // Driver filter and noise state survive a re-strike for the same
        // reason the resonators do: they are the wash that is still sounding.
        // Zeroing them silences the noise bed for a full driver attack (0.25 s
        // on the gong) and steps the DC blocker.
        if (!retain_ring) {
            lpState = hpLowState = dcState = dcState2 = 0.0f;
            driveHp1 = driveHp2 = 0.0f;
            pmVal = pmStep = 0.0f;
            pinkNoise.reset();
        }
        // ctrlCount 0: the first process() sample takes the control-rate
        // branch, so the cached coefficients are valid before they are read.
        ctrlCount = 0u;
        lpCoeff = hpCoeff = 0.0f;
        directNoiseLevel = cfg.directNoiseLevel;

        velocityGain = 0.25f + 0.75f * velocity * sqrtf(velocity);

        const float decay      = cfg.decaySec     * ring_scale * (0.45f + 0.70f * velocity);
        const float high_decay = cfg.highDecaySec * ring_scale * (0.5f  + 0.70f * velocity);
        // A fresh strike starts each envelope at 0 and blooms over its attack.
        // A RE-strike starts where the voice already is: dec back to 1 (the
        // strike refills the reservoir) and atk placed so the envelope is
        // exactly continuous across the strike.
        lowAtk = 1.0f - fminf(1.0f, low_held);   lowDec = 1.0f;
        hiAtk  = 1.0f - fminf(1.0f, high_held);  hiDec  = 1.0f;
        lowAtkMul = brc::env_mul(cfg.lowAttackSec, sr);
        lowDecMul = brc::env_mul(decay, sr);
        hiAtkMul  = brc::env_mul(cfg.highAttackSec, sr);
        hiDecMul  = brc::env_mul(high_decay, sr);

        const float strike_decay = fmaxf(0.04f, cfg.highDecaySec * (0.5f + 1.1f * velocity));
        strikeAtk = strikeDec = 1.0f;
        strikeAtkMul = brc::env_mul(0.0008f, sr);
        strikeDecMul = brc::env_mul(strike_decay, sr);

        noiseGain     = cfg.noiseLevel;
        whiteBlend    = 0.08f + (0.45f - 0.08f) * (velocity * velocity);
        shimmerScale  = cfg.shimmerLevel * velocity;
        maxCutoff     = 6000.0f + 14000.0f * velocity;
        pmDepthBase   = cfg.phaseModDepth;
        pmRateBase    = 0.7f + 0.9f * velocity;
        thwackGain    = cfg.stickLevel * velocity;
        thwackSamples = (uint32_t)(cfg.thwackSec * sr);
        thwackTauInv  = 1.0f / (cfg.thwackSec - 0.001f + 0.000001f);
        // Hard lifetime bound (the squelch in output() normally fires first):
        // without one a gong voice stayed active ~15 s.
        const uint32_t es = (uint32_t)(decay * sr * 8.0f) + (uint32_t)(0.05f * sr);
        const uint32_t es_max = (uint32_t)(8.0f * sr);
        endSample = (es > es_max) ? es_max : es;
        magEnv = 0.0f;

        build_bank(cfg, ring_scale, pitch_ratio, retain_ring);
        for (int i = 0; i < 3; ++i) pmPhase[i] = frand();
    }

    // ── Per sample ──────────────────────────────────────────────────────────
    // Zero libm calls: fastsinf for the PM LFOs, fastexpf for the swept
    // one-pole coefficients and the short stick burst.
    float process() {
        if (!active) return 0.0f;
        const Envelopes e = advance_envelopes();
        if (ctrlCount == 0u) update_control(e);
        --ctrlCount;

        const float wn = frand() * 2.0f - 1.0f;
        const float noise = pinkNoise.process(wn) * (1.0f - whiteBlend) + wn * whiteBlend;
        const float lo_driver = brc::one_pole_lp(noise * noiseGain, lpCoeff, lpState) * e.low;
        const float hi_driver = brc::one_pole_hp(noise * noiseGain, hpCoeff, hpLowState) * (0.18f * e.shimmer);
        const float thwack = stick();

        // Phase modulation, interpolated toward the control-rate target.
        pmVal += pmStep;
        const float pm = pmVal;
        const float pm_depth = pmDepthBase * e.strike;
        const float pm_gain  = 1.0f + pm_depth * pm * 0.22f;
        const float pm_exciter = pm_depth * 0.035f * pm;
        const float drive = guard_drive((lo_driver + hi_driver + thwack + pm_exciter) * pm_gain);

        const float res = ring_bank(drive) * resonatorNorm;
        return output(res, lo_driver, thwack);
    }

private:
    struct Envelopes { float low, high, shimmer, strike; };

    // The voice's random source (the bank's detune, the noise drivers, the
    // PM phases): one stream, so a seed replays a strike exactly.
    float frand() { return brc::lcg_unit(rng); }

    // The resonator bank for one strike.  Exact expf / cosf (r ≈ 0.9999).
    // Lanes below `keep` carry their vibration across a re-strike, at 0.75 so
    // the incoming strike adds energy without slamming the output clamp;
    // everything else starts from rest.
    void build_bank(const CymbalConfig& cfg, float ring_scale, float pitch_ratio, bool retain_ring) {
        const float sr = brc::kSampleRate;
        const uint16_t keep = retain_ring ? resCount : 0u;
        const float keep_gain = 0.75f;
        const uint16_t count = (cfg.resonators > (uint16_t)kCymbalMaxResonators)
                                   ? (uint16_t)kCymbalMaxResonators : cfg.resonators;
        const float ring_decay = fmaxf(0.08f, (0.55f + 0.60f * velocity) * ring_scale);
        const float r  = expf(brc::kLn0001 / (ring_decay * sr));
        const float b0 = sqrtf(1.0f - r * r);
        const float r2 = -(r * r);
        const float pr = brc::clampf(pitch_ratio, 0.25f, 4.0f);
        // Only the LOW clamp follows a downward transpose; the high clamp is an
        // absolute Nyquist guard.  Scaling the top clamp down piles every high
        // mode onto one frequency and kills the transpose (measured: -12
        // semitones moved the centroid by only ×0.87 instead of ×0.5).
        const float f_lo = cfg.minHz * fminf(pr, 1.0f);
        const float f_hi = 20000.0f;
        for (uint16_t i = 0u; i < count; ++i) {
            const float anchor = cfg.freqHz[i % cfg.freqCount];
            const float detune = (frand() * 2.0f - 1.0f) * cfg.jitterSemis;
            const float octave = (float)(i / cfg.freqCount);
            float f = anchor * pr * exp2f(detune * (1.0f / 12.0f)) * exp2f(octave * 0.17f);
            f = brc::clampf(f, f_lo, f_hi);
            const float w = brc::phase_inc(f);
            resB0[i] = b0;
            resA1[i] = 2.0f * r * cosf(w);
            resA2[i] = r2;
            if (i < keep) {
                resY1[i] *= keep_gain;
                resY2[i] *= keep_gain;
            } else {
                resY1[i] = 0.0f;
                resY2[i] = 0.0f;
            }
            float g = 0.85f + 0.30f * frand();
            // sqrt(f / 2 kHz)^hfTilt HF weighting (see CymbalConfig).
            // fasterpowf is fine: an amplitude weight, at strike time, with an
            // argument in [0.15, 10].
            if (cfg.hfTilt > 0.0f) g *= fasterpowf(f * 0.0005f, 0.5f * cfg.hfTilt);
            resGain[i] = g;
        }
        resonatorNorm = cfg.resonatorLevel / sqrtf((float)count);
        const uint16_t padded = (uint16_t)((count + 3u) & ~3u);
        for (uint16_t i = count; i < padded && i < (uint16_t)kCymbalMaxResonators; ++i)
            resB0[i] = resA1[i] = resA2[i] = resY1[i] = resY2[i] = resGain[i] = 0.0f;
        resCount = padded;
    }

    Envelopes advance_envelopes() {
        lowAtk *= lowAtkMul;  lowDec *= lowDecMul;
        hiAtk  *= hiAtkMul;   hiDec  *= hiDecMul;
        strikeAtk *= strikeAtkMul; strikeDec *= strikeDecMul;
        Envelopes e;
        e.low     = (1.0f - lowAtk) * lowDec;
        e.high    = (1.0f - hiAtk)  * hiDec;
        e.shimmer = e.high * shimmerScale;
        e.strike  = velocity * (1.0f - strikeAtk) * strikeDec;
        return e;
    }

    // Every kCymCtrlStride samples: the swept driver cutoffs, and the three PM
    // phases advanced a whole stride at a time (pmVal is aimed at the new sum
    // so the per-sample path is a single add).  Measured on the host: this
    // removes ~19 of the ~37 ns/sample/voice of FIXED cost (the
    // kVoiceFixedLanes part of the CPU budget in synth_engine.h).
    void update_control(const Envelopes& e) {
        ctrlCount = (uint16_t)kCymCtrlStride;
        lpCoeff = brc::one_pole_coeff_fast(10.0f + maxCutoff * e.low);
        hpCoeff = brc::one_pole_coeff_fast(10001.0f - 10000.0f * e.high);

        const float strike_e  = velocity * (1.0f - strikeAtk) * strikeDec;
        const float rate_step = pmRateBase * (1.0f + 0.6f * strike_e) *
                                brc::kInvSampleRate * (float)kCymCtrlStride;
        float pm_next = 0.0f;
        for (int i = 0; i < 3; ++i) {
            pmPhase[i] += kPmRates[i] * rate_step;
            if (pmPhase[i] >= 1.0f) pmPhase[i] -= (float)(int)pmPhase[i];
            const float ph = (pmPhase[i] > 0.5f) ? (pmPhase[i] - 1.0f) : pmPhase[i];
            pm_next += fastsinf(6.28318530717958647692f * ph);
        }
        pmStep = (pm_next - pmVal) * (1.0f / (float)kCymCtrlStride);
    }

    // The stick's contact burst ("thwack"): a 1 ms ramp, then an exponential.
    float stick() const {
        if (sampleIndex >= thwackSamples) return 0.0f;
        const float t = sampleIndex * brc::kInvSampleRate;
        const float env = (t < 0.001f) ? (t * 1000.0f) : fastexpf(-(t - 0.001f) * thwackTauInv);
        return env * thwackGain;
    }

    // ── Subsonic guard on the bank drive ────────────────────────────────────
    // A 2-pole resonator with a constant numerator has a DC gain of
    // b0/(1-a1-a2), which for the lowest lane of a gong (150 Hz, r = 0.99987)
    // is 42 — while its gain AT resonance is 4313.  So drive energy near DC is
    // only ~40 dB down on drive energy at the mode itself, and everything that
    // reaches this line has some: the thwack is a UNIPOLAR contact burst (20 ms
    // on the gong, so nearly all of its energy is under 50 Hz) and the low
    // driver is pink noise, which has more energy in 10-25 Hz than in
    // 120-200 Hz.  Each strike therefore parks a lump of sub-mode energy in the
    // lanes with the most gain, and it rings for a second — so it PILES UP
    // across repeated strikes.  Without the guard, energy under 100 Hz for ONE
    // strike against EIGHT 300 ms apart: RidBel 2.7 % -> 25.0 %, Cymbal
    // 1.7 % -> 13.5 %, Ride 2.5 % -> 9.7 %, Gong 5.6 % -> 9.1 %.  It is
    // inaudible as pitch and it eats the master limiter, so everything that IS
    // audible gets quieter and duller with every hit.
    //
    // Two cascaded one-poles at 55 Hz: DC is removed outright, 25 Hz is
    // -15 dB and 60 Hz -5 dB, while the lowest anchor any preset uses (the
    // gong's 150 Hz) pays about 1 dB and the crash's 343 Hz under 0.3 dB.
    float guard_drive(float drive) {
        drive = brc::one_pole_hp(drive, kCymDriveHpCoef, driveHp1);
        return brc::one_pole_hp(drive, kCymDriveHpCoef, driveHp2);
    }

    // One sample of the bank: Σ gain_i · y_i.
    float ring_bank(float drive) {
        float res = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        // 2× unrolled with two independent accumulator / MAC chains: the
        // Cortex-A7's in-order NEON pipe stalls on back-to-back dependent
        // vmlaq; interleaving the two chains hides that latency.
        const float32x4_t vdrive = vdupq_n_f32(drive);
        float32x4_t vsum0 = vdupq_n_f32(0.0f);
        float32x4_t vsum1 = vdupq_n_f32(0.0f);
        uint16_t i = 0u;
        for (; i + 8u <= resCount; i += 8u) {
            const float32x4_t y1a = vld1q_f32(resY1 + i);
            const float32x4_t y1b = vld1q_f32(resY1 + i + 4);
            float32x4_t ya = vmulq_f32(vld1q_f32(resB0 + i), vdrive);
            float32x4_t yb = vmulq_f32(vld1q_f32(resB0 + i + 4), vdrive);
            ya = vmlaq_f32(ya, vld1q_f32(resA1 + i), y1a);
            yb = vmlaq_f32(yb, vld1q_f32(resA1 + i + 4), y1b);
            ya = vmlaq_f32(ya, vld1q_f32(resA2 + i), vld1q_f32(resY2 + i));
            yb = vmlaq_f32(yb, vld1q_f32(resA2 + i + 4), vld1q_f32(resY2 + i + 4));
            vst1q_f32(resY2 + i, y1a);
            vst1q_f32(resY2 + i + 4, y1b);
            vst1q_f32(resY1 + i, ya);
            vst1q_f32(resY1 + i + 4, yb);
            vsum0 = vmlaq_f32(vsum0, ya, vld1q_f32(resGain + i));
            vsum1 = vmlaq_f32(vsum1, yb, vld1q_f32(resGain + i + 4));
        }
        for (; i < resCount; i += 4u) {   // the count is a multiple of 4, not 8
            const float32x4_t y1 = vld1q_f32(resY1 + i);
            float32x4_t y = vmulq_f32(vld1q_f32(resB0 + i), vdrive);
            y = vmlaq_f32(y, vld1q_f32(resA1 + i), y1);
            y = vmlaq_f32(y, vld1q_f32(resA2 + i), vld1q_f32(resY2 + i));
            vst1q_f32(resY2 + i, y1);
            vst1q_f32(resY1 + i, y);
            vsum0 = vmlaq_f32(vsum0, y, vld1q_f32(resGain + i));
        }
        vsum0 = vaddq_f32(vsum0, vsum1);
        float32x2_t vs = vadd_f32(vget_low_f32(vsum0), vget_high_f32(vsum0));
        vs = vpadd_f32(vs, vs);
        res = vget_lane_f32(vs, 0);
#else
        for (uint16_t i = 0u; i < resCount; ++i) {
            const float y = resB0[i] * drive + resA1[i] * resY1[i] + resA2[i] * resY2[i];
            resY2[i] = resY1[i];
            resY1[i] = y;
            res += y * resGain[i];
        }
#endif
        return res;
    }

    // The bank plus the direct taps, the rumble filter, the level, the panic
    // fade, the squelch and the lifetime bound.
    float output(float res, float lo_driver, float thwack) {
        float out = res + lo_driver * directNoiseLevel + thwack * 0.12f;   // the stick's "tang"
        // Rumble filter: two things reach the output BELOW the lowest
        // resonator and neither is a cymbal — the direct low-driver tap
        // (low-passed PINK noise, with more energy in 10-25 Hz than in
        // 120-200 Hz) and, on repeated strikes, the pulse train the 20 ms stick
        // burst becomes.  Both bypass the drive guard.  With a 7.6 Hz corner, a
        // gong struck 8 times 300 ms apart puts 12 % of the passage energy
        // under 25 Hz and 17 % under 100 Hz; with these two 40 Hz poles, 0.4 %
        // and 2.5 %.  -16 dB at 17 Hz against -0.6 dB at the gong's 150 Hz.
        out = brc::one_pole_hp(out, kCymRumbleCoef, dcState);
        out = brc::one_pole_hp(out, kCymRumbleCoef, dcState2) * 0.9f;
        out *= velocityGain;

        out *= fade;   // panic fade (fadeMul < 1 only after AllNoteOff)
        fade *= fadeMul;
        if (fade < 0.001f) active = false;

        // Release the voice once the tail is inaudible (-66 dB over ~10 ms).
        // The 0.5 s guard protects slow attacks (the gong's 0.25 s).
        brc::one_pole_lp(fabsf(out), 0.002f, magEnv);
        if (sampleIndex > (uint32_t)(0.5f * brc::kSampleRate) && magEnv < 0.0004f) active = false;

        if (++sampleIndex > endSample) active = false;
        return brc::clampf(out, -1.0f, 1.0f);
    }
};

// The voice: one dense-resonator cymbal plus the per-voice output state.
struct VoiceState {
    bool    is_active        = false;
    uint8_t current_note     = 60;
    float   current_velocity = 0.0f;

    // Preset-change fade: a voice cannot survive a preset change, so it fades
    // out over ~10 ms and is then reclaimed.  fade_mul is exactly 1.0f in
    // normal play, where the fade is skipped.
    float fade     = 1.0f;
    float fade_mul = 1.0f;   // < 1 only while fading out

    CymbalVoice cymbal;

    float tone_lp = 0.0f;    // Tone tilt EQ memory

    void reset_for_strike() {
        fade     = 1.0f;     // a re-struck slot is no longer fading
        fade_mul = 1.0f;
        tone_lp  = 0.0f;
        cymbal.active = false;
    }

    bool fading() const { return is_active && fade_mul < 1.0f; }

    float fade_out(float x) {
        if (fade_mul < 1.0f) {
            x *= fade;
            fade *= fade_mul;
            if (fade < 0.001f) { is_active = false; cymbal.active = false; }
        }
        return x;
    }
};

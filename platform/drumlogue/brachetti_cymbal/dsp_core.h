#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include "filter.h"
#include "float_math.h"

/** Because we are optimizing for bare-metal, notice there are no virtual functions,
 * no dynamic memory (new/malloc), and no deep class hierarchies.
 * Just pure data that the CPU cache can read sequentially. */

constexpr float k_dsp_sample_rate     = 48000.0f;
constexpr float k_dsp_inv_sample_rate = 1.0f / 48000.0f;
constexpr float k_dsp_log_0001        = -6.907755279f;

// ============================================================================
// Cymbal engine (ENGINE_CYMBAL) — ported from cymbal_synthesis/realistic_cymbals
// ============================================================================
// A Dan Stowell-style dense inharmonic resonator cymbal: a large bank of
// exponentially-detuned 2-pole ringers driven by band-split noise + a stick
// impulse, with strike-envelope-gated phase modulation for the nonlinear
// shimmer.  This REPLACES the old ENGINE_PLATE crash-bank + FDN + self-PM for
// the metallic cymbal-family presets.
//
// DMI-only: no user-defined constructor (all members have constant default
// initialisers) so the containing `s_synth` stays constant-initialised in
// .data, exactly like the rest of dsp_core.h.  The prototype's 32 KB/voice
// comb section is DROPPED — the resonator bank + PM already supplies the
// density the FDN used to add, so the comb is not needed and would blow the
// per-voice RAM budget.
enum { kCymbalMaxResonators = 112 };
// Corner of the cymbal output rumble filter (two cascaded one-poles).  40 Hz
// sits an octave and a half under the gong's 150 Hz fLo — the lowest anchor
// anywhere in the family — so the filter is inaudible on the instrument and
// removes only the subsonic energy documented at its use site.
static constexpr float kCymRumbleHz   = 40.0f;
static constexpr float kCymRumbleCoef = 6.28318530717958647692f * kCymRumbleHz *
                                        k_dsp_inv_sample_rate;
// Corner of the two-pole subsonic guard on the resonator DRIVE — see the long
// note at its use site in cymbal_process.
//
// Pass 45 set this to 150 Hz, "the lowest anchor frequency in the whole
// family", which sounded principled and was the worst possible choice for the
// one preset that reaches that low: it put the corner ON the gong's own fLo,
// so the gong paid 6 dB on its lowest lane while every other preset paid under
// 1.5 dB.  Pass 46 measured the actual reference recording — Chinese-Gong.wav
// has 0.0 % of its energy below 60 Hz and 68.8 % in ONE mode at 91.5 Hz — so
// the thing to remove sits well under 60 Hz and the corner belongs there, not
// at 150.  At 55 Hz the guard still takes 25 Hz down 15 dB and DC to nothing,
// while the gong's body comes back: measured power centroid 225 -> 201 Hz
// against the reference's 198, the 100-300 Hz share 89.2 % -> 93.2 %, and the
// preset gets 1.7 dB louder instead of thinner.
static constexpr float kCymDriveHpHz   = 55.0f;
static constexpr float kCymDriveHpCoef = 6.28318530717958647692f * kCymDriveHpHz *
                                         k_dsp_inv_sample_rate;
static_assert(kCymbalMaxResonators % 4 == 0, "kCymbalMaxResonators must be a multiple of 4 for NEON loop safety");

// Parameter bundle passed by value from NoteOn (never stored persistently, so
// no member-pointer-in-.data hazard).  freqHz points at a member array of the
// synth class (constant-init in .data); the pointer itself lives only on the
// stack for the duration of cymbal_note_on.
struct CymbalConfig {
    const float* freqHz;      // inharmonic anchor frequencies
    uint16_t     freqCount;
    uint16_t     resonators;  // clamped to kCymbalMaxResonators
    float minHz, maxHz, jitterSemis;
    float lowAttackSec, decaySec, highAttackSec, highDecaySec;
    float thwackSec, stickLevel, noiseLevel, resonatorLevel, shimmerLevel;
    float directNoiseLevel, phaseModDepth;
    // HF gain tilt (0 = legacy flat bank).  At 1.0 each ringer's tap gain is
    // weighted by sqrt(f / 2 kHz), which exactly counters the pink driver's
    // −3 dB/oct amplitude tilt so the wash reads white-bright like the
    // reference cymbals (~6-7 kHz body centroid) instead of ~1.3 kHz dark.
    // Left 0 for presets whose balance is HW-approved (HHat-O, Splash);
    // aggregate initializers with 17 values default this to 0.
    float hfTilt;
};

// TRIED AND REJECTED (pass 46), so it is not re-attempted: a per-lane
// `hfDamp` giving higher lanes a shorter T60 (lane decay scaled by
// (1 kHz / f)^hfDamp).  The theory was that the bank's ONE shared `r` is why
// a static `hfTilt` cannot brighten the strike without also brightening the
// sustain — real metal damps high modes faster, and every `ModalPresetConfig`
// in this unit already gives descending per-mode T60s.  Measured, it moves the
// late centroid the WRONG WAY (3225 -> 3375 -> 3666 Hz as hfDamp goes
// 0 -> 0.3 -> 1.0) because these resonators are CONTINUOUSLY DRIVEN, not
// impulse-excited: lowering a lane's `r` widens its bandwidth and raises its
// `b0`, so the lane passes more of the broadband driver than it loses in peak
// gain.  At `hfTilt = 0` it darkens the tail by 8 % (728 -> 672 Hz), which
// does not pay for a `powf`+`expf` per lane per strike.  The sustain's
// brightness comes from the tilted tap being fed by a full-band driver, so
// nothing about the BANK's decay can separate attack from sustain.

struct CymbalVoice {
    bool     active       = false;
    uint32_t rng          = 1u;
    uint32_t sampleIndex  = 0u;
    uint32_t endSample    = 0u;
    float    velocity     = 0.0f;
    float    directNoiseLevel = 0.0f;

    // Per-note constants (folded once in cymbal_note_on so process() is
    // branch-light and free of libm in steady state).
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

    // Geometric (one-pole) envelope recursions: value = (1-atk)*dec.
    float lowAtk = 0.0f, lowAtkMul = 0.0f, lowDec = 0.0f, lowDecMul = 0.0f;
    float hiAtk  = 0.0f, hiAtkMul  = 0.0f, hiDec  = 0.0f, hiDecMul  = 0.0f;
    float strikeAtk = 0.0f, strikeAtkMul = 0.0f, strikeDec = 0.0f, strikeDecMul = 0.0f;

    // Resonator bank in structure-of-arrays layout (NEON-friendly, count padded
    // to a multiple of 4 with silent lanes).
    uint16_t resCount = 0u;
    float resB0[kCymbalMaxResonators]   = {};
    float resA1[kCymbalMaxResonators]   = {};
    float resA2[kCymbalMaxResonators]   = {};
    float resY1[kCymbalMaxResonators]   = {};
    float resY2[kCymbalMaxResonators]   = {};
    float resGain[kCymbalMaxResonators] = {};

    float lpState = 0.0f, hpLowState = 0.0f, dcState = 0.0f, dcState2 = 0.0f;
    float driveHp1 = 0.0f, driveHp2 = 0.0f;
    float pinkState[7] = {};
    float pmPhase[3]   = {};

    // Control-rate cache (see kCymCtrlStride).  The two driver one-poles used
    // to recompute their coefficient from a fastexpf EVERY sample, and the
    // three PM LFOs a fastsinf each — together ~57 % of a cymbal voice's fixed
    // per-sample cost, for quantities whose fastest time constant is 12 ms.
    uint16_t ctrlCount = 0u;                  // samples left until the next update
    float    lpCoeff = 0.0f, hpCoeff = 0.0f;  // cached one-pole coefficients
    float    pmVal   = 0.0f, pmStep  = 0.0f;  // PM sum, linearly interpolated

    // Panic fade (AllNoteOff): the cymbal has no release envelope by design
    // (gate_off must not choke the tail on the Drumlogue), so the only way to
    // stop it early is a click-free output ramp.  fadeMul stays 1.0 in normal
    // play; AllNoteOff sets it < 1 for a ~15 ms fade-out.
    float fade = 1.0f, fadeMul = 1.0f;

    // Output magnitude envelope for the CPU squelch: a voice whose tail has
    // dropped below audibility must release its resonator bank instead of
    // grinding through it until endSample (4 voices x full banks over long
    // gong-length tails overloaded the target: HW audio crash).
    float magEnv = 0.0f;

    // The seed the ringing bank was built from.  A re-strike (retainRing) must
    // rebuild the SAME frequencies, or every lane it keeps ringing is retuned
    // under its own vibration: NoteOn's seed folds in the MIDI velocity, so two
    // strikes of different force used to hand the same plate two different
    // detune sets.  Kept per voice rather than recomputed because only the
    // voice knows which strike built the bank it is still ringing with.
    uint32_t bankSeed = 0u;
};

// --- cymbal helpers (all noteOn-time except where marked per-sample) ---------
static inline float cym_frand(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return ((s >> 8) * (1.0f / 16777216.0f));
}

// Per-sample geometric-envelope multiplier for time constant tauSec.  Exact
// expf: the argument is ~-1e-4..-1e-2 where float_math.h's fast* variants are
// catastrophically wrong for the resulting T60 (see CLAUDE.md gotchas).
static inline float cym_env_mul(float tauSec, float sr) {
    if (tauSec < 0.00002f) return 0.0f;
    return expf(-1.0f / (tauSec * sr));
}

// One strike.  ringDecayScale shortens/lengthens the whole ring (e.g. open-hat
// vs crash); phaseModAmount is the 0..1 nonlinear-shimmer amount; pitchRatio
// transposes the whole anchor spectrum (2^(semitones/12) relative to the
// preset's shipped default note — a smaller/larger cymbal, not a filter).
//
// retainRing: the voice is still audibly ringing and is being struck again (or
// its slot stolen).  Metal does not reset — the old vibration keeps decaying
// under the new bank instead of being zeroed, so restrikes and steals add to
// what is already sounding rather than cutting it off.  Only meaningful on a
// re-strike; a fresh voice always starts from silence, so the shipped
// single-hit renders are unaffected.
static inline void cymbal_note_on(CymbalVoice& c, const CymbalConfig& cfg,
                                  float velocity, float ringDecayScale,
                                  float phaseModAmount, float pitchRatio,
                                  uint32_t seed, bool retainRing = false) {
    const float sr = k_dsp_sample_rate;
    // The old ceiling of 1.0 made the Velocity knob's "wham" half dead on the
    // whole cymbal family: NoteOn can now hand this a strike hotter than a
    // MIDI 127 (bounded by BrachettiSynth::kVelWhamMax = 1.30), and a hard
    // 1.0 clamp here discarded exactly that.  2.0 is a sanity bound on garbage
    // input, not a voicing decision — every formula below is monotone and
    // bounded past 1.0 (velocityGain grows, decays lengthen, and maxCutoff is
    // re-clamped to 0.45·sr by cym_pole_coeff).
    c.velocity = fmaxf(0.01f, fminf(2.0f, velocity));
    // Level the two noise drivers are sitting at RIGHT NOW, read before the
    // envelope state below is overwritten.  Zero on a fresh voice.
    const float lowEnvHeld  = retainRing ? (1.0f - c.lowAtk) * c.lowDec : 0.0f;
    const float highEnvHeld = retainRing ? (1.0f - c.hiAtk)  * c.hiDec  : 0.0f;
    // A re-strike rebuilds the bank from the seed the RINGING bank was built
    // from, so the lanes it keeps alive below stay on their own frequencies.
    if (!retainRing) c.bankSeed = seed ? seed : 0x12345678u;
    c.rng = c.bankSeed;
    c.sampleIndex = 0u;
    c.active = true;
    c.fade = 1.0f;
    c.fadeMul = 1.0f;
    // Driver filter/noise state survives a re-strike for the same reason the
    // resonators do: these are the wash that is still sounding.  Zeroing them
    // silences the noise bed for a full driver attack (0.25 s on the gong) and
    // steps the DC blocker, which is most of why a re-struck cymbal used to
    // read as "the previous hit disappeared".
    if (!retainRing) {
        c.lpState = c.hpLowState = c.dcState = c.dcState2 = 0.0f;
        c.driveHp1 = c.driveHp2 = 0.0f;
        c.pmVal = c.pmStep = 0.0f;
        for (int i = 0; i < 7; ++i) c.pinkState[i] = 0.0f;
    }
    // ctrlCount 0 makes the first process() sample take the control-rate
    // branch, so lpCoeff/hpCoeff are always valid before they are read.
    c.ctrlCount = 0u;
    c.lpCoeff = c.hpCoeff = 0.0f;
    c.directNoiseLevel = cfg.directNoiseLevel;

    c.velocityGain = 0.25f + 0.75f * c.velocity * sqrtf(c.velocity);

    // Driver envelope time constants (constant for the note).
    const float decay     = cfg.decaySec     * ringDecayScale * (0.45f + 0.70f * c.velocity);
    const float highDecay = cfg.highDecaySec * ringDecayScale * (0.5f  + 0.70f * c.velocity);
    // env = (1 - atk) * dec, with atk and dec both decaying from 1.  A fresh
    // strike starts at env = 0 and blooms over lowAttackSec.  A RE-strike must
    // start where the voice already is and bloom from there: dec goes back to
    // 1 (the strike refills the reservoir) and atk is placed so that env is
    // exactly continuous across the strike.  lowEnvHeld/highEnvHeld are 0 on a
    // fresh voice, so this reproduces `atk = dec = 1` bit-for-bit and the
    // shipped single-hit renders are untouched.
    c.lowAtk = 1.0f - fminf(1.0f, lowEnvHeld);   c.lowDec = 1.0f;
    c.hiAtk  = 1.0f - fminf(1.0f, highEnvHeld);  c.hiDec  = 1.0f;
    c.lowAtkMul = cym_env_mul(cfg.lowAttackSec, sr);
    c.lowDecMul = cym_env_mul(decay, sr);
    c.hiAtkMul  = cym_env_mul(cfg.highAttackSec, sr);
    c.hiDecMul  = cym_env_mul(highDecay, sr);

    const float strikeDecaySec = fmaxf(0.04f, cfg.highDecaySec * (0.5f + 1.1f * c.velocity));
    c.strikeAtk = c.strikeDec = 1.0f;
    c.strikeAtkMul = cym_env_mul(0.0008f, sr);
    c.strikeDecMul = cym_env_mul(strikeDecaySec, sr);

    c.noiseGain     = cfg.noiseLevel;
    c.whiteBlend    = 0.08f + (0.45f - 0.08f) * (c.velocity * c.velocity);
    c.shimmerScale  = cfg.shimmerLevel * c.velocity;
    c.maxCutoff     = 6000.0f + 14000.0f * c.velocity;
    c.pmDepthBase   = cfg.phaseModDepth * fmaxf(0.0f, fminf(1.0f, phaseModAmount));
    c.pmRateBase    = 0.7f + 0.9f * c.velocity;
    c.thwackGain    = cfg.stickLevel * c.velocity;
    c.thwackSamples = (uint32_t)(cfg.thwackSec * sr);
    c.thwackTauInv  = 1.0f / (cfg.thwackSec - 0.001f + 0.000001f);
    // Hard lifetime bound (the -66 dB magnitude squelch below normally fires
    // first).  Uncapped, a gong voice stayed active ~15 s: 4 stacked voices
    // ground through full resonator banks long after audibility -> CPU crash.
    uint32_t es = (uint32_t)(decay * sr * 8.0f) + (uint32_t)(0.05f * sr);
    const uint32_t esMax = (uint32_t)(8.0f * sr);
    c.endSample = (es > esMax) ? esMax : es;
    c.magEnv = 0.0f;

    // Resonator bank.  Exact expf/cosf here (see gotchas): r ~0.9999.
    // Lanes below `keep` carry their vibration across the strike (see
    // retainRing); everything else starts from rest.  0.75 leaves headroom for
    // the incoming strike so a restrike adds energy without slamming the
    // output clamp.
    const uint16_t keep = retainRing ? c.resCount : 0u;
    const float    keepGain = 0.75f;
    uint16_t count = (cfg.resonators > (uint16_t)kCymbalMaxResonators)
                        ? (uint16_t)kCymbalMaxResonators : cfg.resonators;
    const float requestedRingDecay = (0.55f + 0.60f * c.velocity) * ringDecayScale;
    const float ringDecay = fmaxf(0.08f, requestedRingDecay);
    const float r = expf(k_dsp_log_0001 / (ringDecay * sr));  // k_dsp_log_0001 = -6.9077...
    const float b0 = sqrtf(1.0f - r * r);
    const float r2 = -(r * r);
    const float pr = fmaxf(0.25f, fminf(4.0f, pitchRatio));
    // Scale only the LOW clamp with downward transposes; the high clamp is an
    // absolute Nyquist guard.  Scaling the top clamp down piles every high
    // mode onto one frequency and kills the transpose (measured: -12 semis
    // moved the centroid by only x0.87 instead of x0.5).
    const float fLo = cfg.minHz * fminf(pr, 1.0f);
    const float fHi = 20000.0f;
    for (uint16_t i = 0u; i < count; ++i) {
        const float anchor = cfg.freqHz[i % cfg.freqCount];
        const float detune = (cym_frand(c.rng) * 2.0f - 1.0f) * cfg.jitterSemis;
        const float octave = (float)(i / cfg.freqCount);
        float f = anchor * pr * exp2f(detune * (1.0f / 12.0f)) * exp2f(octave * 0.17f);
        f = fmaxf(fLo, fminf(fHi, f));
        const float w = 6.28318530717958647692f * f * k_dsp_inv_sample_rate;
        c.resB0[i]   = b0;
        c.resA1[i]   = 2.0f * r * cosf(w);
        c.resA2[i]   = r2;
        if (i < keep) {
            c.resY1[i] *= keepGain;
            c.resY2[i] *= keepGain;
        } else {
            c.resY1[i] = 0.0f;
            c.resY2[i] = 0.0f;
        }
        float g      = 0.85f + 0.30f * cym_frand(c.rng);
        if (cfg.hfTilt > 0.0f) {
            // sqrt(f/2kHz)^hfTilt HF weighting (see CymbalConfig).  fasterpowf
            // is fine here: amplitude weight, note-on time, arg in [0.15, 10].
            g *= fasterpowf(f * 0.0005f, 0.5f * cfg.hfTilt);
        }
        c.resGain[i] = g;
    }
    c.resonatorNorm = cfg.resonatorLevel / sqrtf((float)count);
    const uint16_t padded = (uint16_t)((count + 3u) & ~3u);
    for (uint16_t i = count; i < padded && i < (uint16_t)kCymbalMaxResonators; ++i) {
        c.resB0[i] = c.resA1[i] = c.resA2[i] = c.resY1[i] = c.resY2[i] = c.resGain[i] = 0.0f;
    }
    c.resCount = padded;

    for (int i = 0; i < 3; ++i) c.pmPhase[i] = cym_frand(c.rng);
}

static inline float cym_pink(CymbalVoice& c, float w) {
    c.pinkState[0] = 0.99886f * c.pinkState[0] + w * 0.0555179f;
    c.pinkState[1] = 0.99332f * c.pinkState[1] + w * 0.0750759f;
    c.pinkState[2] = 0.96900f * c.pinkState[2] + w * 0.1538520f;
    c.pinkState[3] = 0.86650f * c.pinkState[3] + w * 0.3104856f;
    c.pinkState[4] = 0.55000f * c.pinkState[4] + w * 0.5329522f;
    c.pinkState[5] = -0.7616f * c.pinkState[5] - w * 0.0168980f;
    const float p = c.pinkState[0] + c.pinkState[1] + c.pinkState[2] +
                    c.pinkState[3] + c.pinkState[4] + c.pinkState[5] +
                    c.pinkState[6] + w * 0.5362f;
    c.pinkState[6] = w * 0.115926f;
    return p * 0.11f;
}

// Control-rate divider for the cymbal driver coefficients and PM LFOs.  8
// samples = 6 kHz control rate at 48 kHz; the fastest thing updated here is
// HHat-O's 12 ms high-band attack (576 samples), so a stride of 8 still
// resolves it into ~72 steps.  The one-pole coefficients are held piecewise
// constant (the filter output stays continuous, so a stepped cutoff cannot
// click), while the PM sum IS a multiplier and so is linearly interpolated
// back to sample rate — a held PM value would stair-step the carrier at 6 kHz.
enum { kCymCtrlStride = 8 };

// Coefficient half of cym_one_pole_low, split out so it can be evaluated at
// control rate: this is where the fastexpf lives.
static inline float cym_pole_coeff(float cutoff) {
    cutoff = fmaxf(10.0f, fminf(k_dsp_sample_rate * 0.45f, cutoff));
    // fastexpf ok: argument always negative (where fastpow2f is accurate) and
    // the coefficient only nudges an already envelope-swept cutoff.
    return 1.0f - fastexpf(-6.28318530717958647692f * cutoff * k_dsp_inv_sample_rate);
}

// State half: one multiply-add, cheap enough to stay at sample rate.
static inline float cym_one_pole_a(float x, float a, float& s) {
    s += a * (x - s);
    return s;
}

static inline float cym_one_pole_low(float x, float cutoff, float& s) {
    cutoff = fmaxf(10.0f, fminf(k_dsp_sample_rate * 0.45f, cutoff));
    // fastexpf ok: argument always negative (where fastpow2f is accurate) and
    // the coefficient only nudges an already envelope-swept cutoff.
    const float a = 1.0f - fastexpf(-6.28318530717958647692f * cutoff * k_dsp_inv_sample_rate);
    s += a * (x - s);
    return s;
}

// Per-sample.  Zero libm calls: fastsinf for the PM LFOs, fastexpf for the
// swept one-pole coefficient and the short stick burst.
static inline float cymbal_process(CymbalVoice& c) {
    if (!c.active) return 0.0f;

    c.lowAtk *= c.lowAtkMul;  c.lowDec *= c.lowDecMul;
    c.hiAtk  *= c.hiAtkMul;   c.hiDec  *= c.hiDecMul;
    c.strikeAtk *= c.strikeAtkMul; c.strikeDec *= c.strikeDecMul;
    const float lowEnv    = (1.0f - c.lowAtk) * c.lowDec;
    const float highEnv   = (1.0f - c.hiAtk)  * c.hiDec;
    const float shimmerEnv = highEnv * c.shimmerScale;
    const float strikeEnv = c.velocity * (1.0f - c.strikeAtk) * c.strikeDec;

    // ── Control-rate block (every kCymCtrlStride samples) ──────────────────
    // The driver cutoffs are swept by envelopes whose fastest time constant is
    // 12 ms, and the PM LFOs run at 37/71/113 Hz — none of it needs a fresh
    // fastexpf/fastsinf per sample.  Measured on the host: this removes ~19 of
    // the ~37 ns/sample/voice of FIXED cost, the part kCymResonatorBudget
    // cannot see and which pass 26 doubled by allowing 4 voices instead of 2.
    if (c.ctrlCount == 0u) {
        c.ctrlCount = (uint16_t)kCymCtrlStride;
        c.lpCoeff = cym_pole_coeff(10.0f + c.maxCutoff * lowEnv);
        c.hpCoeff = cym_pole_coeff(10001.0f - 10000.0f * highEnv);

        // Advance the three PM phases a whole stride at a time, then aim
        // pmVal at the new sum so the per-sample path is a single add.
        const float strikeE  = c.velocity * (1.0f - c.strikeAtk) * c.strikeDec;
        const float rateStep = c.pmRateBase * (1.0f + 0.6f * strikeE) *
                               k_dsp_inv_sample_rate * (float)kCymCtrlStride;
        static const float rates[3] = { 37.0f, 71.0f, 113.0f };
        float pmNext = 0.0f;
        for (int i = 0; i < 3; ++i) {
            c.pmPhase[i] += rates[i] * rateStep;
            if (c.pmPhase[i] >= 1.0f) c.pmPhase[i] -= (float)(int)c.pmPhase[i];
            const float ph = (c.pmPhase[i] > 0.5f) ? (c.pmPhase[i] - 1.0f) : c.pmPhase[i];
            pmNext += fastsinf(6.28318530717958647692f * ph);
        }
        c.pmStep = (pmNext - c.pmVal) * (1.0f / (float)kCymCtrlStride);
    }
    --c.ctrlCount;

    const float wn = cym_frand(c.rng) * 2.0f - 1.0f;
    const float noise = cym_pink(c, wn) * (1.0f - c.whiteBlend) + wn * c.whiteBlend;
    const float loDriver = cym_one_pole_a(noise * c.noiseGain, c.lpCoeff, c.lpState) * lowEnv;
    const float lpHi = cym_one_pole_a(noise * c.noiseGain, c.hpCoeff, c.hpLowState);
    const float hiDriver = (noise * c.noiseGain - lpHi) * (0.18f * shimmerEnv);

    float thwack = 0.0f;
    if (c.sampleIndex < c.thwackSamples) {
        const float t = c.sampleIndex * k_dsp_inv_sample_rate;
        const float env = (t < 0.001f) ? (t * 1000.0f)
                                       : fastexpf(-(t - 0.001f) * c.thwackTauInv);
        thwack = env * c.thwackGain;
    }

    // PM value: interpolated toward the control-rate target computed above.
    c.pmVal += c.pmStep;
    const float pm = c.pmVal;

    const float pmDepth = c.pmDepthBase * strikeEnv;
    const float pmGain  = 1.0f + pmDepth * pm * 0.22f;
    const float pmExciter = pmDepth * 0.035f * pm;
    float drive = (loDriver + hiDriver + thwack + pmExciter) * pmGain;
    // ── Subsonic guard on the bank drive ───────────────────────────────────
    // A 2-pole resonator with a constant numerator has a DC gain of
    // b0/(1-a1-a2), which for the lowest lane of a gong (150 Hz, r = 0.99987)
    // is 42 — while its gain AT resonance is 4313.  So drive energy near DC is
    // only ~40 dB down on drive energy at the mode itself, and everything that
    // reaches this line has some: `thwack` is a UNIPOLAR contact burst (20 ms
    // on the gong, so nearly all of its energy is under 50 Hz) and `loDriver`
    // is pink noise, which by construction has more energy in 10-25 Hz than in
    // 120-200 Hz.  Each strike therefore parks a lump of sub-mode energy in
    // the lanes with the most gain, and it rings for a second — so it PILES UP
    // across repeated strikes.  Measured on the shipping build, energy under
    // 100 Hz for ONE strike against EIGHT 300 ms apart: RidBel 2.7 % -> 25.0 %,
    // Cymbal 1.7 % -> 13.5 %, Ride 2.5 % -> 9.7 %, Gong 5.6 % -> 9.1 % (the
    // gong reads lower only because it had so little else).  It is inaudible
    // as pitch and it eats the master limiter, so everything that IS audible
    // gets quieter and duller with every hit.  That is the "repeated hits
    // sound muddy" report, and it is not a mix problem — the mud is being
    // synthesised.  T43 is the regression test.
    //
    // Two cascaded one-poles at kCymDriveHpHz: DC is removed outright, 25 Hz
    // is -31 dB and 60 Hz -19 dB, while the lowest anchor any preset in the
    // family uses (the gong's 150 Hz) pays 6 dB and the next one up (crash,
    // 343 Hz) pays 1.5 dB.  Nothing in the family HAS a mode below 150 Hz, so
    // everything this removes is energy the bank was never meant to receive.
    c.driveHp1 += kCymDriveHpCoef * (drive - c.driveHp1);  drive -= c.driveHp1;
    c.driveHp2 += kCymDriveHpCoef * (drive - c.driveHp2);  drive -= c.driveHp2;

    float res = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        // 2x-unrolled with two independent accumulators/MAC chains: the
        // Cortex-A7's in-order NEON pipe stalls on back-to-back dependent
        // vmlaq; interleaving the a/b chains hides that latency.
        const float32x4_t vdrive = vdupq_n_f32(drive);
        float32x4_t vsum0 = vdupq_n_f32(0.0f);
        float32x4_t vsum1 = vdupq_n_f32(0.0f);
        uint16_t i = 0u;
        for (; i + 8u <= c.resCount; i += 8u) {
            const float32x4_t y1a = vld1q_f32(c.resY1 + i);
            const float32x4_t y1b = vld1q_f32(c.resY1 + i + 4);
            float32x4_t ya = vmulq_f32(vld1q_f32(c.resB0 + i), vdrive);
            float32x4_t yb = vmulq_f32(vld1q_f32(c.resB0 + i + 4), vdrive);
            ya = vmlaq_f32(ya, vld1q_f32(c.resA1 + i), y1a);
            yb = vmlaq_f32(yb, vld1q_f32(c.resA1 + i + 4), y1b);
            ya = vmlaq_f32(ya, vld1q_f32(c.resA2 + i), vld1q_f32(c.resY2 + i));
            yb = vmlaq_f32(yb, vld1q_f32(c.resA2 + i + 4), vld1q_f32(c.resY2 + i + 4));
            vst1q_f32(c.resY2 + i, y1a);
            vst1q_f32(c.resY2 + i + 4, y1b);
            vst1q_f32(c.resY1 + i, ya);
            vst1q_f32(c.resY1 + i + 4, yb);
            vsum0 = vmlaq_f32(vsum0, ya, vld1q_f32(c.resGain + i));
            vsum1 = vmlaq_f32(vsum1, yb, vld1q_f32(c.resGain + i + 4));
        }
        for (; i < c.resCount; i += 4u) {  // count is a multiple of 4, not 8
            const float32x4_t y1 = vld1q_f32(c.resY1 + i);
            float32x4_t y = vmulq_f32(vld1q_f32(c.resB0 + i), vdrive);
            y = vmlaq_f32(y, vld1q_f32(c.resA1 + i), y1);
            y = vmlaq_f32(y, vld1q_f32(c.resA2 + i), vld1q_f32(c.resY2 + i));
            vst1q_f32(c.resY2 + i, y1);
            vst1q_f32(c.resY1 + i, y);
            vsum0 = vmlaq_f32(vsum0, y, vld1q_f32(c.resGain + i));
        }
        vsum0 = vaddq_f32(vsum0, vsum1);
        float32x2_t vs = vadd_f32(vget_low_f32(vsum0), vget_high_f32(vsum0));
        vs = vpadd_f32(vs, vs);
        res = vget_lane_f32(vs, 0);
    }
#else
    for (uint16_t i = 0u; i < c.resCount; ++i) {
        const float y = c.resB0[i] * drive + c.resA1[i] * c.resY1[i] + c.resA2[i] * c.resY2[i];
        c.resY2[i] = c.resY1[i];
        c.resY1[i] = y;
        res += y * c.resGain[i];
    }
#endif
    res *= c.resonatorNorm;

    // Direct stick tap raised 0.07 -> 0.12: the "tang" contact click on top of
    // the mode excitation the burst already provides through the bank.
    float out = res + loDriver * c.directNoiseLevel + thwack * 0.12f;
    // Rumble filter (was a single 7.6 Hz DC blocker).  Two things reach the
    // output BELOW the lowest resonator in the bank and neither is a cymbal:
    // the direct `loDriver` tap is lowpassed PINK noise, which by definition
    // carries more energy in 10-25 Hz than in 120-200 Hz, and a repeated
    // strike turns the 20 ms stick burst into a pulse train whose harmonics
    // start at the strike rate.  Both bypass the drive guard above, so they
    // need their own: with that guard in place but this filter reverted to its
    // old 7.6 Hz corner, a gong struck 8 times 300 ms apart still puts 12 % of
    // the passage energy under 25 Hz and 17 % under 100 Hz.  With it: 0.4 %
    // and 2.5 %, and the passage centroid rises 586 -> 696 Hz.
    //
    // Two cascaded one-poles at kCymRumbleHz: -16 dB at 17 Hz against -0.6 dB
    // at 150 Hz, the lowest anchor any preset in the family uses (the gong;
    // crash starts at 343 Hz, ride 481, splash 1123, hat 3271), so it removes
    // only what nothing here is meant to produce.
    c.dcState  += kCymRumbleCoef * (out - c.dcState);
    out -= c.dcState;
    c.dcState2 += kCymRumbleCoef * (out - c.dcState2);
    out = (out - c.dcState2) * 0.9f;
    out *= c.velocityGain;

    // Panic fade (fadeMul < 1 only after AllNoteOff).
    out *= c.fade;
    c.fade *= c.fadeMul;
    if (c.fade < 0.001f) c.active = false;

    // CPU squelch: release the voice once the tail is inaudible (-66 dB for
    // ~10 ms), instead of running the full bank until endSample.  The 0.5 s
    // guard protects slow attacks (gong lowAttackSec = 0.25 s).
    c.magEnv += 0.002f * (fabsf(out) - c.magEnv);
    if (c.sampleIndex > (uint32_t)(0.5f * k_dsp_sample_rate) && c.magEnv < 0.0004f) {
        c.active = false;
    }

    if (++c.sampleIndex > c.endSample) c.active = false;
    return fmaxf(-1.0f, fminf(1.0f, out));
}

/**
 * The voice: one dense-resonator cymbal plus the per-voice output state.
 */
struct VoiceState {
    bool    is_active      = false;
    uint8_t current_note   = 60;    // ← non-zero
    float   current_velocity = 0.0f;

    // Preset-change fade.  A voice cannot survive a preset change, but cutting
    // it dead clicks, so it fades out over ~10 ms and is then reclaimed.
    // fade_mul stays exactly 1.0f in normal play and the fade block is skipped
    // entirely, so shipped renders are bit-identical.
    float fade      = 1.0f;   // ← non-zero
    float fade_mul  = 1.0f;   // ← non-zero (< 1 only while fading out)

    CymbalVoice cymbal;

    float tone_lp = 0.0f;     // tilt-EQ memory

    void PartialReset() {
        // A re-struck slot is no longer fading.
        fade = 1.0f;
        fade_mul = 1.0f;
        tone_lp = 0.0f;
        cymbal.active = false;
    }
};

// Global Synth State (4 Voices limit for strict CPU budgeting)
constexpr int NUM_VOICES = 4;
struct SynthState {
    VoiceState voices[NUM_VOICES];
    uint8_t next_voice_idx  = 0;

    // Master FX
    float master_gain       = 1.0f;   // ← non-zero
    float master_drive      = 1.0f;   // ← non-zero
    // Master limiter peak-follower state (see the Stage 4b comment): holds the
    // recent peak so ONE gain can be applied per cycle instead of reshaping the
    // waveform sample by sample.  Starts at 0 = no gain reduction.
    float master_lim_env    = 0.0f;
    float tone              = 0.0f;   // Tilt EQ amount, cached from k_paramTone [-10, 30]

    FastSVF master_filter;
    // NO user constructor — DMIs only so globals go to .data, not .bss.
    // See WaveguideState comment above for the full explanation.
};

#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include "noise.h"
#include "envelope.h"
#include "filter.h"
#include "tables.h"
#include "float_math.h"


/** Because we are optimizing for bare-metal, notice there are no virtual functions,
 * no dynamic memory (new/malloc), and no deep class hierarchies.
 * Just pure data that the CPU cache can read sequentially. */

// A power-of-two buffer size allows us to use bitwise AND (& DELAY_MASK) for lightning-fast
// wrap-around instead of slow modulo (%) operators.
// 2048 samples @ 48kHz = ~42.7ms max delay.
// Minimum delay for the lowest Note param value (MIDI 24, B0 = 30.87 Hz) is 1555 samples.
// With pitch bend down 2 semitones from note 24: ~1745 samples. Both fit in 2047.
// Reduced from 4096 to cut BSS by 64 KB (8 delay lines × 2048 floats × 4 bytes)
// so the unit fits within the Drumlogue firmware's per-unit BSS budget.
// WARNING: delay_length must be strictly clamped to < 2048 in the DSP loop
// to prevent wrap-around pitch squeals under downward pitch bend/modulation.
constexpr size_t DELAY_BUFFER_SIZE = 2048;
constexpr size_t DELAY_MASK = DELAY_BUFFER_SIZE - 1;

constexpr float k_dsp_sample_rate     = 48000.0f;
constexpr float k_dsp_inv_sample_rate = 1.0f / 48000.0f;
constexpr float k_dsp_log_0001        = -6.907755279f;
#ifndef STAGE2_MODAL_DECAY1
#define STAGE2_MODAL_DECAY1  0.99905f
#define STAGE2_MODAL_DECAY2  0.99810f
#endif

/**
 * The Exciter injects initial energy into the resonators.
 * It is completely passive once the sample or noise burst finishes.
 */
struct ExciterState {
    // PCM Sample Data
    const float* sample_ptr  = nullptr;
    size_t  sample_frames    = 0;
    size_t  current_frame    = 0;
    uint8_t channels         = 1;     // ← non-zero

    FastNoise    noise_gen;           // seed=2463534242 (non-zero) → forces .data
    FastEnvelope noise_env;           // attack_rate=0.01f (non-zero)
    FastEnvelope noise_env_hi;        // short high-band burst for snare/hat click
    FastEnvelope master_env;          // Optional: To choke the whole voice on GateOff

    // Noise Burst Data
    float noise_decay_coeff   = 0.0f;
    float current_noise_env   = 0.0f;

    // Computed each sample by process_exciter; read by processBlock.
    float noise_out_sample    = 0.0f;
    float noise_lp_state      = 0.0f;   // stage-1 dual-band noise shaper LP state
    float noise_band_mix      = 0.5f;   // ← non-zero
    float noise_hi_lp_state   = 0.0f;   // high-band split LP memory
    float noise_hi_lp_coeff   = 0.30f;  // ← non-zero
    float wire_onset_env      = 1.0f;   // ← non-zero
    float wire_onset_attack   = 1.0f;   // ← non-zero
    float snare_wire_z1       = 0.0f;
    float snare_wire_z2       = 0.0f;
    float snare_wire_lp       = 0.0f;   // helper state for multiband wire split
    float snare_wire_hp       = 0.0f;
    float snare_wire_mix      = 0.0f;
    float snare_crack_gain    = 1.0f;   // VlMllRes → crack/snap level  ← non-zero
    float snare_wire_a1       = 1.6951f; // ← non-zero
    float snare_wire_a2       = 0.8930f; // ← non-zero
    // 3-band snare wire: bands B (~4.5 kHz) and C (~7.2 kHz) complement band A.
    float snare_wire_z1b = 0.0f;
    float snare_wire_z2b = 0.0f;
    float snare_wire_a1b = 1.620f;   // 4.5 kHz at r=0.971 (Q≈10)  ← non-zero
    float snare_wire_a2b = 0.942f;   // ← non-zero
    float snare_wire_z1c = 0.0f;
    float snare_wire_z2c = 0.0f;
    float snare_wire_a1c = 1.120f;   // 7.2 kHz at r=0.953 (Q≈10)  ← non-zero
    float snare_wire_a2c = 0.908f;   // ← non-zero
    // updated runtime
    float mallet_lp           = 0.0f;
    float mallet_lp2          = 0.0f;   // Second LP pole state (MlltRes)
    float mallet_stiffness    = 0.5f;   // ← non-zero
    float mallet_res_coeff    = 0.5f;   // ← non-zero

    FastSVF noise_filter;    // Dedicated per-voice noise shaping SVF (NzFltr / NzFltFrq)
    FastSVF hat_filter;      // Dedicated hi-hat centroid filter (biquad/TPT)
    bool use_hat_filter      = false;
};

/**
 * A single Digital Waveguide (Physical Model).
 * Contains the delay line memory and the loop filter states.
 */
// DMI-only structs: no user-defined constructors anywhere in this file.
// This is required so that the global `static BrachettiSynth s_synth` in
// unit.cc undergoes *constant initialization* (not dynamic/runtime init).
// With a user constructor GCC places the object in .bss (zero-filled at
// dlopen time), consuming ~72 KB — far over the drumlogue's BSS budget.
// With DMIs only and at least one non-zero member (lowpass_coeff=1.0f etc.)
// GCC places the object in .data (loaded directly from the ELF), leaving
// only ~600 bytes of .bss (FastTables + unit_runtime_desc). This is the
// same pattern used in the original working commit (e107c25).
struct WaveguideState {
    // delay line — explicit zero-init DMI keeps it in .data (not BSS)
    float buffer[DELAY_BUFFER_SIZE] = {};
    uint32_t write_ptr              = 0;
    // Fractional tuning for exact pitch
    float delay_length              = 0.0f;

    // Fast-math loop coefficients (Calculated from UI parameters)
    float feedback_gain = 0.0f;    // Determines Decay Time
    float lowpass_coeff = 1.0f;    // Determines Material/Tone  ← non-zero → forces .data
    float loss_g_dc     = 1.0f;    // Frequency-independent sustain factor
    float loss_g_hf     = 1.0f;    // HF feedback loss factor (0..1)
    float ap_coeff      = 0.0f;    // Allpass coefficient (-0.99 to 0.99)
    float ap_x1         = 0.0f;    // Allpass delayed input
    float ap_y1         = 0.0f;    // Allpass delayed output

    // Filter State Memory
    float z1          = 0.0f;      // 1st lowpass pole history
    float z2          = 0.0f;      // 2nd lowpass pole history (optional, per-preset)
    float diffuser_mix = 0.0f;     // 0=off, >0 enables 4-stage Schroeder allpass chain
    float diffuser_g   = 0.45f;    // ← non-zero
    float diffuser_buf1[13] = {};
    float diffuser_buf2[19] = {};
    float diffuser_buf3[29] = {};
    float diffuser_buf4[41] = {};
    uint8_t diffuser_i1 = 0, diffuser_i2 = 0, diffuser_i3 = 0, diffuser_i4 = 0;
    bool bypass_loop_lp = false;
    // Second cascaded loss pole (AcousticTom): set per-hit in NoteOn so the
    // render loop doesn't compare the preset index per sample.
    bool double_lp      = false;
    // Physics Topology Multiplier (+1.0f for String, -1.0f for Tube)
    float phase_mult    = 1.0f;    // ← non-zero
    // Per-model baseline allpass dispersion (added to ap_coeff from Inharm parameter).
    float model_ap_base = 0.0f;
};

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
    // Left 0 for presets whose balance is HW-approved (HHat-O, Gong, Splash);
    // aggregate initializers with 17 values default this to 0.
    float hfTilt;
};

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

    float lpState = 0.0f, hpLowState = 0.0f, dcState = 0.0f;
    float pinkState[7] = {};
    float pmPhase[3]   = {};

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
static inline void cymbal_note_on(CymbalVoice& c, const CymbalConfig& cfg,
                                  float velocity, float ringDecayScale,
                                  float phaseModAmount, float pitchRatio,
                                  uint32_t seed) {
    const float sr = k_dsp_sample_rate;
    c.velocity = fmaxf(0.01f, fminf(1.0f, velocity));
    c.rng = seed ? seed : 0x12345678u;
    c.sampleIndex = 0u;
    c.active = true;
    c.fade = 1.0f;
    c.fadeMul = 1.0f;
    c.lpState = c.hpLowState = c.dcState = 0.0f;
    for (int i = 0; i < 7; ++i) c.pinkState[i] = 0.0f;
    c.directNoiseLevel = cfg.directNoiseLevel;

    c.velocityGain = 0.25f + 0.75f * c.velocity * sqrtf(c.velocity);

    // Driver envelope time constants (constant for the note).
    const float decay     = cfg.decaySec     * ringDecayScale * (0.45f + 0.70f * c.velocity);
    const float highDecay = cfg.highDecaySec * ringDecayScale * (0.5f  + 0.70f * c.velocity);
    c.lowAtk = c.lowDec = c.hiAtk = c.hiDec = 1.0f;
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
        c.resY1[i]   = 0.0f;
        c.resY2[i]   = 0.0f;
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

    const float lowCutoff  = 10.0f + c.maxCutoff * lowEnv;
    const float highCutoff = 10001.0f - 10000.0f * highEnv;

    const float wn = cym_frand(c.rng) * 2.0f - 1.0f;
    const float noise = cym_pink(c, wn) * (1.0f - c.whiteBlend) + wn * c.whiteBlend;
    const float loDriver = cym_one_pole_low(noise * c.noiseGain, lowCutoff, c.lpState) * lowEnv;
    const float lpHi = cym_one_pole_low(noise * c.noiseGain, highCutoff, c.hpLowState);
    const float hiDriver = (noise * c.noiseGain - lpHi) * (0.18f * shimmerEnv);

    float thwack = 0.0f;
    if (c.sampleIndex < c.thwackSamples) {
        const float t = c.sampleIndex * k_dsp_inv_sample_rate;
        const float env = (t < 0.001f) ? (t * 1000.0f)
                                       : fastexpf(-(t - 0.001f) * c.thwackTauInv);
        thwack = env * c.thwackGain;
    }

    float pm = 0.0f;
    const float rateScale = c.pmRateBase * (1.0f + 0.6f * strikeEnv) * k_dsp_inv_sample_rate;
    const float rates[3] = { 37.0f, 71.0f, 113.0f };
    for (int i = 0; i < 3; ++i) {
        c.pmPhase[i] += rates[i] * rateScale;
        if (c.pmPhase[i] >= 1.0f) c.pmPhase[i] -= 1.0f;
        const float ph = (c.pmPhase[i] > 0.5f) ? (c.pmPhase[i] - 1.0f) : c.pmPhase[i];
        pm += fastsinf(6.28318530717958647692f * ph);
    }

    const float pmDepth = c.pmDepthBase * strikeEnv;
    const float pmGain  = 1.0f + pmDepth * pm * 0.22f;
    const float pmExciter = pmDepth * 0.035f * pm;
    const float drive = (loDriver + hiDriver + thwack + pmExciter) * pmGain;

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
    c.dcState += 0.001f * (out - c.dcState);
    out = (out - c.dcState) * 0.9f;
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
 * The Master Voice Structure.
 * Holds the Exciter and two parallel Resonators (A and B).
 */
struct VoiceState {
    bool    is_active      = false;
    bool    is_releasing   = false;
    uint8_t current_note   = 60;    // ← non-zero → forces containing struct to .data
    float   current_velocity = 0.0f;

    ExciterState   exciter;
    WaveguideState resA;
    WaveguideState resB;
    CymbalVoice    cymbal;   // ENGINE_CYMBAL state (dense resonator cymbal)

    // Coupling and Tone memory
    float resA_out_prev      = 0.0f;
    float resB_out_prev      = 0.0f;
    float tone_lp            = 0.0f;

    // Pitch Bend
    float base_delay_A       = 0.0f;
    float base_delay_B       = 0.0f;

    // Dynamic Energy Squelch (MAV envelope follower, α=0.01, τ≈2ms)
    float mag_env            = 0.0f;

    // Stage-1 transient complexity boost
    uint32_t transient_frames_left  = 0;
    uint32_t transient_frames_total = 0;
    float transient_inv_total       = 0.0f;
    float transient_lp_jitter       = 0.0f;
    float transient_ap_jitter       = 0.0f;
    float transient_lp_base_a       = 1.0f;  // ← non-zero
    float transient_lp_base_b       = 1.0f;  // ← non-zero
    float transient_ap_base_a       = 0.0f;
    float transient_ap_base_b       = 0.0f;

    // Stage-2 pilot modal-bank (up to 6 modes)
    bool     modal_pilot_enabled = false;
    float    modal_k_1   = 0.0f, modal_k_2 = 0.0f, modal_k_3 = 0.0f;
    float    modal_k_4   = 0.0f, modal_k_5 = 0.0f, modal_k_6 = 0.0f;
    float    modal_y1_1  = 0.0f, modal_y1_2 = 0.0f, modal_y1_3 = 0.0f, modal_y1_4 = 0.0f;
    float    modal_y2_1  = 0.0f, modal_y2_2 = 0.0f, modal_y2_3 = 0.0f, modal_y2_4 = 0.0f;
    float    modal_y1_5  = 0.0f, modal_y1_6 = 0.0f;
    float    modal_y2_5  = 0.0f, modal_y2_6 = 0.0f;
    uint32_t modal_norm_count = 0;
    float    modal_env_1 = 0.0f, modal_env_2 = 0.0f, modal_env_3 = 0.0f;
    float    modal_env_4 = 0.0f, modal_env_5 = 0.0f, modal_env_6 = 0.0f;
    float    modal_decay_1 = 0.9990f;  // ← non-zero
    float    modal_decay_2 = 0.9985f;  // ← non-zero
    float    modal_decay_3 = 0.9980f;  // ← non-zero
    float    modal_decay_4 = 0.9975f;  // ← non-zero
    float    modal_decay_5 = 0.9970f;  // ← non-zero
    float    modal_decay_6 = 0.9965f;  // ← non-zero
    float    modal_mix   = 0.0f;
    uint8_t  modal_mode_count = 0;

    // Stage-2 pitch-envelope and reed nonlinearity
    float pitch_env       = 0.0f;
    float pitch_env_decay = 1.0f;   // ← non-zero
    float pitch_env_amt   = 0.0f;
    bool  reed_nl_enabled = false;
    float reed_nl_drive   = 1.0f;   // ← non-zero

    // Low-body boom oscillator
    float boom_phase      = 0.0f;
    float boom_inc        = 0.0f;
    float boom_env        = 0.0f;
    float boom_decay      = 1.0f;   // ← non-zero
    float boom_mix        = 0.0f;
    float boom_attack_env = 1.0f;   // ← non-zero
    float boom_attack_inc = 1.0f;   // ← non-zero
    // TubRad → boom base-tune multiplier (kick family).  Bigger shell = lower
    // resting pitch.  1.0 = shipped (bit-identical).  Multiplies the per-sample
    // boom frequency in processBlock (KickDrum/808Sub recompute boom_inc each
    // sample, so the tune must live here rather than baked into boom_inc).
    float boom_tune       = 1.0f;   // ← non-zero

    // Kick "thump" transient: a fast pitch-dropping mid sine punch (~500→150 Hz)
    // layered over the boom on the kick family.  The boom gives the sub, the
    // thump gives the beater-impact PUNCH.  Off by default (thump_env 0); the
    // MlltRes/MlltStif knobs dial it in (see NoteOn), reference-anchored so the
    // shipped kicks are bit-identical.  Pitch drops as thump_env/thump_amp0 falls.
    float thump_env       = 0.0f;   // amplitude env (starts at depth·velocity)
    float thump_amp0      = 1.0f;   // initial env value (pitch-drop normaliser) ← non-zero
    float thump_phase     = 0.0f;
    float thump_inc       = 0.0f;   // base angular frequency (bottom of the drop)
    float thump_decay     = 1.0f;   // per-sample decay ← non-zero

    // Metallic transient FM chirp
    float metal_fm_phase  = 0.0f;
    float metal_fm_inc    = 0.0f;
    float metal_fm_env    = 0.0f;
    float metal_fm_decay  = 1.0f;   // ← non-zero
    float metal_fm_depth  = 0.0f;

    // Dedicated post-resonator high-band branch
    float hf_branch_env   = 0.0f;
    float hf_branch_decay = 1.0f;   // ← non-zero
    float hf_branch_mix   = 0.0f;
    float hf_branch_lp    = 0.0f;

    // Global onset ramp
    float onset_env = 1.0f;         // ← non-zero
    float onset_inc = 0.0f;

    // Ring-coupled noise gate: for ENGINE_PLATE, noise amplitude tracks the
    // modal ring decay so noise and ring die together (integrated metallic).
    // Starts at 1.0 on each NoteOn; decays with modal_decay_1 each sample.
    float noise_ring_gate = 1.0f;   // ← non-zero

    // Noise ⇄ ring cross-modulation (metallic plates): the parallel noise is
    // ring-modulated by the previous sample's modal-bank output so wash and
    // ring interact (Risset-style cymbal) instead of sitting side by side.
    float modal_rm_depth  = 0.0f;   // 0 = off; set per-preset in NoteOn
    float modal_out_prev  = 0.0f;   // last modal mixdown (pre-mix, ±~1)

    // Enveloped-LFO amplitude gate on the parallel noise (Clap multi-burst,
    // Shaker grain pulses).  gate = 1 − depth·(0.5 + 0.5·sin(phase)); depth
    // decays each sample so the modulation fades into the plain tail.
    float noise_am_phase  = 0.0f;
    float noise_am_inc    = 0.0f;
    float noise_am_depth  = 0.0f;
    float noise_am_decay  = 1.0f;   // ← non-zero

    // NOTE: the old ENGINE_PLATE crash-resonator bank, self-PM bloom and FDN
    // dense wash (crash_* / fdn_* per-voice state) were REPLACED by the
    // dense-resonator ENGINE_CYMBAL port (CymbalVoice above); their dead state
    // was removed.  See git history (HW passes 7-18) for the retired code.

    // ── Strike transient layer (membrane presets) ────────────────────────────
    // The modal tail is measurably correct; the remaining perceptual gap is the
    // broadband ATTACK the modal bank cannot synthesise — the Taiko stick-slap
    // (ref early centroid ~1.9 kHz) and the Timpani felt-mallet contact.  Layer
    // a short, velocity-scaled, band-passed noise burst over the modal body.
    // Difference-of-one-poles bandpass; pure DSP, no samples.
    float trans_env   = 0.0f;   // burst envelope (0 = layer off)
    float trans_decay = 1.0f;   // per-sample decay (T60 ≈ 10-30 ms)
    float trans_gain  = 0.0f;   // output level
    float trans_a_lo  = 0.0f;   // bandpass low-corner one-pole coeff
    float trans_a_hi  = 0.0f;   // bandpass high-corner one-pole coeff
    float trans_lp_lo = 0.0f, trans_lp_hi = 0.0f;

    void PartialReset() {
        mag_env = 0.0f;

        // Reset exciter frame counter so the mallet fires on every NoteOn and
        // the kSquelchGuardSamples window restarts from zero for the new note.
        exciter.current_frame = 0;
        exciter.mallet_lp  = 0.0f;
        exciter.mallet_lp2 = 0.0f;

        // Reset phase
        // resonators
        resA.ap_x1 = 0.0f;
        resA.ap_y1 = 0.0f;
        resB.ap_x1 = 0.0f;
        resB.ap_y1 = 0.0f;
        resA_out_prev = 0.0f;
        resB_out_prev = 0.0f;
        tone_lp = 0.0f;
        // transient shaper
        transient_frames_left = 0;
        transient_frames_total = 0;
        transient_inv_total = 0.0f;
        transient_lp_jitter = 0.0f;
        transient_ap_jitter = 0.0f;
        transient_lp_base_a = resA.lowpass_coeff;
        transient_lp_base_b = resB.lowpass_coeff;
        transient_ap_base_a = resA.ap_coeff;
        transient_ap_base_b = resB.ap_coeff;
        // modal synthesis
        modal_pilot_enabled = false;
        modal_k_1 = 0.0f;
        modal_k_2 = 0.0f;
        modal_k_3 = 0.0f;
        modal_k_4 = 0.0f;
        modal_k_5 = 0.0f;
        modal_k_6 = 0.0f;
        modal_y1_1 = 0.0f;
        modal_y1_2 = 0.0f;
        modal_y1_3 = 0.0f;
        modal_y1_4 = 0.0f;
        modal_y2_1 = 0.0f;
        modal_y2_2 = 0.0f;
        modal_y2_3 = 0.0f;
        modal_y2_4 = 0.0f;
        modal_y1_5 = 0.0f;
        modal_y1_6 = 0.0f;
        modal_y2_5 = 0.0f;
        modal_y2_6 = 0.0f;
        modal_norm_count = 0;
        modal_env_1 = 0.0f;
        modal_env_2 = 0.0f;
        modal_env_3 = 0.0f;
        modal_env_4 = 0.0f;
        modal_env_5 = 0.0f;
        modal_env_6 = 0.0f;
        modal_decay_1 = 0.9990f;
        modal_decay_2 = 0.9985f;
        modal_decay_3 = 0.9980f;
        modal_decay_4 = 0.9975f;
        modal_decay_5 = 0.9970f;
        modal_decay_6 = 0.9965f;
        modal_mix = 0.0f;
        modal_mode_count = 0;
        // pitch modulation
        pitch_env = 0.0f;
        pitch_env_decay = 1.0f;
        pitch_env_amt = 0.0f;
        // reed table nonlinearity (for reeds and brass models)
        reed_nl_enabled = false;
        reed_nl_drive = 1.0f;
        // boom table modulation (for bass drum models)
        boom_phase = 0.0f;
        boom_inc = 0.0f;
        boom_env = 0.0f;
        boom_decay = 1.0f;
        boom_mix = 0.0f;
        boom_attack_env = 1.0f;
        boom_attack_inc = 1.0f;
        boom_tune = 1.0f;
        // kick thump transient
        thump_env = 0.0f;
        thump_amp0 = 1.0f;
        thump_phase = 0.0f;
        thump_inc = 0.0f;
        thump_decay = 1.0f;
        // metallic FM modulation (for cymbal and metallic models)
        metal_fm_phase = 0.0f;
        metal_fm_inc = 0.0f;
        metal_fm_env = 0.0f;
        metal_fm_decay = 1.0f;
        metal_fm_depth = 0.0f;
        hf_branch_env = 0.0f;
        hf_branch_decay = 1.0f;
        hf_branch_mix = 0.0f;
        hf_branch_lp = 0.0f;
        onset_env = 1.0f;
        onset_inc = 0.0f;
        noise_ring_gate = 1.0f;
        modal_rm_depth = 0.0f;
        modal_out_prev = 0.0f;
        noise_am_phase = 0.0f;
        noise_am_inc = 0.0f;
        noise_am_depth = 0.0f;
        noise_am_decay = 1.0f;
        // strike transient layer
        trans_env = 0.0f;
        trans_decay = 1.0f;
        trans_gain = 0.0f;
        trans_a_lo = 0.0f;
        trans_a_hi = 0.0f;
        trans_lp_lo = trans_lp_hi = 0.0f;
        // exciter state
        exciter.current_frame = 0;
        exciter.mallet_lp  = 0.0f;
        exciter.mallet_lp2 = 0.0f;
        exciter.noise_lp_state = 0.0f; // updated runtime
        exciter.noise_band_mix = 0.5f;
        exciter.noise_hi_lp_state = 0.0f; // updated runtime
        exciter.snare_wire_z1 = 0.0f;
        exciter.snare_wire_z2 = 0.0f;
        exciter.snare_wire_lp = 0.0f;
        exciter.snare_wire_hp = 0.0f;
        exciter.snare_wire_mix = 0.0f;
        exciter.snare_crack_gain = 1.0f;
        exciter.snare_wire_a1 = 1.6951f;
        exciter.snare_wire_a2 = 0.8930f;
        exciter.snare_wire_z1b = 0.0f;
        exciter.snare_wire_z2b = 0.0f;
        exciter.snare_wire_z1c = 0.0f;
        exciter.snare_wire_z2c = 0.0f;
        exciter.snare_wire_a1b = 1.620f;
        exciter.snare_wire_a2b = 0.942f;
        exciter.snare_wire_a1c = 1.120f;
        exciter.snare_wire_a2c = 0.908f;

        resA.write_ptr = 0;
        resB.write_ptr = 0;
        resA.z1 = 0.0f;
        resA.z2 = 0.0f;
        resB.z1 = 0.0f;
        resB.z2 = 0.0f;
        resA.diffuser_g = 0.45f;    // actually never updated
        resB.diffuser_g = 0.45f;
        cymbal.active = false;
    }

    // Stage-2 pilot extensions (CPU-light):
    // - Modal bank for complex metallic presets (Wodblk/Gong/Cymbal)
    // - Kick pitch-envelope (delay-length sweep)
    // - Clarinet reed nonlinearity in exciter path
    void init_modal_modes(float ratio2, float ratio3, float ratio4,
                         float t60_1_ms, float t60_2_ms, float t60_3_ms, float t60_4_ms,
                         float mix, float env1, float env2, float env3, float env4,
                         uint8_t mode_count, float ratio5 = 0.0f, float ratio6 = 0.0f,
                         float env5 = 0.0f, float env6 = 0.0f) {
        uint8_t note = current_note;
        // Exact math here, NOT the faster* approximations.  fastercosfullf has
        // ~1e-3 absolute error; near w→0 the recovered mode frequency shifts by
        // δf ≈ δcos/(w·sin w) — at timpani range (80-170 Hz) the intended
        // 1 : 1.504 : 1.742 : 2.0 spread collapsed to 1 : 1.41 : 1.61 : 1.83
        // with ~17 Hz mode gaps → slow beating heard on HW as a "rough, not
        // smooth" low-end reverberation.  Same family of bug as the fasterexpf
        // T60 failure below; this runs once per NoteOn so accuracy wins.
        float base_f = 440.0f * exp2f(((float)note - 69.0f) * 0.08333333333f); // 1/12
        if (base_f < 20.0f) base_f = 20.0f;
        float f1 = fminf(base_f, 0.45f * k_dsp_sample_rate);
        float f2 = fminf(base_f * ratio2, 0.45f * k_dsp_sample_rate);
        float f3 = fminf(base_f * ratio3, 0.45f * k_dsp_sample_rate);
        float f4 = fminf(base_f * ratio4, 0.45f * k_dsp_sample_rate);
        // Modes 5/6: use explicit ratios when provided (physical plate theory),
        // otherwise fall back to legacy formula (ratio4 × 1.31 / 1.62).
        float f5 = fminf(base_f * (ratio5 > 0.0f ? ratio5 : ratio4 * 1.31f), 0.45f * k_dsp_sample_rate);
        float f6 = fminf(base_f * (ratio6 > 0.0f ? ratio6 : ratio4 * 1.62f), 0.45f * k_dsp_sample_rate);
        float w1 = (M_TWOPI * f1) * k_dsp_inv_sample_rate;
        float w2 = (M_TWOPI * f2) * k_dsp_inv_sample_rate;
        float w3 = (M_TWOPI * f3) * k_dsp_inv_sample_rate;
        float w4 = (M_TWOPI * f4) * k_dsp_inv_sample_rate;
        float w5 = (M_TWOPI * f5) * k_dsp_inv_sample_rate;
        float w6 = (M_TWOPI * f6) * k_dsp_inv_sample_rate;
        modal_pilot_enabled = true;
        modal_mode_count = mode_count;
        // Seed at full amplitude (cosine quadrature pair): oscillator starts
        // at peak on frame 0 instead of tiny sin(w) ≈ 0.034 that takes ~1 ms
        // to build up.  y2=cos(w), y1=1 gives yn=2cos·1-cos=cos, i.e. a
        // cosine starting at 1.0 — correct initial energy for struck bars.
        modal_y2_1 = cosf(w1); modal_y1_1 = 1.0f;
        modal_y2_2 = cosf(w2); modal_y1_2 = 1.0f;
        modal_y2_3 = (mode_count > 2) ? cosf(w3) : 0.0f; modal_y1_3 = (mode_count > 2) ? 1.0f : 0.0f;
        modal_y2_4 = (mode_count > 3) ? cosf(w4) : 0.0f; modal_y1_4 = (mode_count > 3) ? 1.0f : 0.0f;
        modal_y2_5 = (mode_count > 4) ? cosf(w5) : 0.0f; modal_y1_5 = (mode_count > 4) ? 1.0f : 0.0f;
        modal_y2_6 = (mode_count > 5) ? cosf(w6) : 0.0f; modal_y1_6 = (mode_count > 5) ? 1.0f : 0.0f;
        modal_k_1 = 2.0f * modal_y2_1;
        modal_k_2 = 2.0f * modal_y2_2;
        modal_k_3 = 2.0f * modal_y2_3;
        modal_k_4 = 2.0f * modal_y2_4;
        modal_k_5 = 2.0f * modal_y2_5;
        modal_k_6 = 2.0f * modal_y2_6;
        modal_norm_count = 0;
        modal_env_1 = env1 * current_velocity;
        modal_env_2 = env2 * current_velocity;
        modal_env_3 = (mode_count > 2) ? (env3 * current_velocity) : 0.0f;
        modal_env_4 = (mode_count > 3) ? (env4 * current_velocity) : 0.0f;
        modal_env_5 = (mode_count > 4) ? (env5 * current_velocity) : 0.0f;
        modal_env_6 = (mode_count > 5) ? (env6 * current_velocity) : 0.0f;
        float t60_1_s = 0.001f * t60_1_ms;
        float t60_2_s = 0.001f * t60_2_ms;
        float t60_3_s = 0.001f * t60_3_ms;
        float t60_4_s = 0.001f * t60_4_ms;
        // Use expf (not fasterexpf) here: fasterexpf is catastrophically wrong for
        // very small arguments (|x| < 0.001), e.g. T60=5s gives x=-0.0000288 but
        // fasterexpf returns 0.971 instead of 0.99997. modal_decay is computed once
        // at NoteOn time, so accuracy matters far more than speed.
        modal_decay_1 = (t60_1_s > 0.0f) ? expf(k_dsp_log_0001 / (t60_1_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY1;
        modal_decay_2 = (t60_2_s > 0.0f) ? expf(k_dsp_log_0001 / (t60_2_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        modal_decay_3 = (mode_count > 2 && t60_3_s > 0.0f) ? expf(k_dsp_log_0001 / (t60_3_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        modal_decay_4 = (mode_count > 3 && t60_4_s > 0.0f) ? expf(k_dsp_log_0001 / (t60_4_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        // Modes 5 and 6 share T60_4's base but decay faster: T60_5 = 0.85×T60_4,
        // T60_6 = 0.70×T60_4.  Reusing decay_4 via power law avoids two extra expf
        // calls: exp(k / (r*T)) = exp(k/T)^(1/r) = decay_4^(1/r).
        modal_decay_5 = (mode_count > 4 && t60_4_s > 0.0f) ? powf(modal_decay_4, 1.0f / 0.85f) : STAGE2_MODAL_DECAY2;
        modal_decay_6 = (mode_count > 5 && t60_4_s > 0.0f) ? powf(modal_decay_4, 1.0f / 0.70f) : STAGE2_MODAL_DECAY2;
        modal_mix = mix;
    }
};

// Global Synth State (4 Voices limit for strict CPU budgeting)
constexpr int NUM_VOICES = 4;
struct SynthState {
    VoiceState voices[NUM_VOICES];
    uint8_t next_voice_idx  = 0;

    // Master FX
    float mix_ab            = 0.5f;   // ← non-zero
    float master_gain       = 1.0f;   // ← non-zero
    float master_drive      = 1.0f;   // ← non-zero
    float tone              = 0.0f;   // Tilt EQ amount, cached from k_paramTone [-10, 30]

    FastSVF master_filter;
    // NO user constructor — DMIs only so globals go to .data, not .bss.
    // See WaveguideState comment above for the full explanation.
};

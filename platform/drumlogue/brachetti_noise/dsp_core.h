#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include "noise.h"
#include "envelope.h"
#include "filter.h"
#include "float_math.h"

/** Because we are optimizing for bare-metal, notice there are no virtual functions,
 * no dynamic memory (new/malloc), and no deep class hierarchies.
 * Just pure data that the CPU cache can read sequentially. */

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
    size_t  current_frame    = 0;     // samples since the strike

    FastNoise    noise_gen;           // seed=2463534242 (non-zero) → forces .data
    FastEnvelope noise_env;           // attack_rate=0.01f (non-zero)
    FastEnvelope noise_env_hi;        // short high-band burst for snare/hat click
    FastEnvelope master_env;          // Optional: To choke the whole voice on GateOff

    // Noise Burst Data
    float noise_decay_coeff   = 0.0f;

    // Computed each sample by process_exciter; read by processBlock.
    float noise_out_sample    = 0.0f;
    float noise_lp_state      = 0.0f;   // stage-1 dual-band noise shaper LP state
    float noise_band_mix      = 0.5f;   // ← non-zero
    float noise_hi_lp_state   = 0.0f;   // high-band split LP memory
    float noise_hi_lp_coeff   = 0.30f;  // ← non-zero
    // updated runtime
    float mallet_lp           = 0.0f;
    float mallet_lp2          = 0.0f;   // Second LP pole state (MlltRes)
    float mallet_stiffness    = 0.5f;   // ← non-zero
    float mallet_res_coeff    = 0.5f;   // ← non-zero

    FastSVF noise_filter;    // Dedicated per-voice noise shaping SVF (NzFltr / NzFltFrq)
};

// Per-sample geometric-envelope multiplier for time constant tauSec (the
// preset-change fade).  Exact expf: the argument is ~-1e-4..-1e-2, where
// float_math.h's fast* variants are catastrophically wrong.
static inline float env_mul(float tauSec, float sr) {
    if (tauSec < 0.00002f) return 0.0f;
    return expf(-1.0f / (tauSec * sr));
}

/**
 * The Master Voice Structure.
 * Holds the exciter (mallet + noise) and the per-strike layers this unit uses.
 */
struct VoiceState {
    bool    is_active      = false;
    bool    is_releasing   = false;
    uint8_t current_note   = 60;    // ← non-zero → forces containing struct to .data
    float   current_velocity = 0.0f;

    // Preset-change fade.  A voice cannot survive a preset change — the new
    // preset overwrites the shared per-voice DSP coefficients — but cutting it
    // dead clicks, and letting it run re-excited it (a cymbal voice holds an
    // unstarted exciter, so the legacy path fired a whole unplayed attack).
    // So it fades out under its OWN engine over ~10 ms and is then reclaimed.
    // fade_mul stays exactly 1.0f in normal play and the fade block is skipped
    // entirely, so shipped renders are bit-identical.
    float fade      = 1.0f;   // ← non-zero
    float fade_mul  = 1.0f;   // ← non-zero (< 1 only while fading out)

    ExciterState   exciter;

    // Tilt-EQ memory
    float tone_lp            = 0.0f;

    // Dynamic Energy Squelch (MAV envelope follower, α=0.01, τ≈2ms)
    float mag_env            = 0.0f;

    // Enveloped-LFO amplitude gate on the parallel noise (Clap multi-burst,
    // Shaker grain pulses).  gate = 1 − depth·(0.5 + 0.5·sin(phase)); depth
    // decays each sample so the modulation fades into the plain tail.
    float noise_am_phase  = 0.0f;
    float noise_am_inc    = 0.0f;
    float noise_am_depth  = 0.0f;
    float noise_am_decay  = 1.0f;   // ← non-zero

    void PartialReset() {
        mag_env = 0.0f;
        // A re-struck slot is no longer fading.
        fade = 1.0f;
        fade_mul = 1.0f;

        // Reset exciter frame counter so the mallet fires on every NoteOn and
        // the kSquelchGuardSamples window restarts from zero for the new note.
        exciter.current_frame = 0;
        exciter.mallet_lp  = 0.0f;
        exciter.mallet_lp2 = 0.0f;

        tone_lp = 0.0f;
        noise_am_phase = 0.0f;
        noise_am_inc = 0.0f;
        noise_am_depth = 0.0f;
        noise_am_decay = 1.0f;
        // exciter state
        exciter.current_frame = 0;
        exciter.mallet_lp  = 0.0f;
        exciter.mallet_lp2 = 0.0f;
        exciter.noise_lp_state = 0.0f; // updated runtime
        exciter.noise_band_mix = 0.5f;
        exciter.noise_hi_lp_state = 0.0f; // updated runtime
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

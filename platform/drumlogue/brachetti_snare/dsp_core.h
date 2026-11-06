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
    FastEnvelope noise_env_hi;        // short high-band burst (the click)
    FastEnvelope master_env;          // Optional: To choke the whole voice on GateOff

    // Noise Burst Data
    float noise_decay_coeff   = 0.0f;

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
    // dead clicks, and letting it run plays the old voice through the new
    // preset's coefficients.  So it fades out over ~10 ms and is then
    // reclaimed.
    // fade_mul stays exactly 1.0f in normal play and the fade block is skipped
    // entirely, so shipped renders are bit-identical.
    float fade      = 1.0f;   // ← non-zero
    float fade_mul  = 1.0f;   // ← non-zero (< 1 only while fading out)

    ExciterState   exciter;

    // Tilt-EQ memory
    float tone_lp            = 0.0f;

    // Dynamic Energy Squelch (MAV envelope follower, α=0.01, τ≈2ms)
    float mag_env            = 0.0f;

    // Modal bank (up to 6 two-pole modes)
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

    // Pitch envelope (the boom sweeps read it)
    float pitch_env       = 0.0f;
    float pitch_env_decay = 1.0f;   // ← non-zero
    float pitch_env_amt   = 0.0f;

    // Low-body boom oscillator
    float boom_phase      = 0.0f;
    float boom_inc        = 0.0f;
    float boom_env        = 0.0f;
    float boom_decay      = 1.0f;   // ← non-zero
    float boom_mix        = 0.0f;
    float boom_attack_env = 1.0f;   // ← non-zero
    float boom_attack_inc = 1.0f;   // ← non-zero
    // Boom base-tune multiplier, 1.0 = shipped (bit-identical).  Multiplies
    // the per-sample boom frequency in processBlock: a swept boom recomputes
    // boom_inc every sample, so its tune must live here.
    float boom_tune       = 1.0f;   // ← non-zero

    // Global onset ramp
    float onset_env = 1.0f;         // ← non-zero
    float onset_inc = 0.0f;

    // Enveloped-LFO amplitude gate on the parallel noise (BrshSnr's swirl).
    // gate = 1 − depth·(0.5 + 0.5·sin(phase)); depth
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
        // boom oscillator
        boom_phase = 0.0f;
        boom_inc = 0.0f;
        boom_env = 0.0f;
        boom_decay = 1.0f;
        boom_mix = 0.0f;
        boom_attack_env = 1.0f;
        boom_attack_inc = 1.0f;
        boom_tune = 1.0f;
        onset_env = 1.0f;
        onset_inc = 0.0f;
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
        exciter.snare_wire_z1 = 0.0f;
        exciter.snare_wire_z2 = 0.0f;
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
    }

    // Modal bank set-up for one strike: up to six two-pole oscillators
    // (y[n] = k·y[n-1] − y[n-2]) at the given ratios, envelopes scaled by the
    // velocity, decays from the T60s.
    void init_modal_modes(float ratio2, float ratio3, float ratio4,
                         float t60_1_ms, float t60_2_ms, float t60_3_ms, float t60_4_ms,
                         float mix, float env1, float env2, float env3, float env4,
                         uint8_t mode_count, float ratio5 = 0.0f, float ratio6 = 0.0f,
                         float env5 = 0.0f, float env6 = 0.0f) {
        uint8_t note = current_note;
        // Exact math here, NOT the faster* approximations: fastercosfullf's ~1e-3
        // error shifts low mode frequencies (an 80-170 Hz 1 : 1.504 : 1.742 spread
        // collapses toward 1 : 1.41 : 1.61, and the modes beat).  This runs once per
        // NoteOn, so accuracy wins.
        float base_f = 440.0f * exp2f(((float)note - 69.0f) * 0.08333333333f); // 1/12
        if (base_f < 20.0f) base_f = 20.0f;
        float f1 = fminf(base_f, 0.45f * k_dsp_sample_rate);
        float f2 = fminf(base_f * ratio2, 0.45f * k_dsp_sample_rate);
        float f3 = fminf(base_f * ratio3, 0.45f * k_dsp_sample_rate);
        float f4 = fminf(base_f * ratio4, 0.45f * k_dsp_sample_rate);
        // Modes 5/6: use explicit ratios when provided (physical plate theory),
        // otherwise derive them from mode 4 (ratio4 × 1.31 / 1.62).
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
    float master_gain       = 1.0f;   // ← non-zero
    float master_drive      = 1.0f;   // ← non-zero
    // Master limiter peak-follower state (see the Stage 4b comment): holds the
    // recent peak so ONE gain can be applied per cycle instead of reshaping the
    // waveform sample by sample.  Starts at 0 = no gain reduction.
    float master_lim_env    = 0.0f;
    float tone              = 0.0f;   // Tilt EQ amount, cached from k_paramTone [-10, 30]

    FastSVF master_filter;
    // NO user constructor — DMIs only so globals go to .data, not .bss.
    // A user constructor would make the global synth object dynamically
    // initialised, and GCC then places it in .bss (zero-filled at load);
    // with default member initialisers only it is constant-initialised and
    // loaded straight from the ELF.
};

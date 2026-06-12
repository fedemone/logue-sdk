#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include "noise.h"
#include "envelope.h"
#include "filter.h"
#include "tables.h"


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
// This is required so that the global `static RipplerXWaveguide s_synth` in
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
    // Physics Topology Multiplier (+1.0f for String, -1.0f for Tube)
    float phase_mult    = 1.0f;    // ← non-zero
    // Per-model baseline allpass dispersion (added to ap_coeff from Inharm parameter).
    float model_ap_base = 0.0f;
};

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
        float w1 = (2.0f * M_PI * f1) * k_dsp_inv_sample_rate;
        float w2 = (2.0f * M_PI * f2) * k_dsp_inv_sample_rate;
        float w3 = (2.0f * M_PI * f3) * k_dsp_inv_sample_rate;
        float w4 = (2.0f * M_PI * f4) * k_dsp_inv_sample_rate;
        float w5 = (2.0f * M_PI * f5) * k_dsp_inv_sample_rate;
        float w6 = (2.0f * M_PI * f6) * k_dsp_inv_sample_rate;
        modal_pilot_enabled = true;
        modal_mode_count = mode_count;
        modal_k_1 = 2.0f * cosf(w1);
        modal_k_2 = 2.0f * cosf(w2);
        modal_k_3 = (mode_count > 2) ? 2.0f * cosf(w3) : 0.0f;
        modal_k_4 = (mode_count > 3) ? 2.0f * cosf(w4) : 0.0f;
        modal_k_5 = (mode_count > 4) ? 2.0f * cosf(w5) : 0.0f;
        modal_k_6 = (mode_count > 5) ? 2.0f * cosf(w6) : 0.0f;
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

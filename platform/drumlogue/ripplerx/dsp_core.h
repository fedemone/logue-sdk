#pragma once
#include <cstdint>
#include <cstddef>
#include "noise.h"
#include "envelope.h"
#include "filter.h"
#include "tables.h"


/** Because we are optimizing for bare-metal, notice there are no virtual functions,
 * no dynamic memory (new/malloc), and no deep class hierarchies.
 * Just pure data that the CPU cache can read sequentially. */

// A power-of-two buffer size allows us to use bitwise AND (& 4095) for lightning-fast
// wrap-around instead of slow modulo (%) operators.
// 4096 samples @ 48kHz = ~85.3ms max delay (Deepest possible pitch: ~11.7 Hz)
constexpr size_t DELAY_BUFFER_SIZE = 4096;
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
    const float* sample_ptr = nullptr;
    size_t  sample_frames;
    size_t  current_frame;
    uint8_t channels;

    FastNoise noise_gen;
    FastEnvelope noise_env;
    FastEnvelope noise_env_hi; // short high-band burst for snare/hat click
    FastEnvelope master_env;   // Optional: To choke the whole voice on GateOff

    // Noise Burst Data
    float noise_decay_coeff;
    float current_noise_env;

    // Computed each sample by process_exciter; read by processBlock.
    // Noise is NOT injected into the waveguide (which would tonalize it).
    // Instead the render loop mixes it directly into the voice output (parallel path).
    // Exception: tube models (OpenTube=7, ClosedTube=8) also receive noise into their
    // waveguide for physically correct sustained breath excitation (flute, clarinet).
    float noise_out_sample;
    float noise_lp_state;    // stage-1 dual-band noise shaper LP state
    float noise_band_mix;    // 0=mostly low thump, 1=mostly high click
    float noise_hi_lp_state; // high-band split LP memory (high - LP)
    float noise_hi_lp_coeff; // derived private cutoff, no UI exposure
    float wire_onset_env;    // snare-wire onset gate (0..1)
    float wire_onset_attack; // per-sample rise rate for wire onset
    float snare_wire_z1;     // short wire-sizzle resonator state
    float snare_wire_z2;     // short wire-sizzle resonator state
    float snare_wire_lp;     // helper state for multiband wire split
    float snare_wire_hp;     // helper state for multiband wire split
    float snare_wire_mix;    // 0..1 extra wire component mix
    float snare_wire_a1;     // resonator coeff (frequency/Q)
    float snare_wire_a2;     // resonator coeff (frequency/Q)
    // 3-band snare wire: bands B (~4.5 kHz) and C (~7.2 kHz) complement band A.
    // Parallel IIR resonators per Cook, "Real Sound Synthesis for Interactive
    // Applications" (2002), ch. 5.
    float snare_wire_z1b = 0.0f;
    float snare_wire_z2b = 0.0f;
    float snare_wire_a1b = 1.620f;   // 4.5 kHz at r=0.971 (Q≈10)
    float snare_wire_a2b = 0.942f;
    float snare_wire_z1c = 0.0f;
    float snare_wire_z2c = 0.0f;
    float snare_wire_a1c = 1.120f;   // 7.2 kHz at r=0.953 (Q≈10)
    float snare_wire_a2c = 0.908f;
    // updated runtime
    float mallet_lp;
    float mallet_lp2;        // Second LP pole state (MlltRes)
    float mallet_stiffness;
    float mallet_res_coeff;  // Second LP pole coefficient (MlltRes)

    FastSVF noise_filter;    // Dedicated per-voice noise shaping SVF (NzFltr / NzFltFrq)
    FastSVF hat_filter;      // Dedicated hi-hat centroid filter (biquad/TPT)
    bool use_hat_filter;

    // default constructor
    ExciterState() : noise_gen(), noise_env(), noise_env_hi(), master_env(), noise_filter(), hat_filter(),
    sample_frames(0), current_frame(0), channels(1), noise_decay_coeff(0.0f), current_noise_env(0.0f),
    noise_out_sample(0.0f), noise_lp_state(0.0f), noise_band_mix(0.5f), noise_hi_lp_state(0.0f),
    noise_hi_lp_coeff(0.30f), wire_onset_env(1.0f), wire_onset_attack(1.0f), snare_wire_z1(0.0f),
    snare_wire_z2(0.0f), snare_wire_lp(0.0f), snare_wire_hp(0.0f), snare_wire_mix(0.0f), snare_wire_a1(1.6951f), snare_wire_a2(0.8930f),
    snare_wire_z1b(0.0f), snare_wire_z2b(0.0f), snare_wire_a1b(1.620f), snare_wire_a2b(0.942f),
    snare_wire_z1c(0.0f), snare_wire_z2c(0.0f), snare_wire_a1c(1.120f), snare_wire_a2c(0.908f),
    mallet_lp(0.0f), mallet_lp2(0.0f), mallet_stiffness(0.5f), mallet_res_coeff(0.5f),
    use_hat_filter(false) {} ;
};

/**
 * A single Digital Waveguide (Physical Model).
 * Contains the delay line memory and the loop filter states.
 */
struct WaveguideState {
    float buffer[DELAY_BUFFER_SIZE];
    uint32_t write_ptr;
    // Fractional tuning for exact pitch
    float delay_length;

    // Fast-math loop coefficients (Calculated from UI parameters)
    float feedback_gain;    // Determines Decay Time
    float lowpass_coeff;    // Determines Material/Tone
    float loss_g_dc;        // Frequency-independent sustain factor
    float loss_g_hf;        // HF feedback loss factor (0..1)
    float ap_coeff; // Allpass coefficient (-0.99 to 0.99)
    float ap_x1;    // Allpass delayed input
    float ap_y1;    // Allpass delayed output

    // Filter State Memory
    float z1;               // 1st lowpass pole history
    float z2;               // 2nd lowpass pole history (optional, per-preset)
    float diffuser_mix;     // 0=off, >0 enables 4-stage Schroeder allpass chain
    float diffuser_g;
    float diffuser_buf1[13];
    float diffuser_buf2[19];
    float diffuser_buf3[29];
    float diffuser_buf4[41];
    uint8_t diffuser_i1, diffuser_i2, diffuser_i3, diffuser_i4;
    bool bypass_loop_lp = false;
    // Physics Topology Multiplier (+1.0f for String, -1.0f for Tube)
    float phase_mult;
    // Per-model baseline allpass dispersion (added to ap_coeff from Inharm parameter).
    // Gives each physical model a distinct inharmonic character even when Inharm=0.
    float model_ap_base;

    // default constructor
    WaveguideState() : buffer(), write_ptr(0), delay_length(0.0f), feedback_gain(0.0f), lowpass_coeff(1.0f),
    loss_g_dc(1.0f), loss_g_hf(1.0f), ap_coeff(0.0f), ap_x1(0.0f), ap_y1(0.0f), z1(0.0f), z2(0.0f),
    diffuser_mix(0.0f), diffuser_g(0.45f), diffuser_buf1(), diffuser_buf2(), diffuser_buf3(), diffuser_buf4(),
    diffuser_i1(0), diffuser_i2(0), diffuser_i3(0), diffuser_i4(0), bypass_loop_lp(false), phase_mult(1.0f), model_ap_base(0.0f) {} ;
};

/**
 * The Master Voice Structure.
 * Holds the Exciter and two parallel Resonators (A and B).
 */
struct VoiceState {
    bool is_active;
    bool is_releasing;

    uint8_t current_note;
    float current_velocity;

    ExciterState exciter;
    WaveguideState resA;
    WaveguideState resB;

    // Coupling and Tone memory
    // Both _prev fields hold the previous-sample output for symmetric 1-sample-delayed
    // bidirectional coupling.  Using the current ResA output to feed ResB in the same
    // sample (zero delay) was physically asymmetric and created a slight formant artefact.
    float resA_out_prev; // ResA output from previous sample (bidirectional coupling)
    float resB_out_prev; // ResB output from previous sample (bidirectional coupling)
    float tone_lp;       // 1-pole LP state for the Tone tilt EQ

    // Pitch Bend: pre-bend delay lengths so PitchBend() can reapply the multiplier
    // without accumulating error across successive bend messages.
    float base_delay_A;
    float base_delay_B;

    // Dynamic Energy Squelch: 1-pole mean-absolute-value envelope follower.
    // Named mag_env (magnitude) because it smooths |x|, not x² — that is MAV,
    // not RMS.  For the −80 dB silence gate both are equivalent in practice.
    // Smoothing: α=0.01 → τ ≈ 100 samples ≈ 2 ms at 48 kHz.
    float mag_env;

    // Stage-1 transient complexity boost (short post-strike modulation window).
    uint32_t transient_frames_left;
    uint32_t transient_frames_total;
    float transient_inv_total; // 1 / transient_frames_total (cached)
    float transient_lp_jitter;
    float transient_ap_jitter;
    // Unmodulated per-voice coefficient anchors (set by UI/NoteOn).
    // Transient modulation is always applied relative to these bases.
    float transient_lp_base_a;
    float transient_lp_base_b;
    float transient_ap_base_a;
    float transient_ap_base_b;

    // Stage-2 pilot modal-bank path (2 modes) for single-preset A/B.
    bool modal_pilot_enabled;
    // Harmonic quadrature recursion (2*cos(w) form), one biquad-like
    // second-order recursion per mode:
    // y[n] = k*y[n-1] - y[n-2], where k = 2*cos(w).
    float modal_k_1;
    float modal_k_2;
    float modal_k_3;
    float modal_k_4;
    float modal_k_5;
    float modal_k_6;
    float modal_y1_1;
    float modal_y1_2;
    float modal_y1_3;
    float modal_y1_4;
    float modal_y2_1;
    float modal_y2_2;
    float modal_y2_3;
    float modal_y2_4;
    float modal_y1_5;
    float modal_y1_6;
    float modal_y2_5;
    float modal_y2_6;
    uint32_t modal_norm_count;
    float modal_env_1;
    float modal_env_2;
    float modal_env_3;
    float modal_env_4;
    float modal_env_5;
    float modal_env_6;
    float modal_decay_1;
    float modal_decay_2;
    float modal_decay_3;
    float modal_decay_4;
    float modal_decay_5;
    float modal_decay_6;
    float modal_mix;
    uint8_t modal_mode_count; // 2 default pilot, up to 6 for metallic models
    // Stage-2 kick pitch-envelope (delay sweep) and clarinet reed nonlinearity.
    float pitch_env;
    float pitch_env_decay;
    float pitch_env_amt;
    bool  reed_nl_enabled;
    float reed_nl_drive;
    // Low-body boom oscillator (classics rescue path: kick/timpani/tom/snare).
    float boom_phase;
    float boom_inc;
    float boom_env;
    float boom_decay;
    float boom_mix;
    float boom_attack_env;
    float boom_attack_inc;
    // Metallic transient FM chirp (character-focused, not exact sample match).
    float metal_fm_phase;
    float metal_fm_inc;
    float metal_fm_env;
    float metal_fm_decay;
    float metal_fm_depth;
    // Dedicated post-resonator high-band branch (CPU-light structural path).
    float hf_branch_env;
    float hf_branch_decay;
    float hf_branch_mix;
    float hf_branch_lp;
    // Global onset ramp — ramps full voice_out from 0→1 over onset_attack_ms milliseconds.
    float onset_env;
    float onset_inc; // 0 = disabled (env stays 1.0), >0 = ramp rate per sample

    // default constructor
    VoiceState() : is_active(false), is_releasing(false),
    current_note(60), current_velocity(0.0f),
    exciter(), resA(), resB(), resA_out_prev(0.0f), resB_out_prev(0.0f),
    tone_lp(0.0f), base_delay_A(0.0f), base_delay_B(0.0f), mag_env(0.0f),
    transient_frames_left(0), transient_frames_total(0), transient_inv_total(0.0f), transient_lp_jitter(0.0f),
    transient_ap_jitter(0.0f), transient_lp_base_a(1.0f), transient_lp_base_b(1.0f), transient_ap_base_a(0.0f),
    transient_ap_base_b(0.0f),
    modal_pilot_enabled(false),
    modal_k_1(0.0f), modal_k_2(0.0f), modal_k_3(0.0f), modal_k_4(0.0f), modal_k_5(0.0f), modal_k_6(0.0f),
    modal_y1_1(0.0f), modal_y1_2(0.0f), modal_y1_3(0.0f), modal_y1_4(0.0f), modal_y2_1(0.0f), modal_y2_2(0.0f),
    modal_y2_3(0.0f), modal_y2_4(0.0f), modal_y1_5(0.0f), modal_y1_6(0.0f), modal_y2_5(0.0f), modal_y2_6(0.0f),
    modal_norm_count(0), modal_env_1(0.0f), modal_env_2(0.0f), modal_env_3(0.0f), modal_env_4(0.0f),
    modal_env_5(0.0f), modal_env_6(0.0f), modal_decay_1(0.9990f), modal_decay_2(0.9985f), modal_decay_3(0.9980f),
    modal_decay_4(0.9975f), modal_decay_5(0.9970f), modal_decay_6(0.9965f), modal_mix(0.0f), modal_mode_count(0),
    pitch_env(0.0f), pitch_env_decay(1.0f), pitch_env_amt(0.0f),
    reed_nl_enabled(false), reed_nl_drive(1.0f),
    boom_phase(0.0f), boom_inc(0.0f), boom_env(0.0f), boom_decay(1.0f), boom_mix(0.0f), boom_attack_env(1.0f), boom_attack_inc(1.0f),
    metal_fm_phase(0.0f), metal_fm_inc(0.0f), metal_fm_env(0.0f), metal_fm_decay(1.0f), metal_fm_depth(0.0f),
    hf_branch_env(0.0f), hf_branch_decay(1.0f), hf_branch_mix(0.0f), hf_branch_lp(0.0f),
    onset_env(1.0f), onset_inc(0.0f) {};

    void PartialReset() {
        mag_env = 0.0f;

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
        exciter.snare_wire_a2 = 0.8930f;;
        exciter.snare_wire_z1b = 0.0f;
        exciter.snare_wire_z2b = 0.0f;
        exciter.snare_wire_z1c = 0.0f;
        exciter.snare_wire_z2c = 0.0f;

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
                         uint8_t mode_count, float ratio5 = 0.0f, float ratio6 = 0.0f) {
        uint8_t note = current_note;
        float base_f = 440.0f * fasterpowf(2.0f, ((float)note - 69.0f) * 0.08333333333f); // approx 1/12
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
        modal_k_1 = 2.0f * fastercosfullf(w1);
        modal_k_2 = 2.0f * fastercosfullf(w2);
        modal_k_3 = (mode_count > 2) ? 2.0f * fastercosfullf(w3) : 0.0f;
        modal_k_4 = (mode_count > 3) ? 2.0f * fastercosfullf(w4) : 0.0f;
        modal_k_5 = (mode_count > 4) ? 2.0f * fastercosfullf(w5) : 0.0f;
        modal_k_6 = (mode_count > 5) ? 2.0f * fastercosfullf(w6) : 0.0f;
        modal_y2_1 = 0.0f; modal_y1_1 = fastersinfullf(w1);
        modal_y2_2 = 0.0f; modal_y1_2 = fastersinfullf(w2);
        modal_y2_3 = 0.0f; modal_y1_3 = (mode_count > 2) ? fastersinfullf(w3) : 0.0f;
        modal_y2_4 = 0.0f; modal_y1_4 = (mode_count > 3) ? fastersinfullf(w4) : 0.0f;
        modal_y2_5 = 0.0f; modal_y1_5 = (mode_count > 4) ? fastersinfullf(w5) : 0.0f;
        modal_y2_6 = 0.0f; modal_y1_6 = (mode_count > 5) ? fastersinfullf(w6) : 0.0f;
        modal_norm_count = 0;
        modal_env_1 = env1 * current_velocity;
        modal_env_2 = env2 * current_velocity;
        modal_env_3 = (mode_count > 2) ? (env3 * current_velocity) : 0.0f;
        modal_env_4 = (mode_count > 3) ? (env4 * current_velocity) : 0.0f;
        modal_env_5 = (mode_count > 4) ? (0.22f * env4 * current_velocity) : 0.0f;
        modal_env_6 = (mode_count > 5) ? (0.16f * env4 * current_velocity) : 0.0f;
        float t60_1_s = 0.001f * t60_1_ms;
        float t60_2_s = 0.001f * t60_2_ms;
        float t60_3_s = 0.001f * t60_3_ms;
        float t60_4_s = 0.001f * t60_4_ms;
        modal_decay_1 = (t60_1_s > 0.0f) ? fasterexpf(k_dsp_log_0001 / (t60_1_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY1;
        modal_decay_2 = (t60_2_s > 0.0f) ? fasterexpf(k_dsp_log_0001 / (t60_2_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        modal_decay_3 = (mode_count > 2 && t60_3_s > 0.0f) ? fasterexpf(k_dsp_log_0001 / (t60_3_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        modal_decay_4 = (mode_count > 3 && t60_4_s > 0.0f) ? fasterexpf(k_dsp_log_0001 / (t60_4_s * k_dsp_sample_rate)) : STAGE2_MODAL_DECAY2;
        // Modes 5 and 6 share T60_4's base but decay faster: T60_5 = 0.85×T60_4,
        // T60_6 = 0.70×T60_4.  Reusing decay_4 via power law avoids two extra expf
        // calls: exp(k / (r*T)) = exp(k/T)^(1/r) = decay_4^(1/r).
        modal_decay_5 = (mode_count > 4 && t60_4_s > 0.0f) ? fasterpowf(modal_decay_4, 1.0f / 0.85f) : STAGE2_MODAL_DECAY2;
        modal_decay_6 = (mode_count > 5 && t60_4_s > 0.0f) ? fasterpowf(modal_decay_4, 1.0f / 0.70f) : STAGE2_MODAL_DECAY2;
        modal_mix = mix;
    }
};

// Global Synth State (4 Voices limit for strict CPU budgeting)
constexpr int NUM_VOICES = 4;
struct SynthState {
    VoiceState voices[NUM_VOICES];
    uint8_t next_voice_idx;

    // Master FX
    float mix_ab;
    float master_gain;
    float master_drive;
    float tone;        // Tilt EQ amount, cached from k_paramTone [-10, 30]

    FastSVF master_filter;

    // default constructor
    SynthState() : voices(), next_voice_idx(0), mix_ab(0.5f),
    master_gain(1.0f), master_drive(1.0f),
    tone(0.0f), master_filter() {} ;
};

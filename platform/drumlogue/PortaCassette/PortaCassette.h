#pragma once
/**
 * @file PortaCassette.h
 * @brief Tascam Portastudio / vinyl emulator for KORG drumlogue
 *
 * Signal chain, evaluated four frames at a time:
 *
 *   Trim -> Console clip (2x) -> PEQ(3-band) -> dbx encode
 *        -> Record EQ -> Drive/Saturation (2x) -> Playback EQ
 *        -> Hiss or Vinyl dust -> Head bump -> Tape LPF -> Wow & Flutter
 *        -> Crosstalk -> dbx decode -> DC block -> Dry/Wet -> Soft ceiling
 *
 * The noise sources sit *inside* the dbx encode/decode loop on purpose: that is
 * where tape noise enters on a real machine, and it is what lets the decoder
 * actually reduce it.  So does everything else the tape does: the decoder has
 * its own detector, so it hears the tape's saturation and treble loss and
 * reacts to them, as a real dbx decoder does.
 */

#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <arm_neon.h>

#include "unit.h"
#include "float_math.h"
#include "biquad.h"
#include "halfband.h"

// Wow & Flutter delay line: 512 frames ~ 10.7 ms at 48 kHz.  Worst-case read
// offset is (30*1.8 + 2.8) * 2.0 = 113.6 frames at full Age on the T-424N (the
// deepest wf_scale), plus two guard taps.
#define WF_BUFFER_SIZE 512
#define WF_BUFFER_MASK (WF_BUFFER_SIZE - 1)
#define NEON_LANES (4)

// PRNG state (Xorshift128+, two 64-bit lanes each)
struct prng_t { uint64x2_t state0, state1; };

// ============================================================================
// NEON 4-wide sine — bit-identical vectorization of fastsinf/fastersinfullf
// (Paul Mineiro FastFloat).  One call covers both wow & flutter LFOs.
// ============================================================================
static fast_inline float32x4_t fastsinf_ps(float32x4_t x) {
    const uint32x4_t sign  = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u));
    const float32x4_t absx = vabsq_f32(x);
    float32x4_t qpprox = vsubq_f32(vmulq_n_f32(x, M_4_PI),
                                   vmulq_f32(vmulq_n_f32(x, M_4_PI2), absx));
    float32x4_t qpproxsq = vmulq_f32(qpprox, qpprox);
    // p,r get x's sign OR'd in; s gets it XOR'd (matches the scalar bit-tricks).
    float32x4_t p = vreinterpretq_f32_u32(vorrq_u32(
        vreinterpretq_u32_f32(vdupq_n_f32(0.20363937680730309f)), sign));
    float32x4_t r = vreinterpretq_f32_u32(vorrq_u32(
        vreinterpretq_u32_f32(vdupq_n_f32(0.015124940802184233f)), sign));
    float32x4_t s = vreinterpretq_f32_u32(veorq_u32(
        vreinterpretq_u32_f32(vdupq_n_f32(-0.0032225901625579573f)), sign));
    float32x4_t inner = vmlaq_f32(r, qpproxsq, s);          // r + qpproxsq*s
    inner = vmlaq_f32(p, qpproxsq, inner);                  // p + qpproxsq*(...)
    return vmlaq_f32(vmulq_n_f32(qpprox, 0.78444488374548933f), qpproxsq, inner);
}

// Full-domain sine.  fastsinf_ps range-reduces internally, so callers may pass
// un-wrapped (monotonically increasing) phases and still get bit-identical
// results to per-sample fastersinfullf() (sine is 2*pi-periodic).
static fast_inline float32x4_t fastersinfullf_ps(float32x4_t x) {
    float32x4_t kf   = vcvtq_f32_s32(vcvtq_s32_f32(vmulq_n_f32(x, M_1_TWOPI))); // trunc toward zero
    float32x4_t half = vbslq_f32(vcltq_f32(x, vdupq_n_f32(0.0f)),
                                 vdupq_n_f32(-0.5f), vdupq_n_f32(0.5f));
    float32x4_t arg  = vsubq_f32(vmulq_n_f32(vaddq_f32(half, kf), M_TWOPI), x);
    return fastsinf_ps(arg);
}

// 1/x, ~1.5e-5 relative: one Newton-Raphson step on the NEON estimate.  Ample
// for a saturator denominator, where the result is about to be squashed anyway.
static fast_inline float32x4_t recip_fast(float32x4_t x) {
    const float32x4_t r = vrecpeq_f32(x);
    return vmulq_f32(r, vrecpsq_f32(x, r));
}

// 1/x, ~1e-7 relative: a second step, for the dbx decoder's gain, which has to
// invert the encoder's closely enough that the pair cancels.
static fast_inline float32x4_t recip_nr(float32x4_t x) {
    const float32x4_t r = recip_fast(x);
    return vmulq_f32(r, vrecpsq_f32(x, r));
}

class alignas(16) PortaCassette {
public:
    enum ParamId {
        k_param_age = 0,
        k_param_mix,
        k_param_preamp,
        k_param_drive,
        k_param_dbx,
        k_param_model,
        k_param_eq_low_hz,
        k_param_eq_low_gain,
        k_param_eq_mid_hz,
        k_param_eq_mid_gain,
        k_param_eq_high_hz,
        k_param_eq_high_gain,
        k_crosstalk,
        k_hiss,
        k_attack,
        k_release,
        k_num_params
    };

    // New entries are appended, so a value saved by an earlier version still
    // selects the same mode.
    enum DbxMode { k_active = 0, k_encode_only, k_off, k_decode_only };
    enum k7Mode  { k_244    = 0, k_424, k_488, k_vinyl, k_424n };

    // Mirrors the .init column of header.c.  Kept here so the DSP is coherent
    // before the host has pushed any parameter, and so unit_reset() can clear
    // filter state without also forgetting what the user dialled in.
    static constexpr int32_t kParamInit[k_num_params] = {
        10,   // age        10 %
        100,  // mix       100 %
        20,   // trim       20 % (+6 dB in, -3 dB fader: +3 dB, as before)
        40,   // drive      40 %
        0,    // dbx        Active
        0,    // model      T-244
        8,    // eq low hz    80 Hz
        120,  // eq low gain   0 dB
        100,  // eq mid hz  1000 Hz
        120,  // eq mid gain   0 dB
        100,  // eq high hz  10 kHz
        120,  // eq high gain  0 dB
        1,    // crosstalk   1 % (the published separation; 5 % was 17 dB worse)
        15,   // hiss       15 %
        30,   // attack     3.0 ms
        150,  // release    150 ms
    };

    PortaCassette() { Configure(48000.0f); }
    ~PortaCassette() {}

    /// The drumlogue hands a master effect four input channels per frame --
    /// main L, main R, sidechain L, sidechain R (see the SDK's masterfx
    /// template) -- and this unit used to read its input as plain stereo
    /// pairs, so on a four-channel bus it took each frame's sidechain pair for
    /// the next frame and only ever got through the first half of the buffer.
    /// It now steps through the input at whatever width the runtime declares
    /// and uses the main pair; a two-channel bus (the SDK notes the sidechain
    /// might be dropped) still works.
    int8_t Init(const unit_runtime_desc_t* desc) {
        if (desc->input_channels != 2 && desc->input_channels != 4)
            return k_unit_err_geometry;
        if (desc->output_channels != 2)
            return k_unit_err_geometry;
        in_stride_ = desc->input_channels;
        Configure((float)desc->samplerate);
        return k_unit_err_none;
    }

    /// unit_reset(): clear every filter/delay/envelope tail.  Deliberately does
    /// NOT touch the parameters — the host does not re-send them afterwards.
    void Reset() {
        biquad_reset2(&eq_low_);    biquad_reset2(&eq_mid_);   biquad_reset2(&eq_high_);
        biquad_reset2(&dbx_pre_);   biquad_reset2(&dbx_de_);
        biquad_reset2(&rec_pre_);   biquad_reset2(&rec_de_);    biquad_reset2(&console_lpf_);
        biquad_reset2(&tape_head_); biquad_reset2(&tape_lpf_);
        biquad_reset2(&hiss_hpf_);  biquad_reset2(&xtalk_hpf_);
        biquad_reset2(&dc_block_);

        hb4x_reset(&console_os_);
        hb4x_reset(&tape_os_);

        memset(wf_delay_, 0, sizeof(wf_delay_));
        wf_write_pos_     = 0;
        wf_lfo_phase_     = 0.0f;
        wf_flutter_phase_ = 0.0f;

        // Seed both detectors at the compander's unity point so the first
        // block after a reset does not open up to the encode ceiling, and so
        // the encoder and decoder start in step.
        dbx_side_reset(&dbx_enc_);
        dbx_side_reset(&dbx_dec_);

        vinyl_hist_l_ = vinyl_hist_r_ = vdupq_n_f32(0.0f);
        dust_hist_l_  = dust_hist_r_  = vdupq_n_f32(0.0f);

        pop_env_     = 0.0f;
        big_pop_env_ = 0.0f;

        current_trim_       = target_trim_;
        current_fader_      = target_fader_;
        current_mix_        = target_mix_;
        current_sat_drive_  = target_sat_drive_;
        current_sat_makeup_ = target_sat_makeup_;

        prng_init(1337u);
        b_update_filters_.store(true);
    }

    void SetBypass(bool bypass) { b_bypass_ = bypass; }

    void SetParameter(uint8_t id, int32_t value) {
        if (id >= k_num_params) return;
        raw_params_[id] = value;
        const float norm = value * 0.01f;
        switch (id) {
            case k_param_model: {
                model_ = (uint8_t)((value < 0) ? 0 : (value > k_424n ? k_424n : value));
                const MachineSpec& m = spec_for(model_);
                wf_phase_inc_         = M_TWOPI * m.wow_hz     * inverse_samplerate_;
                wf_flutter_phase_inc_ = M_TWOPI * m.flutter_hz * inverse_samplerate_;
                b_update_filters_.store(true);  // tape model affects LPF/head coeffs
                break;
            }
            case k_param_preamp: set_trim(norm);                       break;
            case k_param_drive:  set_drive(norm);                      break;
            case k_param_mix:    target_mix_    = norm;                break;
            case k_param_dbx: {
                const uint8_t mode = (uint8_t)((value < 0) ? 0 : (value > k_decode_only ? k_decode_only : value));
                // A half that was out of circuit has a stale detector; re-seed
                // both so they come back in step (on the audio thread).
                if (mode != dbx_mode_) b_dbx_resync_.store(true);
                dbx_mode_ = mode;
                break;
            }
            case k_param_age:
                tape_age_ = norm;
                b_update_filters_.store(true);  // age affects the tape LPF cutoff
                set_vinyl_asymmetry(norm);
                set_dust_density(norm);         // dust density tracks Age only
                break;
            case k_crosstalk:    xtalk_amount_ = norm;                 break;
            case k_hiss:         hiss_amount_  = norm * 0.02f;         break;
            case k_param_eq_low_hz:   case k_param_eq_low_gain:
            case k_param_eq_mid_hz:   case k_param_eq_mid_gain:
            case k_param_eq_high_hz:  case k_param_eq_high_gain:
                b_update_filters_.store(true);
                break;
            case k_attack:  set_attack_ms(value * 0.1f);   break;   // 0.1..100 ms
            case k_release: set_release_ms((float)value);  break;   // 10..2000 ms
            default: break;
        }
    }

    int32_t GetParameter(uint8_t id) const {
        return (id < k_num_params) ? raw_params_[id] : 0;
    }

    // =========================================================================
    // MAIN DSP ENTRY POINT
    // =========================================================================
    fast_inline void ProcessBlock(const float* in, float* out, uint32_t frames) {
        if (b_bypass_) {
            for (uint32_t f = 0; f < frames; ++f) {
                out[f * 2u]      = in[f * in_stride_];
                out[f * 2u + 1u] = in[f * in_stride_ + 1u];
            }
            return;
        }

        const uint32_t saved_fpscr = fpu_enter_flush_to_zero();

        if (b_update_filters_.exchange(false))
            UpdateCoefficients();
        if (b_dbx_resync_.exchange(false)) {
            dbx_side_reset(&dbx_enc_);
            dbx_side_reset(&dbx_dec_);
        }

        const uint32_t whole = frames & ~3u;
        for (uint32_t f = 0; f < whole; f += 4u)
            ProcessQuad(in + f * in_stride_, out + f * 2u);

        // Buffers shorter than the advertised frames_per_buffer are legal but
        // rare.  Rather than maintain a second, slowly diverging scalar copy of
        // the whole chain, pad the tail to four frames by holding the last
        // valid one and discard the padding on the way out.  Sample-and-hold
        // padding keeps the delay line and the IIR states click-free; the cost
        // is that they advance by up to three extra frames in this rare case.
        const uint32_t rem = frames - whole;
        if (rem != 0u) {
            float scratch_in[NEON_LANES * 4];
            float scratch_out[NEON_LANES * 2];
            const float* tail = in + whole * in_stride_;
            for (uint32_t i = 0; i < NEON_LANES; ++i) {
                const uint32_t s = (i < rem) ? i : rem - 1u;
                for (uint32_t c = 0; c < in_stride_; ++c)
                    scratch_in[i * in_stride_ + c] = tail[s * in_stride_ + c];
            }
            ProcessQuad(scratch_in, scratch_out);
            memcpy(out + whole * 2u, scratch_out, rem * 2u * sizeof(float));
        }

        fpu_restore(saved_fpscr);
    }

private:
    // =========================================================================
    // Tuning constants
    // =========================================================================

    // The compander is referenced to a nominal operating level rather than to
    // full scale.  A real dbx unit compands around 0 VU; -18 dBFS RMS is the
    // usual digital stand-in.  Referencing to 0 dBFS (as this unit used to)
    // meant any realistic master-bus level asked for more than the encode
    // ceiling, so the "compressor" was really just a fixed 3x gain.
    static constexpr float kDbxRefPower = 0.01584893f;   // 10^(-18/10), power

    // Encode-gain limits, which the decoder mirrors.  With both halves engaged
    // the clamp only bounds how hard the tape stage is driven; when only one
    // half runs (EncOnly, DecOnly) the listener hears the companding directly,
    // so it is tightened.
    static constexpr float kEncGainMin      = 0.25f;     // -12 dB
    static constexpr float kEncGainMax      = 4.00f;     // +12 dB
    static constexpr float kHalfGainMin     = 0.50f;     //  -6 dB
    static constexpr float kHalfGainMax     = 2.00f;     //  +6 dB

    // test/measure.py overrides the knee (far out of reach) to look at the
    // tape stage without the output ceiling folded into its numbers.
#ifndef PC_CEILING_KNEE_OVERRIDE
    static constexpr float kCeilingKnee  = 0.70f;        // soft ceiling onset
#else
    static constexpr float kCeilingKnee  = PC_CEILING_KNEE_OVERRIDE;
#endif
    static constexpr float kCeilingRange = 0.30f;        // knee + range = 1.0

    static constexpr float kOutputGain   = 1.30f;        // ~ +2.3 dB make-up

    // Console (mixer) op-amp stage.  The 424 MkII accepts +10 dBV at minimum
    // Trim against a -10 dBV nominal level, i.e. about 20 dB of clean headroom
    // before the channel op-amps reach their rails; here nominal is the
    // compander's -18 dBFS RMS, so a sine clips at +2 dBFS RMS = +5 dBFS peak.
    // The knee is short: rail clipping is hard, but not a brick wall.
    static constexpr float kConsoleRail  = 1.778f;               // +5 dBFS
    static constexpr float kConsoleKnee  = 0.80f * kConsoleRail; // linear below
    static constexpr float kConsoleRange = 0.20f * kConsoleRail; // knee + range = rail
    static constexpr float kTrimMaxDb    = 30.0f;                // Trim 100 %

    // Record EQ: first-order pre-emphasis zero, pole common to all machines.
    // The playback EQ is its exact inverse, so a small signal sees neither.
    static constexpr float kRecEmphPoleHz = 20000.0f;

    /**
     * @brief Per-machine character, from the published specifications.
     *
     *   Tascam 244 (1982)  40 Hz - 14 kHz, W&F 0.06%,          THD 1.5%
     *   Tascam 424 (1990s) 40 Hz - 16 kHz +/-3 dB, W&F <0.05%
     *   Tascam 488 (1988)  40 Hz - 14 kHz +/-3 dB, W&F 0.04%,  THD 1.3%,
     *                      crosstalk 50 dB at 1 kHz without dbx
     *
     * All three run at 9.5 cm/s, so bandwidth is not what separates them; the
     * 424 is the *widest* of the three, not the dullest.  What actually
     * distinguishes them:
     *
     *  - The 244 has the least stable transport (0.06% against the 488's
     *    0.04%) and the most distortion, which is most of why it sounds like
     *    the lo-fi one.
     *  - The 424 is the later, cleaner machine: widest response, quietest,
     *    and steadier than the 244.
     *  - The 488 fits eight tracks in the same 3.81 mm of tape, so each track
     *    is about half the width of a four-track's.  That costs roughly 3 dB
     *    of signal-to-noise and puts the tracks close enough together that
     *    separation drops to the 50 dB in the manual.  Its transport is the
     *    best of the three.
     *  - T-424N is the 424 at its NORMAL speed, 4.8 cm/s: 40 Hz - 10 kHz
     *    +/-3 dB, W&F 0.06% WRMS (against 0.05% at HIGH), S/N 54 dB
     *    unweighted (against 55 dB).  Half the speed halves every wavelength,
     *    so the head bump and the capstan's wow and flutter rates halve too.
     *
     * Treble headroom.  Cassette runs out of treble long before it runs out of
     * midrange: on Type II tape at 4.76 cm/s the 10 kHz saturation level sits
     * about 16 dB below the 400 Hz MOL.  The record EQ boosts the treble into
     * the tape and the playback EQ takes it back out, so a quiet signal comes
     * back flat and a loud one comes back with its treble squashed.  At 9.5
     * cm/s a 10 kHz tone has the wavelength of 5 kHz at 4.76 cm/s, which
     * roughly halves the gap; rec_emph_hz puts the pre-emphasis zero there
     * (about +9 dB at 10 kHz at HIGH speed, +15 dB at NORMAL).  Vinyl has no
     * tape record EQ.
     *
     * Head bump.  The bump is the tape's own response, inside the dbx loop, so
     * with dbx Active the decoder doubles it like any other level error.  The
     * values are half of what they were when the bump sat outside the loop:
     * with dbx Active the bump is what it was, and with dbx Off the tape alone
     * stays inside the published +/-3 dB.
     *
     * Noise, crosstalk and wow depth are relative to the 244.  Crosstalk is
     * calibrated so X-talk 1 gives the published separation at 1 kHz: 55 dB
     * for the 424 (MkII manual), 50 dB for the 488.
     */
    struct MachineSpec {
        float lpf_hz;       // record/playback -3 dB point
        float head_hz;      // head bump centre
        float head_db;      // head bump amount
        float hiss_hz;      // noise band lower corner
        float wf_scale;     // wow & flutter depth (delay, in samples)
        float noise_scale;  // noise floor
        float xtalk_scale;  // inter-track bleed
        float sat_scale;    // relative THD
        float rec_emph_hz;  // record pre-emphasis zero, 0 = none
        float wow_hz;       // capstan wow rate
        float flutter_hz;   // flutter rate
    };

    static const MachineSpec& spec_for(uint8_t model) {
        static const MachineSpec kSpecs[5] = {
            //  lpf     head    dB    hiss   W&F   noise  xtalk  sat    emph   wow    flutter
            { 14000.0f, 60.0f,  2.0f,  3500.0f, 1.00f, 1.00f, 0.68f, 1.00f, 3200.0f, 1.50f, 25.0f },  // 244
            { 16000.0f, 55.0f,  1.5f,  4000.0f, 0.83f, 0.70f, 0.58f, 0.80f, 3200.0f, 1.50f, 25.0f },  // 424
            { 14000.0f, 70.0f,  2.25f, 3000.0f, 0.67f, 1.40f, 1.03f, 0.90f, 3200.0f, 1.50f, 25.0f },  // 488
            { 12000.0f, 150.0f, 2.0f,  400.0f,  1.60f, 1.00f, 0.89f, 1.10f,    0.0f, 0.55f, 25.0f },  // vinyl
            // 424 NORMAL: W&F 1.2x the 424's; depth x2 because the LFOs run at
            // half the rate and speed deviation is depth x rate.
            { 10000.0f, 27.5f,  1.5f,  3000.0f, 2.00f, 0.98f, 0.58f, 0.80f, 1600.0f, 0.75f, 12.5f },  // 424N
        };
        return kSpecs[(model < 5) ? model : 0];
    }

    // RMS averaging windows.  The wideband path is deliberately slow enough not
    // to follow the ripple of a bass note; the Type II HF path is quick so
    // bright transients are caught.
    static constexpr float kSmoothMs = 8.0f;   // knob smoothing
    static constexpr float kWbRmsMs = 12.0f;
    static constexpr float kHfRmsMs =  3.0f;

    // Crackle envelope decay per sample (~2-3 ms tails at 48 kHz).
    static constexpr float kPopDecay    = 0.98f;
    static constexpr float kBigPopDecay = 0.97f;

    // =========================================================================
    // FPU control
    // =========================================================================

    /// Turn on flush-to-zero + default-NaN so denormals cannot stall the FPU,
    /// and hand back the previous FPSCR.
    ///
    /// Only those two bits are touched.  The old code also set bit 22, which is
    /// the low half of RMode, not a denormal bit: it switched the process to
    /// round-toward-plus-infinity and never put it back, so the host and every
    /// other loaded unit inherited it.  (ARMv7 NEON always rounds to nearest
    /// regardless of FPSCR, so the damage was confined to scalar VFP code —
    /// which is most of a typical unit's control-rate arithmetic.)
    static fast_inline uint32_t fpu_enter_flush_to_zero() {
#if defined(__arm__) && !defined(__aarch64__)
        uint32_t fpscr;
        __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
        const uint32_t want = fpscr | (1u << 24) /* FZ */ | (1u << 25) /* DN */;
        if (want != fpscr)
            __asm__ volatile("vmsr fpscr, %0" : : "r"(want));
        return fpscr;
#else
        return 0u;
#endif
    }

    /// Put back what we were handed, except that FZ and DN stay on.
    ///
    /// Leaving those two set is deliberate.  Scalar VFP has no automatic
    /// denormal flushing, so a reverb tail decaying into subnormals costs
    /// hundreds of cycles per operation and is a classic source of dropouts on
    /// a shared audio thread.  The previous build left FZ on as a side effect
    /// of its FPSCR bug; restoring it faithfully would quietly remove that
    /// protection from whatever runs after us.  No audio code has any use for
    /// subnormal precision, so this is the safe direction to err in.
    static fast_inline void fpu_restore(uint32_t fpscr) {
#if defined(__arm__) && !defined(__aarch64__)
        const uint32_t want = fpscr | (1u << 24) | (1u << 25);
        uint32_t now;
        __asm__ volatile("vmrs %0, fpscr" : "=r"(now));
        if (now != want)
            __asm__ volatile("vmsr fpscr, %0" : : "r"(want));
#else
        (void)fpscr;
#endif
    }

    // =========================================================================
    // Setup
    // =========================================================================
    void Configure(float samplerate) {
        samplerate_         = (samplerate > 0.0f) ? samplerate : 48000.0f;
        inverse_samplerate_ = 1.0f / samplerate_;
        b_bypass_           = false;

        // Knob smoothing runs once per quad, so its time constant is expressed
        // against fs/4 (the hard-coded 0.01 it replaces drifted with fs).
        smooth_coeff_ = -expm1f(-4000.0f / (kSmoothMs * samplerate_));

        // The detectors' one-pole RMS averages, four steps at a time:
        // after x0..x3, env = d*env + sum(w_i * x_i), with d = (1-k)^4 and
        // w = k * {(1-k)^3, (1-k)^2, (1-k), 1} -- exactly the serial result.
        set_quad_one_pole(one_pole_coeff(kWbRmsMs), &wb_rms_w_, &wb_rms_decay_);
        set_quad_one_pole(one_pole_coeff(kHfRmsMs), &hf_rms_w_, &hf_rms_decay_);

        // Load the header defaults through the normal parameter path so there
        // is exactly one place where a default is written down.  (The model
        // parameter also sets the wow and flutter rates.)
        for (uint8_t i = 0; i < k_num_params; ++i)
            SetParameter(i, kParamInit[i]);

        // 1-pole DC blocker as a biquad: y[n] = x[n] - x[n-1] + R*y[n-1].
        // R = exp(-2*pi*fc/fs) puts the corner at 20 Hz at any sample rate
        // (the old hard-coded 0.995 was really ~38 Hz at 48 kHz).
        const float R = expf(-M_TWOPI * 20.0f * inverse_samplerate_);
        coeff_dc_block_.b0 = 1.0f;  coeff_dc_block_.b1 = -1.0f; coeff_dc_block_.b2 = 0.0f;
        coeff_dc_block_.a1 = -R;    coeff_dc_block_.a2 = 0.0f;

        Reset();
        UpdateCoefficients();
        b_update_filters_.store(false);
    }

    /**
     * @brief Map the Drive knob to a saturator gain and its make-up.
     *
     * Two things have to be right for the knob to feel like anything.
     *
     * The gain is exponential (1x to 12x), so equal rotation buys equal dB.  A
     * linear 1 + 3*norm ramp barely reached the knee of the saturator, because
     * the compander ahead of it now regulates the level toward its -18 dBFS
     * reference instead of pinning at the encode ceiling: the operating point
     * dropped ~18 dB but the drive range had not moved to match.
     *
     * The make-up falls as 1/sqrt(drive), not as 1/drive.  Full compensation
     * looks right on paper and is wrong in practice, because the saturator is
     * itself compressing: undoing the input gain as well removes all of the
     * level rise and then some, and the knob reads as dead.  A half-power
     * make-up leaves the level climbing a few dB and then flattening, which is
     * what driving tape actually does.
     */
    void set_drive(float norm) {
        target_sat_drive_  = powf(12.0f, norm);
        target_sat_makeup_ = 0.9f / sqrtf(target_sat_drive_);
    }

    /**
     * @brief Map the Trim knob to the console stage's input gain and fader.
     *
     * 0 to +30 dB into the channel op-amps, equal dB per step.  On a
     * Portastudio you crank the Trim to make the channel strip distort and
     * pull the fader down to keep the level; the "fader" here does that
     * automatically with the same half-power law as Drive, so the clean level
     * climbs 15 dB over the whole range instead of 30.  At the default 20 %
     * that is +6 dB in and -3 dB out, the +3 dB the old PreAmp gave at 20 %.
     */
    void set_trim(float norm) {
        target_trim_  = powf(10.0f, norm * kTrimMaxDb * 0.05f);
        target_fader_ = 1.0f / sqrtf(target_trim_);
    }

    static void set_quad_one_pole(float k, float32x4_t* w, float* decay) {
        const float a = 1.0f - k;
        const float wv[4] = { k * a * a * a, k * a * a, k * a, k };
        *w = vld1q_f32(wv);
        *decay = a * a * a * a;
    }

    fast_inline void set_attack_ms(float ms) {
        attack_ms_    = fmaxf(ms, 0.05f);
        attack_coeff_ = one_pole_coeff(attack_ms_);
    }

    fast_inline void set_release_ms(float ms) {
        release_ms_    = fmaxf(ms, 1.0f);
        release_coeff_ = one_pole_coeff(release_ms_);
    }

    /// Per-sample 1-pole coefficient for a time constant in milliseconds:
    /// y += k*(x - y) with k = 1 - exp(-1/(tau*fs)).
    ///
    /// Two traps here.  The "1 -" matters: feeding exp() straight in (as
    /// omnipress's gain smoother does) inverts the knob, so long settings act
    /// instantaneously and short ones crawl.  And this needs a real expm1f
    /// rather than float_math's fast exponential: that one is a
    /// linear-mantissa bit trick with ~3% error, which is fine for an
    /// audio-rate curve but useless here, where the argument is ~1e-4 and the
    /// answer *is* the difference from 1.0 — it turns a 150 ms release into
    /// 0.7 ms.  Parameter-change path only, so libm is affordable.
    float one_pole_coeff(float ms) const {
        const float k = -expm1f(-1000.0f / (ms * samplerate_));
        return fminf(fmaxf(k, 1.0e-6f), 1.0f);
    }

    /**
     * @brief Vinyl crackle density, driven by Age alone.
     *
     * This used to be called at the end of SetParameter() for *every*
     * parameter, so the last knob touched decided the pop rate — moving MidHz
     * to 500 (norm 5.0) asked for 242 pops/second.
     */
    fast_inline void set_dust_density(float age) {
        // 2 Hz at new tape up to ~50 Hz of surface crackle at full Age.
        const float frequency = 2.0f + age * 48.0f;
        vinyl_pop_threshold_ = 1.0f - frequency * inverse_samplerate_;

        // Occasional big pops: 0.05 Hz to 1.5 Hz.
        const float big_freq = 0.05f + age * 1.45f;
        vinyl_big_pop_threshold_ = 1.0f - big_freq * inverse_samplerate_;
    }

    /**
     * @brief Groove-wear asymmetry, mapped from Age.
     * 0.00 symmetric (tape), 0.05 modern hi-fi press, 0.12 vintage sweet spot,
     * 0.22 heavily worn groove / old stylus.
     */
    void set_vinyl_asymmetry(float age) {
        vinyl_bias_ = age * 0.22f;
        // First-order DC compensation for the rational clipper below,
        // f(x) = x / (1 + |x| + 0.3*x^2) evaluated at x = bias.  It is only
        // approximate (the true mean depends on programme level), which is why
        // the DC blocker now runs in vinyl mode too.
        const float b = vinyl_bias_;
        vinyl_dc_offset_ = b / (1.0f + fabsf(b) + 0.3f * b * b);
    }

    // =========================================================================
    // Per-quad (4-frame) processing
    // =========================================================================

    /// Deliberately not inlined.  ProcessQuad has two call sites (the main loop
    /// and the short-buffer tail), and letting the whole chain — the shapers,
    /// both noise generators, a dozen biquads — expand twice pushed
    /// unit_render's stack frame to 2168 bytes of spill.  Every other unit in
    /// this SDK sits under 500.  One call per four frames costs nothing next to
    /// the work inside; keeping a single copy takes the frame to 72 bytes and
    /// leaves far more of it in the instruction cache.
    __attribute__((noinline, optimize("Ofast")))
    void ProcessQuad(const float* in, float* out) {
        // Parameter smoothing: one 1-pole step per quad, ~8 ms at any rate.
        const float ks = smooth_coeff_;
        current_trim_       += ks * (target_trim_       - current_trim_);
        current_fader_      += ks * (target_fader_      - current_fader_);
        current_mix_        += ks * (target_mix_        - current_mix_);
        current_sat_drive_  += ks * (target_sat_drive_  - current_sat_drive_);
        current_sat_makeup_ += ks * (target_sat_makeup_ - current_sat_makeup_);

        float32x4_t dry_l, dry_r;
        if (in_stride_ == 4u) {
            const float32x4x4_t vi = vld4q_f32(in);   // main L, main R, sidechain L, R
            dry_l = vi.val[0];
            dry_r = vi.val[1];
        } else {
            const float32x4x2_t vi = vld2q_f32(in);
            dry_l = vi.val[0];
            dry_r = vi.val[1];
        }

        float32x4_t sig_l = vmulq_n_f32(dry_l, current_trim_);
        float32x4_t sig_r = vmulq_n_f32(dry_r, current_trim_);

        // Channel strip: Trim into the op-amps, which clip at their rails,
        // then the fader.  The EQ comes after the clip, as on the console.
        console_clip(sig_l, sig_r);
        sig_l = vmulq_n_f32(sig_l, current_fader_);
        sig_r = vmulq_n_f32(sig_r, current_fader_);
        // The mixer's 20 kHz bandwidth (20 Hz - 20 kHz +/-2 dB, line to line).
        // It also clears the clip's residue between 20 and 24 kHz -- harmonics
        // in the oversampler's transition band fold there -- before the tape's
        // record EQ boosts it 12 dB and the saturator mixes it back down into
        // the audio band.
        biquad_process4_stereo(&console_lpf_, &coeff_console_lpf_, &sig_l, &sig_r);

        parametric_eq(sig_l, sig_r);

        // dbx encode: HF pre-emphasis + 2:1 RMS compression (Active, EncOnly).
        dbx_nr_encode(sig_l, sig_r);

        // Inside the dbx loop the tape has to give back what it was given, as
        // a calibrated deck does: the decoder now hears the tape, and a 1:2
        // expander would double any gain the tape stage added (Drive's make-up
        // was +3.4 dB at 40 %).  So the saturator runs at unity small-signal
        // gain and the make-up is a playback gain after the decoder.
        const float drive    = current_sat_drive_ * sat_scale_;
        const float playback = current_sat_makeup_ * sat_makeup_comp_ * drive;

        // Record EQ boosts the treble into the tape and the playback EQ takes
        // it out again, so the treble saturates first, as it does on cassette.
        biquad_process4_stereo(&rec_pre_, &coeff_rec_pre_, &sig_l, &sig_r);
        drive_into_soft_saturation(sig_l, sig_r, drive);
        biquad_process4_stereo(&rec_de_,  &coeff_rec_de_,  &sig_l, &sig_r);

        // Noise sits after saturation (so Drive does not amplify it), before
        // the playback roll-off (so it is band-limited by the machine, as tape
        // noise is), and before the decoder (so the decoder can suppress it,
        // as dbx does).  It used to come after the roll-off, which left a
        // high-passed noise band running flat to Nyquist — the "whine".
        // The noise is scaled against the playback gain so the floor at the
        // output is where it was, and a hotter recording still buys S/N.
        const float noise_level = 1.0f / playback;
        if (model_ == k_vinyl) vinyl_dust(sig_l, sig_r, noise_level);
        else                   tape_hiss(sig_l, sig_r, noise_level);

        // The head bump is the playback head's response, so it comes after
        // the tape.  Ahead of the saturator (where it used to be) it drove
        // the kick 3-5 dB harder into saturation than the rest of the mix.
        head_bump(sig_l, sig_r);
        tape_age(sig_l, sig_r);

        wow_and_flutter(sig_l, sig_r);
        crosstalk(sig_l, sig_r);
        dbx_nr_decode(sig_l, sig_r);

        sig_l = vmulq_n_f32(sig_l, playback);
        sig_r = vmulq_n_f32(sig_r, playback);

        // DC block the wet path only, so Mix=0 stays bit-transparent.  Vinyl
        // needs this most: its asymmetric saturator is the main DC source, yet
        // it was the one model that used to skip the blocker entirely.
        biquad_process4_stereo(&dc_block_, &coeff_dc_block_, &sig_l, &sig_r);

        const float wet = current_mix_ * kOutputGain;
        const float dry = 1.0f - current_mix_;
        float32x4_t out_l = vmlaq_n_f32(vmulq_n_f32(dry_l, dry), sig_l, wet);
        float32x4_t out_r = vmlaq_n_f32(vmulq_n_f32(dry_r, dry), sig_r, wet);

        float32x4x2_t vo = {{ soft_ceiling(out_l), soft_ceiling(out_r) }};
        vst2q_f32(out, vo);
    }

    // =========================================================================
    // Stages
    // =========================================================================
    fast_inline void parametric_eq(float32x4_t &sig_l, float32x4_t &sig_r) {
        // A peaking section at exactly 0 dB has b == a, i.e. H(z) = 1, and once
        // its state is cleared it stays at zero.  Skipping the flat bands saves
        // all three stereo sections on the (flat) default patch.
        if (eq_low_active_)  biquad_process4_stereo(&eq_low_,  &coeff_eq_low_,  &sig_l, &sig_r);
        if (eq_mid_active_)  biquad_process4_stereo(&eq_mid_,  &coeff_eq_mid_,  &sig_l, &sig_r);
        if (eq_high_active_) biquad_process4_stereo(&eq_high_, &coeff_eq_high_, &sig_l, &sig_r);
    }

    // =========================================================================
    // dbx compander
    //
    // A dbx encoder is the exact inverse of its decoder because both detectors
    // read the same signal: the encoder's detector listens to its own output
    // (feedback), the decoder's to its input (feed-forward), and on tape those
    // are the same thing.  So the two halves cancel without sharing any state,
    // and whatever the tape does in between -- saturation, treble loss, the
    // head bump -- reaches the decoder's detector and is expanded along with
    // the programme.  A 1:2 expander doubles any level error in dB, which is
    // why a dbx Portastudio squashes harder than its tape alone when pushed,
    // and why dull or misaligned tape "breathes".
    //
    // This unit used to take the decode gain as 1/encode gain, which cancels
    // exactly but hides the tape from the decoder.  (Independent followers
    // reading *different* signals -- the encoder's input against the tape
    // output -- clicked on the first hit after silence; mirrored detectors
    // reading the same signal start and stay in step.)
    // =========================================================================

    /// One half of the compander.  The gain is always held as the *encoder's*
    /// gain, so the decoder's state is a copy of the encoder's whenever the
    /// tape is transparent; the decoder applies its inverse.
    struct DbxSide {
        float env_wb;            // wideband RMS power envelope
        float env_hf;            // Type II HF spectral envelope
        float gain;              // smoothed encode gain
        biquad_state2_t bl;      // control-path band limit, {L,R}
        biquad_state2_t hf;      // detector HF band filter, {L,R}
    };

    static void dbx_side_reset(DbxSide* s) {
        s->env_wb = kDbxRefPower;
        s->env_hf = kDbxRefPower;
        s->gain   = 1.0f;
        biquad_reset2(&s->bl);
        biquad_reset2(&s->hf);
    }

    /// 1/sqrt(x), ~1e-6 relative: NEON estimate plus two Newton-Raphson steps.
    static fast_inline float rsqrt_nr(float x) {
        const float32x2_t v = vdup_n_f32(x);
        float32x2_t r = vrsqrte_f32(v);
        r = vmul_f32(vrsqrts_f32(vmul_f32(v, r), r), r);
        r = vmul_f32(vrsqrts_f32(vmul_f32(v, r), r), r);
        return vget_lane_f32(r, 0);
    }

    /// The gain this half wants for the next quad, from the detector state the
    /// last one left: (env/ref)^(-1/2) of the *compressed* signal's power,
    /// which in steady state is (input/ref)^(-1/4) -- a true 2:1 in dB about
    /// -18 dBFS RMS.  Once per quad: the detectors average over 3-12 ms, so
    /// four samples of latency in the loop cost nothing.
    fast_inline float dbx_target(const DbxSide* s, float gmin, float gmax) const {
        const float env = fmaxf(0.7f * s->env_wb + 0.3f * s->env_hf, 1.0e-9f);
        return fminf(fmaxf(rsqrt_nr(env * (1.0f / kDbxRefPower)), gmin), gmax);
    }

    /// Per-sample gain ballistics toward the target: attack while the gain
    /// falls (the programme got louder), release while it recovers.  Serial,
    /// because a vector update per quad would run four decimated followers.
    fast_inline float32x4_t dbx_ballistics(DbxSide* s, float target) const {
        const float k = (target < s->gain) ? attack_coeff_ : release_coeff_;
        float g = s->gain;
        float32x4_t v = vdupq_n_f32(0.0f);
        g += k * (target - g);  v = vsetq_lane_f32(g, v, 0);
        g += k * (target - g);  v = vsetq_lane_f32(g, v, 1);
        g += k * (target - g);  v = vsetq_lane_f32(g, v, 2);
        g += k * (target - g);  v = vsetq_lane_f32(g, v, 3);
        s->gain = g;
        return v;
    }

    /// Feed a quad of the signal *as it stands on tape* to a detector.  The
    /// encoder and the decoder run this same code on the same signal.
    ///
    /// Fixed-window RMS averaging, not the user's ballistics: a real dbx
    /// detector averages power over a fixed time, and putting a 3 ms attack
    /// straight onto the power made the detector chase the 2f ripple of a bass
    /// note and modulate its own gain at twice the note frequency.  The four
    /// per-sample one-pole steps are folded into one weighted sum, which is
    /// exactly the serial result.
    fast_inline void dbx_listen(DbxSide* s, float32x4_t l, float32x4_t r) const {
        // Type II rolls its control path off below ~30 Hz and above ~10 kHz so
        // errors at the band edges -- rumble, the treble a tape loses -- do not
        // steer the gain.  It does not touch the audio path.
        biquad_process4_stereo(&s->bl, &coeff_dbx_bl_, &l, &r);
        // Type II HF spectral path: the band above ~2 kHz is tracked on its own
        // so bright transients get more reduction than their level implies.
        float32x4_t hl = l, hr = r;
        biquad_process4_stereo(&s->hf, &coeff_dbx_hf_, &hl, &hr);
        const float32x4_t wb = vmlaq_f32(vmulq_f32(l, l), r, r);       // L+R power
        const float32x4_t hf = vmlaq_f32(vmulq_f32(hl, hl), hr, hr);
        const float32x4_t ww = vmulq_f32(wb, wb_rms_w_);
        const float32x4_t hw = vmulq_f32(hf, hf_rms_w_);
        const float32x2_t sw = vpadd_f32(vadd_f32(vget_low_f32(ww), vget_high_f32(ww)),
                                         vadd_f32(vget_low_f32(hw), vget_high_f32(hw)));
        s->env_wb = s->env_wb * wb_rms_decay_ + vget_lane_f32(sw, 0);
        s->env_hf = s->env_hf * hf_rms_decay_ + vget_lane_f32(sw, 1);
    }

    /// Gain limits for the compander: the full range when both halves run, a
    /// tighter one when the listener hears one half on its own.
    fast_inline float dbx_gain_min() const { return (dbx_mode_ == k_active) ? kEncGainMin : kHalfGainMin; }
    fast_inline float dbx_gain_max() const { return (dbx_mode_ == k_active) ? kEncGainMax : kHalfGainMax; }

    __attribute__((noinline, optimize("Ofast")))
    void dbx_nr_encode(float32x4_t &sig_l, float32x4_t &sig_r) {
        if (dbx_mode_ != k_active && dbx_mode_ != k_encode_only)
            return;

        // HF pre-emphasis (record side).
        biquad_process4_stereo(&dbx_pre_, &coeff_dbx_pre_, &sig_l, &sig_r);

        // Compress with the gain the detector chose from what it heard of
        // the *compressed* signal so far, then let it hear this quad.
        const float32x4_t g = dbx_ballistics(&dbx_enc_, dbx_target(&dbx_enc_, dbx_gain_min(), dbx_gain_max()));
        sig_l = vmulq_f32(sig_l, g);
        sig_r = vmulq_f32(sig_r, g);
        dbx_listen(&dbx_enc_, sig_l, sig_r);
    }

    __attribute__((noinline, optimize("Ofast")))
    void dbx_nr_decode(float32x4_t &sig_l, float32x4_t &sig_r) {
        // EncOnly is the classic Portastudio abuse trick: record through the
        // encoder, play back without the decoder, and keep the compressed,
        // pre-emphasised signal -- bright and squashed, "a makeshift aural
        // exciter".  DecOnly is the reverse: a tape recorded without dbx played
        // through the expander and its de-emphasis, which comes out dark and
        // over-dynamic ("lots of bass and no high-end").
        if (dbx_mode_ != k_active && dbx_mode_ != k_decode_only)
            return;

        // Same detector, same code, on what came back off the tape; expand by
        // the inverse of the gain it implies.
        const float32x4_t g = dbx_ballistics(&dbx_dec_, dbx_target(&dbx_dec_, dbx_gain_min(), dbx_gain_max()));
        dbx_listen(&dbx_dec_, sig_l, sig_r);
        const float32x4_t inv = recip_nr(g);
        sig_l = vmulq_f32(sig_l, inv);
        sig_r = vmulq_f32(sig_r, inv);

        biquad_process4_stereo(&dbx_de_, &coeff_dbx_de_, &sig_l, &sig_r);
    }

    fast_inline void head_bump(float32x4_t &sig_l, float32x4_t &sig_r) {
        biquad_process4_stereo(&tape_head_, &coeff_tape_head_, &sig_l, &sig_r);
    }

    fast_inline void tape_age(float32x4_t &sig_l, float32x4_t &sig_r) {
        biquad_process4_stereo(&tape_lpf_, &coeff_tape_lpf_, &sig_l, &sig_r);
    }

    // The memoryless shapers, as functors for the 2x oversampler.
    struct ConsoleShaper {
        fast_inline float32x4_t operator()(float32x4_t x) const {
            return knee_clip(x, kConsoleKnee, kConsoleRange);
        }
    };
    struct TapeShaper {
        fast_inline float32x4_t operator()(float32x4_t x) const {
            return tape_saturation(x);
        }
    };
    struct VinylShaper {
        float bias;
        fast_inline float32x4_t operator()(float32x4_t x) const {
            return vinyl_clip(vaddq_f32(x, vdupq_n_f32(bias)));
        }
    };

    /// Console op-amp stage, run at 4x: rail clipping is the most broadband
    /// thing in the unit.
    ///
    /// This and the other oversampled and compander stages are out of line:
    /// inlined into ProcessQuad, the oversamplers' state spilled to the stack
    /// around every allpass section.  On its own each keeps its state in
    /// registers for the whole quad.
    __attribute__((noinline, optimize("Ofast")))
    void console_clip(float32x4_t &sig_l, float32x4_t &sig_r) {
        hb4x_process4_stereo(&console_os_, &sig_l, &sig_r, ConsoleShaper());
    }

    /// Drive into the shaper and back out by the same factor, so a quiet signal
    /// leaves at the level it came in.  Run at 4x: the record EQ pushes the
    /// treble 9-15 dB harder into the shaper than the midrange, and at 2x a
    /// 10 kHz tone's 9th harmonic still folded back to 6.2 kHz.
    __attribute__((noinline, optimize("Ofast")))
    void drive_into_soft_saturation(float32x4_t &sig_l, float32x4_t &sig_r, float drive) {
        sig_l = vmulq_n_f32(sig_l, drive);
        sig_r = vmulq_n_f32(sig_r, drive);
        if (model_ == k_vinyl) {
            hb4x_process4_stereo(&tape_os_, &sig_l, &sig_r, VinylShaper{ vinyl_bias_ });
            const float32x4_t dc = vdupq_n_f32(vinyl_dc_offset_);
            sig_l = vinyl_trace(vsubq_f32(sig_l, dc), vinyl_hist_l_);
            sig_r = vinyl_trace(vsubq_f32(sig_r, dc), vinyl_hist_r_);
        } else {
            hb4x_process4_stereo(&tape_os_, &sig_l, &sig_r, TapeShaper());
        }
        const float back = 1.0f / drive;
        sig_l = vmulq_n_f32(sig_l, back);
        sig_r = vmulq_n_f32(sig_r, back);
    }

    /// 4-point, 3rd-order Hermite over a {L,R} pair.
    static fast_inline float32x2_t hermite4(float32x2_t p0, float32x2_t p1,
                                            float32x2_t p2, float32x2_t p3, float fr) {
        const float32x2_t c1 = vmul_n_f32(vsub_f32(p2, p0), 0.5f);
        float32x2_t c2 = vmls_n_f32(p0, p1, 2.5f);
        c2 = vmla_n_f32(c2, p2, 2.0f);
        c2 = vmls_n_f32(c2, p3, 0.5f);
        float32x2_t c3 = vmul_n_f32(vsub_f32(p3, p0), 0.5f);
        c3 = vmla_n_f32(c3, vsub_f32(p1, p2), 1.5f);
        float32x2_t y = vmla_n_f32(c2, c3, fr);
        y = vmla_n_f32(c1, y, fr);
        return vmla_n_f32(p1, y, fr);
    }

    fast_inline void wow_and_flutter(float32x4_t &sig_l, float32x4_t &sig_r) {
        const float wow_depth     = (5.0f + tape_age_ * 25.0f) * wf_scale_;
        const float flutter_depth = (0.8f + tape_age_ * 2.0f) * wf_scale_;

        // Both LFOs, first and last lane of the quad, in one NEON sine call.
        // Over four samples a 1.5 Hz and a 25 Hz sine are linear to well under
        // 1e-4 of a sample of delay, so the two inner lanes are interpolated.
        const float32x4_t phases = {
            wf_lfo_phase_,     wf_lfo_phase_     + 3.0f * wf_phase_inc_,
            wf_flutter_phase_, wf_flutter_phase_ + 3.0f * wf_flutter_phase_inc_ };
        const float32x4_t s = fastersinfullf_ps(phases);

        wf_lfo_phase_     += 4.0f * wf_phase_inc_;
        wf_flutter_phase_ += 4.0f * wf_flutter_phase_inc_;
        if (wf_lfo_phase_     >= M_TWOPI) wf_lfo_phase_     -= M_TWOPI;
        if (wf_flutter_phase_ >= M_TWOPI) wf_flutter_phase_ -= M_TWOPI;

        const float rd_a = wow_depth * (1.0f + 0.8f * vgetq_lane_f32(s, 0))
                         + flutter_depth * vgetq_lane_f32(s, 2);
        const float rd_b = wow_depth * (1.0f + 0.8f * vgetq_lane_f32(s, 1))
                         + flutter_depth * vgetq_lane_f32(s, 3);
        const float rd_step = (rd_b - rd_a) * (1.0f / 3.0f);

        // {l0,r0,l1,r1},{l2,r2,l3,r3}
        const float32x4x2_t p = vzipq_f32(sig_l, sig_r);
        float32x2_t in[NEON_LANES] = { vget_low_f32(p.val[0]), vget_high_f32(p.val[0]),
                                       vget_low_f32(p.val[1]), vget_high_f32(p.val[1]) };
        float32x2_t o[NEON_LANES];

        for (int lane = 0; lane < NEON_LANES; ++lane) {
            // Clamp so all four taps stay inside the ring: the tap set spans
            // delays [i_off-1, i_off+2].  At low Age the raw offset dips to
            // 0.2 samples, which used to send the leading tap a whole buffer
            // into the past and crack.
            float rd = rd_a + rd_step * (float)lane;
            rd = fminf(fmaxf(rd, 1.0f), (float)(WF_BUFFER_SIZE - 3));

            const int32_t  i_off = (int32_t)rd;
            const float    fr    = rd - (float)i_off;

            vst1_f32(&wf_delay_[wf_write_pos_ * 2u], in[lane]);

            // Interleaved delay line: one d-register load per tap covers both
            // channels, and the Hermite runs on {L,R} pairs.
            const uint32_t m1 = (wf_write_pos_ - (uint32_t)i_off) & WF_BUFFER_MASK;
            const uint32_t m0 = (m1 + 1u) & WF_BUFFER_MASK;
            const uint32_t m2 = (m1 - 1u) & WF_BUFFER_MASK;
            const uint32_t m3 = (m1 - 2u) & WF_BUFFER_MASK;

            o[lane] = hermite4(vld1_f32(&wf_delay_[m0 * 2u]),
                               vld1_f32(&wf_delay_[m1 * 2u]),
                               vld1_f32(&wf_delay_[m2 * 2u]),
                               vld1_f32(&wf_delay_[m3 * 2u]), fr);

            wf_write_pos_ = (wf_write_pos_ + 1u) & WF_BUFFER_MASK;
        }

        const float32x4x2_t u = vuzpq_f32(vcombine_f32(o[0], o[1]),
                                          vcombine_f32(o[2], o[3]));
        sig_l = u.val[0];
        sig_r = u.val[1];
    }

    fast_inline void crosstalk(float32x4_t &sig_l, float32x4_t &sig_r) {
        if (xtalk_amount_ <= 0.0f) return;
        const float bleed = xtalk_amount_ * xtalk_scale_;
        // Treble leaks between adjacent tape tracks far more readily than bass.
        float32x4_t bleed_l = sig_l, bleed_r = sig_r;
        biquad_process4_stereo(&xtalk_hpf_, &coeff_xtalk_hpf_, &bleed_l, &bleed_r);
        const float32x4_t xl = vmlaq_n_f32(sig_l, bleed_r, bleed);
        const float32x4_t xr = vmlaq_n_f32(sig_r, bleed_l, bleed);
        sig_l = xl;
        sig_r = xr;
    }

    fast_inline void tape_hiss(float32x4_t &sig_l, float32x4_t &sig_r, float level) {
        if (hiss_amount_ <= 0.0f) return;
        // Two independent bipolar noise streams, coloured by the same HPF.
        // The old code filtered L only and built R as 0.3*L + 0.7*rand, where
        // rand was the raw [0,1) PRNG output — an unfiltered, DC-biased stream.
        float32x4_t nl = prng_bipolar();
        float32x4_t nr = prng_bipolar();
        biquad_process4_stereo(&hiss_hpf_, &coeff_hiss_hpf_, &nl, &nr);

        // Older tape sheds oxide, so the floor rises with Age.  No dbx gate
        // here: the hiss is injected inside the companding loop, so when the
        // decoder is engaged it gets pulled down exactly as it would on tape.
        // (The Hiss knob used to do literally nothing in the default patch.)
        const float hiss_scaled = hiss_amount_ * (1.0f + tape_age_ * 2.0f) * noise_scale_ * level;
        sig_l = vmlaq_n_f32(sig_l, nl, hiss_scaled);
        sig_r = vmlaq_n_f32(sig_r, nr, hiss_scaled);
    }

    fast_inline void vinyl_dust(float32x4_t &sig_l, float32x4_t &sig_r, float level) {
        float32x4_t white_l = prng_bipolar();
        float32x4_t white_r = prng_bipolar();
        const float32x4_t trig = prng_unit();

        // The big-pop threshold is the stricter of the two, so a big pop always
        // coincides with a small one — physically that is one large piece of
        // grit, and it saves a whole PRNG round per quad.  The amplitude jitter
        // is taken from |white_l|, which is independent of the trigger stream.
        float32x4_t v_pop = white_l, v_big = white_l;
        float pop = pop_env_, big = big_pop_env_;

#define PC_DUST_STEP(LANE)                                              \
        {                                                               \
            const float t = vgetq_lane_f32(trig, LANE);                 \
            const float u = fabsf(vgetq_lane_f32(white_l, LANE));       \
            if (t > vinyl_pop_threshold_) {                             \
                const float amp = 0.15f + u * 0.30f;                    \
                if (amp > pop) pop = amp;                               \
            }                                                           \
            if (t > vinyl_big_pop_threshold_) {                         \
                const float amp = 0.40f + u * 0.50f;                    \
                if (amp > big) big = amp;                               \
            }                                                           \
            v_pop = vsetq_lane_f32(pop, v_pop, LANE);                   \
            v_big = vsetq_lane_f32(big, v_big, LANE);                   \
            pop *= kPopDecay;                                           \
            big *= kBigPopDecay;                                        \
        }
        PC_DUST_STEP(0)
        PC_DUST_STEP(1)
        PC_DUST_STEP(2)
        PC_DUST_STEP(3)
#undef PC_DUST_STEP
        pop_env_     = pop;
        big_pop_env_ = big;

        // One-sample smoothing of the grain.  vextq carries the last lane of
        // the previous quad, so the delay really is one sample; the old version
        // mixed lane i with lane i of the *previous block*, i.e. a 4-sample
        // comb notching 6 kHz.
        const float32x4_t prev_l = vextq_f32(dust_hist_l_, white_l, 3);
        const float32x4_t prev_r = vextq_f32(dust_hist_r_, white_r, 3);
        dust_hist_l_ = white_l;
        dust_hist_r_ = white_r;
        white_l = vmlaq_n_f32(vmulq_n_f32(white_l, 0.82f), prev_l, 0.18f);
        white_r = vmlaq_n_f32(vmulq_n_f32(white_r, 0.82f), prev_r, 0.18f);

        // Crackle is high-passed for a dusty texture; the big pops bypass the
        // HPF so they keep their low-end thump.
        float32x4_t crack_l = vmulq_f32(white_l, v_pop);
        float32x4_t crack_r = vmulq_f32(white_r, v_pop);
        biquad_process4_stereo(&hiss_hpf_, &coeff_hiss_hpf_, &crack_l, &crack_r);

        crack_l = vmlaq_f32(crack_l, white_l, v_big);
        crack_r = vmlaq_f32(crack_r, white_r, v_big);

        const float dust_level = (0.06f + tape_age_ * 0.08f) * noise_scale_ * level;
        sig_l = vmlaq_n_f32(sig_l, crack_l, dust_level);
        sig_r = vmlaq_n_f32(sig_r, crack_r, dust_level);
    }

    /// Soft output ceiling: unity below the knee, tangent-continuous through a
    /// quadratic knee, and exactly 1.0 once the overshoot reaches 2*range.  The
    /// old stage hard-clipped at +/-1, which sprays broadband aliasing across
    /// the master bus precisely when the unit is being pushed.
    fast_inline float32x4_t soft_ceiling(float32x4_t x) const {
        return knee_clip(x, kCeilingKnee, kCeilingRange);
    }

    /// Odd-symmetric clipper: unity up to `knee`, then a quadratic knee that
    /// lands tangent-flat on knee + range once the overshoot reaches 2*range.
    ///
    /// Written without a division: no reciprocal to refine, and an infinite
    /// input lands on the ceiling instead of going through a 0*inf NaN.
    static fast_inline float32x4_t knee_clip(float32x4_t x, float knee_level, float range) {
        const float32x4_t knee = vdupq_n_f32(knee_level);
        const float32x4_t a    = vabsq_f32(x);
        float32x4_t over = vmaxq_f32(vsubq_f32(a, knee), vdupq_n_f32(0.0f));
        over = vminq_f32(over, vdupq_n_f32(2.0f * range));
        // mag = min(a,knee) + over - over^2 / (4*range)
        float32x4_t mag = vaddq_f32(vminq_f32(a, knee), over);
        mag = vmlsq_n_f32(mag, vmulq_f32(over, over), 0.25f / range);
        const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u));
        return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(mag), sign));
    }

    // =========================================================================
    // Non-linearities
    // =========================================================================

    /// Vinyl's memoryless half: the rational clipper f(x) = x / (1 + |x| + 0.3x^2),
    /// a cheap tanh with a warmer knee.  The caller adds the groove-wear bias
    /// first; the bias breaks the symmetry and generates the even harmonics
    /// that give worn vinyl its warmth.  Run at 2x.
    static fast_inline float32x4_t vinyl_clip(float32x4_t xb) {
        const float32x4_t denom =
            vmlaq_n_f32(vaddq_f32(vdupq_n_f32(1.0f), vabsq_f32(xb)),
                        vmulq_f32(xb, xb), 0.3f);
        return vmulq_f32(xb, recip_fast(denom));
    }

    /// Mechanical tracing distortion: the stylus cannot follow the groove wall
    /// exactly, so add a signed-square of the groove velocity (a 1-sample
    /// difference).
    /// @param hist per-channel one-quad history — L and R used to share a single
    ///        history vector, which cross-fed the two channels' tracing filters.
    static fast_inline float32x4_t vinyl_trace(float32x4_t y, float32x4_t &hist) {
        const float32x4_t shifted = vextq_f32(hist, y, 3);
        hist = y;
        const float32x4_t vel = vsubq_f32(y, shifted);
        return vmlaq_n_f32(y, vmulq_f32(vel, vabsq_f32(vel)), 0.08f);
    }

    /// Odd-symmetric tape saturation (a rational tanh approximation).
    static fast_inline float32x4_t tape_saturation(float32x4_t x) {
        const float32x4_t lim = vdupq_n_f32(5.0f);
        x = vmaxq_f32(vminq_f32(x, lim), vnegq_f32(lim));

        const uint32x4_t sign  = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u));
        const float32x4_t absx = vabsq_f32(x);

        float32x4_t n = vmlaq_n_f32(vdupq_n_f32(0.583691066395175e-1f), absx, 0.3357335044280075e-1f);
        n = vmlaq_f32(vdupq_n_f32(0.2468149110712040f), absx, n);
        n = vmulq_f32(n, absx);

        float32x4_t d = vmlaq_n_f32(vdupq_n_f32(0.1086202599228572f), absx, 0.2874707922475963e-1f);
        d = vmlaq_f32(vdupq_n_f32(0.609347197060491e-1f), absx, d);
        d = vmlaq_f32(vdupq_n_f32(0.2464845986383725f), absx, d);

        const float32x4_t mag = vmulq_f32(n, recip_fast(d));
        return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(mag), sign));
    }

    // =========================================================================
    // Filter coefficients (Audio EQ Cookbook).  Parameter-change path only.
    // =========================================================================
    void UpdateCoefficients() {
        const float low_hz  = raw_params_[k_param_eq_low_hz]   * 10.0f;
        const float low_db  = (raw_params_[k_param_eq_low_gain]  - 120.0f) * 0.1f;
        const float mid_hz  = raw_params_[k_param_eq_mid_hz]   * 10.0f;
        const float mid_db  = (raw_params_[k_param_eq_mid_gain]  - 120.0f) * 0.1f;
        const float high_hz = raw_params_[k_param_eq_high_hz]  * 100.0f;
        const float high_db = (raw_params_[k_param_eq_high_gain] - 120.0f) * 0.1f;

        set_eq_band(&coeff_eq_low_,  &eq_low_,  &eq_low_active_,  low_hz,  low_db,  1.0f);
        set_eq_band(&coeff_eq_mid_,  &eq_mid_,  &eq_mid_active_,  mid_hz,  mid_db,  1.5f);
        set_eq_band(&coeff_eq_high_, &eq_high_, &eq_high_active_, high_hz, high_db, 1.0f);

        calc_high_shelf(&coeff_dbx_pre_, 4000.0f, +6.0f);
        calc_high_shelf(&coeff_dbx_de_,  4000.0f, -6.0f);

        const MachineSpec& m = spec_for(model_);
        wf_scale_    = m.wf_scale;
        noise_scale_ = m.noise_scale;
        xtalk_scale_ = m.xtalk_scale;
        sat_scale_   = m.sat_scale;
        // Keep a machine's relative THD from also being a level difference.
        sat_makeup_comp_ = 1.0f / sqrtf(m.sat_scale);

        // Age dulls the high end: full Age drops 14 kHz to 5.6 kHz.
        const float lpf_hz = m.lpf_hz * (1.0f - tape_age_ * 0.6f);

        // Record EQ and its exact inverse, the playback EQ: swapping the zero
        // and the pole of a first-order shelf inverts it.
        calc_emphasis(&coeff_rec_pre_, m.rec_emph_hz,  kRecEmphPoleHz);
        calc_emphasis(&coeff_rec_de_,  kRecEmphPoleHz, m.rec_emph_hz);

        calc_peaking(&coeff_tape_head_,  m.head_hz, m.head_db, 1.5f);
        calc_low_pass(&coeff_tape_lpf_,  lpf_hz,  0.707f);
        calc_high_pass(&coeff_hiss_hpf_, m.hiss_hz, 0.707f);
        calc_high_pass(&coeff_dbx_hf_,   2000.0f, 0.707f);  // Type II HF path
        calc_band_limit(&coeff_dbx_bl_,  30.0f, 10000.0f);  // Type II control path
        calc_low_pass(&coeff_console_lpf_, 20000.0f, 0.707f);  // mixer bandwidth
        calc_high_pass(&coeff_xtalk_hpf_, 1500.0f, 0.5f);   // treble-biased bleed
    }

    /// First-order high-pass at lo_hz times first-order low-pass at hi_hz, as
    /// one biquad (bilinear, prewarped): a gentle band limit for a detector.
    void calc_band_limit(biquad_coeffs_t* c, float lo_hz, float hi_hz) {
        const float t1 = tanf(M_PI * clamp_hz(lo_hz) * inverse_samplerate_);
        const float t2 = tanf(M_PI * clamp_hz(hi_hz) * inverse_samplerate_);
        // HP: (1 - z^-1) / (1 + t1) / (1 - p1 z^-1);  LP: t2 (1 + z^-1) / (1 + t2) / (1 - p2 z^-1)
        const float p1 = (1.0f - t1) / (1.0f + t1);
        const float p2 = (1.0f - t2) / (1.0f + t2);
        const float g  = t2 / ((1.0f + t1) * (1.0f + t2));
        c->b0 =  g;  c->b1 = 0.0f;  c->b2 = -g;              // (1 - z^-1)(1 + z^-1) = 1 - z^-2
        c->a1 = -(p1 + p2);
        c->a2 =  p1 * p2;
    }

    /// First-order shelf (1 + s/wz) / (1 + s/wp), unity at DC, by the bilinear
    /// transform with both corners prewarped.  zero < pole is a treble boost,
    /// zero > pole the matching cut; a zero of 0 Hz means no emphasis at all.
    void calc_emphasis(biquad_coeffs_t* c, float zero_hz, float pole_hz) {
        if (zero_hz <= 0.0f || pole_hz <= 0.0f) {
            c->b0 = 1.0f;  c->b1 = c->b2 = c->a1 = c->a2 = 0.0f;
            return;
        }
        const float az = 1.0f / tanf(M_PI * clamp_hz(zero_hz) * inverse_samplerate_);
        const float ap = 1.0f / tanf(M_PI * clamp_hz(pole_hz) * inverse_samplerate_);
        const float inv_a0 = 1.0f / (1.0f + ap);
        c->b0 = (1.0f + az) * inv_a0;
        c->b1 = (1.0f - az) * inv_a0;
        c->a1 = (1.0f - ap) * inv_a0;
        c->b2 = c->a2 = 0.0f;
    }

    /// A band at exactly 0 dB is a unity filter, so mark it inactive and clear
    /// its state; parametric_eq() then steps over it entirely.
    void set_eq_band(biquad_coeffs_t* c, biquad_state2_t* s, bool* active,
                     float hz, float db, float q) {
        const bool on = (db != 0.0f);
        if (!on && *active) biquad_reset2(s);
        *active = on;
        if (on) calc_peaking(c, hz, db, q);
    }

    /// The coefficient functions below use libm's cosf/sinf, not float_math's
    /// fast versions: those carry ~1.3e-5 of absolute error, and a biquad's
    /// centre frequency hangs on cos(w0) ~ 1 - w0^2/2, so at the bottom of the
    /// range the error moved the 60 Hz head bump to 72 Hz and a 27.5 Hz one to
    /// 48 Hz.  Parameter-change path only, so libm is affordable.
    ///
    /// Keep every cutoff inside a range where the cookbook formulas are well
    /// conditioned.  A zeroed parameter array used to ask for 0 Hz, which makes
    /// alpha 0 and produces a degenerate pole-on-the-unit-circle section.
    fast_inline float clamp_hz(float hz) const {
        const float hi = samplerate_ * 0.47f;
        return (hz < 10.0f) ? 10.0f : (hz > hi ? hi : hz);
    }

    void calc_peaking(biquad_coeffs_t* c, float hz, float db, float q) {
        const float A      = powf(10.0f, db * 0.025f);   // 10^(db/40)
        const float w0     = M_TWOPI * clamp_hz(hz) * inverse_samplerate_;
        const float cosw   = cosf(w0);
        const float alpha  = sinf(w0) / (2.0f * q);
        const float inv_a0 = A / (A + alpha);   // 1 / (1 + alpha/A)
        c->b0 =  (1.0f + alpha * A) * inv_a0;
        c->b1 = (-2.0f * cosw)      * inv_a0;
        c->b2 =  (1.0f - alpha * A) * inv_a0;
        c->a1 = c->b1;                          // peaking EQ: a1 == b1
        c->a2 =  (1.0f - alpha / A) * inv_a0;
    }

    void calc_high_shelf(biquad_coeffs_t* c, float hz, float db) {
        const float A    = powf(10.0f, db * 0.025f);
        const float w0   = M_TWOPI * clamp_hz(hz) * inverse_samplerate_;
        const float cosw = cosf(w0), sinw = sinf(w0);
        // alpha for the shelf form, S = 1  =>  sqrt((A + 1/A)(1/S - 1) + 2)
        const float alpha  = (sinw * 0.5f) * fasterSqrt((A + 1.0f / A) * 0.414427157f + 2.0f);
        const float sqA2   = 2.0f * fasterSqrt(A) * alpha;
        const float inv_a0 = 1.0f / ((A + 1.0f) - (A - 1.0f) * cosw + sqA2);
        c->b0 =  A * ((A + 1.0f) + (A - 1.0f) * cosw + sqA2) * inv_a0;
        c->b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw) * inv_a0;
        c->b2 =  A * ((A + 1.0f) + (A - 1.0f) * cosw - sqA2) * inv_a0;
        c->a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)     * inv_a0;
        c->a2 = ((A + 1.0f) - (A - 1.0f) * cosw - sqA2)      * inv_a0;
    }

    void calc_low_pass(biquad_coeffs_t* c, float hz, float q) {
        const float w0   = M_TWOPI * clamp_hz(hz) * inverse_samplerate_;
        const float cosw = cosf(w0), sinw = sinf(w0);
        const float alpha  = sinw / (2.0f * q);
        const float inv_a0 = 1.0f / (1.0f + alpha);
        c->b0 = (1.0f - cosw) * 0.5f * inv_a0;
        c->b1 = (1.0f - cosw)        * inv_a0;
        c->b2 = c->b0;
        c->a1 = -2.0f * cosw         * inv_a0;
        c->a2 = (1.0f - alpha)       * inv_a0;
    }

    void calc_high_pass(biquad_coeffs_t* c, float hz, float q) {
        const float w0   = M_TWOPI * clamp_hz(hz) * inverse_samplerate_;
        const float cosw = cosf(w0), sinw = sinf(w0);
        const float alpha  = sinw / (2.0f * q);
        const float inv_a0 = 1.0f / (1.0f + alpha);
        c->b0 =  (1.0f + cosw) * 0.5f * inv_a0;
        c->b1 = -(1.0f + cosw)        * inv_a0;
        c->b2 = c->b0;
        c->a1 = -2.0f * cosw          * inv_a0;
        c->a2 = (1.0f - alpha)        * inv_a0;
    }

    // =========================================================================
    // PRNG — Xorshift128+, two 64-bit lanes
    // =========================================================================
    void prng_init(uint32_t seed) {
        const uint64_t s = seed;
        const uint64_t s0[2] = { s | 1ULL, s * 0x9E3779B97F4A7C15ULL };
        const uint64_t s1[2] = { s * 0xBF58476D1CE4E5B9ULL, s * 0x94D049BB133111EBULL };
        prng_.state0 = vld1q_u64(s0);
        prng_.state1 = vld1q_u64(s1);
    }

    fast_inline uint32x4_t prng_rand_u32() {
        uint64x2_t s0 = prng_.state0, s1 = prng_.state1;
        s1 = veorq_u64(s1, vshlq_n_u64(s1, 23));
        s1 = veorq_u64(s1, vshrq_n_u64(s1, 17));
        s1 = veorq_u64(s1, veorq_u64(s0, vshrq_n_u64(s0, 26)));
        prng_.state0 = s1;
        prng_.state1 = s0;
        const uint64x2_t sum = vaddq_u64(s1, s0);
        return vcombine_u32(vmovn_u64(sum), vshrn_n_u64(sum, 32));
    }

    /// 4 floats in [0, 1).  Uses the top 23 bits: xorshift128+'s low bits are
    /// its weakest (they fail linearity tests), and masking them in was audible
    /// as periodicity in long crackle tails.
    fast_inline float32x4_t prng_unit() {
        const uint32x4_t r = vshrq_n_u32(prng_rand_u32(), 9);
        const uint32x4_t m = vorrq_u32(r, vdupq_n_u32(0x3F800000u));
        return vsubq_f32(vreinterpretq_f32_u32(m), vdupq_n_f32(1.0f));
    }

    /// 4 zero-mean floats in [-1, 1).
    fast_inline float32x4_t prng_bipolar() {
        return vmlaq_n_f32(vdupq_n_f32(-1.0f), prng_unit(), 2.0f);
    }

    // =========================================================================
    // Members
    // =========================================================================
    int32_t raw_params_[k_num_params] __attribute__((aligned(16)));
    float   samplerate_;
    float   inverse_samplerate_;
    bool    b_bypass_;
    uint32_t in_stride_ = 2u;   // input floats per frame: 2, or 4 with the sidechain pair
    std::atomic<bool> b_update_filters_;
    std::atomic<bool> b_dbx_resync_{false};

    uint8_t model_    = 0;
    uint8_t dbx_mode_ = 0;

    float smooth_coeff_;
    float target_trim_,   current_trim_;     // console input gain
    float target_fader_,  current_fader_;    // its make-up
    float target_mix_,    current_mix_;
    float target_sat_drive_,  current_sat_drive_;    // saturator input gain
    float target_sat_makeup_, current_sat_makeup_;   // its make-up
    float tape_age_;
    float vinyl_bias_;
    float vinyl_dc_offset_;
    float xtalk_amount_;
    float hiss_amount_;

    // Per-machine scalings, resolved from the spec table on a model change.
    float wf_scale_, noise_scale_, xtalk_scale_, sat_scale_, sat_makeup_comp_;

    // dbx compander: encoder (feedback) and decoder (feed-forward) detectors
    float attack_ms_,  attack_coeff_;
    float release_ms_, release_coeff_;
    float32x4_t wb_rms_w_, hf_rms_w_;    // quad-folded RMS weights
    float wb_rms_decay_, hf_rms_decay_;
    DbxSide dbx_enc_, dbx_dec_;

    // 4x oversamplers around the console clip and the tape/vinyl shaper
    hb4x_state_t console_os_, tape_os_;

    // Wow & flutter
    float    wf_phase_inc_, wf_lfo_phase_;
    float    wf_flutter_phase_, wf_flutter_phase_inc_;
    float    wf_delay_[WF_BUFFER_SIZE * 2] __attribute__((aligned(16))); // interleaved L,R
    uint32_t wf_write_pos_;

    // Vinyl dust
    float vinyl_pop_threshold_;
    float vinyl_big_pop_threshold_;
    float pop_env_;
    float big_pop_env_;
    float32x4_t dust_hist_l_, dust_hist_r_;
    float32x4_t vinyl_hist_l_, vinyl_hist_r_;

    prng_t prng_;

    // Stereo biquads (one state pair each, L and R sharing coefficients)
    biquad_state2_t eq_low_, eq_mid_, eq_high_;
    biquad_state2_t dbx_pre_, dbx_de_;
    biquad_state2_t rec_pre_, rec_de_, console_lpf_;
    biquad_state2_t tape_head_, tape_lpf_;
    biquad_state2_t hiss_hpf_, xtalk_hpf_;
    biquad_state2_t dc_block_;

    bool eq_low_active_, eq_mid_active_, eq_high_active_;

    biquad_coeffs_t coeff_eq_low_, coeff_eq_mid_, coeff_eq_high_;
    biquad_coeffs_t coeff_dbx_pre_, coeff_dbx_de_, coeff_dbx_hf_, coeff_dbx_bl_;
    biquad_coeffs_t coeff_rec_pre_, coeff_rec_de_, coeff_console_lpf_;
    biquad_coeffs_t coeff_tape_head_, coeff_tape_lpf_;
    biquad_coeffs_t coeff_hiss_hpf_, coeff_xtalk_hpf_;
    biquad_coeffs_t coeff_dc_block_;
};

// C++14 still wants a definition for an odr-used static constexpr member.
constexpr int32_t PortaCassette::kParamInit[PortaCassette::k_num_params];

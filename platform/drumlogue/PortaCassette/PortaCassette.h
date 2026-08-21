#pragma once
/**
 * @file PortaCassette.h
 * @brief Tascam Portastudio / vinyl emulator for KORG drumlogue
 *
 * Signal chain, evaluated four frames at a time:
 *
 *   PreAmp -> PEQ(3-band) -> dbx encode -> Head bump -> Drive/Saturation
 *          -> Tape LPF -> Hiss or Vinyl dust -> Wow & Flutter -> Crosstalk
 *          -> dbx decode -> DC block -> Dry/Wet -> Soft ceiling
 *
 * The noise sources sit *inside* the dbx encode/decode loop on purpose: that is
 * where tape noise enters on a real machine, and it is what lets the decoder
 * actually reduce it.
 */

#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <arm_neon.h>

#include "unit.h"
#include "float_math.h"
#include "biquad.h"

// Wow & Flutter delay line: 512 frames ~ 10.7 ms at 48 kHz.  Worst-case read
// offset is 30*1.8 + 2.8 = 56.8 frames at full Age, plus two guard taps.
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

// 1/x, ~1e-7 relative.  Used where the decode gain has to invert the encode
// gain closely enough that the residual is a fixed offset rather than a
// signal-dependent modulation.
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

    enum DbxMode { k_active = 0, k_encode_only, k_off };
    enum k7Mode  { k_244    = 0, k_424, k_488, k_vinyl };

    // Mirrors the .init column of header.c.  Kept here so the DSP is coherent
    // before the host has pushed any parameter, and so unit_reset() can clear
    // filter state without also forgetting what the user dialled in.
    static constexpr int32_t kParamInit[k_num_params] = {
        10,   // age        10 %
        100,  // mix       100 %
        20,   // preamp     20 %
        40,   // drive      40 %
        0,    // dbx        Active
        0,    // model      T-244
        8,    // eq low hz    80 Hz
        120,  // eq low gain   0 dB
        100,  // eq mid hz  1000 Hz
        120,  // eq mid gain   0 dB
        100,  // eq high hz  10 kHz
        120,  // eq high gain  0 dB
        5,    // crosstalk   5 %
        15,   // hiss       15 %
        30,   // attack     3.0 ms
        150,  // release    150 ms
    };

    PortaCassette() { Configure(48000.0f); }
    ~PortaCassette() {}

    int8_t Init(const unit_runtime_desc_t* desc) {
        Configure((float)desc->samplerate);
        return k_unit_err_none;
    }

    /// unit_reset(): clear every filter/delay/envelope tail.  Deliberately does
    /// NOT touch the parameters — the host does not re-send them afterwards.
    void Reset() {
        biquad_reset2(&eq_low_);    biquad_reset2(&eq_mid_);   biquad_reset2(&eq_high_);
        biquad_reset2(&dbx_pre_);   biquad_reset2(&dbx_de_);   biquad_reset2(&dbx_hf_);
        biquad_reset2(&tape_head_); biquad_reset2(&tape_lpf_);
        biquad_reset2(&hiss_hpf_);  biquad_reset2(&xtalk_hpf_);
        biquad_reset2(&dc_block_);

        memset(wf_delay_, 0, sizeof(wf_delay_));
        wf_write_pos_     = 0;
        wf_lfo_phase_     = 0.0f;
        wf_flutter_phase_ = 0.0f;

        // Seed the detectors at the compander's unity point so the first block
        // after a reset does not open up to the encode ceiling.
        dbx_wb_env_ = kDbxRefPower;
        dbx_hf_env_ = kDbxRefPower;
        dbx_gain_   = 1.0f;

        vinyl_hist_l_ = vinyl_hist_r_ = vdupq_n_f32(0.0f);
        dust_hist_l_  = dust_hist_r_  = vdupq_n_f32(0.0f);

        pop_env_     = 0.0f;
        big_pop_env_ = 0.0f;

        current_preamp_ = target_preamp_;
        current_drive_  = target_drive_;
        current_mix_    = target_mix_;

        prng_init(1337u);
        b_update_filters_.store(true);
    }

    void SetBypass(bool bypass) { b_bypass_ = bypass; }

    void SetParameter(uint8_t id, int32_t value) {
        if (id >= k_num_params) return;
        raw_params_[id] = value;
        const float norm = value * 0.01f;
        switch (id) {
            case k_param_model:
                model_ = (uint8_t)((value < 0) ? 0 : (value > k_vinyl ? k_vinyl : value));
                wf_phase_inc_ = M_TWOPI * ((model_ == k_vinyl) ? 0.55f : 1.5f) * inverse_samplerate_;
                b_update_filters_.store(true);  // tape model affects LPF/head coeffs
                break;
            case k_param_preamp: target_preamp_ = 1.0f + norm * 2.0f;  break;
            case k_param_drive:  target_drive_  = norm;                break;
            case k_param_mix:    target_mix_    = norm;                break;
            case k_param_dbx:
                dbx_mode_ = (uint8_t)((value < 0) ? 0 : (value > k_off ? k_off : value));
                break;
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
            if (in != out) memcpy(out, in, frames * 2u * sizeof(float));
            return;
        }

        const uint32_t saved_fpscr = fpu_enter_flush_to_zero();

        if (b_update_filters_.exchange(false))
            UpdateCoefficients();

        const uint32_t whole = frames & ~3u;
        for (uint32_t f = 0; f < whole; f += 4u)
            ProcessQuad(in + f * 2u, out + f * 2u);

        // Buffers shorter than the advertised frames_per_buffer are legal but
        // rare.  Rather than maintain a second, slowly diverging scalar copy of
        // the whole chain, pad the tail to four frames by holding the last
        // valid one and discard the padding on the way out.  Sample-and-hold
        // padding keeps the delay line and the IIR states click-free; the cost
        // is that they advance by up to three extra frames in this rare case.
        const uint32_t rem = frames - whole;
        if (rem != 0u) {
            float scratch[NEON_LANES * 2];
            const float* tail = in + whole * 2u;
            for (uint32_t i = 0; i < NEON_LANES; ++i) {
                const uint32_t s = (i < rem) ? i : rem - 1u;
                scratch[i * 2u]      = tail[s * 2u];
                scratch[i * 2u + 1u] = tail[s * 2u + 1u];
            }
            ProcessQuad(scratch, scratch);
            memcpy(out + whole * 2u, scratch, rem * 2u * sizeof(float));
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
    static constexpr float kDbxRefScale = 0.35481339f;   // kDbxRefPower^0.25

    // Encode-gain limits.  With decode engaged the pair is exactly inverse, so
    // the clamp only bounds how hard the tape stage is driven; in encode-only
    // mode the listener hears the compression directly, so it is tightened.
    static constexpr float kEncGainMin      = 0.25f;     // -12 dB
    static constexpr float kEncGainMax      = 4.00f;     // +12 dB
    static constexpr float kEncOnlyGainMin  = 0.50f;     //  -6 dB
    static constexpr float kEncOnlyGainMax  = 2.00f;     //  +6 dB

    static constexpr float kCeilingKnee  = 0.70f;        // soft ceiling onset
    static constexpr float kCeilingRange = 0.30f;        // knee + range = 1.0

    static constexpr float kOutputGain   = 1.30f;        // ~ +2.3 dB make-up

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

        wb_rms_coeff_ = one_pole_coeff(kWbRmsMs);
        hf_rms_coeff_ = one_pole_coeff(kHfRmsMs);

        wf_flutter_phase_inc_ = M_TWOPI * 25.0f * inverse_samplerate_;
        wf_phase_inc_         = M_TWOPI * 1.5f  * inverse_samplerate_;

        // Load the header defaults through the normal parameter path so there
        // is exactly one place where a default is written down.
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
    fast_inline void ProcessQuad(const float* in, float* out) {
        // Parameter smoothing: one 1-pole step per quad, ~8 ms at any rate.
        const float ks = smooth_coeff_;
        current_preamp_ += ks * (target_preamp_ - current_preamp_);
        current_drive_  += ks * (target_drive_  - current_drive_);
        current_mix_    += ks * (target_mix_    - current_mix_);

        const float32x4x2_t vi = vld2q_f32(in);
        const float32x4_t dry_l = vi.val[0];
        const float32x4_t dry_r = vi.val[1];

        float32x4_t sig_l = vmulq_n_f32(dry_l, current_preamp_);
        float32x4_t sig_r = vmulq_n_f32(dry_r, current_preamp_);

        parametric_eq(sig_l, sig_r);

        // dbx encode: HF pre-emphasis + 2:1 RMS companding.  Returns the
        // matching decode gain so encode x decode is exactly unity.
        const float32x4_t dec_gain = dbx_nr_encode(sig_l, sig_r);

        head_bump(sig_l, sig_r);
        drive_into_soft_saturation(sig_l, sig_r);
        tape_age(sig_l, sig_r);

        // Noise sits after saturation (so Drive does not amplify it) but before
        // the decoder (so the decoder can suppress it, as dbx does on tape).
        if (model_ == k_vinyl) vinyl_dust(sig_l, sig_r);
        else                   tape_hiss(sig_l, sig_r);

        wow_and_flutter(sig_l, sig_r);
        crosstalk(sig_l, sig_r);
        dbx_nr_decode(sig_l, sig_r, dec_gain);

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

    /// Serial per-sample 1-pole over the four lanes of a quad.
    ///
    /// Keeping the state in a float32x4_t and doing one vector update per quad
    /// — as this used to — gives each lane its own follower running on a
    /// decimated-by-4 stream: the time constants come out 4x too long and the
    /// lanes drift apart, stair-stepping the result at fs/4.  omnipress smooths
    /// its compressor gain serially for the same reason.
    fast_inline float32x4_t smooth_serial(float& state, float32x4_t x, float k) const {
        float st = state;
        float32x4_t y = x;
#define PC_SMOOTH_STEP(LANE)                                        \
        {                                                           \
            st += k * (vgetq_lane_f32(x, LANE) - st);               \
            y = vsetq_lane_f32(st, y, LANE);                        \
        }
        PC_SMOOTH_STEP(0)
        PC_SMOOTH_STEP(1)
        PC_SMOOTH_STEP(2)
        PC_SMOOTH_STEP(3)
#undef PC_SMOOTH_STEP
        state = st;
        return y;
    }

    /// Serial per-sample gain ballistics: attack while the compander is pulling
    /// gain down (the programme got louder), release while it is letting gain
    /// back up.
    fast_inline float32x4_t smooth_gain(float& state, float32x4_t target) const {
        const float ka = attack_coeff_, kr = release_coeff_;
        float st = state;
        float32x4_t y = target;
#define PC_GAIN_STEP(LANE)                                          \
        {                                                           \
            const float g = vgetq_lane_f32(target, LANE);           \
            st += ((g < st) ? ka : kr) * (g - st);                  \
            y = vsetq_lane_f32(st, y, LANE);                        \
        }
        PC_GAIN_STEP(0)
        PC_GAIN_STEP(1)
        PC_GAIN_STEP(2)
        PC_GAIN_STEP(3)
#undef PC_GAIN_STEP
        state = st;
        return y;
    }

    fast_inline float32x4_t dbx_nr_encode(float32x4_t &sig_l, float32x4_t &sig_r) {
        if (dbx_mode_ == k_off)
            return vdupq_n_f32(1.0f);

        // HF pre-emphasis (record side).
        biquad_process4_stereo(&dbx_pre_, &coeff_dbx_pre_, &sig_l, &sig_r);

        // Type II HF spectral path: track the band above ~2 kHz separately so
        // bright transients get more reduction than their level alone implies.
        float32x4_t hf_l = sig_l, hf_r = sig_r;
        biquad_process4_stereo(&dbx_hf_, &coeff_dbx_hf_, &hf_l, &hf_r);

        const float32x4_t hf_sq = vmlaq_f32(vmulq_f32(hf_l,  hf_l),  hf_r,  hf_r);
        const float32x4_t wb_sq = vmlaq_f32(vmulq_f32(sig_l, sig_l), sig_r, sig_r);

        // Fixed-window RMS averaging, not the user's ballistics.  A real dbx
        // detector averages power over a fixed time; putting a 3 ms attack
        // straight onto the power made the detector chase the 2f ripple of a
        // bass note and modulate its own gain at twice the note frequency.
        // Attack/Release now shape the *gain*, which is what they mean on a
        // compressor and what omnipress does.
        const float32x4_t hf_env = smooth_serial(dbx_hf_env_, hf_sq, hf_rms_coeff_);
        const float32x4_t wb_env = smooth_serial(dbx_wb_env_, wb_sq, wb_rms_coeff_);

        float32x4_t env = vmlaq_n_f32(vmulq_n_f32(wb_env, 0.7f), hf_env, 0.3f);
        env = vmaxq_f32(env, vdupq_n_f32(1.0e-9f));

        // 2:1 companding referenced to kDbxRefPower:
        //   gain = (env / ref)^(-1/4)
        // env is power, so amplitude^(-1/2) — a true 2:1 slope — is env^(-1/4).
        // Two Newton-refined rsqrt steps beat the generic pow() path by ~40 ops
        // and track it to better than 2e-5 relative.
        float32x4_t rsq = vrsqrteq_f32(env);
        rsq = vmulq_f32(vrsqrtsq_f32(vmulq_f32(env, rsq), rsq), rsq);   // 1/sqrt(env)
        const float32x4_t sqrt_env = vmulq_f32(env, rsq);               // sqrt(env)
        float32x4_t enc_gain = vrsqrteq_f32(sqrt_env);
        enc_gain = vmulq_f32(vrsqrtsq_f32(vmulq_f32(sqrt_env, enc_gain), enc_gain), enc_gain);
        enc_gain = vmulq_n_f32(enc_gain, kDbxRefScale);

        const bool enc_only = (dbx_mode_ == k_encode_only);
        enc_gain = vminq_f32(enc_gain, vdupq_n_f32(enc_only ? kEncOnlyGainMax : kEncGainMax));
        enc_gain = vmaxq_f32(enc_gain, vdupq_n_f32(enc_only ? kEncOnlyGainMin : kEncGainMin));
        enc_gain = smooth_gain(dbx_gain_, enc_gain);

        sig_l = vmulq_f32(sig_l, enc_gain);
        sig_r = vmulq_f32(sig_r, enc_gain);

        // Deriving the decode gain from the encode gain (rather than from an
        // independent post-tape detector) keeps encode x decode == 1 at all
        // times.  Independent followers used to mismatch on the first hit after
        // silence and produce a click followed by a hole.
        return recip_nr(enc_gain);
    }

    fast_inline void dbx_nr_decode(float32x4_t &sig_l, float32x4_t &sig_r,
                                   float32x4_t dec_gain) {
        // Encode-only is the classic Portastudio abuse trick: record through
        // the encoder, play back without the decoder, and keep the compressed,
        // pre-emphasised signal.  It only means anything if the decoder really
        // is out of circuit — this branch used to run for encode-only too, so
        // the mode did nothing but tighten a clamp.
        if (dbx_mode_ != k_active)
            return;

        sig_l = vmulq_f32(sig_l, dec_gain);
        sig_r = vmulq_f32(sig_r, dec_gain);
        biquad_process4_stereo(&dbx_de_, &coeff_dbx_de_, &sig_l, &sig_r);
    }

    fast_inline void head_bump(float32x4_t &sig_l, float32x4_t &sig_r) {
        biquad_process4_stereo(&tape_head_, &coeff_tape_head_, &sig_l, &sig_r);
    }

    fast_inline void tape_age(float32x4_t &sig_l, float32x4_t &sig_r) {
        biquad_process4_stereo(&tape_lpf_, &coeff_tape_lpf_, &sig_l, &sig_r);
    }

    fast_inline void drive_into_soft_saturation(float32x4_t &sig_l, float32x4_t &sig_r) {
        const float drive = 1.0f + current_drive_ * 3.0f;
        // Compensate most of the drive gain so the knob changes character
        // rather than level; the residual 0.9 keeps some of tape's own bloom.
        const float makeup = 0.9f / (1.0f + current_drive_ * 1.6f);
        if (model_ == k_vinyl) {
            sig_l = vmulq_n_f32(vinyl_saturate(sig_l, drive, vinyl_hist_l_), makeup);
            sig_r = vmulq_n_f32(vinyl_saturate(sig_r, drive, vinyl_hist_r_), makeup);
        } else {
            sig_l = vmulq_n_f32(tape_saturation(vmulq_n_f32(sig_l, drive)), makeup);
            sig_r = vmulq_n_f32(tape_saturation(vmulq_n_f32(sig_r, drive)), makeup);
        }
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
        const float wow_depth     = 5.0f + tape_age_ * 25.0f;
        const float flutter_depth = 0.8f + tape_age_ * 2.0f;

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
        // Treble leaks between adjacent tape tracks far more readily than bass.
        float32x4_t bleed_l = sig_l, bleed_r = sig_r;
        biquad_process4_stereo(&xtalk_hpf_, &coeff_xtalk_hpf_, &bleed_l, &bleed_r);
        const float32x4_t xl = vmlaq_n_f32(sig_l, bleed_r, xtalk_amount_);
        const float32x4_t xr = vmlaq_n_f32(sig_r, bleed_l, xtalk_amount_);
        sig_l = xl;
        sig_r = xr;
    }

    fast_inline void tape_hiss(float32x4_t &sig_l, float32x4_t &sig_r) {
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
        const float hiss_scaled = hiss_amount_ * (1.0f + tape_age_ * 2.0f);
        sig_l = vmlaq_n_f32(sig_l, nl, hiss_scaled);
        sig_r = vmlaq_n_f32(sig_r, nr, hiss_scaled);
    }

    fast_inline void vinyl_dust(float32x4_t &sig_l, float32x4_t &sig_r) {
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

        const float dust_level = 0.06f + tape_age_ * 0.08f;
        sig_l = vmlaq_n_f32(sig_l, crack_l, dust_level);
        sig_r = vmlaq_n_f32(sig_r, crack_r, dust_level);
    }

    /// Soft output ceiling: unity below the knee, tangent-continuous through a
    /// quadratic knee, and exactly 1.0 once the overshoot reaches 2*range.  The
    /// old stage hard-clipped at +/-1, which sprays broadband aliasing across
    /// the master bus precisely when the unit is being pushed.
    ///
    /// Written without a division: no reciprocal to refine, and an infinite
    /// input lands on 1.0 instead of going through a 0*inf NaN.
    fast_inline float32x4_t soft_ceiling(float32x4_t x) const {
        const float32x4_t knee = vdupq_n_f32(kCeilingKnee);
        const float32x4_t a    = vabsq_f32(x);
        float32x4_t over = vmaxq_f32(vsubq_f32(a, knee), vdupq_n_f32(0.0f));
        over = vminq_f32(over, vdupq_n_f32(2.0f * kCeilingRange));
        // mag = min(a,knee) + over - over^2 / (4*range)
        float32x4_t mag = vaddq_f32(vminq_f32(a, knee), over);
        mag = vmlsq_n_f32(mag, vmulq_f32(over, over), 0.25f / kCeilingRange);
        const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), vdupq_n_u32(0x80000000u));
        return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(mag), sign));
    }

    // =========================================================================
    // Non-linearities
    // =========================================================================

    /// Asymmetric vinyl saturation with mechanical tracing distortion.
    /// @param hist per-channel one-quad history — L and R used to share a single
    ///        history vector, which cross-fed the two channels' tracing filters.
    fast_inline float32x4_t vinyl_saturate(float32x4_t x, float drive,
                                           float32x4_t &hist) const {
        // Bias breaks the symmetry and generates the even harmonics that give
        // worn vinyl its warmth.
        const float32x4_t xb = vmlaq_n_f32(vdupq_n_f32(vinyl_bias_), x, drive);

        // Rational clipper f(x) = x / (1 + |x| + 0.3x^2): a cheap tanh with a
        // warmer knee.
        const float32x4_t denom =
            vmlaq_n_f32(vaddq_f32(vdupq_n_f32(1.0f), vabsq_f32(xb)),
                        vmulq_f32(xb, xb), 0.3f);
        const float32x4_t sat = vmulq_f32(xb, recip_fast(denom));
        const float32x4_t y   = vsubq_f32(sat, vdupq_n_f32(vinyl_dc_offset_));

        // Tracing error: the stylus cannot follow the groove wall exactly, so
        // add a signed-square of the groove velocity (a 1-sample difference).
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

        float lpf_hz   = 14000.0f;
        float head_hz  = 65.0f;
        float hiss_cut = 3500.0f;

        switch (model_) {
            case k_424:   lpf_hz = 11000.0f; head_hz =  55.0f; break;
            case k_488:   lpf_hz =  9500.0f; head_hz =  45.0f; break;
            case k_vinyl: lpf_hz = 12000.0f; head_hz = 150.0f; hiss_cut = 400.0f; break;
            default: break;  // k_244
        }

        // Age dulls the high end: full Age drops 14 kHz to 5.6 kHz.
        lpf_hz *= (1.0f - tape_age_ * 0.6f);

        calc_peaking(&coeff_tape_head_,  head_hz, +4.0f, 1.5f);
        calc_low_pass(&coeff_tape_lpf_,  lpf_hz,  0.707f);
        calc_high_pass(&coeff_hiss_hpf_, hiss_cut, 0.707f);
        calc_high_pass(&coeff_dbx_hf_,   2000.0f, 0.707f);  // Type II HF path
        calc_high_pass(&coeff_xtalk_hpf_, 1500.0f, 0.5f);   // treble-biased bleed
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
        const float cosw   = fastercosfullf(w0);
        const float alpha  = fastersinfullf(w0) / (2.0f * q);
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
        const float cosw = fastercosfullf(w0), sinw = fastersinfullf(w0);
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
        const float cosw = fastercosfullf(w0), sinw = fastersinfullf(w0);
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
        const float cosw = fastercosfullf(w0), sinw = fastersinfullf(w0);
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
    std::atomic<bool> b_update_filters_;

    uint8_t model_;
    uint8_t dbx_mode_;

    float smooth_coeff_;
    float target_preamp_, current_preamp_;
    float target_drive_,  current_drive_;
    float target_mix_,    current_mix_;
    float tape_age_;
    float vinyl_bias_;
    float vinyl_dc_offset_;
    float xtalk_amount_;
    float hiss_amount_;

    // dbx detector
    float attack_ms_,  attack_coeff_;
    float release_ms_, release_coeff_;
    float wb_rms_coeff_, hf_rms_coeff_;
    float dbx_wb_env_;      // wideband RMS power envelope
    float dbx_hf_env_;      // Type II HF spectral envelope
    float dbx_gain_;        // smoothed encode gain

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
    biquad_state2_t dbx_pre_, dbx_de_, dbx_hf_;
    biquad_state2_t tape_head_, tape_lpf_;
    biquad_state2_t hiss_hpf_, xtalk_hpf_;
    biquad_state2_t dc_block_;

    bool eq_low_active_, eq_mid_active_, eq_high_active_;

    biquad_coeffs_t coeff_eq_low_, coeff_eq_mid_, coeff_eq_high_;
    biquad_coeffs_t coeff_dbx_pre_, coeff_dbx_de_, coeff_dbx_hf_;
    biquad_coeffs_t coeff_tape_head_, coeff_tape_lpf_;
    biquad_coeffs_t coeff_hiss_hpf_, coeff_xtalk_hpf_;
    biquad_coeffs_t coeff_dc_block_;
};

// C++14 still wants a definition for an odr-used static constexpr member.
constexpr int32_t PortaCassette::kParamInit[PortaCassette::k_num_params];

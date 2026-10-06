#pragma once
/*
 * File: multiband.h
 *
 * Three-band Omnipressor: Standard mode's transfer curve, run once per band.
 *
 * The panel's page 1 means the same thing in both modes.  Each band pivots at
 * THRESH plus its own offset (Lo/Mid/Hi Thresh), follows SLOPE's curve --
 * expansion, compression, limiting or reversal, on both sides of the pivot,
 * bounded by ATT LMT and GAIN LMT -- with its own Ratio in series on top, and
 * is smoothed with ATTACK/RELEASE.  At offsets of 0 and ratios of 1:1 every
 * band does exactly what Standard does to the whole signal, so switching
 * COMP MODE keeps the character and moves it into the bands.
 *
 * DRIVE reaches a tube per band, with the Overlord's drive law (operation_
 * overlord.h), level-matched band by band so DRIVE never moves the balance.
 *
 * Layout.  Everything recursive -- the crossover, the detectors, the gain
 * smoothers, the DC blockers -- runs one sample at a time with the bands (or
 * band x channel) in NEON lanes; the scalar code this replaces ran sixteen
 * biquads per sample one by one and cost 17k instructions per 64-frame render.
 *
 *   crossover        [L R L R]       -> [lowL lowR | restL restR]   (split 1)
 *                    [restL restR x2] -> [midL midR | highL highR]   (split 2)
 *   band signals     v1 = [lowL lowR midL midR], v2 = [highL highR 0 0]
 *   band dynamics    [low mid high -]
 */

#include <arm_neon.h>
#include "constants.h"
#include "float_math.h"

#define BAND_LOW 0
#define BAND_MID 1
#define BAND_HIGH 2
#define NUM_OF_BANDS (3)

/* ---------------------------------------------------------------------------
 * Biquads, one per lane (transposed direct form II)
 * ------------------------------------------------------------------------- */

typedef struct { float b0, b1, b2, a1, a2; } biquad_coeffs_t;
typedef struct { float32x4_t b0, b1, b2, a1, a2; } bq4_coeffs_t;
typedef struct { float32x4_t z1, z2; } bq4_state_t;
typedef struct { float32x2_t b0, b1, b2, a1, a2; } bq2_coeffs_t;
typedef struct { float32x2_t z1, z2; } bq2_state_t;

fast_inline float32x4_t bq4_tick(bq4_state_t* s, const bq4_coeffs_t* c, float32x4_t x) {
    const float32x4_t y = vmlaq_f32(s->z1, c->b0, x);
    s->z1 = vmlsq_f32(vmlaq_f32(s->z2, c->b1, x), c->a1, y);
    s->z2 = vmlsq_f32(vmulq_f32(c->b2, x), c->a2, y);
    return y;
}

fast_inline float32x2_t bq2_tick(bq2_state_t* s, const bq2_coeffs_t* c, float32x2_t x) {
    const float32x2_t y = vmla_f32(s->z1, c->b0, x);
    s->z1 = vmls_f32(vmla_f32(s->z2, c->b1, x), c->a1, y);
    s->z2 = vmls_f32(vmul_f32(c->b2, x), c->a2, y);
    return y;
}

enum { RBJ_LOWPASS, RBJ_HIGHPASS, RBJ_ALLPASS };

/**
 * Butterworth (Q = 1/sqrt 2) low-pass, high-pass or all-pass.  Two of the
 * first two in series are a Linkwitz-Riley 24 dB/oct pair, and that pair sums
 * to exactly the third: LP^2 + HP^2 = (1 + s^4)/D^2 = (s^2 - sqrt2 s + 1)/D,
 * which survives the bilinear transform unchanged.  The crossover leans on
 * that identity -- see multiband_t::ap.
 */
static inline biquad_coeffs_t rbj_butterworth(int type, float hz, float sample_rate) {
    const float w0 = 2.0f * (float)M_PI * hz / sample_rate;
    const float c = cosf(w0);
    const float alpha = sinf(w0) * 0.70710678f;          // sin(w0) / (2 Q)
    const float inv_a0 = 1.0f / (1.0f + alpha);
    biquad_coeffs_t k;
    switch (type) {
        case RBJ_LOWPASS:
            k.b0 = 0.5f * (1.0f - c) * inv_a0; k.b1 = (1.0f - c) * inv_a0; k.b2 = k.b0; break;
        case RBJ_HIGHPASS:
            k.b0 = 0.5f * (1.0f + c) * inv_a0; k.b1 = -(1.0f + c) * inv_a0; k.b2 = k.b0; break;
        default:
            k.b0 = (1.0f - alpha) * inv_a0; k.b1 = -2.0f * c * inv_a0; k.b2 = 1.0f; break;
    }
    k.a1 = -2.0f * c * inv_a0;
    k.a2 = (1.0f - alpha) * inv_a0;
    return k;
}

/* One Linkwitz-Riley split of a stereo signal: lanes L-LP, R-LP, L-HP, R-HP. */
typedef struct {
    bq4_coeffs_t c;
    bq4_state_t  s1, s2;       // the two cascaded Butterworth stages
} lr4_stereo_t;

/* The same for the mono sidechain key: lanes LP, HP. */
typedef struct {
    bq2_coeffs_t c;
    bq2_state_t  s1, s2;
} lr4_mono_t;

fast_inline void lr4_stereo_design(lr4_stereo_t* x, float hz, float sample_rate) {
    const biquad_coeffs_t lp = rbj_butterworth(RBJ_LOWPASS,  hz, sample_rate);
    const biquad_coeffs_t hp = rbj_butterworth(RBJ_HIGHPASS, hz, sample_rate);
    x->c.b0 = (float32x4_t){lp.b0, lp.b0, hp.b0, hp.b0};
    x->c.b1 = (float32x4_t){lp.b1, lp.b1, hp.b1, hp.b1};
    x->c.b2 = (float32x4_t){lp.b2, lp.b2, hp.b2, hp.b2};
    x->c.a1 = (float32x4_t){lp.a1, lp.a1, hp.a1, hp.a1};
    x->c.a2 = (float32x4_t){lp.a2, lp.a2, hp.a2, hp.a2};
}

fast_inline void lr4_mono_design(lr4_mono_t* x, float hz, float sample_rate) {
    const biquad_coeffs_t lp = rbj_butterworth(RBJ_LOWPASS,  hz, sample_rate);
    const biquad_coeffs_t hp = rbj_butterworth(RBJ_HIGHPASS, hz, sample_rate);
    x->c.b0 = (float32x2_t){lp.b0, hp.b0};
    x->c.b1 = (float32x2_t){lp.b1, hp.b1};
    x->c.b2 = (float32x2_t){lp.b2, hp.b2};
    x->c.a1 = (float32x2_t){lp.a1, hp.a1};
    x->c.a2 = (float32x2_t){lp.a2, hp.a2};
}

/* ---------------------------------------------------------------------------
 * Per-band tube voicing.  The drive law is the Overlord's (constants.h,
 * TUBE_*); only the triode's transfer shape differs per band: softer on the
 * lows so the kick thickens rather than farts out, brighter on the highs.
 * ------------------------------------------------------------------------- */
constexpr float BAND_TUBE_SHAPE_POS[NUM_OF_BANDS] = {3.2f, 5.5f, 6.0f};
constexpr float BAND_TUBE_SHAPE_NEG[NUM_OF_BANDS] = {1.1f, 1.8f, 2.5f};

typedef struct {
    // ---- crossover -------------------------------------------------------
    lr4_stereo_t split_lo;       // low | rest, at xover_low_freq
    lr4_stereo_t split_hi;       // mid | high, at xover_high_freq
    // The low band is the only one that skips split_hi, so on its own it
    // misses the all-pass phase that split_hi puts on the other two, and the
    // three did not sum flat: mid + high = HP1 * AP2 but low = LP1, not
    // LP1 * AP2.  This all-pass at the high split puts it back, and the sum
    // becomes AP1 * AP2 -- flat.  The slot for it existed and was never filled.
    bq2_coeffs_t ap_c;
    bq2_state_t  ap_s;
    lr4_mono_t   sc_lo, sc_hi;   // the external key's own tree (detection only)

    // ---- dynamics, lanes = [low mid high -] ---------------------------------
    float32x4_t env;             // peak follower, linear
    float32x4_t gain_db;         // smoothed gain
    float32x4_t slope;           // each band's curve: (1 + f) / ratio - 1
    float32x4_t thresh;          // THRESH + offset, dB

    // ---- drive stage, lanes = [lowL lowR midL midR] and [highL highR - -] --
    float32x4_t dyn_bias[2];     // grid-current bias trackers
    float32x4_t dc_x[2], dc_y[2];
    float32x4_t idle[2];         // the tube's output with no signal
    float32x4_t shape_pos[2], shape_neg[2];
    // level matching, lanes = [low mid high -]
    float32x4_t lm_in, lm_out, lm_in_slow, lm_out_slow, lm_gain;
    bool        drive_on;        // the stage ran last block

    // ---- solo / mute, per lane of the two band vectors -----------------------
    float32x4_t weight[2];

    // ---- settings ---------------------------------------------------------
    float master_slope;          // SLOPE's function slope (MasterFX::function_slope_)
    float master_thresh;         // THRESH, dB
    float atten_db, boost_db;    // ATT LMT, GAIN LMT
    float band_offset[NUM_OF_BANDS];
    float band_ratio[NUM_OF_BANDS];
    float att_coeff, rel_coeff;  // ATTACK / RELEASE, per sample
    float env_pre_coeff;         // detector decay
    float lm_attack, lm_release, lm_slow;   // level matching, per block
    float drive;                 // 0..1
    float drive_g1, drive_g2;    // the two stages' gains
    float blend;                 // parallel fade-in at the bottom of the knob

    float xover_low_freq;        // asked for
    float xover_high_freq;
    float designed_low;          // what the coefficients are for
    float designed_high;
    float sample_rate;
} multiband_t;

/* ---------------------------------------------------------------------------
 * Settings
 * ------------------------------------------------------------------------- */

/** Rebuild the per-band curve from SLOPE, THRESH and the band knobs. */
fast_inline void multiband_update_curve(multiband_t* mb) {
    float s[4] = {0.0f, 0.0f, 0.0f, 0.0f}, t[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int b = 0; b < NUM_OF_BANDS; ++b) {
        // A ratio in series with SLOPE's curve.  SLOPE's output slope is
        // 1 + f (f = +3 expands 1:4, 0 is 1:1, -1 limits, -2 reverses), and a
        // second compressor at r:1 divides it by r -- exact when the two pivot
        // at the same point, which they do here.
        s[b] = (1.0f + mb->master_slope) / mb->band_ratio[b] - 1.0f;
        t[b] = mb->master_thresh + mb->band_offset[b];
    }
    mb->slope  = vld1q_f32(s);
    mb->thresh = vld1q_f32(t);
}

fast_inline void multiband_set_curve(multiband_t* mb, float slope, float thresh_db,
                                     float atten_db, float boost_db) {
    mb->master_slope  = slope;
    mb->master_thresh = thresh_db;
    mb->atten_db      = atten_db;
    mb->boost_db      = boost_db;
    multiband_update_curve(mb);
}

fast_inline void multiband_set_band_offset(multiband_t* mb, int band, float db) {
    if (band < 0 || band >= NUM_OF_BANDS) return;
    mb->band_offset[band] = db;
    multiband_update_curve(mb);
}

fast_inline void multiband_set_band_ratio(multiband_t* mb, int band, float ratio) {
    if (band < 0 || band >= NUM_OF_BANDS) return;
    mb->band_ratio[band] = (ratio < 1.0f) ? 1.0f : ratio;
    multiband_update_curve(mb);
}

fast_inline void multiband_set_ballistics(multiband_t* mb, float attack_ms, float release_ms) {
    mb->att_coeff = ballistics_coeff(attack_ms,  mb->sample_rate);
    mb->rel_coeff = ballistics_coeff(release_ms, mb->sample_rate);
}

/** DRIVE, 0..1: the Overlord's law, so a DRIVE value means the same push in both modes. */
fast_inline void multiband_set_drive(multiband_t* mb, float drive) {
    mb->drive    = drive;
    mb->drive_g1 = 1.0f + drive * drive * TUBE_STAGE1_GAIN;
    mb->drive_g2 = 1.0f + drive * TUBE_STAGE2_GAIN;
    mb->blend    = (drive < 0.1f) ? drive * 10.0f : 1.0f;
}

// Each split point on its own 0..100 knob, log-interpolated so the knob feels
// even (see XOVER_* in constants.h).
fast_inline float multiband_xover_knob_hz(float min_hz, float knob) {
    return min_hz * expf(knob * XOVER_KNOB_LOG_SPAN);
}

// Only the requested frequency is written here; multiband_process() redesigns
// the filters on the audio thread, so a knob turn can never land half a set of
// coefficients under a running filter.
fast_inline void multiband_set_crossover_low(multiband_t* mb, float knob) {
    mb->xover_low_freq = multiband_xover_knob_hz(XOVER_LOW_HZ_MIN, knob);
}

fast_inline void multiband_set_crossover_high(multiband_t* mb, float knob) {
    mb->xover_high_freq = multiband_xover_knob_hz(XOVER_HIGH_HZ_MIN, knob);
}

// One band soloed or muted at a time (SoloMute in constants.h); everything
// else plays.  Applied after the drive stage, so muting a band does not
// disturb its tube or its level matching.
fast_inline void multiband_set_solo_mute(multiband_t* mb, int sel) {
    float w[NUM_OF_BANDS];
    const bool solo = (sel >= SOLO_LOW && sel <= SOLO_HIGH);
    for (int b = 0; b < NUM_OF_BANDS; ++b) {
        if (solo) w[b] = (sel == SOLO_LOW + b) ? 1.0f : 0.0f;
        else      w[b] = (sel == MUTE_LOW + b) ? 0.0f : 1.0f;
    }
    mb->weight[0] = (float32x4_t){w[BAND_LOW], w[BAND_LOW], w[BAND_MID], w[BAND_MID]};
    mb->weight[1] = (float32x4_t){w[BAND_HIGH], w[BAND_HIGH], 0.0f, 0.0f};
}

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

fast_inline void multiband_design(multiband_t* mb) {
    const float lo = mb->xover_low_freq, hi = mb->xover_high_freq;
    lr4_stereo_design(&mb->split_lo, lo, mb->sample_rate);
    lr4_stereo_design(&mb->split_hi, hi, mb->sample_rate);
    lr4_mono_design(&mb->sc_lo, lo, mb->sample_rate);
    lr4_mono_design(&mb->sc_hi, hi, mb->sample_rate);
    const biquad_coeffs_t ap = rbj_butterworth(RBJ_ALLPASS, hi, mb->sample_rate);
    mb->ap_c.b0 = vdup_n_f32(ap.b0); mb->ap_c.b1 = vdup_n_f32(ap.b1);
    mb->ap_c.b2 = vdup_n_f32(ap.b2); mb->ap_c.a1 = vdup_n_f32(ap.a1);
    mb->ap_c.a2 = vdup_n_f32(ap.a2);
    mb->designed_low  = lo;
    mb->designed_high = hi;
}

/** Put the drive stage where it sits with no signal: no DC step, no level memory. */
fast_inline void multiband_prime_drive(multiband_t* mb) {
    const float32x4_t zero = vdupq_n_f32(0.0f);
    for (int k = 0; k < 2; ++k) {
        mb->dyn_bias[k] = zero;
        mb->dc_x[k] = mb->idle[k];
        mb->dc_y[k] = zero;
    }
    mb->lm_in = mb->lm_out = mb->lm_in_slow = mb->lm_out_slow = zero;
    mb->lm_gain = vdupq_n_f32(1.0f);
}

/**
 * Return every audio-rate history to silence and keep every setting.
 * MasterFX's watchdog calls this when a non-finite value has got in, which
 * must not cost the user their per-band settings.
 */
fast_inline void multiband_clear_state(multiband_t* mb) {
    const float32x4_t zero = vdupq_n_f32(0.0f);
    const float32x2_t zero2 = vdup_n_f32(0.0f);
    mb->split_lo.s1.z1 = mb->split_lo.s1.z2 = zero;
    mb->split_lo.s2.z1 = mb->split_lo.s2.z2 = zero;
    mb->split_hi.s1.z1 = mb->split_hi.s1.z2 = zero;
    mb->split_hi.s2.z1 = mb->split_hi.s2.z2 = zero;
    mb->ap_s.z1 = mb->ap_s.z2 = zero2;
    mb->sc_lo.s1.z1 = mb->sc_lo.s1.z2 = zero2;
    mb->sc_lo.s2.z1 = mb->sc_lo.s2.z2 = zero2;
    mb->sc_hi.s1.z1 = mb->sc_hi.s1.z2 = zero2;
    mb->sc_hi.s2.z1 = mb->sc_hi.s2.z2 = zero2;
    mb->env = zero;
    mb->gain_db = zero;
    multiband_prime_drive(mb);
    mb->drive_on = false;
}

/** Anything in the state gone non-finite?  See the watchdog in MasterFX::Process(). */
fast_inline bool multiband_state_is_nonfinite(const multiband_t* mb) {
    uint32x4_t m = vorrq_u32(nonfinite_mask_q(mb->env), nonfinite_mask_q(mb->gain_db));
    m = vorrq_u32(m, vorrq_u32(nonfinite_mask_q(mb->lm_in), nonfinite_mask_q(mb->lm_out)));
    m = vorrq_u32(m, vorrq_u32(nonfinite_mask_q(mb->lm_in_slow), nonfinite_mask_q(mb->lm_out_slow)));
    m = vorrq_u32(m, nonfinite_mask_q(mb->lm_gain));
    for (int k = 0; k < 2; ++k) {
        m = vorrq_u32(m, vorrq_u32(nonfinite_mask_q(mb->dyn_bias[k]), nonfinite_mask_q(mb->dc_x[k])));
        m = vorrq_u32(m, nonfinite_mask_q(mb->dc_y[k]));
    }
    const uint32x2_t m2 = vorr_u32(vget_low_u32(m), vget_high_u32(m));
    return (vget_lane_u32(m2, 0) | vget_lane_u32(m2, 1)) != 0;
}

fast_inline void multiband_init(multiband_t* mb, float sample_rate) {
    mb->sample_rate = sample_rate;
    mb->xover_low_freq  = XOVER_LOW_FREQ_DEFAULT;
    mb->xover_high_freq = XOVER_HIGH_FREQ_DEFAULT;
    multiband_design(mb);

    // Decay of the per-band peak follower. ENV_HOLD_MS matches the release
    // Standard's detector applies, so both modes settle alike.
    mb->env_pre_coeff = ballistics_coeff(ENV_HOLD_MS, sample_rate);
    const float block_rate = sample_rate * (1.0f / NEON_LANES);
    mb->lm_attack  = ballistics_coeff(DRIVE_LEVEL_ATTACK_MS,  block_rate);
    mb->lm_release = ballistics_coeff(DRIVE_LEVEL_RELEASE_MS, block_rate);
    mb->lm_slow    = ballistics_coeff(DRIVE_LEVEL_SLOW_MS,    block_rate);

    // Per-lane tube shapes, and the output each band's tube settles on with no
    // signal: stage 1 is offset to idle at zero, so stage 2 sees only its own
    // bias, b / sqrt(1 + shape_neg b^2).
    float pos[NUM_OF_BANDS], neg[NUM_OF_BANDS], idle[NUM_OF_BANDS];
    for (int b = 0; b < NUM_OF_BANDS; ++b) {
        pos[b] = BAND_TUBE_SHAPE_POS[b];
        neg[b] = BAND_TUBE_SHAPE_NEG[b];
        idle[b] = TUBE_STAGE2_BIAS / sqrtf(1.0f + neg[b] * TUBE_STAGE2_BIAS * TUBE_STAGE2_BIAS);
    }
    mb->shape_pos[0] = (float32x4_t){pos[0], pos[0], pos[1], pos[1]};
    mb->shape_pos[1] = (float32x4_t){pos[2], pos[2], pos[2], pos[2]};
    mb->shape_neg[0] = (float32x4_t){neg[0], neg[0], neg[1], neg[1]};
    mb->shape_neg[1] = (float32x4_t){neg[2], neg[2], neg[2], neg[2]};
    mb->idle[0] = (float32x4_t){idle[0], idle[0], idle[1], idle[1]};
    mb->idle[1] = (float32x4_t){idle[2], idle[2], 0.0f, 0.0f};

    for (int b = 0; b < NUM_OF_BANDS; ++b) {
        mb->band_offset[b] = 0.0f;
        mb->band_ratio[b]  = 1.0f;
    }
    multiband_set_curve(mb, 0.0f, 0.0f, -30.0f, 30.0f);
    multiband_set_ballistics(mb, 10.0f, 100.0f);
    multiband_set_drive(mb, 0.0f);
    multiband_set_solo_mute(mb, SOLO_MUTE_OFF);

    // The state used to be left untouched here, which meant it survived a
    // Reset: the unit relied on it landing in .bss and being zero exactly once,
    // at load. Anything that got into the state afterwards could not be cleared.
    multiband_clear_state(mb);
}

/* ---------------------------------------------------------------------------
 * Processing
 * ------------------------------------------------------------------------- */

/** [a b c d] -> [a a b b] and [c c - -]: band values onto the band x channel lanes. */
fast_inline float32x4_t bands_to_lanes_lo(float32x4_t v) {
    const float32x2x2_t z = vzip_f32(vget_low_f32(v), vget_low_f32(v));
    return vcombine_f32(z.val[0], z.val[1]);
}
fast_inline float32x4_t bands_to_lanes_hi(float32x4_t v) {
    return vcombine_f32(vdup_lane_f32(vget_high_f32(v), 0), vdup_n_f32(0.0f));
}

/** Sum of each band's two lanes: [lowL lowR midL midR], [highL highR - -] -> [low mid high 0]. */
fast_inline float32x4_t lanes_to_bands(float32x4_t v1, float32x4_t v2) {
    const float32x2_t lm = vpadd_f32(vget_low_f32(v1), vget_high_f32(v1));
    const float32x2_t h  = vpadd_f32(vget_low_f32(v2), vdup_n_f32(0.0f));
    return vcombine_f32(lm, h);
}

/** sqrt(a / b), from the NEON estimates plus Newton steps (block rate, ratio only). */
fast_inline float32x4_t sqrt_ratio_q(float32x4_t a, float32x4_t b) {
    float32x4_t r = vrecpeq_f32(b);
    r = vmulq_f32(vrecpsq_f32(b, r), r);
    r = vmulq_f32(vrecpsq_f32(b, r), r);
    const float32x4_t q = vmaxq_f32(vmulq_f32(a, r), vdupq_n_f32(1e-30f));
    float32x4_t e = vrsqrteq_f32(q);
    e = vmulq_f32(vrsqrtsq_f32(vmulq_f32(q, e), e), e);
    e = vmulq_f32(vrsqrtsq_f32(vmulq_f32(q, e), e), e);
    return vmulq_f32(q, e);
}

/**
 * Overlord tube, one vector of band x channel lanes: the rational preamp
 * (stage1_continuous_preamp) into the dynamic-bias triode (stage2_pirkle_
 * triode), phase restored.  `gk_pos` accumulates the triode's grid current for
 * the bias trackers.
 */
fast_inline float32x4_t band_tube(const multiband_t* mb, int k, float32x4_t x, float32x4_t* gk_pos) {
    const float32x4_t one = vdupq_n_f32(1.0f);
    // Stage 1: x g1 + b over 1 + |x g1 + b|, offset so it idles at zero
    const float32x4_t biased = vmlaq_n_f32(vdupq_n_f32(TUBE_STAGE1_BIAS), x, mb->drive_g1);
    const float32x4_t den1 = vaddq_f32(one, vabsq_f32(biased));
    float32x4_t r = vrecpeq_f32(den1);
    r = vmulq_f32(vrecpsq_f32(den1, r), r);
    const float32x4_t pre = vsubq_f32(vmulq_f32(biased, r),
        vdupq_n_f32(TUBE_STAGE1_BIAS / (1.0f + TUBE_STAGE1_BIAS)));
    // Stage 2: the triode, biased by its static point and its grid-current tracker
    const float32x4_t gk = vmlaq_n_f32(vaddq_f32(vdupq_n_f32(TUBE_STAGE2_BIAS), mb->dyn_bias[k]),
                                       pre, mb->drive_g2);
    *gk_pos = vaddq_f32(*gk_pos, vmaxq_f32(gk, vdupq_n_f32(0.0f)));
    const float32x4_t shape = vbslq_f32(vcgtq_f32(gk, vdupq_n_f32(0.0f)),
                                        mb->shape_pos[k], mb->shape_neg[k]);
    const float32x4_t den2 = vmlaq_f32(one, vmulq_f32(gk, gk), shape);
    float32x4_t e = vrsqrteq_f32(den2);
    e = vmulq_f32(vrsqrtsq_f32(vmulq_f32(e, e), den2), e);
    // The triode inverts and overlord_process inverts it back; both cancel here.
    return vmulq_f32(gk, e);
}

/**
 * The drive stage for one block: tube, DC blocker and level matching per band,
 * then the parallel blend.  v1/v2 are the compressed bands, per sample, and
 * are replaced by the driven ones.
 */
fast_inline void multiband_drive(multiband_t* mb, float32x4_t v1[NEON_LANES], float32x4_t v2[NEON_LANES]) {
    if (!mb->drive_on) { multiband_prime_drive(mb); mb->drive_on = true; }

    const float32x4_t zero = vdupq_n_f32(0.0f);
    float32x4_t w1[NEON_LANES], w2[NEON_LANES];
    float32x4_t pin1 = zero, pin2 = zero, pout1 = zero, pout2 = zero;
    float32x4_t gk1 = zero, gk2 = zero;
    for (int t = 0; t < NEON_LANES; ++t) {
        const float32x4_t a = band_tube(mb, 0, v1[t], &gk1);
        const float32x4_t b = band_tube(mb, 1, v2[t], &gk2);
        // DC blocker (the tube's own corner), one lane per band x channel
        const float32x4_t ya = vmlaq_n_f32(vsubq_f32(a, mb->dc_x[0]), mb->dc_y[0], TUBE_DC_POLE);
        const float32x4_t yb = vmlaq_n_f32(vsubq_f32(b, mb->dc_x[1]), mb->dc_y[1], TUBE_DC_POLE);
        mb->dc_x[0] = a; mb->dc_y[0] = ya;
        mb->dc_x[1] = b; mb->dc_y[1] = yb;
        w1[t] = ya; w2[t] = yb;
        pin1  = vmlaq_f32(pin1,  v1[t], v1[t]);
        pin2  = vmlaq_f32(pin2,  v2[t], v2[t]);
        pout1 = vmlaq_f32(pout1, ya, ya);
        pout2 = vmlaq_f32(pout2, yb, yb);
    }
    // (Lanes 2-3 of the high vector carry no band: the tube idles there, and
    // lanes_to_bands() and bands_to_lanes_hi() leave them out of everything.)

    // Grid-current bias trackers, once per block, as overlord_process does it
    const float alpha = TUBE_BIAS_ALPHA;
    mb->dyn_bias[0] = vmlaq_n_f32(mb->dyn_bias[0],
        vsubq_f32(vmulq_n_f32(gk1, -TUBE_GRID_BIAS * 0.25f), mb->dyn_bias[0]), alpha);
    mb->dyn_bias[1] = vmlaq_n_f32(mb->dyn_bias[1],
        vsubq_f32(vmulq_n_f32(gk2, -TUBE_GRID_BIAS * 0.25f), mb->dyn_bias[1]), alpha);
    mb->dyn_bias[0] = flush_denormal_q(mb->dyn_bias[0]);
    mb->dyn_bias[1] = flush_denormal_q(vcombine_f32(vget_low_f32(mb->dyn_bias[1]), vdup_n_f32(0.0f)));

    // Level matching per band, level_match_process()'s law with the bands in
    // lanes: mean power over the block and both channels, a fast follower used
    // as a ceiling over a slow symmetric one, floor and clamp.
    const float32x4_t pin  = vmulq_n_f32(lanes_to_bands(pin1,  pin2),  0.125f);
    const float32x4_t pout = vmulq_n_f32(lanes_to_bands(pout1, pout2), 0.125f);
    const float32x4_t att = vdupq_n_f32(mb->lm_attack), rel = vdupq_n_f32(mb->lm_release);
    float32x4_t c = vbslq_f32(vcgtq_f32(pin, mb->lm_in), att, rel);
    mb->lm_in = flush_denormal_q(vmlaq_f32(pin, c, vsubq_f32(mb->lm_in, pin)));
    c = vbslq_f32(vcgtq_f32(pout, mb->lm_out), att, rel);
    mb->lm_out = flush_denormal_q(vmlaq_f32(pout, c, vsubq_f32(mb->lm_out, pout)));
    mb->lm_in_slow  = flush_denormal_q(vmlaq_n_f32(pin,  vsubq_f32(mb->lm_in_slow,  pin),  mb->lm_slow));
    mb->lm_out_slow = flush_denormal_q(vmlaq_n_f32(pout, vsubq_f32(mb->lm_out_slow, pout), mb->lm_slow));

    const float32x4_t floor = vdupq_n_f32(DRIVE_LEVEL_FLOOR);
    const uint32x4_t fast_ok = vandq_u32(vcgtq_f32(mb->lm_in, floor), vcgtq_f32(mb->lm_out, floor));
    const uint32x4_t slow_ok = vandq_u32(vcgtq_f32(mb->lm_in_slow, floor), vcgtq_f32(mb->lm_out_slow, floor));
    float32x4_t g = sqrt_ratio_q(mb->lm_in, mb->lm_out);
    g = vbslq_f32(slow_ok, vminq_f32(g, sqrt_ratio_q(mb->lm_in_slow, mb->lm_out_slow)), g);
    g = vmaxq_f32(vdupq_n_f32(DRIVE_LEVEL_GAIN_MIN), vminq_f32(vdupq_n_f32(DRIVE_LEVEL_GAIN_MAX), g));
    mb->lm_gain = vbslq_f32(fast_ok, g, mb->lm_gain);

    // Blend against the clean band, after matching, as the Overlord does.
    const float32x4_t wg1 = vmulq_n_f32(bands_to_lanes_lo(mb->lm_gain), mb->blend);
    const float32x4_t wg2 = vmulq_n_f32(bands_to_lanes_hi(mb->lm_gain), mb->blend);
    const float dry = 1.0f - mb->blend;
    for (int t = 0; t < NEON_LANES; ++t) {
        v1[t] = vmlaq_f32(vmulq_n_f32(v1[t], dry), w1[t], wg1);
        v2[t] = vmlaq_f32(vmulq_n_f32(v2[t], dry), w2[t], wg2);
    }
}

/**
 * One 4-sample block.  `sidechain` is the mono key; with use_sidechain the
 * band envelopes come from its own split instead of the audio, so the external
 * input keys each band through its own frequency range.
 */
fast_inline void multiband_process(multiband_t* mb,
                                   float32x4_t in_l, float32x4_t in_r,
                                   float32x4_t sidechain, bool use_sidechain,
                                   float32x4_t* out_l, float32x4_t* out_r) {
    if (mb->xover_low_freq != mb->designed_low || mb->xover_high_freq != mb->designed_high)
        multiband_design(mb);

    const float32x2_t zero2 = vdup_n_f32(0.0f);
    const float32x4x2_t lr = vzipq_f32(in_l, in_r);     // [L0 R0 L1 R1], [L2 R2 L3 R3]
    const float32x2_t frame[NEON_LANES] = {
        vget_low_f32(lr.val[0]), vget_high_f32(lr.val[0]),
        vget_low_f32(lr.val[1]), vget_high_f32(lr.val[1]) };
    float key[NEON_LANES];
    vst1q_f32(key, sidechain);

    // ---- split, and each band's instantaneous level ------------------------
    float32x4_t v1[NEON_LANES], v2[NEON_LANES], inst[NEON_LANES];
    for (int t = 0; t < NEON_LANES; ++t) {
        const float32x4_t x1 = vcombine_f32(frame[t], frame[t]);
        const float32x4_t y1 = bq4_tick(&mb->split_lo.s2, &mb->split_lo.c,
                                        bq4_tick(&mb->split_lo.s1, &mb->split_lo.c, x1));
        const float32x2_t rest = vget_high_f32(y1);
        const float32x4_t y2 = bq4_tick(&mb->split_hi.s2, &mb->split_hi.c,
                                        bq4_tick(&mb->split_hi.s1, &mb->split_hi.c,
                                                 vcombine_f32(rest, rest)));
        const float32x2_t low = bq2_tick(&mb->ap_s, &mb->ap_c, vget_low_f32(y1));
        v1[t] = vcombine_f32(low, vget_low_f32(y2));
        v2[t] = vcombine_f32(vget_high_f32(y2), zero2);

        if (use_sidechain) {
            const float32x2_t k1 = bq2_tick(&mb->sc_lo.s2, &mb->sc_lo.c,
                                            bq2_tick(&mb->sc_lo.s1, &mb->sc_lo.c, vld1_dup_f32(&key[t])));
            const float32x2_t k2 = bq2_tick(&mb->sc_hi.s2, &mb->sc_hi.c,
                                            bq2_tick(&mb->sc_hi.s1, &mb->sc_hi.c, vdup_lane_f32(k1, 1)));
            // k1 = [low, rest], k2 = [mid, high] -> [low mid high 0]
            inst[t] = vabsq_f32(vcombine_f32(vtrn_f32(k1, k2).val[0],
                                             vtrn_f32(k2, zero2).val[1]));
        } else {
            // max(|L|, |R|) per band
            const float32x4_t a1 = vabsq_f32(v1[t]);
            inst[t] = vcombine_f32(vpmax_f32(vget_low_f32(a1), vget_high_f32(a1)),
                                   vpmax_f32(vabs_f32(vget_high_f32(y2)), zero2));
        }
    }

    // ---- per-band gain: Standard's curve and ballistics, bands in lanes -----
    const float32x4_t pre_c  = vdupq_n_f32(mb->env_pre_coeff);
    const float32x4_t att    = vdupq_n_f32(mb->att_coeff);
    const float32x4_t rel    = vdupq_n_f32(mb->rel_coeff);
    const float32x4_t lo_lim = vdupq_n_f32(mb->atten_db);
    const float32x4_t hi_lim = vdupq_n_f32(mb->boost_db);
    const float32x4_t zero   = vdupq_n_f32(0.0f);
    float32x4_t env = mb->env, g_db = mb->gain_db;
    for (int t = 0; t < NEON_LANES; ++t) {
        env = vmaxq_f32(inst[t], vmulq_f32(env, pre_c));
        const float32x4_t env_db =
            vmulq_n_f32(neon_log2q_f32(vmaxq_f32(env, vdupq_n_f32(1e-5f))), 6.0206f);
        float32x4_t target = vmulq_f32(mb->slope, vsubq_f32(env_db, mb->thresh));
        target = vminq_f32(vmaxq_f32(target, lo_lim), hi_lim);
        // Attack while the level is rising, whichever way that moves the gain
        // -- see MasterFX::standard_process(), which shares the rule.
        const uint32x4_t rising = vcgtq_f32(vmulq_f32(vsubq_f32(target, g_db), mb->slope), zero);
        g_db = vmlaq_f32(target, vbslq_f32(rising, att, rel), vsubq_f32(g_db, target));
        const float32x4_t g = neon_expq_f32(vmulq_n_f32(g_db, INV_DB_COEFF));
        v1[t] = vmulq_f32(v1[t], bands_to_lanes_lo(g));
        v2[t] = vmulq_f32(v2[t], bands_to_lanes_hi(g));
    }
    mb->env     = flush_denormal_q(env);
    mb->gain_db = flush_denormal_q(g_db);

    // ---- drive ----------------------------------------------------------------
    if (mb->drive > 0.0f) multiband_drive(mb, v1, v2);
    else                  mb->drive_on = false;

    // ---- solo / mute, and the sum ------------------------------------------------
    float32x2_t o[NEON_LANES];
    for (int t = 0; t < NEON_LANES; ++t) {
        const float32x4_t s = vmlaq_f32(vmulq_f32(v1[t], mb->weight[0]), v2[t], mb->weight[1]);
        // [lowL+highL, lowR+highR, midL, midR] -> [L, R]
        o[t] = vadd_f32(vget_low_f32(s), vget_high_f32(s));
    }
    const float32x4x2_t u = vuzpq_f32(vcombine_f32(o[0], o[1]), vcombine_f32(o[2], o[3]));
    *out_l = u.val[0];
    *out_r = u.val[1];

#if !defined(__arm__)
    // On silence the filter states decay into the subnormal range.  ARMv7's
    // NEON runs flush-to-zero whatever FPSCR says, so on the drumlogue they
    // simply become zero and this would be wasted work; the x86 host bench
    // keeps them, at ~10x the cost (test_levels G9).
    bq4_state_t* st[4] = { &mb->split_lo.s1, &mb->split_lo.s2, &mb->split_hi.s1, &mb->split_hi.s2 };
    for (bq4_state_t* q : st) { q->z1 = flush_denormal_q(q->z1); q->z2 = flush_denormal_q(q->z2); }
    for (int k = 0; k < 2; ++k) mb->dc_y[k] = flush_denormal_q(mb->dc_y[k]);
    for (int i = 0; i < 2; ++i) {
        float* z = (i == 0) ? (float*)&mb->ap_s.z1 : (float*)&mb->ap_s.z2;
        z[0] = flush_denormal(z[0]); z[1] = flush_denormal(z[1]);
    }
#endif
}

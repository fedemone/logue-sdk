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
 * DRIVE gives each band its own shaper -- a fold on the lows, Standard's
 * Overlord tube on the mids, an exciter on the highs (see "Multiband's DRIVE"
 * in constants.h) -- each level-matched band by band, so DRIVE never moves
 * the balance.
 *
 * Layout.  Everything recursive -- the crossover, the detectors, the gain
 * smoothers -- runs one sample at a time with the bands (or band x channel)
 * in NEON lanes; the scalar code this replaces ran sixteen biquads per sample
 * one by one and cost 34k instructions per 64-frame render (this layout: 19k
 * at -Os, 18k at -O3).  The drive stage runs each band's shaper on that
 * band's own [L R] pair, 64-bit vectors, since no two bands share one.
 *
 *   crossover        [L R L R]       -> [lowL lowR | restL restR]   (split 1)
 *                    [restL restR x2] -> [midL midR | highL highR]   (split 2)
 *   band signals     v1 = [lowL lowR midL midR], v2 = [highL highR 0 0]
 *   band dynamics    [low mid high -]
 *   drive            [lowL lowR] fold, [midL midR] tube, [highL highR] exciter
 */

#include <arm_neon.h>
#include "constants.h"
#include "float_math.h"
#include "wavefolder.h"        // sine_folder_bounded(): the low band's fold
#include "distressor_mode.h"   // dist2_even_d(): the high band's exciter

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

    // ---- drive stage, each band's shaper on its own [L R] ------------------
    float32x2_t fold_env;        // low:  peak follower the fold is normalised to
    float32x2_t tube_bias;       // mid:  grid-current bias tracker
    float32x2_t dc_x, dc_y;      // mid:  DC blocker behind the tube
    float32x2_t exc_env;         // high: peak follower the exciter is normalised to
    bq2_state_t exc_hpf_s;       // high: high-pass on what Dist2 adds
    bq2_coeffs_t exc_hpf_c;
    float tube_idle;             // mid:  the tube's output with no signal
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
    float drive_g1, drive_g2;    // mid: the tube's two stages' gains
    float fold_depth, fold_inv_depth;      // low: F, and 1/F to hand the level back
    float exc_amount;            // high: how much of the curve, over its depth
    float fold_env_coeff, exc_env_coeff;   // the two peak followers' decay, per sample
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

/**
 * DRIVE, 0..1, into each band's shaper: the Overlord's law for the mid tube,
 * so a DRIVE value pushes it as hard as Standard's; the fold's depth and the
 * exciter's from their own laws (MB_FOLD_*, MB_EXCITE_* in constants.h).
 */
fast_inline void multiband_set_drive(multiband_t* mb, float drive) {
    mb->drive    = drive;
    mb->drive_g1 = 1.0f + drive * drive * TUBE_STAGE1_GAIN;
    mb->drive_g2 = 1.0f + drive * TUBE_STAGE2_GAIN;
    mb->fold_depth     = MB_FOLD_DEPTH_MIN + (MB_FOLD_DEPTH_MAX - MB_FOLD_DEPTH_MIN) * drive;
    mb->fold_inv_depth = 1.0f / mb->fold_depth;
    mb->exc_amount     = MB_EXCITE_AMOUNT_MAX * drive / MB_EXCITE_SHAPE;
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
    // The exciter's high-pass follows the high split, so it always sits an
    // octave under the band whatever Xover Hi says.
    const biquad_coeffs_t hp = rbj_butterworth(RBJ_HIGHPASS, hi * MB_EXCITE_HPF_RATIO, mb->sample_rate);
    mb->exc_hpf_c.b0 = vdup_n_f32(hp.b0); mb->exc_hpf_c.b1 = vdup_n_f32(hp.b1);
    mb->exc_hpf_c.b2 = vdup_n_f32(hp.b2); mb->exc_hpf_c.a1 = vdup_n_f32(hp.a1);
    mb->exc_hpf_c.a2 = vdup_n_f32(hp.a2);
    mb->designed_low  = lo;
    mb->designed_high = hi;
}

/** Put the drive stage where it sits with no signal: no DC step, no level memory. */
fast_inline void multiband_prime_drive(multiband_t* mb) {
    const float32x4_t zero = vdupq_n_f32(0.0f);
    const float32x2_t zero2 = vdup_n_f32(0.0f);
    mb->fold_env  = zero2;
    mb->exc_env   = zero2;
    mb->tube_bias = zero2;
    mb->dc_x = vdup_n_f32(mb->tube_idle);
    mb->dc_y = zero2;
    mb->exc_hpf_s.z1 = mb->exc_hpf_s.z2 = zero2;
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
    // The drive stage's [L R] pairs, two to a vector, asked directly like the
    // rest rather than left to show up at the output.
    m = vorrq_u32(m, nonfinite_mask_q(vcombine_f32(mb->fold_env, mb->exc_env)));
    m = vorrq_u32(m, nonfinite_mask_q(vcombine_f32(mb->tube_bias, mb->dc_x)));
    m = vorrq_u32(m, nonfinite_mask_q(vcombine_f32(mb->dc_y, mb->exc_hpf_s.z1)));
    m = vorrq_u32(m, nonfinite_mask_q(vcombine_f32(mb->exc_hpf_s.z2, mb->exc_hpf_s.z2)));
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
    mb->fold_env_coeff = ballistics_coeff(MB_FOLD_ENV_MS,   sample_rate);
    mb->exc_env_coeff  = ballistics_coeff(MB_EXCITE_ENV_MS, sample_rate);

    // What the mid tube settles on with no signal: stage 1 is offset to idle
    // at zero, so stage 2 sees only its own bias, b / sqrt(1 + shape_neg b^2).
    // The fold and the exciter idle at zero.
    mb->tube_idle = TUBE_STAGE2_BIAS /
        sqrtf(1.0f + MB_TUBE_SHAPE_NEG * TUBE_STAGE2_BIAS * TUBE_STAGE2_BIAS);

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

/** 1 / x from the NEON estimate and one Newton step (~16 bits), on two lanes. */
fast_inline float32x2_t recip_d(float32x2_t x) {
    const float32x2_t r = vrecpe_f32(x);
    return vmul_f32(vrecps_f32(x, r), r);
}

/**
 * A peak follower on one band's [L R], and the level its shaper is
 * normalised to.  The attack is instant, so the follower is never below |x|
 * and x / level stays within +/-1 -- which is what bounds the shaper's input
 * by its depth.  Below MB_SHAPER_FLOOR the level holds at the floor, so the
 * depth falls away with the signal.
 */
fast_inline float32x2_t band_level(float32x2_t* env, float32x2_t x, float decay) {
    *env = vmax_f32(vabs_f32(x), vmul_n_f32(*env, decay));
    return vmax_f32(*env, vdup_n_f32(MB_SHAPER_FLOOR));
}

/**
 * Low band: sine_folder()'s fold at depth F on the band divided by its level,
 * then scaled back by level / F.  Small signals come out unchanged (the fold
 * has unity slope at zero); the band's peaks reach F, past 1 they fold back.
 * Symmetric, so it leaves no DC and needs no blocker: the tube's 30 Hz one,
 * when this band had a tube, put 50 Hz 1.0-1.2 dB under 200 Hz even where
 * the tube was still linear.
 */
fast_inline float32x2_t band_fold(multiband_t* mb, float32x2_t x) {
    const float32x2_t level = band_level(&mb->fold_env, x, mb->fold_env_coeff);
    const float32x2_t z = vmul_f32(x, vmul_n_f32(recip_d(level), mb->fold_depth));
    return vmul_f32(sine_folder_bounded(z), vmul_n_f32(level, mb->fold_inv_depth));
}

/**
 * Mid band: the Overlord tube with Standard's drive law -- the rational
 * preamp (stage1_continuous_preamp) into the dynamic-bias triode (stage2_
 * pirkle_triode), phase restored.  `gk_pos` accumulates the triode's grid
 * current for the bias tracker.
 */
fast_inline float32x2_t band_tube(const multiband_t* mb, float32x2_t x, float32x2_t* gk_pos) {
    const float32x2_t one = vdup_n_f32(1.0f);
    // Stage 1: x g1 + b over 1 + |x g1 + b|, offset so it idles at zero
    const float32x2_t biased = vmla_n_f32(vdup_n_f32(TUBE_STAGE1_BIAS), x, mb->drive_g1);
    const float32x2_t pre = vsub_f32(vmul_f32(biased, recip_d(vadd_f32(one, vabs_f32(biased)))),
                                     vdup_n_f32(TUBE_STAGE1_BIAS / (1.0f + TUBE_STAGE1_BIAS)));
    // Stage 2: the triode, biased by its static point and its grid-current tracker
    const float32x2_t gk = vmla_n_f32(vadd_f32(vdup_n_f32(TUBE_STAGE2_BIAS), mb->tube_bias),
                                      pre, mb->drive_g2);
    *gk_pos = vadd_f32(*gk_pos, vmax_f32(gk, vdup_n_f32(0.0f)));
    const float32x2_t shape = vbsl_f32(vcgt_f32(gk, vdup_n_f32(0.0f)),
                                       vdup_n_f32(MB_TUBE_SHAPE_POS), vdup_n_f32(MB_TUBE_SHAPE_NEG));
    const float32x2_t den2 = vmla_f32(one, vmul_f32(gk, gk), shape);
    float32x2_t e = vrsqrte_f32(den2);
    e = vmul_f32(vrsqrts_f32(vmul_f32(e, e), den2), e);
    // The triode inverts and overlord_process inverts it back; both cancel here.
    return vmul_f32(gk, e);
}

/**
 * High band: the Distressor's Dist2 as an exciter.  The even half of its
 * curve, on the band divided by its level, scaled back by that level and by
 * DRIVE's amount, through the high-pass and onto the band.  The band itself
 * passes untouched, so the crossover still sums flat under it.
 */
fast_inline float32x2_t band_excite(multiband_t* mb, float32x2_t x) {
    const float32x2_t level = band_level(&mb->exc_env, x, mb->exc_env_coeff);
    const float32x2_t z = vmul_f32(x, vmul_n_f32(recip_d(level), MB_EXCITE_SHAPE));
    const float32x2_t added = vmul_f32(dist2_even_d(z), vmul_n_f32(level, mb->exc_amount));
    return vadd_f32(x, bq2_tick(&mb->exc_hpf_s, &mb->exc_hpf_c, added));
}

/**
 * The drive stage for one block: each band's shaper, then level matching per
 * band and the parallel blend.  v1/v2 are the compressed bands, per sample,
 * and are replaced by the driven ones.
 */
fast_inline void multiband_drive(multiband_t* mb, float32x4_t v1[NEON_LANES], float32x4_t v2[NEON_LANES]) {
    if (!mb->drive_on) { multiband_prime_drive(mb); mb->drive_on = true; }

    const float32x4_t zero = vdupq_n_f32(0.0f);
    const float32x2_t zero2 = vdup_n_f32(0.0f);
    float32x4_t w1[NEON_LANES], w2[NEON_LANES];
    float32x4_t pin1 = zero, pin2 = zero, pout1 = zero, pout2 = zero;
    float32x2_t gk = zero2;
    for (int t = 0; t < NEON_LANES; ++t) {
        const float32x2_t lo = band_fold(mb, vget_low_f32(v1[t]));
        const float32x2_t mi = band_tube(mb, vget_high_f32(v1[t]), &gk);
        const float32x2_t hi = band_excite(mb, vget_low_f32(v2[t]));
        // DC blocker behind the tube (the tube's own corner); the fold is
        // symmetric, and the exciter's high-pass already keeps DC out.
        const float32x2_t ym = vmla_n_f32(vsub_f32(mi, mb->dc_x), mb->dc_y, TUBE_DC_POLE);
        mb->dc_x = mi; mb->dc_y = ym;
        w1[t] = vcombine_f32(lo, ym);
        w2[t] = vcombine_f32(hi, zero2);
        pin1  = vmlaq_f32(pin1,  v1[t], v1[t]);
        pin2  = vmlaq_f32(pin2,  v2[t], v2[t]);
        pout1 = vmlaq_f32(pout1, w1[t], w1[t]);
        pout2 = vmlaq_f32(pout2, w2[t], w2[t]);
    }

    // The tube's grid-current bias tracker, once per block, as overlord_process does it
    mb->tube_bias = vmla_n_f32(mb->tube_bias,
        vsub_f32(vmul_n_f32(gk, -TUBE_GRID_BIAS * 0.25f), mb->tube_bias), TUBE_BIAS_ALPHA);
    mb->tube_bias = vget_low_f32(flush_denormal_q(vcombine_f32(mb->tube_bias, zero2)));

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
    // The drive stage's [L R] states, two to a vector
    float32x4_t f = flush_denormal_q(vcombine_f32(mb->fold_env, mb->exc_env));
    mb->fold_env = vget_low_f32(f); mb->exc_env = vget_high_f32(f);
    f = flush_denormal_q(vcombine_f32(mb->exc_hpf_s.z1, mb->exc_hpf_s.z2));
    mb->exc_hpf_s.z1 = vget_low_f32(f); mb->exc_hpf_s.z2 = vget_high_f32(f);
    mb->dc_y = vget_low_f32(flush_denormal_q(vcombine_f32(mb->dc_y, mb->dc_y)));
    for (int i = 0; i < 2; ++i) {
        float* z = (i == 0) ? (float*)&mb->ap_s.z1 : (float*)&mb->ap_s.z2;
        z[0] = flush_denormal(z[0]); z[1] = flush_denormal(z[1]);
    }
#endif
}

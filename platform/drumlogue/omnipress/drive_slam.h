#pragma once
/**
 * @file drive_slam.h
 * @brief The Distressor's high-drive ("slam") region.
 *
 * Why this exists
 * ---------------
 * Every shaper in OmniPress is bounded: soft clip, both harmonic saturators
 * and the tube all converge on +/-1, and the folders on their fold pattern.
 * The drive law feeding them was linear in the knob -- g = 1 + 19d for the
 * wavefolder, 1 + 39d for the harmonic saturators -- so the whole top half of
 * DRIVE was worth only ~4 dB of extra push.  On a -20 dBFS bus that left the
 * shapers barely into their knee: DRIVE 60 -> 100 moved Soft from 12.8% to
 * 16.9% THD, and what little did change arrived mostly as level (+17 dB of
 * fundamental across the knob), which the ear hears as "louder", not "dirtier".
 *
 * The slam region fixes that from DRIVE = 60 up, with three things a bounded
 * shaper needs if turning the knob further is going to keep meaning something:
 *
 *   1. Pre-gain that grows *geometrically* rather than linearly, so each click
 *      past the knee is worth a constant number of dB of push instead of a
 *      vanishing one.  That alone carries every saturating type from its knee
 *      into square-wave territory (~48% THD for a symmetric square).
 *
 *   2. A program-dependent bias ahead of the shaper.  Once a stage is fully
 *      saturated, more gain genuinely cannot change the waveform -- a square is
 *      a square.  What still can is moving the level the shaper clips *around*,
 *      which shifts the duty cycle and brings in the even harmonics: the
 *      hollow, nasal, octave-ish quality of a hard-biased fuzz.  The bias has
 *      to track the envelope to do that (a fixed offset is negligible next to a
 *      few hundred times gain), which also makes it breathe with the program,
 *      the way grid blocking does in the tube stage next door.
 *
 *   3. An output trim, because the point is to hear character rather than
 *      level.  Without it the slam region simply parks on the output limiter
 *      and every type collapses into the same clipped mush.
 *
 * Below the knee all three are inert -- gain 1.0, bias 0, trim 1.0 -- so
 * DRIVE 0..60 measures exactly as it did before, and Standard and Multiband,
 * which never arm the slam, are untouched.
 */

#include <arm_neon.h>
#include <math.h>

#include "constants.h"
#include "float_math.h"
#include "filters.h"

/* ---------------------------------------------------------------------------
 * Slam state
 * --------------------------------------------------------------------------- */

typedef struct {
    // Set once per parameter change.  The pre-gain itself is not here: each
    // stage folds drive_slam_gain() into the gain it already caches, so the
    // audio path multiplies by one number rather than two.
    float slam;        // 0 = disarmed, which is what the audio path branches on
    float bias_amt;    // bias ahead of the shaper, as a fraction of the envelope
    float trim;        // output trim

    // Audio-rate state
    float env;         // rectified envelope of the signal entering the drive
    float env_atk;     // per-block coefficients (one update per 4 samples)
    float env_rel;
    dc_blocker_state_t dc_l;
    dc_blocker_state_t dc_r;
} slam_t;

fast_inline void slam_init(slam_t* s, float sample_rate) {
    s->slam     = 0.0f;
    s->bias_amt = 0.0f;
    s->trim     = 1.0f;
    s->env      = 0.0f;

    // The bias is a capacitor voltage, not an audio signal: it is stepped once
    // per 4-sample block, exactly like the Overlord's grid-current tracker, so
    // the coefficients are computed at the block rate.
    const float block_rate = sample_rate * (1.0f / NEON_LANES);
    s->env_atk = 1.0f - expf(-1.0f / (DRIVE_SLAM_BIAS_ATK_MS * 0.001f * block_rate));
    s->env_rel = 1.0f - expf(-1.0f / (DRIVE_SLAM_BIAS_REL_MS * 0.001f * block_rate));

    dc_blocker_init(&s->dc_l);
    dc_blocker_init(&s->dc_r);
}

/**
 * How far into the slam region the knob is: 0 at and below the knee, 1 at
 * DRIVE = 100.
 */
fast_inline float drive_slam_amount(float drive) {
    if (drive <= DRIVE_SLAM_KNEE) return 0.0f;
    const float s = (drive - DRIVE_SLAM_KNEE) * (1.0f / (1.0f - DRIVE_SLAM_KNEE));
    return (s > 1.0f) ? 1.0f : s;
}

/**
 * Which voicing a DstrDist setting belongs to.  Takes the wavefolder's own
 * mode ids too, since DistressorMode numbers both in one enum.
 */
fast_inline int drive_slam_family(uint32_t dist_mode) {
    switch (dist_mode) {
        case DIST_MODE_CLEAN:     return SLAM_FAMILY_TUBE;
        case DRIVE_MODE_TRIANGLE:
        case DRIVE_MODE_SINE:     return SLAM_FAMILY_FOLD;
        default:                  return SLAM_FAMILY_SAT;
    }
}

/**
 * Extra pre-gain for a stage whose ceiling is gain_max times its pre-slam
 * value.  Geometric in the knob, so the push per click is constant in dB and
 * the knee is continuous (gain = 1 exactly at slam = 0).
 */
fast_inline float drive_slam_gain(float slam, float gain_max) {
    if (slam <= 0.0f || gain_max <= 1.0f) return 1.0f;
    return expf(slam * logf(gain_max));
}

/**
 * Arm the slam stage.  `slam` is drive_slam_amount() of the current knob, or 0
 * in any mode that does not have a slam region; `family` is which voicing the
 * selected shaper takes (see constants.h).
 */
fast_inline void slam_set(slam_t* s, float slam, int family) {
    s->slam = (slam > 0.0f) ? slam : 0.0f;
    if (slam <= 0.0f) {
        s->bias_amt = 0.0f;
        s->trim     = 1.0f;
        // Leaving the region has to clear the blocker, or the next entry
        // resumes its delay line from however long ago the slam was last
        // active and lands as a click -- the same trap the shelving filter's
        // flat-bypass fell into.
        s->env = 0.0f;
        dc_blocker_init(&s->dc_l);
        dc_blocker_init(&s->dc_r);
        return;
    }
    const slam_voicing_t* v = &drive_slam_voicing[family];
    s->bias_amt = DRIVE_SLAM_BIAS * v->bias_scale * slam;
    s->trim     = 1.0f + slam * (v->trim - 1.0f);
}

/**
 * Pre-shaper half: add the program-dependent bias.
 *
 * Kept as a fraction of the envelope rather than an absolute offset because
 * the shaper's own pre-gain is already in the hundreds by the top of the knob;
 * an absolute offset would move the clipping point by a part in a thousand of
 * the waveform and do nothing at all.  DRIVE_SLAM_BIAS caps the offset well
 * inside the peak so the waveform always still crosses the clipping point --
 * bias the whole waveform past it and the output is a constant the DC blocker
 * then removes, i.e. silence.
 */
fast_inline void slam_bias(slam_t* s, float32x4_t* l, float32x4_t* r) {
    if (s->slam <= 0.0f) return;

    const float rect = 0.5f * (vmeanq_f32(vabsq_f32(*l)) + vmeanq_f32(vabsq_f32(*r)));
    const float c = (rect > s->env) ? s->env_atk : s->env_rel;
    s->env = flush_denormal(s->env + c * (rect - s->env));

    const float32x4_t bias = vdupq_n_f32(s->bias_amt * s->env);
    *l = vaddq_f32(*l, bias);
    *r = vaddq_f32(*r, bias);
}

/**
 * Post-shaper half: remove the DC the biased clipping left behind, then trim.
 * Identity when the slam is disarmed.
 */
fast_inline void slam_output(slam_t* s, float32x4_t* l, float32x4_t* r) {
    if (s->slam <= 0.0f) return;

    *l = dc_block_process(&s->dc_l, *l, DRIVE_SLAM_DC_POLE);
    *r = dc_block_process(&s->dc_r, *r, DRIVE_SLAM_DC_POLE);

    const float32x4_t trim = vdupq_n_f32(s->trim);
    *l = vmulq_f32(*l, trim);
    *r = vmulq_f32(*r, trim);
}

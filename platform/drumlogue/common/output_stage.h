#pragma once
/**
 * @file output_stage.h
 * @brief Calibrated output stage for drumlogue user units.
 *
 * Why this exists
 * ---------------
 * A user unit is mixed into the same bus as the drumlogue's internal engines,
 * whose samples are already peak-limited and therefore carry a lot of RMS for
 * their peak level.  A unit that peaks at -0.1 dBFS but only averages -14 LUFS
 * sounds correct on its own and vanishes inside a pattern: what places a part
 * in a mix is its loudness, not its peak.  That is why a level problem shows up
 * on hardware and not while auditioning a render.
 *
 * Once the peaks are at the ceiling, a plain multiplier cannot fix that.  It
 * only moves the clipping point down, and a hard clip is the worst possible
 * trade: it flat-tops the transient (the part that carries the attack) while
 * adding nothing to the sustain.
 *
 * This stage raises loudness under a fixed peak ceiling instead:
 *
 *     trim (dB)  ->  soft knee  ->  bounded by `ceil` for every finite input
 *
 * The knee is exactly unity below `thr`, so quiet material is untouched, and
 * asymptotes to `ceil` above it, so `trim` is spent on RMS rather than on
 * clipping.  It is a memoryless waveshaper, not a gain-riding limiter, so it
 * cannot duck the body of a sound behind its own transient.
 *
 * Transfer function, for a = |x| and over = max(0, a - thr), span = ceil - thr:
 *
 *     y = sign(x) * ( min(a, thr) + span * over / (over + span) )
 *
 * Continuous and C1 at the knee (slope is 1 on both sides), strictly monotone,
 * and strictly below `ceil` for every finite input, so the result never needs a
 * clamp behind it and no sample is ever flat-topped.
 *
 * Calibration
 * -----------
 * Pick `trim_db` with platform/drumlogue/tools/level_meter, which reports gated
 * BS.1770 loudness per preset.  The suggested target for a drumlogue user unit
 * is about -9 LUFS for one hit at velocity 127, which is where the units that
 * sit correctly in a pattern measure.  Do NOT normalise per preset or per
 * instrument: the level differences between a kick and a triangle are musical,
 * and only the unit as a whole should be trimmed.
 */

#include <cmath>
#include <cstddef>

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define DL_OUTPUT_STAGE_NEON 1
#endif

namespace dl {

/** Default knee: transparent below -3.1 dBFS, asymptotic to -0.04 dBFS. */
static constexpr float kOutThrDefault = 0.70f;
static constexpr float kOutCeilDefault = 0.995f;

static inline float db_to_lin(float db) { return powf(10.0f, db * (1.0f / 20.0f)); }

/** Scalar soft knee. `thr` < `ceil`, both positive. */
static inline float soft_knee(float x, float thr = kOutThrDefault, float ceil = kOutCeilDefault) {
  const float span = ceil - thr;
  const float a = fabsf(x);
  if (a <= thr) return x;
  const float over = a - thr;
  const float y = thr + span * over / (over + span);
  return (x < 0.0f) ? -y : y;
}

#if defined(DL_OUTPUT_STAGE_NEON)
/** Newton-refined reciprocal; the denominator here is always >= span > 0. */
static inline float32x4_t recip_q(float32x4_t x) {
  float32x4_t r = vrecpeq_f32(x);
  r = vmulq_f32(vrecpsq_f32(x, r), r);
  r = vmulq_f32(vrecpsq_f32(x, r), r);
  return r;
}

/** Vector soft knee, same transfer function as soft_knee(). */
static inline float32x4_t soft_knee_q(float32x4_t x, float thr = kOutThrDefault,
                                      float ceil = kOutCeilDefault) {
  const float span = ceil - thr;
  const float32x4_t v_thr = vdupq_n_f32(thr);
  const float32x4_t v_span = vdupq_n_f32(span);
  const float32x4_t a = vabsq_f32(x);
  const float32x4_t over = vmaxq_f32(vsubq_f32(a, v_thr), vdupq_n_f32(0.0f));
  const float32x4_t mag = vaddq_f32(vminq_f32(a, v_thr),
                                    vmulq_f32(vmulq_f32(over, v_span),
                                              recip_q(vaddq_f32(over, v_span))));
  // Take the sign bit from x and the magnitude from `mag`.
  return vbslq_f32(vdupq_n_u32(0x80000000u), x, mag);
}
#endif

/**
 * Trim + soft knee for one unit.  Hold one of these as a member and call
 * process() as the last thing a Render()/Process() does.
 */
class OutputStage {
 public:
  explicit OutputStage(float trim_db = 0.0f, float thr = kOutThrDefault,
                       float ceil = kOutCeilDefault)
      : trim_(db_to_lin(trim_db)), thr_(thr), ceil_(ceil) {}

  void setTrimDb(float trim_db) { trim_ = db_to_lin(trim_db); }
  float trim() const { return trim_; }

  /** One sample. */
  inline float process(float x) const { return soft_knee(x * trim_, thr_, ceil_); }

#if defined(DL_OUTPUT_STAGE_NEON)
  /** Four samples. */
  inline float32x4_t processq(float32x4_t x) const {
    return soft_knee_q(vmulq_n_f32(x, trim_), thr_, ceil_);
  }
#endif

  /** In-place over an interleaved stereo buffer of `frames` frames. */
  inline void processStereo(float* __restrict buf, size_t frames) const {
    const size_t n = frames << 1;
    size_t i = 0;
#if defined(DL_OUTPUT_STAGE_NEON)
    for (; i + 4 <= n; i += 4) vst1q_f32(&buf[i], processq(vld1q_f32(&buf[i])));
#endif
    for (; i < n; ++i) buf[i] = process(buf[i]);
  }

 private:
  float trim_;
  float thr_;
  float ceil_;
};

}  // namespace dl

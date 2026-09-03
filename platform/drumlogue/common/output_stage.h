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


/**
 * Look-ahead peak limiter for a stereo bus.
 *
 * Why this exists next to the knee above
 * --------------------------------------
 * `soft_knee` is a memoryless waveshaper.  That is the right tool for a peak:
 * it acts on the handful of samples that overshoot and it cannot duck the body
 * of a sound behind its own transient.  It is the wrong tool for *sustained*
 * overdrive, and a polyphonic unit produces exactly that -- every extra voice
 * multiplies the bus level, so the shaper ends up bending the whole waveform
 * instead of its tips.
 *
 * How bad that is depends entirely on the material.  Waveshaping one dominant
 * low partial (a kick) manufactures harmonics of it and reads as "fat".
 * Waveshaping a dense inharmonic spectrum (a cymbal, a gong, several voices at
 * once) manufactures every intermodulation product between every pair of
 * partials, which is broadband, atonal, and reads as harsh.  Measured on
 * EffeESP32 before this stage existed, against its own linear voice mix: one
 * Splash hit came out with its distortion 9.9 dB below the signal and four
 * stacked hits 6.2 dB below it.  With this stage in front of the knee the same
 * renders measure -38.8 and -37.6 dB.  See platform/drumlogue/tools/stack_meter.
 *
 * A limiter fixes that by riding a *gain* instead of bending the samples: the
 * spectrum is untouched, only the level moves.  This one is feed-forward with a
 * fixed look-ahead, so the gain has already come down by the time the peak that
 * caused it is emitted, and there is no overshoot to shape:
 *
 *     bus -> [ look-ahead limiter ] -> [ soft knee ] -> out
 *
 * Put the knee behind it, with its threshold at or above this stage's ceiling.
 * The limiter then does the work and the knee only catches what leaks through.
 *
 * Cost: `kLookahead` samples of latency (0.67 ms at 48 kHz) and about ten
 * scalar flops per sample.  The detector is stereo-linked (it follows
 * max(|L|,|R|)) so the stereo image cannot shift while it acts, and the whole
 * stage is exactly transparent -- gain stays at 1.0 -- while the bus stays
 * under `ceiling`, so quiet material is untouched.
 *
 * Real-time safe: fixed-size state, no allocation, no branches on data beyond a
 * pair of selects.
 */
class PeakLimiter {
 public:
  /** 32 samples = 0.67 ms at 48 kHz. */
  static constexpr int kLookahead = 32;

  /**
   * @param sample_rate  host rate, Hz.
   * @param ceiling      level the bus is held at, in the domain this stage sees
   *                     (i.e. after whatever master gain precedes it).
   * @param release_s    how fast gain returns.  Keep this longer than the
   *                     period of the lowest content the unit produces -- a
   *                     follower faster than that modulates the waveform and
   *                     becomes a distortion generator itself.  A 32 Hz kick
   *                     needs at least ~31 ms; 120 ms leaves margin.
   */
  void init(float sample_rate, float ceiling = 0.9f, float release_s = 0.12f) {
    ceiling_ = ceiling;
    // The gain must reach its target within the look-ahead window, so that the
    // peak which asked for it is already scaled when it leaves the delay line.
    // tau = kLookahead/4 samples puts it within 2% after a full window.
    atk_ = 1.0f - expf(-4.0f / (float)kLookahead);
    rel_ = 1.0f - expf(-1.0f / (release_s * sample_rate));
    reset();
  }

  void reset() {
    for (int i = 0; i < kLookahead * 2; ++i) delay_[i] = 0.0f;
    widx_ = 0;
    env_ = 0.0f;
    gain_ = 1.0f;
  }

  /** Largest gain reduction currently applied, as a linear factor (1 = none). */
  float gain() const { return gain_; }

  /** In-place over an interleaved stereo buffer of `frames` frames. */
  inline void processStereo(float* __restrict buf, size_t frames) {
    float env = env_, gain = gain_;
    const float ceiling = ceiling_, atk = atk_, rel = rel_;
    int w = widx_;
    for (size_t i = 0; i < frames; ++i) {
      const float l = buf[2 * i], r = buf[2 * i + 1];
      const float a = fmaxf(fabsf(l), fabsf(r));
      // Peak follower: instant attack, exponential release.
      env = (a > env) ? a : env + (a - env) * rel;
      if (env < 1e-20f) env = 0.0f;  // keep the tail out of denormal territory
      const float target = (env > ceiling) ? ceiling / env : 1.0f;
      // Down fast (inside the look-ahead window), up slowly.
      gain += (target - gain) * ((target < gain) ? atk : rel);
      // Emit the delayed sample under the gain that its own peak asked for.
      buf[2 * i] = delay_[2 * w] * gain;
      buf[2 * i + 1] = delay_[2 * w + 1] * gain;
      delay_[2 * w] = l;
      delay_[2 * w + 1] = r;
      w = (w + 1 == kLookahead) ? 0 : w + 1;
    }
    env_ = env;
    gain_ = gain;
    widx_ = w;
  }

 private:
  float delay_[kLookahead * 2] = {0.0f};
  int widx_ = 0;
  float env_ = 0.0f;
  float gain_ = 1.0f;
  float ceiling_ = 0.9f;
  float atk_ = 0.1f;
  float rel_ = 1e-4f;
};

}  // namespace dl

#pragma once
/**
 * @file clm.h
 * @brief Common Lisp Music primitives, translated for the drumlogue.
 *
 * Percussio is a translation of Stephen Dill's 1998 CCRMA Music 220a project
 * "Percussion Synthesis" (https://ccrma.stanford.edu/~sdill/220A-project/drums.html)
 * into a drumlogue user synth unit.  Everything the project's five instrument
 * families are built from is a CLM generator, so this header holds those
 * generators and nothing else; the engines in voice.h are then a fairly
 * mechanical transcription of the .ins sources the page documents.
 *
 * Each generator below names the CLM function it stands in for and states any
 * deviation.  The deviations are all listed in README.md as well; if you change
 * one here, change it there.
 */

#include <cmath>
#include <cstdint>

#include "attributes.h"
#include "float_math.h"

namespace clm {

/**
 * Anything below this is flushed out of a feedback path that can decay on its
 * own.  A user unit is a shared object, so it never gets crtfastmath.o's FPSCR
 * setup, and whether a denormal then costs nothing or costs support-code cycles
 * is not ours to know.
 *
 * Only the Karplus-Strong recursion needs it.  OnePole and TwoPole are driven
 * by randh here and nothing else, and randh is uniform on [-1, 1) every sample,
 * so their state never decays anywhere near this -- and the check is not free:
 * on the two-pole it cost 30% of an already tight loop.
 */
static constexpr float kDenormal = 1.0e-25f;

/** Integer clamp; float_math.h only ships the float ones. */
static inline int32_t clampi(int32_t lo, int32_t x, int32_t hi) {
  return (x < lo) ? lo : ((x > hi) ? hi : x);
}

/*===========================================================================*/
/* rand / randh                                                              */
/*===========================================================================*/

/**
 * Xorshift32.  The only randomness in the original is CLM's `randh` and the
 * Karplus-Strong sign draw, and neither needs more than this.
 */
struct Rng {
  uint32_t s = 2463534242UL;

  inline void seed(uint32_t v) { s = v ? v : 2463534242UL; }

  fast_inline uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }

  /** Uniform on [0, 1). */
  fast_inline float next01() { return next() * (1.0f / 4294967296.0f); }

  /** Uniform on [-1, 1). */
  fast_inline float nextBi() { return (int32_t)next() * (1.0f / 2147483648.0f); }
};

/**
 * CLM `randh`: a new uniform random value, held until the next tick.
 *
 * Every subtractive instrument on the page starts from
 * `(make-randh :frequency (* .49 *srate*))`, and add-partials' noise floor and
 * add-noisy-freqs use randh too.  At 0.49 * fs a value is held for two samples
 * more often than one, which puts a gentle sinc tilt on the noise -- that tilt
 * is part of the sound, so the rate is a real parameter here, not a shortcut
 * for white noise.
 *
 * DEVIATION: the page says the amplitudes are ~U[0,1]; CLM's randh is bipolar
 * (~U[-amp,+amp]) and that is what is implemented.  A unipolar source would put
 * a large DC step into the one-pole and two-pole filters, which the drumlogue's
 * output stage would then have to spend headroom removing.
 */
struct Randh {
  float phase = 1.0f;  // [0,1), wraps once per held value
  float inc = 0.49f;   // frequency / samplerate
  float value = 0.0f;
  Rng rng;

  inline void init(uint32_t seed, float rate_norm) {
    phase = 1.0f;  // force a draw on the first tick
    inc = rate_norm;
    value = 0.0f;
    rng.seed(seed);
  }

  inline void setRate(float rate_norm) { inc = rate_norm; }

  fast_inline float process() {
    phase += inc;
    if (phase >= 1.0f) {
      phase -= 1.0f;
      value = rng.nextBi();
    }
    return value;
  }
};

/*===========================================================================*/
/* env                                                                       */
/*===========================================================================*/

/** Largest breakpoint count any envelope on the page needs. */
enum { kMaxEnvPoints = 4 };

/**
 * CLM `env`, including `:base`.
 *
 * CLM interpolates a segment from y0 to y1 over normalised u in [0,1] as
 *
 *     y = y0 + (y1 - y0) * (base^u - 1) / (base - 1)
 *
 * so base = 1 is linear, base -> 0 is a step to y1, base < 1 reaches the target
 * early (a decay that drops fast and tails), and base > 1 reaches it late (a
 * decay that holds and then falls off a cliff).  The page's noise instruments
 * default to `:amp-env-base 10` and go as high as 1000000, which is why a
 * brushed snare there is a sizzle that stops rather than a hit that decays.
 *
 * Evaluated incrementally the way CLM does it: base^u is advanced by one
 * multiply per sample, so the transcendentals are paid once per segment
 * (at most three times per note) and never in the sample loop.
 */
struct Env {
  float x[kMaxEnvPoints];  // breakpoint positions, normalised to [0,1] of the duration
  float y[kMaxEnvPoints];  // breakpoint values
  uint8_t n;               // number of breakpoints in use
  uint8_t seg;             // current segment index
  float base;

  float offset, scaler, power, rate;  // y = offset + scaler * power; power *= rate
  float value;
  uint32_t seg_end;    // sample index at which the current segment ends
  uint32_t seg_start;  // sample index at which it began
  uint32_t total;      // duration in samples

  /** Set the breakpoint list.  Positions are fractions of the total duration. */
  inline void setPoints(const float* xs, const float* ys, uint8_t count, float b) {
    n = (count > (uint8_t)kMaxEnvPoints) ? (uint8_t)kMaxEnvPoints : count;
    for (uint8_t i = 0; i < n; ++i) {
      x[i] = xs[i];
      y[i] = ys[i];
    }
    base = b;
  }

  /** Start the envelope over `duration_samples`. */
  inline void trigger(uint32_t duration_samples) {
    total = duration_samples ? duration_samples : 1;
    seg = 0;
    seg_start = 0;
    value = (n > 0) ? y[0] : 0.0f;
    beginSegment();
  }

  /**
   * Install the incremental coefficients for the segment starting at `seg`.
   * Zero-length segments are skipped, so a shape whose breakpoints collapse
   * (attack == hold, or hold == 100%) costs nothing at run time.
   */
  inline void beginSegment() {
    while (seg + 1 < n) {
      const uint32_t end = (uint32_t)(x[seg + 1] * (float)total + 0.5f);
      if (end > seg_start) {
        seg_end = end;
        const float y0 = y[seg];
        const float y1 = y[seg + 1];
        const uint32_t len = seg_end - seg_start;
        if (base == 1.0f || y0 == y1) {
          offset = y0;
          scaler = (y1 - y0);
          power = 0.0f;  // used as a plain ramp position for the linear case
          rate = 1.0f / (float)len;
        } else if (base <= 0.0f) {
          offset = y1;  // CLM: base 0 is a step to the target
          scaler = 0.0f;
          power = 0.0f;
          rate = 0.0f;
        } else {
          const float d = 1.0f / (base - 1.0f);
          offset = y0 - (y1 - y0) * d;
          scaler = (y1 - y0) * d;
          power = 1.0f;                            // base^0
          rate = expf(logf(base) / (float)len);    // base^(1/len)
        }
        value = y0;
        return;
      }
      // Zero-length: jump straight to the next breakpoint.
      ++seg;
      value = y[seg];
    }
    // Past the last breakpoint: hold the final value.
    seg_end = 0xFFFFFFFFu;
    offset = value;
    scaler = 0.0f;
    power = 0.0f;
    rate = 0.0f;
  }

  /** One sample.  `pos` is the sample index since trigger(). */
  fast_inline float process(uint32_t pos) {
    if (pos >= seg_end) {
      ++seg;
      seg_start = seg_end;
      value = y[(seg < n) ? seg : (uint8_t)(n - 1)];
      beginSegment();
    }
    if (rate == 0.0f) {
      value = offset;
    } else if (power == 0.0f) {
      // Linear segment: offset + scaler * (pos - seg_start) / len
      value = offset + scaler * (float)(pos - seg_start) * rate;
    } else {
      value = offset + scaler * power;
      power *= rate;
    }
    return value;
  }
};

/*===========================================================================*/
/* Filters                                                                   */
/*===========================================================================*/

/**
 * CLM `one-pole`: y[n] = a0 * x[n] - b1 * y[n-1].
 *
 * The page's subtract-op relies on CLM's sign convention: b1 < 0 low-passes,
 * b1 > 0 high-passes, and the filter gets stronger as |b1| grows.
 */
struct OnePole {
  float a0 = 1.0f, b1 = 0.0f, y1 = 0.0f;

  inline void clear() { y1 = 0.0f; }
  inline void set(float a0_, float b1_) {
    a0 = a0_;
    b1 = clipminmaxf(-0.999f, b1_, 0.999f);
  }
  fast_inline float process(float x) {
    y1 = a0 * x - b1 * y1;
    return y1;
  }
};

/**
 * CLM `one-zero`: y[n] = a0 * x[n] + a1 * x[n-1].
 *
 * subtract-oz: a1 > 0 averages (low frequencies), a1 < 0 differences (high
 * frequencies).  Unconditionally stable, so a1 is not clamped.
 */
struct OneZero {
  float a0 = 1.0f, a1 = 0.0f, x1 = 0.0f;

  inline void clear() { x1 = 0.0f; }
  inline void set(float a0_, float a1_) {
    a0 = a0_;
    a1 = a1_;
  }
  fast_inline float process(float x) {
    const float y = a0 * x + a1 * x1;
    x1 = x;
    return y;
  }
};

/**
 * A one-pole tone control, for the engines that do not already spend `Coef` on
 * a filter of their own.
 *
 *   low-pass    y = (1-b) x + b y1                unity at DC
 *   high-pass   y = a0 (x - x1) + b y1, a0=(1+b)/2, unity at Nyquist
 *
 * both with b = e^(-2 pi fc / fs), so the knob moves a corner rather than a
 * coefficient.  CLM's one-pole is the low-pass here with a0 left at 1, which
 * is why subtract-op needs an amplitude of .4 against a b1 of 0.9; a tone
 * control that changes the level as you turn it is not a tone control, and
 * unlike subtract-op there is no published amplitude to be faithful to.
 *
 * The obvious alternative -- take `Coef` straight as the pole and normalise
 * whichever end of the band the pole leaves alone -- is worse than it looks on
 * the high-pass side: a pole at -0.95 normalised at Nyquist puts a bell at
 * 1.4 kHz 32 dB down, because the whole audible band is on the stopband side
 * of a 6 dB/octave rise that only reaches unity at 24 kHz.
 */
struct Tone {
  float a0 = 1.0f, a1 = 0.0f, b1 = 0.0f, x1 = 0.0f, y1 = 0.0f;

  inline void clear() { x1 = 0.0f; y1 = 0.0f; }
  inline void set(float fc, float fs, bool hp) {
    const float b = expf(-2.0f * M_PI * clipminmaxf(1.0f, fc, 0.45f * fs) / fs);
    b1 = b;
    if (hp) {
      a0 = 0.5f * (1.0f + b);
      a1 = -a0;
    } else {
      a0 = 1.0f - b;
      a1 = 0.0f;
    }
  }
  fast_inline float process(float x) {
    float y = a0 * x + a1 * x1 + b1 * y1;
    // The granular engine really is silent between grains, so this recursion
    // can be left to decay into denormals with nothing to drive it out.  See
    // kDenormal: a .so never gets crtfastmath.o's flush-to-zero setup.
    if (si_fabsf(y) < kDenormal) y = 0.0f;
    x1 = x;
    y1 = y;
    return y;
  }
};

/**
 * CLM `two-pole` / `ppolar`: a resonator with poles at r * e^(+-j*theta),
 *
 *     y[n] = g * x[n] + 2 r cos(theta) y[n-1] - r^2 y[n-2]
 *
 * used by subtract-pp (band-limited noise) and by add-noise (tuned noise
 * oscillators, r = 0.99).
 *
 * DEVIATION: g normalises the filter, which CLM does not do.  Without it the
 * gain runs from about 1 at r = 0.2 to about 4*10^5 at r = 0.9999, and the page
 * compensates by hand in every call -- amplitude .4 for a one-pole against
 * .0004 for the blown bottle.  The page even flags this as a defect of
 * add-noise ("it is impossible to control the peak amplitude, so it doesn't
 * work with real sounds").
 *
 * The normalisation is for unit output RMS under white noise, not unit peak
 * gain, because noise is the only thing this resonator is ever fed here --
 * randh in subtract-pp, randh again in add-noise.  Peak normalisation would
 * leave Reso as a volume control: a narrow band passes proportionally less
 * noise power, which is a 25 dB drop between r = 0.2 and r = 0.9999 and is
 * exactly the spread the page was compensating for by hand.  Under RMS
 * normalisation Reso changes timbre and leaves level alone, and in add-noise
 * each tuned-noise partial contributes in proportion to its own amplitude,
 * which is what a partial amplitude is supposed to mean.
 *
 * The energy of the un-normalised impulse response has a closed form,
 *
 *     sum |h[n]|^2 = (1 + r^2) / ((1 - r^2) * A * B)
 *     A = (1 - r)^2 + 4 r sin^2(theta/2)
 *     B = (1 - r)^2 + 4 r cos^2(theta/2)
 *
 * so g is its reciprocal square root.  A and B are the factored form of
 * (1 + r^2)^2 - 4 r^2 cos^2(theta); written that way they cancel catastrophically
 * in float32 near r = 1 (two ~4.0 quantities differing by 3e-5), and the factors
 * above do not.
 */
struct TwoPole {
  float g = 1.0f, c1 = 0.0f, c2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

  inline void clear() {
    y1 = 0.0f;
    y2 = 0.0f;
  }

  /** theta in radians/sample, radius r in [0, 1). */
  inline void set(float theta, float r) {
    r = clipminmaxf(0.0f, r, 0.99995f);
    theta = clipminmaxf(1.0e-5f, theta, M_PI - 1.0e-5f);
    c1 = 2.0f * r * cosf(theta);
    c2 = -r * r;

    const float h = 0.5f * theta;
    const float sh = sinf(h);
    const float ch = cosf(h);
    const float om = (1.0f - r) * (1.0f - r);
    const float A = om + 4.0f * r * sh * sh;
    const float B = om + 4.0f * r * ch * ch;
    const float energy = (1.0f + r * r) / ((1.0f - r * r) * A * B);
    g = (energy > 1.0e-12f) ? (1.0f / sqrtf(energy)) : 1.0f;
  }

  fast_inline float process(float x) {
    const float y = g * x + c1 * y1 + c2 * y2;
    y2 = y1;
    y1 = y;
    return y;
  }
};

/*===========================================================================*/
/* oscil                                                                     */
/*===========================================================================*/

/**
 * CLM `oscil`, for the additive banks.
 *
 * Magic-circle (modified coupled form) recursion:
 *
 *     x -= eps * y ;  y += eps * x        with eps = 2 sin(w/2)
 *
 * The state matrix has determinant 1, so the orbit is a closed ellipse and the
 * amplitude cannot drift the way the direct-form y[n] = 2cos(w) y[n-1] - y[n-2]
 * resonator does over the two-second decays the bells need.  It costs two
 * multiplies per partial per sample, so a 28-partial gong bank is affordable.
 *
 * Initialising x = cos(w/2), y = 0 makes the y output exactly sin(w*n): the
 * ellipse's y semi-axis is 1/cos(w/2), which is what that initial value cancels.
 * All partials therefore start at zero and in phase, as CLM's oscil does.
 */
struct Oscil {
  float x = 1.0f, y = 0.0f, eps = 0.0f;

  /** w in radians/sample. */
  inline void set(float w) {
    w = clipminmaxf(1.0e-6f, w, M_PI * 0.999f);
    const float h = 0.5f * w;
    eps = 2.0f * sinf(h);
    x = cosf(h);
    y = 0.0f;
  }

  /**
   * Retune without restarting.  `set` reseeds the state to a known phase, which
   * is what a note-on wants and exactly what a pitch envelope must not do; this
   * moves only the rotation angle and leaves the oscillator where it is.
   *
   * The coupled form conserves x^2 + y^2 - eps*x*y, so changing eps in place
   * moves the amplitude a little.  The bound is |d(eps)|/4 per step and the
   * steps are small, which is well under the level the pitch envelope is
   * moving things by anyway; renormalising would cost a square root per
   * partial per step.
   */
  inline void setFreq(float w) {
    w = clipminmaxf(1.0e-6f, w, M_PI * 0.999f);
    eps = 2.0f * sinf(0.5f * w);
  }

  fast_inline float process() {
    x -= eps * y;
    y += eps * x;
    return y;
  }
};

/**
 * A phase-accumulator sine, for FM where the increment is modulated per sample
 * and a recursion would have to be retuned every sample.  Phase is normalised
 * to [0,1) so an arbitrarily large modulation excursion still wraps correctly.
 */
struct Phasor {
  float phase = 0.0f;

  inline void clear() { phase = 0.0f; }

  /**
   * Advance and wrap to [0, 1).
   *
   * The increment is clamped first, which is what keeps si_floorf() inside the
   * signed-32-bit domain its own docstring restricts it to.  It needs the room:
   * true FM displaces the carrier's increment by index * inc_mod, so at index
   * 10 and ratio 16 the instantaneous increment is 160x the carrier's and
   * routinely negative.
   *
   * HISTORY: si_floorf() used to be (float)((uint32_t)x) in every drumlogue
   * copy of float_math.h, which is undefined for negative input -- it returned
   * 2^32 -- so every one of those samples came back NaN.  The fix restored
   * KORG's own implementation from the prologue/minilogue-xd/NTS-1 copies,
   * which was correct all along; test_dsp.cpp checks it against libm so this
   * unit cannot be built against a copy that has drifted back.
   */
  fast_inline float advance(float inc) {
    float p = phase + clipminmaxf(-1.0e6f, inc, 1.0e6f);
    p -= si_floorf(p);
    if (p >= 1.0f) p = 0.0f;  // rounding can land exactly on 1
    phase = p;
    return p;
  }

  /** sin(2*pi*phase), via the Mineiro approximation valid on [-pi, pi]. */
  static fast_inline float sine(float ph) {
    return -fastsinf((ph - 0.5f) * M_TWOPI);
  }
};

}  // namespace clm

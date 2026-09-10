/**
 * @file test_dsp.cpp
 * @brief Host tests for Percussio.
 *
 *   g++ -std=gnu++14 -O2 -I. -I../common -U__ARM_NEON__ -U__ARM_NEON \
 *       -Wno-strict-aliasing test_dsp.cpp header.c -o /tmp/percussio_test
 *   /tmp/percussio_test
 *
 * Two of these check things that can only bite on hardware and are silent on a
 * render: that the header's parameter defaults equal preset 0, and that every
 * value any preset ships is inside the range the header declares.  Both are
 * read out of the real `unit_header`, so they cannot drift from what ships.
 */

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>

#include "synth.h"
#include "unit.h"

extern "C" {
extern const unit_header_t unit_header;
}

static int g_fail = 0;

static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char* fmt, ...) {
  if (ok) return;
  va_list ap;
  va_start(ap, fmt);
  std::printf("  FAIL: ");
  std::vprintf(fmt, ap);
  std::printf("\n");
  va_end(ap);
  ++g_fail;
}

static void banner(const char* s) { std::printf("\n== %s ==\n", s); }

/*===========================================================================*/

static void test_header_matches_preset0() {
  banner("header defaults == preset 0, and ranges cover every preset");

  check(unit_header.num_params == pcs::k_num_params, "num_params %u != %d",
        (unsigned)unit_header.num_params, (int)pcs::k_num_params);
  check((int)unit_header.num_presets == pcs::kNumPresets, "num_presets %u != %d",
        (unsigned)unit_header.num_presets, (int)pcs::kNumPresets);

  for (int i = 0; i < pcs::k_num_params; ++i) {
    const unit_param_t& up = unit_header.params[i];
    check(up.init == pcs::kPresets[0].p[i],
          "param %d (%s): header default %d != preset 0 value %d", i, up.name, (int)up.init,
          (int)pcs::kPresets[0].p[i]);
  }

  for (int p = 0; p < pcs::kNumPresets; ++p) {
    for (int i = 0; i < pcs::k_num_params; ++i) {
      const unit_param_t& up = unit_header.params[i];
      const int16_t v = pcs::kPresets[p].p[i];
      check(v >= up.min && v <= up.max, "preset %d (%s) param %d (%s) = %d, range is [%d, %d]", p,
            pcs::kPresets[p].name, i, up.name, (int)v, (int)up.min, (int)up.max);
    }
    // A model index a preset ships must exist for the method it ships.
    const int m = pcs::kPresets[p].p[pcs::k_method];
    const int mo = pcs::kPresets[p].p[pcs::k_model];
    check(m >= 0 && m < (int)pcs::kNumMethods, "preset %d method %d out of range", p, m);
    if (m >= 0 && m < (int)pcs::kNumMethods)
      check(mo < (int)pcs::kNumModels[m], "preset %d (%s): model %d but method %s has %d", p,
            pcs::kPresets[p].name, mo, pcs::kMethodName[m], (int)pcs::kNumModels[m]);
  }

  // Preset names must be distinct and short enough to read on the display.
  for (int a = 0; a < pcs::kNumPresets; ++a) {
    check(std::strlen(pcs::kPresets[a].name) <= 8, "preset %d name '%s' is longer than 8 chars", a,
          pcs::kPresets[a].name);
    for (int b = a + 1; b < pcs::kNumPresets; ++b)
      check(std::strcmp(pcs::kPresets[a].name, pcs::kPresets[b].name) != 0,
            "presets %d and %d share the name '%s'", a, b, pcs::kPresets[a].name);
  }
}

/*===========================================================================*/

/**
 * The Karplus-Strong presets ship the page's wavetable length p scaled from
 * CLM's 22050 Hz default to 48 kHz.  Check every one of them against the number
 * printed on the page, so a hand-edited row cannot quietly detune a preset.
 */
static void test_ks_lengths() {
  banner("Karplus-Strong lengths are the page's p, rate-scaled");

  struct Row {
    int preset;
    int page_p;
  };
  const Row rows[] = {
      {38, 800}, {39, 1000}, {40, 4000}, {41, 2000}, {42, 100}, {43, 20}, {44, 50},
      {45, 150}, {46, 25},   {47, 40},   {48, 25},   {49, 200}, {50, 400},
  };
  for (const Row& r : rows) {
    check(pcs::kPresets[r.preset].p[pcs::k_method] == pcs::kKarplus,
          "preset %d (%s) is not a Karplus-Strong preset", r.preset,
          pcs::kPresets[r.preset].name);
    const int want = (int)(r.page_p * pcs::kKsRateScale + 0.5f);
    check(pcs::kPresets[r.preset].p[pcs::k_length] == want,
          "preset %d (%s): p = %d scales to %d, row has %d", r.preset,
          pcs::kPresets[r.preset].name, r.page_p, want,
          (int)pcs::kPresets[r.preset].p[pcs::k_length]);
  }
  // Every other preset carries the neutral length, which is preset 38's.
  for (int i = 0; i < pcs::kNumPresets; ++i) {
    if (pcs::kPresets[i].p[pcs::k_method] == pcs::kKarplus) continue;
    check(pcs::kPresets[i].p[pcs::k_length] == pcs::kPresets[38].p[pcs::k_length],
          "preset %d (%s) has a stray Length of %d", i, pcs::kPresets[i].name,
          (int)pcs::kPresets[i].p[pcs::k_length]);
  }
}

/*===========================================================================*/

/** CLM env: y = y0 + (y1-y0) * (base^u - 1) / (base - 1), per segment. */
static void test_env_base() {
  banner("clm::Env reproduces CLM's :base interpolation");

  const uint32_t n = 1000;
  const float xs[2] = {0.0f, 1.0f};
  const float ys[2] = {1.0f, 0.0f};

  const float bases[] = {0.1f, 1.0f, 10.0f, 100000.0f};
  for (float b : bases) {
    clm::Env e;
    e.setPoints(xs, ys, 2, b);
    e.trigger(n);
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
      const float got = e.process(i);
      const float u = (float)i / (float)n;
      const float want =
          (b == 1.0f) ? (1.0f - u) : (1.0f - (std::pow(b, u) - 1.0f) / (b - 1.0f));
      worst = std::fmax(worst, std::fabs(got - want));
    }
    check(worst < 2.0e-3f, "base %g: worst error %g", (double)b, (double)worst);
  }

  // base > 1 holds then drops; base < 1 drops then tails.  This is the
  // behaviour every one of the page's noise presets depends on.
  auto midpoint = [&](float b) {
    clm::Env e;
    e.setPoints(xs, ys, 2, b);
    e.trigger(n);
    float v = 0.0f;
    for (uint32_t i = 0; i <= n / 2; ++i) v = e.process(i);
    return v;
  };
  check(midpoint(100000.0f) > 0.98f, "base 100000 should still be near 1 at the midpoint, is %g",
        (double)midpoint(100000.0f));
  check(midpoint(1.0f) > 0.49f && midpoint(1.0f) < 0.51f, "base 1 should be 0.5 at the midpoint");
  // base 0.1 at the midpoint is 1 - (sqrt(.1) - 1)/(.1 - 1) = 0.2402.
  check(std::fabs(midpoint(0.1f) - 0.2402f) < 0.005f,
        "base 0.1 should be 0.240 at the midpoint, is %g", (double)midpoint(0.1f));

  // The four-point shape the voice builds must start, peak and end where asked.
  const float xs4[4] = {0.0f, 0.3f, 0.5f, 1.0f};
  const float ys4[4] = {0.0f, 1.0f, 1.0f, 0.0f};
  clm::Env e;
  e.setPoints(xs4, ys4, 4, 1.0f);
  e.trigger(n);
  float at30 = 0.0f, at50 = 0.0f, at100 = 0.0f;
  for (uint32_t i = 0; i < n; ++i) {
    const float v = e.process(i);
    if (i == (uint32_t)(0.3f * n)) at30 = v;
    if (i == (uint32_t)(0.5f * n)) at50 = v;
    if (i == n - 1) at100 = v;
  }
  check(std::fabs(at30 - 1.0f) < 0.01f, "4-point env at the attack breakpoint is %g", (double)at30);
  check(std::fabs(at50 - 1.0f) < 0.01f, "4-point env at the hold breakpoint is %g", (double)at50);
  check(at100 < 0.01f, "4-point env at the end is %g", (double)at100);
}

/*===========================================================================*/

/**
 * clm::Phasor leans on float_math.h's si_floorf(), which in every drumlogue
 * copy of that header used to be (float)((uint32_t)x) -- undefined for negative
 * input, and negative input is exactly what an FM deviation larger than the
 * carrier increment produces.  Pin both helpers against libm so this unit
 * cannot be built against a copy that has drifted back.
 */
/**
 * CLM's `:base`, pinned against Bill Schottstaedt's own published numbers.
 *
 * From the sndclm documentation, for the rising ramp '(0 0  1 1):
 *
 *     > (envelope-interp .1 '(0 0 1 1) 32.0)
 *     0.0133617278184869
 *     > (envelope-interp .1 '(0 0 1 1) .012)
 *     0.361774730775292
 *
 * These settle the direction of the control, which is worth having nailed down:
 * a base above 1 starts slowly, so on a DECAY it holds near full and then falls
 * off a cliff, and a base below 1 is the ordinary percussive fast-then-tail.
 * The page's noise instruments all use 10 and up, which is why they are sizzles
 * that stop rather than hits that decay -- that is the page, not a port bug.
 */
static void test_clm_base_against_clm() {
  banner("clm::Env :base matches CLM's published envelope-interp values");

  struct Row {
    float base;
    double want;
  };
  const Row rows[] = {{32.0f, 0.0133617278184869}, {0.012f, 0.361774730775292},
                      {1.0f, 0.1}};
  for (const Row& r : rows) {
    clm::Env e;
    const float xs[2] = {0.0f, 1.0f};
    const float ys[2] = {0.0f, 1.0f};
    e.setPoints(xs, ys, 2, r.base);
    e.trigger(10000);
    float got = 0.0f;
    for (uint32_t i = 0; i <= 1000; ++i) got = e.process(i);  // x = 0.1
    std::printf("  base %-8g -> %.12f (CLM: %.12f)\n", (double)r.base, (double)got, r.want);
    check(std::fabs((double)got - r.want) < 2.0e-5, "base %g: %.9f, CLM says %.9f", (double)r.base,
          (double)got, r.want);
  }
}

/*===========================================================================*/

static void test_float_math_floor() {
  banner("float_math.h si_floorf / si_ceilf agree with libm");

  int wrong = 0;
  float worst = 0.0f;
  for (double x = -1000.0; x <= 1000.0; x += 0.0009765625) {  // exact binary step
    const float v = (float)x;
    const float df = si_floorf(v) - std::floor(v);
    const float dc = si_ceilf(v) - std::ceil(v);
    if (df != 0.0f || dc != 0.0f) {
      ++wrong;
      worst = std::fmax(worst, std::fmax(std::fabs(df), std::fabs(dc)));
    }
  }
  check(wrong == 0, "si_floorf/si_ceilf disagree with libm at %d points, worst by %g", wrong,
        (double)worst);

  // And the wrap built on it: an increment far larger than one period, in both
  // directions, must still land in [0, 1).
  clm::Phasor ph;
  ph.clear();
  bool ok = true;
  for (int i = 0; i < 20000; ++i) {
    const float inc = (i & 1) ? -37.4159f : 41.5926f;
    const float p = ph.advance(inc);
    if (!(p >= 0.0f && p < 1.0f) || !std::isfinite(p)) {
      check(false, "Phasor left [0,1) at step %d: %g", i, (double)p);
      ok = false;
      break;
    }
  }
  if (ok) std::printf("  Phasor stayed in [0,1) over 20000 alternating +-40-period jumps\n");
}

/*===========================================================================*/

static void test_oscil() {
  banner("clm::Oscil frequency and amplitude over a 2 s decay");

  const float freqs[] = {50.0f, 440.0f, 1062.5f, 5357.0f};
  for (float f : freqs) {
    clm::Oscil o;
    o.set(M_TWOPI * f / 48000.0f);
    int zc = 0;
    float prev = 0.0f, peak = 0.0f, peak_late = 0.0f;
    const int n = 96000;  // 2 s, the longest preset decay bar Chowning's bell
    for (int i = 0; i < n; ++i) {
      const float v = o.process();
      if (prev <= 0.0f && v > 0.0f) ++zc;
      prev = v;
      const float a = std::fabs(v);
      if (a > peak) peak = a;
      if (i > n - 4800 && a > peak_late) peak_late = a;
      if (!std::isfinite(v)) {
        check(false, "%g Hz: non-finite at sample %d", (double)f, i);
        break;
      }
    }
    // The oscillator starts at zero and rises, so the run opens with a
    // crossing that does not close a cycle.
    const float got = (zc - 1) / 2.0f;
    check(std::fabs(got - f) / f < 0.01f, "%g Hz: measured %g Hz", (double)f, (double)got);
    check(std::fabs(peak - 1.0f) < 0.02f, "%g Hz: peak %g, want 1", (double)f, (double)peak);
    // A direct-form resonator drifts here; the coupled form must not.
    check(std::fabs(peak_late - 1.0f) < 0.02f, "%g Hz: peak after 2 s is %g", (double)f,
          (double)peak_late);
  }
}

/*===========================================================================*/

static void test_twopole_norm() {
  banner("clm::TwoPole holds its noise level across the Reso range");

  // The whole point of the RMS normalisation: driven by noise, output level
  // must not depend on r, so Reso is a timbre control and not a volume one.
  const float rs[] = {0.0f, 0.2f, 0.7f, 0.9f, 0.99f, 0.999f, 0.9999f, 0.99995f};
  for (float f : {100.0f, 400.0f, 4000.0f, 20000.0f}) {
    float lo = 1.0e9f, hi = 0.0f;
    for (float r : rs) {
      clm::TwoPole tp;
      tp.set(M_TWOPI * f / 48000.0f, r);
      clm::Rng rng;
      rng.seed(4242);
      const int n = 2000000;  // long enough for r = 0.99995 to settle
      const int skip = 500000;
      double acc = 0.0;
      bool bad = false;
      for (int i = 0; i < n; ++i) {
        const float y = tp.process(rng.nextBi());
        if (!std::isfinite(y)) {
          bad = true;
          break;
        }
        if (i >= skip) acc += (double)y * y;
      }
      check(!bad, "r=%g f=%g: non-finite", (double)r, (double)f);
      // randh here is uniform on [-1,1), variance 1/3, so unit-variance white
      // noise in would give 1 and this gives 1/sqrt(3).
      const float rms = (float)std::sqrt(acc / (n - skip)) * std::sqrt(3.0f);
      lo = std::fmin(lo, rms);
      hi = std::fmax(hi, rms);
    }
    const float spread_db = 20.0f * std::log10(hi / lo);
    std::printf("  %6.0f Hz: rms %.3f .. %.3f (%.1f dB across r)\n", (double)f, (double)lo,
                (double)hi, (double)spread_db);
    check(spread_db < 1.5f, "%g Hz: %.1f dB of level change across the Reso range", (double)f,
          (double)spread_db);
  }
}

/*===========================================================================*/

static void test_karplus_blend() {
  banner("Karplus-Strong blend factor");

  // b = 0 is pure averaging: the page's plucked string, and a signal whose
  // period is p.  b = 1 always inverts: period 2p, the hollow metallic one.
  auto period_energy = [](float b, uint32_t p, uint32_t lag) {
    pcs::Voice v;
    v.init(12345);
    pcs::VoiceParams vp;
    std::memset(&vp, 0, sizeof(vp));
    vp.method = pcs::kKarplus;
    vp.model = pcs::kKsRandom;
    vp.dur = 48000;
    vp.amp = 1.0f;
    vp.atk = 0.0f;
    vp.hold = 1.0f;
    vp.env_base = 1.0f;
    vp.note_ratio = 1.0f;
    vp.blend = b;
    vp.ks_len = p;
    v.trigger(vp, nullptr, nullptr);
    static float buf[24000];
    for (int i = 0; i < 24000; ++i) buf[i] = v.process();
    // Normalised autocorrelation at `lag`, over the settled part.
    double num = 0.0, den = 0.0;
    for (int i = 12000; i < 24000 - (int)lag; ++i) {
      num += buf[i] * buf[i + lag];
      den += buf[i] * buf[i];
    }
    return (den > 1e-12) ? (num / den) : 0.0;
  };

  const uint32_t p = 200;
  const double str_p = period_energy(0.0f, p, p);
  const double met_p = period_energy(1.0f, p, p);
  const double met_2p = period_energy(1.0f, p, 2 * p);
  std::printf("  b=0  corr at p   = %+.3f\n", str_p);
  std::printf("  b=1  corr at p   = %+.3f\n", met_p);
  std::printf("  b=1  corr at 2p  = %+.3f\n", met_2p);
  check(str_p > 0.8, "b=0 should be strongly periodic at p (got %.3f)", str_p);
  check(met_p < -0.5, "b=1 should invert every p (got %.3f)", met_p);
  check(met_2p > 0.5, "b=1 should be periodic at 2p (got %.3f)", met_2p);

  // b = 0.5 is the page's snare: the most randomness, so little periodicity.
  const double snare_p = period_energy(0.5f, p, p);
  std::printf("  b=.5 corr at p   = %+.3f\n", snare_p);
  check(std::fabs(snare_p) < 0.8, "b=0.5 should be far less periodic (got %.3f)", snare_p);
}

/*===========================================================================*/

static void test_randh_rate() {
  banner("clm::Randh holds at the requested rate");

  for (float rate : {0.49f, 0.1f, 0.01f}) {
    clm::Randh r;
    r.init(999, rate);
    int changes = 0;
    float prev = r.process();
    const int n = 48000;
    for (int i = 1; i < n; ++i) {
      const float v = r.process();
      if (v != prev) ++changes;
      prev = v;
    }
    const float got = (float)changes / (float)n;
    check(std::fabs(got - rate) < 0.02f, "rate %g: measured %g", (double)rate, (double)got);
  }
}

/*===========================================================================*/

struct Stats {
  float peak = 0.0f;
  double rms = 0.0;
  int nonfinite = 0;
};

static Stats render(Synth& s, int frames) {
  static float buf[2 * 512];
  Stats st;
  double acc = 0.0;
  int done = 0;
  while (done < frames) {
    const int n = (frames - done > 512) ? 512 : (frames - done);
    std::memset(buf, 0, sizeof(float) * 2 * n);
    s.Render(buf, n);
    for (int i = 0; i < 2 * n; ++i) {
      const float v = buf[i];
      if (!std::isfinite(v)) {
        ++st.nonfinite;
        continue;
      }
      const float a = std::fabs(v);
      if (a > st.peak) st.peak = a;
      acc += (double)v * v;
    }
    done += n;
  }
  st.rms = std::sqrt(acc / (2.0 * frames));
  return st;
}

static Synth& engine() {
  static Synth s;
  static bool ready = false;
  if (!ready) {
    unit_runtime_desc_t d;
    std::memset(&d, 0, sizeof(d));
    d.samplerate = 48000;
    d.output_channels = 2;
    d.input_channels = 0;
    d.frames_per_buffer = 64;
    const int8_t r = s.Init(&d);
    if (r != k_unit_err_none) {
      std::printf("  FAIL: Init returned %d\n", (int)r);
      ++g_fail;
    }
    ready = true;
  }
  return s;
}

/*===========================================================================*/

/** Render into a caller's buffer, so two renders can be compared sample by
 *  sample rather than only by their statistics. */
static void renderTo(Synth& s, float* mono, int frames) {
  static float buf[2 * 512];
  int done = 0;
  while (done < frames) {
    const int n = (frames - done > 512) ? 512 : (frames - done);
    std::memset(buf, 0, sizeof(float) * 2 * n);
    s.Render(buf, n);
    for (int i = 0; i < n; ++i) mono[done + i] = buf[2 * i];
    done += n;
  }
}

/** Sign changes per second: for a sine this is exactly twice the frequency. */
static double zeroCrossRate(const float* x, int from, int to) {
  int c = 0;
  for (int i = from + 1; i < to; ++i)
    if ((x[i] >= 0.0f) != (x[i - 1] >= 0.0f)) ++c;
  return (double)c * 48000.0 / (double)(to - from);
}

/** Normalised autocorrelation at one lag: a narrower band is more periodic. */
static double autocorr(const float* x, int n, int lag) {
  double r0 = 0.0, rk = 0.0;
  for (int i = 0; i < n - lag; ++i) {
    r0 += (double)x[i] * x[i];
    rk += (double)x[i] * x[i + lag];
  }
  return (r0 > 1.0e-12) ? (rk / r0) : 0.0;
}

/**
 * The pitch envelope in closed form, averaged over a window:
 *
 *     f(t) = f0 * 2^(d * e^(-t/tau) / 12)
 *
 * which is what the zero-crossing rate over that window measures.
 */
static double meanFreq(double f0, double semis, double tau, double t0, double t1) {
  const int n = 4096;
  double acc = 0.0;
  for (int i = 0; i < n; ++i) {
    const double t = t0 + (t1 - t0) * ((double)i + 0.5) / (double)n;
    acc += f0 * std::pow(2.0, semis * std::exp(-t / tau) / 12.0);
  }
  return acc / (double)n;
}

/** Mean absolute first difference over RMS: a cheap brightness proxy. */
static double brightness(const float* x, int n) {
  double d = 0.0, e = 0.0;
  for (int i = 1; i < n; ++i) {
    d += std::fabs((double)x[i] - x[i - 1]);
    e += (double)x[i] * x[i];
  }
  return (e > 1.0e-12) ? (d / (double)(n - 1) / std::sqrt(e / (double)(n - 1))) : 0.0;
}

static float g_a[48000];
static float g_b[48000];

/*===========================================================================*/

/**
 * The two constant-Q models.  Their whole claim is that they cost nothing where
 * a preset already sits and change only what happens when it is moved, so the
 * first half of this is an exact comparison and not a statistical one.
 */
/** Re-Init the shared engine, which reseeds every voice's RNG.  Two noise
 *  renders are only comparable sample for sample if they start there. */
static Synth& reseeded() {
  Synth& s = engine();
  unit_runtime_desc_t d;
  std::memset(&d, 0, sizeof(d));
  d.samplerate = 48000;
  d.output_channels = 2;
  d.frames_per_buffer = 64;
  s.Init(&d);
  return s;
}

static void test_constant_q() {
  banner("TwoPolQ and TunedNsQ: identical at the anchor, constant Q away from it");

  Synth& s = engine();
  const int n = 24000;

  struct Case {
    int preset, plain, cq;
    const char* name;
  };
  const Case cases[] = {
      {11, pcs::kTwoPole, pcs::kTwoPoleQ, "Subtr TwoPole/TwoPolQ"},
      {28, pcs::kTunedNoise, pcs::kTunedNoiseQ, "Addit TunedNs/TunedNsQ"},
  };

  for (const Case& c : cases) {
    // At note 60 with no punch the pitch ratio is 1, so r^1 is r.
    reseeded().LoadPreset((uint8_t)c.preset);
    s.setParameter(pcs::k_model, c.plain);
    s.NoteOn(60, 127);
    renderTo(s, g_a, n);

    reseeded().LoadPreset((uint8_t)c.preset);
    s.setParameter(pcs::k_model, c.cq);
    s.NoteOn(60, 127);
    renderTo(s, g_b, n);

    check(std::memcmp(g_a, g_b, sizeof(float) * n) == 0,
          "%s: the Q model is not bit-identical at note 60", c.name);
  }

  // Two octaves up, a fixed radius keeps its bandwidth in hertz, so against a
  // centre four times higher it is four times narrower -- and a narrower band
  // is a more periodic signal.  Held at Q the band widens with the note, so its
  // autocorrelation at the centre period has to come out lower.
  reseeded().LoadPreset(11);
  s.setParameter(pcs::k_model, pcs::kTwoPole);
  s.NoteOn(84, 127);
  renderTo(s, g_a, n);

  reseeded().LoadPreset(11);
  s.setParameter(pcs::k_model, pcs::kTwoPoleQ);
  s.NoteOn(84, 127);
  renderTo(s, g_b, n);

  // Preset 11 is Freq 100 Hz, and note 84 is two octaves up.
  const int lag = (int)(48000.0 / 400.0 + 0.5);
  const double ra = autocorr(g_a, n, lag), rb = autocorr(g_b, n, lag);
  std::printf("  note 84, autocorrelation at the centre period: fixed r %.3f, held Q %.3f\n", ra,
              rb);
  check(ra > rb + 0.05, "at note 84 the Q model is not the broader band (%.3f vs %.3f)", ra, rb);
}

/*===========================================================================*/

/**
 * The pitch envelope: depth in semitones at the attack, falling exponentially
 * with a time constant of ModDcy of the duration.  Measured on an FM carrier
 * with both indices at zero, which is a bare sine, so twice the zero-crossing
 * rate is the frequency and nothing else.
 */
static void test_punch() {
  banner("Punch is 24 semitones at full scale, decaying over ModDcy of the note");

  Synth& s = engine();
  const int f0 = 100;

  const int knob[] = {0, 250, 500, 1000};
  const float semis[] = {0.0f, 6.0f, 12.0f, 24.0f};

  for (int k = 0; k < 4; ++k) {
    s.AllNoteOff();
    s.LoadPreset(29);  // ChwnBell: FM, and long enough to hold a pitch still
    s.setParameter(pcs::k_index1, 0);
    s.setParameter(pcs::k_index2, 0);  // no modulation: a bare carrier
    s.setParameter(pcs::k_freq, f0);
    s.setParameter(pcs::k_decay, 4000);
    s.setParameter(pcs::k_moddcy, 99);  // tau is 3.96 s, so 200 ms is flat
    s.setParameter(pcs::k_attack, 0);
    s.setParameter(pcs::k_hold, 990);
    s.setParameter(pcs::k_punch, knob[k]);
    s.NoteOn(60, 127);
    renderTo(s, g_a, 9600);

    // The zero-crossing rate over a window is the mean frequency over it, and
    // the pitch is still falling, so the closed form has to be averaged the
    // same way rather than read at t = 0.
    const double got = zeroCrossRate(g_a, 480, 9600) * 0.5;
    const double want = meanFreq((double)f0, (double)semis[k], 0.99 * 4.0, 0.01, 0.2);
    std::printf("  Punch %5.1f%% -> %7.2f Hz (want %7.2f, %+.2f%%)\n", knob[k] * 0.1, got, want,
                100.0 * (got - want) / want);
    check(std::fabs(got - want) < 0.02 * want, "Punch %d: %.2f Hz, wanted %.2f", knob[k], got,
          want);
  }

  // And the time constant.  ModDcy 25% of a 2 s note is 500 ms, so one time
  // constant in is 24 * e^-1 = 8.83 semitones above the settled note.
  s.AllNoteOff();
  s.LoadPreset(29);
  s.setParameter(pcs::k_index1, 0);
  s.setParameter(pcs::k_index2, 0);
  s.setParameter(pcs::k_freq, f0);
  s.setParameter(pcs::k_decay, 2000);
  s.setParameter(pcs::k_moddcy, 25);
  s.setParameter(pcs::k_attack, 0);
  s.setParameter(pcs::k_hold, 990);
  s.setParameter(pcs::k_punch, 1000);
  s.NoteOn(60, 127);
  renderTo(s, g_a, 48000);

  const int lo = (int)(0.45 * 48000.0), hi = (int)(0.55 * 48000.0);
  const double got = zeroCrossRate(g_a, lo, hi) * 0.5;
  const double want = meanFreq((double)f0, 24.0, 0.25 * 2.0, 0.45, 0.55);
  std::printf("  at one time constant: %7.2f Hz (want %7.2f, %+.2f%%)\n", got, want,
              100.0 * (got - want) / want);
  check(std::fabs(got - want) < 0.05 * want, "at tau: %.2f Hz, wanted %.2f", got, want);
}

/*===========================================================================*/

/**
 * The tone control.  Unity in the passband is the part that matters: a knob
 * that changes the level as you turn it cannot be used while listening.
 */
static void test_tone() {
  banner("Coef as a tone control: unity in the passband, monotonic in brightness");

  // Low-pass: the settled response to DC must be 1.  High-pass: the settled
  // response to the alternating sequence, which is Nyquist, must be 1.
  for (float fc : {60.0f, 400.0f, 4000.0f, 18000.0f}) {
    clm::Tone lp, hp;
    lp.set(fc, 48000.0f, false);
    hp.set(fc, 48000.0f, true);
    float ylp = 0.0f, yhp = 0.0f;
    for (int i = 0; i < 400000; ++i) {
      ylp = lp.process(1.0f);
      yhp = hp.process((i & 1) ? -1.0f : 1.0f);
    }
    check(std::fabs(ylp - 1.0f) < 1.0e-3f, "low-pass at %g Hz: DC gain %.6f", (double)fc,
          (double)ylp);
    check(std::fabs(std::fabs(yhp) - 1.0f) < 1.0e-3f, "high-pass at %g Hz: Nyquist gain %.6f",
          (double)fc, (double)std::fabs(yhp));
  }

  // And through the unit, on a bank of sinusoids that spends no Coef of its own.
  Synth& s = engine();
  const int n = 24000;
  double prev = -1.0, lo = 0.0, hi = 0.0;
  for (int c : {-95, -50, -10, 0, 10, 50, 95}) {
    s.AllNoteOff();
    s.LoadPreset(21);  // TubBell1: additive, so Coef is free to be the tone
    s.setParameter(pcs::k_coef, c);
    s.NoteOn(60, 127);
    renderTo(s, g_a, n);
    const double b = brightness(g_a, n);
    std::printf("  Coef %4d -> brightness %.4f\n", c, b);
    check(b >= prev - 1.0e-4, "Coef %d is darker than the setting below it (%.4f vs %.4f)", c, b,
          prev);
    prev = b;
    if (c == -95) lo = b;
    if (c == 95) hi = b;
  }
  check(hi > lo * 1.3, "the tone control spans only %.4f to %.4f", lo, hi);
}

static void test_all_presets() {
  banner("every preset renders: finite, audible, bounded");

  Synth& s = engine();
  std::printf("  %-10s %8s %8s %8s\n", "preset", "peak", "rms", "dBFS");
  for (int i = 0; i < pcs::kNumPresets; ++i) {
    s.AllNoteOff();
    s.LoadPreset((uint8_t)i);
    // Long enough for the longest decay bar Chowning's 10 s bell, plus a tail.
    const int frames = 48000 * 3;
    s.NoteOn(60, 127);
    const Stats st = render(s, frames);
    const double db = (st.rms > 1e-9) ? 20.0 * std::log10(st.rms) : -200.0;
    std::printf("  %-10s %8.4f %8.5f %8.1f\n", pcs::kPresets[i].name, (double)st.peak, st.rms, db);
    check(st.nonfinite == 0, "preset %d (%s): %d non-finite samples", i, pcs::kPresets[i].name,
          st.nonfinite);
    check(st.peak > 1.0e-4f, "preset %d (%s): silent (peak %g)", i, pcs::kPresets[i].name,
          (double)st.peak);
    check(st.peak <= 1.0f, "preset %d (%s): peak %g exceeds full scale", i, pcs::kPresets[i].name,
          (double)st.peak);
  }
}

/*===========================================================================*/

static void test_param_sweep() {
  banner("every parameter, across its whole declared range");

  Synth& s = engine();
  int worst_preset = -1;
  float worst_peak = 0.0f;
  for (int prm = 0; prm < pcs::k_num_params; ++prm) {
    const unit_param_t& up = unit_header.params[prm];
    const int steps = 12;
    for (int k = 0; k <= steps; ++k) {
      const int v = up.min + (int)((int64_t)(up.max - up.min) * k / steps);
      // Sweep the parameter on a preset from each engine family, so the value
      // is exercised in the engine that reads it as well as in one that does not.
      for (int base : {0, 17, 29, 38, 51}) {
        s.AllNoteOff();
        s.LoadPreset((uint8_t)base);
        s.setParameter((uint8_t)prm, v);
        s.NoteOn(60, 127);
        const Stats st = render(s, 24000);
        check(st.nonfinite == 0, "param %s = %d on preset %d: %d non-finite", up.name, v, base,
              st.nonfinite);
        check(st.peak <= 1.0f, "param %s = %d on preset %d: peak %g", up.name, v, base,
              (double)st.peak);
        if (st.peak > worst_peak) {
          worst_peak = st.peak;
          worst_preset = base;
        }
      }
    }
  }
  std::printf("  worst peak over the sweep: %.4f (preset %d)\n", (double)worst_peak, worst_preset);
}

/*===========================================================================*/

static void test_notes_and_voices() {
  banner("note range, velocity, voice stealing");

  Synth& s = engine();
  for (int i = 0; i < pcs::kNumPresets; i += 7) {
    s.AllNoteOff();
    s.LoadPreset((uint8_t)i);
    for (int note = 12; note <= 108; note += 12) {
      s.NoteOn((uint8_t)note, 100);
      const Stats st = render(s, 4800);
      check(st.nonfinite == 0, "preset %d note %d: non-finite", i, note);
      check(st.peak <= 1.0f, "preset %d note %d: peak %g", i, note, (double)st.peak);
    }
  }

  // More simultaneous hits than there are voices, on a long preset.
  s.AllNoteOff();
  s.LoadPreset(29);  // ChwnBell, 10 s
  for (int i = 0; i < 12; ++i) {
    s.NoteOn((uint8_t)(48 + i), (uint8_t)(30 + i * 8));
    const Stats st = render(s, 480);
    check(st.nonfinite == 0, "voice steal %d: non-finite", i);
    check(st.peak <= 1.0f, "voice steal %d: peak %g", i, (double)st.peak);
  }

  // Velocity 0 must not start anything, and a gate with no preceding note must
  // still use the last note rather than reading uninitialised state.
  s.AllNoteOff();
  s.LoadPreset(0);
  s.NoteOn(60, 0);
  const Stats silent = render(s, 4800);
  check(silent.peak < 1.0e-6f, "velocity 0 produced sound (peak %g)", (double)silent.peak);
  s.GateOn(127);
  const Stats gated = render(s, 4800);
  check(gated.peak > 1.0e-4f, "GateOn produced nothing (peak %g)", (double)gated.peak);
}

/*===========================================================================*/

static void test_param_strings() {
  banner("string parameters resolve for every value they can take");

  Synth& s = engine();
  for (int m = 0; m < (int)pcs::kNumMethods; ++m) {
    s.setParameter(pcs::k_method, m);
    check(s.getParameterStrValue(pcs::k_method, m) != nullptr, "method %d has no string", m);
    for (int mo = unit_header.params[pcs::k_model].min; mo <= unit_header.params[pcs::k_model].max;
         ++mo) {
      const char* str = s.getParameterStrValue(pcs::k_model, mo);
      check(str != nullptr && str[0] != '\0', "method %d model %d has no string", m, mo);
    }
  }
  for (int c = unit_header.params[pcs::k_curve].min; c <= unit_header.params[pcs::k_curve].max; ++c)
    check(s.getParameterStrValue(pcs::k_curve, c) != nullptr, "curve %d has no string", c);
  for (int b = unit_header.params[pcs::k_bank].min; b <= unit_header.params[pcs::k_bank].max; ++b)
    check(s.getParameterStrValue(pcs::k_bank, b) != nullptr, "bank %d has no string", b);

  for (int i = 0; i < pcs::kNumPresets; ++i)
    check(Synth::getPresetName((uint8_t)i) != nullptr, "preset %d has no name", i);
  check(Synth::getPresetName((uint8_t)pcs::kNumPresets) == nullptr,
        "out-of-range preset index returned a name");
}

/*===========================================================================*/

/**
 * Real-time factor for the heaviest setting of each engine at full polyphony.
 *
 * Informational -- the host is not a Cortex-A7 -- but a regression that makes
 * one engine an order of magnitude dearer than the others shows up here, and
 * that is the shape of the fault that has crashed this hardware before (a note
 * change running two full modal banks, in Brachetti's pass 43).
 */
static void test_cpu() {
  banner("cost per engine, four voices, worst case (host real-time factor)");

  Synth& s = engine();
  struct Case {
    const char* name;
    int preset;
    int model;
    int bank;
  };
  // Additive is measured on the 28-partial gong, which is the largest bank.
  const Case cases[] = {
      {"Subtr TwoPole", 9, pcs::kTwoPole, 0},   {"Addit Pure", 24, pcs::kPure, 7},
      {"Addit NsyFrq", 24, pcs::kNoisyFreqs, 7}, {"Addit TunedNs", 24, pcs::kTunedNoise, 7},
      {"FM", 29, 0, 0},                          {"KarplS", 40, 0, 0},
      {"Granul", 51, pcs::kGrainCym, 0},
  };
  for (const Case& c : cases) {
    s.AllNoteOff();
    s.LoadPreset((uint8_t)c.preset);
    s.setParameter(pcs::k_model, c.model);
    if (c.bank) s.setParameter(pcs::k_bank, c.bank);
    s.setParameter(pcs::k_decay, 10000);  // keep all four voices alive
    for (int v = 0; v < 4; ++v) s.NoteOn((uint8_t)(48 + v * 5), 127);

    const int frames = 48000 * 2;
    const clock_t t0 = clock();
    const Stats st = render(s, frames);
    const double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    const double rtf = (frames / 48000.0) / (secs > 0 ? secs : 1e-9);
    std::printf("  %-14s %8.0fx real time%s\n", c.name, rtf,
                st.nonfinite ? "   !! non-finite" : "");
    check(st.nonfinite == 0, "%s: non-finite output", c.name);
    check(rtf > 20.0, "%s: only %.0fx real time on the host", c.name, rtf);
  }
}

/*===========================================================================*/

int main() {
  std::printf("--- Percussio DSP tests ---\n");
  test_header_matches_preset0();
  test_ks_lengths();
  test_env_base();
  test_clm_base_against_clm();
  test_float_math_floor();
  test_oscil();
  test_twopole_norm();
  test_karplus_blend();
  test_randh_rate();
  test_all_presets();
  test_constant_q();
  test_punch();
  test_tone();
  test_notes_and_voices();
  test_param_strings();
  test_param_sweep();
  test_cpu();

  std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail,
              g_fail == 1 ? "" : "s");
  return g_fail ? 1 : 0;
}

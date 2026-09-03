/**
 * @file measure.cpp
 * @brief Host-side polyphony meter for drumlogue user synth units.
 *
 * The level meter next door asks what one hit puts on the bus.  This asks what
 * happens when a unit is given more than one at a time, which is where a fixed
 * master gain into a memoryless waveshaper falls apart: every extra voice
 * multiplies the bus level, so the shaper stops acting on peaks and starts
 * bending the whole waveform.  On one dominant low partial that reads as "fat";
 * on a dense inharmonic spectrum -- a cymbal, a gong, or simply several voices
 * at once -- it manufactures every intermodulation product between every pair
 * of partials, which reads as harshness.  Nothing in a single-hit loudness
 * measurement shows it.
 *
 * Three modes, all driving the public unit_* API the way the runtime does:
 *
 *   poly N        N simultaneous note-ons on distinct notes, for 1..N voices.
 *                 Reports the delivered level against the ideal, plus a
 *                 gain-compensated distortion figure: the best scalar gain is
 *                 fitted per 512-frame window against the one-voice render and
 *                 only the residual is counted, so a unit that legitimately
 *                 turns a stack down is not scored as if it had distorted it.
 *
 *   roll N T      N hits on the SAME note, T ms apart -- the drumlogue
 *                 sequencer path, where unit_gate_on fires the instrument's
 *                 assigned note every step.  Reports the RMS either side of
 *                 each retrigger: an allocator that resets a voice while its
 *                 tail is still loud steps to silence and then climbs the new
 *                 attack, and that step is the click.
 *
 *   dump N FILE   Raw stereo floats for N simultaneous voices, so the same
 *                 render can be compared against a linear-gain build of the
 *                 same unit with compare.py.  That is the only way to see the
 *                 absolute distortion of the output stage rather than how it
 *                 changes with voice count.
 *
 * Build and run with ./run.sh <project-dir>; see README.md.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "unit.h"

static uint8_t mock_banks() { return 0; }
static uint8_t mock_samples(uint8_t) { return 0; }
static const sample_wrapper_t* mock_sample(uint8_t, uint8_t) { return nullptr; }

static const uint32_t kSR = 48000;
static const uint32_t kFrames = 64;

static bool bad_float(float v) {
  uint32_t u; memcpy(&u, &v, 4);
  return ((u >> 23) & 0xFFu) == 0xFFu;
}
static uint64_t g_bad = 0;

static void render_frames(std::vector<float>& dst, uint32_t frames) {
  float buf[kFrames * 2];
  for (uint32_t done = 0; done < frames; done += kFrames) {
    memset(buf, 0, sizeof(buf));
    unit_render(nullptr, buf, kFrames);
    for (uint32_t i = 0; i < kFrames * 2; ++i) {
      float v = buf[i];
      if (bad_float(v)) { g_bad++; v = 0.f; }
      dst.push_back(v);
    }
  }
}

static double db(double x) { return x > 1e-12 ? 20.0 * log10(x) : -240.0; }

static int g_sel_param = 0;

static void setup(int instr) {
  unit_reset();
  unit_resume();
  for (uint32_t i = 0; i < unit_header.num_params; ++i)
    unit_set_param_value((uint8_t)i, unit_header.params[i].init);
  if (g_sel_param >= 0 && g_sel_param < (int)unit_header.num_params)
    unit_set_param_value((uint8_t)g_sel_param, instr);
  std::vector<float> warm;
  render_frames(warm, kFrames * 4);
}

static const char* sel_name(int instr) {
  if (g_sel_param < 0 || g_sel_param >= (int)unit_header.num_params) return "-";
  const char* n = unit_get_param_str_value((uint8_t)g_sel_param, instr);
  return n ? n : "-";
}

int main(int argc, char** argv) {
  const char* mode = (argc > 1) ? argv[1] : "poly";
  int instr = (argc > 2) ? atoi(argv[2]) : 0;
  int nhits = (argc > 3) ? atoi(argv[3]) : 4;
  int gap_ms = (argc > 4) ? atoi(argv[4]) : 125;
  int vel = (argc > 5) ? atoi(argv[5]) : 127;
  double secs = (argc > 6) ? atof(argv[6]) : 3.0;
  // Which parameter selects the sound.  EffeESP32 and EffeMD use param 0
  // ("Instr"), ScrutaAstri uses "Prgrm"; -1 leaves every parameter at its
  // header default and selects nothing.
  g_sel_param = (getenv("SEL_PARAM") ? atoi(getenv("SEL_PARAM")) : 0);

  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = unit_header.target;
  desc.api = UNIT_API_VERSION;
  desc.samplerate = kSR;
  desc.frames_per_buffer = kFrames;
  desc.input_channels = 2;
  desc.output_channels = 2;
  desc.get_num_sample_banks = mock_banks;
  desc.get_num_samples_for_bank = mock_samples;
  desc.get_sample = mock_sample;
  if (unit_init(&desc) != k_unit_err_none) { printf("unit_init failed\n"); return 1; }

  const uint32_t total = (uint32_t)(secs * kSR);
  const char* iname = sel_name(instr);

  if (!strcmp(mode, "poly")) {
    // one voice alone -> reference
    setup(instr);
    std::vector<float> one;
    unit_note_on(60, (uint8_t)vel);
    g_bad = 0;
    render_frames(one, total);

    printf("instr %d (%s)  vel %d   %.1f s\n", instr, iname ? iname : "?", vel, secs);
    printf("%-6s %9s %9s %9s %9s %9s\n", "voices", "peak dB", "rms dB",
           "gain dB", "ideal dB", "dist dB");
    for (int n = 1; n <= nhits; ++n) {
      setup(instr);
      std::vector<float> s;
      for (int k = 0; k < n; ++k) unit_note_on((uint8_t)(60 + k), (uint8_t)vel);
      g_bad = 0;
      render_frames(s, total);

      // Distortion, separated from level.  A limiter legitimately turns the
      // stack down, so comparing against a fixed N x reference would score a
      // clean gain reduction as if it were distortion.  Instead fit the best
      // scalar gain per 512-sample window (10.7 ms -- long enough that the fit
      // cannot absorb audio-rate distortion, short enough to track a limiter)
      // and report only what the gain cannot explain.
      double peak = 0, sumsq = 0, errsq = 0, refsq = 0;
      size_t m = std::min(s.size(), one.size());
      const size_t win = 1024;   // 512 frames, interleaved
      for (size_t w0 = 0; w0 + win <= m; w0 += win) {
        double num = 0, den = 0;
        for (size_t i = w0; i < w0 + win; ++i) { num += (double)s[i] * one[i]; den += (double)one[i] * one[i]; }
        if (den <= 1e-18) continue;
        const double alpha = num / den;
        for (size_t i = w0; i < w0 + win; ++i) {
          const double ref = alpha * one[i];
          const double e = (double)s[i] - ref;
          errsq += e * e; refsq += ref * ref;
        }
      }
      for (float v : s) { peak = std::max(peak, (double)fabsf(v)); sumsq += (double)v * v; }
      double rms = sqrt(sumsq / (s.size() ? s.size() : 1));
      double onepk = 0, onesq = 0;
      for (float v : one) { onepk = std::max(onepk, (double)fabsf(v)); onesq += (double)v * v; }
      double onerms = sqrt(onesq / (one.size() ? one.size() : 1));
      printf("%-6d %9.2f %9.2f %9.2f %9.2f %9.2f%s\n", n, db(peak), db(rms),
             db(rms) - db(onerms), 20.0 * log10((double)n),
             db(sqrt(errsq / (refsq > 0 ? refsq : 1))),
             g_bad ? "  !!nonfinite" : "");
    }
  } else if (!strcmp(mode, "dump")) {
    // Render `nhits` simultaneous voices and dump the raw stereo floats, so the
    // same render can be compared against a linear-gain build of the same code.
    const char* path = (argc > 7) ? argv[7] : "/tmp/dump.f32";
    setup(instr);
    std::vector<float> s;
    for (int k = 0; k < nhits; ++k) unit_note_on((uint8_t)(60 + k), (uint8_t)vel);
    g_bad = 0;
    render_frames(s, total);
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return 1; }
    fwrite(s.data(), sizeof(float), s.size(), f);
    fclose(f);
    double peak = 0;
    for (float v : s) peak = std::max(peak, (double)fabsf(v));
    printf("%s: %zu frames, peak %.2f dB%s\n", path, s.size() / 2, db(peak),
           g_bad ? "  !!nonfinite" : "");
  } else {  // roll
    setup(instr);
    std::vector<float> s;
    const uint32_t gap = (uint32_t)((double)gap_ms * kSR / 1000.0);
    std::vector<size_t> hit_at;
    g_bad = 0;
    for (int k = 0; k < nhits; ++k) {
      hit_at.push_back(s.size() / 2);
      unit_gate_on((uint8_t)vel);
      render_frames(s, gap);
    }
    render_frames(s, total);

    double peak = 0;
    const size_t n = s.size() / 2;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, (double)fabsf(s[2 * i]));

    // Continuity across a retrigger.  A voice whose envelope is reset to zero
    // while its tail is still loud steps straight to silence and then climbs
    // the new attack, so the 1 ms after the hit is far quieter than the 1 ms
    // before it: that step is the click.  A voice that is faded out instead
    // keeps the ratio near 1.
    auto rms = [&](size_t a, size_t b) {
      double acc = 0; size_t c = 0;
      for (size_t i = a; i < b && i < n; ++i) { acc += (double)s[2 * i] * s[2 * i]; ++c; }
      return c ? sqrt(acc / c) : 0.0;
    };
    const size_t ms = kSR / 1000;
    printf("instr %d (%s)  %d hits, %d ms apart, vel %d\n", instr,
           iname ? iname : "?", nhits, gap_ms, vel);
    printf("  peak            %8.2f dB\n", db(peak));
    for (size_t k = 1; k < hit_at.size(); ++k) {
      const size_t i = hit_at[k];
      const double before = rms(i > ms ? i - ms : 0, i);
      const double after = rms(i, i + ms);
      printf("  hit %zu @%6.1f ms: rms 1 ms before %7.4f  after %7.4f  ratio %5.2f\n",
             k + 1, 1000.0 * i / kSR, before, after,
             before > 1e-9 ? after / before : 0.0);
    }
      // Mono-per-note choke is deliberate on a drum part, so this number is
    // expected to stay put: it says only the last hit is still ringing.  What
    // must change is HOW the previous hit ended -- faded, not cut.
    printf("  tail rms, last 200 ms  %7.4f\n", rms(n > 200 * ms ? n - 200 * ms : 0, n));
    if (g_bad) printf("  !! %llu non-finite samples\n", (unsigned long long)g_bad);
  }
  unit_teardown();
  return 0;
}

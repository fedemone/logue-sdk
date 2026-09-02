/**
 * @file measure.cpp
 * @brief Host-side level and loudness meter for drumlogue user synth units.
 *
 * Drives a unit through the public unit_* API exactly the way the drumlogue
 * runtime does -- init, header defaults, preset (or a parameter sweep), note on,
 * hold, note off, release -- and reports, per preset:
 *
 *   peak dB   sample peak.  Anything at 0.00 is hard clipping.
 *   rms dB    plain RMS over the whole window.
 *   LUFS      gated ITU-R BS.1770-4 loudness.  This is the number that predicts
 *             whether a part sits in a pattern or disappears under the drums;
 *             peak level does not.  0 LUFS == a full scale sine on both
 *             channels, so a drumlogue part wants roughly -9.
 *   DC dB     DC offset.  Anything above about -40 dB is eating headroom.
 *   crest     peak minus RMS.  A large crest factor with the peak already at
 *             the ceiling is the "sounds fine solo, vanishes in the mix" case:
 *             there is no headroom left for a plain gain, only for raising RMS
 *             under a fixed ceiling (see common/output_stage.h).
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

static uint8_t mock_get_num_sample_banks() { return 0; }
static uint8_t mock_get_num_samples_for_bank(uint8_t) { return 0; }
static const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

static const uint32_t kSR = 48000;
static const uint32_t kFrames = 64;

// ---- BS.1770 K-weighting (48 kHz coefficients) -------------------------------
struct Biquad {
  double b0, b1, b2, a1, a2;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  double process(double x) {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
  }
};
static Biquad make_shelf() {
  return Biquad{1.53512485958697, -2.69169618940638, 1.19839281085285,
                -1.69065929318241, 0.73248077421585};
}
static Biquad make_rlb() {
  return Biquad{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
}

// Gated integrated loudness over interleaved stereo.
static double lufs_integrated(const std::vector<float>& s) {
  Biquad sl = make_shelf(), sr = make_shelf();
  Biquad rl = make_rlb(), rr = make_rlb();
  const size_t n = s.size() / 2;
  const size_t block = kSR * 400 / 1000;   // 400 ms
  const size_t hop = block / 4;            // 75 % overlap
  std::vector<double> kl(n), kr(n);
  for (size_t i = 0; i < n; ++i) {
    kl[i] = rl.process(sl.process(s[2 * i]));
    kr[i] = rr.process(sr.process(s[2 * i + 1]));
  }
  std::vector<double> bl;  // per-block loudness
  std::vector<double> bp;  // per-block mean-square power
  for (size_t start = 0; start + block <= n; start += hop) {
    double acc = 0.0;
    for (size_t i = start; i < start + block; ++i) acc += kl[i] * kl[i] + kr[i] * kr[i];
    double p = acc / (double)block;
    if (p <= 0.0) p = 1e-30;
    bp.push_back(p);
    bl.push_back(-0.691 + 10.0 * log10(p));
  }
  if (bl.empty()) return -200.0;
  // absolute gate at -70 LUFS
  double sum = 0.0; size_t cnt = 0;
  for (size_t i = 0; i < bl.size(); ++i)
    if (bl[i] > -70.0) { sum += bp[i]; cnt++; }
  if (!cnt) return -200.0;
  double rel = -0.691 + 10.0 * log10(sum / cnt) - 10.0;  // relative gate
  sum = 0.0; cnt = 0;
  for (size_t i = 0; i < bl.size(); ++i)
    if (bl[i] > -70.0 && bl[i] > rel) { sum += bp[i]; cnt++; }
  if (!cnt) return -200.0;
  return -0.691 + 10.0 * log10(sum / cnt);
}

static double db(double x) { return x > 1e-9 ? 20.0 * log10(x) : -180.0; }

// -ffast-math makes std::isfinite unreliable, so test the bit pattern.
static bool bad_float(float v) {
  uint32_t u; memcpy(&u, &v, 4);
  return ((u >> 23) & 0xFFu) == 0xFFu;  // NaN or Inf
}
static uint64_t g_bad = 0;

static void render_seconds(std::vector<float>& dst, double seconds) {
  float buf[kFrames * 2];
  uint32_t blocks = (uint32_t)(seconds * kSR / kFrames);
  for (uint32_t b = 0; b < blocks; ++b) {
    memset(buf, 0, sizeof(buf));
    unit_render(nullptr, buf, kFrames);
    for (uint32_t i = 0; i < kFrames * 2; ++i) {
      float v = buf[i];
      if (bad_float(v)) { g_bad++; v = 0.f; }
      dst.push_back(v);
    }
  }
}

int main(int argc, char** argv) {
  int note = (argc > 1) ? atoi(argv[1]) : 60;
  int vel = (argc > 2) ? atoi(argv[2]) : 127;
  int max_presets = (argc > 3) ? atoi(argv[3]) : -1;
  const char* wav_dir = (argc > 4 && argv[4][0] != '-') ? argv[4] : nullptr;
  int sweep_param = (argc > 5) ? atoi(argv[5]) : -1;   // sweep this param instead of presets
  int sweep_steps = (argc > 6) ? atoi(argv[6]) : 16;

  unit_runtime_desc_t desc;
  memset(&desc, 0, sizeof(desc));
  desc.target = unit_header.target;
  desc.api = UNIT_API_VERSION;
  desc.samplerate = kSR;
  desc.frames_per_buffer = kFrames;
  desc.input_channels = 2;
  desc.output_channels = 2;
  desc.get_num_sample_banks = mock_get_num_sample_banks;
  desc.get_num_samples_for_bank = mock_get_num_samples_for_bank;
  desc.get_sample = mock_get_sample;

  if (unit_init(&desc) != k_unit_err_none) { printf("unit_init failed\n"); return 1; }

  uint32_t np = unit_header.num_presets ? unit_header.num_presets : 1;
  if (max_presets > 0 && np > (uint32_t)max_presets) np = (uint32_t)max_presets;
  int sp_min = 0, sp_max = 0;
  if (sweep_param >= 0 && sweep_param < (int)unit_header.num_params) {
    sp_min = unit_header.params[sweep_param].min;
    sp_max = unit_header.params[sweep_param].max;
    np = (uint32_t)sweep_steps;
    printf("sweeping param %d (%s) over [%d..%d] in %u steps\n", sweep_param,
           unit_header.params[sweep_param].name, sp_min, sp_max, np);
  }

  printf("unit=%.8s presets=%u params=%u  note=%d vel=%d\n", unit_header.name,
         (unsigned)unit_header.num_presets, (unsigned)unit_header.num_params, note, vel);
  printf("%-4s %-9s %9s %9s %9s %9s %8s\n", "prst", "name", "peak dB", "rms dB", "LUFS", "DC dB", "crest");

  double best = -300, worst = 300, sum = 0; uint32_t cnt = 0;

  for (uint32_t p = 0; p < np; ++p) {
    unit_reset();
    unit_resume();
    for (uint32_t i = 0; i < unit_header.num_params; ++i)
      unit_set_param_value((uint8_t)i, unit_header.params[i].init);
    int sv = 0;
    if (sweep_param >= 0) {
      sv = sp_min + (int)((int64_t)(sp_max - sp_min) * p / (np > 1 ? np - 1 : 1));
      unit_set_param_value((uint8_t)sweep_param, sv);
    } else if (unit_header.num_presets > 0) {
      unit_load_preset((uint8_t)p);
    }

    g_bad = 0;
    std::vector<float> scratch;
    render_seconds(scratch, 0.05);  // settle
    scratch.clear();

    unit_note_on((uint8_t)note, (uint8_t)vel);
    render_seconds(scratch, 2.0);
    unit_note_off((uint8_t)note);
    render_seconds(scratch, 1.0);

    double peak = 0, sumsq = 0, dc = 0;
    for (float v : scratch) { peak = std::max(peak, (double)fabsf(v)); sumsq += (double)v * v; dc += v; }
    size_t n = scratch.size() ? scratch.size() : 1;
    double rms = sqrt(sumsq / n);
    dc /= n;
    double l = lufs_integrated(scratch);

    char lbl[16];
    const char* nm;
    if (sweep_param >= 0) {
      const char* sv_s = unit_get_param_str_value((uint8_t)sweep_param, sv);
      if (sv_s) snprintf(lbl, sizeof(lbl), "%d:%s", sv, sv_s);
      else snprintf(lbl, sizeof(lbl), "val=%d", sv);
      nm = lbl;
    } else {
      const char* raw = unit_get_preset_name((uint8_t)p);
      // Preset names may contain spaces ("Ac Tom"); keep every row one
      // whitespace-separated field so the output stays greppable.
      size_t k = 0;
      for (; raw && raw[k] && k + 1 < sizeof(lbl); ++k) lbl[k] = (raw[k] == ' ') ? '_' : raw[k];
      lbl[k] = '\0';
      nm = (raw && *raw) ? lbl : "-";
    }
    printf("%-4u %-9s %9.2f %9.2f %9.2f %9.2f %8.1f\n", (unsigned)p, nm ? nm : "-",
           db(peak), db(rms), l, db(fabs(dc)), db(peak) - db(rms));
    if (g_bad) printf("     !! %llu non-finite samples\n", (unsigned long long)g_bad);

    if (l > -190) { best = std::max(best, l); worst = std::min(worst, l); sum += l; cnt++; }

    if (wav_dir) {
      char path[512];
      snprintf(path, sizeof(path), "%s/%.8s_p%02u.wav", wav_dir, unit_header.name, (unsigned)p);
      FILE* f = fopen(path, "wb");
      if (f) {
        uint32_t ns = scratch.size(), dataBytes = ns * 2;
        auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
        auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
        fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2); w32(kSR); w32(kSR * 4); w16(4); w16(16);
        fwrite("data", 1, 4, f); w32(dataBytes);
        for (float v : scratch) {
          float c = std::max(-1.f, std::min(1.f, v));
          w16((uint16_t)(int16_t)lrintf(c * 32767.f));
        }
        fclose(f);
      }
    }
  }

  if (cnt)
    printf("\nSUMMARY %.8s: loudest %.2f LUFS, quietest %.2f LUFS, mean %.2f LUFS (%u presets)\n",
           unit_header.name, best, worst, sum / cnt, cnt);
  unit_teardown();
  return 0;
}

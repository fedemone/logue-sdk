/**
 * @file test_reverb.cpp
 * @brief Host-side regression tests for NeonAdvancedLabirinto.
 *
 * These tests compile and run the *real* engine header — not a scalar copy of
 * it — against a small stand-in for <arm_neon.h> and the SDK's float_math.h
 * (see test/shim/). Everything but the accuracy of the fast-math
 * approximations is therefore exactly what runs on the drumlogue.
 *
 *   make        # build and run
 *   make run    # same
 *
 * Exit status is 0 only if every check passes.
 */

// The feedback matrices and limiter are implementation details with no public
// accessor, but they are exactly the parts whose maths must not drift. Opening
// the class up is contained to this translation unit.
#define private public
#include "NeonAdvancedLabirinto.h"
#undef private

#include <cstdio>
#include <cmath>
#include <vector>

static const float SR  = 48000.0f;
static const int   BLK = 64;          // drumlogue render block size

static int g_failures = 0;
static int g_checks   = 0;

static void check(bool ok, const char* what, const char* detail = "") {
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("  FAIL  %s %s\n", what, detail);
    } else {
        printf("  ok    %s %s\n", what, detail);
    }
}

/* ------------------------------------------------------------------ stimulus */

static uint32_t g_seed = 22222;
static float noise() {
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
    return (float)g_seed / (float)0xFFFFFFFFu * 2.0f - 1.0f;
}

// A drum-ish hit: pitched thump for the kick, noise burst for the snare.
static void hit(std::vector<float>& L, std::vector<float>& R, int at, bool snare) {
    const float f0 = snare ? 200.0f : 55.0f;
    const float dur = snare ? 0.12f : 0.30f;
    const int n = (int)(dur * SR);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        int k = at + i;
        if (k < 0 || k >= (int)L.size()) continue;
        float t = i / SR;
        float env = expf(-t / (dur * 0.28f));
        phase += 2.0f * (float)M_PI * (f0 * (1.0f + (snare ? 0.0f : 3.0f) * expf(-t * 40.0f))) / SR;
        float s = env * (snare ? 0.35f * sinf(phase) + 0.65f * noise()
                               : 0.95f * sinf(phase) + 0.05f * noise());
        L[k] += s * 0.7f;
        R[k] += s * 0.7f;
    }
}

struct Render {
    std::vector<float> L, R;
    float peak = 0.0f;
    bool  finite = true;
};

static Render render(int preset, const int* overrides, float seconds, bool pattern) {
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(preset);
    if (overrides)
        for (int i = 1; i < k_total; i++)
            if (overrides[i] >= 0) rv.setParameter(i, overrides[i]);

    const int N = (int)(seconds * SR);
    std::vector<float> iL(N, 0.f), iR(N, 0.f);
    if (pattern) {
        const int step = (int)(0.35f * SR);
        for (int k = 0; k * step < N - (int)(0.5f * SR); k++) hit(iL, iR, k * step, k % 2 == 1);
    } else {
        hit(iL, iR, (int)(0.05f * SR), false);
    }

    Render out;
    out.L.assign(N, 0.f);
    out.R.assign(N, 0.f);
    for (int i = 0; i + BLK <= N; i += BLK)
        rv.process(&iL[i], &iR[i], &out.L[i], &out.R[i], BLK);

    for (int i = 0; i < N; i++) {
        if (!std::isfinite(out.L[i]) || !std::isfinite(out.R[i])) { out.finite = false; break; }
        out.peak = fmaxf(out.peak, fmaxf(fabsf(out.L[i]), fabsf(out.R[i])));
    }
    return out;
}

/* -------------------------------------------------------------------- metrics */

// RT60 via Schroeder backward integration over the -5..-25 dB span.
static float rt60(const Render& r, float skipSeconds) {
    const int from = (int)(skipSeconds * SR);
    const int n = (int)r.L.size();
    if (n <= from) return 0.0f;
    std::vector<double> sch(n - from);
    double acc = 0;
    for (int i = n - 1; i >= from; i--) {
        acc += ((double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]) * 0.5;
        sch[i - from] = acc;
    }
    if (sch[0] <= 0) return 0.0f;
    int i5 = -1, i25 = -1;
    for (size_t i = 0; i < sch.size(); i++) {
        double d = 10.0 * log10(sch[i] / sch[0]);
        if (i5  < 0 && d <= -5.0)  i5  = (int)i;
        if (i25 < 0 && d <= -25.0) { i25 = (int)i; break; }
    }
    if (i5 < 0 || i25 <= i5) return 0.0f;
    return 3.0f * (i25 - i5) / SR;      // -20 dB span extrapolated to -60 dB
}

struct Bounce { float periodMs = 0.0f; float strength = 0.0f; };

// A tail that alternates sides every T ms has a stereo-balance envelope of
// period 2T, so its autocorrelation has its first trough at lag T. That trough
// is the ping-pong: unlike a peak it cannot be confused with a harmonic, and
// diffuse (non-bouncing) tails simply do not produce one.
static Bounce bounce(const Render& r, float fromSeconds) {
    const int win = (int)(0.010f * SR);
    std::vector<float> bal;
    for (int i = (int)(fromSeconds * SR); i + win <= (int)r.L.size(); i += win) {
        double sl = 0, sr = 0;
        for (int k = i; k < i + win; k++) {
            sl += (double)r.L[k] * r.L[k];
            sr += (double)r.R[k] * r.R[k];
        }
        float l = (float)sqrt(sl / win), rr = (float)sqrt(sr / win);
        if (l + rr < 1e-6f) continue;
        bal.push_back((l - rr) / (l + rr));
    }
    Bounce b;
    if (bal.size() < 40) return b;
    double mean = 0; for (float v : bal) mean += v; mean /= bal.size();
    double v0 = 0; for (float& v : bal) { v -= (float)mean; v0 += (double)v * v; }
    if (v0 <= 0) return b;

    const int hi = (int)fmin(100.0, bal.size() / 3.0);
    std::vector<float> ac(hi + 1, 0.f);
    for (int lag = 1; lag <= hi; lag++) {
        double s = 0;
        for (size_t i = 0; i + lag < bal.size(); i++) s += (double)bal[i] * bal[i + lag];
        ac[lag] = (float)(s / v0);
    }
    for (int lag = 2; lag < hi; lag++)
        if (ac[lag] < ac[lag - 1] && ac[lag] <= ac[lag + 1] && ac[lag] < -0.15f) {
            b.periodMs = lag * 10.0f;
            b.strength = -ac[lag];
            break;
        }
    return b;
}

static void allDefaults(int* ovr) { for (int i = 0; i < k_total; i++) ovr[i] = -1; }

/* ---------------------------------------------------------------------- tests */

// Both feedback matrices must be orthonormal or the network gains energy on
// every pass and the RT60 the user asked for means nothing.
static void test_matrices() {
    printf("\n[matrices] feedback matrices are energy preserving\n");
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();

    for (int trial = 0; trial < 32; trial++) {
        float32x4_t in[FDN_CHANNELS], out[FDN_CHANNELS];
        double ein = 0;
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float v[NEON_LANES];
            for (int s = 0; s < NEON_LANES; s++) { v[s] = noise(); ein += (double)v[s] * v[s]; }
            in[ch] = vld1q_f32(v);
        }
        for (int which = 0; which < 2; which++) {
            if (which == 0) rv.applyHadamard4(in, out);
            else            rv.applyPingPongMatrix(in, out);
            double eout = 0;
            for (int ch = 0; ch < FDN_CHANNELS; ch++) {
                float v[NEON_LANES];
                vst1q_f32(v, out[ch]);
                for (int s = 0; s < NEON_LANES; s++) eout += (double)v[s] * v[s];
            }
            if (fabs(eout - ein) > 1e-3 * ein) {
                char buf[128];
                snprintf(buf, sizeof(buf), "(matrix %d: in %.4f out %.4f)", which, ein, eout);
                check(false, "energy preserved", buf);
                return;
            }
        }
    }
    check(true, "8-point Hadamard and ping-pong bank swap are orthonormal", "");

    // The ping-pong matrix must land each bank in the *other* bank's slots.
    float32x4_t in[FDN_CHANNELS], out[FDN_CHANNELS];
    for (int ch = 0; ch < FDN_CHANNELS; ch++) in[ch] = vdupq_n_f32(ch < 4 ? 1.0f : 0.0f);
    rv.applyPingPongMatrix(in, out);
    double eLeft = 0, eRight = 0;
    for (int ch = 0; ch < FDN_CHANNELS; ch++) {
        float v[NEON_LANES]; vst1q_f32(v, out[ch]);
        for (int s = 0; s < NEON_LANES; s++)
            (ch < 4 ? eLeft : eRight) += (double)v[s] * v[s];
    }
    check(eRight > 100.0 * eLeft + 1e-9, "left bank writes into the right bank", "");
}

// The limiter must not touch the signal below its knee — that was the whole
// bug: a limiter with gain < 1 everywhere silently shortens every reverb tail.
static void test_limiter() {
    printf("\n[limiter] transparent below the knee, bounded above\n");
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();

    float worstErr = 0.0f, worstAt = 0.0f;
    for (float x = -kLimitThreshold; x <= kLimitThreshold; x += 0.005f) {
        float32x4_t v[1] = { vdupq_n_f32(x) };
        rv.softClipPair(v);
        float o[NEON_LANES]; vst1q_f32(o, v[0]);
        if (fabsf(o[0] - x) > worstErr) { worstErr = fabsf(o[0] - x); worstAt = x; }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "(max error %.2e at x=%.2f)", worstErr, worstAt);
    check(worstErr < 1e-5f, "unity gain below the knee", buf);

    float maxOut = 0.0f;
    bool monotonic = true, prevSet = false; float prev = 0.0f;
    for (float x = 0.0f; x <= 50.0f; x += 0.01f) {
        float32x4_t v[1] = { vdupq_n_f32(x) };
        rv.softClipPair(v);
        float o[NEON_LANES]; vst1q_f32(o, v[0]);
        maxOut = fmaxf(maxOut, o[0]);
        if (prevSet && o[0] < prev - 1e-4f) monotonic = false;
        prev = o[0]; prevSet = true;
    }
    snprintf(buf, sizeof(buf), "(peak %.4f, ceiling %.4f)", maxOut, (float)kLimitCeiling);
    check(maxOut <= kLimitCeiling + 1e-3f && maxOut > kLimitCeiling - 1e-3f,
          "reaches and holds its ceiling", buf);
    check(monotonic, "monotonic (no fold-back)", "");

    float32x4_t v[1] = { vdupq_n_f32(-9.0f) };
    rv.softClipPair(v);
    float o[NEON_LANES]; vst1q_f32(o, v[0]);
    check(o[0] <= -kLimitThreshold && o[0] >= -kLimitCeiling - 1e-3f, "sign preserved", "");
}

// TIME must buy decay time. Before the fix it topped out around 1.4 s whatever
// the setting, because the loop gain was capped near 0.5.
static void test_time_controls_rt60() {
    printf("\n[decay] RT60 tracks TIME\n");
    const int times[] = {10, 30, 50, 70, 100};
    float prev = 0.0f;
    bool monotonic = true;
    float shortest = 0.0f, longest = 0.0f;
    for (int i = 0; i < 5; i++) {
        int ovr[k_total]; allDefaults(ovr);
        ovr[k_time] = times[i];
        Render r = render(2, ovr, 16.0f, false);
        float t = rt60(r, 0.06f);
        printf("        TIME=%3d -> RT60 %5.2f s  (peak %.3f)\n", times[i], t, r.peak);
        if (i && t <= prev) monotonic = false;
        if (i == 0) shortest = t;
        longest = t;
        prev = t;
    }
    check(monotonic, "RT60 increases with TIME", "");
    char buf[96];
    snprintf(buf, sizeof(buf), "(%.2f s)", longest);
    // Before the decay rework the loop gain was capped around 0.5, which pinned
    // RT60 near 1.4 s no matter where TIME sat.
    check(longest > 5.0f, "long TIME gives a genuinely long tail", buf);
    snprintf(buf, sizeof(buf), "(%.2f s -> %.2f s)", shortest, longest);
    check(longest > 6.0f * shortest, "TIME spans a wide range", buf);
    snprintf(buf, sizeof(buf), "(%.2f s)", shortest);
    check(shortest < 1.0f, "short TIME stays short", buf);
}

// PILL=1 must produce a real, periodic left/right alternation whose period is
// what BNCE asks for. The old implementation only reshuffled a random
// channel-to-side map, which is stereo jitter, not a bounce.
static void test_pingpong() {
    printf("\n[ping-pong] PILL=1 bounces, and BNCE sets the bounce time\n");

    const int bnce[] = {100, 190, 300};
    for (int i = 0; i < 3; i++) {
        int ovr[k_total]; allDefaults(ovr);
        ovr[k_bounce] = bnce[i];
        Render r = render(2, ovr, 8.0f, false);
        Bounce b = bounce(r, 0.15f);
        char buf[160];
        snprintf(buf, sizeof(buf), "(BNCE=%d ms: measured %.0f ms, strength %.2f)",
                 bnce[i], b.periodMs, b.strength);
        // The balance envelope is measured in 10 ms buckets and smeared by the
        // spread inside each bank, so allow a generous tolerance on the period.
        bool ok = b.strength > 0.2f && fabsf(b.periodMs - bnce[i]) < 0.45f * bnce[i];
        check(ok, "bounce period follows BNCE", buf);
    }

    // SHMR must no longer move the bounce: it is a shimmer control again.
    int a[k_total], z[k_total];
    allDefaults(a); allDefaults(z);
    a[k_shimmer_freq] = 0;
    z[k_shimmer_freq] = 100;
    Bounce ba = bounce(render(2, a, 8.0f, false), 0.15f);
    Bounce bz = bounce(render(2, z, 8.0f, false), 0.15f);
    char sbuf[160];
    snprintf(sbuf, sizeof(sbuf), "(SHMR 0 -> %.0f ms, SHMR 100 -> %.0f ms)", ba.periodMs, bz.periodMs);
    check(fabsf(ba.periodMs - bz.periodMs) < 1e-3f, "SHMR does not affect the bounce", sbuf);

    // ...and the diffuse modes must not bounce.
    int ovr[k_total]; allDefaults(ovr);
    Render diffuse = render(0, ovr, 8.0f, false);      // foresta, PILL=3
    Bounce db = bounce(diffuse, 0.15f);
    char buf[128];
    snprintf(buf, sizeof(buf), "(foresta strength %.2f)", db.strength);
    check(db.strength < 0.2f, "diffuse presets show no periodic bounce", buf);
}

// Every preset, everything maxed, must stay finite and must not throw several
// times full scale at the mix bus.
static void test_stability_and_level() {
    printf("\n[safety] bounded and finite at the extremes\n");
    const char* names[] = {"foresta", "tempio", "labirinto", "esotico", "stellare"};

    for (int p = 0; p < k_preset_number; p++) {
        int ovr[k_total]; allDefaults(ovr);
        ovr[k_time] = 100; ovr[k_low] = 100; ovr[k_high] = 100;
        ovr[k_damp] = 1000; ovr[k_diffusion] = 100; ovr[k_wide] = 200;
        Render r = render(p, ovr, 12.0f, true);
        char buf[128];
        snprintf(buf, sizeof(buf), "(%s peak %.3f)", names[p], r.peak);
        check(r.finite && r.peak <= kLimitCeiling + 1e-3f, "max settings stay bounded", buf);
    }

    printf("\n[level] presets are level matched at their defaults\n");
    float lo = 1e9f, hi = 0.0f;
    for (int p = 0; p < k_preset_number; p++) {
        Render r = render(p, nullptr, 6.0f, false);
        printf("        %-10s peak %.3f (%+.1f dB)\n", names[p], r.peak, 20 * log10f(r.peak));
        lo = fminf(lo, r.peak);
        hi = fmaxf(hi, r.peak);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "(spread %.1f dB)", 20 * log10f(hi / lo));
    check(20 * log10f(hi / lo) < 4.0f, "preset levels within 4 dB of each other", buf);
    snprintf(buf, sizeof(buf), "(quietest %.1f dB)", 20 * log10f(lo));
    check(lo > 0.35f, "wet output is present, not subtle", buf);
}

// frames not a multiple of 4 exercise processScalar, which feeds the same delay
// lines as the vector path and so must use the same matrix scaling and gains.
static void test_scalar_path() {
    printf("\n[scalar] odd block sizes stay bounded\n");
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(2);
    rv.setParameter(k_time, 100);

    const int N = (int)(8.0f * SR);
    std::vector<float> iL(N, 0.f), iR(N, 0.f), oL(N, 0.f), oR(N, 0.f);
    for (int k = 0; k * (int)(0.35f * SR) < N - (int)(0.5f * SR); k++)
        hit(iL, iR, k * (int)(0.35f * SR), k % 2 == 1);

    float peak = 0.0f;
    bool finite = true;
    const int odd = 37;   // not a multiple of 4
    for (int i = 0; i + odd <= N; i += odd) {
        rv.process(&iL[i], &iR[i], &oL[i], &oR[i], odd);
        for (int k = i; k < i + odd; k++) {
            if (!std::isfinite(oL[k]) || !std::isfinite(oR[k])) { finite = false; break; }
            peak = fmaxf(peak, fmaxf(fabsf(oL[k]), fabsf(oR[k])));
        }
        if (!finite) break;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "(peak %.3f)", peak);
    check(finite && peak <= kLimitCeiling + 1e-3f, "37-frame blocks stay bounded", buf);
}

int main() {
    printf("NeonAdvancedLabirinto host tests\n");
    test_matrices();
    test_limiter();
    test_time_controls_rt60();
    test_pingpong();
    test_stability_and_level();
    test_scalar_path();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

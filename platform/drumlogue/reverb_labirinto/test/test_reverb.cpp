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
#include <cstring>

static const float SR  = 48000.0f;
static const int   BLK = 64;          // drumlogue render block size

// Is this float an infinity or a NaN?
//
// Asked on the exponent bits, not with std::isfinite, because this suite is
// also built with -ffast-math to match how the unit ships — and that implies
// -ffinite-math-only, under which std::isfinite(x) folds to a constant true.
// Every finiteness assertion below was passing vacuously in that build, which
// is precisely the build where the engine's own guards need checking.
static bool notFinite(float x) {
    uint32_t u;
    memcpy(&u, &x, sizeof u);
    return (u & 0x7F800000u) == 0x7F800000u;
}

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
        if (notFinite(out.L[i]) || notFinite(out.R[i])) { out.finite = false; break; }
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
        // The longest delay the engine can ask for, so this also pins the
        // ring buffer: a read that wrapped past the write head would replay
        // stale audio rather than decay.
        ovr[k_bounce] = (int)PINGPONG_MAX_MS;
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

// The host normally sends whole buffers, but the SDK allows shorter ones, and
// frame counts that are not a multiple of 4 land on the remainder path. That
// path must be the *same reverb* — it writes into the same delay lines, so a
// remainder path with different gains does not just sound different for its own
// three samples, it poisons the tail.
//
// The check drives each preset once in 64-frame calls and once one frame at a
// time, so every single sample goes through the remainder path, and compares the
// energy. A hand-written scalar twin used to live here that skipped the colour
// biquad, the metal comb, the cross-feedback and the noise injection; under this
// test it drops 25% of labirinto's energy and 36% of its peak.
static void test_block_size_equivalence() {
    printf("\n[blocks] the reverb does not depend on the host's block size\n");
    const char* names[] = {"foresta", "tempio", "labirinto", "esotico", "stellare"};

    const int N = (int)(6.0f * SR);
    std::vector<float> iL(N, 0.f), iR(N, 0.f);
    for (int k = 0; k * (int)(0.35f * SR) < N - (int)(0.5f * SR); k++)
        hit(iL, iR, k * (int)(0.35f * SR), k % 2 == 1);

    for (int p = 0; p < k_preset_number; p++) {
        double rms[2] = {0.0, 0.0};
        float  peak[2] = {0.0f, 0.0f};
        bool   finite = true;
        const int blk[2] = {BLK, 1};

        for (int v = 0; v < 2; v++) {
            static NeonAdvancedLabirinto rv;
            rv = NeonAdvancedLabirinto();
            rv.init();
            rv.loadPreset(p);

            std::vector<float> oL(N, 0.f), oR(N, 0.f);
            for (int i = 0; i + blk[v] <= N; i += blk[v])
                rv.process(&iL[i], &iR[i], &oL[i], &oR[i], blk[v]);

            for (int i = 0; i < N; i++) {
                if (notFinite(oL[i]) || notFinite(oR[i])) { finite = false; break; }
                rms[v] += (double)oL[i] * oL[i] + (double)oR[i] * oR[i];
                peak[v] = fmaxf(peak[v], fmaxf(fabsf(oL[i]), fabsf(oR[i])));
            }
            rms[v] = sqrt(rms[v] / N);
        }

        const double drift = rms[0] > 0 ? (rms[1] / rms[0] - 1.0) * 100.0 : 100.0;
        char buf[192];
        snprintf(buf, sizeof(buf), "(%s: rms %.5f at %d frames, %.5f at 1 frame, %+.2f%%)",
                 names[p], rms[0], BLK, rms[1], drift);
        check(finite && fabs(drift) < 3.0 &&
                  peak[1] <= kLimitCeiling + 1e-3f,
              "one frame per call matches whole buffers", buf);
    }
}

// VIBR is calibrated in Hz on the panel, so the engine has to deliver Hz. The
// counter it drives is ticked once per NEON block, but was being loaded with a
// period in samples — so every VIBR setting ran four times slow.
static void test_vibr_rate() {
    printf("\n[vibr] the random LFO runs at the rate VIBR asks for\n");
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(0);

    const int   vibr[]   = {1, 10, 30};              // panel value
    const float wantHz[] = {0.1f, 1.0f, 3.0f};       // what it claims to mean
    const float seconds  = 30.0f;
    const int   blocks   = (int)(seconds * SR / NEON_LANES);

    for (int i = 0; i < 3; i++) {
        rv.setParameter(k_vibr, vibr[i]);
        rv.randomLfoCounter = 0;
        int steps = 0;
        float prev = rv.randomLfoValue;
        for (int b = 0; b < blocks; b++) {
            rv.updateRandomLfo();
            if (rv.randomLfoValue != prev) { steps++; prev = rv.randomLfoValue; }
        }
        const float gotHz = steps / seconds;
        char buf[128];
        snprintf(buf, sizeof(buf), "(VIBR=%2d: want %.2f Hz, got %.2f Hz)",
                 vibr[i], wantHz[i], gotHz);
        check(fabsf(gotHz - wantHz[i]) <= 0.1f * wantHz[i] + 1.0f / seconds,
              "LFO steps at the requested rate", buf);
    }
}

// To save cycles the engine only re-derives the filter coefficients on one block
// in eight. With DFSN at 0 there is no modulation, so that refresh is idempotent
// — it recomputes exactly the coefficients already in place — and *when* it
// happens cannot possibly matter. Running the same input twice with the update
// cycle at different phases must therefore give identical output.
//
// It did not: the same flag was doing double duty as "recompute" and "blend
// 0.82*dry into the metal biquad", so metal mode switched signal paths at
// 48000/(4*8) = 1500 Hz. That artifact is smeared by the feedback network and
// invisible in the tail's envelope, but the phase comparison sees it exactly.
static void test_filter_update_phase() {
    printf("\n[metal] the coefficient-update cycle does not colour the signal\n");

    const int N = (int)(4.0f * SR);
    std::vector<float> iL(N, 0.f), iR(N, 0.f);
    for (int k = 0; k * (int)(0.35f * SR) < N - (int)(0.5f * SR); k++)
        hit(iL, iR, k * (int)(0.35f * SR), k % 2 == 1);

    std::vector<float> out[2][2];
    const int phases[2] = {8, 4};                    // half a cycle apart
    for (int p = 0; p < 2; p++) {
        static NeonAdvancedLabirinto rv;
        rv = NeonAdvancedLabirinto();
        rv.init();
        rv.loadPreset(2);                            // labirinto = metal mode
        rv.setParameter(k_diffusion, 0);             // no modulation at all
        rv.filterUpdateCounter = phases[p];

        out[p][0].assign(N, 0.f);
        out[p][1].assign(N, 0.f);
        for (int i = 0; i + BLK <= N; i += BLK)
            rv.process(&iL[i], &iR[i], &out[p][0][i], &out[p][1][i], BLK);
    }

    float worst = 0.0f, peak = 0.0f;
    for (int i = 0; i < N; i++) {
        worst = fmaxf(worst, fabsf(out[0][0][i] - out[1][0][i]));
        worst = fmaxf(worst, fabsf(out[0][1][i] - out[1][1][i]));
        peak  = fmaxf(peak,  fabsf(out[0][0][i]));
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "(worst divergence %.2e against a %.3f peak)", worst, peak);
    check(worst < 1e-6f, "output does not depend on the update phase", buf);
}

// PILL and DFSN both feed the modulation depth, the shimmer gain and the
// modulation rate. Setting them in either order has to land in the same place —
// it did not, because setPillar() derived the shimmer gain from the depth of the
// mode it was leaving and setDiffusion() never revisited either.
static void test_derived_state_order_independent() {
    printf("\n[state] PILL and DFSN commute\n");
    static NeonAdvancedLabirinto a, b;
    bool ok = true;
    char detail[192] = "";

    for (int pill = 0; pill <= 4 && ok; pill++) {
        for (int dfsn = 0; dfsn <= 100 && ok; dfsn += 25) {
            a = NeonAdvancedLabirinto(); a.init(); a.loadPreset(0);
            b = NeonAdvancedLabirinto(); b.init(); b.loadPreset(0);

            a.setParameter(k_pill, pill);
            a.setParameter(k_diffusion, dfsn);

            b.setParameter(k_diffusion, dfsn);
            b.setParameter(k_pill, pill);

            if (a.targetModDepth != b.targetModDepth || a.targetShimmerDepth_ != b.targetShimmerDepth_ ||
                a.modRate != b.modRate) {
                ok = false;
                snprintf(detail, sizeof(detail),
                         "(PILL=%d DFSN=%d: depth %.5f/%.5f shimmer %.5f/%.5f rate %.5f/%.5f)",
                         pill, dfsn, a.targetModDepth, b.targetModDepth,
                         a.targetShimmerDepth_, b.targetShimmerDepth_, a.modRate, b.modRate);
            }
        }
    }
    check(ok, "depth, shimmer gain and mod rate are order independent", detail);

    // ...and DFSN has to actually reach the shimmer, which it never did.
    a = NeonAdvancedLabirinto(); a.init(); a.loadPreset(3);   // esotico, PILL=4
    a.setParameter(k_diffusion, 0);
    const float quiet = a.targetShimmerDepth_;
    a.setParameter(k_diffusion, 100);
    const float loud = a.targetShimmerDepth_;
    char buf[128];
    snprintf(buf, sizeof(buf), "(DFSN 0 -> %.4f, DFSN 100 -> %.4f)", quiet, loud);
    check(loud > quiet, "DFSN moves the shimmer depth", buf);
}

// Render labirinto with the bounce locked to a note division of `bpm`.
// `syncFirst` picks which of the two inputs arrives last, so both the
// setBounceSync() and the setTempo() recompute paths get exercised.
static Render renderTempo(int division, float bpm, bool syncFirst, float seconds) {
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(2);                                  // labirinto: PILL=1
    const uint32_t fixed = (uint32_t)(bpm * 65536.0f); // BPM in 16.16
    if (syncFirst) {
        rv.setParameter(k_bounce_sync, division);
        rv.setTempo(fixed);
    } else {
        rv.setTempo(fixed);
        rv.setParameter(k_bounce_sync, division);
    }

    const int N = (int)(seconds * SR);
    std::vector<float> iL(N, 0.f), iR(N, 0.f);
    hit(iL, iR, (int)(0.05f * SR), false);

    Render out;
    out.L.assign(N, 0.f);
    out.R.assign(N, 0.f);
    for (int i = 0; i + BLK <= N; i += BLK)
        rv.process(&iL[i], &iR[i], &out.L[i], &out.R[i], BLK);
    for (int i = 0; i < N; i++) {
        if (notFinite(out.L[i]) || notFinite(out.R[i])) { out.finite = false; break; }
        out.peak = fmaxf(out.peak, fmaxf(fabsf(out.L[i]), fabsf(out.R[i])));
    }
    return out;
}

// SYNC locks the ping-pong to the transport, which is the point of the feature
// on a drum machine: a bounce at a note division is part of the pattern, a
// bounce at some millisecond value drifts against it.
static void test_tempo_sync() {
    printf("\n[sync] SYNC locks the bounce to the host tempo\n");

    // At 120 BPM a quarter note is 500 ms, so these come out at 125/250/500.
    const int   div[]    = {k_sync_16th, k_sync_8th, k_sync_quarter};
    const char* name[]   = {"1/16", "1/8", "1/4"};
    const float wantMs[] = {125.0f, 250.0f, 500.0f};
    for (int i = 0; i < 3; i++) {
        Bounce b = bounce(renderTempo(div[i], 120.0f, true, 8.0f), 0.15f);
        char buf[160];
        snprintf(buf, sizeof(buf), "(%s at 120 BPM: want %.0f ms, measured %.0f ms, strength %.2f)",
                 name[i], wantMs[i], b.periodMs, b.strength);
        check(b.strength > 0.2f && fabsf(b.periodMs - wantMs[i]) < 0.45f * wantMs[i],
              "a synced division bounces at its note length", buf);
    }

    // ...and the tempo has to actually move it. 1/8 is 333 ms at 90 BPM and
    // 188 ms at 160.
    Bounce slow = bounce(renderTempo(k_sync_8th, 90.0f, true, 8.0f), 0.15f);
    Bounce fast = bounce(renderTempo(k_sync_8th, 160.0f, true, 8.0f), 0.15f);
    char buf[160];
    snprintf(buf, sizeof(buf), "(1/8 at 90 BPM %.0f ms, at 160 BPM %.0f ms)",
             slow.periodMs, fast.periodMs);
    check(slow.periodMs > fast.periodMs + 40.0f, "a slower tempo bounces slower", buf);

    // With SYNC off the tempo must not reach the bounce at all.
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto(); rv.init(); rv.loadPreset(2);
    const float freeMs = rv.bounceTimeMs;
    rv.setTempo((uint32_t)(60.0f * 65536.0f));
    snprintf(buf, sizeof(buf), "(%.0f ms before, %.0f ms after)", freeMs, rv.bounceTimeMs);
    check(rv.bounceTimeMs == freeMs, "tempo is ignored while SYNC is off", buf);

    // Order must not matter, the same way PILL and DFSN must not.
    static NeonAdvancedLabirinto a, b;
    a = NeonAdvancedLabirinto(); a.init(); a.loadPreset(2);
    b = NeonAdvancedLabirinto(); b.init(); b.loadPreset(2);
    const uint32_t t140 = (uint32_t)(140.0f * 65536.0f);
    a.setParameter(k_bounce_sync, k_sync_8th_dotted); a.setTempo(t140);
    b.setTempo(t140); b.setParameter(k_bounce_sync, k_sync_8th_dotted);
    snprintf(buf, sizeof(buf), "(%.2f ms vs %.2f ms)", a.bounceTimeMs, b.bounceTimeMs);
    check(a.bounceTimeMs == b.bounceTimeMs, "SYNC and tempo arrive in either order", buf);

    // A division longer than the delay lines can hold clamps rather than
    // wrapping to something arbitrary: a quarter note at 60 BPM is 1000 ms.
    static NeonAdvancedLabirinto z;
    z = NeonAdvancedLabirinto(); z.init(); z.loadPreset(2);
    z.setParameter(k_bounce_sync, k_sync_quarter);
    z.setTempo((uint32_t)(60.0f * 65536.0f));
    snprintf(buf, sizeof(buf), "(%.0f ms, ceiling %.0f ms)", z.bounceTimeMs, (float)PINGPONG_MAX_MS);
    check(z.bounceTimeMs == PINGPONG_MAX_MS, "a tempo past the buffer clamps", buf);

    // BNCE is inert while synced, and takes over again when SYNC goes off.
    z.setParameter(k_bounce, 60);
    const bool ignored = (z.bounceTimeMs == PINGPONG_MAX_MS);
    z.setParameter(k_bounce_sync, k_sync_off);
    const bool restored = (z.bounceTimeMs == PINGPONG_MIN_MS);
    snprintf(buf, sizeof(buf), "(synced %s, released to %.0f ms)",
             ignored ? "held" : "moved", z.bounceTimeMs);
    check(ignored && restored, "BNCE is ignored while synced and resumes after", buf);
}

/* ------------------------------------------------------------- click hunting */

// A steady tone keeps the tail alive so there is always something to glitch,
// and makes the wet output smooth enough that a one-sample discontinuity shows
// up plainly in the second difference.
static void tone(std::vector<float>& L, std::vector<float>& R) {
    // Two slightly detuned tones, each on its own wrapped accumulator, under a
    // slow tremolo so the input is not pathologically periodic.
    float phL = 0.0f, phR = 0.0f;
    const float twoPi = 2.0f * (float)M_PI;
    for (size_t i = 0; i < L.size(); i++) {
        phL += twoPi * 220.0f  / SR; if (phL > twoPi) phL -= twoPi;
        phR += twoPi * 220.22f / SR; if (phR > twoPi) phR -= twoPi;
        const float env = 0.75f + 0.25f * sinf(twoPi * 0.37f * (i / SR));
        L[i] = 0.5f * env * sinf(phL);
        R[i] = 0.5f * env * sinf(phR);
    }
}

// Worst |y[n] - 2y[n-1] + y[n-2]| across both channels over a window, as a
// fraction of the loudest sample in that window. A click is a step, and a step
// is exactly what the second difference is largest on. The level is taken
// across both channels because that is what a listener hears the click against:
// a small jump in the quiet side of a wide tail is masked by the loud side.
static float roughness(const std::vector<float>& L, const std::vector<float>& R,
                       float t0, float t1) {
    const int a = (int)(t0 * SR), b = (int)(t1 * SR);
    float worst = 0.0f, level = 0.0f;
    for (int i = a; i < b; i++) {
        worst = fmaxf(worst, fabsf(L[i] - 2.0f * L[i - 1] + L[i - 2]));
        worst = fmaxf(worst, fabsf(R[i] - 2.0f * R[i - 1] + R[i - 2]));
        level = fmaxf(level, fmaxf(fabsf(L[i]), fabsf(R[i])));
    }
    return level > 0.0f ? worst / level : 0.0f;
}

// Change one parameter mid-tail and report how rough the output gets.
static float jolt(int fromPreset, int id, int a, int b) {
    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(fromPreset);
    if (id >= 0) rv.setParameter(id, a);

    const int N = (int)(13.0f * SR);
    std::vector<float> iL(N), iR(N), oL(N, 0.f), oR(N, 0.f);
    tone(iL, iR);
    const int flipAt = (int)(10.0f * SR);
    for (int i = 0; i + BLK <= N; i += BLK) {
        if (id >= 0 && i <= flipAt && i + BLK > flipAt) rv.setParameter(id, b);
        rv.process(&iL[i], &iR[i], &oL[i], &oR[i], BLK);
    }
    return roughness(oL, oR, 10.0f, 10.4f);
}

// Nothing the panel can do may step the output. Every gain that multiplies live
// signal is slewed, the two feedback matrices and the two colour stages are
// crossfaded across a mode change, and the delay lengths are rate-limited so a
// glide bends pitch instead of scrubbing the line backwards. Before that work
// the numbers here read 12% for TIME, 9% for PDLY, 28% for the Preset knob off
// labirinto and 167% for stellare to esotico.
static void test_no_clicks_on_parameter_changes() {
    printf("\n[clicks] moving a control does not step the output\n");

    // What the engine's own roughness is when nobody touches anything.
    const float idle = jolt(2, -1, 0, 0);
    char buf[160];
    snprintf(buf, sizeof(buf), "(idle roughness %.1f%% of level)", 100.0f * idle);
    check(idle < 0.02f, "an untouched tail is smooth", buf);

    // A knob move may not be more than a few times rougher than sitting still.
    const float kKnobCeiling = 0.05f;
    struct { int id; const char* nm; int a, b; } moves[] = {
        { k_time,        "TIME 20 -> 90",   20,  90  },
        { k_low,         "LOW 10 -> 90",    10,  90  },
        { k_high,        "HIGH 10 -> 90",   10,  90  },
        { k_damp,        "DAMP 200 -> 900", 200, 900 },
        { k_wide,        "WIDE 0 -> 200",   0,   200 },
        { k_diffusion,   "DFSN 0 -> 100",   0,   100 },
        { k_pill,        "PILL 1 -> 4",     1,   4   },
        { k_pill,        "PILL 4 -> 2",     4,   2   },
        { k_pill,        "PILL 3 -> 0",     3,   0   },
        { k_pre_delay,   "PDLY 0 -> 200",   0,   200 },
        { k_vibr,        "VIBR 1 -> 30",    1,   30  },
        { k_bounce,      "BNCE 60 -> 500",  60,  500 },
        { k_bounce_sync, "SYNC 0 -> 5",     0,   5   },
    };
    for (size_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
        const float r = jolt(2, moves[i].id, moves[i].a, moves[i].b);
        snprintf(buf, sizeof(buf), "(%s: %.1f%% of level)", moves[i].nm, 100.0f * r);
        check(r < kKnobCeiling, "knob move stays smooth", buf);
    }

    // Every preset transition, including the ones that swap the material filter
    // in and out of the feedback loop and change how many channels reach the
    // output. A mode change costs more than a knob move and is allowed more.
    const float kPresetCeiling = 0.08f;
    float worst = 0.0f; int worstFrom = 0, worstTo = 0;
    for (int from = 0; from < k_preset_number; from++) {
        for (int to = 0; to < k_preset_number; to++) {
            if (from == to) continue;
            const float r = jolt(from, k_paramProgram, from, to);
            if (r > worst) { worst = r; worstFrom = from; worstTo = to; }
        }
    }
    snprintf(buf, sizeof(buf), "(worst of 20: %s -> %s at %.1f%% of level)",
             k_preset_names[worstFrom], k_preset_names[worstTo], 100.0f * worst);
    check(worst < kPresetCeiling, "every preset change stays smooth", buf);
}

// A delay length that moves faster than one sample per sample stops the read
// pointer and then reverses it, scrubbing the line backwards with nothing but
// linear interpolation to hide it. The cap keeps every glide a tape bend.
static void test_delay_glide_is_rate_limited() {
    printf("\n[glide] delay lengths never outrun the read pointer\n");

    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(2);

    const int N = (int)(3.0f * SR);
    std::vector<float> iL(N), iR(N), oL(N, 0.f), oR(N, 0.f);
    tone(iL, iR);

    float prev[FDN_CHANNELS];
    float worst = 0.0f;
    bool  set = false;
    // Measurement starts after the engine is awake: the first block carrying
    // signal deliberately snaps the geometry into place (see the [wake] test),
    // and that snap is not a glide.
    const int measureFrom = (int)(0.5f * SR);
    for (int i = 0; i + BLK <= N; i += BLK) {
        // Both ends of BNCE, then a routing change: the three biggest jumps the
        // delay geometry can be asked to make.
        if (i == (int)(1.0f * SR)) rv.setParameter(k_bounce, PINGPONG_MAX_MS);
        if (i == (int)(1.8f * SR)) rv.setParameter(k_bounce, PINGPONG_MIN_MS);
        if (i == (int)(2.4f * SR)) rv.setParameter(k_pill, 4);

        for (int b = 0; b < BLK; b += NEON_LANES) {
            if (set && i >= measureFrom)
                for (int c = 0; c < FDN_CHANNELS; c++) {
                    // samples of delay change per output sample
                    const float d = fabsf(rv.delayTimes[c] - prev[c]) * SR / (float)NEON_LANES;
                    if (d > worst) worst = d;
                }
            for (int c = 0; c < FDN_CHANNELS; c++) prev[c] = rv.delayTimes[c];
            set = true;
            rv.process(&iL[i + b], &iR[i + b], &oL[i + b], &oR[i + b], NEON_LANES);
        }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "(worst %.3f samples/sample, cap %.2f)", worst, DELAY_MAX_RATE);
    check(worst <= DELAY_MAX_RATE * 1.001f, "delay glide respects the rate cap", buf);
    check(worst < 1.0f, "the read pointer never stalls or reverses", buf);
}

// Everything that glides does so to avoid bending audio that is already
// sounding. Waking from silence there is none, so the first hit of a session
// must start on the settings the panel is showing, not slide into them.
static void test_settings_apply_immediately_when_silent() {
    printf("\n[wake] a preset chosen while quiet is in force on the first hit\n");

    static NeonAdvancedLabirinto rv;
    rv = NeonAdvancedLabirinto();
    rv.init();
    rv.loadPreset(2);            // ping-pong geometry, unlike the constructor's

    float before = 0.0f;
    for (int c = 0; c < FDN_CHANNELS; c++)
        before = fmaxf(before, fabsf(rv.delayTimes[c] - rv.targetDelayTimes[c]));

    // One block carrying signal is enough to wake the engine.
    float inL[NEON_LANES] = {0.5f, 0.5f, 0.5f, 0.5f};
    float inR[NEON_LANES] = {0.5f, 0.5f, 0.5f, 0.5f};
    float outL[NEON_LANES], outR[NEON_LANES];
    rv.process(inL, inR, outL, outR, NEON_LANES);

    float after = 0.0f;
    for (int c = 0; c < FDN_CHANNELS; c++)
        after = fmaxf(after, fabsf(rv.delayTimes[c] - rv.targetDelayTimes[c]));

    char buf[160];
    snprintf(buf, sizeof(buf), "(off by %.1f ms before the first block, %.4f ms after)",
             before * 1000.0f, after * 1000.0f);
    check(before > 0.001f && after == 0.0f,
          "the delay geometry is in force on the first block", buf);
}

// A NaN in a feedback delay network never washes out — it circulates forever,
// and on hardware it takes the host's mix bus with it. The watchdog rides on the
// energy sum the APC already computes, so it costs one comparison; what matters
// is that it actually recovers rather than merely noticing.
static void test_watchdog_recovers_from_poisoned_state() {
    printf("\n[watchdog] a poisoned delay line recovers instead of wedging\n");

    // Each kind of poison on its own. Seeding both at once let either guard
    // cover for the other: the engine's threshold test catches an infinity and
    // then clear() wipes the NaN with it, so a NaN check that had stopped
    // working still looked fine.
    struct { const char* what; float value; } poisons[] = {
        { "NaN",      NAN      },
        { "infinity", INFINITY },
        { "runaway",  1e34f    },
    };
    for (auto& poison : poisons)
    for (int p = 0; p < k_preset_number; p++) {
        static NeonAdvancedLabirinto rv;
        rv = NeonAdvancedLabirinto();
        rv.init();
        rv.loadPreset(p);

        const int N = (int)(3.0f * SR);
        std::vector<float> iL(N, 0.f), iR(N, 0.f), oL(N, 0.f), oR(N, 0.f);
        tone(iL, iR);

        const int poisonAt = (int)(1.0f * SR);
        for (int i = 0; i + BLK <= N; i += BLK) {
            if (i <= poisonAt && i + BLK > poisonAt) {
                // Whatever the cause would have been, this is the state it lands
                // in. It has to be planted where the read heads are about to
                // arrive: this used to write at rv.writePos, which is the one
                // place writeDelayLines4 overwrites on the very next block, so
                // the poison was gone before anything could read it and the
                // check passed no matter what the watchdog did.
                for (int c = 0; c < FDN_CHANNELS; c++) {
                    const int d = (int)(rv.delayTimes[c] * SR);
                    for (int k = 0; k < 2 * BLK; k++) {
                        const uint32_t at = (uint32_t)(rv.writePos - d + k) & BUFFER_MASK;
                        rv.delayLine[at].samples[c] = poison.value;
                    }
                }
            }
            rv.process(&iL[i], &iR[i], &oL[i], &oR[i], BLK);
        }

        bool anyBad = false;
        for (int i = 0; i < N; i++)
            if (notFinite(oL[i]) || notFinite(oR[i])) { anyBad = true; break; }

        // And it must come back, not sit silent for ever after.
        double after = 0.0;
        const int from = (int)(2.0f * SR);
        for (int i = from; i < N; i++) after += (double)oL[i] * oL[i] + (double)oR[i] * oR[i];
        after = sqrt(after / (2 * (N - from)));

        char buf[160];
        snprintf(buf, sizeof(buf), "(%s, %s: output stayed finite, rms %.4f one second later)",
                 k_preset_names[p], poison.what, after);
        check(!anyBad && after > 0.01, "recovers from a poisoned delay line", buf);
    }
}

// The loop gain is divided by colourPeakGain() so the *total* round trip is
// what TIME asked for. Understate it and the round trip exceeds unity at the
// colour's resonant frequency: the reverb stops being a reverb and becomes an
// oscillator. Crystal was estimated at 1.09 from the bandpass peak alone while
// the stage reached 1.45, which is how esotico came to self-oscillate. So
// measure the stage rather than reasoning about it.
static void test_colour_peak_gain_is_not_understated() {
    printf("\n[colour] the declared peak gain is not less than the real one\n");

    for (int p = 0; p < k_preset_number; p++) {
        static NeonAdvancedLabirinto rv;
        // Each mode is a fixed-Q design, so DAMP moves the peak without changing
        // its height; sweeping DAMP too only guards against that stopping being
        // true.
        float worst = 0.0f;
        const int damps[] = { 20, 300, 1000 };
        for (int d = 0; d < 3; d++) {
            rv = NeonAdvancedLabirinto();
            rv.init();
            rv.loadPreset(p);
            rv.setParameter(k_damp, damps[d]);

            for (float f = 40.0f; f < 14000.0f; f *= 1.03f) {
                memset(rv.filterState1,   0, sizeof(rv.filterState1));
                memset(rv.filterState2,   0, sizeof(rv.filterState2));
                memset(rv.metalState,     0, sizeof(rv.metalState));
                memset(rv.crystalAPState, 0, sizeof(rv.crystalAPState));

                float ph = 0.0f;
                const int n = 4096;
                for (int i = 0; i < n; i += NEON_LANES) {
                    float32x4_t T[NEON_LANES];
                    for (int t = 0; t < NEON_LANES; t++) {
                        ph += 2.0f * (float)M_PI * f / SR;
                        if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
                        float v[NEON_LANES];
                        for (int c = 0; c < NEON_LANES; c++) v[c] = sinf(ph);
                        T[t] = vld1q_f32(v);
                    }
                    rv.applyColour4(T, 0, rv.liveColour());
                    if (i > n / 2)                       // past the settling time
                        for (int t = 0; t < NEON_LANES; t++) {
                            float o[NEON_LANES];
                            vst1q_f32(o, T[t]);
                            for (int c = 0; c < NEON_LANES; c++)
                                worst = fmaxf(worst, fabsf(o[c]));
                        }
                }
            }
        }
        const float claimed = rv.colourPeakGain();
        char buf[160];
        snprintf(buf, sizeof(buf), "(%s: measured %.3f, declared %.3f)",
                 k_preset_names[p], worst, claimed);
        check(worst <= claimed * 1.005f, "colour peak gain is declared honestly", buf);
    }
}

// The output limiter bounds a runaway, so "did it stay finite" and "did it stay
// under the ceiling" both pass while the unit sits there howling. The thing that
// actually distinguishes a reverb from an oscillator is that its tail keeps
// getting quieter, so check that directly, at the longest decay on offer.
static void test_no_preset_self_oscillates() {
    printf("\n[stability] the tail keeps decaying at maximum TIME\n");

    for (int p = 0; p < k_preset_number; p++) {
        int ovr[k_total]; allDefaults(ovr);
        ovr[k_time] = 100;
        Render r = render(p, ovr, 25.0f, false);

        // Energy in a window early in the tail, and one much later.
        auto rms = [&](float t0, float t1) {
            const int a = (int)(t0 * SR), b = (int)(t1 * SR);
            double acc = 0;
            for (int i = a; i < b; i++)
                acc += (double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i];
            return sqrt(acc / (2 * (b - a)));
        };
        const double early = rms(1.0f, 2.0f);
        const double late  = rms(20.0f, 24.0f);

        char buf[160];
        snprintf(buf, sizeof(buf), "(%s: %.5f at 1-2 s, %.7f at 20-24 s)",
                 k_preset_names[p], early, late);
        // 20 seconds after a single hit, with TIME at its longest, the tail must
        // be a long way down — not merely bounded.
        check(late < early * 0.05, "tail decays instead of sustaining", buf);
    }
}

// A send effect returns wet. If the output correlates with its own input at a
// short lag then some of the input is arriving unprocessed, and because the two
// injection points both sit in the left bank it arrives on one side only. That
// is what made the unit sound like it only reached one speaker: the left channel
// was mostly a copy of the dry signal and the right carried the actual reverb.
static void test_no_dry_leak_into_the_wet_output() {
    printf("\n[wet] the output is the tail, not a copy of the input\n");

    for (int p = 0; p < k_preset_number; p++) {
        static NeonAdvancedLabirinto rv;
        rv = NeonAdvancedLabirinto();
        rv.init();
        rv.loadPreset(p);

        // White noise, so any lag that passes the input through stands out.
        const int N = (int)(4.0f * SR);
        std::vector<float> iL(N), iR(N), oL(N, 0.f), oR(N, 0.f);
        for (int i = 0; i < N; i++) { float v = 0.4f * noise(); iL[i] = v; iR[i] = v; }
        for (int i = 0; i + BLK <= N; i += BLK)
            rv.process(&iL[i], &iR[i], &oL[i], &oR[i], BLK);

        const int from = (int)(1.0f * SR);
        double ii = 0;
        for (int i = from; i < N; i++) ii += (double)iL[i] * iL[i];

        double worst = 0.0;
        for (int lag = 0; lag < (int)(0.040f * SR); lag++) {
            double cl = 0, cr = 0, ol = 0, orr = 0;
            for (int i = from; i < N; i++) {
                cl += (double)oL[i] * iL[i - lag];  cr  += (double)oR[i] * iL[i - lag];
                ol += (double)oL[i] * oL[i];        orr += (double)oR[i] * oR[i];
            }
            worst = fmax(worst, fabs(cl) / sqrt(ol * ii + 1e-30));
            worst = fmax(worst, fabs(cr) / sqrt(orr * ii + 1e-30));
        }

        char buf[160];
        snprintf(buf, sizeof(buf), "(%s: %.3f)", k_preset_names[p], worst);
        // A diffuse tail decorrelates from its own excitation; anything above a
        // few percent is the input itself coming through.
        check(worst < 0.10, "no undelayed input in the wet output", buf);
    }
}

// Neither speaker may be systematically quieter than the other. Ping-pong is
// allowed — required, even — to swing hard from side to side; what it may not do
// is spend a whole part favouring one of them.
static void test_channels_are_balanced() {
    printf("\n[balance] neither side is systematically quieter\n");

    // Every preset at its defaults, and every routing mode, over a played part.
    for (int p = 0; p < k_preset_number; p++) {
        Render r = render(p, nullptr, 8.0f, true);
        double eL = 0, eR = 0;
        for (size_t i = 0; i < r.L.size(); i++) {
            eL += (double)r.L[i] * r.L[i]; eR += (double)r.R[i] * r.R[i];
        }
        const double db = 10.0 * log10((eR + 1e-30) / (eL + 1e-30));
        char buf[160];
        snprintf(buf, sizeof(buf), "(%s: %+.2f dB)", k_preset_names[p], db);
        check(fabs(db) < 3.0, "the two channels carry the same energy", buf);
    }

    for (int pill = 0; pill <= 4; pill++) {
        int ovr[k_total]; allDefaults(ovr);
        ovr[k_pill] = pill;
        Render r = render(0, ovr, 8.0f, true);
        double eL = 0, eR = 0;
        for (size_t i = 0; i < r.L.size(); i++) {
            eL += (double)r.L[i] * r.L[i]; eR += (double)r.R[i] * r.R[i];
        }
        const double db = 10.0 * log10((eR + 1e-30) / (eL + 1e-30));
        char buf[160];
        snprintf(buf, sizeof(buf), "(PILL=%d: %+.2f dB)", pill, db);
        check(fabs(db) < 3.0, "the two channels carry the same energy", buf);
    }

    // The case the alternation exists for: the bounce is often locked to the
    // tempo and the source is a drum machine, so hits land on an exact multiple
    // of the bounce time. A handover scheme that keeps step with that puts every
    // hit into the same bank, and one side stays quiet all the way through.
    // Measured at +16.9 dB before the handover interval and the bank were both
    // drawn fresh each time.
    for (int bnce : {190, 250, 300, 500}) {
        for (float beats : {2.0f, 4.0f}) {
            static NeonAdvancedLabirinto rv;
            rv = NeonAdvancedLabirinto();
            rv.init();
            rv.loadPreset(2);
            rv.setParameter(k_bounce, bnce);

            const float interval = bnce * 0.001f * beats;
            const int N = (int)(20.0f * SR);
            std::vector<float> iL(N, 0.f), iR(N, 0.f), oL(N, 0.f), oR(N, 0.f);
            for (float t = 0.05f; t < 19.0f; t += interval) hit(iL, iR, (int)(t * SR), true);
            for (int i = 0; i + BLK <= N; i += BLK)
                rv.process(&iL[i], &iR[i], &oL[i], &oR[i], BLK);

            double eL = 0, eR = 0;
            for (int i = (int)SR; i < N; i++) {
                eL += (double)oL[i] * oL[i]; eR += (double)oR[i] * oR[i];
            }
            const double db = 10.0 * log10((eR + 1e-30) / (eL + 1e-30));
            char buf[160];
            snprintf(buf, sizeof(buf), "(BNCE=%d, a hit every %.0f bounces: %+.2f dB)",
                     bnce, beats, db);
            check(fabs(db) < 4.0, "the bounce does not lock to the note grid", buf);
        }
    }
}

int main() {
    printf("NeonAdvancedLabirinto host tests\n");
    test_matrices();
    test_limiter();
    test_time_controls_rt60();
    test_pingpong();
    test_stability_and_level();
    test_block_size_equivalence();
    test_vibr_rate();
    test_filter_update_phase();
    test_derived_state_order_independent();
    test_tempo_sync();
    test_no_clicks_on_parameter_changes();
    test_delay_glide_is_rate_limited();
    test_settings_apply_immediately_when_silent();
    test_watchdog_recovers_from_poisoned_state();
    test_colour_peak_gain_is_not_understated();
    test_no_preset_self_oscillates();
    test_no_dry_leak_into_the_wet_output();
    test_channels_are_balanced();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

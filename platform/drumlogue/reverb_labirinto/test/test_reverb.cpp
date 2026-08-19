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
                if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) { finite = false; break; }
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

            if (a.modDepth != b.modDepth || a.shimmerDepth_ != b.shimmerDepth_ ||
                a.modRate != b.modRate) {
                ok = false;
                snprintf(detail, sizeof(detail),
                         "(PILL=%d DFSN=%d: depth %.5f/%.5f shimmer %.5f/%.5f rate %.5f/%.5f)",
                         pill, dfsn, a.modDepth, b.modDepth,
                         a.shimmerDepth_, b.shimmerDepth_, a.modRate, b.modRate);
            }
        }
    }
    check(ok, "depth, shimmer gain and mod rate are order independent", detail);

    // ...and DFSN has to actually reach the shimmer, which it never did.
    a = NeonAdvancedLabirinto(); a.init(); a.loadPreset(3);   // esotico, PILL=4
    a.setParameter(k_diffusion, 0);
    const float quiet = a.shimmerDepth_;
    a.setParameter(k_diffusion, 100);
    const float loud = a.shimmerDepth_;
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
        if (!std::isfinite(out.L[i]) || !std::isfinite(out.R[i])) { out.finite = false; break; }
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

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

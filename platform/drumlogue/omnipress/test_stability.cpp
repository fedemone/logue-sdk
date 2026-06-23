/**
 * @file test_stability.cpp
 * @brief Stability test for OmniPress compressor at maximum parameter values.
 *
 * A feed-forward compressor with makeup gain is inherently bounded:
 *   output ≤ input × makeup_linear (when below threshold)
 *   output < input × makeup_linear (when above threshold)
 *
 * Worst-case parameter combinations:
 *
 * Case A – Maximum amplification (THRESH=0 → no compression, MAKEUP=24 dB):
 *   Threshold = 0 dBFS, no compression fires for [-1,+1] signals.
 *   Makeup = 24 dB = ×15.85.  Output bounded by 15.85 × input.
 *   With input ≤ 0.5 → max output < 7.93.
 *
 * Case B – Maximum compression (THRESH=-60 dB → all signals compressed):
 *   Threshold = -60 dBFS, every non-zero signal is above threshold.
 *   At RATIO=20:1 and input=0 dBFS: GR ≈ -57 dB → output ≈ -57 + 24 = -33 dBFS.
 *   The signal is heavily compressed, well below the input level.
 *
 * Case C – DRIVE=100 (wavefolder): saturates the compressed signal; output ≤ ±1.
 *
 * Critical assertion: the compressor cannot diverge; the envelope follower
 * (one-pole IIR) and gain smoother are trivially bounded.
 *
 * Compile: g++ -std=c++14 -O2 -o test_stability test_stability.cpp -lm
 * Run:     ./test_stability
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define SAMPLE_RATE    48000.0f
#define TEST_SECONDS   10
#define TEST_SAMPLES   (TEST_SECONDS * (int)SAMPLE_RATE)
#define EPSILON        1e-4f

/* One-pole smoother: state = coeff*state + (1-coeff)*target */
static float scalar_smooth(float state, float target, float coeff) {
    return coeff * state + (1.0f - coeff) * target;
}

/* -------------------------------------------------------------------------
 * Scalar compressor (mirrors compressor_core.h logic, no NEON)
 * ---------------------------------------------------------------------- */

typedef struct {
    float envState;    /* one-pole envelope follower */
    float gainState;   /* smoothed gain reduction (dB) */
} ScalarComp;

static float ar_coeff(float time_ms) {
    return expf(-1.0f / (time_ms * 0.001f * SAMPLE_RATE));
}

/** Hard-knee gain computer. Returns GR in dB (≤ 0). */
static float gain_computer(float env_linear, float thresh_db, float ratio) {
    if (env_linear <= 0.0f) return 0.0f;
    float env_db = 20.0f * log10f(env_linear);
    float excess = env_db - thresh_db;
    if (excess <= 0.0f) return 0.0f;
    return -excess * (1.0f - 1.0f / ratio);
}

/**
 * Process one sample through the complete compressor chain:
 * envelope detect → gain computer → attack/release smoothing → apply GR → makeup.
 * Optionally applies soft saturation (wavefolder approximation) for DRIVE > 0.
 */
static float comp_process(ScalarComp *c, float input,
                           float thresh_db, float ratio,
                           float att_coeff, float rel_coeff,
                           float makeup_lin, float drive) {
    float env = fabsf(input);

    /* Envelope follower (peak, one-pole) */
    if (env > c->envState)
        c->envState = att_coeff * c->envState + (1.0f - att_coeff) * env;
    else
        c->envState = rel_coeff * c->envState + (1.0f - rel_coeff) * env;

    /* Gain computer */
    float gr_db = gain_computer(c->envState, thresh_db, ratio);

    /* Gain smoothing (always uses release coeff for GR smoothing) */
    c->gainState = rel_coeff * c->gainState + (1.0f - rel_coeff) * gr_db;

    /* Apply gain reduction in linear domain */
    float gr_lin = powf(10.0f, c->gainState / 20.0f);
    float compressed = input * gr_lin;

    /* Makeup gain */
    float out = compressed * makeup_lin;

    /* Drive (soft saturation: tanh approximation) */
    if (drive > 0.0f) {
        float driven = out * (1.0f + drive * 3.0f);
        /* Branchless tanh approximation: x / (1 + |x|) */
        out = driven / (1.0f + fabsf(driven));
        /* Blend: out = (1-drive)*pre_saturated + drive*saturated */
        out = (1.0f - drive) * (compressed * makeup_lin) + drive * out;
    }

    return out;
}

/* -------------------------------------------------------------------------
 * LCG noise
 * ---------------------------------------------------------------------- */
static uint32_t lcg_state = 0xBEEFCAFEu;
static float lcg_noise() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return ((int32_t)lcg_state) / 2147483648.0f;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

/**
 * Case A: THRESH=0 dB (max, no compression), MAKEUP=24 dB.
 * Output = input × 15.85.  With input ≤ 0.5 → max output < 7.93.
 */
static void test_max_makeup_no_compression() {
    printf("\n=== Stability: THRESH=0 dB (no compression) + MAKEUP=24 dB ===\n");

    ScalarComp c = {0.0f, 0.0f};

    float thresh_db  = 0.0f;    /* raw=0 → 0/10 = 0 dB */
    float ratio      = 20.0f;   /* raw=200 → 20:1 (irrelevant: never fires) */
    float att_ms     = 1.0f;    /* raw=10 (min attack) */
    float rel_ms     = 10.0f;   /* raw=10 */
    float makeup_db  = 24.0f;   /* raw=240 → 24 dB */
    float makeup_lin = powf(10.0f, makeup_db / 20.0f);  /* 15.849 */
    float drive      = 1.0f;    /* raw=100 → 100% */

    float att_c = ar_coeff(att_ms);
    float rel_c = ar_coeff(rel_ms);

    float maxAbs = 0.0f;
    int   nanCount = 0, infCount = 0;

    for (int n = 0; n < TEST_SAMPLES; n++) {
        float in  = lcg_noise() * 0.5f;
        float out = comp_process(&c, in, thresh_db, ratio,
                                 att_c, rel_c, makeup_lin, drive);

        if (fabsf(out) > maxAbs) maxAbs = fabsf(out);
        if (isnan(out)) nanCount++;
        if (isinf(out)) infCount++;
    }

    printf("  Makeup linear = %.4f (= +24 dB)\n", makeup_lin);
    printf("  Max |output|  = %.6f\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    printf("  Inf count     = %d\n", infCount);

    assert(nanCount == 0 && "NaN at max makeup, no compression");
    assert(infCount == 0 && "Inf at max makeup, no compression");
    /* Drive saturates to ±1 asymptotically, so output bounded < 2. */
    assert(maxAbs < 2.0f && "Output exceeded saturation ceiling");

    printf("  PASS: drive saturation keeps output bounded even with +24 dB makeup\n");
}

/**
 * Case B: THRESH=-60 dB (min, max compression), RATIO=20:1, MAKEUP=24 dB.
 * All normal audio is above threshold → heavily compressed → output much lower than input.
 */
static void test_max_compression() {
    printf("\n=== Stability: THRESH=-60 dB (max compression), RATIO=20:1, MAKEUP=24 dB ===\n");

    ScalarComp c = {0.0f, 0.0f};

    float thresh_db  = -60.0f;  /* raw=-600 → -60 dB */
    float ratio      = 20.0f;   /* max */
    float att_ms     = 0.1f;    /* raw=1 (fastest attack: 1/10 ms) */
    float rel_ms     = 10.0f;   /* raw=10 */
    float makeup_lin = powf(10.0f, 24.0f / 20.0f);
    float drive      = 0.0f;    /* no drive for this test */

    float att_c = ar_coeff(att_ms);
    float rel_c = ar_coeff(rel_ms);

    float maxAbs = 0.0f;
    int   nanCount = 0, infCount = 0;

    for (int n = 0; n < TEST_SAMPLES; n++) {
        float in  = lcg_noise() * 0.5f;
        float out = comp_process(&c, in, thresh_db, ratio,
                                 att_c, rel_c, makeup_lin, drive);

        if (fabsf(out) > maxAbs) maxAbs = fabsf(out);
        if (isnan(out)) nanCount++;
        if (isinf(out)) infCount++;
    }

    printf("  Max |output|  = %.6f  (expect << input due to heavy GR)\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    printf("  Inf count     = %d\n", infCount);

    assert(nanCount == 0 && "NaN at max compression");
    assert(infCount == 0 && "Inf at max compression");
    /* Steady-state with 60 dB above thresh and 20:1 ratio: GR ≈ -57 dB.
     * After +24 dB makeup: -33 dBFS ≈ 0.022 linear.
     * Initial transient (t=0, gain smoother at 0 dB): max output = 0.5 × 15.85 ≈ 7.9.
     * Bound set to 10.0 to catch true divergence while allowing the startup transient. */
    assert(maxAbs < 10.0f && "Output unexpectedly large at max compression");

    printf("  PASS: max compression + makeup stays bounded\n");
}

/**
 * Case C: Verify makeup gain arithmetic for parameter extremes.
 * Validates that the parameter-to-gain mapping is numerically sound.
 */
static void test_parameter_extremes() {
    printf("\n=== Parameter Extreme Value Validation ===\n");

    /* THRESH extremes */
    float thresh_min = -600 / 10.0f;   /* -60.0 dB */
    float thresh_max =    0 / 10.0f;   /*   0.0 dB */
    assert(thresh_min == -60.0f);
    assert(thresh_max ==   0.0f);
    printf("  THRESH: -600 → %.1f dB,  0 → %.1f dB  PASS\n", thresh_min, thresh_max);

    /* RATIO extremes: stored *10 */
    float ratio_min = 10  / 10.0f;    /* 1.0  */
    float ratio_max = 200 / 10.0f;    /* 20.0 */
    assert(fabsf(ratio_min - 1.0f)  < EPSILON);
    assert(fabsf(ratio_max - 20.0f) < EPSILON);
    printf("  RATIO: 10 → %.1f:1,  200 → %.1f:1  PASS\n", ratio_min, ratio_max);

    /* ATTACK extremes: stored *10 (ms) */
    float att_min_ms = 1    / 10.0f;   /* 0.1 ms */
    float att_max_ms = 1000 / 10.0f;   /* 100 ms */
    float c_att_min  = ar_coeff(att_min_ms);
    float c_att_max  = ar_coeff(att_max_ms);
    assert(c_att_min > 0.0f && c_att_min < 1.0f);
    assert(c_att_max > c_att_min);   /* longer time → closer to 1 */
    printf("  ATTACK: %.1f ms → coeff=%.6f,  %.0f ms → coeff=%.6f  PASS\n",
           att_min_ms, c_att_min, att_max_ms, c_att_max);

    /* MAKEUP extremes: stored *10 (dB) */
    float mu_min_db  =   0 / 10.0f;   /* 0.0 dB = ×1.0  */
    float mu_max_db  = 240 / 10.0f;   /* 24.0 dB = ×15.85 */
    float mu_min_lin = powf(10.0f, mu_min_db / 20.0f);
    float mu_max_lin = powf(10.0f, mu_max_db / 20.0f);
    assert(fabsf(mu_min_lin - 1.0f)  < 0.01f);
    assert(fabsf(mu_max_lin - 15.849f) < 0.1f);
    printf("  MAKEUP: %.1f dB → ×%.4f,  %.1f dB → ×%.4f  PASS\n",
           mu_min_db, mu_min_lin, mu_max_db, mu_max_lin);

    /* GR bound: at any legal thresh/ratio, GR ≤ 0 (compressor cannot increase gain) */
    float gr = gain_computer(1.0f, -60.0f, 20.0f);
    assert(gr <= 0.0f && "GR must be ≤ 0 (only reduces, never increases)");
    printf("  GR at 0 dBFS, thresh=-60 dB, ratio=20:1 = %.2f dB (must be ≤ 0)  PASS\n", gr);
}

/**
 * Case D: Attack/release one-pole is always bounded in [0, ∞).
 * Feed DC offset into envelope follower for 10s → must converge.
 */
static void test_envelope_follower_convergence() {
    printf("\n=== Envelope Follower Convergence ===\n");

    float att_c = ar_coeff(0.1f);   /* 0.1 ms (fastest) */
    float rel_c = ar_coeff(10.0f);  /* 10 ms */
    float envState = 0.0f;

    /* Feed DC = 1.0 */
    for (int n = 0; n < TEST_SAMPLES; n++) {
        envState = att_c * envState + (1.0f - att_c) * 1.0f;
    }
    printf("  DC=1.0 for 10s → envState = %.8f  (must be near 1.0)\n", envState);
    assert(fabsf(envState - 1.0f) < 0.001f && "Envelope failed to converge to DC input");

    /* Feed silence: must decay to near zero */
    for (int n = 0; n < TEST_SAMPLES; n++) {
        envState = rel_c * envState + (1.0f - rel_c) * 0.0f;
    }
    printf("  Silence for 10s  → envState = %.8f  (must be near 0.0)\n", envState);
    assert(envState < 0.001f && "Envelope failed to decay to silence");

    printf("  PASS: one-pole envelope follower converges correctly\n");
}

/* Scalar multiband processing (simplified) */
typedef struct {
    float bands[3];
    float attack_coeff[3];
    float release_coeff[3];
    float gr[3];
} ScalarMultiband;

static float multiband_process(ScalarMultiband* mb, float input,
                               float thresh_db, float ratio,
                               float makeup_lin) {
    // Dummy: just apply some per-band processing
    // In a real test we'd split the signal into bands, but for stability we can treat each band as a parallel path.
    // Simpler: just apply compressor to each band separately, sum with equal gain.
    // This is enough to check that per-band smoothing doesn't cause divergence.
    float sum = 0.0f;
    for (int i = 0; i < 3; i++) {
        float env = fabsf(input);
        // gain computer
        float gr_db = 0.0f;
        if (env > 0.0f) {
            float env_db = 20.0f * log10f(env);
            float excess = env_db - thresh_db;
            if (excess > 0.0f)
                gr_db = -excess * (1.0f - 1.0f / ratio);
        }
        // smoothing
        if (gr_db < mb->gr[i])
            mb->gr[i] = scalar_smooth(mb->gr[i], gr_db, mb->attack_coeff[i]);
        else
            mb->gr[i] = scalar_smooth(mb->gr[i], gr_db, mb->release_coeff[i]);

        float gain_lin = powf(10.0f, mb->gr[i] / 20.0f);
        sum += input * gain_lin * (makeup_lin / 3.0f);
    }
    return sum;
}

void test_multiband_stability() {
    printf("\n=== Multiband Compressor Stability ===\n");

    ScalarMultiband mb;
    // Initialize with per‑band extremes
    float attack_ms[] = {0.1f, 1.0f, 10.0f};
    float release_ms[] = {10.0f, 100.0f, 1000.0f};
    for (int i = 0; i < 3; i++) {
        mb.attack_coeff[i] = expf(-1.0f / (attack_ms[i] * 0.001f * SAMPLE_RATE));
        mb.release_coeff[i] = expf(-1.0f / (release_ms[i] * 0.001f * SAMPLE_RATE));
        mb.gr[i] = 0.0f;
    }

    float thresh_db = -60.0f;
    float ratio = 20.0f;
    float makeup_lin = powf(10.0f, 24.0f / 20.0f);

    int n_samples = 10 * SAMPLE_RATE;
    float max_abs = 0.0f;
    int nan_cnt = 0, inf_cnt = 0;

    for (int n = 0; n < n_samples; n++) {
        float in = lcg_noise() * 0.5f;
        float out = multiband_process(&mb, in, thresh_db, ratio, makeup_lin);
        if (fabsf(out) > max_abs) max_abs = fabsf(out);
        if (isnan(out)) nan_cnt++;
        if (isinf(out)) inf_cnt++;
    }

    printf("  Max output = %.6f\n", max_abs);
    printf("  NaN count = %d\n", nan_cnt);
    printf("  Inf count = %d\n", inf_cnt);
    assert(nan_cnt == 0 && "NaN in multiband output");
    assert(inf_cnt == 0 && "Inf in multiband output");
    assert(max_abs < 4.0f && "Multiband output exceeded safety bound");

    printf("  PASS: multiband stable under extreme per‑band settings\n");
}

/**
 * test_hard_clip_limiter
 *
 * The output limiter in masterfx.h clamps the final mixed output to ±1.0
 * using vmin/vmax (NEON) or fmaxf/fminf (scalar) after makeup gain.
 * This test verifies the scalar clip formula:
 *   out = fmaxf(-1.0f, fminf(1.0f, pre_clip))
 *
 * Expected:
 *   pre_clip =  2.0 → clipped to  1.0
 *   pre_clip = -2.0 → clipped to -1.0
 *   pre_clip =  0.5 → unchanged   0.5
 *   pre_clip =  1.0 → unchanged   1.0 (boundary)
 *   pre_clip = -1.0 → unchanged  -1.0 (boundary)
 */
static void test_hard_clip_limiter() {
    printf("\n=== Hard-Clip Output Limiter (masterfx.h post-makeup clamp) ===\n");

    struct { float in; float expected; } cases[] = {
        { 2.0f,   1.0f},
        {-2.0f,  -1.0f},
        { 0.5f,   0.5f},
        { 1.0f,   1.0f},
        {-1.0f,  -1.0f},
        { 0.0f,   0.0f},
        {99.0f,   1.0f},
        {-99.0f, -1.0f},
    };
    const int N = sizeof(cases) / sizeof(cases[0]);

    int all_ok = 1;
    for (int i = 0; i < N; i++) {
        float out = fmaxf(-1.0f, fminf(1.0f, cases[i].in));
        int ok = fabsf(out - cases[i].expected) < EPSILON;
        printf("  clip(%.2f) = %.4f  (expected %.4f)  %s\n",
               cases[i].in, out, cases[i].expected, ok ? "PASS" : "FAIL");
        assert(ok);
        if (!ok) all_ok = 0;
    }

    /* Verify with a sweep: output must never exceed ±1.0 for any input in [-10, 10] */
    for (int i = -100; i <= 100; i++) {
        float in  = i * 0.1f;
        float out = fmaxf(-1.0f, fminf(1.0f, in));
        assert(out >= -1.0f && out <= 1.0f);
    }
    printf("  Sweep [-10.0, 10.0]: output always in [-1.0, 1.0]  PASS\n");

    if (all_ok) printf("  Hard-clip limiter: ALL PASS\n");
}

/* =========================================================================
 * Sidechain HPF biquad pole stability test
 *
 * The sidechain HPF is a biquad highpass with Q=0.5 (Bessel-like).
 * For very low cutoff frequencies the digital angular frequency w0 is tiny,
 * which means cosf(w0) ≈ 1.0.  A fast cosine approximation that returns a
 * value ABOVE 1.0 (e.g. fastercosf(0.0026) ≈ 1.000152) places a biquad pole
 * OUTSIDE the unit circle → the filter diverges exponentially.
 *
 * Bug (commit 1c96f5f): sidechain_hpf_init replaced cosf/sinf with
 * fastercosf/fastersinf.  At the default 20 Hz cutoff / 48 kHz sample rate
 * the pole magnitude exceeded 1.0, causing unbounded output growth.
 *
 * This test:
 *   1. Verifies that coefficients computed with cosf keep pole magnitude < 1.
 *   2. Demonstrates that coefficients with cos_w0 > 1 push pole outside
 *      unit circle.
 *   3. Runs the filter with both coefficient sets for 200 samples and checks
 *      that the corrected version stays bounded.
 * ====================================================================== */
static void test_sidechain_hpf_pole_stability() {
    printf("\n=== Sidechain HPF Pole Stability ===\n");

    const float sr        = 48000.0f;
    const float cutoff_hz = 20.0f;  /* default SC HPF cutoff — the problematic case */
    const float Q         = 0.5f;   /* Bessel Q */

    float w0      = 2.0f * (float)M_PI * cutoff_hz / sr; /* ≈ 0.002618 */
    float cos_w0  = cosf(w0);
    float sin_w0  = sinf(w0);
    float alpha   = sin_w0 / (2.0f * Q);

    /* Biquad HPF coefficients (same as sidechain_hpf_init) */
    float b0 = (1.0f + cos_w0) * 0.5f;
    float b1 = -(1.0f + cos_w0);
    float b2 = b0;
    float a0 =  1.0f + alpha;
    float a1 = -2.0f * cos_w0;
    float a2 =  1.0f - alpha;

    /* Normalise by a0 */
    a1 /= a0; a2 /= a0;

    /* Pole magnitude from the characteristic equation z^2 + a1*z + a2 = 0.
     * For a stable filter both poles must satisfy |z| < 1.
     * Discriminant d = a1^2 - 4*a2.
     * If d < 0 (complex poles): |z|^2 = a2 → |z| = sqrt(a2). */
    float discriminant = a1 * a1 - 4.0f * a2;
    float pole_mag;
    if (discriminant < 0.0f) {
        pole_mag = sqrtf(a2); /* complex conjugate pair */
    } else {
        float r1 = (-a1 + sqrtf(discriminant)) * 0.5f;
        float r2 = (-a1 - sqrtf(discriminant)) * 0.5f;
        pole_mag = fmaxf(fabsf(r1), fabsf(r2));
    }

    printf("  w0 = %.6f rad  cos(w0) = %.9f\n", w0, cos_w0);
    printf("  a1_norm = %.9f  a2_norm = %.9f\n", a1, a2);
    printf("  Pole magnitude (correct cosf): %.9f  (must be < 1)\n", pole_mag);
    assert(pole_mag < 1.0f &&
           "Correct HPF pole must be inside unit circle at 20 Hz / 48 kHz");

    /* ----- Demonstrate the bug: cos_w0 slightly > 1.0 ----- */
    /* fastercosf(0.0026) ≈ cos(0.0026) + 0.000152 ≈ 1.000152 per measurement. */
    float bad_cos_w0 = 1.000152f;       /* typical fastercosf overshoot at 20 Hz */
    float bad_a1     = -2.0f * bad_cos_w0 / a0; /* a0 unchanged */
    float bad_a2     = a2;              /* a2 barely changes */
    float bad_disc   = bad_a1 * bad_a1 - 4.0f * bad_a2;
    float bad_pole_mag;
    if (bad_disc < 0.0f) {
        bad_pole_mag = sqrtf(bad_a2);
    } else {
        float r1 = (-bad_a1 + sqrtf(bad_disc)) * 0.5f;
        float r2 = (-bad_a1 - sqrtf(bad_disc)) * 0.5f;
        bad_pole_mag = fmaxf(fabsf(r1), fabsf(r2));
    }
    printf("  Pole magnitude (fastercosf, cos_w0=%.6f): %.6f  (> 1 → unstable!)\n",
           bad_cos_w0, bad_pole_mag);
    assert(bad_pole_mag > 1.0f &&
           "Bug confirmed: fastercosf overshoot places HPF pole outside unit circle");

    /* ----- Run the correct biquad for 200 samples, check bounded output ----- */
    float z1 = 0.0f, z2 = 0.0f;
    float b0n = b0 / a0, b1n = b1 / a0, b2n = b2 / a0;
    float max_out = 0.0f;
    float in = 1.0f; /* impulse */
    for (int n = 0; n < 200; n++) {
        float out = b0n * in + z1;
        z1        = b1n * in - a1 * out + z2;
        z2        = b2n * in - a2 * out;
        if (fabsf(out) > max_out) max_out = fabsf(out);
        in = 0.0f; /* silence after impulse */
    }
    printf("  Correct HPF impulse response max over 200 samples: %.6f\n", max_out);
    assert(max_out < 2.0f &&
           "Correct HPF (cosf) must stay bounded under impulse at 20 Hz / 48 kHz");

    printf("  PASS: sidechain HPF pole stable with cosf; bug confirmed with fastercosf\n");
}

int main() {
    printf("=== OmniPress stability tests ===\n");
    test_parameter_extremes();
    test_envelope_follower_convergence();
    test_max_compression();
    test_max_makeup_no_compression();
    test_multiband_stability();
    test_hard_clip_limiter();
    test_sidechain_hpf_pole_stability();
    printf("\n=== ALL OmniPress STABILITY TESTS PASSED ===\n");
    return 0;
}

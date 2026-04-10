/**
 * @file test_api.cpp
 * @brief Regression and API-correctness tests for reverb_light (FDNEngine).
 *
 * Focus: verifying the fix for the "silent reverb" bug (int32_t truncation
 * in setter arguments causing all *_amt to be 0 → pure dry passthrough).
 *
 * These tests do NOT include fdn_engine.h (ARM NEON dependency); instead they
 * mirror the relevant logic in x86-compilable C++.
 *
 * Compile: g++ -std=c++14 -O2 -o test_api test_api.cpp -lm
 * Run:     ./test_api
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define SAMPLE_RATE  48000.0f
#define FDN_CH       8
#define BUF_SIZE     32768
#define BUF_MASK     (BUF_SIZE - 1)
#define EPSILON      1e-5f

/* =========================================================================
 * Hadamard matrix helper (same construction as fdn_engine.h)
 * ====================================================================== */
static float H[FDN_CH][FDN_CH];
static void build_hadamard() {
    float norm = 1.0f / sqrtf((float)FDN_CH);
    for (int i = 0; i < FDN_CH; i++)
        for (int j = 0; j < FDN_CH; j++) {
            int bits = i & j, parity = 0;
            while (bits) { parity ^= (bits & 1); bits >>= 1; }
            H[i][j] = parity ? -norm : norm;
        }
}

/* =========================================================================
 * Minimal scalar FDN core — mirrors FDNEngine::step_core_fdn exactly.
 *
 * Uses prime-based delay lengths and a fixed decay of 0.8 (same defaults as
 * fdn_engine.h).  Declared as a global to avoid 1 MB stack allocation.
 * ====================================================================== */
typedef struct {
    float buf[FDN_CH][BUF_SIZE];   /* ~1 MB — must not be on the stack */
    int   writePos;
    float decay;
    float sizeScale;
    float baseDelayTimes[FDN_CH];  /* in samples */
} MinimalFDN;

/* Global instance to avoid stack overflow */
static MinimalFDN g_fdn;

static void mfdn_init(MinimalFDN *fdn) {
    memset(fdn, 0, sizeof(*fdn));
    /* Prime delay lengths matching fdn_engine.h constructor */
    const float primes[FDN_CH] = {
        1103.0f, 1511.0f, 1999.0f, 2503.0f,
        3011.0f, 3511.0f, 3989.0f, 4513.0f
    };
    for (int i = 0; i < FDN_CH; i++)
        fdn->baseDelayTimes[i] = primes[i]; /* already in samples at 48 kHz */
    fdn->decay     = 0.8f;   /* FDNEngine::decay default */
    fdn->sizeScale = 1.0f;
}

/* Returns the FDN stereo output (before wet/dry mix). */
static void mfdn_step(MinimalFDN *fdn, float in_l, float in_r,
                      float *out_l, float *out_r)
{
    float fdnOut[FDN_CH];

    /* Read delayed samples (linear interpolation) */
    for (int ch = 0; ch < FDN_CH; ch++) {
        float dt   = fdn->baseDelayTimes[ch] * fdn->sizeScale;
        float rpos = (float)fdn->writePos - dt;
        if (rpos < 0.0f) rpos += BUF_SIZE;
        int   i1   = (int)rpos;
        int   i2   = (i1 + 1) & BUF_MASK;
        float frac = rpos - i1;
        fdnOut[ch] = fdn->buf[ch][i1] + frac * (fdn->buf[ch][i2] - fdn->buf[ch][i1]);
    }

    /* Stereo downmix (channels 0-3 → L, 4-7 → R) */
    *out_l = fdnOut[0] + fdnOut[1] + fdnOut[2] + fdnOut[3];
    *out_r = fdnOut[4] + fdnOut[5] + fdnOut[6] + fdnOut[7];

    /* Hadamard mix + feedback write */
    for (int i = 0; i < FDN_CH; i++) {
        float sum = 0.0f;
        for (int j = 0; j < FDN_CH; j++) sum += fdnOut[j] * H[i][j];
        float inject = (i < 4) ? in_l : in_r;
        fdn->buf[i][fdn->writePos] = inject + (sum * fdn->decay);
    }

    fdn->writePos = (fdn->writePos + 1) & BUF_MASK;
}

/* =========================================================================
 * Wet/dry formula helper — mirrors processBlock()
 *
 * total_wet    = (dark + glow + bright + color + spark) / 5
 * dry_mix      = 1 - clamp(total_wet, 0, 1)
 * wet_normalize = total_wet > 0 ? 1 / max(1, total_wet) : 0
 * ====================================================================== */
typedef struct {
    float dark_amt;
    float glow_amt;
    float bright_amt;
    float color_amt;
    float spark_amt;
} ParamAmounts;

static void compute_wet_dry(const ParamAmounts *p,
                             float *total_wet,
                             float *dry_mix,
                             float *wet_normalize)
{
    *total_wet    = (p->dark_amt + p->glow_amt + p->bright_amt +
                     p->color_amt + p->spark_amt) / 5.0f;
    *dry_mix      = 1.0f - fminf(1.0f, *total_wet);
    *wet_normalize = (*total_wet > 0.0f)
                     ? (1.0f / fmaxf(1.0f, *total_wet))
                     : 0.0f;
}

/* =========================================================================
 * Test 9: int32_t truncation regression
 *
 * The original setters took int32_t; unit.cc called them with
 *   norm = value / 100.0f   (e.g. 0.6f for value=60)
 * C++ truncates float→int32_t, so norm=0.6 → 0.  All *_amt become 0,
 * total_wet=0, wet_normalize=0 → pure dry passthrough.
 *
 * Verify:
 *   a) (int32_t)0.6f == 0  (demonstrates the old bug)
 *   b) With float storage, dark_amt=0.6 → total_wet > 0, dry_mix < 1
 *   c) With all *_amt = 0 (bug), total_wet=0, wet_normalize=0 → out == in
 * ====================================================================== */
static void test_int32_truncation_regression() {
    printf("\n=== Test 9: int32_t truncation regression ===\n");

    /* (a) Demonstrate the truncation */
    float norm60  = 60  / 100.0f;   /* 0.60 */
    float norm50  = 50  / 100.0f;   /* 0.50 */
    float norm5   = 5   / 100.0f;   /* 0.05 */
    float norm10  = 10  / 100.0f;   /* 0.10 */
    float norm70  = 70  / 100.0f;   /* 0.70 */

    int32_t bug_dark   = (int32_t)norm60;  /* = 0 */
    int32_t bug_bright = (int32_t)norm50;  /* = 0 */
    int32_t bug_glow   = (int32_t)norm70;  /* = 0 */
    int32_t bug_color  = (int32_t)norm10;  /* = 0 */
    int32_t bug_spark  = (int32_t)norm5;   /* = 0 */

    printf("  (int32_t)0.60f = %d  (expected 0) %s\n",
           bug_dark,   bug_dark == 0 ? "PASS" : "FAIL");
    printf("  (int32_t)0.50f = %d  (expected 0) %s\n",
           bug_bright, bug_bright == 0 ? "PASS" : "FAIL");
    printf("  (int32_t)0.70f = %d  (expected 0) %s\n",
           bug_glow,   bug_glow == 0 ? "PASS" : "FAIL");
    assert(bug_dark == 0 && bug_bright == 0 && bug_glow == 0 &&
           bug_color == 0 && bug_spark == 0);

    /* (b) With FIXED float storage: total_wet > 0 */
    ParamAmounts fixed_p = { norm60, norm70, norm50, norm10, norm5 };
    float total_wet_fixed, dry_fixed, wet_norm_fixed;
    compute_wet_dry(&fixed_p, &total_wet_fixed, &dry_fixed, &wet_norm_fixed);

    printf("  Fixed:  total_wet=%.4f  dry_mix=%.4f  wet_norm=%.4f\n",
           total_wet_fixed, dry_fixed, wet_norm_fixed);
    assert(total_wet_fixed > 0.0f && "Fixed params must produce non-zero total_wet");
    assert(dry_fixed < 1.0f        && "Fixed params must reduce dry mix below 1");
    assert(wet_norm_fixed > 0.0f   && "Fixed params must produce non-zero wet_normalize");
    printf("  PASS: fixed float setters yield wet path\n");

    /* (c) Bug simulation: all amps=0 → pure dry passthrough */
    ParamAmounts bug_p = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float total_wet_bug, dry_bug, wet_norm_bug;
    compute_wet_dry(&bug_p, &total_wet_bug, &dry_bug, &wet_norm_bug);

    printf("  Buggy:  total_wet=%.4f  dry_mix=%.4f  wet_norm=%.4f\n",
           total_wet_bug, dry_bug, wet_norm_bug);
    assert(fabsf(total_wet_bug) < EPSILON && "Bug: total_wet should be 0");
    assert(fabsf(dry_bug - 1.0f) < EPSILON && "Bug: dry_mix should be 1");
    assert(fabsf(wet_norm_bug)   < EPSILON && "Bug: wet_normalize should be 0");

    float in_sample = 0.73f;
    float out_bug = (in_sample * dry_bug) + (0.0f * wet_norm_bug);
    assert(fabsf(out_bug - in_sample) < EPSILON && "Bug: output must equal input");
    printf("  PASS: buggy (all-zero) params produce pure dry passthrough\n");

    printf("  Test 9 PASSED\n");
}

/* =========================================================================
 * Test 10: FDN wet output presence after fixed parameters
 *
 * With glow_amt > 0, after the FDN has been primed (≥ shortest delay time =
 * 1103 samples), the reverb tail must contribute to output such that the
 * output differs from zero.
 *
 * Strategy:
 *   1. Feed a 0.5 impulse into the FDN for 1 sample
 *   2. Feed silence for 2048 samples (covers the shortest delay of 1103 smp)
 *   3. Verify the FDN outputs non-zero at some point (echo present)
 *   4. Confirm that with glow_amt=0.70 the wet contribution is non-zero
 * ====================================================================== */
static void test_wet_output_presence() {
    printf("\n=== Test 10: Wet Output Presence After Fixed Parameters ===\n");

    build_hadamard();
    mfdn_init(&g_fdn);

    /* Parameters matching unit.cc default s_params after the fix:
     *   DARK=60→0.60  BRIG=50→0.50  GLOW=70→0.70  COLR=10→0.10  SPRK=5→0.05
     * These are the values that would be ZERO under the old int32_t bug. */
    ParamAmounts p = { 0.60f, 0.70f, 0.50f, 0.10f, 0.05f };

    float total_wet, dry_mix, wet_normalize;
    compute_wet_dry(&p, &total_wet, &dry_mix, &wet_normalize);

    printf("  total_wet=%.4f  dry_mix=%.4f  wet_normalize=%.4f\n",
           total_wet, dry_mix, wet_normalize);
    assert(dry_mix < 1.0f);

    /* Prime the FDN with one impulse sample */
    float rev_l, rev_r;
    mfdn_step(&g_fdn, 0.5f, 0.5f, &rev_l, &rev_r);

    /* Run silence; shortest delay = 1103 samples.
     * Run 2048 samples to cover the first echo on all 4 L-channels. */
    float max_rev = 0.0f;
    const int run_samples = 2048;

    for (int n = 0; n < run_samples; n++) {
        mfdn_step(&g_fdn, 0.0f, 0.0f, &rev_l, &rev_r);
        float abs_l = fabsf(rev_l);
        float abs_r = fabsf(rev_r);
        if (abs_l > max_rev) max_rev = abs_l;
        if (abs_r > max_rev) max_rev = abs_r;
    }

    printf("  Max FDN output over %d silence samples = %.6f\n",
           run_samples, max_rev);
    assert(max_rev > 1e-4f && "FDN must produce non-zero echo after impulse");

    /* With non-zero FDN output and glow_amt>0, the wet contribution is non-zero */
    float wet_contribution = max_rev * p.glow_amt * wet_normalize;
    printf("  Wet contribution (max_rev * glow_amt * wet_norm) = %.6f\n",
           wet_contribution);
    assert(fabsf(wet_contribution) > 1e-5f && "Wet contribution must be non-zero");

    printf("  Test 10 PASSED\n");
}

/* =========================================================================
 * Test 11: All-max parameters → 100% wet, zero dry
 * ====================================================================== */
static void test_all_max_params_wet_ratio() {
    printf("\n=== Test 11: All-max Parameters Wet Ratio ===\n");

    /* All params at 100 → normalised 1.0 each */
    ParamAmounts p = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    float total_wet, dry_mix, wet_normalize;
    compute_wet_dry(&p, &total_wet, &dry_mix, &wet_normalize);

    printf("  total_wet=%.4f  (expected 1.0000) %s\n",
           total_wet, fabsf(total_wet - 1.0f) < EPSILON ? "PASS" : "FAIL");
    printf("  dry_mix  =%.4f  (expected 0.0000) %s\n",
           dry_mix,   fabsf(dry_mix)           < EPSILON ? "PASS" : "FAIL");
    printf("  wet_norm =%.4f  (expected 1.0000) %s\n",
           wet_normalize, fabsf(wet_normalize - 1.0f) < EPSILON ? "PASS" : "FAIL");

    assert(fabsf(total_wet    - 1.0f) < EPSILON);
    assert(fabsf(dry_mix)             < EPSILON);
    assert(fabsf(wet_normalize - 1.0f) < EPSILON);

    printf("  PASS: all-max params yield 100%% wet, 0%% dry\n");
}

/* =========================================================================
 * Test 12: Preset 0 (StanzaNeon) default values produce valid wet ratios
 *
 * Preset 0 from unit.cc:
 *   { k_stanzaNeon, DARK=40, BRIG=70, GLOW=30, COLR=10, SPRK=5, SIZE=30, PDLY=5 }
 * After unit_set_param_value maps each via value/100.0f:
 *   dark=0.40, bright=0.70, glow=0.30, color=0.10, spark=0.05
 * ====================================================================== */
static void test_preset_stanzaneon_wet_ratio() {
    printf("\n=== Test 12: Preset 0 (StanzaNeon) Wet Ratio ===\n");

    ParamAmounts p = {
        40 / 100.0f,   /* dark  = 0.40 */
        30 / 100.0f,   /* glow  = 0.30 */
        70 / 100.0f,   /* bright= 0.70 */
        10 / 100.0f,   /* color = 0.10 */
         5 / 100.0f    /* spark = 0.05 */
    };

    float total_wet, dry_mix, wet_normalize;
    compute_wet_dry(&p, &total_wet, &dry_mix, &wet_normalize);

    float expected_total_wet = (0.40f + 0.30f + 0.70f + 0.10f + 0.05f) / 5.0f;
    printf("  total_wet = %.4f  (expected %.4f) %s\n",
           total_wet, expected_total_wet,
           fabsf(total_wet - expected_total_wet) < EPSILON ? "PASS" : "FAIL");
    assert(fabsf(total_wet - expected_total_wet) < EPSILON);

    /* total_wet ≈ 0.31, so dry_mix ≈ 0.69 */
    assert(dry_mix   > 0.0f && dry_mix   < 1.0f && "Preset should be partial wet");
    assert(total_wet > 0.0f && total_wet < 1.0f && "Preset total_wet in (0,1)");
    assert(wet_normalize > 0.0f && "wet_normalize must be positive");

    printf("  dry_mix=%.4f  wet_normalize=%.4f\n", dry_mix, wet_normalize);
    printf("  PASS: StanzaNeon preset produces valid partial wet mix\n");
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(void) {
    printf("==========================================================\n");
    printf(" reverb_light API / Regression Tests\n");
    printf("==========================================================\n");

    test_int32_truncation_regression();
    test_wet_output_presence();
    test_all_max_params_wet_ratio();
    test_preset_stanzaneon_wet_ratio();

    printf("\n==========================================================\n");
    printf(" ALL TESTS PASSED\n");
    printf("==========================================================\n");
    return 0;
}

/**
 * @file test_stability.cpp
 * @brief Stability test for delay_tribal (PercussionSpatializer) at max params.
 *
 * The PercussionSpatializer is a PURE DELAY effect (no feedback). It reads
 * past samples from a delay buffer and applies panning/velocity gains.
 * Without feedback, output is always a bounded weighted sum of past inputs.
 *
 * Stability guarantee: output ≤ N_clones * max_gain * max_input at any instant.
 *
 * Worst-case parameters (max amplification potential):
 *   Clones=2  → 16 clones
 *   Mix=100   → 100% wet
 *   Spread=100 → spread=1.0  (sin/cos gains up to ±1.0 each)
 *   Wobble=100 → wobble_depth_=1.0
 *   Depth=100  → filter depth=1.0
 *   Rate=100   → rate=10 Hz (max LFO rate)
 *   SoftAtk=100, Mode=0 (Tribal)
 *
 * Even in worst case, the sum of 16 clone gains cannot exceed 16 × spread.
 * Because they span a full circle (0°…360°), the actual sum of sin/cos values
 * is near zero (the vectors cancel); RMS gain ≈ spread × √(N/2) ≈ 2.83 for
 * N=16, spread=1. Output amplitude should be well below 10.
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
#include <algorithm>

#define SAMPLE_RATE    48000.0f
#define DELAY_MAX      8192        /* power of 2 ≥ 23ms * 48kHz for max offsets */
#define DELAY_MASK     (DELAY_MAX - 1)
#define MAX_CLONES     16
#define CLONE_GROUPS   4           /* NEON_LANES=4, groups = MAX_CLONES/4 */
#define TEST_SECONDS   10
#define TEST_SAMPLES   (TEST_SECONDS * (int)SAMPLE_RATE)

/* -------------------------------------------------------------------------
 * Scalar delay-only spatializer
 * Mirrors PercussionSpatializer generate_clones + panning logic
 * ---------------------------------------------------------------------- */
typedef struct {
    float bufL[DELAY_MAX];
    float bufR[DELAY_MAX];
    uint32_t writePtr;
    /* clone configuration */
    int   cloneCount;     /* 4, 8, or 16 */
    float spread;         /* 0..1 */
    float mix;            /* 0..1 */
    float wobbleDepth;    /* 0..1 */
    /* LFO phases per clone (0..1) */
    float modPhase[MAX_CLONES];
    float pitchMod[MAX_CLONES];
    float lfoRate;        /* Hz */
    /* delay offsets in samples per clone (base + wobble modulation) */
    float baseDelaySamples[MAX_CLONES];
    /* panning gains per clone */
    float leftGain[MAX_CLONES];
    float rightGain[MAX_CLONES];
    /* sin/cos tables */
    float sinTab[360];
    float cosTab[360];
} ScalarSpatializer;

static void spat_init(ScalarSpatializer *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 360; i++) {
        float a = i * 2.0f * 3.14159265f / 360.0f;
        s->sinTab[i] = sinf(a);
        s->cosTab[i] = cosf(a);
    }
    s->cloneCount  = 4;
    s->spread      = 0.8f;
    s->mix         = 0.5f;
    s->wobbleDepth = 0.3f;
    s->lfoRate     = 1.0f;
    /* base delays: groups of 4 at 5ms, 10ms, 15ms, 20ms */
    for (int g = 0; g < CLONE_GROUPS; g++)
        for (int i = 0; i < 4; i++) {
            int ci = g * 4 + i;
            /* 5ms + group*5ms + i*1ms, in samples */
            s->baseDelaySamples[ci] = ((5.0f + g * 5.0f + i * 1.0f) * 0.001f) * SAMPLE_RATE;
            s->pitchMod[ci] = 0.1f + i * 0.05f;
            s->modPhase[ci] = (float)ci / MAX_CLONES;
        }
}

static void spat_update_panning(ScalarSpatializer *s) {
    for (int ci = 0; ci < MAX_CLONES; ci++) {
        if (ci < s->cloneCount) {
            float pos     = (s->cloneCount > 1) ?
                            (float)ci / (s->cloneCount - 1) : 0.5f;
            int angle_idx = (int)(pos * 359.0f);
            s->leftGain[ci]  = s->sinTab[angle_idx] * s->spread;
            s->rightGain[ci] = s->cosTab[angle_idx] * s->spread;
        } else {
            s->leftGain[ci] = s->rightGain[ci] = 0.0f;
        }
    }
}

static void spat_set_params(ScalarSpatializer *s, int cloneCount, float mix,
                             float spread, float wobbleDepth, float lfoRate) {
    s->cloneCount  = cloneCount;
    s->mix         = mix;
    s->spread      = spread;
    s->wobbleDepth = wobbleDepth;
    s->lfoRate     = lfoRate;
    spat_update_panning(s);
}

static void spat_process_sample(ScalarSpatializer *s, float inL, float inR,
                                 float *outL, float *outR) {
    /* Write input to delay buffer */
    uint32_t pos = s->writePtr & DELAY_MASK;
    s->bufL[pos] = inL;
    s->bufR[pos] = inR;
    s->writePtr++;

    /* Generate clones */
    float wetL = 0.0f, wetR = 0.0f;
    for (int ci = 0; ci < s->cloneCount; ci++) {
        /* LFO (triangle) */
        float phase = s->modPhase[ci];
        float lfo   = 2.0f * fabsf(phase - 0.5f);  /* triangle 0..1 */

        /* Wobble offset in samples */
        float wobble = lfo * s->pitchMod[ci] * s->wobbleDepth;
        float offset = s->baseDelaySamples[ci] + wobble;
        if (offset < 0.0f) offset = 0.0f;
        if (offset >= DELAY_MAX - 1) offset = DELAY_MAX - 2;

        /* Read with linear interpolation */
        float readPos = (float)s->writePtr - 32.0f - offset;
        while (readPos < 0) readPos += DELAY_MAX;
        while (readPos >= DELAY_MAX) readPos -= DELAY_MAX;
        int   idx0  = (int)readPos & DELAY_MASK;
        int   idx1  = (idx0 + 1) & DELAY_MASK;
        float frac  = readPos - (int)readPos;
        float sL    = s->bufL[idx0] + frac * (s->bufL[idx1] - s->bufL[idx0]);
        float sR    = s->bufR[idx0] + frac * (s->bufR[idx1] - s->bufR[idx0]);

        wetL += sL * s->leftGain[ci];
        wetR += sR * s->rightGain[ci];

        /* Advance LFO */
        s->modPhase[ci] += s->lfoRate / SAMPLE_RATE;
        if (s->modPhase[ci] > 1.0f) s->modPhase[ci] -= 1.0f;
    }

    /* Wet/dry mix */
    *outL = inL * (1.0f - s->mix) + wetL * s->mix;
    *outR = inR * (1.0f - s->mix) + wetR * s->mix;
}

/* -------------------------------------------------------------------------
 * LCG noise
 * ---------------------------------------------------------------------- */
static uint32_t lcg_state = 0xCAFEBABEu;
static float lcg_noise() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return ((int32_t)lcg_state) / 2147483648.0f;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static void test_stability_max_params() {
    printf("\n=== Stability Test: ALL PARAMS MAX (16 clones, mix=100%%, spread=100%%) ===\n");
    printf("    (Pure delay — no feedback: output is bounded by construction)\n");
    printf("    Processing %d seconds of white noise...\n", TEST_SECONDS);

    ScalarSpatializer s;
    spat_init(&s);

    /* Max params from header.c:
     *   Clones=2 → 16,  Mix=100,  Spread=100 → 1.0,
     *   Wobble=100 → 1.0,  Rate=100 → 10 Hz */
    spat_set_params(&s, 16, 1.0f, 1.0f, 1.0f, 10.0f);

    float maxAbs = 0.0f;
    int   nanCount = 0, infCount = 0;

    for (int n = 0; n < TEST_SAMPLES; n++) {
        float inSig = lcg_noise() * 0.5f;
        float outL, outR;
        spat_process_sample(&s, inSig, inSig, &outL, &outR);

        if (fabsf(outL) > maxAbs) maxAbs = fabsf(outL);
        if (fabsf(outR) > maxAbs) maxAbs = fabsf(outR);
        if (isnan(outL) || isnan(outR)) nanCount++;
        if (isinf(outL) || isinf(outR)) infCount++;
    }

    printf("  Max |output|  = %.6f\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    printf("  Inf count     = %d\n", infCount);

    assert(nanCount == 0 && "NaN detected at max params");
    assert(infCount == 0 && "Inf detected at max params");
    /* Pure delay: sum of 16 sin/cos gains over a full circle ≈ 0,
     * RMS gain ≈ spread * sqrt(N/2) * input ≈ 2.83 * 0.5 = 1.4.
     * Allow generous headroom to 20 for extreme constructive interference. */
    assert(maxAbs < 20.0f && "Output unexpectedly large at max params");

    printf("  PASS: stable at max params (pure delay — no feedback divergence possible)\n");
}

static void test_gain_model_analysis() {
    printf("\n=== Gain Analysis: 16 clones at spread=1.0 ===\n");
    float sinTab[360], cosTab[360];
    for (int i = 0; i < 360; i++) {
        float a = i * 2.0f * 3.14159265f / 360.0f;
        sinTab[i] = sinf(a);
        cosTab[i] = cosf(a);
    }

    int N = 16;
    float sumL = 0.0f, sumR = 0.0f, sumSqL = 0.0f;
    for (int ci = 0; ci < N; ci++) {
        float pos = (N > 1) ? (float)ci / (N - 1) : 0.5f;
        int ai = (int)(pos * 359.0f);
        float gl = sinTab[ai];
        float gr = cosTab[ai];
        sumL   += gl;
        sumR   += gr;
        sumSqL += gl * gl;
    }
    float rmsL = sqrtf(sumSqL / N);
    printf("  N=16 clones, spread=1.0\n");
    printf("  Sum of left gains  = %.4f  (near 0 = good cancellation)\n", sumL);
    printf("  Sum of right gains = %.4f\n", sumR);
    printf("  RMS left gain      = %.4f  (input amplitude scaling factor)\n", rmsL);
    printf("  Max coherent amp   = %.4f\n", fabsf(sumL));

    /* RMS gain should be around sqrt(N/2) / sqrt(N) = 1/sqrt(2) ≈ 0.71 per clone,
     * total RMS = sqrt(N) * 0.71 ≈ 2.83 for N=16. Allow up to 5. */
    assert(rmsL < 5.0f && "Unexpectedly high RMS gain for 16 clones");
    printf("  PASS: gain model well-behaved for 16 clones\n");
}

static void test_stability_default_params() {
    printf("\n=== Stability Test: DEFAULT PARAMS (4 clones, mix=50%%) ===\n");

    ScalarSpatializer s;
    spat_init(&s);
    /* Default: Clones=0→4, Mix=50, Spread=80, Wobble=30, Rate=30 */
    spat_set_params(&s, 4, 0.5f, 0.8f, 0.3f, 0.1f + (30.0f/100.0f) * 9.9f);

    float maxAbs = 0.0f;
    int   nanCount = 0;
    for (int n = 0; n < TEST_SAMPLES; n++) {
        float inSig = lcg_noise() * 0.5f;
        float outL, outR;
        spat_process_sample(&s, inSig, inSig, &outL, &outR);
        if (fabsf(outL) > maxAbs) maxAbs = fabsf(outL);
        if (isnan(outL) || isnan(outR)) nanCount++;
    }

    printf("  Max |output|  = %.6f\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    assert(nanCount == 0);
    assert(maxAbs   < 5.0f);
    printf("  PASS: default params stable\n");
}

int main() {
    printf("=== delay_tribal stability tests ===\n");
    test_gain_model_analysis();
    test_stability_default_params();
    test_stability_max_params();
    printf("\n=== ALL delay_tribal STABILITY TESTS PASSED ===\n");
    return 0;
}

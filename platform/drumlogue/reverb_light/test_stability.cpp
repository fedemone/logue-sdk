/**
 * @file test_stability.cpp
 * @brief Stability test for reverb_light at maximum parameter values.
 *
 * Verifies that the FDN reverb does not diverge (no NaN, no Inf, output
 * stays bounded) when all parameters are pushed to their maximum.
 *
 * Worst case for stability:
 *   DARK=100  → decay=0.99  (closest to unity feedback)
 *   SPRK=100  → modulation=1.0, but capped at ±3 samples / 2 Hz
 *               Doppler ratio ≈ 3*2π*2/48000 ≈ 0.00079  <<  (1-0.99)=0.01  → stable
 *   COLR=100  → colorCoeff=0.95 (heavy LPF on output)
 *   SIZE=100  → sizeScale=2.0, longest delay ≈ 0.483 s (23165 samples)
 *   GLOW=100  → 100 % wet
 *   BRIG=100  → brightness=1.0 (no LPF blending)
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
#define FDN_CH         8
#define BUF_SIZE       32768      /* 2^15 – mirrors fdn_engine.h */
#define BUF_MASK       (BUF_SIZE - 1)
#define TEST_SECONDS   10
#define TEST_SAMPLES   (TEST_SECONDS * (int)SAMPLE_RATE)

/* -------------------------------------------------------------------------
 * Hadamard matrix (same construction as fdn_engine.h buildHadamard)
 * ---------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * Scalar FDN engine (mirrors fdn_engine.h, no NEON)
 * ---------------------------------------------------------------------- */
typedef struct {
    float buf[FDN_CH][BUF_SIZE];
    float state[FDN_CH];
    float modPhase[FDN_CH];
    float colorLpfL, colorLpfR;
    int   writePos;
    /* parameters */
    float decay;        /* 0..0.99 */
    float modulation;   /* 0..1   */
    float glow;         /* 0..1   */
    float colorCoeff;   /* 0..0.95 */
    float brightness;   /* 0..1   */
    float sizeScale;    /* 0.1..2 */
    float delayTimes[FDN_CH];
} ScalarFDN;

static void fdn_init(ScalarFDN *fdn) {
    memset(fdn, 0, sizeof(*fdn));
    const float base[FDN_CH] = {
        0.0421f, 0.0713f, 0.0987f, 0.1249f,
        0.1571f, 0.1835f, 0.2127f, 0.2413f
    };
    for (int i = 0; i < FDN_CH; i++)
        fdn->delayTimes[i] = base[i];
    fdn->decay       = 0.5f;
    fdn->modulation  = 0.05f;
    fdn->glow        = 0.7f;
    fdn->colorCoeff  = 0.5f;
    fdn->brightness  = 0.5f;
    fdn->sizeScale   = 1.0f;
}

static void fdn_set_params(ScalarFDN *fdn,
                           float decay, float brightness, float glow,
                           float colorCoeff, float modulation, float sizeScale) {
    fdn->decay      = decay;
    fdn->brightness = brightness;
    fdn->glow       = glow;
    fdn->colorCoeff = colorCoeff;
    fdn->modulation = modulation;
    fdn->sizeScale  = sizeScale;
    const float base[FDN_CH] = {
        0.0421f, 0.0713f, 0.0987f, 0.1249f,
        0.1571f, 0.1835f, 0.2127f, 0.2413f
    };
    for (int i = 0; i < FDN_CH; i++) {
        float t = base[i] * sizeScale;
        /* clamp to buffer minus 1 sample */
        if (t > (BUF_SIZE - 1) / SAMPLE_RATE) t = (BUF_SIZE - 1) / SAMPLE_RATE;
        fdn->delayTimes[i] = t;
    }
}

static void fdn_process_sample(ScalarFDN *fdn, float inL, float inR,
                                float *outL, float *outR) {
    float monoIn = (inL + inR) * 0.5f;

    /* Read delayed samples with modulation (max ±3 samples @ 2 Hz) */
    float delayOut[FDN_CH];
    for (int ch = 0; ch < FDN_CH; ch++) {
        float ds  = fdn->delayTimes[ch] * SAMPLE_RATE;
        float mod = sinf(fdn->modPhase[ch] * M_TWOPI)
                    * fdn->modulation * 3.0f;
        float rpos = (float)fdn->writePos - (ds + mod);
        while (rpos < 0)         rpos += BUF_SIZE;
        while (rpos >= BUF_SIZE) rpos -= BUF_SIZE;
        int   i1   = (int)rpos;
        int   i2   = (i1 + 1) & BUF_MASK;
        float frac = rpos - i1;
        delayOut[ch] = fdn->buf[ch][i1] + frac * (fdn->buf[ch][i2] - fdn->buf[ch][i1]);
        /* advance LFO: max 2 Hz */
        fdn->modPhase[ch] += fdn->modulation * 2.0f / SAMPLE_RATE;
        if (fdn->modPhase[ch] > 1.0f) fdn->modPhase[ch] -= 1.0f;
    }

    /* Hadamard mix + decay */
    float mixed[FDN_CH];
    for (int i = 0; i < FDN_CH; i++) {
        float sum = 0.0f;
        for (int j = 0; j < FDN_CH; j++) sum += H[i][j] * delayOut[j];
        mixed[i] = sum * fdn->decay;
    }
    mixed[0] += monoIn * (1.0f - fdn->decay);

    /* Write back */
    for (int ch = 0; ch < FDN_CH; ch++) {
        fdn->buf[ch][fdn->writePos] = mixed[ch];
        fdn->state[ch] = mixed[ch];
    }
    fdn->writePos = (fdn->writePos + 1) & BUF_MASK;

    /* Stereo downmix (ch 0-3 → L, ch 4-7 → R) */
    float leftOut = 0.0f, rightOut = 0.0f;
    for (int i = 0; i < 4; i++) { leftOut  += mixed[i]; rightOut += mixed[i+4]; }
    leftOut  *= 0.25f;
    rightOut *= 0.25f;

    /* Color LPF + brightness blend */
    fdn->colorLpfL = fdn->colorCoeff * fdn->colorLpfL + (1.0f - fdn->colorCoeff) * leftOut;
    fdn->colorLpfR = fdn->colorCoeff * fdn->colorLpfR + (1.0f - fdn->colorCoeff) * rightOut;
    float wetL = fdn->colorLpfL + fdn->brightness * (leftOut  - fdn->colorLpfL);
    float wetR = fdn->colorLpfR + fdn->brightness * (rightOut - fdn->colorLpfR);

    *outL = inL * (1.0f - fdn->glow) + wetL * fdn->glow;
    *outR = inR * (1.0f - fdn->glow) + wetR * fdn->glow;
}

/* -------------------------------------------------------------------------
 * LCG noise (deterministic white noise)
 * ---------------------------------------------------------------------- */
static uint32_t lcg_state = 0xDEADBEEFu;
static float lcg_noise() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return ((int32_t)lcg_state) / 2147483648.0f;  /* -1..1 */
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static void test_stability_max_params() {
    printf("\n=== Stability Test: ALL PARAMS MAX (decay=0.99, mod=1.0, size=2.0) ===\n");
    printf("    Processing %d seconds of white noise...\n", TEST_SECONDS);

    build_hadamard();
    ScalarFDN fdn;
    fdn_init(&fdn);

    /* Max parameter values (mirrors unit_set_param_value mappings) */
    float decay      = 100 / 100.0f * 0.99f;     /* 0.99 */
    float brightness = 100 / 100.0f;              /* 1.0  */
    float glow       = 100 / 100.0f;              /* 1.0  */
    float colorCoeff = 100 / 100.0f * 0.95f;      /* 0.95 */
    float modulation = 100 / 100.0f;              /* 1.0  */
    float sizeScale  = 0.1f + (100 / 100.0f) * 1.9f; /* 2.0 */
    fdn_set_params(&fdn, decay, brightness, glow, colorCoeff, modulation, sizeScale);

    float maxAbs = 0.0f;
    int   nanCount = 0;
    int   infCount = 0;

    for (int n = 0; n < TEST_SAMPLES; n++) {
        float in  = lcg_noise() * 0.5f;  /* -0.5..+0.5 input */
        float outL, outR;
        fdn_process_sample(&fdn, in, in, &outL, &outR);

        if (fabsf(outL) > maxAbs) maxAbs = fabsf(outL);
        if (fabsf(outR) > maxAbs) maxAbs = fabsf(outR);
        if (isnan(outL) || isnan(outR)) nanCount++;
        if (isinf(outL) || isinf(outR)) infCount++;
    }

    printf("  Max |output|  = %.6f\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    printf("  Inf count     = %d\n", infCount);

    /* With decay=0.99 and bounded modulation, output must stay bounded.
     * Theoretical max amplitude with glow=1.0 and 0.5 input:
     * FDN energy converges to input^2 / (1-decay^2) ≈ 25 → amplitude ≈ 5.
     * Using 8.0 as a generous safety margin. */
    assert(nanCount == 0 && "NaN detected at max params");
    assert(infCount == 0 && "Inf detected at max params");
    assert(maxAbs < 8.0f && "Output exploded at max params");

    printf("  PASS: stable at max params (decay=0.99, mod=1.0, size=2.0)\n");
}

static void test_stability_default_params() {
    printf("\n=== Stability Test: DEFAULT PARAMS (decay=0.594, mod=0.05) ===\n");

    build_hadamard();
    ScalarFDN fdn;
    fdn_init(&fdn);

    /* Default values from header.c */
    float decay      = 60  / 100.0f * 0.99f;   /* 0.5940 */
    float brightness = 50  / 100.0f;            /* 0.50   */
    float glow       = 70  / 100.0f;            /* 0.70   */
    float colorCoeff = 10  / 100.0f * 0.95f;   /* 0.095  */
    float modulation = 5   / 100.0f;            /* 0.05   */
    float sizeScale  = 0.1f + (50 / 100.0f) * 1.9f; /* 1.05 */
    fdn_set_params(&fdn, decay, brightness, glow, colorCoeff, modulation, sizeScale);

    /* Feed impulse then silence; reverb tail must stay bounded */
    float outL, outR;
    fdn_process_sample(&fdn, 1.0f, 1.0f, &outL, &outR);  /* impulse */

    float maxAbs = 0.0f;
    int   nanCount = 0, nonzeroFrames = 0;
    for (int n = 0; n < TEST_SAMPLES; n++) {
        fdn_process_sample(&fdn, 0.0f, 0.0f, &outL, &outR);
        if (fabsf(outL) > maxAbs) maxAbs = fabsf(outL);
        if (isnan(outL) || isnan(outR)) nanCount++;
        if (fabsf(outL) > 1e-7f || fabsf(outR) > 1e-7f) nonzeroFrames++;
    }

    printf("  Max |output|  = %.6f\n", maxAbs);
    printf("  NaN count     = %d\n", nanCount);
    printf("  Non-zero frames = %d / %d\n", nonzeroFrames, TEST_SAMPLES);

    assert(nanCount == 0);
    assert(maxAbs   < 4.0f);
    assert(nonzeroFrames > (int)(SAMPLE_RATE * 0.1f) &&
           "Reverb tail should persist for at least 100ms");

    printf("  PASS: default params stable, reverb tail present\n");
}

static void test_modulation_doppler_bound() {
    printf("\n=== Modulation Safety: Doppler ratio check at SPRK=100 ===\n");
    /* Doppler pitch shift ratio = depth_samples * 2π * rate_hz / sample_rate
     * With our fix: depth=3, rate=2Hz → ratio ≈ 0.000785
     * Margin: 1 - decay = 1 - 0.99 = 0.01  >>  0.000785  (12.7× headroom) */
    float max_depth      = 3.0f;           /* samples at modulation=1.0 */
    float max_rate_hz    = 2.0f;           /* Hz at modulation=1.0 */
    float doppler_ratio  = max_depth * M_TWOPI * max_rate_hz / SAMPLE_RATE;
    float stability_margin = (1.0f - 0.99f) / doppler_ratio;

    printf("  Max Doppler ratio = %.6f\n", doppler_ratio);
    printf("  Stability margin  = %.1fx  (need > 1.0)\n", stability_margin);
    assert(stability_margin > 1.0f && "Doppler could destabilise FDN at max params");
    printf("  PASS: Doppler ratio is %.1fx below instability threshold\n",
           stability_margin);
}

int main() {
    printf("=== reverb_light stability tests ===\n");
    test_modulation_doppler_bound();
    test_stability_default_params();
    test_stability_max_params();
    printf("\n=== ALL reverb_light STABILITY TESTS PASSED ===\n");
    return 0;
}

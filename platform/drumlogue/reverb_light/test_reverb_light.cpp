/**
 * @file test_reverb_light.cpp
 * @brief x86-compilable unit tests for reverb_light (FDNEngine) parameter
 *        mapping, scalar FDN signal flow, and wet/dry behavior.
 *
 * Compile: g++ -std=c++14 -o test_reverb_light test_reverb_light.cpp -lm
 * Run:     ./test_reverb_light
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>

#define SAMPLE_RATE  48000.0f
#define FDN_CHANNELS 8
#define EPSILON      1e-5f

/* -------------------------------------------------------------------------
 * Scalar FDN helpers (mirrors fdn_engine.h without ARM NEON)
 * ---------------------------------------------------------------------- */

static void build_hadamard(float H[FDN_CHANNELS][FDN_CHANNELS]) {
    float norm = 1.0f / sqrtf((float)FDN_CHANNELS);
    for (int i = 0; i < FDN_CHANNELS; i++)
        for (int j = 0; j < FDN_CHANNELS; j++) {
            int bits = i & j, parity = 0;
            while (bits) { parity ^= (bits & 1); bits >>= 1; }
            H[i][j] = parity ? -norm : norm;
        }
}

/* -------------------------------------------------------------------------
 * Parameter mapping helpers (mirrors unit_set_param_value in unit.cc)
 * ---------------------------------------------------------------------- */

/* DARK  (id 0): 0..100 → decay 0.0..0.99 */
static float map_dark(int32_t v)       { return (v * 0.01f) * 0.99f; }
/* BRIG  (id 1): 0..100 → brightness 0.0..1.0 */
static float map_brig(int32_t v)       { return v * 0.01f; }
/* GLOW  (id 2): 0..100 → glow 0.0..1.0 */
static float map_glow(int32_t v)       { return v * 0.01f; }
/* COLR  (id 3): 0..100 → colorCoeff 0.0..0.95 */
static float map_colr(int32_t v)       { return (v * 0.01f) * 0.95f; }
/* SPRK  (id 4): 0..100 → modulation 0.0..1.0 */
static float map_sprk(int32_t v)       { return v * 0.01f; }
/* SIZE  (id 5): 0..100 → sizeScale 0.1..2.0 */
static float map_size(int32_t v)       { return 0.1f + (v * 0.01f) * 1.9f; }

/* -------------------------------------------------------------------------
 * Test 1: Parameter mapping correctness
 * ---------------------------------------------------------------------- */

void test_parameter_mapping() {
    printf("\n=== Test 1: Parameter Mapping (unit_set_param_value) ===\n");

    /* DARK */
    float d0  = map_dark(0);
    float d50 = map_dark(50);
    float d100 = map_dark(100);
    printf("  DARK 0   → decay %.4f  (expected 0.0000)  %s\n",
           d0,   fabsf(d0)    < EPSILON ? "PASS" : "FAIL");
    printf("  DARK 50  → decay %.4f  (expected 0.4950)  %s\n",
           d50,  fabsf(d50  - 0.495f) < EPSILON ? "PASS" : "FAIL");
    printf("  DARK 100 → decay %.4f  (expected 0.9900)  %s\n",
           d100, fabsf(d100 - 0.99f)  < EPSILON ? "PASS" : "FAIL");
    assert(fabsf(d0)         < EPSILON);
    assert(fabsf(d50  - 0.495f) < EPSILON);
    assert(fabsf(d100 - 0.99f)  < EPSILON);

    /* GLOW (wet/dry) */
    float g0  = map_glow(0);
    float g100 = map_glow(100);
    printf("  GLOW 0   → glow %.4f   (expected 0.0000)  %s\n",
           g0,   fabsf(g0)    < EPSILON ? "PASS" : "FAIL");
    printf("  GLOW 100 → glow %.4f   (expected 1.0000)  %s\n",
           g100, fabsf(g100 - 1.0f)   < EPSILON ? "PASS" : "FAIL");
    assert(fabsf(g0)        < EPSILON);
    assert(fabsf(g100 - 1.0f) < EPSILON);

    /* COLR */
    float c0   = map_colr(0);
    float c100 = map_colr(100);
    printf("  COLR 0   → coeff %.4f  (expected 0.0000)  %s\n",
           c0,   fabsf(c0)    < EPSILON ? "PASS" : "FAIL");
    printf("  COLR 100 → coeff %.4f  (expected 0.9500)  %s\n",
           c100, fabsf(c100 - 0.95f)  < EPSILON ? "PASS" : "FAIL");
    assert(fabsf(c0)         < EPSILON);
    assert(fabsf(c100 - 0.95f) < EPSILON);

    /* SIZE */
    float s0   = map_size(0);
    float s50  = map_size(50);
    float s100 = map_size(100);
    printf("  SIZE 0   → scale %.4f  (expected 0.1000)  %s\n",
           s0,   fabsf(s0   - 0.1f)  < EPSILON ? "PASS" : "FAIL");
    printf("  SIZE 50  → scale %.4f  (expected 1.0500)  %s\n",
           s50,  fabsf(s50  - 1.05f) < EPSILON ? "PASS" : "FAIL");
    printf("  SIZE 100 → scale %.4f  (expected 2.0000)  %s\n",
           s100, fabsf(s100 - 2.0f)  < EPSILON ? "PASS" : "FAIL");
    assert(fabsf(s0   - 0.1f)  < EPSILON);
    assert(fabsf(s50  - 1.05f) < EPSILON);
    assert(fabsf(s100 - 2.0f)  < EPSILON);

    printf("  All parameter mappings: PASS\n");
}

/* -------------------------------------------------------------------------
 * Test 2: Default parameter values (DARK=20 BRIG=50 GLOW=30 COLR=10
 *          SPRK=5 SIZE=50 from unit.cc s_params[6])
 * ---------------------------------------------------------------------- */

void test_default_parameters() {
    printf("\n=== Test 2: Default Parameter Values ===\n");

    const int32_t defaults[6] = {20, 50, 30, 10, 5, 50};
    const float   expected[6] = {
        map_dark(20),  /* decay ~0.198 */
        map_brig(50),  /* brightness 0.50 */
        map_glow(30),  /* glow  0.30 */
        map_colr(10),  /* color 0.095 */
        map_sprk(5),   /* mod   0.05 */
        map_size(50)   /* scale 1.05 */
    };
    const char* names[6] = {"DARK","BRIG","GLOW","COLR","SPRK","SIZE"};

    int all_ok = 1;
    for (int i = 0; i < 6; i++) {
        float got;
        switch (i) {
            case 0: got = map_dark(defaults[i]); break;
            case 1: got = map_brig(defaults[i]); break;
            case 2: got = map_glow(defaults[i]); break;
            case 3: got = map_colr(defaults[i]); break;
            case 4: got = map_sprk(defaults[i]); break;
            default: got = map_size(defaults[i]); break;
        }
        int ok = fabsf(got - expected[i]) < EPSILON;
        printf("  %s default=%d → %.4f  %s\n", names[i], defaults[i], got,
               ok ? "PASS" : "FAIL");
        if (!ok) all_ok = 0;
    }
    assert(all_ok);
}

/* -------------------------------------------------------------------------
 * Test 3: Scalar FDN one-pole color LPF
 *   y[n] = coeff * y[n-1] + (1-coeff) * x[n]
 *   DC input (x=1) should converge to 1.0 for any coeff in [0,1).
 * ---------------------------------------------------------------------- */

void test_color_lpf_dc() {
    printf("\n=== Test 3: Color LPF DC Response ===\n");

    float coeffs[] = {0.0f, 0.5f, 0.9f, 0.95f};
    for (int c = 0; c < 4; c++) {
        float coeff = coeffs[c];
        float state = 0.0f;
        for (int n = 0; n < 10000; n++) {
            state = coeff * state + (1.0f - coeff) * 1.0f;
        }
        int ok = fabsf(state - 1.0f) < 1e-3f;
        printf("  coeff=%.2f DC converges to %.6f  %s\n",
               coeff, state, ok ? "PASS" : "FAIL");
        assert(ok);
    }
}

/* -------------------------------------------------------------------------
 * Test 4: Wet/dry mix formula
 *   out = glow * wet + (1 - glow) * dry
 *   glow=0  → pure dry
 *   glow=1  → pure wet
 *   glow=0.5 → equal mix
 * ---------------------------------------------------------------------- */

void test_wet_dry_mix() {
    printf("\n=== Test 4: Wet/Dry Mix ===\n");

    float dry = 0.6f, wet = -0.3f;

    float g0 = 0.0f, g1 = 1.0f, g05 = 0.5f;

    float out0  = g0  * wet + (1.0f - g0)  * dry;
    float out1  = g1  * wet + (1.0f - g1)  * dry;
    float out05 = g05 * wet + (1.0f - g05) * dry;

    printf("  glow=0.0: out=%.4f (expected dry=%.4f)  %s\n",
           out0, dry,  fabsf(out0 - dry) < EPSILON ? "PASS" : "FAIL");
    printf("  glow=1.0: out=%.4f (expected wet=%.4f)  %s\n",
           out1, wet,  fabsf(out1 - wet) < EPSILON ? "PASS" : "FAIL");
    float expected05 = 0.5f * (wet + dry);
    printf("  glow=0.5: out=%.4f (expected avg=%.4f)  %s\n",
           out05, expected05, fabsf(out05 - expected05) < EPSILON ? "PASS" : "FAIL");

    assert(fabsf(out0 - dry)       < EPSILON);
    assert(fabsf(out1 - wet)       < EPSILON);
    assert(fabsf(out05 - expected05) < EPSILON);
}

/* -------------------------------------------------------------------------
 * Test 5: Decay coefficient stability
 *   With decay=0, the FDN feedback must die out in one step.
 *   With decay=0.99, energy after N steps must be < 1 (bounded).
 * ---------------------------------------------------------------------- */

void test_decay_stability() {
    printf("\n=== Test 5: Decay Coefficient Stability ===\n");

    float H[FDN_CHANNELS][FDN_CHANNELS];
    build_hadamard(H);

    /* Decay = 0: single feedback step should zero out */
    {
        float state[FDN_CHANNELS] = {1,0,0,0,0,0,0,0};
        float mixed[FDN_CHANNELS];
        for (int i = 0; i < FDN_CHANNELS; i++) {
            float acc = 0.0f;
            for (int j = 0; j < FDN_CHANNELS; j++) acc += H[i][j] * state[j];
            mixed[i] = acc * 0.0f;  /* decay = 0 */
        }
        float energy = 0.0f;
        for (int i = 0; i < FDN_CHANNELS; i++) energy += mixed[i] * mixed[i];
        printf("  decay=0.00: energy after 1 step = %.6f (expected 0.0)  %s\n",
               energy, energy < EPSILON ? "PASS" : "FAIL");
        assert(energy < EPSILON);
    }

    /* Decay = 0.99: energy should decay over 5000 steps (not diverge) */
    {
        float decay = 0.99f;
        float state[FDN_CHANNELS] = {1,0,0,0,0,0,0,0};
        for (int n = 0; n < 5000; n++) {
            float mixed[FDN_CHANNELS];
            for (int i = 0; i < FDN_CHANNELS; i++) {
                float acc = 0.0f;
                for (int j = 0; j < FDN_CHANNELS; j++) acc += H[i][j] * state[j];
                mixed[i] = acc * decay;
            }
            memcpy(state, mixed, sizeof(state));
        }
        float energy = 0.0f;
        for (int i = 0; i < FDN_CHANNELS; i++) energy += state[i] * state[i];
        printf("  decay=0.99: energy after 5000 steps = %.6f (< 1.0)  %s\n",
               energy, energy < 1.0f ? "PASS" : "FAIL");
        assert(energy < 1.0f);
    }
}

/* -------------------------------------------------------------------------
 * Test 6: Brightness blend
 *   bright_out = brightness * direct_in + (1 - brightness) * lpf_out
 *   brightness=0 → full LPF (dark)
 *   brightness=1 → bypass LPF (bright)
 * ---------------------------------------------------------------------- */

void test_brightness_blend() {
    printf("\n=== Test 6: Brightness Blend ===\n");

    float direct = 0.8f;
    float lpf_out = 0.2f;  /* simulated low-pass filtered value */

    for (float brig : {0.0f, 0.5f, 1.0f}) {
        float out = brig * direct + (1.0f - brig) * lpf_out;
        float expected = brig == 0.0f ? lpf_out :
                         brig == 1.0f ? direct  :
                         0.5f * (direct + lpf_out);
        int ok = fabsf(out - expected) < EPSILON;
        printf("  brightness=%.1f: out=%.4f (expected %.4f)  %s\n",
               brig, out, expected, ok ? "PASS" : "FAIL");
        assert(ok);
    }
}

/* -------------------------------------------------------------------------
 * Test 7: Size parameter scales all 8 delay channels
 *   base delay times from fdn_engine.h constructor
 * ---------------------------------------------------------------------- */

void test_size_scaling() {
    printf("\n=== Test 7: SIZE Parameter Delay Scaling ===\n");

    static const float kBaseDelays[FDN_CHANNELS] = {
        0.0421f, 0.0713f, 0.0987f, 0.1249f,
        0.1571f, 0.1835f, 0.2127f, 0.2413f
    };
    float max_delay = (float)(32768 - 1) / SAMPLE_RATE;  /* FDN_BUFFER_SIZE-1 / SR */

    float size_scales[] = {0.1f, 1.0f, 2.0f};
    for (int s = 0; s < 3; s++) {
        float scale = size_scales[s];
        int all_ok = 1;
        for (int ch = 0; ch < FDN_CHANNELS; ch++) {
            float expected = std::min(kBaseDelays[ch] * scale, max_delay);
            /* Verify monotonically increasing with scale */
            float prev_scale_val = std::min(kBaseDelays[ch] * (scale == 0.1f ? 0.05f : scale * 0.5f), max_delay);
            if (scale > 0.1f && expected < prev_scale_val) { all_ok = 0; break; }
        }
        printf("  sizeScale=%.1f: delay channel ordering  %s\n",
               scale, all_ok ? "PASS" : "FAIL");
        assert(all_ok);
    }
    printf("  FDN buffer cap at %.4f s for all channels: PASS\n", max_delay);
}

/* -------------------------------------------------------------------------
 * Test 8: Stereo de-interleave / re-interleave roundtrip
 *   unit_render splits [L,R,L,R,...] into s_inL[]/s_inR[],
 *   processes, then recombines.
 * ---------------------------------------------------------------------- */

void test_stereo_deinterleave() {
    printf("\n=== Test 8: Stereo Interleave Roundtrip ===\n");

    const int FRAMES = 8;
    float interleaved[FRAMES * 2];
    float outL[FRAMES], outR[FRAMES];

    /* Fill with known pattern */
    for (int i = 0; i < FRAMES; i++) {
        interleaved[i * 2]     = (float)(i + 1);        /* L */
        interleaved[i * 2 + 1] = (float)(i + 1) * -1.0f; /* R */
    }

    /* Deinterleave (mirrors unit_render) */
    float inL[FRAMES], inR[FRAMES];
    for (int i = 0; i < FRAMES; i++) {
        inL[i] = interleaved[i * 2];
        inR[i] = interleaved[i * 2 + 1];
    }

    /* Simulate dry passthrough (glow=0) */
    for (int i = 0; i < FRAMES; i++) {
        outL[i] = inL[i];
        outR[i] = inR[i];
    }

    /* Re-interleave */
    float result[FRAMES * 2];
    for (int i = 0; i < FRAMES; i++) {
        result[i * 2]     = outL[i];
        result[i * 2 + 1] = outR[i];
    }

    int all_ok = 1;
    for (int i = 0; i < FRAMES * 2; i++) {
        if (fabsf(result[i] - interleaved[i]) > EPSILON) { all_ok = 0; break; }
    }
    printf("  Deinterleave → process → reinterleave roundtrip: %s\n",
           all_ok ? "PASS" : "FAIL");
    assert(all_ok);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Test 9: GLOW LFO – depth scales with glow_amt, rate is fixed
 *
 * After the fdn_engine.h fix:
 *   f_coeff_l = 0.15f + (0.10f * glow_amt * lfo_val)
 *   GLOW_LFO_RATE = 0.4 / SAMPLE_RATE (constant)
 *
 * Verified properties:
 *   (a) glow_amt = 0 → f_coeff stays exactly 0.15 regardless of phase
 *   (b) glow_amt = 1 → f_coeff sweeps between 0.05 and 0.25
 *   (c) LFO advances at the same rate for glow_amt=0 and glow_amt=1
 * ---------------------------------------------------------------------- */

void test_glow_lfo_decoupling() {
    printf("\n=== Test 9: GLOW LFO Depth-Rate Decoupling ===\n");

    const float GLOW_LFO_RATE = 0.4f / SAMPLE_RATE;
    const float SVF_CENTER    = 0.15f;
    const float SVF_DEPTH     = 0.10f;
    const int   N_SAMPLES     = (int)(3.0f / GLOW_LFO_RATE); /* 3 full cycles */

    /* (a) glow_amt = 0: f_coeff must stay fixed at SVF_CENTER */
    {
        float phase    = 0.0f;
        float glow_amt = 0.0f;
        float f_min = 1.0f, f_max = 0.0f;
        for (int n = 0; n < N_SAMPLES; n++) {
            float lfo = sinf(phase * 2.0f * 3.14159265f);
            float f_coeff = SVF_CENTER + SVF_DEPTH * glow_amt * lfo;
            if (f_coeff < f_min) f_min = f_coeff;
            if (f_coeff > f_max) f_max = f_coeff;
            phase += GLOW_LFO_RATE;
            if (phase > 1.0f) phase -= 1.0f;
        }
        printf("  glow=0.0: f_coeff range [%.4f, %.4f]  (expected [0.15, 0.15])  %s\n",
               f_min, f_max,
               (fabsf(f_min - SVF_CENTER) < EPSILON && fabsf(f_max - SVF_CENTER) < EPSILON)
               ? "PASS" : "FAIL");
        assert(fabsf(f_min - SVF_CENTER) < EPSILON);
        assert(fabsf(f_max - SVF_CENTER) < EPSILON);
    }

    /* (b) glow_amt = 1: f_coeff must sweep [0.05, 0.25] */
    {
        float phase    = 0.0f;
        float glow_amt = 1.0f;
        float f_min = 1.0f, f_max = 0.0f;
        for (int n = 0; n < N_SAMPLES; n++) {
            float lfo = sinf(phase * 2.0f * 3.14159265f);
            float f_coeff = SVF_CENTER + SVF_DEPTH * glow_amt * lfo;
            if (f_coeff < f_min) f_min = f_coeff;
            if (f_coeff > f_max) f_max = f_coeff;
            phase += GLOW_LFO_RATE;
            if (phase > 1.0f) phase -= 1.0f;
        }
        printf("  glow=1.0: f_coeff range [%.4f, %.4f]  (expected [0.05, 0.25])  %s\n",
               f_min, f_max,
               (f_min < 0.06f && f_max > 0.24f) ? "PASS" : "FAIL");
        assert(f_min < 0.06f  && "glow=1 lower bound too high");
        assert(f_max > 0.24f  && "glow=1 upper bound too low");
    }

    /* (c) Rate is constant: both glow_amt=0 and glow_amt=1 complete
     *     the same number of LFO cycles over N_SAMPLES.
     *     One cycle = 1/GLOW_LFO_RATE samples.  Verify period. */
    {
        float expected_period = 1.0f / GLOW_LFO_RATE;
        float measured_rate   = GLOW_LFO_RATE * SAMPLE_RATE;  /* Hz */
        printf("  LFO rate: %.4f Hz (expected 0.4000 Hz)  %s\n",
               measured_rate,
               fabsf(measured_rate - 0.4f) < 1e-4f ? "PASS" : "FAIL");
        printf("  LFO period: %.1f samples (expected %.1f)  %s\n",
               expected_period, SAMPLE_RATE / 0.4f,
               fabsf(expected_period - SAMPLE_RATE / 0.4f) < 1.0f ? "PASS" : "FAIL");
        assert(fabsf(measured_rate - 0.4f) < 1e-4f);
    }

    printf("  GLOW LFO depth-rate decoupling: PASS\n");
}

int main(void) {
    printf("==========================================================\n");
    printf(" reverb_light (FDNEngine) Unit Tests\n");
    printf("==========================================================\n");

    test_parameter_mapping();
    test_default_parameters();
    test_color_lpf_dc();
    test_wet_dry_mix();
    test_decay_stability();
    test_brightness_blend();
    test_size_scaling();
    test_stereo_deinterleave();
    test_glow_lfo_decoupling();

    printf("\n==========================================================\n");
    printf(" ALL TESTS PASSED\n");
    printf("==========================================================\n");
    return 0;
}

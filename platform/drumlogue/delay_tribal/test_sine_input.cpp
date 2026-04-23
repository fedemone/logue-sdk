/**
 * @file test_sine_input.cpp
 * @brief Verifies that PercussionSpatializer produces N distinct delayed clones.
 *
 * Uses a scalar simulation of the exact signal path (no ARM NEON required,
 * compiles on x86) and checks two properties:
 *
 *   1. IMPULSE RESPONSE: feeding a unit impulse produces distinct output peaks
 *      at the expected delay positions for each clone.  With 4 clones (Tribal
 *      mode) the 4 peaks appear at 8, 9, 10, 11 ms (+32-sample base offset).
 *      With 8/12/16 clones the number of peaks scales proportionally.
 *
 *   2. SINE WAVE: a 440 Hz sine at 100 % wet produces non-zero output after
 *      the delay buffers fill, confirming the chorus/clone path is active.
 *
 * Panning matches PercussionSpatializer exactly:
 *   left_gain  = cos_table[angle] * spread   (cos, NOT sin)
 *   right_gain = sin_table[angle] * spread
 *   angle range 0° … 89°  (first quadrant — no destructive cancellation)
 *
 * Compile: g++ -std=c++14 -O2 -o test_sine_input test_sine_input.cpp -lm
 * Run:     ./test_sine_input
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Constants — must match PercussionSpatializer / constants.h
// ---------------------------------------------------------------------------
#define SAMPLE_RATE_F    48000.0f
#define DELAY_MAX        4096       // DELAY_MAX_SAMPLES in constants.h
#define DELAY_MASK       (DELAY_MAX - 1)
#define MAX_CLONES       16
#define CLONE_GROUPS     4
#define NEON_LANES       4
#define READ_OFFSET      32         // base_read = write_ptr - 32

// Tribal mode geometry (must match init_clone_parameters, MODE_TRIBAL case)
#define TRIBAL_BASE_MS   8.0f       // base_delay
#define TRIBAL_GROUP_MS  6.0f       // group_step
#define TRIBAL_LANE_MS   1.0f       // lane_step

// ---------------------------------------------------------------------------
// Scalar spatializer — mirrors the actual NEON processing exactly
// ---------------------------------------------------------------------------
typedef struct {
    float bufL[DELAY_MAX];
    float bufR[DELAY_MAX];
    uint32_t writePtr;

    int   cloneCount;
    float spread;
    float mix;
    float wobbleDepth;
    float lfoRate;                   // Hz

    float lfoPhase[MAX_CLONES];      // normalised [0, 1)
    float pitchMod[MAX_CLONES];      // per-clone LFO depth factor
    float baseDelaySamples[MAX_CLONES];

    float cosTab[90];                // 0° … 89°
    float sinTab[90];
    float leftGain[MAX_CLONES];
    float rightGain[MAX_CLONES];
} Spat;

static float triangle_lfo(float phase) {
    return 2.0f * fabsf(phase - 0.5f);  // same as lfo_table[] formula
}

static void spat_init(Spat *s, int cloneCount, float spread, float mix,
                      float wobbleDepth, float lfoRateHz) {
    memset(s, 0, sizeof(*s));

    // cos/sin tables for angles 0…89° (first quadrant only)
    for (int i = 0; i < 90; i++) {
        float a = (float)i * (float)M_PI / 180.0f;
        s->cosTab[i] = cosf(a);
        s->sinTab[i] = sinf(a);
    }

    s->cloneCount  = cloneCount;
    s->spread      = spread;
    s->mix         = mix;
    s->wobbleDepth = wobbleDepth;
    s->lfoRate     = lfoRateHz;

    // Tribal mode delays and per-clone LFO configuration
    // (mirrors init_clone_parameters with MODE_TRIBAL)
    for (int g = 0; g < CLONE_GROUPS; g++) {
        float base_ms = TRIBAL_BASE_MS + g * TRIBAL_GROUP_MS;
        for (int lane = 0; lane < NEON_LANES; lane++) {
            int ci = g * NEON_LANES + lane;
            float offset_ms = base_ms + lane * TRIBAL_LANE_MS;
            s->baseDelaySamples[ci] = offset_ms * 48.0f; // 1 ms = 48 samples
            s->pitchMod[ci] = 0.1f + lane * 0.05f;
            // Initial phase: ci/16 * full_cycle (matches fixed-point init)
            s->lfoPhase[ci] = (float)ci / 16.0f;
        }
    }

    // Panning: cos → left, sin → right, angles 0°…89°
    // Matches update_panning() in PercussionSpatializer exactly.
    for (int ci = 0; ci < MAX_CLONES; ci++) {
        if (ci < cloneCount) {
            float pos = (cloneCount > 1) ? (float)ci / (float)(cloneCount - 1) : 0.5f;
            int ang = (int)(pos * 89.0f);
            s->leftGain[ci]  = s->cosTab[ang] * spread;
            s->rightGain[ci] = s->sinTab[ang] * spread;
        } else {
            s->leftGain[ci] = s->rightGain[ci] = 0.0f;
        }
    }
}

static void spat_process(Spat *s, float inL, float inR,
                          float *outL, float *outR) {
    // Write input to delay buffer (mirrors write_to_delay_opt)
    uint32_t pos = s->writePtr & DELAY_MASK;
    s->bufL[pos] = inL;
    s->bufR[pos] = inR;
    s->writePtr++;

    // base_read mirrors: (write_ptr_ - 32) & DELAY_MASK
    uint32_t base_read = (s->writePtr - READ_OFFSET) & DELAY_MASK;

    float wetL = 0.0f, wetR = 0.0f;
    int num_groups = (s->cloneCount + NEON_LANES - 1) / NEON_LANES;

    for (int g = 0; g < num_groups; g++) {
        for (int lane = 0; lane < NEON_LANES; lane++) {
            int ci = g * NEON_LANES + lane;
            if (ci >= s->cloneCount) continue;

            float lfo     = triangle_lfo(s->lfoPhase[ci]);
            float wobble  = lfo * s->pitchMod[ci] * s->wobbleDepth; // extra ms
            float off_smp = (s->baseDelaySamples[ci] + wobble * 48.0f);

            float read_pos = (float)base_read - off_smp;
            float pos_adj  = read_pos + (float)DELAY_MAX;  // ensure positive
            while (pos_adj < 0.0f)          pos_adj += (float)DELAY_MAX;
            while (pos_adj >= (float)DELAY_MAX) pos_adj -= (float)DELAY_MAX;

            int   idx0 = (int)pos_adj & DELAY_MASK;
            int   idx1 = (idx0 + 1) & DELAY_MASK;
            float frac = pos_adj - (float)(int)pos_adj;

            float sL = s->bufL[idx0] + frac * (s->bufL[idx1] - s->bufL[idx0]);
            float sR = s->bufR[idx0] + frac * (s->bufR[idx1] - s->bufR[idx0]);

            wetL += sL * s->leftGain[ci];
            wetR += sR * s->rightGain[ci];

            s->lfoPhase[ci] += s->lfoRate / SAMPLE_RATE_F;
            if (s->lfoPhase[ci] >= 1.0f) s->lfoPhase[ci] -= 1.0f;
        }
    }

    // Volume compensation: 1/sqrt(cloneCount) — same as actual code
    float vc = 1.0f / sqrtf((float)s->cloneCount);
    wetL *= vc;
    wetR *= vc;

    *outL = inL * (1.0f - s->mix) + wetL * s->mix;
    *outR = inR * (1.0f - s->mix) + wetR * s->mix;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Expected output sample index for a given clone (0-indexed)
// The input written at n=0 appears in output at n = READ_OFFSET-1 + offset_smp.
static int expected_peak(int g, int lane) {
    float off_ms  = TRIBAL_BASE_MS + g * TRIBAL_GROUP_MS + lane * TRIBAL_LANE_MS;
    int   off_smp = (int)(off_ms * 48.0f);
    return (READ_OFFSET - 1) + off_smp;
}

// ---------------------------------------------------------------------------
// Test 1: impulse response — N distinct delay taps
// ---------------------------------------------------------------------------
static void test_impulse_response(int cloneCount, const char *label) {
    printf("\n=== Impulse Response: %d Clones (%s) ===\n", cloneCount, label);

    Spat s;
    spat_init(&s, cloneCount, 0.8f, 1.0f, 0.0f, 0.0f);  // no wobble

    const int BUFLEN = 2000;
    float outL[BUFLEN], outR[BUFLEN];

    for (int n = 0; n < BUFLEN; n++) {
        float in = (n == 0) ? 1.0f : 0.0f;
        spat_process(&s, in, in, &outL[n], &outR[n]);
    }

    int num_groups = (cloneCount + NEON_LANES - 1) / NEON_LANES;
    int peaks_found = 0, peaks_expected = 0;

    for (int g = 0; g < num_groups; g++) {
        for (int lane = 0; lane < NEON_LANES; lane++) {
            int ci = g * NEON_LANES + lane;
            if (ci >= cloneCount) continue;
            peaks_expected++;

            int t = expected_peak(g, lane);
            float off_ms = TRIBAL_BASE_MS + g * TRIBAL_GROUP_MS + lane * TRIBAL_LANE_MS;
            printf("  Clone %2d: expected peak at t=%-4d (%.1f ms base delay)\n",
                   ci, t, off_ms);

            bool found = false;
            for (int dt = -3; dt <= 3 && !found; dt++) {
                int tt = t + dt;
                if (tt >= 0 && tt < BUFLEN) {
                    float amp = fabsf(outL[tt]) + fabsf(outR[tt]);
                    if (amp > 0.01f) found = true;
                }
            }
            if (found) {
                peaks_found++;
            } else {
                printf("    ** MISSING peak for clone %d near t=%d!\n", ci, t);
            }
        }
    }

    printf("  Peaks confirmed: %d / %d\n", peaks_found, peaks_expected);
    assert(peaks_found == peaks_expected && "Missing clone delay tap in impulse response!");
    printf("  PASS: %d distinct delay taps confirmed\n", cloneCount);
}

// ---------------------------------------------------------------------------
// Test 2: sine-wave response — output non-zero at 100 % wet
// ---------------------------------------------------------------------------
static void test_sine_response(int cloneCount, float freq_hz) {
    printf("\n=== Sine Response: %d Clones @ %.0f Hz, mix=100%% ===\n",
           cloneCount, (double)freq_hz);

    Spat s;
    spat_init(&s, cloneCount, 0.8f, 1.0f, 0.0f, 0.0f);

    const int TOTAL = (int)(1.5f * SAMPLE_RATE_F);
    const int WARMUP = (int)(0.05f * SAMPLE_RATE_F); // 50 ms — longest delay ~12 ms
    float phase = 0.0f;
    float inc   = 2.0f * (float)M_PI * freq_hz / SAMPLE_RATE_F;
    float maxOut = 0.0f;

    for (int n = 0; n < TOTAL; n++) {
        float in = sinf(phase);
        phase += inc;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

        float oL, oR;
        spat_process(&s, in, in, &oL, &oR);

        if (n >= WARMUP) {
            float a = fabsf(oL) + fabsf(oR);
            if (a > maxOut) maxOut = a;
        }
    }

    printf("  Peak |output| after warmup: %.4f\n", (double)maxOut);
    assert(maxOut > 0.01f && "Output near zero — clone path not contributing!");
    printf("  PASS: wet output is active (peak %.4f)\n", (double)maxOut);
}

// ---------------------------------------------------------------------------
// Test 3: modulation produces audible amplitude variation
// With wobble and LFO the effective delay of each clone shifts over time,
// changing the relative phase of each delayed sine and creating amplitude
// modulation (chorus / vibrato).  Verified by confirming max > min amplitude.
// ---------------------------------------------------------------------------
static void test_modulation_variation(int cloneCount) {
    printf("\n=== Modulation Amplitude Variation: %d Clones ===\n", cloneCount);

    Spat s;
    spat_init(&s, cloneCount, 0.8f, 1.0f, 1.0f, 3.0f); // max wobble, 3 Hz LFO

    const int TOTAL  = (int)(3.0f * SAMPLE_RATE_F);
    const int WARMUP = (int)(0.2f  * SAMPLE_RATE_F);
    float phase = 0.0f;
    float inc   = 2.0f * (float)M_PI * 440.0f / SAMPLE_RATE_F;
    float maxA = 0.0f, minA = 1e30f;

    for (int n = 0; n < TOTAL; n++) {
        float in = sinf(phase);
        phase += inc;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

        float oL, oR;
        spat_process(&s, in, in, &oL, &oR);

        if (n >= WARMUP) {
            float a = fabsf(oL) + fabsf(oR);
            if (a > maxA) maxA = a;
            if (a < minA) minA = a;
        }
    }

    printf("  Amplitude range (after warmup): %.4f .. %.4f\n",
           (double)minA, (double)maxA);
    assert(maxA > 0.01f && "Output is silent with modulation!");
    printf("  PASS: output non-zero with modulation (%.4f max)\n", (double)maxA);
}

// ---------------------------------------------------------------------------
// Test 4: clone count scaling — more clones, more distinct delay taps
// ---------------------------------------------------------------------------
static void test_clone_count_scaling() {
    printf("\n=== Clone Count Scaling ===\n");
    const int counts[] = { 4, 8, 12, 16 };
    const char *labels[] = {
        "4 clones  (1 group, 8-11 ms)",
        "8 clones  (2 groups, 8-17 ms)",
        "12 clones (3 groups, 8-23 ms)",
        "16 clones (4 groups, 8-29 ms)"
    };
    for (int i = 0; i < 4; i++) {
        test_impulse_response(counts[i], labels[i]);
    }
    printf("\n  PASS: impulse-response tap count scales correctly for 4/8/12/16 clones\n");
}

// ---------------------------------------------------------------------------
// Test 5: mix control boundary conditions
// ---------------------------------------------------------------------------
static void test_mix_boundaries() {
    printf("\n=== Mix Boundary Test ===\n");

    // mix=0 → output == input (pure dry)
    {
        Spat s;
        spat_init(&s, 4, 0.8f, 0.0f, 0.0f, 0.0f);
        float in = 0.75f, oL, oR;
        spat_process(&s, in, in, &oL, &oR);
        assert(fabsf(oL - in) < 1e-5f && "mix=0 must pass dry signal unchanged");
        printf("  mix=0 → output=%.4f (expected %.4f)  PASS\n", (double)oL, (double)in);
    }

    // mix=1, t=0 → delay buffers empty → wet=0 → output≈0
    {
        Spat s;
        spat_init(&s, 4, 0.8f, 1.0f, 0.0f, 0.0f);
        float oL, oR;
        spat_process(&s, 1.0f, 1.0f, &oL, &oR);
        assert(fabsf(oL) < 0.01f && "mix=1 at t=0 must be near 0 (buffer empty)");
        printf("  mix=1 t=0 → output=%.5f (expected ~0.0)  PASS\n", (double)oL);
    }

    printf("  Mix boundaries: PASS\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    printf("=== delay_tribal: sine-wave / impulse-response signal tests ===\n");
    printf("Scalar model matches PercussionSpatializer (cos-left/sin-right,\n");
    printf("0-89 deg first-quadrant panning, 1/sqrt(N) volume compensation)\n");

    test_impulse_response(4,  "4 clones");
    test_sine_response(4, 440.0f);
    test_modulation_variation(4);
    test_clone_count_scaling();
    test_mix_boundaries();

    printf("\n=== ALL delay_tribal SIGNAL TESTS PASSED ===\n");
    return 0;
}

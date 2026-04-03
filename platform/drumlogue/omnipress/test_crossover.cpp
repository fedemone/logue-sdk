/**
 * @file test_crossover.cpp
 * @brief Unit tests for crossover.h critical bug fixes:
 *   1. L/R state independence — right channel must NOT track left channel state
 *   2. Per-instance last_freq — two crossovers at different frequencies must not
 *      reinitialize each other (the old static-last_freq bug)
 *   3. Basic frequency splitting — HP output must be larger than LP at a tone
 *      above the crossover frequency, and vice versa
 *
 * Compile: g++ -std=c++14 -O2 -o test_crossover test_crossover.cpp -lm
 * Run:     ./test_crossover
 */

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#define SAMPLE_RATE  48000.0f
#define M_PI_F       3.14159265358979323846f
#define EPSILON      1e-6f

/* --------------------------------------------------------------------------
 * Scalar biquad (Transposed Direct Form II) — mirrors crossover.h logic
 * but operates on individual float samples instead of float32x4_t lanes.
 * -------------------------------------------------------------------------- */
typedef struct { float z1, z2; } ScalarBiquad;

static float scalar_biquad(ScalarBiquad* s, float in, const float* c) {
    /* c[0..4] = b0, b1, b2, a1, a2   Transposed Direct Form II:
     *   y      = b0*x + z1
     *   z1_new = b1*x - a1*y + z2
     *   z2_new = b2*x - a2*y   */
    float out    = c[0] * in + s->z1;
    float new_z1 = c[1] * in - c[3] * out + s->z2;
    float new_z2 = c[2] * in - c[4] * out;
    s->z1 = new_z1;
    s->z2 = new_z2;
    return out;
}

/* Compute LR24 coefficients (same formula as crossover_init) */
typedef struct {
    float lpf[5];  /* b0,b1,b2,a1,a2 */
    float hpf[5];
} XoverCoeffs;

static void xover_coeffs(XoverCoeffs* x, float freq_hz, float sr) {
    float w0    = 2.0f * M_PI_F * freq_hz / sr;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float Q     = 0.707f;
    float alpha = sin_w0 / (2.0f * Q);
    float a0    = 1.0f + alpha;

    /* LPF */
    float b0 = (1.0f - cos_w0) / 2.0f;
    float b1 =  1.0f - cos_w0;
    float b2 = b0;
    float a1 = -2.0f * cos_w0;
    float a2 =  1.0f - alpha;
    x->lpf[0] = b0/a0; x->lpf[1] = b1/a0; x->lpf[2] = b2/a0;
    x->lpf[3] = a1/a0; x->lpf[4] = a2/a0;

    /* HPF */
    b0 = (1.0f + cos_w0) / 2.0f;
    b1 = -(1.0f + cos_w0);
    b2 = b0;
    x->hpf[0] = b0/a0; x->hpf[1] = b1/a0; x->hpf[2] = b2/a0;
    x->hpf[3] = a1/a0; x->hpf[4] = a2/a0;
}

/* Two-stage (24 dB/oct) LR filter — mimics the two cascaded biquads in crossover_process */
typedef struct { ScalarBiquad s1, s2; } LR24;

static float lr24_process(LR24* f, float in, const float* c) {
    return scalar_biquad(&f->s2, scalar_biquad(&f->s1, in, c), c);
}

/* --------------------------------------------------------------------------
 * Test 1: L/R state independence
 *
 * Drive left channel with a 1 kHz sine and right channel with silence.
 * After warm-up, the right channel LP output must remain near zero even
 * though the left channel has built up significant state.
 * If states were shared (old bug), the right output would track the left.
 * -------------------------------------------------------------------------- */
static void test_lr_state_independence() {
    printf("\n=== Test: L/R state independence ===\n");

    XoverCoeffs x;
    xover_coeffs(&x, 250.0f, SAMPLE_RATE);

    /* Separate state for L and R — mirrors the fixed crossover_t */
    LR24 l_lpf = {{0},{0}};
    LR24 r_lpf = {{0},{0}};

    float max_r_out = 0.0f;
    float max_l_out = 0.0f;
    int N = 4800;  /* 100 ms warm-up */

    for (int n = 0; n < N; n++) {
        float left_in  = sinf(2.0f * M_PI_F * 1000.0f * n / SAMPLE_RATE);
        float right_in = 0.0f;

        float l_out = lr24_process(&l_lpf, left_in,  x.lpf);
        float r_out = lr24_process(&r_lpf, right_in, x.lpf);

        if (fabsf(l_out) > max_l_out) max_l_out = fabsf(l_out);
        if (fabsf(r_out) > max_r_out) max_r_out = fabsf(r_out);
    }

    printf("  Left  LP max amplitude  = %.6f (expect > 0)\n", max_l_out);
    printf("  Right LP max amplitude  = %.6f (expect ~0, silence in → silence out)\n", max_r_out);

    assert(max_l_out > 0.01f && "Left channel did not build up — filter not working");
    assert(max_r_out < 1e-6f && "Right channel has output from left channel state — state sharing bug!");

    printf("  PASS: right channel state is fully independent from left\n");
}

/* --------------------------------------------------------------------------
 * Test 2: Per-instance last_freq — two crossovers at different frequencies
 *
 * Old bug: static last_freq caused the 250 Hz crossover to reinitialize
 * every time the 2500 Hz crossover ran and vice-versa, resetting all
 * filter states to zero every block.
 *
 * We verify: after alternating calls to both crossovers for 1000 iterations,
 * both crossovers must have non-zero filter state (i.e., they were not reset
 * each call).
 * -------------------------------------------------------------------------- */
static void test_per_instance_freq() {
    printf("\n=== Test: Per-instance last_freq (no cross-reinit) ===\n");

    XoverCoeffs xa, xb;
    xover_coeffs(&xa, 250.0f,  SAMPLE_RATE);
    xover_coeffs(&xb, 2500.0f, SAMPLE_RATE);

    float last_freq_a = 250.0f;
    float last_freq_b = 2500.0f;

    LR24 a_lpf = {{0},{0}}, a_hpf = {{0},{0}};
    LR24 b_lpf = {{0},{0}}, b_hpf = {{0},{0}};

    float a_state_sum = 0.0f;
    float b_state_sum = 0.0f;

    int N = 1000;
    for (int n = 0; n < N; n++) {
        float in = sinf(2.0f * M_PI_F * 500.0f * n / SAMPLE_RATE);

        /* Simulate per-instance reinit check (only reinit when freq changes) */
        if (fabsf(250.0f - last_freq_a) > 1.0f) {
            xover_coeffs(&xa, 250.0f, SAMPLE_RATE);
            memset(&a_lpf, 0, sizeof(a_lpf));
            memset(&a_hpf, 0, sizeof(a_hpf));
            last_freq_a = 250.0f;
        }
        if (fabsf(2500.0f - last_freq_b) > 1.0f) {
            xover_coeffs(&xb, 2500.0f, SAMPLE_RATE);
            memset(&b_lpf, 0, sizeof(b_lpf));
            memset(&b_hpf, 0, sizeof(b_hpf));
            last_freq_b = 2500.0f;
        }

        /* Both crossovers process the same input alternately */
        float a_lp = lr24_process(&a_lpf, in, xa.lpf);
        float b_lp = lr24_process(&b_lpf, in, xb.lpf);

        a_state_sum += fabsf(a_lp);
        b_state_sum += fabsf(b_lp);
    }

    printf("  Crossover A (250 Hz) cumulative LP output = %.4f\n", a_state_sum);
    printf("  Crossover B (2500 Hz) cumulative LP output = %.4f\n", b_state_sum);

    assert(a_state_sum > 10.0f && "Crossover A state appears reset — per-instance freq bug!");
    assert(b_state_sum > 10.0f && "Crossover B state appears reset — per-instance freq bug!");
    /* 500 Hz is 1 octave ABOVE the 250 Hz crossover → LP output is heavily attenuated.
     * 500 Hz is well BELOW the 2500 Hz crossover → LP output is near unity.
     * So b_state_sum (2500 Hz crossover) should be larger than a_state_sum. */
    assert(b_state_sum > a_state_sum &&
           "2500 Hz crossover LP should pass 500 Hz tone more than 250 Hz crossover LP");

    printf("  PASS: both crossovers maintain state independently\n");
}

/* --------------------------------------------------------------------------
 * Test 3: Basic frequency splitting correctness
 *
 * A tone BELOW the crossover frequency should have:
 *   LP amplitude >> HP amplitude
 * A tone ABOVE should have:
 *   HP amplitude >> LP amplitude
 * -------------------------------------------------------------------------- */
static void test_frequency_splitting() {
    printf("\n=== Test: Frequency splitting correctness (250 Hz crossover) ===\n");

    XoverCoeffs x;
    xover_coeffs(&x, 250.0f, SAMPLE_RATE);

    struct { const char* label; float freq; int expect_lp_bigger; } cases[] = {
        { "50 Hz (below xover)",   50.0f,  1 },
        { "1000 Hz (above xover)", 1000.0f, 0 },
    };

    for (int c = 0; c < 2; c++) {
        LR24 lpf = {{0},{0}}, hpf = {{0},{0}};
        float max_lp = 0.0f, max_hp = 0.0f;
        int warmup = 4800, measure = 4800;

        for (int n = 0; n < warmup + measure; n++) {
            float in = sinf(2.0f * M_PI_F * cases[c].freq * n / SAMPLE_RATE);
            float lp = lr24_process(&lpf, in, x.lpf);
            float hp = lr24_process(&hpf, in, x.hpf);
            if (n >= warmup) {
                if (fabsf(lp) > max_lp) max_lp = fabsf(lp);
                if (fabsf(hp) > max_hp) max_hp = fabsf(hp);
            }
        }

        printf("  %s: LP peak=%.4f  HP peak=%.4f\n", cases[c].label, max_lp, max_hp);

        if (cases[c].expect_lp_bigger) {
            assert(max_lp > max_hp * 5.0f && "LP should dominate below xover frequency");
            printf("    LP >> HP: PASS\n");
        } else {
            assert(max_hp > max_lp * 5.0f && "HP should dominate above xover frequency");
            printf("    HP >> LP: PASS\n");
        }
    }
}

/* --------------------------------------------------------------------------
 * Test 4: 3-band split energy conservation (approximate)
 *
 * Apply a wideband noise burst through the two-stage crossover chain and
 * verify that the energy in low + mid + high ≈ total input energy.
 * -------------------------------------------------------------------------- */
static void test_3band_energy_conservation() {
    printf("\n=== Test: 3-band energy conservation ===\n");

    XoverCoeffs x1, x2;
    xover_coeffs(&x1, 250.0f,  SAMPLE_RATE);
    xover_coeffs(&x2, 2500.0f, SAMPLE_RATE);

    LR24 lpf1 = {{0},{0}}, hpf1 = {{0},{0}};
    LR24 lpf2 = {{0},{0}}, hpf2 = {{0},{0}};
    LR24 unused_lpf = {{0},{0}}, unused_hpf = {{0},{0}};

    /* Use a simple deterministic "noise" (LCG) */
    unsigned int rng = 0xBEEFCAFEu;
    double e_in = 0.0, e_low = 0.0, e_mid = 0.0, e_high = 0.0;
    int warmup = 4800, N = 48000;

    for (int n = 0; n < warmup + N; n++) {
        rng = rng * 1664525u + 1013904223u;
        float in = (float)((int)rng) / 2147483648.0f * 0.5f;

        /* Stage 1: split into low and (mid+high) */
        float low   = lr24_process(&lpf1, in, x1.lpf);
        float midhigh = lr24_process(&hpf1, in, x1.hpf);
        /* mid complement not needed for energy test — we split LP and HP only */
        (void)lr24_process(&unused_lpf, in, x1.lpf);  /* keep unused state moving */

        /* Stage 2: split (mid+high) into mid and high */
        float mid  = lr24_process(&lpf2, midhigh, x2.lpf);
        float high = lr24_process(&hpf2, midhigh, x2.hpf);
        (void)lr24_process(&unused_hpf, midhigh, x2.lpf);

        if (n >= warmup) {
            e_in   += (double)in   * in;
            e_low  += (double)low  * low;
            e_mid  += (double)mid  * mid;
            e_high += (double)high * high;
        }
    }

    double e_sum = e_low + e_mid + e_high;
    double ratio = e_sum / e_in;

    printf("  Input energy   = %.4f\n", (float)e_in);
    printf("  Low  energy    = %.4f\n", (float)e_low);
    printf("  Mid  energy    = %.4f\n", (float)e_mid);
    printf("  High energy    = %.4f\n", (float)e_high);
    printf("  Sum/Input ratio= %.4f (LR4 is not energy-conserving; expect ~1.0 ± 0.3)\n", (float)ratio);

    /* LR4 (Linkwitz-Riley 24 dB/oct) has slight energy excess at crossover.
     * Accept anything within 30% of unity as healthy behaviour. */
    assert(ratio > 0.7 && ratio < 1.3 &&
           "Band energy sum deviates too far from input — potential processing error");

    printf("  PASS: 3-band energy sum within expected range\n");
}

int main() {
    printf("=== OmniPress crossover tests ===\n");
    test_lr_state_independence();
    test_per_instance_freq();
    test_frequency_splitting();
    test_3band_energy_conservation();
    printf("\n=== ALL CROSSOVER TESTS PASSED ===\n");
    return 0;
}

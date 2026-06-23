/**
 * @file test_multiband.cpp
 * @brief Multiband compressor unit tests (scalar version)
 */

#include <stdio.h>
#include <math.h>
#include <assert.h>

#define SAMPLE_RATE 48000.0f
#define EPSILON 1e-4f

/* Scalar version of compressor_smooth (attack/release) */
static float scalar_smooth(float state, float target, float coeff) {
    return coeff * state + (1.0f - coeff) * target;
}

/* Simulate per-band envelope follower */
static void test_per_band_attack_release() {
    printf("\n=== Test: Per-Band Attack/Release Times ===\n");

    float attack_ms[] = {1.0f, 10.0f, 100.0f};
    float release_ms[] = {10.0f, 100.0f, 1000.0f};

    float attack_coeff[3], release_coeff[3];
    for (int i = 0; i < 3; i++) {
        attack_coeff[i] = expf(-1.0f / (attack_ms[i] * 0.001f * SAMPLE_RATE));
        release_coeff[i] = expf(-1.0f / (release_ms[i] * 0.001f * SAMPLE_RATE));
        printf("  Band %d: attack_coeff=%.6f, release_coeff=%.6f\n",
               i, attack_coeff[i], release_coeff[i]);
        assert(attack_coeff[i] > 0.0f && attack_coeff[i] < 1.0f);
        assert(release_coeff[i] > 0.0f && release_coeff[i] < 1.0f);
    }

    // Simulate step response for each band
    float state[3] = {0.0f, 0.0f, 0.0f};
    float target = 1.0f;

    // Attack phase: 1000 samples
    int n_attack = (int)(attack_ms[2] * 0.001f * SAMPLE_RATE); // longest attack
    for (int n = 0; n < n_attack; n++) {
        for (int i = 0; i < 3; i++) {
            state[i] = scalar_smooth(state[i], target, attack_coeff[i]);
        }
    }
    // After longest attack, band 0 should be ~1, band 2 should be lower
    printf("  After attack step: band0=%.4f, band1=%.4f, band2=%.4f\n",
           state[0], state[1], state[2]);
    assert(fabsf(state[0] - 1.0f) < 0.01f);
    assert(state[2] < 0.9f); // still rising

    // Release phase: 2000 samples, target=0
    int n_release = (int)(release_ms[2] * 0.001f * SAMPLE_RATE);
    target = 0.0f;
    for (int n = 0; n < n_release; n++) {
        for (int i = 0; i < 3; i++) {
            state[i] = scalar_smooth(state[i], target, release_coeff[i]);
        }
    }
    printf("  After release step: band0=%.4f, band1=%.4f, band2=%.4f\n",
           state[0], state[1], state[2]);
    assert(fabsf(state[0]) < 0.01f);
    assert(state[2] > 0.1f); // still decaying

    printf("  PASS: per-band attack/release coefficients behave independently\n");
}

int main() {
    printf("=== OmniPress multiband tests ===\n");
    test_per_band_attack_release();
    printf("\n=== ALL MULTIBAND TESTS PASSED ===\n");
    return 0;
}
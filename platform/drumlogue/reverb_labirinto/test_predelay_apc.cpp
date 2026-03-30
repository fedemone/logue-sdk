/**
 * @file test_predelay_apc.cpp
 * @brief Unit tests for the Labirinto Scalar Pre-Delay & Active Partial Counting
 *
 * Compile: g++ -std=c++14 -o test_predelay_apc test_predelay_apc.cpp -lm -I.
 * Run:     ./test_predelay_apc
 */

#include <cassert>
#include <iostream>
#include <cmath>
#include "NeonAdvancedLabirinto.h"

// Helper macro for testing floats with an epsilon
#define ASSERT_FLOAT_EQ(expected, actual, epsilon) \
    assert(std::fabs((expected) - (actual)) <= (epsilon))

void test_predelay_timing_and_wakeup() {
    std::cout << "Running Test: Pre-Delay Timing & APC Wake-up... ";

    NeonAdvancedLabirinto reverb;
    reverb.init();

    // 10ms at 48kHz = 480 samples
    reverb.setPreDelay(10.0f);
    reverb.setDecay(0.1f);

    float wetL = 0.0f, wetR = 0.0f;

    // 1. Inject a single impulse (1.0f)
    // At this moment, delayedInput is 0. APC is ASLEEP.
    // In sleep mode, wetL/R just passes the input directly.
    reverb.processScalar(1.0f, wetL, wetR);
    ASSERT_FLOAT_EQ(1.0f, wetL, 1e-6f);

    // 2. Process 478 frames of absolute silence
    // The impulse is traveling through the pre-delay buffer.
    // APC remains ASLEEP.
    for (int i = 1; i < 480; i++) {
        reverb.processScalar(0.0f, wetL, wetR);
        ASSERT_FLOAT_EQ(0.0f, wetL, 1e-6f);
    }

    // 3. Frame 480: The impulse hits the read head!
    // delayedInput becomes 1.0f. APC WAKES UP.
    // The matrix processes the impulse, so wetL/R will now contain the first FDN reflection.
    reverb.processScalar(0.0f, wetL, wetR);

    // If the engine woke up, the output should no longer be exactly 0.0f
    // (it should be the first reflection multiplied by the Hadamard coefficients).
    assert(std::fabs(wetL) > 1e-6f);

    std::cout << "PASSED\n";
}

void test_apc_noise_floor_rejection() {
    std::cout << "Running Test: APC Noise Floor Rejection... ";

    NeonAdvancedLabirinto reverb;
    reverb.init();
    reverb.setPreDelay(5.0f); // 240 samples

    float wetL = 0.0f, wetR = 0.0f;

    // 1. Inject a sub-threshold signal (e.g., 1e-6f, below the 1e-5f threshold)
    reverb.processScalar(1e-6f, wetL, wetR);

    // 2. Wait for it to pass the pre-delay
    for (int i = 1; i <= 240; i++) {
        reverb.processScalar(0.0f, wetL, wetR);
    }

    // 3. At this point, delayedInput is 1e-6f.
    // Because 1e-6f < 1e-5f, the engine should REMAIN ASLEEP and bypass.
    // Since input is currently 0.0f, wetL should perfectly equal 0.0f.
    ASSERT_FLOAT_EQ(0.0f, wetL, 1e-9f);

    std::cout << "PASSED\n";
}

void test_apc_tail_timeout() {
    std::cout << "Running Test: APC Tail Timeout (Sleep Mode)... ";

    NeonAdvancedLabirinto reverb;
    reverb.init();

    // Very short decay to minimize the test loop count
    reverb.setPreDelay(0.0f);
    reverb.setDecay(0.01f);

    float wetL = 0.0f, wetR = 0.0f;

    // 1. Wake the engine up with an impulse
    reverb.processScalar(1.0f, wetL, wetR);

    // Calculate expected tail length based on the logic:
    // activeSampleCount = (int)(sampleRate * (1.0f + decay * 5.0f));
    int expectedActiveSamples = (int)(48000.0f * (1.0f + 0.01f * 5.0f));

    // 2. Process silence until just before the timeout
    for (int i = 1; i < expectedActiveSamples; i++) {
        reverb.processScalar(0.0f, wetL, wetR);
    }

    // 3. Process one more sample to cross the threshold (counter hits 0)
    // The engine should now switch to SLEEP mode.
    reverb.processScalar(0.0f, wetL, wetR);

    // 4. Verify sleep mode behavior.
    // If we feed a small DC offset (e.g., 0.5f) while asleep, wetL must strictly equal the input.
    reverb.processScalar(0.5f, wetL, wetR);
    ASSERT_FLOAT_EQ(0.5f, wetL, 1e-6f);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "--- Starting Labirinto Scalar Unit Tests ---\n";
    test_predelay_timing_and_wakeup();
    test_apc_noise_floor_rejection();
    test_apc_tail_timeout();
    std::cout << "--- All Tests Passed! ---\n";
    return 0;
}
/**
 * @file test_predelay_apc.cpp
 * @brief Unit tests for Labirinto Pre-Delay & Active Partial Counting (APC) logic.
 *
 * Tests the scalar pre-delay and APC state machine independently of ARM NEON
 * intrinsics, so the file compiles and runs on x86/x64 for CI purposes.
 *
 * The three behaviours under test:
 *   1. Pre-delay timing: impulse appears at output exactly after N samples.
 *   2. APC noise-floor rejection: sub-threshold delayed input keeps engine asleep.
 *   3. APC tail timeout: engine transitions back to sleep after decay tail.
 *
 * Compile: g++ -std=c++14 -O2 -o test_predelay_apc test_predelay_apc.cpp -lm
 * Run:     ./test_predelay_apc
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>

/* -------------------------------------------------------------------------
 * Scalar pre-delay + APC stub
 * Mirrors the exact logic from NeonAdvancedLabirinto::processScalar() without
 * any ARM NEON intrinsics.
 * ---------------------------------------------------------------------- */
#define PREDELAY_BUFFER_SIZE 16384   /* ~341 ms at 48 kHz */
#define PREDELAY_MASK        (PREDELAY_BUFFER_SIZE - 1)
#define FDN_CHANNELS         8
#define BUFFER_SIZE          65536
#define BUFFER_MASK          (BUFFER_SIZE - 1)
#define SAMPLE_RATE          48000.0f

struct ScalarLabirinto {
    float  preDelayBuffer[PREDELAY_BUFFER_SIZE];
    int    preDelayWritePos;
    int    preDelayOffsetSamples;
    int    activeSampleCount;
    float  decay;
    float  sampleRate;

    /* Minimal FDN state (8 channels, single delay tap each) */
    float  delayLine[BUFFER_SIZE][FDN_CHANNELS];
    float  modPhase[FDN_CHANNELS];
    float  lpfState[FDN_CHANNELS];
    float  hadamard[FDN_CHANNELS][FDN_CHANNELS];
    float  delayTimes[FDN_CHANNELS];
    int    writePos;

    float  modDepth;
    float  modRate;
    float  diffusion;
    float  width;
    float  dampingCoeff;
    float  lowDecayMult;
    float  highDecayMult;
};

static void lab_init(ScalarLabirinto *s) {
    memset(s, 0, sizeof(*s));
    s->sampleRate  = SAMPLE_RATE;
    s->decay       = 0.5f;
    s->diffusion   = 0.3f;
    s->modDepth    = 0.1f;
    s->modRate     = 0.5f;
    s->width       = 1.0f;
    s->lowDecayMult  = 1.0f;
    s->highDecayMult = 0.5f;

    /* Same prime-spaced delay times as the real engine */
    float baseDelays[FDN_CHANNELS] = {
        0.0421f, 0.0713f, 0.0987f, 0.1249f,
        0.1571f, 0.1835f, 0.2127f, 0.2413f
    };
    for (int i = 0; i < FDN_CHANNELS; i++)
        s->delayTimes[i] = baseDelays[i];

    /* Hadamard mixing matrix */
    float norm = 1.0f / sqrtf(8.0f);
    for (int i = 0; i < FDN_CHANNELS; i++)
        for (int j = 0; j < FDN_CHANNELS; j++) {
            int bits = i & j, parity = 0;
            while (bits) { parity ^= (bits & 1); bits >>= 1; }
            s->hadamard[i][j] = parity ? -norm : norm;
        }
}

static void lab_set_params(ScalarLabirinto *s,
                           float decayVal, float predelayMs,
                           float diffusion, float modDepth, float modRate,
                           float mix, float width,
                           float dampingFreqHz, float lowDecay, float highDecay) {
    s->decay     = fmaxf(0.0f, fminf(0.99f, decayVal));
    s->diffusion = fmaxf(0.0f, fminf(1.0f, diffusion));
    s->modDepth  = fmaxf(0.0f, fminf(1.0f, modDepth));
    s->modRate   = fmaxf(0.1f, fminf(10.0f, modRate));
    s->width     = fmaxf(0.0f, fminf(2.0f, width));
    s->lowDecayMult  = 0.9f + (lowDecay  * 0.01f) * 0.6f;
    s->highDecayMult = 0.1f + (highDecay * 0.01f) * 0.9f;

    float clampedMs = fmaxf(0.0f, fminf(340.0f, predelayMs));
    s->preDelayOffsetSamples = (int)(clampedMs * s->sampleRate / 1000.0f);

    float omega = 2.0f * (float)M_PI * fmaxf(200.0f, fminf(10000.0f, dampingFreqHz))
                  / s->sampleRate;
    s->dampingCoeff = expf(-omega);
}

static void lab_process_sample(ScalarLabirinto *s, float input,
                               float *wetL, float *wetR) {
    /* 1. Pre-delay write */
    s->preDelayBuffer[s->preDelayWritePos] = input;

    /* 2. Pre-delay read */
    int rp = (s->preDelayWritePos - s->preDelayOffsetSamples + PREDELAY_BUFFER_SIZE)
             & PREDELAY_MASK;
    float delayedInput = s->preDelayBuffer[rp];
    s->preDelayWritePos = (s->preDelayWritePos + 1) & PREDELAY_MASK;

    /* 3. APC: update active sample count */
    if (fabsf(delayedInput) > 1e-5f) {
        s->activeSampleCount = (int)(s->sampleRate * (1.0f + s->decay * 5.0f));
    } else if (s->activeSampleCount > 0) {
        s->activeSampleCount--;
    }

    /* Bypass when tail is dead */
    if (s->activeSampleCount <= 0) {
        *wetL = input;
        *wetR = input;
        return;
    }

    /* 4. Minimal FDN processing (scalar, NEON-free) */
    float delayOut[FDN_CHANNELS];
    for (int ch = 0; ch < FDN_CHANNELS; ch++) {
        float delaySamples = s->delayTimes[ch] * s->sampleRate;
        float modOff = sinf(s->modPhase[ch] * 2.0f * (float)M_PI)
                       * s->modDepth * 100.0f;
        float rdp = (float)s->writePos - (delaySamples + modOff);
        while (rdp < 0)           rdp += BUFFER_SIZE;
        while (rdp >= BUFFER_SIZE) rdp -= BUFFER_SIZE;

        int idx  = (int)rdp;
        int idx2 = (idx + 1) & BUFFER_MASK;
        float frac = rdp - idx;
        delayOut[ch] = s->delayLine[idx][ch]
                       + frac * (s->delayLine[idx2][ch] - s->delayLine[idx][ch]);

        s->modPhase[ch] += s->modRate / s->sampleRate;
        if (s->modPhase[ch] >= 1.0f) s->modPhase[ch] -= 1.0f;
    }

    float mixed[FDN_CHANNELS];
    for (int i = 0; i < FDN_CHANNELS; i++) {
        float sum = 0.0f;
        for (int j = 0; j < FDN_CHANNELS; j++)
            sum += s->hadamard[i][j] * delayOut[j];
        float dm = fminf(0.99f, s->decay * sqrtf(s->highDecayMult * s->lowDecayMult));
        mixed[i] = sum * dm;
    }
    mixed[0] += delayedInput * (1.0f - s->decay);

    for (int ch = 0; ch < FDN_CHANNELS; ch++)
        s->delayLine[s->writePos][ch] = mixed[ch];
    s->writePos = (s->writePos + 1) & BUFFER_MASK;

    /* Stereo mix-down (full 8 channels) */
    float leftRaw = 0.0f, rightRaw = 0.0f;
    for (int i = 0; i < 4; i++) leftRaw  += mixed[i];
    for (int i = 4; i < 8; i++) rightRaw += mixed[i];
    leftRaw  *= 0.25f;
    rightRaw *= 0.25f;

    float mid  = (leftRaw + rightRaw) * 0.5f;
    float side = (leftRaw - rightRaw) * 0.5f;
    *wetL = mid + side * s->width;
    *wetR = mid - side * s->width;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */
static void test_predelay_timing_and_wakeup() {
    printf("Running Test: Pre-Delay Timing & APC Wake-up... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.1f, 10.0f /* 10 ms = 480 samples */,
                   0.3f, 0.1f, 0.5f, 0.3f, 1.0f, 5000.0f, 50.0f, 50.0f);

    float wetL = 0.0f, wetR = 0.0f;

    /* Frame 0: impulse; engine asleep → bypass */
    lab_process_sample(&rev, 1.0f, &wetL, &wetR);
    assert(fabsf(wetL - 1.0f) < 1e-6f);

    /* Frames 1..479: silence; impulse in pre-delay buffer */
    for (int i = 1; i < 480; i++) {
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);
        assert(fabsf(wetL) < 1e-6f);
    }

    /* Frame 480: impulse hits read head, engine wakes up */
    lab_process_sample(&rev, 0.0f, &wetL, &wetR);
    assert(fabsf(wetL) > 1e-6f);   /* first FDN reflection must be non-zero */

    printf("PASSED\n");
}

static void test_apc_noise_floor_rejection() {
    printf("Running Test: APC Noise Floor Rejection... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.5f, 5.0f /* 240 samples */,
                   0.3f, 0.1f, 0.5f, 0.3f, 1.0f, 5000.0f, 50.0f, 50.0f);

    float wetL = 0.0f, wetR = 0.0f;

    /* Sub-threshold signal (1e-6 < APC threshold 1e-5) */
    lab_process_sample(&rev, 1e-6f, &wetL, &wetR);

    /* Wait for it to clear the pre-delay */
    for (int i = 1; i <= 240; i++)
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* Engine must still be asleep: output equals current input (0) */
    assert(fabsf(wetL) < 1e-9f);

    printf("PASSED\n");
}

static void test_apc_tail_timeout() {
    printf("Running Test: APC Tail Timeout (Sleep Mode)... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.01f, 0.0f,
                   0.3f, 0.1f, 0.5f, 0.3f, 1.0f, 5000.0f, 50.0f, 50.0f);

    float wetL = 0.0f, wetR = 0.0f;

    /* Wake the engine with an impulse */
    lab_process_sample(&rev, 1.0f, &wetL, &wetR);

    int expectedActive = (int)(SAMPLE_RATE * (1.0f + 0.01f * 5.0f));

    /* Drain silence until just before timeout */
    for (int i = 1; i < expectedActive; i++)
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* One more sample pushes counter to 0 → engine sleeps */
    lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* Feed sub-threshold value while asleep: engine must stay asleep and bypass.
     * We use 1e-7f (well below the 1e-5f APC threshold) so that delayedInput
     * does not re-trigger APC wake-up.  With 0ms predelay the read head reads
     * back the same position that was just written, so the value fed must stay
     * below threshold. */
    float bypass_in = 1e-7f;
    lab_process_sample(&rev, bypass_in, &wetL, &wetR);
    assert(fabsf(wetL - bypass_in) < 1e-9f);

    printf("PASSED\n");
}

int main() {
    printf("--- Labirinto Pre-Delay & APC Scalar Unit Tests ---\n");
    test_predelay_timing_and_wakeup();
    test_apc_noise_floor_rejection();
    test_apc_tail_timeout();
    printf("--- All Tests Passed! ---\n");
    return 0;
}

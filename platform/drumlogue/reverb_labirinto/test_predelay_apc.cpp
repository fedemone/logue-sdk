/**
 * @file test_predelay_apc.cpp
 * @brief Unit tests for Labirinto Pre-Delay & Active Partial Counting (APC) logic.
 *
 * Tests the scalar pre-delay and APC state machine independently of ARM NEON
 * intrinsics, so the file compiles and runs on x86/x64 for CI purposes.
 *
 * The four behaviours under test:
 *   1. Pre-delay timing: impulse appears at FDN input exactly after N samples.
 *   2. APC noise-floor rejection: sub-threshold input keeps engine asleep.
 *   3. APC tail timeout: engine transitions back to sleep after decay tail.
 *   4. Raw-input wake-up: with 0 ms pre-delay, audio wakes engine immediately.
 *
 * REGRESSION NOTE — APC deadlock (fixed in commit 8f8d255):
 *   Before the fix, the bypass check fired BEFORE the pre-delay write/read.
 *   With pre-delay = 0 ms, delayedInput could only wake the APC if the
 *   pre-delay code ran — but that code was gated by the bypass.  The engine
 *   could never escape bypass: a classic chicken-and-egg deadlock.
 *   The fix adds a raw-input amplitude check *before* the bypass guard so the
 *   engine wakes up the moment real audio arrives, regardless of pre-delay.
 *   test_deadlock_regression() demonstrates the old buggy control flow
 *   (bypass-before-predelay, no raw-input check) to make the regression
 *   explicit.
 *
 * Control-flow order that the stub MUST mirror (matches processScalar()):
 *   1. Raw-input wake-up check  (new — prevents deadlock)
 *   2. Bypass guard             (returns wetL/R = 0 when still asleep)
 *   3. Pre-delay write + read
 *   4. APC check on delayedInput
 *   5. Second bypass guard      (tail expired mid-block)
 *   6. FDN processing
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
 * Constants — mirror NeonAdvancedLabirinto.h
 * ---------------------------------------------------------------------- */
#define PREDELAY_BUFFER_SIZE 16384
#define PREDELAY_MASK        (PREDELAY_BUFFER_SIZE - 1)
#define FDN_CHANNELS         8
#define BUFFER_SIZE          65536
#define BUFFER_MASK          (BUFFER_SIZE - 1)
#define SAMPLE_RATE          48000.0f
#define APC_THRESHOLD        1e-5f

/* -------------------------------------------------------------------------
 * Scalar stub — mirrors the EXACT control-flow of processScalar().
 * wetL / wetR are the RAW wet signal (0 in bypass); the caller mixes dry.
 * ---------------------------------------------------------------------- */
struct ScalarLabirinto {
    float  preDelayBuffer[PREDELAY_BUFFER_SIZE];
    int    preDelayWritePos;
    int    preDelayOffsetSamples;
    int    activeSampleCount;
    float  decay;
    float  sampleRate;
    float  delayLine[BUFFER_SIZE][FDN_CHANNELS];
    float  modPhase[FDN_CHANNELS];
    float  lpfState[FDN_CHANNELS];
    float  hadamard[FDN_CHANNELS][FDN_CHANNELS];
    float  delayTimes[FDN_CHANNELS];
    int    writePos;
    float  modDepth;
    float  modRate;
    float  width;
};

static void lab_init(ScalarLabirinto *s) {
    memset(s, 0, sizeof(*s));
    s->sampleRate = SAMPLE_RATE;
    s->decay      = 0.5f;
    s->modDepth   = 0.1f;
    s->modRate    = 0.5f;
    s->width      = 1.0f;

    float baseDelays[FDN_CHANNELS] = {
        0.0421f, 0.0713f, 0.0987f, 0.1249f,
        0.1571f, 0.1835f, 0.2127f, 0.2413f
    };
    for (int i = 0; i < FDN_CHANNELS; i++)
        s->delayTimes[i] = baseDelays[i];

    float norm = 1.0f / sqrtf(8.0f);
    for (int i = 0; i < FDN_CHANNELS; i++)
        for (int j = 0; j < FDN_CHANNELS; j++) {
            int bits = i & j, parity = 0;
            while (bits) { parity ^= (bits & 1); bits >>= 1; }
            s->hadamard[i][j] = parity ? -norm : norm;
        }
}

static void lab_set_params(ScalarLabirinto *s,
                           float decayVal, float predelayMs) {
    s->decay = fmaxf(0.0f, fminf(0.99f, decayVal));
    float clampedMs = fmaxf(0.0f, fminf(340.0f, predelayMs));
    s->preDelayOffsetSamples = (int)(clampedMs * s->sampleRate / 1000.0f);
}

/* Correct control-flow stub — matches processScalar() post-fix. */
static void lab_process_sample(ScalarLabirinto *s, float input,
                               float *wetL, float *wetR) {
    /* Step 1: RAW-INPUT WAKE-UP (must come before the bypass guard). */
    if (fabsf(input) > APC_THRESHOLD)
        s->activeSampleCount = (int)(s->sampleRate * (1.0f + s->decay * 5.0f));

    /* Step 2: BYPASS GUARD — returns wet=0 when still asleep. */
    if (s->activeSampleCount <= 0) {
        *wetL = 0.0f;
        *wetR = 0.0f;
        s->writePos = (s->writePos + 1) & BUFFER_MASK;
        return;
    }

    /* Step 3: PRE-DELAY write then read. */
    s->preDelayBuffer[s->preDelayWritePos] = input;
    int rp = (s->preDelayWritePos - s->preDelayOffsetSamples + PREDELAY_BUFFER_SIZE)
             & PREDELAY_MASK;
    float delayedInput = s->preDelayBuffer[rp];
    s->preDelayWritePos = (s->preDelayWritePos + 1) & PREDELAY_MASK;

    /* Step 4: APC check on DELAYED input. */
    if (fabsf(delayedInput) > APC_THRESHOLD)
        s->activeSampleCount = (int)(s->sampleRate * (1.0f + s->decay * 5.0f));
    else if (s->activeSampleCount > 0)
        s->activeSampleCount--;

    /* Step 5: Second bypass guard — tail expired mid-block. */
    if (s->activeSampleCount <= 0) {
        *wetL = 0.0f;
        *wetR = 0.0f;
        s->writePos = (s->writePos + 1) & BUFFER_MASK;
        return;
    }

    /* Step 6: FDN processing. */
    float delayOut[FDN_CHANNELS];
    for (int ch = 0; ch < FDN_CHANNELS; ch++) {
        float delaySamples = s->delayTimes[ch] * s->sampleRate;
        float modOff = sinf(s->modPhase[ch] * 2.0f * (float)M_PI)
                       * s->modDepth * 100.0f;
        float rdp = (float)s->writePos - (delaySamples + modOff);
        while (rdp < 0)            rdp += BUFFER_SIZE;
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
        float dm = fminf(0.99f, s->decay);
        mixed[i] = sum * dm;
    }
    mixed[0] += delayedInput * (1.0f - s->decay);

    for (int ch = 0; ch < FDN_CHANNELS; ch++)
        s->delayLine[s->writePos][ch] = mixed[ch];
    s->writePos = (s->writePos + 1) & BUFFER_MASK;

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

/* Buggy stub — OLD control-flow (pre-fix): bypass BEFORE pre-delay,
 * no raw-input check.  Used by test_deadlock_regression() to document
 * the original bug. */
static void lab_process_sample_buggy(ScalarLabirinto *s, float input,
                                     float *wetL, float *wetR) {
    /* OLD Step 1: BYPASS GUARD fires immediately — pre-delay never runs. */
    if (s->activeSampleCount <= 0) {
        *wetL = 0.0f;
        *wetR = 0.0f;
        s->writePos = (s->writePos + 1) & BUFFER_MASK;
        return;
    }

    /* Pre-delay and APC are unreachable when activeSampleCount starts at 0. */
    s->preDelayBuffer[s->preDelayWritePos] = input;
    int rp = (s->preDelayWritePos - s->preDelayOffsetSamples + PREDELAY_BUFFER_SIZE)
             & PREDELAY_MASK;
    float delayedInput = s->preDelayBuffer[rp];
    s->preDelayWritePos = (s->preDelayWritePos + 1) & PREDELAY_MASK;

    if (fabsf(delayedInput) > APC_THRESHOLD)
        s->activeSampleCount = (int)(s->sampleRate * (1.0f + s->decay * 5.0f));
    else if (s->activeSampleCount > 0)
        s->activeSampleCount--;

    if (s->activeSampleCount <= 0) {
        *wetL = 0.0f;
        *wetR = 0.0f;
        s->writePos = (s->writePos + 1) & BUFFER_MASK;
        return;
    }

    /* FDN — same as correct stub. */
    float delayOut[FDN_CHANNELS];
    for (int ch = 0; ch < FDN_CHANNELS; ch++) {
        float delaySamples = s->delayTimes[ch] * s->sampleRate;
        float rdp = (float)s->writePos - delaySamples;
        while (rdp < 0)            rdp += BUFFER_SIZE;
        while (rdp >= BUFFER_SIZE) rdp -= BUFFER_SIZE;
        int idx = (int)rdp;
        delayOut[ch] = s->delayLine[idx][ch];
    }
    float mixed[FDN_CHANNELS];
    for (int i = 0; i < FDN_CHANNELS; i++) {
        float sum = 0.0f;
        for (int j = 0; j < FDN_CHANNELS; j++)
            sum += s->hadamard[i][j] * delayOut[j];
        mixed[i] = sum * fminf(0.99f, s->decay);
    }
    mixed[0] += delayedInput * (1.0f - s->decay);
    for (int ch = 0; ch < FDN_CHANNELS; ch++)
        s->delayLine[s->writePos][ch] = mixed[ch];
    s->writePos = (s->writePos + 1) & BUFFER_MASK;

    float leftRaw = 0.0f;
    for (int i = 0; i < 4; i++) leftRaw += mixed[i];
    *wetL = leftRaw * 0.25f;
    *wetR = *wetL;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

/* Test 1: 10 ms pre-delay timing and APC wake-up via delayedInput.
 * With the raw-input wake-up, the engine activates on frame 0 (input=1.0)
 * but produces no wet output until the pre-delay expires and the impulse
 * reaches the FDN at frame 480. */
static void test_predelay_timing_and_wakeup() {
    printf("Running Test 1: Pre-Delay Timing & APC Wake-up (10 ms)... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.1f, 10.0f /* 10 ms = 480 samples */);

    float wetL = 0.0f, wetR = 0.0f;

    /* Frame 0: impulse.  Raw-input check wakes engine.  Pre-delay hasn't
     * expired so delayedInput = 0.  FDN receives no input → wet = 0. */
    lab_process_sample(&rev, 1.0f, &wetL, &wetR);
    assert(fabsf(wetL) < 1e-6f && "Frame 0: engine active but FDN input still zero (pre-delay)");

    /* Frames 1..479: silence.  Engine stays active (activeSampleCount >> 480).
     * delayedInput = 0 still.  wet = 0. */
    for (int i = 1; i < 480; i++) {
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);
        assert(fabsf(wetL) < 1e-6f && "Pre-delay fill: wet must be zero before impulse arrives");
    }

    /* Frame 480: impulse hits FDN input.  Engine injects it into mixed[0].
     * Stereo mix-down produces non-zero wet immediately. */
    lab_process_sample(&rev, 0.0f, &wetL, &wetR);
    assert(fabsf(wetL) > 1e-6f && "Frame 480: FDN must produce non-zero wet when impulse arrives");

    printf("PASSED\n");
}

/* Test 2: sub-threshold input keeps engine asleep (noise floor rejection). */
static void test_apc_noise_floor_rejection() {
    printf("Running Test 2: APC Noise Floor Rejection... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.5f, 5.0f /* 240 samples */);

    float wetL = 0.0f, wetR = 0.0f;

    /* 1e-6f < APC_THRESHOLD (1e-5f): raw-input check does NOT fire. */
    lab_process_sample(&rev, 1e-6f, &wetL, &wetR);
    assert(fabsf(wetL) < 1e-9f && "Sub-threshold input must leave engine asleep");

    /* Wait for the sub-threshold value to clear the pre-delay. */
    for (int i = 1; i <= 240; i++)
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* Engine must still be asleep: wet = 0. */
    assert(fabsf(wetL) < 1e-9f && "Engine must stay asleep after sub-threshold pre-delay content");

    printf("PASSED\n");
}

/* Test 3: engine returns to sleep after tail expires. */
static void test_apc_tail_timeout() {
    printf("Running Test 3: APC Tail Timeout (Sleep Mode)... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.01f, 0.0f /* 0 ms pre-delay */);

    float wetL = 0.0f, wetR = 0.0f;

    /* Wake engine with an impulse. */
    lab_process_sample(&rev, 1.0f, &wetL, &wetR);
    assert(rev.activeSampleCount > 0 && "Impulse must wake the engine");

    int expectedActive = (int)(SAMPLE_RATE * (1.0f + 0.01f * 5.0f));

    /* Drain with silence until the counter reaches 1. */
    for (int i = 1; i < expectedActive; i++)
        lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* One more zero-input sample: counter hits 0 → engine sleeps. */
    lab_process_sample(&rev, 0.0f, &wetL, &wetR);

    /* Feed strictly sub-threshold value: raw-input check does NOT fire,
     * engine must stay asleep → wet = 0. */
    float bypass_in = 1e-7f;    /* well below APC_THRESHOLD = 1e-5f */
    lab_process_sample(&rev, bypass_in, &wetL, &wetR);
    assert(fabsf(wetL) < 1e-9f && "Engine must stay asleep with sub-threshold input after tail timeout");

    printf("PASSED\n");
}

/* Test 4: 0 ms pre-delay — raw-input wake-up produces wet output on frame 0.
 * This is the scenario that was permanently deadlocked before commit 8f8d255.
 * With delayedInput = raw input (write-before-read, delay = 0), the engine
 * must inject the impulse into the FDN immediately and produce wet audio. */
static void test_zero_predelay_raw_wakeup() {
    printf("Running Test 4: Zero Pre-delay Raw-Input Wake-up... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.5f, 0.0f /* 0 ms pre-delay */);

    /* Engine starts cold (activeSampleCount = 0). */
    assert(rev.activeSampleCount == 0);

    float wetL = 0.0f, wetR = 0.0f;

    /* Frame 0: impulse.  Raw-input check fires, engine activates.
     * Pre-delay = 0 so delayedInput = input = 1.0 (write-before-read).
     * FDN injects delayedInput into mixed[0], wet signal is non-zero. */
    lab_process_sample(&rev, 1.0f, &wetL, &wetR);
    assert(rev.activeSampleCount > 0 && "Engine must be active after impulse with 0 ms pre-delay");
    assert(fabsf(wetL) > 1e-6f    && "Engine must produce non-zero wet output on frame 0 (0 ms pre-delay)");

    printf("PASSED\n");
}

/* Test 5: DEADLOCK REGRESSION — demonstrate that the old control-flow
 * (bypass before pre-delay, no raw-input check) permanently stalls the
 * engine when activeSampleCount starts at 0.
 *
 * With pre-delay = 0 ms and the buggy ordering the APC wake-up code is
 * unreachable: the bypass guard fires first and returns, so delayedInput
 * is never computed, so activeSampleCount can never leave 0.
 * The reverb produces zero wet output for the entire duration regardless
 * of input amplitude.  This confirms why the raw-input check is essential. */
static void test_deadlock_regression() {
    printf("Running Test 5: Deadlock Regression (buggy stub must produce no wet)... ");

    ScalarLabirinto rev;
    lab_init(&rev);
    lab_set_params(&rev, 0.5f, 0.0f /* 0 ms pre-delay */);

    /* Start from cold state — exactly the hardware power-on condition. */
    assert(rev.activeSampleCount == 0);

    bool ever_produced_wet = false;

    /* Feed 1 second of loud audio (well above threshold). */
    for (int i = 0; i < (int)SAMPLE_RATE; i++) {
        float in = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * i / SAMPLE_RATE);
        float wL = 0.0f, wR = 0.0f;
        lab_process_sample_buggy(&rev, in, &wL, &wR);
        if (fabsf(wL) > 1e-9f) { ever_produced_wet = true; break; }
    }

    /* The buggy stub must NEVER wake up — the deadlock is reproduced. */
    assert(!ever_produced_wet &&
           "Buggy stub (bypass-before-predelay, no raw-input check) must deadlock");

    printf("PASSED (deadlock confirmed in buggy stub — fix is essential)\n");
}

int main() {
    printf("--- Labirinto Pre-Delay & APC Scalar Unit Tests ---\n");
    test_predelay_timing_and_wakeup();
    test_apc_noise_floor_rejection();
    test_apc_tail_timeout();
    test_zero_predelay_raw_wakeup();
    test_deadlock_regression();
    printf("--- All Tests Passed! ---\n");
    return 0;
}

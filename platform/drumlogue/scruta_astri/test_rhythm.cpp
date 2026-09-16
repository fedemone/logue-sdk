/**
 * @file test_rhythm.cpp
 * @brief Filter modes from the program, filter 2's opt-in howl, and the
 *        rhythmic add-ons.
 *
 * Covers:
 *   1. Filter modes are a pure function of the program number. Visiting the
 *      highpass / bandpass / notch bands and coming back must land on exactly
 *      the sound program 0 has when reached directly -- that is what "not
 *      latched" means, and it is the property the old LFO-wave hijack broke.
 *   2. Filter 2 is silent on a silent input below the F2Res howl threshold and
 *      self-oscillates above it. Both directions matter: the old filter howled
 *      unconditionally, so nothing downstream could ever be quiet.
 *   3. The rhythmic add-ons are inert until LFO 1 or LFO 2 carries a strike
 *      shape, each one changes the sound, and none of them breaks the output.
 *
 * Compile: arm-linux-gnueabihf-g++ -O2 -march=armv7-a -mtune=cortex-a7 -marm \
 *            -mfloat-abi=hard -mfpu=neon-vfpv4 -fno-math-errno -Wno-psabi \
 *            -D__ARM_NEON__ -I. -I../common -static -o test_rhythm test_rhythm.cpp -lm
 * Run:     qemu-arm ./test_rhythm
 */

#include "unit.h"
#include "synth.h"

#include <stdio.h>
#include <math.h>

#define BLOCK 64

static int failures = 0;
#define CHECK(cond, ...)                    \
    do {                                    \
        if (!(cond)) {                      \
            printf("  FAIL: " __VA_ARGS__); \
            printf("\n");                   \
            failures++;                     \
        }                                   \
    } while (0)

struct Meas { float rms, peak; int bad; };

static void common_patch(ScrutaAstri &s) {
    s.setParameter(ScrutaAstri::k_paramNote, 36);
    s.setParameter(ScrutaAstri::k_paramOsc1Wave, 32);
    s.setParameter(ScrutaAstri::k_paramOsc2Wave, 40);
    s.setParameter(ScrutaAstri::k_paramOsc2Mix, 50);
    s.setParameter(ScrutaAstri::k_paramMastrVol, 80);
    s.setParameter(ScrutaAstri::k_paramF1Cutoff, 200);
    s.setParameter(ScrutaAstri::k_paramF2Cutoff, 200);
}

static Meas measure(ScrutaAstri &s, int settle_blocks, int meas_blocks) {
    float buf[2 * BLOCK];
    Meas m = { 0.0f, 0.0f, 0 };
    for (int b = 0; b < settle_blocks; ++b) s.processBlock(buf, BLOCK);
    double acc = 0.0;
    for (int b = 0; b < meas_blocks; ++b) {
        s.processBlock(buf, BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            float v = buf[2 * i];
            if (isnan(v) || isinf(v) || fabsf(v) > 1.0f) m.bad++;
            if (fabsf(v) > m.peak) m.peak = fabsf(v);
            acc += (double)v * v;
        }
    }
    m.rms = sqrtf((float)(acc / (meas_blocks * BLOCK)));
    return m;
}

/* ---------------------------------------------------------------------------
 * 1. Filter modes follow the program, and nothing survives leaving it
 * ------------------------------------------------------------------------ */
static float program_rms(const int *visit, int n) {
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    float buf[2 * BLOCK];
    for (int i = 0; i < n; ++i) {
        s.setParameter(ScrutaAstri::k_paramProgram, visit[i]);
        s.NoteOn(36, 127);
        /* let each visited program actually run before moving on */
        for (int b = 0; b < 300; ++b) s.processBlock(buf, BLOCK);
    }
    return measure(s, 300, 600).rms;
}

static void test_filter_modes(void) {
    const int direct0[] = { 0 };
    const int viaModes[] = { 96, 120, 144, 168, 192, 216, 0 };
    const int prog96[] = { 96 };
    const int prog168[] = { 168 };

    float a = program_rms(direct0, 1);
    float b = program_rms(viaModes, 7);
    float hp1 = program_rms(prog96, 1);
    float hp2 = program_rms(prog168, 1);

    printf("  program 0 reached directly            RMS %.5f\n", a);
    printf("  program 0 after visiting 96..216      RMS %.5f\n", b);
    printf("  program 96  (filter 1 highpass)       RMS %.5f\n", hp1);
    printf("  program 168 (filter 2 highpass)       RMS %.5f\n", hp2);

    const float drift = fabsf(a - b) / fmaxf(a, 1e-6f);
    CHECK(drift < 0.05f,
          "program 0 sounds %.1f%% different after visiting the filter-mode bands"
          " -- a mode is latching", drift * 100.0f);
    CHECK(fabsf(a - hp1) / fmaxf(a, 1e-6f) > 0.05f,
          "program 96 is indistinguishable from program 0 -- filter 1 highpass is not engaging");
    CHECK(fabsf(a - hp2) / fmaxf(a, 1e-6f) > 0.05f,
          "program 168 is indistinguishable from program 0 -- filter 2 highpass is not engaging");
}

/* ---------------------------------------------------------------------------
 * 2. Filter 2 howls only above the F2Res threshold
 * ------------------------------------------------------------------------ */
static float gate_db(int f2res) {
    /* Preset 12 gates Osc 1 with an AR strike; Osc 2 is muted, so between
       strikes the whole engine is fed silence and only filter 2 can still ring. */
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    s.setParameter(ScrutaAstri::k_paramOsc2Mix, 0);
    s.setParameter(ScrutaAstri::k_paramF2Cutoff, 610);
    s.setParameter(ScrutaAstri::k_paramF2Reso, f2res);
    s.setParameter(ScrutaAstri::k_paramL1Rate, 50);
    s.setParameter(ScrutaAstri::k_paramL1Wave, LFO_AR_PERC);
    s.setParameter(ScrutaAstri::k_paramL1Depth, 100);
    s.setParameter(ScrutaAstri::k_paramProgram, 12);
    s.NoteOn(36, 127);

    float buf[2 * BLOCK];
    for (int b = 0; b < 600; ++b) s.processBlock(buf, BLOCK);

    float lo = 1e9f, hi = 0.0f;
    for (int slot = 0; slot < 60; ++slot) {
        double acc = 0.0;
        for (int b = 0; b < 4; ++b) {
            s.processBlock(buf, BLOCK);
            for (int i = 0; i < BLOCK; ++i) acc += (double)buf[2 * i] * buf[2 * i];
        }
        float r = sqrtf((float)(acc / (4 * BLOCK)));
        if (r < lo) lo = r;
        if (r > hi) hi = r;
    }
    return lo > 1e-7f ? 20.0f * log10f(lo / hi) : -99.0f;
}

static void test_howl(void) {
    const int res[] = { 0, 40, 67, 80, 100 };
    float g[5];
    for (int i = 0; i < 5; ++i) {
        g[i] = gate_db(res[i]);
        printf("  F2Res %3d -> gate %6.1f dB   %s\n", res[i], g[i],
               g[i] < -40.0f ? "silent between strikes" : "filter 2 keeps sounding");
    }
    CHECK(g[0] < -40.0f, "F2Res 0 does not go silent (%.1f dB) -- filter 2 still howls by default", g[0]);
    CHECK(g[1] < -40.0f, "F2Res 40 does not go silent (%.1f dB)", g[1]);
    CHECK(g[4] > -40.0f, "F2Res 100 goes silent (%.1f dB) -- the howl is unreachable", g[4]);
}

/* ---------------------------------------------------------------------------
 * 3. Rhythmic add-ons
 * ------------------------------------------------------------------------ */
static Meas addon_run(int l1wave, int l3wave, int o1wave, int o2wave) {
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    s.setParameter(ScrutaAstri::k_paramOsc1Wave, o1wave);
    s.setParameter(ScrutaAstri::k_paramOsc2Wave, o2wave);
    s.setParameter(ScrutaAstri::k_paramF2Reso, 40);
    s.setParameter(ScrutaAstri::k_paramL1Rate, 55);
    s.setParameter(ScrutaAstri::k_paramL1Wave, l1wave);
    s.setParameter(ScrutaAstri::k_paramL1Depth, 100);
    s.setParameter(ScrutaAstri::k_paramL3Wave, l3wave);
    s.setParameter(ScrutaAstri::k_paramProgram, 12);
    s.NoteOn(36, 127);
    return measure(s, 300, 900);
}

/* Share of 10 ms windows sitting below 5% of the loudest one. A skipped
   euclidean step leaves a real hole; a different wavetable never does. */
static float quiet_share(int l1wave, int l3wave, int oscwave) {
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    s.setParameter(ScrutaAstri::k_paramOsc1Wave, oscwave);
    s.setParameter(ScrutaAstri::k_paramOsc2Wave, oscwave);
    s.setParameter(ScrutaAstri::k_paramOsc2Mix, 0);
    s.setParameter(ScrutaAstri::k_paramF2Reso, 40);
    s.setParameter(ScrutaAstri::k_paramL1Rate, 55);
    s.setParameter(ScrutaAstri::k_paramL1Wave, l1wave);
    s.setParameter(ScrutaAstri::k_paramL1Depth, 100);
    s.setParameter(ScrutaAstri::k_paramL3Wave, l3wave);
    s.setParameter(ScrutaAstri::k_paramProgram, 12);
    s.NoteOn(36, 127);

    float buf[2 * BLOCK];
    for (int b = 0; b < 400; ++b) s.processBlock(buf, BLOCK);

    const int kWins = 240;
    float w[kWins];
    float hi = 0.0f;
    for (int i = 0; i < kWins; ++i) {
        double acc = 0.0;
        for (int b = 0; b < 8; ++b) {
            s.processBlock(buf, BLOCK);
            for (int k = 0; k < BLOCK; ++k) acc += (double)buf[2 * k] * buf[2 * k];
        }
        w[i] = sqrtf((float)(acc / (8 * BLOCK)));
        if (w[i] > hi) hi = w[i];
    }
    int quiet = 0;
    for (int i = 0; i < kWins; ++i) if (w[i] < hi * 0.05f) quiet++;
    return (float)quiet / (float)kWins;
}

static void test_addons(void) {
    static const char * const names[RHYTHMIC_ADD_ONS] = {
        "SeqSync", "Euclid", "SrrZap", "BitSnap", "FiltPluck", "NoiseBurst", "SHPitch", "Accent"
    };

    /* Disarmed: LFO 1 is a plain triangle, so L3Wave must not matter. */
    printf("  disarmed (LFO 1 = triangle), sweeping L3Wave:\n");
    float base = -1.0f;
    bool moved = false;
    for (int w = 0; w < RHYTHMIC_ADD_ONS; ++w) {
        Meas m = addon_run(LFO_TRIANGLE, w, 32, 40);
        if (base < 0.0f) base = m.rms;
        else if (fabsf(m.rms - base) / fmaxf(base, 1e-6f) > 0.001f) moved = true;
        CHECK(m.bad == 0, "disarmed L3Wave %d produced %d bad samples", w, m.bad);
    }
    printf("    RMS %.5f for every L3Wave, %s\n", base, moved ? "MOVED" : "identical");
    CHECK(!moved, "add-ons are doing something with no strike shape armed");

    /* Armed: each add-on should leave its own mark. */
    printf("  armed (LFO 1 = AR strike), one add-on at a time:\n");
    float rms[RHYTHMIC_ADD_ONS];
    for (int w = 0; w < RHYTHMIC_ADD_ONS; ++w) {
        /* Park both oscillator wave knobs on the same add-on so exactly one is live. */
        Meas m = addon_run(LFO_AR_PERC, w, w, w);
        rms[w] = m.rms;
        printf("    %-10s RMS %.5f  peak %.4f  bad %d\n", names[w], m.rms, m.peak, m.bad);
        CHECK(m.bad == 0, "add-on %s produced %d non-finite or clipping samples", names[w], m.bad);
    }

    int distinct = 0;
    for (int i = 0; i < RHYTHMIC_ADD_ONS; ++i) {
        bool uniq = true;
        for (int j = 0; j < i; ++j)
            if (fabsf(rms[i] - rms[j]) / fmaxf(rms[j], 1e-6f) < 0.005f) uniq = false;
        if (uniq) distinct++;
    }
    printf("    %d of %d add-ons produce a distinct level\n", distinct, RHYTHMIC_ADD_ONS);
    CHECK(distinct >= RHYTHMIC_ADD_ONS - 2,
          "only %d of %d add-ons change anything measurable", distinct, RHYTHMIC_ADD_ONS);

    /* Stacking: three different picks must differ from any one of them. */
    Meas stacked = addon_run(LFO_AR_PERC, k_rhEuclid, k_rhNoiseBurst, k_rhBitSnap);
    printf("  stacked Euclid + NoiseBurst + BitSnap: RMS %.5f  bad %d\n", stacked.rms, stacked.bad);
    CHECK(stacked.bad == 0, "stacked add-ons produced %d bad samples", stacked.bad);
    CHECK(fabsf(stacked.rms - rms[k_rhEuclid]) / fmaxf(rms[k_rhEuclid], 1e-6f) > 0.005f,
          "stacking three add-ons sounds the same as Euclid alone");

    /* Oscillator wave knobs only select an add-on when parked below 8. Wave 1
       and wave 129 are the same index modulo RHYTHMIC_ADD_ONS, so a plain
       modulo would pick Euclid for both. Moving the knob also moves the
       wavetable, so level is no use as evidence -- what Euclid does that a
       wavetable cannot is leave whole stretches silent, which is measured
       directly as the share of quiet windows. */
    /* An AR strike already leaves most windows quiet on its own, so the number
       that matters is how much Euclid adds on top of that baseline. */
    const float qLo = quiet_share(LFO_AR_PERC, k_rhSeqSync, 1);
    const float qHi = quiet_share(LFO_AR_PERC, k_rhSeqSync, 129);
    printf("  wave 1   (Euclid stacked)   : %.0f%% of windows silent\n", qLo * 100.0f);
    printf("  wave 129 (nothing stacked)  : %.0f%% of windows silent  <- strike baseline\n",
           qHi * 100.0f);
    CHECK(qLo - qHi > 0.20f,
          "wave 1 adds only %.0f points of silence over the baseline -- Euclid is not stacking",
          (qLo - qHi) * 100.0f);
    CHECK(qHi < 0.75f,
          "wave 129 is already %.0f%% silent -- it looks like Euclid is running there too,"
          " so the below-8 gate is not holding", qHi * 100.0f);
}

int main(void) {
    printf("=== ScrutaAstri program layout / howl / rhythm tests ===\n");
    printf("\n=== 1. Filter modes follow the program and do not latch ===\n");
    test_filter_modes();
    printf("\n=== 2. Filter 2 howls only above the F2Res threshold ===\n");
    test_howl();
    printf("\n=== 3. Rhythmic add-ons ===\n");
    test_addons();

    if (failures == 0) {
        printf("\n=== ALL ScrutaAstri RHYTHM TESTS PASSED ===\n");
        return 0;
    }
    printf("\n=== %d ScrutaAstri RHYTHM TEST FAILURE(S) ===\n", failures);
    return 1;
}

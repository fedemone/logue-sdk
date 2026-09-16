/**
 * @file test_rhythm.cpp
 * @brief Filter modes from the program, the opt-in howl on both filters, the
 *        rhythmic add-ons, and the CMOS Sherman zone.
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
 *   4. CMOS above 66 engages the Sherman wavefolder. The zone was inert for as
 *      long as its asymmetry divided by the wrong constant, and inert is hard
 *      to notice by ear on a knob that is already saturating -- so it is
 *      measured as harmonic content, which folding adds and level does not.
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
static float gate_db(int f1res, int f2res, int cmos) {
    /* Preset 12 gates Osc 1 with an AR strike; Osc 2 is muted, so between
       strikes the whole engine is fed silence and only a filter can still ring. */
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    s.setParameter(ScrutaAstri::k_paramOsc2Mix, 0);
    s.setParameter(ScrutaAstri::k_paramF2Cutoff, 610);
    s.setParameter(ScrutaAstri::k_paramF1Reso, f1res);
    s.setParameter(ScrutaAstri::k_paramF2Reso, f2res);
    s.setParameter(ScrutaAstri::k_paramCMOSDist, cmos);
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
    float g2[5], g1[5];

    printf("  filter 2, driven from F2Res:\n");
    for (int i = 0; i < 5; ++i) {
        g2[i] = gate_db(0, res[i], 0);
        printf("    F2Res %3d -> gate %6.1f dB   %s\n", res[i], g2[i],
               g2[i] < -40.0f ? "silent between strikes" : "filter 2 keeps sounding");
    }
    CHECK(g2[0] < -40.0f, "F2Res 0 does not go silent (%.1f dB) -- filter 2 still howls by default", g2[0]);
    CHECK(g2[1] < -40.0f, "F2Res 40 does not go silent (%.1f dB)", g2[1]);
    CHECK(g2[4] > -40.0f, "F2Res 100 goes silent (%.1f dB) -- the howl is unreachable", g2[4]);

    printf("  filter 1, driven from F1Res:\n");
    for (int i = 0; i < 5; ++i) {
        g1[i] = gate_db(res[i], 0, 0);
        printf("    F1Res %3d -> gate %6.1f dB   %s\n", res[i], g1[i],
               g1[i] < -40.0f ? "silent between strikes" : "filter 1 keeps sounding");
    }
    /* F1Res above 0 also raises filter 1's drive, which is what used to put its
       integrators through fast_tanh and set it singing on its own. */
    CHECK(g1[0] < -40.0f, "F1Res 0 does not go silent (%.1f dB) -- filter 1 still howls by default", g1[0]);
    CHECK(g1[1] < -40.0f, "F1Res 40 does not go silent (%.1f dB) -- drive alone is setting filter 1 off", g1[1]);
    CHECK(g1[2] < -40.0f, "F1Res 67 does not go silent (%.1f dB) -- the threshold is leaking", g1[2]);
    CHECK(g1[4] > -40.0f, "F1Res 100 goes silent (%.1f dB) -- filter 1's howl is unreachable", g1[4]);

    /* Maximum distortion drives filter 1 hard without asking for a howl. */
    const float gc = gate_db(0, 0, 100);
    printf("  CMOS 100, both resonances 0 -> gate %6.1f dB\n", gc);
    CHECK(gc < -40.0f, "full distortion stops the gate reaching silence (%.1f dB)", gc);
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

/* ---------------------------------------------------------------------------
 * 4. The CMOS Sherman zone
 * ------------------------------------------------------------------------ */
/* Zero crossings per second. The wavefolder tears extra crossings into the
   waveform; a gain or makeup change cannot, which is what makes this tell a
   live wavefolder apart from a merely louder one. */
static float crossings(int cmos, int *bad_out, float *peak_out) {
    ScrutaAstri s;
    unit_runtime_desc_t d = {};
    d.samplerate = 48000;
    d.output_channels = 2;
    s.Init(&d);
    common_patch(s);
    /* Both filters wide open: the wavefolder's product is harmonic content high
       above the fundamental, and a 5 kHz lowpass removes the evidence. */
    s.setParameter(ScrutaAstri::k_paramF1Cutoff, 1000);
    s.setParameter(ScrutaAstri::k_paramF2Cutoff, 1000);
    s.setParameter(ScrutaAstri::k_paramCMOSDist, cmos);
    s.setParameter(ScrutaAstri::k_paramProgram, 0);
    s.NoteOn(36, 127);

    float buf[2 * BLOCK];
    for (int b = 0; b < 300; ++b) s.processBlock(buf, BLOCK);

    int zx = 0, n = 0, bad = 0;
    float prev = 0.0f, pk = 0.0f;
    for (int b = 0; b < 1200; ++b) {
        s.processBlock(buf, BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            float v = buf[2 * i];
            if (isnan(v) || isinf(v) || fabsf(v) > 1.0f) bad++;
            if (fabsf(v) > pk) pk = fabsf(v);
            if ((v >= 0.0f) != (prev >= 0.0f)) zx++;
            prev = v;
            n++;
        }
    }
    *bad_out = bad;
    *peak_out = pk;
    return zx * 48000.0f / (float)n;
}

static void test_sherman(void) {
    const int cmos[] = { 0, 40, 66, 67, 85, 100 };
    float zx[6], pk[6];
    int bad[6];
    for (int i = 0; i < 6; ++i) {
        zx[i] = crossings(cmos[i], &bad[i], &pk[i]);
        printf("  CMOS %3d -> %6.0f zero crossings/s   peak %.4f   bad %d\n",
               cmos[i], zx[i], pk[i], bad[i]);
        CHECK(bad[i] == 0, "CMOS %d produced %d non-finite or clipping samples", cmos[i], bad[i]);
    }
    /* 0..66 is flat: Moog saturation rounds the waveform, it does not fold it,
       so it adds no crossings. The asymmetry only ramps from 0 at 67, and the
       fold threshold is not reached often until it is well up, so 67 itself is
       a few percent and the evidence is at the top of the zone. If the divisor
       regressed, every value from 67 up would sit back down at the 66 figure. */
    CHECK(zx[2] < zx[0] * 1.3f,
          "CMOS 66 already looks folded (%.0f vs %.0f crossings/s at 0) --"
          " the zone border has moved", zx[2], zx[0]);
    CHECK(zx[4] > zx[2] * 1.5f,
          "CMOS 85 adds no harmonics over 66 (%.0f vs %.0f crossings/s) --"
          " the wavefolder is not engaging", zx[4], zx[2]);
    CHECK(zx[5] > zx[2] * 2.0f,
          "CMOS 100 adds little over 66 (%.0f vs %.0f crossings/s) --"
          " the Sherman zone is not ramping", zx[5], zx[2]);
    CHECK(zx[5] > zx[3],
          "the Sherman zone is not monotonic (67 -> %.0f, 100 -> %.0f crossings/s)",
          zx[3], zx[5]);
}

int main(void) {
    printf("=== ScrutaAstri program layout / howl / rhythm tests ===\n");
    printf("\n=== 1. Filter modes follow the program and do not latch ===\n");
    test_filter_modes();
    printf("\n=== 2. Filter 2 howls only above the F2Res threshold ===\n");
    test_howl();
    printf("\n=== 3. Rhythmic add-ons ===\n");
    test_addons();
    printf("\n=== 4. CMOS Sherman zone engages above 66 ===\n");
    test_sherman();

    if (failures == 0) {
        printf("\n=== ALL ScrutaAstri RHYTHM TESTS PASSED ===\n");
        return 0;
    }
    printf("\n=== %d ScrutaAstri RHYTHM TEST FAILURE(S) ===\n", failures);
    return 1;
}

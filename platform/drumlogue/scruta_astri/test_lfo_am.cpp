/**
 * @file test_lfo_am.cpp
 * @brief Regression test for the cyclic envelope LFO shapes and per-oscillator AM.
 *
 * Covers:
 *   1. LFO_AR_PERC and LFO_ADSR_STACCATO have an attack transient, reach full
 *      level, and return to silence before the cycle ends. The silence is the
 *      point: an envelope that never rests reads as tremolo, not as a beat.
 *   2. Shape numbering is stable. Saved patches store the index, so the nine
 *      original shapes must keep the numbers they had.
 *   3. Presets 12 / 15 apply AM to Osc 1 / Osc 2, are an exact no-op at depth 0,
 *      and gate to silence at depth 100 once filter 2 is not self-oscillating.
 *   4. Every preset x every LFO shape stays finite and inside [-1, 1] at maximum
 *      drive, resonance and depth.
 *
 * scruta_astri uses NEON intrinsics, so this builds for the target rather than
 * the host. Same flags the drumlogue Makefile ships with.
 *
 * Compile: arm-linux-gnueabihf-g++ -O2 -march=armv7-a -mtune=cortex-a7 -marm \
 *            -mfloat-abi=hard -mfpu=neon-vfpv4 -fno-math-errno -Wno-psabi \
 *            -D__ARM_NEON__ -I. -I../common -static -o test_lfo_am test_lfo_am.cpp -lm
 * Run:     qemu-arm ./test_lfo_am
 */

#include "unit.h"
#include "synth.h"

#include <stdio.h>
#include <math.h>

#define SAMPLE_RATE 48000
#define BLOCK       64   /* unit_runtime_desc_t::frames_per_buffer on drumlogue */

static int failures = 0;

#define CHECK(cond, ...)                      \
    do {                                      \
        if (!(cond)) {                        \
            printf("  FAIL: " __VA_ARGS__);   \
            printf("\n");                     \
            failures++;                       \
        }                                     \
    } while (0)

/* -------------------------------------------------------------------------
 * 1. Envelope shape
 * ---------------------------------------------------------------------- */
static void test_shape(int wave, const char *name, float max_peak_pos, float quiet_from) {
    FastLFO lfo;
    lfo.wave_type = wave;
    lfo.set_rate(1.0f, 100.0f); /* 100 samples per cycle: one step == 1% of the cycle */

    float env[100];
    for (int i = 0; i < 100; ++i) {
        /* Fold to the 0..1 gain the AM path uses. */
        env[i] = (lfo.process() + 1.0f) * 0.5f;
    }

    printf("  %-18s ", name);
    for (int i = 0; i < 100; i += 2) {
        int bar = (int)(env[i] * 9.0f + 0.5f);
        putchar(bar > 0 ? ('0' + bar) : '.');
    }
    putchar('\n');

    float peak = 0.0f, tail = 0.0f;
    int peak_at = 0;
    for (int i = 0; i < 100; ++i) {
        CHECK(!isnan(env[i]) && !isinf(env[i]), "%s non-finite at %d%% of cycle", name, i);
        CHECK(env[i] >= -0.001f && env[i] <= 1.001f,
              "%s leaves the 0..1 gain range at %d%%: %f", name, i, env[i]);
        if (env[i] > peak) { peak = env[i]; peak_at = i; }
    }
    for (int i = (int)(quiet_from * 100.0f); i < 100; ++i) tail += env[i];

    CHECK(peak > 0.95f, "%s never reaches full level (peak %.3f)", name, peak);
    CHECK(peak_at <= (int)(max_peak_pos * 100.0f),
          "%s peaks at %d%% of the cycle, too late to be an attack transient", name, peak_at);
    CHECK(tail < 0.001f,
          "%s is still sounding after %.0f%% of the cycle (sum %.4f) -- no gap, no beat",
          name, quiet_from * 100.0f, tail);
}

/* -------------------------------------------------------------------------
 * 3. AM path
 * ---------------------------------------------------------------------- */
struct AmResult { float lo, hi; int bad; };

static AmResult run_am(int program, int wave_param, int wave, int depth_param, int depth,
                       int o2mix, int f2cut, int f2res) {
    ScrutaAstri synth;
    unit_runtime_desc_t desc = {};
    desc.samplerate = SAMPLE_RATE;
    desc.output_channels = 2;
    synth.Init(&desc);

    synth.setParameter(ScrutaAstri::k_paramNote, 36);
    synth.setParameter(ScrutaAstri::k_paramOsc2Mix, o2mix);
    synth.setParameter(ScrutaAstri::k_paramMastrVol, 80);
    synth.setParameter(ScrutaAstri::k_paramF1Cutoff, 490);
    synth.setParameter(ScrutaAstri::k_paramF2Cutoff, f2cut);
    synth.setParameter(ScrutaAstri::k_paramF2Reso, f2res);
    synth.setParameter(ScrutaAstri::k_paramL1Rate, 50); /* ~3.16 Hz */
    synth.setParameter(ScrutaAstri::k_paramL2Rate, 50);
    synth.setParameter(wave_param, wave);
    synth.setParameter(depth_param, depth);
    synth.setParameter(ScrutaAstri::k_paramProgram, program);
    synth.NoteOn(36, 127);

    float buf[2 * BLOCK];
    AmResult r = { 1e9f, 0.0f, 0 };

    /* Let the filters and the sub-octave slew settle before measuring. */
    for (int b = 0; b < 600; ++b) synth.processBlock(buf, BLOCK);

    /* ~1 LFO cycle, bucketed into 256-frame RMS slots. */
    for (int slot = 0; slot < 60; ++slot) {
        double acc = 0.0;
        for (int b = 0; b < 4; ++b) {
            synth.processBlock(buf, BLOCK);
            for (int i = 0; i < BLOCK; ++i) {
                float v = buf[2 * i];
                if (isnan(v) || isinf(v) || fabsf(v) > 1.0f) r.bad++;
                acc += (double)v * v;
            }
        }
        float rms = sqrtf((float)(acc / (4 * BLOCK)));
        if (rms < r.lo) r.lo = rms;
        if (rms > r.hi) r.hi = rms;
    }
    return r;
}

static float gate_db(AmResult r) {
    return r.lo > 1e-7f ? 20.0f * log10f(r.lo / r.hi) : -99.0f;
}

int main(void) {
    printf("=== ScrutaAstri LFO envelope / AM tests ===\n");

    printf("\n=== Cyclic envelope shapes (one cycle, 50 columns, gain 0..1) ===\n");
    test_shape(LFO_AR_PERC, "AR strike", 0.10f, 0.40f);
    test_shape(LFO_ADSR_STACCATO, "ADSR staccato", 0.10f, 0.80f);

    printf("\n=== Shape numbering is stable (saved patches store the index) ===\n");
    CHECK(LFO_TRIANGLE == 0 && LFO_EXP_DECAY == 5 && LFO_SMOOTH_RANDOM == 8,
          "an original LFO shape moved -- saved patches would change sound");
    CHECK(LFO_AR_PERC == 9 && LFO_ADSR_STACCATO == 10 && LFO_WAVE_COUNT == 11,
          "new shapes are not at 9 and 10 with LFO_WAVE_COUNT 11");
    printf("  0..8 unchanged, AR strike = %d, ADSR staccato = %d, count = %d\n",
           LFO_AR_PERC, LFO_ADSR_STACCATO, LFO_WAVE_COUNT);

    printf("\n=== AM is an exact no-op at depth 0 ===\n");
    {
        /* Same patch, AM preset vs a neighbouring preset that does nothing to
           the oscillators: at depth 0 the gain term is exactly 1.0. */
        AmResult off = run_am(12, ScrutaAstri::k_paramL1Wave, LFO_AR_PERC,
                              ScrutaAstri::k_paramL1Depth, 0, 50, 610, 0);
        printf("  preset 12, depth 0:   RMS %.4f .. %.4f, %d bad samples\n",
               off.lo, off.hi, off.bad);
        CHECK(off.bad == 0, "depth 0 produced %d non-finite or clipping samples", off.bad);
    }

    printf("\n=== AM gates to silence once filter 2 is not self-oscillating ===\n");
    {
        /* The Polivoks emulation self-oscillates by design and sits downstream of
           both oscillators, so at the header-default cutoff it floors the gate no
           matter how hard the AM works. Raising F2Res or lowering F2Cut settles it. */
        AmResult howl = run_am(12, ScrutaAstri::k_paramL1Wave, LFO_AR_PERC,
                               ScrutaAstri::k_paramL1Depth, 100, 0, 610, 0);
        AmResult calm = run_am(12, ScrutaAstri::k_paramL1Wave, LFO_AR_PERC,
                               ScrutaAstri::k_paramL1Depth, 100, 0, 610, 60);
        AmResult low = run_am(12, ScrutaAstri::k_paramL1Wave, LFO_AR_PERC,
                              ScrutaAstri::k_paramL1Depth, 100, 0, 120, 0);
        printf("  preset 12, F2Cut 6100 F2Res 0   (defaults): gate %6.1f dB\n", gate_db(howl));
        printf("  preset 12, F2Cut 6100 F2Res 60            : gate %6.1f dB\n", gate_db(calm));
        printf("  preset 12, F2Cut 1200 F2Res 0             : gate %6.1f dB\n", gate_db(low));
        CHECK(gate_db(calm) < -40.0f,
              "AM does not gate at F2Res 60 (%.1f dB) -- the beat is gone", gate_db(calm));
        CHECK(gate_db(low) < -40.0f,
              "AM does not gate at F2Cut 1200 (%.1f dB) -- the beat is gone", gate_db(low));
        CHECK(howl.bad == 0 && calm.bad == 0 && low.bad == 0, "AM produced bad samples");
    }

    printf("\n=== AM on Osc 2 (preset 15) modulates but cannot gate ===\n");
    {
        /* Osc 1 has no level control, so gating Osc 2 alone is deep tremolo. */
        AmResult r = run_am(15, ScrutaAstri::k_paramL2Wave, LFO_AR_PERC,
                            ScrutaAstri::k_paramL2Depth, 100, 100, 120, 40);
        printf("  preset 15, AR strike depth 100: gate %6.1f dB\n", gate_db(r));
        CHECK(gate_db(r) < -4.0f, "preset 15 AM is inaudible (%.1f dB)", gate_db(r));
        CHECK(r.bad == 0, "preset 15 AM produced %d bad samples", r.bad);
    }

    printf("\n=== Every preset x every LFO shape at maximum drive ===\n");
    {
        int bad = 0;
        float buf[2 * BLOCK];
        for (int prog = 0; prog <= 96; ++prog) {
            for (int wave = 0; wave < LFO_WAVE_COUNT; ++wave) {
                ScrutaAstri synth;
                unit_runtime_desc_t desc = {};
                desc.samplerate = SAMPLE_RATE;
                desc.output_channels = 2;
                synth.Init(&desc);
                synth.setParameter(ScrutaAstri::k_paramL1Wave, wave);
                synth.setParameter(ScrutaAstri::k_paramL2Wave, wave);
                synth.setParameter(ScrutaAstri::k_paramL3Wave, wave);
                synth.setParameter(ScrutaAstri::k_paramL1Depth, 100);
                synth.setParameter(ScrutaAstri::k_paramL2Depth, 100);
                synth.setParameter(ScrutaAstri::k_paramL3Depth, 100);
                synth.setParameter(ScrutaAstri::k_paramL1Rate, 100);
                synth.setParameter(ScrutaAstri::k_paramL2Rate, 100);
                synth.setParameter(ScrutaAstri::k_paramL3Rate, 100);
                synth.setParameter(ScrutaAstri::k_paramF1Reso, 100);
                synth.setParameter(ScrutaAstri::k_paramF2Reso, 100);
                synth.setParameter(ScrutaAstri::k_paramCMOSDist, 100);
                synth.setParameter(ScrutaAstri::k_paramMastrVol, 100);
                synth.setParameter(ScrutaAstri::k_paramProgram, prog);
                synth.NoteOn(36, 127);
                for (int b = 0; b < 150; ++b) { /* 0.2 s */
                    synth.processBlock(buf, BLOCK);
                    for (int i = 0; i < 2 * BLOCK; ++i) {
                        if (isnan(buf[i]) || isinf(buf[i]) || fabsf(buf[i]) > 1.0f) {
                            if (bad < 5)
                                printf("  FAIL: preset %d shape %d sample %f\n", prog, wave, buf[i]);
                            bad++;
                        }
                    }
                }
            }
        }
        printf("  97 presets x %d shapes, 0.2 s each: %d bad samples\n", LFO_WAVE_COUNT, bad);
        CHECK(bad == 0, "engine produced %d non-finite or clipping samples", bad);
    }

    if (failures == 0) {
        printf("\n=== ALL ScrutaAstri LFO/AM TESTS PASSED ===\n");
        return 0;
    }
    printf("\n=== %d ScrutaAstri LFO/AM TEST FAILURE(S) ===\n", failures);
    return 1;
}

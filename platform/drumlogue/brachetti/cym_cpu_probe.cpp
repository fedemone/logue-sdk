/**
 * cym_cpu_probe.cpp — where does an ENGINE_CYMBAL voice actually spend its
 * time?  (host-only, throwaway)
 *
 * HW report: "cymbal very quiet, additional hit leads to silence (audio
 * crash)"; "gong: voice stealing audible, crackling then silence, possible CPU
 * overload."  Pass 26 raised the cymbal cap from 2 voices to 4 and replaced the
 * voice-count guard with kCymResonatorBudget, a ceiling on the TOTAL resonator
 * count.  That only bounds the resonator loop.  Everything else in
 * cymbal_process — pink noise, TWO swept one-poles (a fastexpf each), three PM
 * sinusoids, the DC blocker — is a FIXED per-voice cost the budget does not
 * see, and pass 26 doubled how many voices pay it.
 *
 * Part A decomposes cost = fixed + k * resonators by timing cymbal_process at
 * several bank sizes.  Part B times the real processBlock for 1..4 stacked
 * gong voices and prints the aggregate bank the budget actually hands out.
 *
 * Host is scalar (no NEON), so the resonator loop is ~4x cheaper on target
 * relative to the fixed part: whatever share the fixed cost shows here is a
 * LOWER bound for the device.
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *            -DRUNTIME_COMMON_H_ cym_cpu_probe.cpp -o /tmp/cymcpu && /tmp/cymcpu
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0, ut_delay_read = 0, ut_voice_out = 0;
#include "synth_engine.h"

static unit_runtime_desc_t D;
static BrachettiSynth s;
static CymbalVoice g_cv;

static double now_ns() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

// Keep the voice alive for the whole timing run: the squelch and endSample
// would otherwise retire it and we would be timing `if (!active) return 0`.
static void keep_alive(CymbalVoice& c) {
    c.active = true;
    c.sampleIndex = 0;
    c.endSample = 0xFFFFFFFFu;
    c.magEnv = 1.0f;
}

static double time_bank(int resonators, int nsamples) {
    static const float anchors[8] = { 150.f, 233.f, 337.f, 461.f,
                                      613.f, 787.f, 971.f, 1201.f };
    CymbalConfig cc{};
    cc.freqHz = anchors; cc.freqCount = 8;
    cc.resonators = (uint16_t)resonators;
    cc.minHz = 150.f; cc.maxHz = 14000.f; cc.jitterSemis = 2.4f;
    cc.lowAttackSec = 0.25f; cc.decaySec = 1.70f;
    cc.highAttackSec = 0.50f; cc.highDecaySec = 1.20f;
    cc.thwackSec = 0.020f; cc.stickLevel = 0.32f; cc.noiseLevel = 0.035f;
    cc.resonatorLevel = 0.126f; cc.shimmerLevel = 0.22f;
    cc.directNoiseLevel = 0.010f; cc.phaseModDepth = 0.15f;

    cymbal_note_on(g_cv, cc, 0.9f, 1.0f, 1.0f, 1.0f, 0x1234u, false);
    // warm up
    keep_alive(g_cv);
    for (int i = 0; i < 4800; ++i) cymbal_process(g_cv);

    double best = 1e30;
    for (int rep = 0; rep < 5; ++rep) {
        keep_alive(g_cv);
        const double t0 = now_ns();
        float acc = 0.f;
        for (int i = 0; i < nsamples; ++i) acc += cymbal_process(g_cv);
        const double dt = now_ns() - t0;
        if (acc == 12345.678f) printf(" ");   // defeat DCE
        if (dt < best) best = dt;
    }
    return best / nsamples;   // ns per sample
}

int main() {
    D.samplerate = 48000; D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;

    printf("=== A. cymbal_process cost vs bank size (one voice, scalar host) ===\n");
    printf("%10s %12s %14s\n", "resonators", "ns/sample", "vs 0 lanes");
    const int N = 200000;
    double c0 = time_bank(4, N);        // 4 = the smallest padded bank
    const int sizes[] = { 4, 32, 64, 96, 112 };
    double cost[5];
    for (int i = 0; i < 5; ++i) {
        cost[i] = time_bank(sizes[i], N);
        printf("%10d %12.3f %14.2fx\n", sizes[i], cost[i], cost[i] / c0);
    }
    // least-squares-free two-point decomposition (4 vs 112 lanes)
    const double per_lane = (cost[4] - cost[0]) / (112.0 - 4.0);
    const double fixed    = cost[0] - 4.0 * per_lane;
    printf("\n  fixed overhead   = %.3f ns/sample/voice\n", fixed);
    printf("  per resonator    = %.4f ns/sample\n", per_lane);
    printf("  fixed cost is worth %.0f resonator lanes\n", fixed / per_lane);
    printf("  => a 32-lane gong voice is %.0f%% fixed cost\n\n",
           100.0 * fixed / (fixed + 32 * per_lane));

    printf("=== B. real processBlock, Gong (preset 14), stacked voices ===\n");
    printf("  Rsntrs (m_cym_reso_scale) default = 40%%\n");
    printf("%7s %10s %14s %12s %12s\n",
           "voices", "banks", "aggregate res", "ns/block", "x1 voice");
    double base = 0;
    for (int nv = 1; nv <= 4; ++nv) {
        s.Init(&D);
        s.LoadPreset(14);
        s.setParameter(BrachettiSynth::k_paramCymPoly, 4);
        float st[256];
        char banks[64] = {0};
        int agg = 0;
        for (int v = 0; v < nv; ++v) {
            s.GateOn(110);
            s.GateOff();
            // let it bloom a little so the next strike sees it as busy
            for (int b = 0; b < 40; ++b) { memset(st, 0, sizeof(st)); s.processBlock(st, 128); }
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active) {
                char tmp[16];
                snprintf(tmp, sizeof tmp, "%d ", (int)s.state.voices[i].cymbal.resCount);
                strncat(banks, tmp, sizeof(banks) - strlen(banks) - 1);
                agg += (int)s.state.voices[i].cymbal.resCount;
            }
        }
        double best = 1e30;
        for (int rep = 0; rep < 7; ++rep) {
            const double t0 = now_ns();
            for (int b = 0; b < 200; ++b) { memset(st, 0, sizeof(st)); s.processBlock(st, 128); }
            const double dt = (now_ns() - t0) / 200.0;
            if (dt < best) best = dt;
        }
        if (nv == 1) base = best;
        printf("%7d %10s %14d %12.0f %12.2fx\n", nv, banks, agg, best, best / base);
    }

    printf("\n=== C. same, at Rsntrs = 60%% (max) ===\n");
    printf("%7s %10s %14s %12s\n", "voices", "banks", "aggregate res", "ns/block");
    for (int nv = 1; nv <= 4; ++nv) {
        s.Init(&D);
        s.LoadPreset(13);                       // Cymbal: the biggest base bank (96)
        s.setParameter(BrachettiSynth::k_paramCymPoly, 4);
        s.setParameter(BrachettiSynth::k_paramCymReso, 60);
        float st[256];
        char banks[64] = {0};
        int agg = 0;
        for (int v = 0; v < nv; ++v) {
            s.GateOn(127);
            s.GateOff();
            for (int b = 0; b < 40; ++b) { memset(st, 0, sizeof(st)); s.processBlock(st, 128); }
        }
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (s.state.voices[i].is_active && s.state.voices[i].cymbal.active) {
                char tmp[16];
                snprintf(tmp, sizeof tmp, "%d ", (int)s.state.voices[i].cymbal.resCount);
                strncat(banks, tmp, sizeof(banks) - strlen(banks) - 1);
                agg += (int)s.state.voices[i].cymbal.resCount;
            }
        }
        double best = 1e30;
        for (int rep = 0; rep < 7; ++rep) {
            const double t0 = now_ns();
            for (int b = 0; b < 200; ++b) { memset(st, 0, sizeof(st)); s.processBlock(st, 128); }
            const double dt = (now_ns() - t0) / 200.0;
            if (dt < best) best = dt;
        }
        printf("%7d %10s %14d %12.0f\n", nv, banks, agg, best);
    }
    return 0;
}

// ring_budget_check.cpp — the CPU budget across a ring-over (ring_over.h).
//
// A ring-over must never cost two instruments: on the drumlogue that is the
// level that stops the audio interface.  Built per family unit (as
// equiv_driver.cpp: -I<unit-dir>), it plays every instrument p at the voice
// cap, changes Program to the next instrument q while they ring, and strikes q
// as many times; after every strike the two copies together must hold no more
// live voices than one instrument's budget (Poly, or one full bank on the
// kernel), and before the first strike of q the old sound must still ring.
//
// Built with -DBRACHETTI_ALL against brachetti/ (its header.c and
// family_headers.c linked), it checks Brachetti's family switch: the family
// left behind rings on until the new one is struck, and then has no live
// voice left.
//
// ring_budget_check.sh runs both.  Exit 0 = within budget everywhere.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../common/runtime.h"
static uint8_t nb() { return 0; }
static uint8_t ns(uint8_t) { return 0; }
static const sample_wrapper_t* sm(uint8_t, uint8_t) { return nullptr; }
static unit_runtime_desc_t desc() {
    unit_runtime_desc_t d = {};
    d.target = UNIT_TARGET_PLATFORM | k_unit_module_synth;
    d.api = UNIT_API_VERSION;
    d.samplerate = 48000; d.frames_per_buffer = 64; d.output_channels = 2;
    d.get_num_sample_banks = nb; d.get_num_samples_for_bank = ns; d.get_sample = sm;
    return d;
}
static int g_fail = 0;
#define FAIL(...) do { if (g_fail++ < 20) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

#ifndef BRACHETTI_ALL
#include "synth_engine.h"
#include "header.c"
#include "../../brachetti_common/ring_over.h"
static brc::RingOver<BrachettiSynth> g_rs;

int main() {
    unit_runtime_desc_t d = desc();
    if (g_rs.Init(&d) != k_unit_err_none) { fprintf(stderr, "Init failed\n"); return 2; }
    float buf[128];
    const int n = BrachettiSynth::k_NumPrograms;
    int worst = 0, cases = 0;
    for (int p = 0; p < n; ++p) {
        const int q = (p + 1) % n;
        if (q == p) continue;
        g_rs.Reset(); g_rs.setParameter(0, p); g_rs.Reset();
        const int budget = g_rs.VoiceBudget();
        for (int k = 0; k < 4; ++k) { g_rs.NoteOn((uint8_t)(48 + 5 * k), 110); g_rs.processBlock(buf, 64); }
        for (int b = 0; b < 8; ++b) g_rs.processBlock(buf, 64);
        g_rs.setParameter(0, q);
        g_rs.processBlock(buf, 64);
        if (!g_rs.Sounding()) FAIL("%s -> %s: the old sound stopped at the Program change",
                                   BrachettiSynth::getPresetName(p), BrachettiSynth::getPresetName(q));
        for (int k = 0; k < 4; ++k) {
            g_rs.NoteOn((uint8_t)(50 + 5 * k), 100);
            const int live = g_rs.LiveVoices();
            if (live > worst) worst = live;
            if (live > budget) FAIL("%s -> %s, strike %d: %d live voices, budget %d",
                                    BrachettiSynth::getPresetName(p), BrachettiSynth::getPresetName(q), k + 1, live, budget);
            for (int b = 0; b < 4; ++b) g_rs.processBlock(buf, 64);
        }
        ++cases;
    }
    printf("%s: %s — %d ring-overs, at most %d live voices across the two copies\n",
           unit_header.name, g_fail ? "OVER BUDGET" : "within budget", cases, worst);
    return g_fail ? 1 : 0;
}
#else
#include "brachetti_synth.h"
static brachetti::Synth g_s;

int main() {
    unit_runtime_desc_t d = desc();
    if (g_s.Init(&d) != k_unit_err_none) { fprintf(stderr, "Init failed\n"); return 2; }
    float buf[128];
    using brachetti::kFamilies;
    using brachetti::kNumFamilies;
    int cases = 0;
    for (int f = 0; f < kNumFamilies; ++f) {
        const int g = (f + 1) % kNumFamilies;
        g_s.LoadPreset((uint8_t)f); g_s.Reset();
        for (int k = 0; k < 4; ++k) { g_s.NoteOn((uint8_t)(48 + 5 * k), 110); g_s.Render(buf, 64); }
        for (int b = 0; b < 8; ++b) g_s.Render(buf, 64);
        g_s.LoadPreset((uint8_t)g);
        g_s.Render(buf, 64);
        if (!kFamilies[f].sounding(kFamilies[f].engine))
            FAIL("%s -> %s: the old family stopped at the switch", kFamilies[f].name, kFamilies[g].name);
        g_s.NoteOn(52, 100);
        if (kFamilies[f].live_voices(kFamilies[f].engine) != 0)
            FAIL("%s -> %s: %d voices still live in the old family after the new one's strike",
                 kFamilies[f].name, kFamilies[g].name, (int)kFamilies[f].live_voices(kFamilies[f].engine));
        for (int b = 0; b < 400; ++b) g_s.Render(buf, 64);
        ++cases;
    }
    printf("brachetti: %s — %d family switches, the family left behind quiets at the new one's first strike\n",
           g_fail ? "OVER BUDGET" : "within budget", cases);
    return g_fail ? 1 : 0;
}
#endif

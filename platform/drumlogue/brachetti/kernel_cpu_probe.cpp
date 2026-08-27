/**
 * kernel_cpu_probe.cpp — what a Timpani/Taiko note change COSTS, in CPU.
 *
 * The standing report is "Timpani: changing note leads to sporadic clicks for
 * the next 8-10 seconds" (and, earlier and harder, "changing note while playing
 * leads to silence / audio interface crash").  Pass 41 looked for a waveform
 * DISCONTINUITY at the note change and found none worth shipping a fix for.
 *
 * This probe tests the other explanation, the one pass 30 already proved once
 * on the cymbals: the clicks are not IN the signal, they are the audio driver
 * missing its deadline.  A note change is exactly the event that puts a SECOND
 * kettle in play — the old note keeps ringing on kettle 0 while the new note
 * takes kettle 1 — and each kettle steps `m_num_modes_padded` biquads per
 * sample.  On Timpani that is 280 modes, so a note change DOUBLES the dominant
 * per-sample cost of the whole unit, and it stays doubled until the older
 * kettle's silence gate fires.
 *
 * Reported here:
 *   1. per-block cost with 1 kettle vs 2 kettles vs idle
 *   2. the same figure for the CYMBAL family, whose safe/unsafe levels are the
 *      one CPU calibration this unit has field evidence for (pass 30)
 *   3. how long two kettles actually stay live after a note change
 *
 * Host timings are not the A7's, so read the RATIOS and the cymbal comparison,
 * never the absolute microseconds.
 *
 * Usage:
 *   g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
 *       kernel_cpu_probe.cpp -o /tmp/kernel_cpu_probe && /tmp/kernel_cpu_probe
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <ctime>

#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }

#include "synth_engine.h"

static const int kSR = 48000;
static const int kBlock = 128;
static float stereo[kBlock * 2];

static double now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
}

static BrachettiSynth g_s;

static void init(int preset) {
    unit_runtime_desc_t d = {};
    d.samplerate = kSR; d.output_channels = 2;
    d.get_num_sample_banks = mock_get_num_sample_banks;
    d.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    d.get_sample = mock_get_sample;
    g_s.Init(&d);
    g_s.LoadPreset((uint8_t)preset);
}

static void run_blocks(int n) {
    for (int i = 0; i < n; ++i) { memset(stereo, 0, sizeof(stereo)); g_s.processBlock(stereo, kBlock); }
}

static void strike(int note, int vel) {
    g_s.setParameter(BrachettiSynth::k_paramNote, note);
    g_s.GateOn((uint8_t)vel);
    g_s.GateOff();
}

// Best-of-N per-block cost.  `rearm` runs before every rep: without it a
// decaying voice goes silent partway through the measurement and the min over
// reps reports the IDLE cost — which is exactly how the first run of this
// probe measured the cymbal family at 0.03 µs.
static double cost_us(int blocks, int reps, void (*rearm)()) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        if (rearm) rearm();
        double t0 = now_us();
        run_blocks(blocks);
        double dt = (now_us() - t0) / blocks;
        if (dt < best) best = dt;
    }
    return best;
}

static int g_root = 52;
static void rearm_one()  { strike(g_root, 100); }
static void rearm_two()  { strike(g_root, 100); strike(g_root + 5, 100); }
static void rearm_none() {}
static void rearm_cym()  { strike(65, 100); strike(65, 100); }

int main() {
    printf("kernel CPU probe — cost per %d-frame block (host µs; read the RATIOS)\n\n", kBlock);

    struct Case { int preset; int root; const char* name; };
    const Case cases[] = { { 5, 52, "Timpani" }, { 7, 41, "Taiko" } };

    for (const auto& c : cases) {
        g_root = c.root;
        init(c.preset); run_blocks(50);
        double idle = cost_us(200, 5, rearm_none);
        // one kettle: repeating the SAME note retriggers kettle 0 in place
        init(c.preset);
        double one = cost_us(200, 5, rearm_one);
        // two kettles: a NOTE CHANGE — this is the reported action
        init(c.preset);
        double two = cost_us(200, 5, rearm_two);
        printf("%-8s  %3d modes   idle %6.2f   1 kettle %6.2f   2 kettles %6.2f   "
               "2/1 = %.2fx\n",
               c.name, g_s.m_drum_kernel.ModeCount(), idle, one, two, two / one);
    }

    // The calibrated comparison: the cymbal family's known CPU levels.  Pass 30
    // measured the build that CRASHED hardware against the last known-good one
    // and shipped a budget of 2 voices at the largest bank — the only CPU level
    // in this unit with field evidence behind it.
    {
        init(13); run_blocks(50);
        double idle = cost_us(200, 5, rearm_none);
        init(13);
        g_s.setParameter(BrachettiSynth::k_paramPartls, 7);   // largest bank
        double two = cost_us(200, 5, rearm_cym);
        printf("%-8s  %13s idle %6.2f   %30s %6.2f   <-- pass-30 shipped ceiling\n",
               "Cymbal", "", idle, "2 voices @ max bank", two);
    }

    // The honest end-to-end number: total render cost of a played passage with
    // a note change in it, plus the mode-bound trajectory that produces it.
    printf("\npassage cost (strike every 500 ms for 16 s, note changes every 2 s"
           " from t=3 s):\n");
    for (const auto& c : cases) {
        init(c.preset);
        const int notes[5] = { c.root, c.root + 3, c.root + 7, c.root - 2, c.root + 5 };
        int ni = 0, frame = 0, next_strike = 0;
        const int total = 16 * kSR;
        double t0 = now_us();
        long long mode_steps = 0, full_steps = 0;
        int peak_live = 0, peak_full = 0;
        while (frame < total) {
            const float t = (float)frame / kSR;
            if (frame >= next_strike) {
                if (t > 3.0f) ni = (ni + 1) % 5;
                strike(notes[ni], 100);
                next_strike = frame + kSR / 2;
            }
            memset(stereo, 0, sizeof(stereo));
            g_s.processBlock(stereo, kBlock);
            frame += kBlock;
            int live = g_s.m_drum_kernel.LiveModes();
            mode_steps += (long long)live * kBlock;
            // What the same passage would step with no retirement: every live
            // kettle running its full bank, which is the pre-change behaviour.
            full_steps += (long long)g_s.m_drum_kernel.LiveVoices() *
                          g_s.m_drum_kernel.ModeCount() * kBlock;
            if (live > peak_live) peak_live = live;
            if (g_s.m_drum_kernel.LiveVoices() * g_s.m_drum_kernel.ModeCount() > peak_full)
                peak_full = g_s.m_drum_kernel.LiveVoices() * g_s.m_drum_kernel.ModeCount();
        }
        double dt = now_us() - t0;
        printf("  %-8s %7.1f ms host CPU / 16 s audio   mode-steps %.3f G "
               "vs %.3f G unretired (%.0f%%)   peak live modes %d vs %d\n",
               c.name, dt / 1000.0, (double)mode_steps * 1e-9,
               (double)full_steps * 1e-9,
               100.0 * (double)mode_steps / (double)full_steps,
               peak_live, peak_full);
    }

    // How long do two kettles stay live after a note change, while the
    // sequencer keeps playing?  This is the width of the reported window.
    printf("\nhow long a note change keeps BOTH kettles live "
           "(strike every 500 ms, note changes once at t=2 s):\n");
    for (const auto& c : cases) {
        init(c.preset);
        int frame = 0;
        const int total = 20 * kSR;
        float last_two = 0.0f;
        int next_strike = 0;
        bool changed = false;
        int note = c.root;
        while (frame < total) {
            const float t = (float)frame / kSR;
            if (frame >= next_strike) {
                if (!changed && t >= 2.0f) { note = c.root + 5; changed = true; }
                strike(note, 100);
                next_strike = frame + kSR / 2;
            }
            memset(stereo, 0, sizeof(stereo));
            g_s.processBlock(stereo, kBlock);
            frame += kBlock;
            if (g_s.m_drum_kernel.LiveVoices() >= 2) last_two = t;
        }
        printf("  %-8s both kettles live until t = %.2f s "
               "(note change at 2.00 s → %.2f s of doubled cost)\n",
               c.name, last_two, last_two - 2.0f);
    }
    return 0;
}

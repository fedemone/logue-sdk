/**
 * samegate_probe.cpp — same-tick gate probe (host-only, throwaway).
 *
 * The Drumlogue sequencer fires gate_on THEN gate_off in one scheduler tick,
 * before any audio block is rendered.  render_presets.cpp holds the gate for
 * ~50 ms, so it cannot see anything that only breaks under same-tick gating.
 * This prints RMS per 25 ms window for both gate models side by side.
 *
 * Build: g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common \
 *            -DRUNTIME_COMMON_H_ samegate_probe.cpp -o /tmp/samegate && /tmp/samegate
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../common/runtime.h"
uint8_t mock_get_num_sample_banks() { return 1; }
uint8_t mock_get_num_samples_for_bank(uint8_t) { return 1; }
const sample_wrapper_t* mock_get_sample(uint8_t, uint8_t) { return nullptr; }
float ut_exciter_out = 0, ut_delay_read = 0, ut_voice_out = 0;
#include "synth_engine.h"

static unit_runtime_desc_t D;

// hold_ms < 0 → same-tick (gate_off before the first audio block)
static void run(int preset, int hold_ms, double* win, int nwin) {
    static BrachettiSynth s;
    s.Init(&D);
    s.LoadPreset((uint8_t)preset);
    for (int i = 0; i < NUM_VOICES; ++i)
        s.state.voices[i].exciter.noise_gen.seed = 2463534242UL;

    s.GateOn(100);
    if (hold_ms < 0) s.GateOff();

    const int W = 48000 / 40;          // 25 ms
    float st[256];
    bool released = (hold_ms < 0);
    for (int w = 0; w < nwin; ++w) {
        double acc = 0;
        for (int f = 0; f < W; f += 128) {
            memset(st, 0, sizeof(st));
            s.processBlock(st, 128);
            for (int i = 0; i < 128; ++i) acc += (double)st[i * 2] * st[i * 2];
            if (!released && (w * W + f) >= hold_ms * 48) { s.GateOff(); released = true; }
        }
        win[w] = sqrt(acc / W);
    }
}

int main() {
    D.samplerate = 48000; D.output_channels = 2;
    D.get_num_sample_banks = mock_get_num_sample_banks;
    D.get_num_samples_for_bank = mock_get_num_samples_for_bank;
    D.get_sample = mock_get_sample;

    struct P { int idx; const char* name; } ps[] = {
        {21, "Clap"}, {22, "Shaker"}, {26, "HHat-C"},          // ENGINE_NOISE
        {3,  "AcSnare"}, {0, "Kick2"}, {25, "GtrStr"},          // controls
        {13, "Cymbal"}, {1, "Marimba"}, {5, "Timpani"},
    };
    const int N = 8;
    double a[N], b[N];
    printf("%-9s %-10s %s\n", "preset", "gate", "RMS per 25 ms window");
    for (auto& p : ps) {
        run(p.idx, -1, a, N);
        run(p.idx, 50, b, N);
        printf("%-9s %-10s", p.name, "same-tick");
        for (int i = 0; i < N; ++i) printf(" %.4f", a[i]);
        printf("\n%-9s %-10s", "", "held 50ms");
        for (int i = 0; i < N; ++i) printf(" %.4f", b[i]);
        double sa = 0, sb = 0;
        for (int i = 0; i < N; ++i) { sa += a[i]; sb += b[i]; }
        printf("   [same/held = %.2f]\n", sb > 1e-9 ? sa / sb : 0.0);
    }
    return 0;
}

// soak.cc — Braquetti playing a pattern for minutes of audio, for soak.sh.
// Every 10 s of audio: the mean and slowest render time per block (the
// host's clock: a trend, not the drumlogue's), the loudest sample, how many
// samples were a NaN or an infinity, how many were subnormal, the most
// voices and load.  A fault that builds up over time shows here.
//
//   soak <famA> <codesA> <famB> <codesB> <famC> <codesC> <gap> <seconds>
//
// A <codes> is 16 comma-separated step codes (0 = off, n = the family's
// instrument n-1), a family -1 an empty lane; <gap>: frames between gates
// (6000: 16ths at 120 BPM).  Built without -ffast-math, so its checks
// stand; the unit with the drumlogue's flags.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include "unit.h"
int braquetti_test_live_voices(); int braquetti_test_load();
static int32_t s_panel[24];
static void knob(uint8_t id, int32_t v) { timespec ts = {0, 6000000}; nanosleep(&ts, nullptr); s_panel[id] = v; unit_set_param_value(id, v); }
static void turn(uint8_t id, int c) { while (c) { int k = c > 16 ? 16 : c < -16 ? -16 : c; knob(id, s_panel[id] + k); c -= k; } }
static uint8_t nb() { return 0; } static uint8_t ns(uint8_t) { return 0; } static const sample_wrapper_t* sm(uint8_t, uint8_t) { return nullptr; }
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char** argv) {
    if (argc < 9) return 1;
    unit_runtime_desc_t d = {}; d.target = unit_header.target; d.api = unit_header.api; d.samplerate = 48000;
    d.frames_per_buffer = 64; d.output_channels = 2; d.get_num_sample_banks = nb; d.get_num_samples_for_bank = ns; d.get_sample = sm;
    if (unit_init(&d)) return 2;
    for (int i = 0; i < 24; ++i) s_panel[i] = unit_header.params[i].init;
    int family[3] = {0, 3, 7}, want[3];                 // catalog.h's defaults; one family per lane
    for (int l = 0; l < 3; ++l) want[l] = atoi(argv[1 + 2 * l]);
    for (int pass = 0; pass < 4; ++pass)
        for (int l = 0; l < 3; ++l) {
            if (want[l] < 0 || family[l] == want[l]) continue;
            bool held = false;
            for (int o = 0; o < 3; ++o) if (o != l && family[o] == want[l]) held = true;
            if (held) continue;
            knob(16, l); turn(17, want[l] - family[l]); family[l] = want[l];
        }
    for (int l = 0; l < 3; ++l) {
        if (want[l] < 0) continue;
        if (family[l] != want[l]) { printf("lane %d not placed\n", l); return 3; }
        knob(16, l);
        const char* p = argv[2 + 2 * l];
        for (int s = 0; s < 16 && p; ++s) { int c = atoi(p); if (c) turn(uint8_t(s), c); p = strchr(p, ','); if (p) ++p; }
        printf("lane %c:", 'A' + l);
        for (int s = 0; s < 16; ++s) printf(" %s", unit_get_param_str_value(uint8_t(s), 0));
        printf("\n");
    }
    const int gap = atoi(argv[7]), seconds = atoi(argv[8]);
    float buf[128];
    int until = 0, step = 0;
    long long blocks = 0;
    double t_sum = 0, t_max = 0; float peak = 0; long bad = 0, sub = 0; int most_live = 0, most_load = 0;
    unit_reset();
    const long long total = (long long)seconds * 48000 / 64;
    for (long long b = 0; b < total; ++b) {
        if (until <= 0) { unit_gate_on(step++ % 16 == 0 ? 127 : 64); until += gap; }
        const double t0 = now();
        unit_render(nullptr, buf, 64);
        const double dt = now() - t0;
        t_sum += dt; if (dt > t_max) t_max = dt;
        until -= 64;
        if (until <= gap / 2 && until + 64 > gap / 2) unit_gate_off();
        for (float x : buf) {
            if (!std::isfinite(x)) ++bad;
            else { if (std::fabs(x) > peak) peak = std::fabs(x); if (x != 0.0f && std::fabs(x) < 1.17549435e-38f) ++sub; }
        }
        const int lv = braquetti_test_live_voices(), ld = braquetti_test_load();
        if (lv > most_live) most_live = lv; if (ld > most_load) most_load = ld;
        if (++blocks % (48000 / 64 * 10) == 0) {
            printf("%4llds: render %.1f us/block mean, %.1f max; peak %.3f; nan/inf %ld; subnormal out %ld; live<=%d load<=%d\n",
                   blocks * 64 / 48000, t_sum / (48000 / 64 * 10) * 1e6, t_max * 1e6, peak, bad, sub, most_live, most_load);
            t_sum = t_max = 0; peak = 0; bad = sub = 0; most_live = most_load = 0;
        }
    }
    return 0;
}

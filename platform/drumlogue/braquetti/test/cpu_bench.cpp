// cpu_bench.cpp — Braquetti's render cost through its drumlogue callbacks, for
// cpu_cycles.sh (the method of tools/brachetti_equiv/unit_cycles.sh, with
// gates instead of notes).
//
//   cpu_bench <render|setup> <famA> <progA> <famB> <progB> <famC> <progC> <gap> <frames> [settle]
//   cpu_bench names          every family's instruments: "<family> <program> <name>" per line
//
// A <prog> is one instrument on all 16 steps, or 16 comma-separated step
// codes (0 = off, n = the family's instrument n-1): a real pattern.
// Sets each lane to its family and instrument on all 16 steps (knob turns,
// as the panel sends them), plays 4 gates <gap> frames apart to settle,
// then — in render mode only — <frames> more frames with a gate every <gap>
// frames: the part measured (<settle> gates instead of 4: 0 measures from
// step 1, where a pattern may be heaviest) (setup mode stops before it, so the two runs'
// difference is that playing).  A family of -1 leaves the lane empty; two
// lanes cannot play one family (exit code 3 if asked to).  A step the unit
// refuses ("!!!", limits.h) plays nothing, as on the drumlogue.
//
// <gap> 0: one strike instead — after a reset, a single gate at the start of
// the measured part, then <frames> frames: the cost of one voice of each
// lane's instrument as it sounds and decays.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include "unit.h"
#include "../catalog.h"   // the names (this file's own copy of the engines stays idle)

static uint8_t nb() { return 0; }
static uint8_t ns(uint8_t) { return 0; }
static const sample_wrapper_t* sm(uint8_t, uint8_t) { return nullptr; }

enum { P_STEP1 = 0, P_LANE = 16, P_FAMILY = 17 };
static int32_t s_panel[24];

// Knobs 6 ms apart: three in a row closer than 5 ms are a program load.
static void knob(uint8_t id, int32_t v) {
    timespec ts = {0, 6 * 1000 * 1000};
    nanosleep(&ts, nullptr);
    s_panel[id] = v;
    unit_set_param_value(id, v);
}
static void turn(uint8_t id, int clicks) {
    while (clicks) {
        const int c = clicks > 16 ? 16 : clicks < -16 ? -16 : clicks;
        knob(id, s_panel[id] + c);
        clicks -= c;
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "names") == 0) {
        for (int f = 0; f < brq::BrachettiCatalog::kFamilies; ++f)
            for (int p = 0; p < brq::BrachettiCatalog::programs(f); ++p)
                std::printf("%s %d %s\n", brq::BrachettiCatalog::family_name(f), p,
                            brq::BrachettiCatalog::program_name(f, p));
        return 0;
    }
    if (argc < 10) return 1;
    const bool rend = std::strcmp(argv[1], "render") == 0;
    const int gap = std::atoi(argv[8]), frames = std::atoi(argv[9]);
    const int settle = (argc > 10) ? std::atoi(argv[10]) : 4;
    unit_runtime_desc_t d = {}; d.target = unit_header.target; d.api = unit_header.api; d.samplerate = 48000;
    d.frames_per_buffer = 64; d.output_channels = 2; d.get_num_sample_banks = nb;
    d.get_num_samples_for_bank = ns; d.get_sample = sm;
    if (unit_init(&d)) return 2;
    for (int i = 0; i < 24; ++i) s_panel[i] = unit_header.params[i].init;

    // The families first.  A lane cannot take a family another lane plays
    // (seq_core.h), so a lane whose family is still held waits for the
    // others to move; one held by a lane that stays empty, or a swap, frees
    // it through a family nobody wants.
    int family[3] = {0, 3, 7}, want[3];                // catalog.h's defaults
    for (int lane = 0; lane < 3; ++lane) want[lane] = std::atoi(argv[2 + 2 * lane]);
    auto held = [&](int lane, int f) {
        for (int l = 0; l < 3; ++l) if (l != lane && family[l] == f) return true;
        return false;
    };
    auto move = [&](int lane, int f) {
        knob(P_LANE, lane);
        turn(P_FAMILY, f - family[lane]);
        family[lane] = f;
    };
    for (int pass = 0; pass < 6; ++pass) {
        bool moved = false, done = true;
        for (int lane = 0; lane < 3; ++lane) {
            if (want[lane] < 0 || family[lane] == want[lane]) continue;
            done = false;
            if (!held(lane, want[lane])) { move(lane, want[lane]); moved = true; }
        }
        if (done) break;
        if (moved) continue;
        for (int lane = 0; lane < 3; ++lane) {          // stuck: step a holder aside
            if (want[lane] < 0 || family[lane] == want[lane]) continue;
            for (int l = 0; l < 3; ++l) {
                if (l == lane || family[l] != want[lane]) continue;
                int spare = 0;
                while (held(l, spare) || spare == want[0] || spare == want[1] || spare == want[2]) ++spare;
                move(l, spare);
            }
            break;
        }
    }
    for (int lane = 0; lane < 3; ++lane) {
        const int prog = std::atoi(argv[3 + 2 * lane]);
        if (want[lane] < 0) continue;
        if (family[lane] != want[lane]) return 3;      // cannot happen: three lanes, twelve families
        knob(P_LANE, lane);
        const char* spec = argv[3 + 2 * lane];
        if (std::strchr(spec, ',')) {
            for (int s = 0; s < 16 && *spec; ++s) {
                const int code = std::atoi(spec);
                if (code) turn(uint8_t(P_STEP1 + s), code);
                spec = std::strchr(spec, ',');
                if (!spec) break;
                ++spec;
            }
        } else {
            for (int s = 0; s < 16; ++s) turn(uint8_t(P_STEP1 + s), prog + 1);
        }
    }

    float buf[128];
    int until_gate = 0, step = 0;
    auto play = [&](int n) {
        for (int f = 0; f < n; f += 64) {
            if (until_gate <= 0) {
                unit_gate_on(step++ % 16 == 0 ? 127 : 64);
                until_gate += gap;
            }
            unit_render(nullptr, buf, 64);
            until_gate -= 64;
            if (until_gate <= gap / 2 && until_gate + 64 > gap / 2) unit_gate_off();
        }
    };
    if (gap <= 0) {
        unit_reset();
        unit_render(nullptr, buf, 64);   // the reset itself, outside the measure
        if (rend) {
            unit_gate_on(64);
            for (int f = 0; f < frames; f += 64) unit_render(nullptr, buf, 64);
        }
        return 0;
    }
    unit_reset();
    play(settle * gap);
    if (rend) play(frames);
    return 0;
}

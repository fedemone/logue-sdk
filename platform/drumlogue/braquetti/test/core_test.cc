// Braquetti sequencer core: host test.
//
//   ./run_tests.sh          host, address + undefined sanitizers (all three tests)
//   ARM=1 ./run_tests.sh    ARM build (the SDK's -march/-mfpu), qemu-arm
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "../seq_core.h"

using namespace brq;

// A small catalog of its own, so this test needs no engines: Brachetti's
// first eight families as the family units first shipped them.  The real
// catalog (Brachetti's family table) is checked by catalog_test.cc.
static const char* const kKickNames[]   = {"Kick2", "808Sub", "Kick"};
static const char* const kTomNames[]    = {"Djambe", "Ac Tom", "DeepBs", "Conga", "Handpn", "Bongo", "RackTom"};
static const char* const kKernelNames[] = {"Timpni", "Taiko"};
static const char* const kSnareNames[]  = {"AcSnre", "MrchSnr", "BrshSnr", "RimShot"};
static const char* const kBarNames[]    = {"Marmba", "TblrBel", "Vibrph", "Wodblk", "Kalimba",
                                           "StelPan", "Claves", "GlsBwl", "SltDrm", "GlsBotl"};
static const char* const kPlateNames[]  = {"Cowbel", "Trngle", "BelTre", "Tick"};
static const char* const kCymbalNames[] = {"Cymbal", "Gong", "HHat-O", "Ride", "RidBel", "Splash"};
static const char* const kNoiseNames[]  = {"Clap", "Shaker", "HHat-C"};
struct TestFamily { const char* name; int count; const char* const* programs; };
static const TestFamily kTestFamilies[] = {
    {"Kick", 3, kKickNames},     {"Tom", 7, kTomNames},     {"Kernel", 2, kKernelNames},
    {"Snare", 4, kSnareNames},   {"Bar", 10, kBarNames},    {"Plate", 4, kPlateNames},
    {"Cymbal", 6, kCymbalNames}, {"Noise", 3, kNoiseNames},
};
struct TestCatalog {
    static constexpr int kFamilies = 8;
    static constexpr int kMaxPrograms = 10;
    static const char* family_name(int f) { return kTestFamilies[f].name; }
    static int programs(int f) { return kTestFamilies[f].count; }
    static const char* program_name(int f, int p) { return kTestFamilies[f].programs[p]; }
    static int default_family(int lane) { return lane == 0 ? 0 : lane == 1 ? 3 : 6; }
};
using Seq = Sequencer<TestCatalog>;
constexpr uint8_t P_STEP5 = P_STEP1 + 4;

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++s_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Seq s_seq;          // a global, as in the unit: zero-initialised, then init()
static uint64_t s_t = 1000000;

// One knob turn as the panel makes it: its own number +/- clicks, 20 ms apart.
static int32_t s_panel[kNumParams];
static void turn(uint8_t id, int clicks) {
    s_t += 20000;
    s_panel[id] += clicks;
    s_seq.set_param(id, s_panel[id], s_t);
}
static void set_abs(uint8_t id, int32_t v) {
    s_t += 20000;
    s_panel[id] = v;
    s_seq.set_param(id, v, s_t);
}
static std::string text(uint8_t id) { return s_seq.param_text(id, s_panel[id]); }

// A program load: preset, then all 24 knobs at once, 100 us apart.
static void program_load(const int32_t* stored) {
    s_t += 20000;
    s_seq.preset_loaded(s_t);
    for (uint8_t i = 0; i < kNumParams; ++i) {
        s_t += 100;
        s_panel[i] = stored[i];
        s_seq.set_param(i, stored[i], s_t);
    }
}

int main() {
    s_seq.init();
    for (int i = 0; i < kNumParams; ++i) s_panel[i] = is_relative(uint8_t(i)) ? kRelInit : 0;

    // ── Defaults ────────────────────────────────────────────────────────────
    CHECK(text(P_LANE) == "Lane A" && text(P_FAMILY) == "Kick" && text(P_LEVEL) == "80%",
          "lane A: %s %s %s", text(P_LANE).c_str(), text(P_FAMILY).c_str(), text(P_LEVEL).c_str());
    CHECK(text(P_STEP1) == "---", "empty step: %s", text(P_STEP1).c_str());
    CHECK(s_seq.family(1) == 3 && s_seq.family(2) == 6, "lanes B, C: snare, cymbal");

    // ── Steps: one instrument per click, clamped at the family's ends ───────
    turn(P_STEP1, +1);
    CHECK(text(P_STEP1) == "Kick2", "one click: %s", text(P_STEP1).c_str());
    turn(P_STEP1, +1);
    CHECK(text(P_STEP1) == "808Sub", "two: %s", text(P_STEP1).c_str());
    turn(P_STEP1, +5);
    CHECK(text(P_STEP1) == "Kick", "clamped at the last: %s", text(P_STEP1).c_str());
    turn(P_STEP1, -3);
    CHECK(text(P_STEP1) == "---", "back to off: %s", text(P_STEP1).c_str());
    turn(P_STEP1, -2);
    CHECK(text(P_STEP1) == "---", "clamped at off: %s", text(P_STEP1).c_str());
    turn(P_STEP1, +1);
    turn(P_STEP5, +1);
    turn(P_STEP5 + 4, +1);   // step 9
    turn(P_STEP5 + 8, +1);   // step 13
    CHECK(text(P_STEP5) == "Kick2" && s_seq.step(0, 8) == 1 && s_seq.step(0, 12) == 1, "four on the floor");

    // A panel that took the unit's answer (asked, then counts from it).
    s_t += 20000;
    const int32_t answer = s_seq.get_param(P_STEP1);
    CHECK(answer == s_panel[P_STEP1] - kRelSwing || answer == s_panel[P_STEP1] + kRelSwing,
          "answer sits %d from the panel's number", kRelSwing);
    s_panel[P_STEP1] = answer;
    turn(P_STEP1, +1);
    CHECK(text(P_STEP1) == "808Sub", "counted from the answer: %s", text(P_STEP1).c_str());
    // …and one that was asked but kept its own number.
    s_seq.get_param(P_STEP1);
    turn(P_STEP1, -1);
    CHECK(text(P_STEP1) == "Kick2", "counted from its own number: %s", text(P_STEP1).c_str());
    // A jump is a resync, not an edit.
    set_abs(P_STEP1, s_panel[P_STEP1] + 300);
    CHECK(text(P_STEP1) == "Kick2", "resync: %s", text(P_STEP1).c_str());

    // ── Lane B, and a family change that keeps positions ────────────────────
    set_abs(P_LANE, 1);
    CHECK(text(P_LANE) == "Lane B" && text(P_FAMILY) == "Snare" && text(P_STEP1) == "---",
          "lane B: %s %s %s", text(P_LANE).c_str(), text(P_FAMILY).c_str(), text(P_STEP1).c_str());
    turn(P_STEP5, +4);                 // RimShot (program 4 of 4)
    turn(P_STEP5 + 8, +1);             // step 13: AcSnre
    CHECK(text(P_STEP5) == "RimShot", "lane B step 5: %s", text(P_STEP5).c_str());
    turn(P_FAMILY, -1);                // Snare -> Kernel (2 programs)
    CHECK(text(P_FAMILY) == "Kernel", "family: %s", text(P_FAMILY).c_str());
    CHECK(text(P_STEP5) == "Taiko", "on, moved to the family's last: %s", text(P_STEP5).c_str());
    CHECK(s_seq.step(1, 12) == 1 && s_seq.step(1, 0) == 0, "step 13 keeps program 1, step 1 stays off");
    turn(P_FAMILY, +2);                // Kernel -> Bar (10 programs): programs stay as they are
    CHECK(text(P_FAMILY) == "Bar" && text(P_STEP5) == "TblrBel", "Bar keeps program 2: %s",
          text(P_STEP5).c_str());
    turn(P_FAMILY, +16);               // more than 16 clicks at once would be a resync
    CHECK(text(P_FAMILY) == "Noise", "family clamps at the last: %s", text(P_FAMILY).c_str());
    CHECK(text(P_STEP5) == "Shaker", "Noise keeps program 2: %s", text(P_STEP5).c_str());
    turn(P_FAMILY, -4);                // Noise -> Snare
    CHECK(text(P_FAMILY) == "Snare" && text(P_STEP5) == "MrchSnr", "back to Snare: %s %s",
          text(P_FAMILY).c_str(), text(P_STEP5).c_str());
    CHECK(s_seq.family(0) == 0, "lane A untouched");

    // Level, per lane.
    turn(P_LEVEL, +5);
    CHECK(text(P_LEVEL) == "85%", "level: %s", text(P_LEVEL).c_str());
    turn(P_LEVEL, +16);
    turn(P_LEVEL, +16);
    CHECK(text(P_LEVEL) == "100%", "level clamps: %s", text(P_LEVEL).c_str());
    set_abs(P_LANE, 0);
    CHECK(text(P_LEVEL) == "80%", "lane A's level: %s", text(P_LEVEL).c_str());

    // ── A program load edits nothing ────────────────────────────────────────
    const uint16_t before_b5 = uint16_t(s_seq.step(1, 4));
    int32_t stored[kNumParams];
    for (int i = 0; i < kNumParams; ++i) stored[i] = s_panel[i] + (is_relative(uint8_t(i)) ? 7 : 0);
    stored[P_LANE] = 0;
    program_load(stored);
    CHECK(s_seq.step(1, 4) == before_b5 && s_seq.step(0, 0) == 1 && s_seq.family(1) == 3,
          "the load edited nothing");
    // A "turn" that turns out to be the start of a burst is undone, family
    // change included.
    s_t += 100000;   // past the 50 ms after a preset load that count as restore
    s_seq.set_param(P_FAMILY, s_panel[P_FAMILY] + 1, s_t);    // a family change on lane A...
    CHECK(s_seq.family(0) == 1, "...which applied");
    s_seq.set_param(P_STEP1, 999, s_t + 100);
    s_seq.set_param(P_LEVEL, 999, s_t + 200);
    CHECK(s_seq.family(0) == 0 && s_seq.step(0, 0) == 1, "undone when the burst showed");
    s_panel[P_FAMILY] += 1; s_panel[P_STEP1] = 999; s_panel[P_LEVEL] = 999;

    // ── Clock ───────────────────────────────────────────────────────────────
    // Lane A: Kick2 on 1, 5, 9, 13.  Lane B: MrchSnr on 5, AcSnre on 13.
    Hit hits[kLanes];
    int n = s_seq.gate_on(64, hits);   // first gate after init: step 1
    CHECK(s_seq.position() == 0 && n == 1 && hits[0].lane == 0 && hits[0].family == 0 &&
          hits[0].program == 0 && hits[0].velocity == 64, "step 1: Kick2");
    for (int i = 0; i < 3; ++i) CHECK(s_seq.gate_on(64, hits) == 0, "steps 2-4 empty");
    n = s_seq.gate_on(64, hits);
    CHECK(s_seq.position() == 4 && n == 2 && hits[1].lane == 1 && hits[1].family == 3 && hits[1].program == 1,
          "step 5: kick and MrchSnr");
    for (int i = 0; i < 11; ++i) s_seq.gate_on(64, hits);
    CHECK(s_seq.position() == 15, "step 16");
    s_seq.gate_on(64, hits);
    CHECK(s_seq.position() == 0, "wraps to step 1");
    for (int i = 0; i < 6; ++i) s_seq.gate_on(64, hits);
    s_seq.gate_on(127, hits);
    CHECK(s_seq.position() == 0, "an accent is step 1 (realigns a drifted count)");
    s_seq.gate_on(64, hits);
    s_seq.reset();
    s_seq.gate_on(64, hits);
    CHECK(s_seq.position() == 0, "the first gate after a reset is step 1");

    // ── Overview, the queue ─────────────────────────────────────────────────
    const Bitmap* ov = reinterpret_cast<const Bitmap*>(s_seq.param_bitmap(P_OVERVIEW));
    CHECK(ov && ov->get(0, 1) && ov->get(0, 2) && ov->get(4, 1) && !ov->get(1, 1), "lane A row");
    CHECK(ov->get(4, 6) && ov->get(12, 6) && !ov->get(0, 6), "lane B row");
    CHECK(ov->get(0, 4) && !ov->get(0, 9), "dots under the selected lane (A)");
    CHECK(s_seq.param_bitmap(P_STEP1) == nullptr, "only the overview is a bitmap");


    static HitQueue q;   // static, as in the unit: zero-initialised
    int pushed = 0;
    while (q.push({0, 0, 0, 64})) ++pushed;
    Hit h;
    int popped = 0;
    while (q.pop(h)) ++popped;
    CHECK(pushed == 16 && popped == 16, "queue holds 16: %d %d", pushed, popped);

    // ── Gates on one thread, knobs on another ───────────────────────────────
    std::atomic<bool> run{true};
    std::atomic<int> bad{0};
    std::thread gates([&] {
        Hit hh[kLanes];
        while (run.load()) {
            const int k = s_seq.gate_on(64, hh);
            for (int i = 0; i < k; ++i)
                if (hh[i].program >= TestCatalog::programs(hh[i].family)) ++bad;
        }
    });
    for (int i = 0; i < 2000; ++i) {
        turn(uint8_t(P_STEP1 + i % 16), (i % 3) - 1);
        if (i % 50 == 0) turn(P_FAMILY, (i % 100) ? 1 : -1);
    }
    run = false;
    gates.join();
    CHECK(bad.load() == 0, "no hit out of its family while editing (%d)", bad.load());

    if (s_fail) { std::printf("%d check(s) FAILED\n", s_fail); return 1; }
    std::printf("all checks passed\n");
    return 0;
}

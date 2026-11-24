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
#include "../limits.h"

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

// A Policy for the test: refuses Bar's 3rd instrument (Vibrph) on any step,
// and anything on step 9 of lane C.
struct TestPolicy {
    template <class S>
    static bool allows(const S&, int lane, int step, int family, int code) {
        return !(family == 4 && code == 3) && !(lane == 2 && step == 8);
    }
};
using PolicySeq = Sequencer<TestCatalog, TestPolicy>;
using LimitSeq = Sequencer<TestCatalog, CpuLimits>;
constexpr uint8_t P_STEP5 = P_STEP1 + 4;

// limits.h's three rules, over the whole pattern.
static bool rules_hold(const LimitSeq& s) {
    for (int st = 0; st < kSteps; ++st) {
        int sum = 0, hits = 0, heavies = 0;
        for (int l = 0; l < kLanes; ++l) {
            const int c = s.step(l, st);
            if (!c) continue;
            ++hits;
            sum += cost(s.family(l), c);
            if (heavy(s.family(l), c)) ++heavies;
        }
        if (sum > kCpuBudget || (heavies && hits > 1)) return false;
    }
    for (int l = 0; l < kLanes; ++l) {
        int run = 0, longest = 0;
        for (int k = 0; k < 2 * kSteps; ++k) {   // twice round: runs across 16 -> 1
            run = heavy(s.family(l), s.step(l, k % kSteps)) ? run + 1 : 0;
            if (run > longest) longest = run;
        }
        if (longest > kHeavyRun) return false;
    }
    return true;
}

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
    // Lane A Kick2 on 1, 5, 9, 13; lane B on 5 and 13; lane A selected.
    // Four knobs, four steps each: a cell of 3x3 pixels per step, lanes at
    // y 0, 5, 10.
    const Bitmap* ov1 = reinterpret_cast<const Bitmap*>(s_seq.param_bitmap(P_OVERVIEW));       // steps 1-4
    const Bitmap* ov2 = reinterpret_cast<const Bitmap*>(s_seq.param_bitmap(P_OVERVIEW + 1));   // steps 5-8
    CHECK(ov1 && ov2 && ov1 != ov2, "four overview bitmaps");
    CHECK(ov1->get(0, 0) && ov1->get(2, 2) && !ov1->get(4, 0) && ov1->get(4, 1) && ov1->get(6, 1),
          "lane A (selected): step 1 filled, step 2 a dash");
    CHECK(!ov1->get(0, 5) && ov1->get(1, 6) && !ov1->get(0, 6), "lane B step 1: a dot");
    CHECK(ov2->get(0, 0) && ov2->get(1, 6) && ov2->get(2, 7), "step 5: lanes A and B filled");
    CHECK(!ov1->get(0, 3) && !ov1->get(0, 4), "nothing between the lanes");
    CHECK(s_seq.param_bitmap(P_OVERVIEW + 3) != nullptr && s_seq.param_bitmap(P_OVERVIEW + 4) == nullptr &&
          s_seq.param_bitmap(P_STEP1) == nullptr, "only the four overview knobs are bitmaps");


    static HitQueue q;   // static, as in the unit: zero-initialised
    int pushed = 0;
    while (q.push({0, 0, 0, 64})) ++pushed;
    Hit h;
    int popped = 0;
    while (q.pop(h)) ++popped;
    CHECK(pushed == 16 && popped == 16, "queue holds 16: %d %d", pushed, popped);

    // ── One family per lane ─────────────────────────────────────────────────
    // Lane A Kick, B Snare, C Cymbal: browsing lane B onto Cymbal shows "!!!"
    // and keeps Snare playing; one click on (Plate is free) switches.
    set_abs(P_LANE, 1);
    CHECK(text(P_FAMILY) == "Snare", "lane B: %s", text(P_FAMILY).c_str());
    turn(P_FAMILY, +3);   // Snare -> Cymbal, lane C's
    CHECK(text(P_FAMILY) == "!!!" && s_seq.family(1) == 3, "a family in use shows !!!, lane B keeps Snare: %s %d",
          text(P_FAMILY).c_str(), s_seq.family(1));
    turn(P_FAMILY, -1);   // -> Plate, free
    CHECK(text(P_FAMILY) == "Plate" && s_seq.family(1) == 5, "a free family is taken: %s", text(P_FAMILY).c_str());
    turn(P_FAMILY, -5);   // -> Kick, lane A's
    CHECK(text(P_FAMILY) == "!!!" && s_seq.family(1) == 5, "Kick in use: %s", text(P_FAMILY).c_str());
    set_abs(P_LANE, 0);
    set_abs(P_LANE, 1);   // selecting the lane shows the family it plays again
    CHECK(text(P_FAMILY) == "Plate", "back on its own family: %s", text(P_FAMILY).c_str());
    turn(P_FAMILY, -2);   // -> Snare again
    CHECK(s_seq.family(0) == 0 && s_seq.family(1) == 3 && s_seq.family(2) == 6, "families as before");
    int distinct = 1;
    for (int i = 0; i < 200; ++i) {   // random browsing never puts two lanes on one family
        set_abs(P_LANE, i % 3);
        turn(P_FAMILY, (i * 7) % 11 - 5);
        if (s_seq.family(0) == s_seq.family(1) || s_seq.family(1) == s_seq.family(2) ||
            s_seq.family(0) == s_seq.family(2)) distinct = 0;
    }
    CHECK(distinct, "no two lanes ever share a family");

    // ── A Policy refusing a step ────────────────────────────────────────────
    {
        static PolicySeq ps;
        ps.init();
        int32_t panel[kNumParams];
        for (int i = 0; i < kNumParams; ++i) panel[i] = is_relative(uint8_t(i)) ? kRelInit : 0;
        auto pturn = [&](uint8_t id, int clicks) { s_t += 20000; panel[id] += clicks; ps.set_param(id, panel[id], s_t); };
        auto pset = [&](uint8_t id, int32_t v) { s_t += 20000; panel[id] = v; ps.set_param(id, v, s_t); };
        auto ptext = [&](uint8_t id) { return std::string(ps.param_text(id, panel[id])); };
        pset(P_LANE, 1);
        pturn(P_FAMILY, +1);   // lane B: Snare -> Bar
        CHECK(ptext(P_FAMILY) == "Bar", "policy seq lane B: %s", ptext(P_FAMILY).c_str());
        pturn(P_STEP1, +2);
        CHECK(ptext(P_STEP1) == "TblrBel" && ps.step(1, 0) == 2, "allowed: %s", ptext(P_STEP1).c_str());
        pturn(P_STEP1, +1);    // Vibrph: refused
        CHECK(ptext(P_STEP1) == "!!!" && ps.step(1, 0) == 0 && ps.step_cursor(1, 0) == 3 && ps.blocked(1, 0),
              "refused: shows !!!, plays nothing: %s", ptext(P_STEP1).c_str());
        Hit hits[kLanes];
        CHECK(ps.gate_on(64, hits) == 0, "a refused step strikes nothing");
        pturn(P_STEP1, +1);    // Wodblk: allowed again
        CHECK(ptext(P_STEP1) == "Wodblk" && ps.step(1, 0) == 4, "next click checked again: %s", ptext(P_STEP1).c_str());
        pturn(P_STEP1, -1);    // back onto Vibrph
        CHECK(ptext(P_STEP1) == "!!!", "refused again: %s", ptext(P_STEP1).c_str());
        pturn(P_FAMILY, +1);   // Bar -> Plate: the step is checked again, Plate's 3rd (BelTre) is allowed
        CHECK(ptext(P_STEP1) == "BelTre" && ps.step(1, 0) == 3, "a family change unblocks: %s", ptext(P_STEP1).c_str());
        pset(P_LANE, 2);
        pturn(P_STEP1 + 8, +1);   // lane C step 9: refused whatever it is
        CHECK(ptext(P_STEP1 + 8) == "!!!" && ps.step(2, 8) == 0, "lane C step 9 refused: %s", ptext(P_STEP1 + 8).c_str());
        pturn(P_STEP1 + 8, -1);   // back to off
        CHECK(ptext(P_STEP1 + 8) == "---" && !ps.blocked(2, 8), "off is never refused: %s", ptext(P_STEP1 + 8).c_str());
    }

    // ── Braquetti's limits (limits.h) ───────────────────────────────────────
    // This catalog's eight families are Brachetti's first eight, with the
    // same indices and the same first instruments, so limits.h's costs apply.
    {
        static LimitSeq ls;
        ls.init();
        int32_t panel[kNumParams];
        for (int i = 0; i < kNumParams; ++i) panel[i] = is_relative(uint8_t(i)) ? kRelInit : 0;
        auto lturn = [&](uint8_t id, int clicks) { s_t += 20000; panel[id] += clicks; ls.set_param(id, panel[id], s_t); };
        auto lset = [&](uint8_t id, int32_t v) { s_t += 20000; panel[id] = v; ls.set_param(id, v, s_t); };
        auto ltext = [&](uint8_t id) { return std::string(ls.param_text(id, panel[id])); };
        auto step_id = [](int s) { return uint8_t(P_STEP1 + s - 1); };   // steps 1-16
        CHECK(heavy(2, 1) && !heavy(2, 2) && !heavy(6, 2), "heavy: the Timpani, not the Taiko nor the Gong");

        // Lane A on Kernel: a Timpani on step 1 plays; nothing joins it there.
        lset(P_LANE, 0);
        lturn(P_FAMILY, +2);                  // Kick -> Kernel
        lturn(step_id(1), +1);
        CHECK(ltext(step_id(1)) == "Timpni" && ls.step(0, 0) == 1, "a Timpani alone plays: %s", ltext(step_id(1)).c_str());
        lset(P_LANE, 1);                      // lane B, Snare
        lturn(step_id(1), +1);
        CHECK(ltext(step_id(1)) == "!!!" && ls.step(1, 0) == 0, "a snare on the Timpani's step: %s", ltext(step_id(1)).c_str());
        lturn(step_id(2), +1);                // AcSnre on step 2
        CHECK(ltext(step_id(2)) == "AcSnre", "step 2: %s", ltext(step_id(2)).c_str());
        lset(P_LANE, 0);
        lturn(step_id(2), +1);                // a Timpani where the snare plays
        CHECK(ltext(step_id(2)) == "!!!" && ls.step(0, 1) == 0, "a Timpani on the snare's step: %s", ltext(step_id(2)).c_str());
        lturn(step_id(2), +1);                // on to the Taiko: not heavy, joins the snare
        CHECK(ltext(step_id(2)) == "Taiko" && ls.step(0, 1) == 2, "a Taiko with the snare: %s", ltext(step_id(2)).c_str());
        Hit hits[kLanes];
        CHECK(ls.gate_on(127, hits) == 1 && hits[0].lane == 0 && ls.gate_on(64, hits) == 2,
              "step 1 strikes the Timpani alone, step 2 the Taiko and the snare");

        // Four Timpani in a row, not five; the 16 steps loop (16 -> 1).
        for (int s = 9; s <= 12; ++s) lturn(step_id(s), +1);
        CHECK(ls.step(0, 8) == 1 && ls.step(0, 11) == 1, "steps 9-12: four Timpani");
        lturn(step_id(13), +1);
        lturn(step_id(8), +1);
        CHECK(ltext(step_id(13)) == "!!!" && ltext(step_id(8)) == "!!!", "a fifth either side: %s %s",
              ltext(step_id(13)).c_str(), ltext(step_id(8)).c_str());
        lturn(step_id(13), +1);               // the Taiko is not counted
        CHECK(ltext(step_id(13)) == "Taiko", "step 13: %s", ltext(step_id(13)).c_str());
        lturn(step_id(14), +1);
        lturn(step_id(15), +1);
        lturn(step_id(16), +1);               // 14, 15, 16 and step 1: four
        CHECK(ls.step(0, 15) == 1, "steps 14-16 with step 1: four in a row: %s", ltext(step_id(16)).c_str());
        lturn(step_id(13), -1);               // step 13 back to a Timpani: 9-16 and 1 would be nine
        CHECK(ltext(step_id(13)) == "!!!", "joining two runs: %s", ltext(step_id(13)).c_str());

        // Together a step's instruments fit the budget.
        lturn(step_id(13), -1);               // off
        lset(P_LANE, 2);                      // lane C, Cymbal: the Gong on step 5
        lturn(step_id(5), +2);
        lset(P_LANE, 1);
        lturn(step_id(5), +1);                // AcSnre with the Gong
        const bool both = cost(6, 2) + cost(3, 1) <= kCpuBudget;
        CHECK((ltext(step_id(5)) == "AcSnre") == both, "Gong + AcSnre (%d): %s", cost(6, 2) + cost(3, 1),
              ltext(step_id(5)).c_str());
        lset(P_LANE, 0);
        lturn(step_id(5), +2);                // and a Taiko: over the budget
        CHECK(cost(6, 2) + cost(3, 1) + cost(2, 2) > kCpuBudget && ltext(step_id(5)) == "!!!",
              "Gong + AcSnre + Taiko (%d): %s", cost(6, 2) + cost(3, 1) + cost(2, 2), ltext(step_id(5)).c_str());
        lset(P_LANE, 2);
        lturn(step_id(6), +3);                // HHat-O, AcSnre and a Taiko on step 6
        lset(P_LANE, 1);
        lturn(step_id(6), +1);
        lset(P_LANE, 0);
        lturn(step_id(6), +2);
        CHECK(ls.step(0, 5) == 2 && ls.step(1, 5) == 1 && ls.step(2, 5) == 3,
              "HHat-O + AcSnre + Taiko (%d) fit", cost(6, 3) + cost(3, 1) + cost(2, 2));

        CHECK(rules_hold(ls), "the rules hold on the whole pattern");

        // A family change checks the lane's steps again, in order: lane A to
        // Kick plays Kick2 where a fifth Timpani was refused (step 8).
        lturn(P_FAMILY, -2);                  // Kernel -> Kick
        CHECK(ls.family(0) == 0 && ls.step(0, 7) == 1, "step 8 plays Kick2: %d", ls.step(0, 7));
        CHECK(rules_hold(ls), "the rules hold after Kernel -> Kick");
        lturn(P_FAMILY, +2);                  // and back
        CHECK(ls.step(0, 0) == 1 && ls.step(0, 1) == 2, "Kernel again: step 1 Timpni, step 2 Taiko: %d %d",
              ls.step(0, 0), ls.step(0, 1));
        CHECK(rules_hold(ls), "the rules hold after Kick -> Kernel");

        // Any edit keeps them: random turns, lanes and families.
        uint32_t rnd = 12345;
        auto next = [&](int n) { rnd = rnd * 1103515245u + 12345u; return int((rnd >> 16) % uint32_t(n)); };
        int broken = 0;
        for (int i = 0; i < 400; ++i) {
            lset(P_LANE, next(kLanes));
            if (next(8) == 0) lturn(P_FAMILY, next(5) - 2);
            else lturn(step_id(1 + next(kSteps)), next(7) - 3);
            if (!rules_hold(ls)) ++broken;
        }
        CHECK(broken == 0, "400 random edits: the rules always hold (%d broken)", broken);
    }

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

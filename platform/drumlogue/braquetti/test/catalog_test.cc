// Braquetti: the catalog read from Brachetti's family table, with Brachetti's
// engines compiled in (as the unit will have them).  Checks the families and
// instruments the lanes offer, that every name fits the panel, and that the
// core drives the largest family (Tom, 32 instruments) to its end.
#include <cstdio>
#include <cstring>
#include <string>

#include "../catalog.h"
#include "../seq_core.h"

using namespace brq;
using Cat = BrachettiCatalog;

static int s_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++s_fail; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Sequencer<Cat> s_seq;
static uint64_t s_t = 1000000;
static int32_t s_panel[kNumParams];
static void turn(uint8_t id, int clicks) {
    s_t += 20000;
    s_panel[id] += clicks;
    s_seq.set_param(id, s_panel[id], s_t);
}
static std::string text(uint8_t id) { return s_seq.param_text(id, s_panel[id]); }

int main() {
    // The twelve families and their instrument counts (brachetti/README.md).
    static const char* const kNames[] = {"Kick", "Tom", "Kernel", "Snare", "Bar", "Plate", "Cymbal",
                                         "Noise", "World", "Lippa", "Pizzico", "Cordiera"};
    static const int kCounts[] = {3, 32, 2, 8, 19, 7, 6, 8, 9, 8, 5, 7};
    CHECK(Cat::kFamilies == 12, "families: %d", Cat::kFamilies);
    int total = 0, most = 0;
    for (int f = 0; f < Cat::kFamilies && f < 12; ++f) {
        CHECK(std::strcmp(Cat::family_name(f), kNames[f]) == 0, "family %d: %s", f, Cat::family_name(f));
        CHECK(Cat::programs(f) == kCounts[f], "%s: %d instruments", kNames[f], Cat::programs(f));
        CHECK(std::strlen(Cat::family_name(f)) <= 8, "%s fits the panel", Cat::family_name(f));
        total += Cat::programs(f);
        if (Cat::programs(f) > most) most = Cat::programs(f);
        for (int p = 0; p < Cat::programs(f); ++p) {
            const char* n = Cat::program_name(f, p);
            CHECK(n && n[0] && std::strlen(n) <= 8, "%s %d: '%s'", kNames[f], p, n ? n : "(null)");
        }
    }
    CHECK(total == 114, "instruments: %d", total);
    CHECK(most == Cat::kMaxPrograms, "kMaxPrograms is the largest family: %d", most);
    CHECK(std::strcmp(Cat::program_name(1, 0), "Djambe") == 0 && std::strcmp(Cat::program_name(1, 31), "UduBass") == 0 &&
          std::strcmp(Cat::program_name(11, 6), "Gopichd") == 0, "names come from the family units");

    // The core over the real catalog: lane A Kick, B Snare, C Noise; lane B to
    // Tom, step 1 through all 32 instruments.
    s_seq.init();
    for (int i = 0; i < kNumParams; ++i) s_panel[i] = is_relative(uint8_t(i)) ? kRelInit : 0;
    CHECK(text(P_FAMILY) == "Kick", "lane A: %s", text(P_FAMILY).c_str());
    s_panel[P_LANE] = 2;
    s_t += 20000;
    s_seq.set_param(P_LANE, 2, s_t);
    CHECK(text(P_FAMILY) == "Noise", "lane C: %s", text(P_FAMILY).c_str());
    turn(P_STEP1, +3);
    CHECK(text(P_STEP1) == "HHat-C", "lane C step 1: %s", text(P_STEP1).c_str());
    s_panel[P_LANE] = 1;
    s_t += 20000;
    s_seq.set_param(P_LANE, 1, s_t);
    CHECK(text(P_FAMILY) == "Snare", "lane B: %s", text(P_FAMILY).c_str());
    turn(P_FAMILY, -2);
    CHECK(text(P_FAMILY) == "Tom", "lane B to Tom: %s", text(P_FAMILY).c_str());
    turn(P_STEP1, +16);
    turn(P_STEP1, +16);
    CHECK(text(P_STEP1) == "UduBass", "Tom's 32nd: %s", text(P_STEP1).c_str());
    turn(P_STEP1, +1);
    CHECK(text(P_STEP1) == "UduBass", "and no further: %s", text(P_STEP1).c_str());
    turn(P_FAMILY, +10);   // Tom -> Cordiera (7 instruments): position kept, on its last
    CHECK(text(P_FAMILY) == "Cordiera" && text(P_STEP1) == "Gopichd", "family change keeps the step: %s %s",
          text(P_FAMILY).c_str(), text(P_STEP1).c_str());
    Hit hits[kLanes];
    const int n = s_seq.gate_on(127, hits);
    CHECK(n == 2 && hits[0].lane == 1 && hits[0].family == 11 && hits[0].program == 6 &&
          hits[1].lane == 2 && hits[1].family == 7 && hits[1].program == 2, "step 1 strikes Gopichd and HHat-C");

    if (s_fail) { std::printf("%d check(s) FAILED\n", s_fail); return 1; }
    std::printf("catalog: all checks passed (%d families, %d instruments)\n", Cat::kFamilies, total);
    return 0;
}

// Braquetti: the catalog read from Brachetti's family table, with Brachetti's
// engines compiled in (as the unit will have them).  Checks the families and
// instruments the lanes offer, that every name fits the panel, that the
// core drives the largest family (Tom, 32 instruments) to its end, and that
// limits.h has a cost for every instrument (and what README.md says of them).
#include <cstdio>
#include <cstring>
#include <string>

#include "../catalog.h"
#include "../limits.h"
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

    // limits.h: a cost for every instrument, measured (test/instrument_costs.sh).
    CHECK(kCostFamilies == Cat::kFamilies && kCostPrograms == Cat::kMaxPrograms, "the cost table's size");
    int missing = 0, extra = 0, heavies = 0;
    for (int f = 0; f < Cat::kFamilies && f < kCostFamilies; ++f)
        for (int p = 0; p < kCostPrograms; ++p) {
            if (p < Cat::programs(f) && kCost[f][p] == 0) ++missing;
            if (p >= Cat::programs(f) && kCost[f][p] != 0) ++extra;
            if (p < Cat::programs(f) && heavy(f, p + 1)) {
                ++heavies;
                CHECK(std::strcmp(Cat::program_name(f, p), "Timpni") == 0, "heavy: %s", Cat::program_name(f, p));
            }
        }
    CHECK(missing == 0 && extra == 0, "every instrument measured, nothing else (%d missing, %d extra)", missing, extra);
    CHECK(heavies == 1, "one heavy instrument, the Timpani: %d", heavies);
    // README.md, "When the panel shows !!!": any three instruments of three
    // families fit one step, but for the Timpani, the Gong, Riq and Pandeir.
    int worst = 0;
    for (int a = 0; a < Cat::kFamilies; ++a)
        for (int b = a + 1; b < Cat::kFamilies; ++b)
            for (int c = b + 1; c < Cat::kFamilies; ++c) {
                int top[3] = {0, 0, 0};
                const int fam[3] = {a, b, c};
                for (int k = 0; k < 3; ++k)
                    for (int p = 0; p < Cat::programs(fam[k]); ++p) {
                        const char* n = Cat::program_name(fam[k], p);
                        if (!std::strcmp(n, "Timpni") || !std::strcmp(n, "Gong") || !std::strcmp(n, "Riq") ||
                            !std::strcmp(n, "Pandeir")) continue;
                        if (cost(fam[k], p + 1) > top[k]) top[k] = cost(fam[k], p + 1);
                    }
                if (top[0] + top[1] + top[2] > worst) worst = top[0] + top[1] + top[2];
            }
    CHECK(worst <= kCpuBudget, "the heaviest three others cost %d, within %d", worst, kCpuBudget);
    // Its examples (Kick2 0/1, AcSnre 3/1, Gong 6/2, Clap 7/1, HHat-C 7/3, Riq 8/6, Pandeir 8/7).
    auto fits = [](int a, int b, int c) { return a + b + c <= kCpuBudget; };
    CHECK(fits(cost(6, 2), cost(0, 1), cost(7, 3)) && !fits(cost(6, 2), cost(0, 1), cost(7, 1)),
          "the Gong with a kick and a closed hat, not a kick and a clap");
    for (int code = 6; code <= 7; ++code)
        CHECK(fits(cost(8, code), cost(0, 1), cost(7, 1)) && !fits(cost(8, code), cost(0, 1), cost(3, 1)),
              "%s with a kick and a clap, not a kick and a snare", Cat::program_name(8, code - 1));
    CHECK(!fits(cost(6, 2), cost(8, 6), 0) && !fits(cost(6, 2), cost(8, 7), 0), "Gong and Riq or Pandeir: never");

    if (s_fail) { std::printf("%d check(s) FAILED\n", s_fail); return 1; }
    std::printf("catalog: all checks passed (%d families, %d instruments)\n", Cat::kFamilies, total);
    return 0;
}

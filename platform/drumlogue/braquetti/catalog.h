#pragma once
// Braquetti — the instruments the lanes choose from: Brachetti's eight
// families and their programs, as the family units show them today
// (README.md, "Brachetti documents").  When Brachetti on main becomes one
// unit with the families as presets and the instruments as Program values,
// this table follows it.
//
// The sequencer core (seq_core.h) sees only this interface:
//   kFamilies, family_name(f), programs(f), program_name(f, p),
//   default_family(lane)
// Names are at most 8 characters: what the drumlogue's panel shows.
//
// Tables at namespace scope, `static`: a static local of an inline function
// would be shared among every unit the drumlogue loads (STB_GNU_UNIQUE).

namespace brq {

static const char* const kKickNames[]   = {"Kick2", "808Sub", "Kick"};
static const char* const kTomNames[]    = {"Djambe", "Ac Tom", "DeepBs", "Conga", "Handpn", "Bongo", "RackTom"};
static const char* const kKernelNames[] = {"Timpni", "Taiko"};
static const char* const kSnareNames[]  = {"AcSnre", "MrchSnr", "BrshSnr", "RimShot"};
static const char* const kBarNames[]    = {"Marmba", "TblrBel", "Vibrph", "Wodblk", "Kalimba",
                                           "StelPan", "Claves", "GlsBwl", "SltDrm", "GlsBotl"};
static const char* const kPlateNames[]  = {"Cowbel", "Trngle", "BelTre", "Tick"};
static const char* const kCymbalNames[] = {"Cymbal", "Gong", "HHat-O", "Ride", "RidBel", "Splash"};
static const char* const kNoiseNames[]  = {"Clap", "Shaker", "HHat-C"};

struct FamilyDef {
    const char* name;
    int count;
    const char* const* programs;
};

static const FamilyDef kFamilyDefs[] = {
    {"Kick", 3, kKickNames},     {"Tom", 7, kTomNames},     {"Kernel", 2, kKernelNames},
    {"Snare", 4, kSnareNames},   {"Bar", 10, kBarNames},    {"Plate", 4, kPlateNames},
    {"Cymbal", 6, kCymbalNames}, {"Noise", 3, kNoiseNames},
};

struct BrachettiCatalog {
    static constexpr int kFamilies = 8;
    static constexpr int kMaxPrograms = 10;   // the step code stores program + 1 in 4 bits

    static const char* family_name(int f) { return kFamilyDefs[f].name; }
    static int programs(int f) { return kFamilyDefs[f].count; }
    static const char* program_name(int f, int p) { return kFamilyDefs[f].programs[p]; }
    // Lane A kick, lane B snare, lane C cymbal: a beat out of the box.
    static int default_family(int lane) { return lane == 0 ? 0 : lane == 1 ? 3 : 6; }
};

static_assert(sizeof(kFamilyDefs) / sizeof(kFamilyDefs[0]) == BrachettiCatalog::kFamilies,
              "one entry per family");

}  // namespace brq

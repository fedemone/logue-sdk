#pragma once
// Braquetti — the instruments the lanes choose from: Brachetti's families
// and their instruments, read from Brachetti's own family table
// (../brachetti/brachetti_synth.h, `brachetti::kFamilies`).  Nothing is
// copied: a family or an instrument added to Brachetti appears here with the
// next build.
//
// Including this header brings in Brachetti's engines: one ring-over pair
// per family (static in brachetti_synth.h).  That is Braquetti's engine pool
// (PLAN.md, "Engines"); link ../brachetti/family_headers.c with it, which
// defines each family's header the table points to.
//
// The sequencer core (seq_core.h) sees only this interface:
//   kFamilies, kMaxPrograms, family_name(f), programs(f),
//   program_name(f, p), default_family(lane)
#include "../brachetti/brachetti_synth.h"

namespace brq {

struct BrachettiCatalog {
    static constexpr int kFamilies    = brachetti::kNumFamilies;
    static constexpr int kMaxPrograms = 32;   // Tom's 32 instruments; catalog_test checks it

    static const char* family_name(int f) { return brachetti::kFamilies[f].name; }
    static int programs(int f) { return brachetti::kFamilies[f].num_programs; }

    // The family unit's own Program text: its instrument names.
    static const char* program_name(int f, int p) {
        const brachetti::Family& fam = brachetti::kFamilies[f];
        return fam.text(fam.engine, fam.slot[brc::CommonLayout::k_paramProgram], p);
    }

    // Lane A Kick, lane B Snare, lane C Noise (its HHat-C): a beat out of the box.
    static int default_family(int lane) { return lane == 0 ? 0 : lane == 1 ? 3 : 7; }
};

}  // namespace brq

#pragma once
// Brachetti — the twelve family engines, compiled into one unit.
//
// Each family unit in ../brachetti_split is a complete synth (one class,
// BrachettiSynth, plus its presets, voice and file-scope delay lines).  They
// all use the same names, so each is included here inside a namespace of its
// own: fam_kick::BrachettiSynth, fam_tom::BrachettiSynth, ...  Nothing in a
// family's folder is changed or copied: Brachetti plays exactly the code the
// single-family unit plays.
//
// For that to work, everything a family header includes from OUTSIDE its own
// folder must already be included here, at global scope, before the first
// namespace opens: the system headers, the SDK headers and the whole
// brachetti_common library.  Their #pragma once / include guards then make
// the family's own #includes of them no-ops, so they are not re-declared
// inside the family's namespace.  A family header that starts including a
// new outside header needs it added to the list below (the build fails
// loudly otherwise: the header's names end up inside fam_<x>::).

// ── System and SDK headers the families include ─────────────────────────────
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <math.h>
#include <stdint.h>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#include "unit.h"
#include "attributes.h"
#include "runtime.h"

// ── The whole common library ────────────────────────────────────────────────
#include "../brachetti_common/dsp_math.h"
#include "../brachetti_common/float_math.h"
#include "../brachetti_common/knobs.h"
#include "../brachetti_common/common_knobs.h"
#include "../brachetti_common/envelope.h"
#include "../brachetti_common/filter.h"
#include "../brachetti_common/small_filters.h"
#include "../brachetti_common/noise.h"
#include "../brachetti_common/exciter.h"
#include "../brachetti_common/master_stage.h"
#include "../brachetti_common/models.h"
#include "../brachetti_common/modal_bank.h"
#include "../brachetti_common/voice_base.h"
#include "../brachetti_common/voice_layers.h"
#include "../brachetti_common/modal_synth.h"
#include "../brachetti_common/knob_steps.h"
#include "../brachetti_common/ring_over.h"

// ── The families, one namespace each ────────────────────────────────────────
namespace fam_kick     {
#include "../brachetti_split/brachetti_kick/synth_engine.h"
}
namespace fam_tom      {
#include "../brachetti_split/brachetti_tom/synth_engine.h"
}
namespace fam_kernel   {
#include "../brachetti_split/brachetti_kernel/synth_engine.h"
}
namespace fam_snare    {
#include "../brachetti_split/brachetti_snare/synth_engine.h"
}
namespace fam_bar      {
#include "../brachetti_split/brachetti_bar/synth_engine.h"
}
namespace fam_plate    {
#include "../brachetti_split/brachetti_plate/synth_engine.h"
}
namespace fam_cymbal   {
#include "../brachetti_split/brachetti_cymbal/synth_engine.h"
}
namespace fam_noise    {
#include "../brachetti_split/brachetti_noise/synth_engine.h"
}
namespace fam_world    {
#include "../brachetti_split/brachetti_world/synth_engine.h"
}
namespace fam_lippa    {
#include "../brachetti_split/brachetti_lippa/synth_engine.h"
}
namespace fam_pizzico  {
#include "../brachetti_split/brachetti_pizzico/synth_engine.h"
}
namespace fam_cordiera {
#include "../brachetti_split/brachetti_cordiera/synth_engine.h"
}

// Each family unit's own header.c, compiled under another name
// (family_headers.c): its knobs' ranges, types and names.
extern "C" {
extern const unit_header_t brc_hdr_kick, brc_hdr_tom, brc_hdr_kernel, brc_hdr_snare,
                           brc_hdr_bar, brc_hdr_plate, brc_hdr_cymbal, brc_hdr_noise,
                           brc_hdr_world, brc_hdr_lippa, brc_hdr_pizzico, brc_hdr_cordiera;
}

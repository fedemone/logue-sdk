# Brachetti (all families) – Session Brief

`brachetti` (`Brachetti` on the drumlogue, unit_id 'BrAl') is the twelve
Brachetti family units of `../brachetti_split` in one unit: its 12 presets
are the families, `Program` picks the instrument in the selected family.
README.md has the presets, the knob tables and the verification.

## Before changing anything

- **No sound lives here.**  The engines are the family units' own code
  (`families.h` includes each family's `synth_engine.h` inside a namespace
  `fam_<family>`); this folder only routes calls and translates knobs.  A
  change to how an instrument sounds is made in its family unit (and its own
  CLAUDE.md applies), never here.
- **The check.**  After any change here, in a family unit, or in
  `../brachetti_common`:

      cd ../tools/brachetti_equiv && ./brachetti_check.sh

  It must stay IDENTICAL: every family, every instrument, every knob
  position it tries renders bit for bit as on the family unit, and every
  knob reads back as the family's own value with its display text.
  `arm_brachetti_check.sh` runs it on the ARM build (NEON paths, -90 dB per
  render; the Gong at Tang +100 is the known exception, see README.md); `multi_load.sh` checks it loaded next to the family units;
  `arm_size.sh ../../brachetti` measures it and fails on a GNU-unique symbol.
- **Family code must compile inside a namespace.**  So in a family unit:
  never qualify a family name with a bare `::` (the class's
  `preset_name` once called `::preset_name`); and any header a family
  includes from outside its folder (system, SDK, `brachetti_common`) must
  also be in `families.h`'s pre-include list, or its names land inside
  `fam_<family>` and the build fails.  Explicit specialisations of library
  templates cannot be written in a family file.
- **The knob layout is fixed by `header.c`.**  The drumlogue reads the 24
  names and ranges once.  `kFamilies[f].slot` (brachetti_synth.h) says which
  family knob sits in each slot; `brachetti_check.cpp` keeps its own copy of
  that table on purpose, so a change must be made in both.  A slot must be
  at least as wide as every family knob placed in it (the check verifies that
  every value survives the translation); dedicated slots are all "strings"
  knobs so the family's own format is shown.  Header defaults must equal
  what the first preset (Kick, Kick2) loads — the check verifies it.
- **A new family unit** goes in five places: `families.h` (its namespace),
  `family_headers.c` (its header.c), `kFamilies` (preset name, engine, slot
  map; append, so saved projects keep their family numbers), `header.c`
  (`num_presets`, and Program's max if it has more than 32 instruments), and
  `brachetti_check.cpp` / `.sh` / `arm_brachetti_check.sh` and README.md.
  Never reorder `kFamilies`: a project stores the preset index.
- **A new instrument in a family** needs nothing here unless it takes the
  family past 32 instruments (Program's range in `header.c`).
- **`-ffp-contract=off`** is in every Brachetti unit's `config.mk`: without
  it the family code fuses multiply-adds differently in this unit and the ARM
  renders differ from the family units at -30..-50 dB.  Keep it in all
  thirteen.
- **Ringing on.**  Every engine is a `brc::RingOver` pair
  (`../brachetti_common/ring_over.h`): a Program change while it rings hands
  the new instrument to the other copy.  A family switch lets the family left
  behind render until it is silent (`Render` plays every family that still
  sounds); a target family still ringing gets a ring-over Program change, not
  `Init`.  Note-offs, all-notes-off and pitch bend go to every family, a
  gate's release to the family that took the gate.  `brachetti_check.sh`
  checks it (the ring-over cases).
- **Threads.**  `LoadPreset` sets the new family up completely before the
  one atomic store that publishes it.  `Init` runs only on a family that is
  neither selected nor sounding, which the audio thread never renders.  Keep
  any new cross-thread state behind that store.
- **Nothing shared between units.**  As in the family units: no static local
  of an inline function or template (it becomes STB_GNU_UNIQUE and the loader
  shares it among every loaded unit); keep tables and buffers at namespace
  scope with `static` (`s_text`, `kFamilies`, the engines).

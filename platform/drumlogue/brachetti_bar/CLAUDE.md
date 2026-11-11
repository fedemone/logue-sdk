# Brachetti Bar – Session Brief

`brachetti_bar` (`BrcBar` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds mallet and
struck-bar instruments on the free-bar modal bank: Marimba, Tubular Bell,
Vibraphone, Woodblock, Kalimba, Steel Pan, Claves, Glass Bowl, Slit Drum and
Glass Bottle (presets 0-9), and 9 presets added after the split (10-18: WdBlkHi, TmplBlk, SltDrHi, SltDrLo, Bamboo, Coconut, UduTone, KlmbE4, KlmbA4), voiced after the Pelle e Legno samples by `../tools/brachetti_equiv/match_samples.py match_specs.py bar`; `run_equiv.sh` names them and compares the rest.  After any voicing change re-run `preset_probe calib` and update `kOutTrim`.  README.md lists the presets and what every
knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_bar

  Once you change the voicing on purpose it will start to differ — that is
  expected, and the listing then names exactly the presets and knobs that
  moved.  Say so in the commit.
- **Shared code.**  This folder holds the family (`presets.h`, `voice.h`,
  `synth_engine.h`); what the family units share is in `../brachetti_common`,
  whose README has the rules and an index of every helper: look there before
  writing one.  Each file's `From brachetti_common` block lists the helpers it
  uses (`../tools/brachetti_equiv/lib_uses.py --write` keeps it true,
  `--check` verifies it).  A change in the library reaches all eight units: run
  `run_equiv.sh` on each.  A fix that also applies to the original must be made
  there separately.
- **REFERENCE ANCHORS.**  LoadPreset captures each knob's shipped value into
  `m_anchor` through the same `knob::` function the mapping reads; every knob
  mapping is a function of the DELTA from it, built so the delta 0 gives
  exactly the shipped sound (`knob_exp2(0)` is exactly 1.0 — `fasterpow2f(0)`
  is not).  A new mapping must be anchored the same way, or the presets move.
- **Knob ↔ header.c:** `brc::ModalLayout`
  (`../brachetti_common/modal_synth.h`) must match `header.c` slot for slot (a
  static_assert checks the count; the order is on you), header defaults must
  equal preset 0's row, and `kModalHostKnobs` (`modal_synth.h`) must
  match the defaults of the knobs LoadPreset skips (Pitch, Poly, Velocity,
  VlMllRes, Tone, Cutoff, Resnc — static_asserts in `presets.h` pin their
  table columns).
- **Nothing shared between units.**  The drumlogue loads several units into one
  process, and a static local of an inline function or template (a table, a
  buffer) becomes an STB_GNU_UNIQUE symbol the loader shares among all of
  them: the family units once all showed the first-loaded unit's Program
  names.  Keep tables at namespace scope with `static` (as `kPresetNames` in
  `presets.h`) and build display strings in `brc::knob_text`.  `arm_size.sh`
  fails a unit with a unique symbol; `multi_load.sh` checks the units loaded
  together.
- **Exact math for coefficients.**  Tuning and decay coefficients use exact
  `expf` / `cosf` / `exp2f`: the `faster*` approximations in `float_math.h`
  are badly wrong for small arguments and shift low mode frequencies.
- **Memory layout.**  No user-defined constructors: the global synth object
  must stay constant-initialised (it then loads from the ELF instead of being
  built at run time).  Check `arm_size.sh` after adding members or tables.

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_bar`
  (shipped: .text 12476 · .rodata 3168 · .data 1584 · .bss 292).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_bar --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

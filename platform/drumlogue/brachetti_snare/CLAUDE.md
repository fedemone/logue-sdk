# Brachetti Snare – Session Brief

`brachetti_snare` (`BrcSnare` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds the snare
drums: a modal head plus three-band snare-wire resonators — AcSnre, MrchSnr,
BrshSnr and RimShot (presets 0-3), and 4 presets added after the split (4-7: BndDoum, BndTek, CajBass, CajSlap), voiced after the Pelle e Legno samples by `../tools/brachetti_equiv/match_samples.py match_specs.py snare`; `run_equiv.sh` names them and compares the rest.  After any voicing change re-run `preset_probe calib` and update `kOutTrim`.  README.md lists the presets and what
every knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_snare

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
- **Knobs: common, then dedicated.** Pages 1-2 (slots 0-7) are the common
  knobs (`../brachetti_common/common_knobs.h`), the same on all nine units;
  pages 3-6 are this family's.  `brc::ModalLayout`
  (`../brachetti_common/modal_synth.h`) must match `header.c` slot for slot (a
  static_assert checks the count; the order is on you), header defaults must
  equal preset 0's row, and the knobs the host owns (the common Pitch, Poly,
  Velocity, Tone, Cutoff, Resnc, and Crack (the VlMllRes role)) are never
  loaded: static_asserts in `presets.h` pin their table columns.  A knob
  relabelled in `header.c` keeps its role and is listed in
  `../tools/brachetti_equiv/aliases.txt`, so `run_equiv.sh` still compares it
  with the original; a knob with a new job is left out of the comparison and
  named.
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
- `reset_for_strike()` clears the snare-wire state on every hit, so NoteOn
  rebuilds it after the reset (`strike_wires`) — keep that order.
- BrshSnr has its own quadratic velocity curve; the character mappings
  normalise it back to 0..1.

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_snare`
  (shipped: .text 16112 · .rodata 2152 · .data.rel.ro 80 · .data 2016 · .bss 292).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_snare --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

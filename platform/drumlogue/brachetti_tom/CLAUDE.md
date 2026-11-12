# Brachetti Tom – Session Brief

`brachetti_tom` (`BrcTom` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds toms and hand
drums on the membrane modal bank: the original's Djambe, Ac Tom, DeepBs,
Conga, Handpan, Bongo and RackTom (presets 0-6), and 25 hand-drum strokes
added after the split (7-31), voiced after the Pelle e Legno samples.
README.md lists the presets and what every knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_tom

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
  Velocity, Tone, Cutoff, Resnc, and Slap (the VlMllRes role)) are never
  loaded: static_asserts in `presets.h` pin their table columns.  Rel's slot
  is `Punch` (`role_at` returns `brc::kFamilyKnob` there; `fixed_knob` gives
  Rel each preset's shipped value, `kShippedRel`).  A knob relabelled in
  `header.c` keeps its role and is listed in
  `../tools/brachetti_equiv/aliases.txt`, so `run_equiv.sh` still compares it
  with the original; a knob with a new job is left out of the comparison and
  named.
- **Punch** is the shared transient shaper (`brc::Punch`, `voice_layers.h`):
  triggered at the end of NoteOn, applied right after `follow_and_gate`.  At 0
  it is skipped entirely, so every preset renders byte-identically; keep it
  that way.
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
- **The added presets (7-31)** have no counterpart in the original:
  `run_equiv.sh` names them and compares the other seven.  They were voiced by
  `../tools/brachetti_equiv/match_samples.py match_specs.py tom` against
  `../pelle_legno/Samples`; to revoice one, change its spec, re-run the search
  with `--state`, then `--rewrite` replaces its rows in place (`--write`
  appends new presets).
  Their table-driven layers (`slap_*`, `boom_hz` / `glide`, `glide_hold_ms` /
  `glide_ms` in `kVoicing`) stay
  0 on presets 0-6, which keep their own branches (RackTom's glide, Ac Tom's
  and RackTom's slaps): keep it that way, or those presets move.
  After any voicing change, re-run `preset_probe calib` and update `kOutTrim`.
- RackTom's fundamental is the gliding boom, not a mode — its modal config
  note explains why static modes there beat ("sdeng").

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_tom`
  (shipped: .text 15820 · .rodata 7292 · .data.rel.ro 176 · .data 2048 · .bss 292).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_tom --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

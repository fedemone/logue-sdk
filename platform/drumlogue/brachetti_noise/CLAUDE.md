# Brachetti Noise – Session Brief

`brachetti_noise` (`BrcNoise` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds the pure-noise
voices: Clap, Shaker and the closed hi-hat (HHat-C) (presets 0-2), and 5 presets added after the split (3-7: ShkFwd, ShkBack, Maracas, Caxixi, Cabasa), voiced after the Pelle e Legno samples by `../tools/brachetti_equiv/match_samples.py match_specs.py noise`; `run_equiv.sh` names them and compares the rest.  After any voicing change re-run `preset_probe calib` and update `kOutTrim`.
README.md lists the presets and what every knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_noise

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
  knobs (`../brachetti_common/common_knobs.h`: `brc::CommonKnobs` handles
  them), the same on all nine units; the pages after them are this family's.
  `ParamIndex` (`presets.h`) must match `header.c` slot for slot (a
  static_assert checks the count; the order is on you), header defaults must
  equal preset 0's row, and `kHostKnobs` (`presets.h`) must match the defaults
  of the knobs LoadPreset skips (Poly, Velocity, Tone, Cutoff, Resnc —
  static_asserts pin their table columns).  Pitch's slot is blank
  (`k_paramNoPitch`).  A knob relabelled in `header.c` is listed in
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
- **Known difference:** re-selecting Shaker while it rings (README).
- **Knobs of the family's own** (`GrnFade`, `Swell`, `Band`, `VelTone`, and
  `GrnDpth`'s added contrast) took the slots of knobs that did nothing here
  (HitPos, VlMllStf, Model, VlMllRes).  The exciter still reads those at each
  preset's shipped value (`kShippedHitPos`, `kShippedVelKnob`,
  `kShippedModel` in `presets.h`): a new preset needs its Model there.
- There is no Dkay knob; the per-preset `Dky` column only sets how fast
  AllNoteOff fades a voice (`Exciter::set_release_time`).

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_noise`
  (shipped: .text 8292 · .rodata 684 · .data.rel.ro 44 · .data 1008 · .bss 292).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_noise --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

# Brachetti Filo – Session Brief

`brachetti_filo` (`BrcFilo` on the drumlogue) is the eleventh Brachetti family
unit: BrcSnare's eight snares with their wires driven by the head, and
BrcWorld's two berimbaus with their wire made stiff.  It exists so those two
improvements do not break BrcSnare's bit-identity with the original, or
BrcWorld's renders.  README.md has the design, the voicing, the measurements
and the credits.

## Before changing anything

- **It is a copy, and the copy is checked.**  Built with
  `-DBRC_FILO_AS_COPIED` the new layers are off and the presets play
  `kCopiedTrim`: the unit must then be IDENTICAL to BrcSnare (its eight
  snares), to the original (its four classic snares) and to BrcWorld (the
  berimbaus, `Crack` left out).  README.md "Verification" has the three
  commands.  Run them after any change to the shared paths.
- **Each new layer is off at 0, and then the code is the copied code.**  A
  snare with `head_follow` 0 runs `SnareWires::process()` (BrcSnare's), not
  `process_driven()`; a berimbau with `bw_stages` 0 runs BrcWorld's loop and
  BrcWorld's tuning.  Keep the two paths separate: `x + 0.0f` is not always
  `x` (a −0.0 becomes +0.0, and the check hashes the bits).
- **Snare branches come from the table** (`snare_kind`), not the preset
  index, so a preset the voicing search appends takes the branches of the one
  it was copied from.  A new snare preset sets its kind.
- **The berimbau's strike is BrcWorld's**, including what BrcSnare's strike
  leaves in a voice slot: it re-applies the NzRes setter (BrcSnare's strike
  overwrites the noise envelopes' decay) and shapes the exciter with VlMllRes
  at 0 (`Crack` is BrcWorld's `Rattle` there, which only the wire reads).
- **Voicing**: `../tools/brachetti_equiv/match_samples.py match_specs.py
  filo`.  The specs read each preset's rows from `presets.h` and free only the
  new values; write the result back by patching those columns (the classic
  snares' rows are hand-formatted: do not regenerate them), then
  `preset_probe calib` and update `kOutTrim`.  The field order of `Voicing`
  must match `VOICING_FIELDS["filo"]`.
- **Shared code.**  The engine is `../brachetti_common/modal_synth.h`; a
  change in the library reaches all eleven units.  Each file's `From
  brachetti_common` block lists the helpers it uses
  (`../tools/brachetti_equiv/lib_uses.py --write` / `--check`).
- **Knobs: common, then dedicated.**  BrcSnare's layout and names (`Buzz`,
  `BuzzTon`, `Crack`, `Tight`, `Punch` in Partls' slot, `BuzzLen`), with
  `Crack` host-owned; static_asserts pin it and Punch to 0 on every row, and
  `kShippedPartls` holds Partls.  `../tools/brachetti_equiv/aliases.txt`
  names the relabelled knobs for the check against the original.
- **Nothing shared between units** (`kPresetNames`, `s_wire_lines` at
  namespace scope, `static`), **no user-defined constructors**, **per-sample
  code `force_inline`**.

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_filo`
  (shipped: .text 22972 · .rodata 3468 · .data.rel.ro 88 · .data 4 · .bss 19440).
- CPU: `../tools/brachetti_equiv/arm_cpu.sh ../../brachetti_filo`
  (shipped: 1077.0 ARM instructions per rendered frame).
- Knobs: `../tools/brachetti_equiv/knob_liveness.py filo`.

Not yet run on hardware.

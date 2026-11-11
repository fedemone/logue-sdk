# Brachetti World – Session Brief

`brachetti_world` (`BrcWorld` on the drumlogue) is the ninth Brachetti family
unit, built after the split for the instruments no other family engine can
make: Cuica, Berimbu, BerimHi, BalafnC, BalafnG, Riq, Pandeir, Shekere and
Rainstk.  README.md lists the presets, the layers and what the knobs do.

## Before changing anything

- **There is no original to compare with.**  `run_equiv.sh` does not apply.
  Before a refactor, render every preset (`preset_probe render 0 8 110 3 dir`,
  built against this directory) and compare after: a refactor must leave the
  files byte-identical.
- **The presets are voiced after the Pelle e Legno samples** by
  `../tools/brachetti_equiv/match_samples.py match_specs.py world`.  To
  revoice one: change its spec, search with `--state`, then `--rewrite`
  replaces its rows.  Then `preset_probe calib` and update `kOutTrim`.
- **Layers are table-driven.**  A layer is on when its level in `kVoicing`
  is above 0; the field order of `Voicing` must match `VOICING_FIELDS["world"]`
  in `match_samples.py`.
- **Shared code.**  The engine is `../brachetti_common/modal_synth.h`; a
  change in the library reaches all nine units (run `run_equiv.sh` on the
  other eight, and the render comparison above here).  Each file's
  `From brachetti_common` block lists the helpers it uses
  (`../tools/brachetti_equiv/lib_uses.py --write` / `--check`).
- **Nothing shared between units.**  Tables and buffers at namespace scope
  with `static` (`kPresetNames`, `kModeWeight`, `s_wire_lines`), never static
  locals of inline functions (STB_GNU_UNIQUE).  `arm_size.sh` and
  `multi_load.sh` check it.
- **Memory.**  The wires' delay lines (16 KB) live in `.bss`
  (`s_wire_lines`), not in the synth object.  No user-defined constructors.
- **The layers' knobs are anchored too.**  `layer_knobs()` turns six knobs
  into factors once per strike (length, bend, body, material, density,
  presence), each `knob_exp2(slope × (knob − anchor))`, so exactly 1 at the
  shipped value: the strike functions multiply by them and a preset renders
  byte-identically until a knob moves.  A new layer setting that a knob should
  reach goes through one of these factors.  README.md "Knobs" tabulates them.
- **Per-sample code stays inline** (`force_inline` on the layers'
  `process()`).

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_world`
  (shipped: .text 18920 · .rodata 3504 · .data.rel.ro 84 · .data 4 · .bss 21200).
- CPU: `../tools/brachetti_equiv/arm_cpu.sh ../../brachetti_world` (needs
  qemu-user; not yet measured).

Not yet run on hardware.

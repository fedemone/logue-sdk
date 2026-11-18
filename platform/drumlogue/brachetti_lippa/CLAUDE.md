# Brachetti Lippa – Session Brief

`brachetti_lippa` (`BrcLippa` on the drumlogue) is the tenth Brachetti family
unit, built after the split: drums from a delay loop — a Karplus-Strong loop
with a Schroeder all-pass inside it, after Miguel Moreno's "Realistic drums
with karplus-strong synthesis" (2022) and his Pd instrument mymembrane~
(pd-mkmr, BSD-3-Clause: README.md keeps its notice).  Presets 0-7 play the
loop drum, all voiced after recordings of drums: Snare, SnrHvy and Bendir are
drums; Harp, Oud, Fretls, Guitar and BassPiz (0, 1, 4, 5, 7) rang like plucked
strings and are kept as strings, each tuned to the exact pitch of its root.
8-12 (MmSnare, MmKick, MmSnr2, MmTom, MmFloor) play a port of mymembrane~
itself with Moreno's own settings.  README.md has the design, its sources and
credits, the presets and what the knobs do.

## Before changing anything

- **There is no original to compare with.**  `run_equiv.sh` does not apply.
  Before a refactor, render every preset (`preset_probe render 0 12 110 3 dir`,
  built against this directory) and compare after: a refactor must leave the
  files byte-identical.
- **The presets are voiced after recordings** by
  `../tools/brachetti_equiv/match_samples.py match_specs.py lippa`: 0-5
  after the original brachetti's reference recordings (its `samples/`, on the
  `original_brachetti` branch: set `BRACHETTI_ORIG` as for `run_equiv.sh`),
  Bendir and BassPiz after Pelle e Legno samples (BassPiz after the surdo).
  To revoice one: change its spec, search with `--state`, then `--rewrite`
  replaces its rows.  Then `preset_probe calib` and update `kOutTrim`.  The
  specs compare each band's decay after 300 ms (`late`): without it the search
  opens the loss filter and the highs ring on like a string's; and on the
  presets with snares their kurtosis (`sparsity`), without which `sn_crackle`
  is invisible.
- **The strings stay in tune.**  Their specs are `string_spec`: `lp_hz` is the
  root's equal-tempered frequency and is not free, so a search cannot detune
  them.  The specs still score them against the drums they were voiced after,
  so a search would pull them back towards those drums: to improve a string,
  voice it by ear or against a recording of a string, with a new spec.
- **The field order of `Voicing`** must match `VOICING_FIELDS["lippa"]` in
  `match_samples.py`.  The loop drum is on when `lp_level` is above 0,
  mymembrane~ when `mm_level` is (one or the other).
- **mymembrane~ is a port, checked against Pd** (`mm::Membrane`, README.md
  "mymembrane~ in BrcLippa").  Its arithmetic follows Pd's objects; after a
  change run `../tools/brachetti_equiv/mymembrane_check.py <pd-mkmr>/instruments`
  (Pd renders the patch with its help and demo settings; the five Mm presets
  must correlate ≥ 0.93 over the first 200 ms, with the same t40 and
  periodicity).  The trial of the string presets on it (they did not
  become drums) is `match_specs.py`'s `lippa_mm` at commit 8faf63a;
  `mm_spec` is kept for a drum voiced on it, which goes in `SPECS["lippa"]`.
- **The tuning is exact by construction.**  `strike_loop` sets the delay so
  the loop's phase at the note is one period: the delay line plus the phase
  delays of the all-pass, the loss filter and the DC blocker (README.md,
  "Tuning").  Any filter added to the loop must add its phase delay there,
  or the presets go out of tune.  Measure the first resonance after a change
  (README.md, "Verification").
- **Stability.**  The loop gain is capped at `kMaxLoopGain` (0.9995) and every
  filter in the loop has a gain of at most 1, so no knob can make it grow;
  the all-pass gain is clamped to ±0.95.  Keep both true.
- **Shared code.**  The engine is `../brachetti_common/modal_synth.h`; a
  change in the library reaches all eleven units (run `run_equiv.sh` on the
  eight split units, and the render comparison on BrcWorld and here).  Each
  file's `From brachetti_common` block lists the helpers it uses
  (`../tools/brachetti_equiv/lib_uses.py --write` / `--check`).
- **A restrike keeps the ring.**  A slot struck while its loop rings keeps
  its delay lines and filter states (`loop::Drum::Motion`, taken before the
  strike's reset and given back after `strike_loop`), and the glide slews
  from the old ring's factor (`k_slew`).  A fresh strike must stay as it is:
  `k_slew` is 0 there and the per-sample path is the old one.
- **Punch** is the shared transient shaper (`brc::Punch`, `voice_layers.h`):
  triggered at the end of NoteOn, applied right after `follow_and_gate`.  At 0
  it is skipped entirely, so every preset renders byte-identically; keep it
  that way.
- **Nothing shared between units.**  Tables and buffers at namespace scope
  with `static` (`kPresetNames`, `s_loop_lines`, `s_ap_lines`), never static
  locals of inline functions (STB_GNU_UNIQUE).  `arm_size.sh` and
  `multi_load.sh` check it.
- **Memory.**  The loops' delay lines (4 × (2048 + 1024) floats, 48 KB) live
  in `.bss`, not in the synth object.  No user-defined constructors.
- **Knobs: common, then dedicated.**  Pages 1-2 (slots 0-7) are the common
  knobs (`../brachetti_common/common_knobs.h`), the same on all eleven units;
  pages 3-6 are the modal engine's standard set (`brc::ModalLayout`, which
  `header.c` must match slot for slot), except six slots that are BrcLippa's
  own: `Bend` (VlMllRes), `BendTm` (Partls), `Snares` (Model), `Punch` (Rel),
  `Blend` (NzRes), `SnrTone` (NzFltr).  `role_at` returns `brc::kFamilyKnob`
  there; `fixed_knob` gives the displaced knobs their values (Rel each
  preset's `kShippedRel`, Partls 2, Model Drumhd, NzRes 300, NzFltr LP,
  VlMllRes 0).  Every family knob ships 0 (static_asserts) and loads with the
  preset.  `TubRad` is relabelled `ApDelay`.
- **The loop's knobs are anchored.**  `layer_knobs()` turns the knobs into
  factors (or offsets) once per strike, each exactly 1 (0) at the preset's
  shipped value, so a preset renders byte-identically until a knob moves.  A
  new loop setting that a knob should reach goes through one of them.
  README.md "Knobs" tabulates them.
- **Per-sample code stays inline** (`force_inline` on `Drum::process` and
  `excite`).

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_lippa`
  (shipped: .text 23252 · .rodata 3656 · .data.rel.ro 100 · .data 4 · .bss 52752).
- CPU: `../tools/brachetti_equiv/arm_cpu.sh ../../brachetti_lippa`
  (shipped: 998.1 ARM instructions per rendered frame, with the five
  mymembrane~ presets in the bench; 857.4 for the eight loop presets).
- Knobs: `../tools/brachetti_equiv/knob_liveness.py lippa`.

Not yet run on hardware.

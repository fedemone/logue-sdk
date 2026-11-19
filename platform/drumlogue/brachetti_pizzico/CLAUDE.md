# Brachetti Pizzico – Session Brief

`brachetti_pizzico` (`BrcPizzico` on the drumlogue) is the twelfth Brachetti
family unit: five plucked strings (Harp, Oud, Fretls, Guitar, BassPiz) from
BrcLippa's Karplus-Strong loop drum, copied verbatim.  They were voiced in
BrcLippa as drums after recordings of drums, rang as strings, were kept as
strings at the exact pitch of their roots, and moved here byte for byte.
README.md has where they come from, the knobs and the credits;
`../brachetti_lippa/README.md` has the loop itself ("The loop drum",
"Tuning").

## Before changing anything

- **There is no original to compare with.**  `run_equiv.sh` does not apply.
  Before a refactor, render every preset (`preset_probe render 0 4 110 3 dir`,
  built against this directory) and compare after: a refactor must leave the
  files byte-identical.
- **The loop is BrcLippa's, verbatim** (`namespace loop` in `voice.h`,
  `strike_loop` in `synth_engine.h`).  A fix to it in one unit belongs in the
  other: diff the two.  BrcLippa's brief has the loop's rules, and they hold
  here: the tuning is exact by construction (any filter added to the loop
  adds its phase delay to `strike_loop`'s sum, or the strings go out of
  tune); the loop gain is capped at `kMaxLoopGain` and every filter in it has
  a gain of at most 1; the all-pass gain is clamped to ±0.95; a restrike
  keeps the ring (`loop::Drum::Motion`, `k_slew`), a fresh strike's path stays
  the old one.
- **The exciter runs on every voice**, even at `exciter_gain` 0: the loop's
  excitation noise comes from its generator (`v.exciter.noise_gen`).
  BrcLippa skips it only on mymembrane~ voices, which have none here.
- **The strings stay in tune.**  Their specs (`match_specs.py`
  `SPECS["pizzico"]`) are `string_spec`s: `lp_hz` is the root's
  equal-tempered frequency and is not free.  The score is against the drum
  each began as, so a search pulls a string back towards it: to improve a
  string, voice it by ear or against a string's recording, with a new spec;
  then `preset_probe calib` and update `kOutTrim`.
- **The field order of `Voicing`** must match `VOICING_FIELDS["pizzico"]` in
  `match_samples.py` (BrcLippa's fields without mymembrane~'s).
- **Knobs: common, then dedicated.**  Pages 1-2 are the common knobs, the
  same on all twelve units; pages 3-6 the modal engine's standard set, except
  six slots that are the unit's own, as BrcLippa's: `Bend` (VlMllRes),
  `BendTm` (Partls), `Buzz` (Model; BrcLippa's `Snares`), `Punch` (Rel),
  `Blend` (NzRes), `BuzzTon` (NzFltr; BrcLippa's `SnrTone`).  Every family
  knob ships 0 (static_asserts); `fixed_knob` gives the displaced knobs their
  values.  `TubRad` is relabelled `ApDelay`.  The knobs are anchored:
  `layer_knobs()` is exactly the identity at each preset's shipped values.
- **Shared code.**  The engine is `../brachetti_common/modal_synth.h`; a
  change in the library reaches all twelve units.  Each file's `From
  brachetti_common` block lists the helpers it uses
  (`../tools/brachetti_equiv/lib_uses.py --write` / `--check`).
- **Nothing shared between units** (`kPresetNames`, `s_loop_lines`,
  `s_ap_lines` at namespace scope, `static`), **no user-defined
  constructors**, **per-sample code `force_inline`**.

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_pizzico`
  (shipped: .text 19492 · .rodata 1508 · .data.rel.ro 68 · .data 4 · .bss 51824).
- CPU: `../tools/brachetti_equiv/arm_cpu.sh ../../brachetti_pizzico`
  (shipped: 809.7 ARM instructions per rendered frame);
  `arm_cycles.sh ../../brachetti_pizzico Guitar 4` (~1280 estimated A7 cycles
  per frame, four voices).
- Knobs: `../tools/brachetti_equiv/knob_liveness.py pizzico`.
- Restrikes: `../tools/brachetti_equiv/restrike_check.py pizzico`.

Not yet run on hardware.

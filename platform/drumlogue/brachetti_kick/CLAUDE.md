# Brachetti Kick – Session Brief

`brachetti_kick` (`BrcKick` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds the boom-
oscillator kick drums: Kick2 (a kettledrum-bodied kick), 808Sub (a
TR-808-style pitch-diving sub) and Kick (an acoustic bass drum), and nothing
else.  README.md lists the presets and what every knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_kick

  Once you change the voicing on purpose it will start to differ — that is
  expected, and the listing then names exactly the presets and knobs that
  moved.  Say so in the commit.
- **Shared code.**  This folder holds the family (`presets.h`, `voice.h`,
  `synth_engine.h`); what the family units share is in `../brachetti_common`,
  whose README has the rules.  A change there reaches all eight units: run
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
  equal preset 0's row, and `seed_performance_params()` (`modal_synth.h`) must
  match the defaults of the knobs LoadPreset skips (Pitch, Poly, Velocity,
  VlMllRes, Tone, Cutoff, Resnc — static_asserts in `presets.h` pin their
  table columns).
- **Exact math for coefficients.**  Tuning and decay coefficients use exact
  `expf` / `cosf` / `exp2f`: the `faster*` approximations in `float_math.h`
  are badly wrong for small arguments and shift low mode frequencies.
- **Memory layout.**  No user-defined constructors: the global synth object
  must stay constant-initialised (it then loads from the ELF instead of being
  built at run time).  Check `arm_size.sh` after adding members or tables.
- **Known difference:** re-selecting Kick while it rings (README).
- Kick2's velocity → balance trade is scoped to Kick2 on purpose: 808Sub and
  Kick are approved on hardware as they stand.

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_kick`
  (shipped: .text 15692 · .rodata 912 · .data 1952 · .bss 64).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_kick --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

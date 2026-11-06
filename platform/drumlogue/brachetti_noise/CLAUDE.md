# Brachetti Noise – Session Brief

`brachetti_noise` (`BrcNoise` on the drumlogue) is one of the eight per-family
units split out of the 40-preset `../brachetti` unit.  It holds the pure-noise
voices: Clap, Shaker and the closed hi-hat (HHat-C), and nothing else.
README.md lists the presets and what every knob does.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_noise

  Once you change the voicing on purpose it will start to differ — that is
  expected, and the listing then names exactly the presets and knobs that
  moved.  Say so in the commit.
- **This unit is independent.**  A fix that also applies to the original or a
  sibling unit must be made there separately.
- **REFERENCE ANCHORS.**  LoadPreset captures each knob's shipped value into
  an `m_*_ref`; every knob mapping is a function of the DELTA from it, built so
  the delta 0 gives exactly the shipped sound (`knob_exp2(0)` is exactly 1.0 —
  `fasterpow2f(0)` is not).  A new mapping must be anchored the same way, or
  the presets move.
- **Knob ↔ header.c:** `ParamIndex` must match `header.c` slot for slot (a
  static_assert checks the count; the order is on you), header defaults must
  equal preset 0's row, and `seed_performance_params()` must match the
  defaults of the knobs LoadPreset skips (Poly, Velocity, VlMllRes, Tone,
  Cutoff, Resnc — static_asserts in LoadPreset pin their table columns).
- **Exact math for coefficients.**  Tuning and decay coefficients use exact
  `expf` / `cosf` / `exp2f`: the `faster*` approximations in float_math.h are
  badly wrong for small arguments and shift low mode frequencies.
- **Memory layout.**  No user-defined constructors: the global synth object
  must stay constant-initialised (it then loads from the ELF instead of being
  built at run time).  Check `arm_size.sh` after adding members or tables.
- **Known difference:** re-selecting Shaker while it rings (README).
- There is no Dkay knob; the per-preset `Dky` column only sets how fast
  AllNoteOff fades a voice (`apply_release_rate`).

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_noise`
  (shipped: .text 7292 · .rodata 264 · .data 1008 · .bss 48).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_noise --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

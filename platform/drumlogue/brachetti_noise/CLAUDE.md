# Brachetti Noise – Session Brief

`brachetti_noise` (`BrcNoise` on the drumlogue) is one of the nine per-model units
split out of the 40-preset `../brachetti` unit (November 2026).  It holds
the pure-noise voices: Clap, Shaker and the closed hi-hat (HHat-C), and nothing else.

## Before changing anything

- **The original is the reference.**  At the shipped knob values every preset
  here renders bit-identically to the same preset in `../brachetti`.  Check it
  after any refactor (a refactor must keep it IDENTICAL):

      cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_noise

  Once you change the voicing on purpose it will start to differ — that is
  expected, and the listing then names exactly the presets and knobs that
  moved.  Say so in the commit.
- **History lives in `../brachetti/CLAUDE.md` and `../brachetti/README.md`.**
  The code is the original's minus what this model never reaches; comments,
  identifiers (`m_modal_*_ref`, `k_param*`) and the REFERENCE-ANCHOR pattern
  are unchanged, so every pass entry there still applies to the code here.
  `k_paramLowCut` is `k_paramCutoff` and `k_paramCymPoly` is `k_paramPoly`.
- **This unit is independent.**  A fix that also applies to the original or a
  sibling unit must be made there separately.
- **Knob ↔ header.c:** `ParamIndex` must match `header.c` slot for slot (a
  static_assert checks the count; the order is on you), header defaults must
  equal preset 0's row, and `seed_performance_params()` must match the
  defaults of the knobs LoadPreset skips — the same rules as the original.

- **Known difference:** re-selecting Shaker while it rings does not bring
  back the original's modal "tok" (README, "Known difference").
- Dkay has no knob here; the per-preset `Dky` column only sets the AllNoteOff
  fade rate (`apply_release_rate`).

## Measure, do not assume

- ARM sizes: `../tools/brachetti_equiv/arm_size.sh ../../brachetti_noise`
  (shipped: .text 7292 · .rodata 264 · .data 1008 · .bss 48).
- NEON paths on ARM (qemu): `../tools/brachetti_equiv/arm_equiv.sh ../../brachetti_noise --fuzz 20 --knob-points 1`
- Dead knobs: build `../tools/brachetti_equiv/equiv_driver.cpp` against this
  unit and run `dead_params.py` on it.

Not yet run on hardware.

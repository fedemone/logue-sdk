# Brachetti Kernel (`BrcKernel`)

> **Disclaimer:** Brachetti Kernel is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds Timpani and Taiko on the dense coupled-resonator drum kernel.

| | |
|---|---|
| Unit name on the drumlogue | `BrcKernel` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724B6E` ('BrKn') |
| Engine | dense coupled-resonator drum kernel (`ModalDrumKernel`, ~280 modes) |
| Presets | 2 |
| Knobs | 20 |
| ARM size (cross-built, `arm_size.sh`) | .text 10716 · .rodata 31248 · .data 4 · .bss 15428 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Timpni | 52 | timpani: measured kettle modes plus a dense membrane fill, two kettles |
| 1 | Taiko | 41 | taiko: the same kernel with a noise wedge for the broadband decay |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

**Choosing a preset.**  Pick it from the drumlogue's preset menu: the unit
loads it and the panel shows the preset's own knob values.  The `Program`
knob selects the same presets, and it is the one stored with a pattern,
sequenced and motion-recorded — but the drumlogue never redraws the other
knobs when a parameter changes them (a unit cannot update the panel), so
after a Program change they keep showing the previous preset's values, and
turning one sends that stale value plus your turn.  Use the preset menu to
choose a sound, Program to automate one.

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Velocity` · `MlltRes` |
| 2 | `MlltStif` · `VlMllRes` · `VlMllStf` · `Partls` |
| 3 | `Dkay` · `Mterl` · `Tone` · `HitPos` |
| 4 | `Rel` · `Inharm` · `Cutoff` · `TubRad` |
| 5 | `Gain` · `NzMix` · `NzFltFrq` · `Resnc` |

Removed from the original's 24 knobs: `Poly`, `Model`, `NzRes`, `NzFltr`.  The kernel always has its two kettles (Poly), and no modal template or exciter noise path for Model, NzRes or NzFltr to act on.  Each was measured inert on every preset of this unit — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from the root — retunes a kettle (a new note takes the second kettle) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | knock ("wham") level |
| `MlltStif` | impulse sharpness (shorter strike = brighter) |
| `VlMllRes` | hit prominence: boosts the knock and flattens its velocity curve |
| `VlMllStf` | velocity → impulse sharpness amount |
| `Partls` | density of the membrane fill around the measured modes (display: share kept) |
| `Dkay` | T60 (coarse) |
| `Mterl` | material: brighter material lets the HF modes ring longer |
| `HitPos` | knock click |
| `Rel` | T60 (fine) |
| `Inharm` | upper-mode stretch |
| `TubRad` | body size (longer and darker) |
| `Gain` | drive, re-anchored so the shipped value is transparent |
| `NzMix` | noise-wedge level (Timpani ships none — opening it adds the taiko-style air) |
| `NzFltFrq` | noise-wedge start cutoff |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Velocity`, `VlMllRes`, `Tone`, `Cutoff` and
`Resnc` are performance knobs the host owns: a Program change leaves them where
they are.  Every other knob loads with the preset, and the knob mappings are
REFERENCE-ANCHORED on the shipped values — a preset plays its calibrated sound
at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_kernel
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_kernel.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_kernel
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_kernel --fuzz 20 --knob-points 1
```

## Code

| File | What it holds |
|---|---|
| `presets.h` | the 20-knob layout, the presets and the kernel recipe behind each |
| `modal_drum_kernel.h` | the drum: two kettles of up to 280 resonators, the knock, the noise wedge, the bloom, the pitch glide, mode retirement |
| `modal_drum_data.h` | the measured mode tables and recorded transients (generated) |
| `synth_engine.h` | the knobs → the kernel's modifiers, and the unit's own master stage |

The parts every family unit shares — the knob helpers, the REFERENCE ANCHORS, the Velocity knob, note routing and the master stage (the kernel uses its knob half and runs its own limiter) — are in [`brachetti_common`](../brachetti_common/), whose README has the rules for changing them.

## Relation to the original

The code began as the original's, with everything this family never reaches
taken out: the other engines, the other families' per-preset branches and
voice layers, their table columns and their knobs.  Its comments describe this
unit only; the history of how the sounds were voiced stays with the original
unit's documentation.  It was then restructured onto the common library, with
the renders checked bit-identical at every step.  The names follow the
restructured code (`m_anchor.decay` for the original's `m_modal_dkay_ref`, and
so on), so a fix ports between the two by the mapping it changes rather than
by search and replace.

A change to this unit's sound belongs to this unit.  A change in
`brachetti_common` reaches all eight family units, so check all eight; a fix
that also applies to the original has to be made there too (it keeps its own
code).

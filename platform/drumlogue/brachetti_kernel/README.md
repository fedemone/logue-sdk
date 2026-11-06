# Brachetti Kernel (`BrcKernel`)

> **Disclaimer:** Brachetti Kernel is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds Timpani and Taiko on the dense coupled-resonator drum kernel.

| | |
|---|---|
| Unit name on the drumlogue | `BrcKernel` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724B6E` ('BrKn') |
| Engine | dense coupled-resonator drum kernel (`ModalDrumKernel`, ~280 modes) |
| Presets | 2 |
| Knobs | 20 |
| ARM size (cross-built, `arm_size.sh`) | .text 10412 · .rodata 31244 · .data 4 · .bss 15420 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Timpni | 52 | dense-kernel voice. |
| 1 | Taiko | 41 | bright open "TAAAN": data-driven inharmonic modes + bright 1472Hz partial (ratio 16.86, the "AAN" vowel) from Taiko-Hit.wav. … |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend, pitch bend and every knob at its
ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Velocity` · `MlltRes` |
| 2 | `MlltStif` · `VlMllRes` · `VlMllStf` · `Partls` |
| 3 | `Dkay` · `Mterl` · `Tone` · `HitPos` |
| 4 | `Rel` · `Inharm` · `Cutoff` · `TubRad` |
| 5 | `Gain` · `NzMix` · `NzFltFrq` · `Resnc` |

Removed from the original's 24 knobs: `Poly`, `Model`, `NzRes`, `NzFltr`.  The Timpani/Taiko kernel always has its two kettles (Poly), and has no modal-template or legacy noise exciter to give Model, NzRes or NzFltr anything to act on.  Each one was measured inert on every preset of this unit in the original — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

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

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_kernel
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_kernel.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

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

## Relation to the original

The code is the original's, with everything this family never reaches taken
out: the other engines, the other families' per-preset branches and voice
layers, their table columns and their knobs.  Comments, identifiers
(`m_modal_*_ref`, `k_param*`) and the REFERENCE-ANCHOR design are kept as they
were, so the original's `README.md` and `CLAUDE.md` still explain this code —
look there for the history behind any mapping or constant.  `k_paramLowCut` is
called `k_paramCutoff` here (it is the master low-pass "Cutoff"), and
`k_paramCymPoly` is `k_paramPoly`.

Changes made from here on belong to this unit only.  A fix that also applies
to the original or to a sibling unit has to be made there too.

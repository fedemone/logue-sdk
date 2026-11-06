# Brachetti Bar (`BrcBar`)

> **Disclaimer:** Brachetti Bar is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds mallet and struck-bar instruments on the free-bar modal bank: Marimba, Tubular Bell, Vibraphone, Woodblock, Kalimba, Steel Pan, Claves, Glass Bowl, Slit Drum and Glass Bottle.

| | |
|---|---|
| Unit name on the drumlogue | `BrcBar` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724272` ('BrBr') |
| Engine | free-bar modal bank + mallet exciter (`ENGINE_BAR`) |
| Presets | 10 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 13324 · .rodata 690 · .data 2752 · .bss 64 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Marmba | 72 | marimba |
| 1 | TblrBel | 72 | tubular bell |
| 2 | Vibrph | 72 | vibraphone |
| 3 | Wodblk | 48 | woodblock |
| 4 | Kalimba | 65 | kalimba |
| 5 | StelPan | 60 | steel pan (with a metallic FM chirp on the strike) |
| 6 | Claves | 79 | claves |
| 7 | GlsBwl | 76 | glass bowl |
| 8 | SltDrm | 60 | slit drum |
| 9 | GlsBotl | 88 | glass bottle |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Model` · `Dkay` · `Mterl` |
| 4 | `Tone` · `HitPos` · `Rel` · `Inharm` |
| 5 | `Cutoff` · `TubRad` · `Gain` · `NzMix` |
| 6 | `NzRes` · `NzFltr` · `NzFltFrq` · `Resnc` |

Every one of the original's 24 knobs is live on this family (`tools/brachetti_equiv/dead_params.py` measured each across its whole range), so none was removed.

`Partls` sets the mode count around the preset's own (positions 0-4) and displays the count it gives the current preset (`4 md`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from the preset's root |
| `Poly` | voice cap 1-4; every strike stacks (for a bar the overlap is the sound) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | modal presence |
| `MlltStif` | mallet hardness: upper-mode brightness |
| `VlMllRes` | velocity → noise-attack speed (live where the preset has noise: Claves, GlsBotl, …) |
| `VlMllStf` | velocity → mallet stiffness |
| `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | modal ratio template (free bar, marimba bar, tube, plate, …) |
| `Dkay / Rel` | ring length (coarse / fine) |
| `Mterl` | upper-mode material damping |
| `HitPos` | strike position tilt (end vs centre of the bar) |
| `Inharm` | overtone spread |
| `TubRad` | whole-body ring length |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Poly`, `Velocity`, `VlMllRes`, `Tone`,
`Cutoff` and `Resnc` are performance knobs the host owns: a Program change
leaves them where they are.  Every other knob loads with the preset, and the
knob mappings are REFERENCE-ANCHORED on the shipped values — a preset plays its
calibrated sound at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_bar
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_bar.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_bar
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_bar --fuzz 20 --knob-points 1
```

## Relation to the original

The code began as the original's, with everything this family never reaches
taken out: the other engines, the other families' per-preset branches and
voice layers, their table columns and their knobs.  Its comments describe this
unit only; the history of how the sounds were voiced stays with the original
unit's documentation.  Identifiers such as `m_modal_*_ref` keep the original's
names, so a fix can still be ported between the two.

Changes made from here on belong to this unit only.  A fix that also applies
to the original or to a sibling unit has to be made there too.

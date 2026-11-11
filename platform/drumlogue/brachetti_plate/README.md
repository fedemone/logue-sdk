# Brachetti Plate (`BrcPlate`)

> **Disclaimer:** Brachetti Plate is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds small metal percussion on the inharmonic plate modal bank: Cowbell, Triangle, Bell Tree and Tick, and 3 cone bells and finger cymbals voiced after the [Pelle e Legno](../pelle_legno/) samples (AgogoHi, AgogoLo, Zills).

| | |
|---|---|
| Unit name on the drumlogue | `BrcPlate` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272506C` ('BrPl') |
| Engine | inharmonic plate modal bank + ring-coupled noise (`ENGINE_PLATE`) |
| Presets | 7 (4 from the original, 3 added) |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 12996 · .rodata 1616 · .data 1808 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Cowbel | 67 | cowbell: a short, bright clank |
| 1 | Trngle | 69 | triangle: six modes with the >5 kHz metal sheen |
| 2 | BelTre | 84 | bell tree |
| 3 | Tick | 79 | a metallic closed-hat chick with a low clack under it |

### Added: voiced after the Pelle e Legno samples

Voiced by measurement against each sample with `tools/brachetti_equiv/match_samples.py` (specs in `match_specs.py`); the method is described in [BrcTom's README](../brachetti_tom/README.md#how-the-added-presets-were-voiced). No code changed for them.

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 4 | AgogoHi | 82 | agogo, the high cone bell, A#5 | `053_AgogoHi.wav` |
| 5 | AgogoLo | 77 | agogo, the low cone bell, F5 | `054_AgogoLo.wav` |
| 6 | Zills | 99 | finger cymbals clashed: slowly beating partials from 2.5 kHz | `059_Zills.wav` |

The original presets (0-3) render **bit-identically** to
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
| `Poly` | voice cap 1-4; every strike stacks |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | modal presence |
| `MlltStif` | beater hardness: upper-mode brightness |
| `VlMllRes` | velocity → noise-attack speed (live on Tick) |
| `VlMllStf` | velocity → beater stiffness |
| `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | modal ratio template |
| `Dkay / Rel` | ring length (coarse / fine); the noise follows the ring down |
| `Mterl` | upper-mode material damping |
| `HitPos` | strike position tilt |
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
build drumlogue/brachetti_plate
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_plate.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_plate
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_plate --fuzz 20 --knob-points 1
```

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their plate modal configs, each preset's voicing (chirp, shimmer, Tick's hat filter) and output trim |
| `voice.h` | one voice: the exciter with its noise following the ring down, the modal bank, the chirp, the shimmer and Tick's hat filter |
| `synth_engine.h` | the plate on the shared modal engine: the strike recipe and the per-sample voice |

The parts the modal units share — the 24-knob layout, preset loading, the knob mappings, note routing, the exciter, the modal bank, the voice life cycle and the master stage — are in [`brachetti_common`](../brachetti_common/) (`modal_synth.h` is the engine), whose README has the rules for changing them.

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

# Brachetti Snare (`BrcSnare`)

> **Disclaimer:** Brachetti Snare is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds the snare drums: a modal head plus three-band snare-wire resonators — AcSnre, MrchSnr, BrshSnr and RimShot.

| | |
|---|---|
| Unit name on the drumlogue | `BrcSnare` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272536E` ('BrSn') |
| Engine | modal head + three-band snare-wire resonators (`ENGINE_SNARE`) |
| Presets | 4 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 15628 · .rodata 1288 · .data 2000 · .bss 64 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | AcSnre | 38 | acoustic snare: wire buzz plus a swept body boom |
| 1 | MrchSnr | 65 | marching snare: click and buzz land together, tight buzz |
| 2 | BrshSnr | 55 | brush snare: a swish, not a crack — velocity shapes the stroke |
| 3 | RimShot | 69 | rimshot: an 877 Hz honk and a tight buzz |

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
| `Pitch` | semitones from the preset's root — moves the modal head |
| `Poly` | voice cap 1-4; fast same-note strokes fuse and keep the wires rattling (buzz roll) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | buzz AMOUNT (wire rattle vs plain filtered noise) |
| `MlltStif` | buzz BRIGHTNESS (all three wire bands) |
| `VlMllRes` | crack / SNAP (the broadband onset burst) |
| `VlMllStf` | buzz TIGHTNESS (wire Q: loose ↔ zingy) |
| `Partls / Model / Mterl / HitPos / Inharm` | reshape the modal head, as on the toms |
| `Dkay` | modal head ring; on BrshSnr also the swish tail (coarse) |
| `Rel` | buzz TAIL length |
| `TubRad` | body depth: drops the low wire band for a deeper shell (up is capped — it reads toy-ish) |
| `NzRes` | buzz decay |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Poly`, `Velocity`, `VlMllRes`, `Tone`,
`Cutoff` and `Resnc` are performance knobs the host owns: a Program change
leaves them where they are.  Every other knob loads with the preset, and the
knob mappings are REFERENCE-ANCHORED on the shipped values — a preset plays its
calibrated sound at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_snare
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_snare.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_snare
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_snare --fuzz 20 --knob-points 1
```

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their head modal configs, each preset's voicing (wires, boom, swirl, onset) and output trim |
| `voice.h` | one voice: the exciter, the head modal bank, the three-band snare wires, AcSnre's boom, BrshSnr's swirl and the onset ramp |
| `synth_engine.h` | the snare on the shared modal engine: the strike recipe (brush dynamics, the wires, the noise attack), the snare's knob mappings and the per-sample voice |

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

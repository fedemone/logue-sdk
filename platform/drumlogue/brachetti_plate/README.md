# Brachetti Plate (`BrcPlate`)

> **Disclaimer:** Brachetti Plate is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds small metal percussion on the inharmonic plate modal bank: Cowbell, Triangle, Bell Tree and Tick.

| | |
|---|---|
| Unit name on the drumlogue | `BrcPlate` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272506C` ('BrPl') |
| Engine | inharmonic plate modal bank + ring-coupled noise (`ENGINE_PLATE`) |
| Presets | 4 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 13704 · .rodata 354 · .data 2464 · .bss 64 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Cowbel | 67 | clank shortened (modal cfg 500→180ms) + more strike noise (NzMx 25→40) toward the bright ref clank |
| 1 | Trngle | 69 | NzMx 5→10: feeds the raised hf_branch (NoteOn) for the missing >8kHz metal sheen. |
| 2 | BelTre | 84 |  |
| 3 | Tick | 79 | the pre-redesign HHat-C chick + clack mode (modal cfg) |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend, pitch bend and every knob at its
ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Model` · `Dkay` · `Mterl` |
| 4 | `Tone` · `HitPos` · `Rel` · `Inharm` |
| 5 | `Cutoff` · `TubRad` · `Gain` · `NzMix` |
| 6 | `NzRes` · `NzFltr` · `NzFltFrq` · `Resnc` |

No knob was removed: every one of the original's 24 knobs is live on this family (`tools/brachetti_equiv/dead_params.py` measured each across its whole range).

`Partls` keeps only its mode-count positions 0-4: positions 5-7 were the original's ResA/ResB edit selector for its Karplus-Strong string, which this unit does not have.  It now displays the mode count it gives the current preset (`4 md`).

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

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_plate
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_plate.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

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

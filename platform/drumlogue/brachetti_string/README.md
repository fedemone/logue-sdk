# Brachetti String (`BrcString`)

> **Disclaimer:** Brachetti String is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds the Karplus-Strong string: Koto.

| | |
|---|---|
| Unit name on the drumlogue | `BrcString` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42725374` ('BrSt') |
| Engine | dual Karplus-Strong waveguide + overtone modal layer (`ENGINE_KS`) |
| Presets | 1 |
| Knobs | 23 |
| ARM size (cross-built, `arm_size.sh`) | .text 13392 · .rodata 368 · .data 67760 · .bss 568 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Koto | 60 | + harmonic-overtone modal bank (mix 0.10) |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Reset / Suspend, pitch bend and every knob at its
ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Pitch` · `Poly` · `Velocity` · `MlltRes` |
| 2 | `MlltStif` · `VlMllRes` · `VlMllStf` · `Partls` |
| 3 | `Model` · `Dkay` · `Mterl` · `Tone` |
| 4 | `HitPos` · `Rel` · `Inharm` · `Cutoff` |
| 5 | `TubRad` · `Gain` · `NzMix` · `NzRes` |
| 6 | `NzFltr` · `NzFltFrq` · `Resnc` |

Removed from the original's 24 knobs: `Program`.  Koto is the only preset, so there is nothing for a Program knob to choose.  Each one was measured inert on every preset of this unit in the original — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from Koto's root (C4) |
| `Poly` | voice cap 1-4: a different note takes its own string, a repluck reuses it |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes / MlltStif` | pluck body / pluck brightness |
| `VlMllRes` | velocity → noise-attack speed (audible once NzMix is up — Koto ships 0) |
| `VlMllStf` | velocity → pluck stiffness |
| `Partls` | 0-4: partial count (from 16 the second resonator joins, with coupling); 5/6/7 select which resonator Model/Dkay/Mterl/Inharm edit (ResA+B / ResA / ResB) |
| `Model` | physical model of the selected resonator(s): string, beam, plate, membrane, tubes (inverted loop), … |
| `Dkay` | string sustain (loop gain) and the master decay |
| `Mterl / TubRad` | loop loss filter: dull wood … lossless metal, wider tube = less HF loss |
| `HitPos` | ResA/ResB balance and strike radius |
| `Rel` | noise tail (audible with NzMix up) |
| `Inharm` | dispersion (stiff-string inharmonicity) |
| `NzMix / NzRes / NzFltr / NzFltFrq` | noise exciter (into the tube models' loop as well) |

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_string
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_string.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_string
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_string --fuzz 20 --knob-points 1
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

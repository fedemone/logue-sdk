# Brachetti Tom (`BrcTom`)

> **Disclaimer:** Brachetti Tom is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds toms and hand drums on the membrane modal bank: Djambe, Ac Tom, DeepBs, Conga, Handpan, Bongo and RackTom.

| | |
|---|---|
| Unit name on the drumlogue | `BrcTom` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272546D` ('BrTm') |
| Engine | membrane modal bank + boom + strike-transient layer (`ENGINE_MEMBRANE`) |
| Presets | 7 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 15100 · .rodata 520 · .data 3008 · .bss 64 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Djambe | 48 | (HW: ok) |
| 1 | Ac Tom | 45 | Tom |
| 2 | DeepBs | 41 | the pre-redesign Taiko (deep membrane), replaces PluckBass per HW request |
| 3 | Conga | 62 | open tone extended (modal cfg 90→250ms), slap softened (NzFq 710→450) |
| 4 | Handpn | 62 |  |
| 5 | Bongo | 50 | + wood 'tock' mode 5 in modal config |
| 6 | RackTom | 53 | note 53 (F3, 174.6 Hz) = the rack tom an octave-ish over Ac Tom's 45/110 Hz. |

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
| `Pitch` | semitones from the preset's root — moves the modal head (and RackTom's swept boom) |
| `Poly` | voice cap 1-4; same-note strokes inside 80 ms fuse (DeepBs: 300 ms) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | modal presence (how loud the head rings over the strike) |
| `MlltStif` | strike brightness (upper-mode tilt) |
| `VlMllRes` | velocity-weighted SLAP prominence (raises a hand-slap burst where a preset has none) |
| `VlMllStf` | slap sharpness (brighter + shorter up, rounder + longer down) |
| `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | modal ratio template (membrane, drumhead, plate, bar, …) |
| `Dkay` | ring length (coarse) |
| `Mterl` | upper-mode material damping (metal sustains overtones, wood damps them) |
| `HitPos` | strike position: rim feeds the upper modes, centre the fundamental |
| `Rel` | ring length (fine) |
| `Inharm` | overtone spread around the fundamental |
| `TubRad` | whole-body ring length and boom amount |

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_tom
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_tom.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_tom
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_tom --fuzz 20 --knob-points 1
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

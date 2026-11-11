# Brachetti Tom (`BrcTom`)

> **Disclaimer:** Brachetti Tom is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds toms and hand drums on the membrane modal bank: Djambe, Ac Tom, DeepBs, Conga, Handpan, Bongo and RackTom.

| | |
|---|---|
| Unit name on the drumlogue | `BrcTom` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272546D` ('BrTm') |
| Engine | membrane modal bank + boom + strike-transient layer (`ENGINE_MEMBRANE`) |
| Presets | 7 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 14152 · .rodata 1644 · .data 1872 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Djambe | 48 | djembe: bright slap modes over a 240 ms body |
| 1 | Ac Tom | 45 | close-room acoustic tom: 500 ms head plus a stick transient |
| 2 | DeepBs | 41 | deep Taiko-style membrane as a bass voice (~500 ms) |
| 3 | Conga | 62 | conga open tone with a softened slap |
| 4 | Handpn | 62 | handpan: a long, tuned five-mode ring |
| 5 | Bongo | 50 | bongo with a wood "tock" mode |
| 6 | RackTom | 53 | rock rack tom at F3: a gliding boom fundamental under well-separated head modes |

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

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Poly`, `Velocity`, `VlMllRes`, `Tone`,
`Cutoff` and `Resnc` are performance knobs the host owns: a Program change
leaves them where they are.  Every other knob loads with the preset, and the
knob mappings are REFERENCE-ANCHORED on the shipped values — a preset plays its
calibrated sound at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_tom
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_tom.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

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

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their membrane modal configs, each preset's voicing (boom, slap, chirp, onset) and output trim |
| `voice.h` | one voice: the exciter, the membrane modal bank, the boom and its pitch envelope, the stick / hand slap, the chirp and the onset ramp |
| `synth_engine.h` | the tom on the shared modal engine: the strike recipe (roll fusion, the slap), the tom's knob mappings and the per-sample voice |

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

# Brachetti Noise (`BrcNoise`)

> **Disclaimer:** Brachetti Noise is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds the pure-noise voices: Clap, Shaker and the closed hi-hat (HHat-C), and 5 shakers and rattles voiced after the [Pelle e Legno](../pelle_legno/) samples (ShkFwd, ShkBack, Maracas, Caxixi, Cabasa).

| | |
|---|---|
| Unit name on the drumlogue | `BrcNoise` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724E7A` ('BrNz') |
| Engine | shaped noise exciter + AM gating (`ENGINE_NOISE`) |
| Presets | 8 (3 from the original, 5 added) |
| Knobs | 18 |
| ARM size (cross-built, `arm_size.sh`) | .text 7636 · .rodata 728 · .data 992 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Clap | 60 | hand clap: a multi-burst noise gate and a 'tcha' tail |
| 1 | Shaker | 84 | shaker: grain-pulse rattle for the whole tail |
| 2 | HHat-C | 72 | closed hi-hat: a short, bright noise chick |

### Added: voiced after the Pelle e Legno samples

Voiced by measurement against each sample with `tools/brachetti_equiv/match_samples.py` (specs in `match_specs.py`); the method is described in [BrcTom's README](../brachetti_tom/README.md#how-the-added-presets-were-voiced). No code changed for them beyond a table-driven gate (`gate_hz` / `gate_depth` / `gate_decay` / `attack_rate` in `kVoicing`, 0 on Clap, Shaker and HHat-C, which keep their own code).

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 3 | ShkFwd | 84 | shaker, strong stroke: the seeds hit the end wall on the beat | `033_ShakerFwd.wav` |
| 4 | ShkBack | 84 | shaker, weak stroke: a softer hit, more slide | `034_ShakerBack.wav` |
| 5 | Maracas | 84 | a pair of maracas: a bright seed cloud | `035_Maracas.wav` |
| 6 | Caxixi | 84 | caxixi: seeds on the basket's hard gourd base, a sharp chk | `036_Caxixi.wav` |
| 7 | Cabasa | 84 | cabasa: a steel bead chain twisted on a ridged cylinder | `037_Cabasa.wav` |

The original presets (0-2) render **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Poly` · `Velocity` · `MlltRes` |
| 2 | `MlltStif` · `VlMllRes` · `VlMllStf` · `Model` |
| 3 | `Tone` · `HitPos` · `Rel` · `Cutoff` |
| 4 | `Gain` · `NzMix` · `NzRes` · `NzFltr` |
| 5 | `NzFltFrq` · `Resnc` |

Removed from the original's 24 knobs: `Pitch`, `Partls`, `Dkay`, `Mterl`, `Inharm`, `TubRad`.  A noise voice has no modal body (Partls, Mterl, Inharm, TubRad) and nothing in its sound depends on the note (Pitch only ever decided which strokes count as the same note for roll fusion; the sequencer now always plays the preset's root).  Dkay's only remaining effect was how fast AllNoteOff fades a voice; that is kept exact as a per-preset constant in the preset table.  Each was measured inert on every preset of this unit — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Poly` | voice cap 1-4; fast strokes on the same note fuse |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | burst / grain contrast (AM depth on Clap and Shaker), and the mallet body |
| `MlltStif` | burst / grain RATE (AM speed, ±2 octaves on Clap and Shaker) |
| `VlMllRes` | velocity → noise-attack speed |
| `VlMllStf` | velocity → mallet stiffness |
| `Model` | noise band balance: percussion models 0.70, tubes 0.35, others 0.50 |
| `HitPos` | rim strike: brighter mallet and a faster noise attack |
| `Rel` | noise tail ('tcha' on Clap, rattle length on Shaker) |
| `NzMix / NzRes` | noise level / noise envelope |
| `NzFltr / NzFltFrq` | noise filter mode / cutoff |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Poly`, `Velocity`, `VlMllRes`, `Tone`, `Cutoff` and
`Resnc` are performance knobs the host owns: a Program change leaves them where
they are.  Every other knob loads with the preset, and the knob mappings are
REFERENCE-ANCHORED on the shipped values — a preset plays its calibrated sound
at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_noise
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_noise.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_noise
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_noise --fuzz 20 --knob-points 1
```

### Known difference

Re-selecting **Shaker** while a Shaker voice still rings: the original then re-strikes a modal bank in the ringing voice, bringing back a woodblock "tok" the preset does not play.  This unit has no modal bank, so the re-select changes nothing.  The equivalence scripts therefore never re-select that preset while it rings (`--avoid-reselect`).

## Code

| File | What it holds |
|---|---|
| `presets.h` | the 18-knob layout, the presets (with their root note and decay columns), their voicing and output trim |
| `voice.h` | one voice: the exciter and the gate that makes Clap's bursts and Shaker's grains |
| `synth_engine.h` | the unit's own engine, built from the shared pieces: presets and knobs → voices, the gate, voices → the master stage |

The parts every family unit shares — the knob helpers, the REFERENCE ANCHORS, the Velocity knob, note routing, the exciter and voice life cycle and the master stage — are in [`brachetti_common`](../brachetti_common/), whose README has the rules for changing them.

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

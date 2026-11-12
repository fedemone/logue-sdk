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

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Chirp` · `VlMllStf` |
| 4 | dedicated | `Partls` · `Model` · `Dkay` · `Punch` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root |
| `Poly` | voice cap 1-4; every strike stacks |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `MlltRes` | `MlltRes` | modal presence |
| `MlltStif` | `MlltStif` | beater hardness: upper-mode brightness |
| `Chirp` | **own** | the metallic FM chirp on the strike: − subtler, + deeper (×8) and up to 4× longer; BelTre, which ships none, gets one at 1.5 kHz |
| `VlMllStf` | `VlMllStf` | velocity → beater stiffness |
| `Partls` | `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | `Model` | modal ratio template |
| `Dkay` | `Dkay` | ring length; the noise follows the ring down |
| `Punch` | **own** (in `Rel`'s slot) | the transient shaper (see below) |
| `Mterl` | `Mterl` | upper-mode material damping |
| `HitPos` | `HitPos` | strike position tilt |
| `Inharm` | `Inharm` | overtone spread |
| `TubRad` | `TubRad` | whole-body ring length |
| `NzMix / NzRes / NzFltr / NzFltFrq` | the noise knobs | the strike noise |

**Is** names the standard modal-engine knob a dedicated knob is (renamed where it does something specific here), or **own** for a knob of this family in that slot.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 5 ms pass untouched and then the ring drops (τ 30 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

`VlMllRes` (velocity → noise-attack speed) reached only Tick.  Its slot is now `Chirp`, the strike's metallic FM chirp, strongest on Cowbel and Tick (on the agogôs and zills the bank, voiced loud, covers most of it).  `Rel`, a second and finer ring length beside `Dkay`, gave its slot to `Punch` (it plays at each preset's shipped value).

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

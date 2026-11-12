# Brachetti Bar (`BrcBar`)

> **Disclaimer:** Brachetti Bar is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds mallet and struck-bar instruments on the free-bar modal bank: Marimba, Tubular Bell, Vibraphone, Woodblock, Kalimba, Steel Pan, Claves, Glass Bowl, Slit Drum and Glass Bottle, and 9 wood, bamboo, clay and kalimba tines voiced after the [Pelle e Legno](../pelle_legno/) samples (WdBlkHi, TmplBlk, SltDrHi, SltDrLo, Bamboo, Coconut, UduTone, KlmbE4, KlmbA4).

| | |
|---|---|
| Unit name on the drumlogue | `BrcBar` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724272` ('BrBr') |
| Engine | free-bar modal bank + mallet exciter (`ENGINE_BAR`) |
| Presets | 19 (10 from the original, 9 added) |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 12476 · .rodata 3168 · .data 1584 · .bss 292 |

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

### Added: voiced after the Pelle e Legno samples

Voiced by measurement against each sample with `tools/brachetti_equiv/match_samples.py` (specs in `match_specs.py`); the method is described in [BrcTom's README](../brachetti_tom/README.md#how-the-added-presets-were-voiced). No code changed for them.

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 10 | WdBlkHi | 85 | high wood block: 1.15 kHz over a 1.11 kHz partner | `044_WoodBlockHi.wav` |
| 11 | TmplBlk | 72 | temple block (mokugyo): a hollow carved wood bell around C5 | `046_TempleBlock.wav` |
| 12 | SltDrHi | 64 | log-drum tongue, E4, rubber mallet, over the trunk's cavity | `047_SlitDrumHi.wav` |
| 13 | SltDrLo | 57 | log-drum tongue, A3 | `048_SlitDrumLo.wav` |
| 14 | Bamboo | 64 | bamboo tube knocked with a stick: odd air-column harmonics over the wall | `049_BambooKnock.wav` |
| 15 | Coconut | 74 | two coconut half-shells clapped: a hollow clop | `050_Coconut.wav` |
| 16 | UduTone | 45 | udu: a fingertip on the clay belly over the pot's 112 Hz air | `052_UduTone.wav` |
| 17 | KlmbE4 | 64 | kalimba tine, E4: a clamped-free bar, its 'ping' 6.37x up | `055_KalimbaE4.wav` |
| 18 | KlmbA4 | 69 | kalimba tine, A4 | `056_KalimbaA4.wav` |

The original presets (0-9) render **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Punch` · `VlMllStf` |
| 4 | dedicated | `Partls` · `Model` · `Dkay` · `Rel` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root |
| `Poly` | voice cap 1-4; every strike stacks (for a bar the overlap is the sound) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `MlltRes` | `MlltRes` | modal presence |
| `MlltStif` | `MlltStif` | mallet hardness: upper-mode brightness |
| `Punch` | **own** | the transient (see below): + the hit kept and the ring dropped after it, a sharper onset ramp; − a slower onset, up to a 60 ms swell (a bowed vibraphone, a rubbed glass bowl) |
| `VlMllStf` | `VlMllStf` | velocity → mallet stiffness |
| `Partls` | `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | `Model` | modal ratio template (free bar, marimba bar, tube, plate, …) |
| `Dkay / Rel` | `Dkay` / `Rel` | ring length (coarse / fine) |
| `Mterl` | `Mterl` | upper-mode material damping |
| `HitPos` | `HitPos` | strike position tilt (end vs centre of the bar) |
| `Inharm` | `Inharm` | overtone spread |
| `TubRad` | `TubRad` | whole-body ring length |
| `NzMix / NzRes / NzFltr / NzFltFrq` | the noise knobs | the strike noise (the filter knobs only matter where `NzMix` is up) |

**Is** names the standard modal-engine knob a dedicated knob is (renamed where it does something specific here), or **own** for a knob of this family in that slot.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 5 ms pass untouched and then the ring drops (τ 30 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

`VlMllRes` (velocity → noise-attack speed) did nothing on the 14 bars that ship no noise.  Its slot is now `Punch`, which reaches every preset.  Every other knob is live, and keeps its standard name.

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their free-bar modal configs, each preset's voicing and output trim |
| `voice.h` | one voice: the exciter, the modal bank, Steel Pan's chirp and the onset ramp |
| `synth_engine.h` | the bar on the shared modal engine: the strike recipe and the per-sample voice |

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

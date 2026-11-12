# Brachetti Noise (`BrcNoise`)

> **Disclaimer:** Brachetti Noise is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
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

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · — · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `GrnRate` · `GrnDpth` · `GrnFade` · `Swell` |
| 4 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |
| 5 | dedicated | `Band` · `Rel` · `VelTone` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Poly` | voice cap 1-4; fast strokes on the same note fuse |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).  `Pitch`'s slot is blank: nothing in a noise voice follows the note (the sequencer plays the preset's root).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `GrnRate` | `MlltStif` | the grain / burst RATE (the gate on the noise), ±2 octaves; also the faint mallet's stiffness |
| `GrnDpth` | **own** (was `MlltRes`) | the grains' CONTRAST; above the shipped value it also adds contrast, and gives a 30 Hz grain to the presets that ship none (HHat-C, Caxixi) |
| `GrnFade` | **own** | how fast the grains melt into a plain hiss: − slower (×⅛), + faster, and it makes the shakers' never-fading grains fade too |
| `Swell` | **own** | the noise's rise: − snappier, + slower (×1/32 .. ×32, up to ~70 ms) |
| `NzMix / NzRes` | the noise knobs | noise level / noise envelope |
| `NzFltr / NzFltFrq` | the noise knobs | noise filter mode / cutoff |
| `Band` | **own** | the noise's balance: − all the soft body band, + all the bright click band |
| `Rel` | `Rel` | the noise tail ("tcha" on Clap, the rattle's length on the shakers) |
| `VelTone` | **own** | velocity → brightness: + a harder stroke opens the noise filter and a soft one closes it (±2 octaves), − the reverse |

**Is** names the original knob a dedicated knob is, or **own** for a knob of this family.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Knob review

`HitPos` (the strike position) and `VlMllStf` (velocity → stiffness) moved only the mallet, which `NzMix` crossfades almost silent, so they did nothing on any preset; `VlMllRes` (velocity → noise attack) was overridden by the attack of six of the eight presets; and `Model` only picked one of three band balances, on the three presets without their own.  Those slots are now `GrnFade`, `Swell`, `VelTone` and a continuous `Band`.  The mallet knobs, which here set the grains, are named for them.  (Removed in the split, and still absent: `Pitch`, `Partls`, `Dkay`, `Mterl`, `Inharm` and `TubRad`; a noise voice has no modal body and nothing in it follows the note.  Dkay's one remaining job, how fast AllNoteOff fades a voice, is a per-preset constant.)  The liveness tool under-reads the grain knobs: it averages over 43 ms, longer than a grain.

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

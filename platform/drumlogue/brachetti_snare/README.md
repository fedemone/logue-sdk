# Brachetti Snare (`BrcSnare`)

> **Disclaimer:** Brachetti Snare is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds the snare drums: a modal head plus three-band snare-wire resonators — AcSnre, MrchSnr, BrshSnr and RimShot, and 4 a frame drum and a cajón, whose snares are their identity voiced after the [Pelle e Legno](../pelle_legno/) samples (BndDoum, BndTek, CajBass, CajSlap).

| | |
|---|---|
| Unit name on the drumlogue | `BrcSnare` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272536E` ('BrSn') |
| Engine | modal head + three-band snare-wire resonators (`ENGINE_SNARE`) |
| Presets | 8 (4 from the original, 4 added) |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 15492 · .rodata 2152 · .data 2000 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | AcSnre | 38 | acoustic snare: wire buzz plus a swept body boom |
| 1 | MrchSnr | 65 | marching snare: click and buzz land together, tight buzz |
| 2 | BrshSnr | 55 | brush snare: a swish, not a crack — velocity shapes the stroke |
| 3 | RimShot | 69 | rimshot: an 877 Hz honk and a tight buzz |

### Added: voiced after the Pelle e Legno samples

Voiced by measurement against each sample with `tools/brachetti_equiv/match_samples.py` (specs in `match_specs.py`); the method is described in [BrcTom's README](../brachetti_tom/README.md#how-the-added-presets-were-voiced). No code changed for them.

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 4 | BndDoum | 41 | bendir, centre: a large thin head on 88 Hz over its gut snares | `020_BendirDoum.wav` |
| 5 | BndTek | 66 | bendir, edge: the head's upper modes, the snares lighter | `021_BendirTek.wav` |
| 6 | CajBass | 40 | cajon bass: the box's port air and the tapa's low modes | `023_CajonBass.wav` |
| 7 | CajSlap | 65 | cajon slap: the tapa's edge with the snare wires | `024_CajonSlap.wav` |

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
| 3 | dedicated | `Buzz` · `BuzzTon` · `Crack` · `Tight` |
| 4 | dedicated | `Partls` · `Model` · `Dkay` · `BuzzLen` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root: moves the modal head |
| `Poly` | voice cap 1-4; fast same-note strokes fuse and keep the wires rattling (buzz roll) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff`, `Resnc` and `Crack` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `Buzz` | `MlltRes` | buzz AMOUNT (wire rattle vs plain filtered noise) |
| `BuzzTon` | `MlltStif` | buzz BRIGHTNESS (all three wire bands, ±1.6 octaves) |
| `Crack` | `VlMllRes` | the CRACK: the broadband burst before the wires engage; up also lengthens it (up to 4×) and gives one to the snares that ship none (host-owned) |
| `Tight` | `VlMllStf` | buzz TIGHTNESS (the wires' Q: loose ↔ zingy) |
| `Partls / Model / Mterl / HitPos / Inharm` | the modal knobs | reshape the modal head, as on the toms (the frame drums and cajóns have a real head; on AcSnre, MrchSnr and BrshSnr it is a faint ~10 % layer) |
| `Dkay` | `Dkay` | modal head ring; on BrshSnr also the swish tail (coarse) |
| `BuzzLen` | `Rel` | buzz TAIL length |
| `TubRad` | `TubRad` | body depth: drops the low wire band for a deeper shell (up is capped: it reads toy-ish) |
| `NzMix / NzFltr / NzFltFrq` | the noise knobs | the noise that drives the wires: level, filter mode and cutoff |
| `NzRes` | `NzRes` | buzz decay |

**Is** names the standard modal-engine knob a dedicated knob is (renamed where it does something specific here), or **own** for a knob of this family in that slot.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Knob review

`VlMllRes`, the crack, only changed the first few milliseconds of the three snares that ship a crack phase, and nothing on the others.  It is now `Crack` and also lengthens the crack and adds one where a preset has none.  The wire knobs are named for the wires.  `Model` and `Inharm` read as dead on AcSnre, MrchSnr and BrshSnr, whose head is a faint layer, but they are live on RimShot and the four frame-drum and cajón presets, so they keep their job.

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

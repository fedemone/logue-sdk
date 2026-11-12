# Brachetti Kernel (`BrcKernel`)

> **Disclaimer:** Brachetti Kernel is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds Timpani and Taiko on the dense coupled-resonator drum kernel.

| | |
|---|---|
| Unit name on the drumlogue | `BrcKernel` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724B6E` ('BrKn') |
| Engine | dense coupled-resonator drum kernel (`ModalDrumKernel`, ~280 modes) |
| Presets | 2 |
| Knobs | 20 |
| ARM size (cross-built, `arm_size.sh`) | .text 10724 · .rodata 31248 · .data 4 · .bss 15664 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Timpni | 52 | timpani: measured kettle modes plus a dense membrane fill, two kettles |
| 1 | Taiko | 41 | taiko: the same kernel with a noise wedge for the broadband decay |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · — · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `Knock` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 4 | dedicated | `Fill` · `Dkay` · `Rel` · `Mterl` |
| 5 | dedicated | `HitPos` · `Inharm` · `TubRad` · `Punch` |
| 6 | dedicated | `NzMix` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the root: retunes a kettle (a new note takes the second kettle) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Velocity`, `Tone`, `Cutoff`, `Resnc` and `VlMllRes` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).  `Poly`'s slot is blank: the kernel always has its two kettles.

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `Knock` | `MlltRes` | the knock ("wham") level |
| `MlltStif` | `MlltStif` | impulse sharpness (a shorter strike is brighter) |
| `VlMllRes` | `VlMllRes` | hit prominence: boosts the knock and flattens its velocity curve (host-owned) |
| `VlMllStf` | `VlMllStf` | velocity → impulse sharpness |
| `Fill` | `Partls` | density of the membrane fill around the measured modes (the display shows the share kept) |
| `Dkay / Rel` | `Dkay` / `Rel` | T60 (coarse / fine) |
| `Mterl` | `Mterl` | material: a brighter material lets the HF modes ring longer |
| `HitPos` | `HitPos` | knock click |
| `Inharm` | `Inharm` | upper-mode stretch |
| `TubRad` | `TubRad` | body size (longer and darker) |
| `Punch` | **own** | the transient shaper (see below); the two kettles share one output, so a strike on either restarts it |
| `NzMix` | `NzMix` | noise-wedge level (Timpani ships none: opening it adds the taiko-style air) |
| `NzFltFrq` | `NzFltFrq` | noise-wedge start cutoff |

**Is** names the original knob a dedicated knob is, or **own** for a knob of this family.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 8 ms pass untouched and then the kettles' ring drops (τ 30 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

Every knob is live on the kernel (`NzFltFrq` needs `NzMix` up on Timpani, which ships no noise), so none got a new job; two are named for what they do here (`Knock`, `Fill`).  Removed in the split, and still absent: `Poly` (its slot is blank: the kernel always has its two kettles), `Model`, `NzRes` and `NzFltr`.  `Gain` is drive re-anchored so the shipped value is transparent.

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

## Build

```
build drumlogue/brachetti_kernel
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_kernel.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_kernel
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_kernel --fuzz 20 --knob-points 1
```

## Code

| File | What it holds |
|---|---|
| `presets.h` | the 20-knob layout, the presets and the kernel recipe behind each |
| `modal_drum_kernel.h` | the drum: two kettles of up to 280 resonators, the knock, the noise wedge, the bloom, the pitch glide, mode retirement |
| `modal_drum_data.h` | the measured mode tables and recorded transients (generated) |
| `synth_engine.h` | the knobs → the kernel's modifiers, and the unit's own master stage |

The parts every family unit shares — the knob helpers, the REFERENCE ANCHORS, the Velocity knob, note routing and the master stage (the kernel uses its knob half and runs its own limiter) — are in [`brachetti_common`](../brachetti_common/), whose README has the rules for changing them.

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

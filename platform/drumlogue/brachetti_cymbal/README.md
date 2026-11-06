# Brachetti Cymbal (`BrcCymbal`)

> **Disclaimer:** Brachetti Cymbal is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds the dense-resonator metal: Cymbal (crash), Gong, HHat-O, Ride, RidBel and Splash.

| | |
|---|---|
| Unit name on the drumlogue | `BrcCymbal` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724379` ('BrCy') |
| Engine | dense-resonator cymbal (`CymbalVoice`, `ENGINE_CYMBAL`) |
| Presets | 6 |
| Knobs | 19 |
| ARM size (cross-built, `arm_size.sh`) | .text 11704 · .rodata 368 · .data 12272 · .bss 52 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Cymbal | 65 | crash cymbal |
| 1 | Gong | 50 | Chinese gong: a 96-lane bank that swells and blooms |
| 2 | HHat-O | 79 | open hi-hat sizzle |
| 3 | Ride | 69 | ride |
| 4 | RidBel | 60 | ride bell: a stronger, longer stick ping |
| 5 | Splash | 76 | small pitched splash |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Dkay` · `Mterl` · `Tone` |
| 4 | `HitPos` · `Rel` · `Inharm` · `Cutoff` |
| 5 | `TubRad` · `Gain` · `Resnc` |

Removed from the original's 24 knobs: `Model`, `NzMix`, `NzRes`, `NzFltr`, `NzFltFrq`.  The dense-resonator cymbal configures its own noise drivers per preset and has no modal template, so Model and the four noise knobs had nothing to act on.  Each was measured inert on every preset of this unit — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from the root: transposes the whole anchor spectrum (a smaller or larger cymbal); a re-strike on the same note re-excites the ringing voice, a different note takes its own |
| `Poly` | voice cap 1-4; the CPU budget (kCymCostBudget) affords two cymbal voices |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | ring PRESENCE: metal over air (down = the wash dominates) |
| `MlltStif` | beater hardness: stick ping and fast bite vs a slow felt-mallet swell |
| `VlMllRes` | stick "tang" level and attack |
| `VlMllStf` | stick stiffness: faster bite, shorter ping, brighter strike |
| `Partls` | resonator-bank DENSITY 25-60 % (display Rs25% … Rs60%) |
| `Dkay` | overall ring decay |
| `Mterl` | metal brightness (HF tilt of the bank) |
| `HitPos` | edge ↔ bell: wash vs stick ping and pitched ring |
| `Rel` | sizzle / wash tail |
| `Inharm` | jitter spread: beating density / shimmer thickness |
| `TubRad` | instrument size: transposes the spectrum, adds breath |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Poly`, `Velocity`, `VlMllRes`, `Tone`,
`Cutoff` and `Resnc` are performance knobs the host owns: a Program change
leaves them where they are.  Every other knob loads with the preset, and the
knob mappings are REFERENCE-ANCHORED on the shipped values — a preset plays its
calibrated sound at its own knob positions and only movement alters it.

## Build

```
build drumlogue/brachetti_cymbal
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_cymbal.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_cymbal
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_cymbal --fuzz 20 --knob-points 1
```

## Relation to the original

The code began as the original's, with everything this family never reaches
taken out: the other engines, the other families' per-preset branches and
voice layers, their table columns and their knobs.  Its comments describe this
unit only; the history of how the sounds were voiced stays with the original
unit's documentation.  Identifiers such as `m_modal_*_ref` keep the original's
names, so a fix can still be ported between the two.

Changes made from here on belong to this unit only.  A fix that also applies
to the original or to a sibling unit has to be made there too.

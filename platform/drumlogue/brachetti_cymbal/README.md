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
| ARM size (cross-built, `arm_size.sh`) | .text 11120 · .rodata 684 · .data 11936 · .bss 52 |

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

**Choosing a preset.**  Pick it from the drumlogue's preset menu: the unit
loads it and the panel shows the preset's own knob values.  The `Program`
knob selects the same presets, and it is the one stored with a pattern,
sequenced and motion-recorded — but the drumlogue never redraws the other
knobs when a parameter changes them (a unit cannot update the panel), so
after a Program change they keep showing the previous preset's values, and
turning one sends that stale value plus your turn.  Use the preset menu to
choose a sound, Program to automate one.

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
| `Poly` | voice cap 1-4; the CPU budget (`kCostBudget`) affords two cymbal voices |
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

## Code

| File | What it holds |
|---|---|
| `presets.h` | the 19-knob layout, the presets, the inharmonic anchor tables, each preset's strike (config, root, ring scale, level) and output trim |
| `voice.h` | one voice: the dense resonator bank, its noise drivers, the stick, the phase modulation, the subsonic guards and the output stage |
| `synth_engine.h` | the voice policy and CPU budget, the knob → strike mappings, voices → the master stage |

The parts every family unit shares — the knob helpers, the REFERENCE ANCHORS, the Velocity knob, note routing and the master stage — are in [`brachetti_common`](../brachetti_common/), whose README has the rules for changing them.

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

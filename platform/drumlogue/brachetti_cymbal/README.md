# Brachetti Cymbal (`BrcCymbal`)

> **Disclaimer:** Brachetti Cymbal is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
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
| ARM size (cross-built, `arm_size.sh`) | .text 11136 · .rodata 684 · .data 11936 · .bss 292 |

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

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Tang` · `VlMllStf` |
| 4 | dedicated | `Density` · `Dkay` · `Rel` · `Mterl` |
| 5 | dedicated | `HitPos` · `Inharm` · `TubRad` · `Punch` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the root: transposes the whole anchor spectrum (a smaller or larger cymbal); a re-strike on the same note re-excites the ringing voice, a different note takes its own |
| `Poly` | voice cap 1-4; the CPU budget (`kCostBudget`) affords two cymbal voices |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff`, `Resnc` and `Tang` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `MlltRes` | `MlltRes` | ring PRESENCE: metal over air (down = the wash dominates) |
| `MlltStif` | `MlltStif` | beater hardness: stick ping and fast bite vs a slow felt-mallet swell |
| `Tang` | `VlMllRes` | the stick's "tang": level and attack (host-owned) |
| `VlMllStf` | `VlMllStf` | stick stiffness: faster bite, shorter ping, brighter strike |
| `Density` | `Partls` | resonator-bank DENSITY 25-60 % (display Rs25% … Rs60%) |
| `Dkay` | `Dkay` | overall ring decay |
| `Rel` | `Rel` | sizzle / wash tail |
| `Mterl` | `Mterl` | metal brightness (HF tilt of the bank) |
| `HitPos` | `HitPos` | edge ↔ bell: wash vs stick ping and pitched ring |
| `Inharm` | `Inharm` | jitter spread: beating density / shimmer thickness |
| `TubRad` | `TubRad` | instrument size: transposes the spectrum, adds breath |
| `Punch` | **own** | the transient shaper (see below): up, the stick and bite pass and the wash drops after them |

**Is** names the original knob a dedicated knob is, or **own** for a knob of this family.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 5 ms pass untouched and then the wash drops (τ 40 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

Every knob is live on the cymbals, so none got a new job; two are named for what they do here (`Density`, `Tang`).  Removed in the split, and still absent: `Model` and the four noise knobs (the cymbal configures its own noise drivers per preset and has no modal template).

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

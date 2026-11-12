# Brachetti Kick (`BrcKick`)

> **Disclaimer:** Brachetti Kick is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds the boom-oscillator kick drums: Kick2 (a kettledrum-bodied kick), 808Sub (a TR-808-style pitch-diving sub) and Kick (an acoustic bass drum).

| | |
|---|---|
| Unit name on the drumlogue | `BrcKick` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724B6B` ('BrKk') |
| Engine | membrane modal bank + boom oscillator + thump layer (`ENGINE_MEMBRANE`, kick block) |
| Presets | 3 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 15560 · .rodata 912 · .data 1952 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Kick2 | 36 | kettledrum-bodied kick: a fixed boom under a fundamental-dominant modal body |
| 1 | 808Sub | 36 | TR-808-style sub: boom pitch dive 160→45 Hz (`Dive`, `DiveTm`) |
| 2 | Kick | 36 | acoustic bass drum: swept boom (~90→55 Hz) plus beater thump and click |

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
| 3 | dedicated | `Thump` · `Snap` · `VelThmp` · `VelSnap` |
| 4 | dedicated | `DiveTm` · `Punch` · `Dkay` · `Rel` |
| 5 | dedicated | `Weight` · `Click` · `Dive` · `Tune` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root (the booms are absolute Hz: Pitch moves Kick2's modal body and the roll-fusion note) |
| `Poly` | voice cap 1-4; same-note strokes inside 80 ms fuse into one voice |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff`, `Resnc` and `VelThmp` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset (it is part of its gain staging).  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

| Knob | Is | In this unit |
|---|---|---|
| `Thump` | `MlltRes` | the thump AMOUNT: a fast pitch-dropping mid punch over the boom (0 at the shipped value) |
| `Snap` | `MlltStif` | the thump's snap and pitch; below the shipped value it stretches the boom's onset (a felt-mallet "whoomp") |
| `VelThmp` | `VlMllRes` | velocity-weighted thump prominence: trades boom for punch (host-owned) |
| `VelSnap` | `VlMllStf` | velocity-weighted beater knock; down = rounder, weightier |
| `DiveTm` | **own** | how fast the pitch dive falls, ×¼ .. ×4: 808Sub's dive, Kick's 90 → 55 Hz sweep, Kick2's dive once `Dive` is up |
| `Punch` | **own** | the transient (see below): + a shorter onset ramp, a faster boom rise and the boom dropped after a 10 ms hit; − softer and slower (808Sub, which ships no onset, gets up to 30 ms) |
| `Dkay` | `Dkay` | boom decay length (coarse); also how fast AllNoteOff fades a voice |
| `Rel` | `Rel` | boom decay length (fine) |
| `Weight` | `Mterl` | boom body WEIGHT, down to silence at the floor (a thump-only kick with `Thump` up) |
| `Click` | `HitPos` | a beater click above the shipped value (the lower half is inert by design) |
| `Dive` | `Inharm` | pitch-dive depth: 808Sub's dive, Kick's sweep depth; up gives Kick2 a dive (up to +87 Hz) on top of spreading its body's overtones |
| `Tune` | `TubRad` | boom base tune (bigger shell = lower) |
| `NzMix / NzRes / NzFltr / NzFltFrq` | the noise knobs | the strike noise: level, envelope, filter mode and cutoff |

**Is** names the standard modal-engine knob a dedicated knob is (renamed where it does something specific here), or **own** for a knob of this family in that slot.  Every dedicated knob (but a host-owned one, which keeps its place across a Program change) loads with the preset, and is REFERENCE-ANCHORED on the value it shipped with: a preset plays its calibrated sound at its own knob positions, and only movement alters it.  The knobs this unit's own knobs replaced keep playing at each preset's shipped value, so every preset sounds exactly as it did.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 10 ms pass untouched and then the boom drops (τ 25 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

The modal knobs had little to act on: `Partls` and `Model` only reached Kick2's small modal body and the noise band balance (under 1 dB on Kick2, nothing on 808Sub and Kick), and `Inharm` did nothing on Kick.  Their slots now hold `DiveTm` and `Punch`, and `Inharm` became `Dive`, a pitch dive on all three kicks.  The other knobs keep their roles under names that say what they do on a kick.  (`Pitch` still does not move 808Sub's or Kick's boom, whose pitch is absolute; `Tune` is their tuning.)

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

## Build

```
build drumlogue/brachetti_kick
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_kick.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_kick
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_kick --fuzz 20 --knob-points 1
```

### Known difference

Re-selecting **Kick** (the Program knob set to the preset that is already selected) while a Kick voice still rings: the original then writes a stray snare-wire level (0.03) from its preset table into the ringing voice.  This unit has no snare-wire layer, so the re-select changes nothing.  The equivalence scripts therefore never re-select that preset while it rings (`--avoid-reselect`).

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets (one row of knob values each), Kick2's membrane modal config, each preset's voicing (boom, thump, click, chirp, onset) and output trim |
| `voice.h` | one voice: the exciter, Kick2's modal bank, the boom and its pitch sweep, the beater thump, the beater click, the chirp and the onset ramp |
| `synth_engine.h` | the kick on the shared modal engine: the strike recipe, the kick's knob mappings and the per-sample voice |

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

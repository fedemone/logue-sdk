# Brachetti Kick (`BrcKick`)

> **Disclaimer:** Brachetti Kick is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds the boom-oscillator kick drums: Kick2 (a kettledrum-bodied kick), 808Sub (a TR-808-style pitch-diving sub) and Kick (an acoustic bass drum).

| | |
|---|---|
| Unit name on the drumlogue | `BrcKick` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724B6B` ('BrKk') |
| Engine | membrane modal bank + boom oscillator + thump layer (`ENGINE_MEMBRANE`, kick block) |
| Presets | 3 |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 16220 · .rodata 298 · .data 2624 · .bss 64 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Kick2 | 36 | the pre-redesign Timpani body as a solid kettledrum kick (HW-approved) |
| 1 | 808Sub | 36 | boom_osc pitch dive 160→45Hz, depth on Inharm (pass 22's mapping, live again since pass 35). |
| 2 | Kick | 36 | Drum — (HW: ok) |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend, pitch bend and every knob at its
ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Model` · `Dkay` · `Mterl` |
| 4 | `Tone` · `HitPos` · `Rel` · `Inharm` |
| 5 | `Cutoff` · `TubRad` · `Gain` · `NzMix` |
| 6 | `NzRes` · `NzFltr` · `NzFltFrq` · `Resnc` |

No knob was removed: every one of the original's 24 knobs is live on this family (`tools/brachetti_equiv/dead_params.py` measured each across its whole range).

`Partls` keeps only its mode-count positions 0-4: positions 5-7 were the original's ResA/ResB edit selector for its Karplus-Strong string, which this unit does not have.  It now displays the mode count it gives the current preset (`4 md`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from the preset's root (the booms are absolute Hz; Pitch moves Kick2's modal body and the roll-fusion note) |
| `Poly` | voice cap 1-4; same-note strokes inside 80 ms fuse into one voice |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | thump AMOUNT — a fast pitch-dropping mid punch over the boom (0 at the shipped value) |
| `MlltStif` | thump SNAP / pitch; below the shipped value it stretches the boom onset (a felt-mallet "whoomp") |
| `VlMllRes` | velocity-weighted thump prominence — trades boom for punch |
| `VlMllStf` | velocity-weighted beater snap (adds its own knock; down = rounder, weightier) |
| `Partls / Model / Inharm(modal)` | reshape Kick2's modal body (808Sub and Kick have none) |
| `Dkay` | boom decay length (coarse) |
| `Mterl` | boom body WEIGHT — down to silence at the floor (thump-only kick with MlltRes up) |
| `HitPos` | beater click (a bright tick above the shipped value) |
| `Rel` | boom decay length (fine) |
| `Inharm` | 808 pitch-dive depth (strongest on 808Sub) |
| `TubRad` | boom base tune (bigger shell = lower) |

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_kick
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_kick.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

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

Re-selecting **Kick** (the Program knob set to the preset that is already selected) while a Kick voice is still ringing.  The original then writes Kick's table value `snare_wire_mix = 0.03` into the ringing voice, which switches on a faint snare-wire layer for the rest of that voice — a snare mechanism on a kick drum.  This unit has no snare-wire code, so the re-select changes nothing.  Every other path is bit-identical (the harness proves the only difference is that one write: see tools/brachetti_equiv/README.md).  The equivalence scripts therefore never re-select that preset while it rings (`--avoid-reselect`).

## Relation to the original

The code is the original's, with everything this family never reaches taken
out: the other engines, the other families' per-preset branches and voice
layers, their table columns and their knobs.  Comments, identifiers
(`m_modal_*_ref`, `k_param*`) and the REFERENCE-ANCHOR design are kept as they
were, so the original's `README.md` and `CLAUDE.md` still explain this code —
look there for the history behind any mapping or constant.  `k_paramLowCut` is
called `k_paramCutoff` here (it is the master low-pass "Cutoff"), and
`k_paramCymPoly` is `k_paramPoly`.

Changes made from here on belong to this unit only.  A fix that also applies
to the original or to a sibling unit has to be made there too.

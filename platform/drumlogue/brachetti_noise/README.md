# Brachetti Noise (`BrcNoise`)

> **Disclaimer:** Brachetti Noise is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of nine drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per sound
model, so each can be browsed, used and improved on its own.  The original
unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and how they were derived.

This unit holds the pure-noise voices: Clap, Shaker and the closed hi-hat (HHat-C).

| | |
|---|---|
| Unit name on the drumlogue | `BrcNoise` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724E7A` ('BrNz') |
| Engine | shaped noise exciter + AM gating (`ENGINE_NOISE`) |
| Presets | 3 |
| Knobs | 18 |
| ARM size (cross-built, `arm_size.sh`) | .text 7292 · .rodata 264 · .data 1008 · .bss 48 |

## Presets

| # | Name | Root | Notes |
|---|---|---|---|
| 0 | Clap | 60 | multi-burst AM (~55Hz, NoteOn) + Rel 19 for the 'tcha' tail; NzFq 300→200 (BP 2kHz) toward ref centroid 2986Hz (was 4540) |
| 1 | Shaker | 84 | Rel 18→19 longer tail so the now-sustained 17Hz rattle (noise_am_decay=1.0) is audible by default; raise Rel for a longer rattle |
| 2 | HHat-C | 72 | the pre-redesign Shaker noise voice ('a perfect closed hi-hat' per HW) |

At the knob values each preset ships with, it renders **bit-identically** to
the same preset in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend, pitch bend and every knob at its
ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Poly` · `Velocity` · `MlltRes` |
| 2 | `MlltStif` · `VlMllRes` · `VlMllStf` · `Model` |
| 3 | `Tone` · `HitPos` · `Rel` · `Cutoff` |
| 4 | `Gain` · `NzMix` · `NzRes` · `NzFltr` |
| 5 | `NzFltFrq` · `Resnc` |

Removed from the original's 24 knobs: `Pitch`, `Partls`, `Dkay`, `Mterl`, `Inharm`, `TubRad`.  A noise voice has no modal body (Partls, Mterl, Inharm, TubRad) and nothing in its sound depends on the note (Pitch only ever decided which strokes count as the same note for roll fusion; the sequencer now always plays the preset's root).  Dkay's only remaining effect was how fast AllNoteOff fades a voice; that is kept exact as a per-preset constant in the preset table.  Each one was measured inert on every preset of this unit in the original — swept across its whole range in several contexts, every render bit-identical (`tools/brachetti_equiv/dead_params.py`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Poly` | voice cap 1-4; fast strokes on the same note fuse |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | burst / grain contrast (AM depth on Clap and Shaker), and the mallet body |
| `MlltStif` | burst / grain RATE (AM speed, ±2 octaves on Clap and Shaker) |
| `VlMllRes` | velocity → noise-attack speed |
| `VlMllStf` | velocity → mallet stiffness |
| `Model` | noise band balance: percussion models 0.70, tubes 0.35, others 0.50 |
| `HitPos` | rim strike: brighter mallet and a faster noise attack |
| `Rel` | noise tail ('tcha' on Clap, rattle length on Shaker) |
| `NzMix / NzRes` | noise level / noise envelope |
| `NzFltr / NzFltFrq` | noise filter mode / cutoff |

`Tone` (master tilt EQ), `Cutoff` / `Resnc` (master low-pass and its Q) and
`Gain` (master drive) work as in the original.  `Pitch`, `Poly`, `Velocity`,
`VlMllRes`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns:
a Program change leaves them where they are.  Every other knob loads with the
preset, and the knob mappings are REFERENCE-ANCHORED on the shipped values — a
preset plays its calibrated sound at its own knob positions and only movement
alters it.

## Build

```
build drumlogue/brachetti_noise
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_noise.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.

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

Re-selecting **Shaker** while a Shaker voice is still ringing.  The original then re-strikes the voice's modal bank at its config mix (0.15), so the woodblock "tok" the preset deliberately removed comes back for that voice.  This unit has no modal bank, so the re-select changes nothing.  The equivalence scripts therefore never re-select that preset while it rings (`--avoid-reselect`).

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

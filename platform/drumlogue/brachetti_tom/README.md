# Brachetti Tom (`BrcTom`)

> **Disclaimer:** Brachetti Tom is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

One of eight drumlogue synth units split out of the 40-preset
[`brachetti`](../brachetti/) physical-modelling percussion unit, one per
instrument family, so each can be browsed, used and improved on its own.  The
original unit stays as it is; see [Brachetti family](../tools/brachetti_equiv/README.md)
for the whole set and the tools that check them.

This unit holds toms and hand drums on the membrane modal bank: the original's Djambe, Ac Tom, DeepBs, Conga, Handpan, Bongo and RackTom, and 25 hand-drum strokes voiced after the [Pelle e Legno](../pelle_legno/) samples (djembe, conga, bongo, tabla, darbuka, talking drum, surdo, tamborim, udu).

| | |
|---|---|
| Unit name on the drumlogue | `BrcTom` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272546D` ('BrTm') |
| Engine | membrane modal bank + boom + strike-transient layer (`ENGINE_MEMBRANE`) |
| Presets | 32 (7 from the original, 25 added) |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 14420 · .rodata 7004 · .data 1872 · .bss 292 |

## Presets

| # | Name | Root | Sound |
|---|---|---|---|
| 0 | Djambe | 48 | djembe: bright slap modes over a 240 ms body |
| 1 | Ac Tom | 45 | close-room acoustic tom: 500 ms head plus a stick transient |
| 2 | DeepBs | 41 | deep Taiko-style membrane as a bass voice (~500 ms) |
| 3 | Conga | 62 | conga open tone with a softened slap |
| 4 | Handpn | 62 | handpan: a long, tuned five-mode ring |
| 5 | Bongo | 50 | bongo with a wood "tock" mode |
| 6 | RackTom | 53 | rock rack tom at F3: a gliding boom fundamental under well-separated head modes |

### Added: voiced after the Pelle e Legno samples

One preset per stroke of the [Pelle e Legno](../pelle_legno/) kit's hand
drums, so a single MULTI track can play a whole djembe or conga part with the
Program knob motion-sequenced per step.  The Pelle samples are rendered from
the same physical descriptions; each preset here is voiced after its sample by
measurement (see *How the added presets were voiced* below).

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 7 | DjmTone | 60 | tone: the head's 262 Hz pitch with its 0.6-1.9 kHz ring | `002_DjembeTone.wav` |
| 8 | DjmSlap | 84 | slap: a skin crack over a 1.07 kHz ring | `003_DjembeSlap.wav` |
| 9 | DjmBass | 38 | bass: the goblet's 74 Hz air, pushed by the palm, settling from 7 % sharp | `001_DjembeBass.wav` |
| 10 | CngOpen | 60 | open tone at 271 Hz (mode 1 is its weak partner at the C4 root) | `004_CongaOpen.wav` |
| 11 | CngMute | 76 | muted tone: 657 Hz, closed by the hand | `005_CongaMute.wav` |
| 12 | CngSlap | 72 | open slap: the head's edge-mode cloud under a skin crack | `006_CongaSlap.wav` |
| 13 | CngHeel | 51 | heel of the palm: a closed low thud | `007_CongaHeel.wav` |
| 14 | Tumba | 55 | the big conga's open tone, G3 | `008_TumbaOpen.wav` |
| 15 | BngMach | 70 | the small bongo | `009_BongoMacho.wav` |
| 16 | BngHmbr | 69 | the large bongo | `010_BongoHembra.wav` |
| 17 | TblaNa | 61 | Na: the dayan's harmonic series on C#4, 2nd and 3rd harmonics ringing | `011_TablaNa.wav` |
| 18 | TblaTin | 58 | Tin: harmonics of A#3, the 3rd strongest | `012_TablaTin.wav` |
| 19 | TblaTun | 60 | Tun: the open centre stroke, the C4 fundamental rings | `013_TablaTun.wav` |
| 20 | TblaGe | 43 | Ge: the bayan's open bass on G2, settling from 6 % sharp | `014_TablaGe.wav` |
| 21 | TblGeBd | 43 | Ge with the heel sliding toward the syahi: the bass bends up ~5 semitones | `015_TablaGeBend.wav` |
| 22 | TblaKe | 43 | Ke: a flat palm on the bayan, left there | `016_TablaKe.wav` |
| 23 | DrbDoum | 43 | doum: the goblet's 96 Hz air under the head | `017_DarbukaDoum.wav` |
| 24 | DrbTek | 84 | tek: the ring finger on the rim, the bright edge modes ring | `018_DarbukaTek.wav` |
| 25 | DrbKa | 84 | ka: the other hand's rim stroke, a softer tek | `019_DarbukaKa.wav` |
| 26 | TalkUp | 50 | talking drum squeezed as it rings: D3 up about a fifth | `025_TalkDrumUp.wav` |
| 27 | TalkDn | 50 | talking drum released as it rings: down about a fifth to D3 | `026_TalkDrumDn.wav` |
| 28 | SurdOpn | 34 | surdo, felt mallet, A#1: a long 58 Hz boom settling from 6 % sharp | `027_SurdoOpen.wav` |
| 29 | SurdMut | 34 | the surdo damped by the hand | `028_SurdoMute.wav` |
| 30 | Tambrim | 73 | tamborim: a small tight head under a thin stick | `029_Tamborim.wav` |
| 31 | UduBass | 45 | udu: the palm over the side hole, the pot's air rising 85 -> 112 Hz | `051_UduBass.wav` |

The original seven presets (0-6) render **bit-identically** to the same presets
in the original `brachetti` — through the sequencer gate, MIDI
notes, stacked strokes, Program changes over ringing voices, Reset / Suspend,
pitch bend and every knob at its ends and in between (see Verification).

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Model` · `Dkay` · `Mterl` |
| 4 | `Tone` · `HitPos` · `Rel` · `Inharm` |
| 5 | `Cutoff` · `TubRad` · `Gain` · `NzMix` |
| 6 | `NzRes` · `NzFltr` · `NzFltFrq` · `Resnc` |

Every one of the original's 24 knobs is live on this family (`tools/brachetti_equiv/dead_params.py` measured each across its whole range), so none was removed.

`Partls` sets the mode count around the preset's own (positions 0-4) and displays the count it gives the current preset (`4 md`).

What each knob does in this unit:

| Knob | In this unit |
|---|---|
| `Pitch` | semitones from the preset's root — moves the modal head (and RackTom's swept boom) |
| `Poly` | voice cap 1-4; same-note strokes inside 80 ms fuse (DeepBs: 300 ms) |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `MlltRes` | modal presence (how loud the head rings over the strike) |
| `MlltStif` | strike brightness (upper-mode tilt) |
| `VlMllRes` | velocity-weighted SLAP prominence (raises a hand-slap burst where a preset has none) |
| `VlMllStf` | slap sharpness (brighter + shorter up, rounder + longer down) |
| `Partls` | mode count around the preset's own (the display shows the count) |
| `Model` | modal ratio template (membrane, drumhead, plate, bar, …) |
| `Dkay` | ring length (coarse) |
| `Mterl` | upper-mode material damping (metal sustains overtones, wood damps them) |
| `HitPos` | strike position: rim feeds the upper modes, centre the fundamental |
| `Rel` | ring length (fine) |
| `Inharm` | overtone spread around the fundamental |
| `TubRad` | whole-body ring length and boom amount |

`Tone` (tilt EQ), `Cutoff` / `Resnc` (low-pass and its Q) and `Gain` (drive)
are the master stage.  `Pitch`, `Poly`, `Velocity`, `VlMllRes`, `Tone`,
`Cutoff` and `Resnc` are performance knobs the host owns: a Program change
leaves them where they are.  Every other knob loads with the preset, and the
knob mappings are REFERENCE-ANCHORED on the shipped values — a preset plays its
calibrated sound at its own knob positions and only movement alters it.

## How the added presets were voiced

[`tools/brachetti_equiv/match_samples.py`](../tools/brachetti_equiv/match_samples.py)
with [`match_specs.py`](../tools/brachetti_equiv/match_specs.py) (one spec per preset):

- **The modal bank is measured, not searched.**  Each sample's partials
  (frequency, level, T60) are the bank's modes.  Mode 1 sounds at the root, an
  integer note, so where a sample's main partial falls between notes mode 1 is
  a weak partner just below it and the partial itself is mode 2 at its exact
  ratio (CngOpen: 261.6 Hz under 271 Hz).  Each env is the measured level
  divided by the bank's fixed output weight for that mode.
- **The attack is searched.**  The noise burst, mallet, slap, onset, modal
  level, the first two T60s and, on the gliding presets, the glide's depth and
  time are walked downhill against the sample: energy over time (six windows
  from 0-10 ms to 1-3 s), the spectral balance of the first 300 ms in six
  bands, the 40 dB decay time, the strongest partial and, on a glide, the
  pitch track.  What is compared is the VOICE before the master limiter
  (`preset_probe match`): the limiter is the family's gain staging, the same
  on every preset, not voicing.
- **Trims** are calibrated as the original's: body peak over [10 ms, 1 s] at
  velocity 127 on the root, on a body target of 1.5 (`preset_probe calib`
  reprints the shipped table).

Measured, preset vs sample (velocity 110, sequencer gate, before the limiter):
every added preset matches its sample's pitch (strongest partial within 2 %,
except where the sample's strongest partial is the start of a glide), and 23 of
the 25 match its 40 dB decay within 15 %.  What six modes cannot hold is the
dense upper mode field of a membrane slap: CngMute, CngSlap, BngMach, Tambrim
and DrbTek put 30-60 % less of their attack in 1-3 kHz than their samples.  The
talking-drum glides are exponential where the drummer's are S-shaped, so they
start moving sooner.

Two table-driven layers were added for them, both 0 on the original seven
(which keep their own code paths and render as before):
- `slap_*` in `kVoicing`: a band-passed noise slap from the table instead of a
  per-preset branch;
- `boom_hz` / `glide`: a **tracked** boom that follows the played note from the
  preset's root and, with a pitch envelope, glides down onto its frequency
  (`glide` +1: a hard hit settling) or up to it (−1: the talking drum, the
  udu, the tabla bayan under the heel).

## Build

```
build drumlogue/brachetti_tom
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_tom.drmlgunit`; copy it to the drumlogue's `Units/Synths`
folder.  Not yet tested on hardware.

## Verification

Equivalence with the original, bit-for-bit on the host:

```
cd ../tools/brachetti_equiv && ./run_equiv.sh ../../brachetti_tom
```

and the same comparison cross-built for the Cortex-A7 with NEON and
`-ffast-math`, run under qemu (float-rounding-level differences expected):

```
cd ../tools/brachetti_equiv && ./arm_equiv.sh ../../brachetti_tom --fuzz 20 --knob-points 1
```

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their membrane modal configs, each preset's voicing (boom, slap, chirp, onset) and output trim |
| `voice.h` | one voice: the exciter, the membrane modal bank, the boom and its pitch envelope, the stick / hand slap, the chirp and the onset ramp |
| `synth_engine.h` | the tom on the shared modal engine: the strike recipe (roll fusion, the slap), the tom's knob mappings and the per-sample voice |

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

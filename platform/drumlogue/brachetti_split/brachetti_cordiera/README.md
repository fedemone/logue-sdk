# Brachetti Cordiera (`BrcCordiera`)

> **Disclaimer:** Brachetti Cordiera is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../../README.md#disclaimer) for full terms.

The eleventh unit of the [Brachetti family](../../tools/brachetti_equiv/README.md):
instruments whose voice is a **wire** — *cordiera* is Italian for a drum's
snare wire.  Two engines, each from a family unit, each with what Miguel
Moreno's *Realistic drums with karplus-strong synthesis* taught this family
(see [BrcLippa](../brachetti_lippa/README.md)):

- **Wires on a head** — BrcSnare's engine (a modal head, three snare-wire
  resonators, the noise burst), its wires also driven by the head's motion,
  as the article's snares follow the drum, in white noise or the sparse
  crackle of Moreno's mymembrane~: they rattle as long as, and as hard as,
  the head moves.
- **A struck or plucked wire** — BrcWorld's berimbau engine (a Karplus-Strong
  wire under a moving gourd resonance), made stiff by first-order all-passes
  in its loop (Van Duyne & Smith 1994), tuned through its filters, and able
  to bend its pitch.

Its instruments are its own: none of them is in another Brachetti unit.  Its
engines were first checked bit-identical to BrcSnare's and BrcWorld's, on
those units' own presets, with the two additions off.

| | |
|---|---|
| Unit name on the drumlogue | `BrcCordiera` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724364` ('BrCd') |
| Engine | modal head + snare-wire resonators driven by the head; stiff Karplus-Strong wire + gourd |
| Presets | 7 |
| Knobs | 24 (BrcSnare's layout) |
| ARM size (cross-built, `arm_size.sh`) | .text 23172 · .rodata 2652 · .data.rel.ro 76 · .data 4 · .bss 36048 |
| CPU (`arm_cpu.sh`) | 1072.1 ARM instructions per rendered frame (an instruction count, for comparing this unit with itself) |

## Presets

| # | Name | Root | Instrument | Engine |
|---|---|---|---|---|
| 0 | Tabor | 50 | **tabor**: a deep two-headed drum, a gut snare across the head it is struck on, buzzing with every stroke | wires on a head |
| 1 | Daf | 38 | **daf**: a large frame drum, metal rings along the inside of its frame rattling on the head's motion | wires on a head |
| 2 | MbiraBz | 55 | **mbira with buzzers**: a steel tine over a soundboard, bottle caps rattling while it sings | wires on a head |
| 3 | TomBuzz | 45 | **tom with the snares on**: a kit tom ringing, the snare drum beside it buzzing in sympathy | wires on a head |
| 4 | Marranz | 45 | **marranzano**: the Sicilian jaw harp, a steel reed plucked before the mouth, its formant swept | wire |
| 5 | Spring | 26 | **spring drum** (thunder drum): a long steel spring hanging from a drum head, its dispersion a falling chirp | stiff wire |
| 6 | Gopichd | 50 | **gopichand**: a steel string from a skin head to a split bamboo neck, squeezed to bend the pitch down and back | stiff wire |

## The engines

### Wires on a head

BrcSnare's voice: the mallet and the noise burst, a modal head (here each
instrument's own: a membrane's modes for the tabor, the daf and the tom, a
cantilever tine's 1 : 6.27 : 17.55 for the mbira), the boom where a preset has
one (the tom's), and three two-pole wire bands on the noise.  To it,
`HeadDrive` (`voice.h`): a follower on the head — the modal bank and the boom,
as they enter the voice — with a 1 ms attack and the preset's release
(`head_rel_ms`) sets the level of a noise added to the three bands' input,
through the burst and after it.  The noise is white blended towards
`brc::crackle` (`head_crackle`): uniform noise to the 50th power, mymembrane~'s
snare noise (`noise~` → `pow~ 50`), a sparse rattle where white noise is a
hiss.  `head_follow` is the drive's level.  The wires' lowest band, which
BrcSnare fixes per preset, is a table value here (`band_a_hz`): 1.8 kHz for the
tabor's gut snare, 3.5 kHz for the daf's rings, 2.4 kHz for the mbira's caps.

### The stiff wire

BrcWorld's `Wire` (`cordiera::Wire` in `voice.h`) with `stages` first-order
all-passes (coefficient `stiff`) between the loss filter and the line:
`v = u + a·v[n−1]`, `y = −a·v + v[n−1]`.  A first-order all-pass delays the
low frequencies more than the high ones, so each higher partial comes round
the loop a little sooner: sharp, as a stiff string's are (Van Duyne & Smith).
With many stages the same effect is a spring's dispersion: every echo arrives
highs first, a falling chirp.  The line gives back the all-passes' delay at
the fundamental and the loss filter's, so the wire is in tune (within 0.2
cents of the note from its root to two octaves up; BrcWorld's own wire, which
leaves the loss filter's delay in, plays 8 cents flat at its root).  If the
all-passes would take more than half the period, the coefficient backs off.
Up to 16 stages; a line of 2048 samples (down to 23.4 Hz: the spring's loop).

The pitch bend (`bw_bend_up`, `bw_bend_down`) rides the gourd's two curves: the
delay × 1 / (1 + up·(1 − curve₁) − down·(1 − curve₂)), the first curve over
`gourd_up_ms`, the second after `gourd_down_hold_ms` over `gourd_down_ms`.  On
the gopichand, −0.16 and −0.16: the neck squeezed drops the pitch three
semitones, let go it comes back.

### Snare kinds

BrcSnare's engine keeps its per-preset branches (AcSnare's swept boom,
MrchSnr's and RimShot's band A, BrshSnr's brush) behind a table value,
`snare_kind`; every Cordiera instrument is kind 0, the generic snare.

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all twelve Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**: BrcSnare's layout and names.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `Buzz` · `BuzzTon` · `Crack` · `Tight` |
| 4 | dedicated | `Punch` · `Model` · `Dkay` · `BuzzLen` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

Every mapping is REFERENCE-ANCHORED (exactly the shipped sound at the shipped
value).  `Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff`, `Resnc` and `Crack` are
host-owned: a Program change leaves them where they are.

| Knob | On the instruments with wires on a head | On the wire instruments |
|---|---|---|
| `Buzz` (MlltRes) | the rattle's amount, and the head drive's | presence: the wire's level |
| `BuzzTon` (MlltStif) | the wire bands' brightness | density: the stick's hardness (a shorter burst) |
| `Crack` (VlMllRes) | the crack before the wires engage | the wire's dry contact sound beside the gourd |
| `Tight` (VlMllStf) | the wires' Q, loose ↔ zingy | **the stiffness** (on 1 − the all-passes' coefficient, ×¼ .. ×4) |
| `Punch` | the transient shaper (`brc::Punch`, shared by all twelve units) | the same |
| `Model`, `Mterl`, `HitPos`, `Inharm` | the modal head, as on BrcSnare | `Mterl` brightness, `Inharm` the bend (the gourd's sweep and the pitch bend) |
| `Dkay` | the head's ring | the wire's ring and the gourd's timing |
| `BuzzLen` (Rel) | the buzz tail, and the head drive's release | the wire's fine ring length |
| `TubRad` | the shell depth (the low wire band) | the body: the gourd lower |
| `NzMix`, `NzRes`, `NzFltr`, `NzFltFrq` | the noise burst that drives the wires | the exciter's noise (barely used) |

Measured with [`knob_liveness.py`](../../tools/brachetti_equiv/knob_liveness.py)
(each knob to both ends, mean dB change over bands and time):

```
=== brachetti_cordiera   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)
           Pitch    Poly Velocit    Buzz BuzzTon   Crack   Tight   Punch   Model    Dkay BuzzLen   Mterl  HitPos  Inharm  TubRad   NzMix   NzRes  NzFltr NzFltFr
Tabor        5.7       .    15.6    10.6     3.2    :0.6     2.9     7.7    :1.0     9.9    21.9    22.6     3.9    :0.8    10.6    :0.6     7.6    :1.0    :0.9
Daf          8.9       .    18.1    16.1     5.8    :0.4     4.2     8.8     2.2     8.5    18.1    19.9     4.8     2.0    13.1       .     1.6    :0.4    :0.4
MbiraBz      6.9       .    25.5    29.6     5.1    :0.3     2.3    12.2    :0.4    28.0    14.5     3.4    15.7    :0.4     9.4       .       .       .       .
TomBuzz     10.0       .    16.4    14.9     4.3    :0.6     1.9     8.6     4.1    13.1    18.3    20.1     4.6     3.7    14.2    :1.1    :0.9       .       .
Marranz      9.6     3.5    12.4    14.2     4.4     7.8       .    10.6       .    26.0    11.1     2.7       .     3.5     2.7       .       .       .       .
Spring       5.7    :0.8    11.7    13.2     3.7     7.6     1.6     9.7       .    19.4     6.9     6.9       .       .     1.9       .    :0.5       .       .
Gopichd     12.0     1.5    10.5    12.0     3.8     5.6       .     8.5       .    21.2    10.5     7.7       .     3.2     2.5       .    :0.7       .       .
```

`Tight` reads as nothing on Marranz (it has no all-passes: a jaw harp's reed
is harmonic) and on Gopichd, whose single all-pass moves partials in pitch,
which band energies barely see.  The noise knobs are small where an
instrument's noise burst is (the daf's palm, the mbira's thumb, the tom's
sympathetic snares are mostly the head drive).

## How the instruments were voiced

There is no recording of these instruments among the family's references
(the Pelle e Legno kit, the original's samples).  A freely licensed recording
of a Sicilian jaw harp exists on Wikimedia Commons
(["Klangdemonstration einer sizilianischen Maultrommel"](https://commons.wikimedia.org/wiki/File:Klangdemonstration_einer_sizilianischen_Maultrommel.wav),
DrTrumpet, CC BY-SA 4.0), but Wikimedia refused this environment's downloads
(HTTP 429) for the length of the session, so Marranz is not yet voiced to
it — the first thing to do when it can be fetched.

So the instruments were set from their physical descriptions and then checked
against measured targets, the targets taken from the references that do
exist:

- **The wires on a head** — the drive set on two measures the voicing tools
  have: the share of the energy above 2 kHz over 50-300 ms (the buzz), and the
  kurtosis of the 2-8 kHz band (how impulsive it is: 3 for a hiss).  Measured
  on real instruments: snare drums 4-8 % and 3.5-4.5 (acoustic-snare.wav,
  snare heavy.wav, the marching snare), the bendir's gut snares 1 % and 9.7,
  the pandeiro's jingles 10 % and 3.3, the cajón's wires 29 % and 3.3.
- **The wire instruments** — on spectrograms and the pitch track: the jaw
  harp's harmonic buzz under a formant swept 0.9 → 1.6 → 1 kHz; the spring's
  echoes and their chirp; the gopichand's bend.

Measured (velocity 110):

| Preset | above 2 kHz | kurtosis | t40, ms | strongest partial | | target, from |
|---|---:|---:|---:|---:|---|---|
| Tabor | 8 % | 6.7 | 235 | 147 Hz | | gut snare: between a snare drum and the bendir |
| Daf | 9 % | 5.1 | 375 | 74 Hz | | rings: as the pandeiro's jingles, more impulsive |
| MbiraBz | 10 % | 5.7 | 1200 | 196 Hz | | the buzz while the tine sings (the kalimba alone: 4 %, 1.7) |
| TomBuzz | 3 % | 4.0 | 400 | 110 Hz | | sympathetic: under a struck snare's 4-8 % |
| Marranz | — | — | 1050 | 992 Hz (the formant) | | a 110 Hz harmonic buzz under the sweep |
| Spring | — | — | 1935 | 147 Hz | | echoes every 26.9 ms (the 36.7 Hz loop), each highs first; periodicity 0.09 |
| Gopichd | — | — | — | 146.9 Hz | | −2.5 st at 0.1 s, −3.0 at 0.2 s, back by 0.45 s |

They are a starting point, not a measured match: listen, and move them (the
search tools take a reference as soon as there is one).  The generator that
wrote the tables from the descriptions is not part of the unit; the tables are
the source.

## Build

```
build --drumlogue drumlogue/brachetti_split/brachetti_cordiera
```

(`--drumlogue` because the unit sits in `brachetti_split/`; `build drumlogue/brachetti_split`
builds all twelve family units, and the [`brachetti`](../../brachetti/README.md) unit plays
this family as one of its twelve presets.)

(see [the drumlogue README](../../README.md) for the Docker build environment).
The product is `brachetti_cordiera.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  Not yet tested on hardware.  An earlier development
build, `brachetti_filo.drmlgunit` (BrcFilo), is not part of the family: if it
is on the drumlogue, delete it from `Units/Synths`.

## Verification

- **The engines** were checked bit-identical to BrcSnare's and BrcWorld's,
  with the additions off, on those units' presets (commit `e7eeca5`); the
  changes since add table values those presets did not use (the wires' low
  band, the pitch bend, more stages, a longer line).
- **Stability**: a fuzz of 3000 random sessions (every preset, every knob
  anywhere in its range, random notes, velocities and stacked strikes):
  every output sample finite and within ±0.99.
- **Tuning** of the stiff wire: within 0.2 cents of the note from the root to
  two octaves up.
- `multi_load.sh` (all twelve units, 512,206 display strings identical alone
  and together, unit ids distinct), `lib_uses.py --check`.

## Code

| File | What it holds |
|---|---|
| `presets.h` | the instruments: knob rows, modal heads, voicing (BrcSnare's columns, then the head drive, the wire, the bend), trims |
| `voice.h` | one voice: BrcSnare's, plus `HeadDrive` and `cordiera::Wire` |
| `synth_engine.h` | the two strikes and renders (BrcSnare's with the head drive; BrcWorld's berimbau with the stiffness and the bend); the wires' delay lines |

## Credits

- **Miguel Moreno**: the snares that follow the drum, from
  [*Realistic drums with karplus-strong synthesis*](https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/)
  (2022), and the crackle, from his `mymembrane~` in
  [pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr) (BSD-3-Clause; the
  notice is in [BrcLippa's README](../brachetti_lippa/README.md#credits-and-sources)).
- **Scott A. Van Duyne and Julius O. Smith III**, "A Simplified Approach to
  Modeling Dispersion Caused by Stiffness in Strings and Plates", *Proc.
  International Computer Music Conference*, 1994 — all-passes in the loop for
  a stiff string, and many for a spring.
- **David A. Jaffe and Julius O. Smith**, "Extensions of the Karplus-Strong
  Plucked-String Algorithm", *Computer Music Journal* 7(2), 1983 — tuning a
  loop through its filters' phase delays.
- BrcSnare's and BrcWorld's own sources, in their READMEs.

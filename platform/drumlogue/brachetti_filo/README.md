# Brachetti Filo (`BrcFilo`)

> **Disclaimer:** Brachetti Filo is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The eleventh unit of the [Brachetti family](../tools/brachetti_equiv/README.md):
**the wires** (*filo* is Italian for wire).  It takes two improvements that came
out of Miguel Moreno's *Realistic drums with karplus-strong synthesis* (see
[BrcLippa](../brachetti_lippa/README.md)) to the two family units they would
change, without changing those units:

- **BrcSnare's eight snares, with the wires on the head.**  BrcSnare's wires
  ring on the exciter's noise burst, so they stop when the burst does.  Here a
  follower on the head's motion also drives them, as the article's (and its
  Pd patch's) snares follow the drum: the wires rattle as long as, and as hard
  as, the head moves.
- **BrcWorld's two berimbaus, with a stiff wire.**  The Pelle e Legno
  berimbau samples' partials run sharp — +17 cents at the 8th, +46 at the 12th:
  a steel wire's stiffness — and BrcWorld's Karplus-Strong wire is harmonic.
  Here a cascade of first-order all-passes in the loop sharpens them.

BrcSnare is bit-identical to the original `brachetti`, and BrcWorld's presets
are checked against their own renders; either change would have broken that.
So both live here, in a family of their own, and the two units stay as they
are.

| | |
|---|---|
| Unit name on the drumlogue | `BrcFilo` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272466C` ('BrFl') |
| Engine | BrcSnare's modal head + snare-wire resonators, the wires driven by the head; BrcWorld's wire + gourd, the wire stiff |
| Presets | 10 (BrcSnare's 8, BrcWorld's 2) |
| Knobs | 24 (BrcSnare's layout) |
| ARM size (cross-built, `arm_size.sh`) | .text 22972 · .rodata 3468 · .data.rel.ro 88 · .data 4 · .bss 19440 |
| CPU (`arm_cpu.sh`) | 1077.0 ARM instructions per rendered frame (an instruction count, for comparing this unit with itself) |

## Presets

| # | Name | Root | From | What changed |
|---|---|---|---|---|
| 0 | AcSnre | 38 | BrcSnare 0 | the wires on the head |
| 1 | MrchSnr | 65 | BrcSnare 1 | the wires on the head, part crackle |
| 2 | BrshSnr | 55 | BrcSnare 2 | the wires on the head |
| 3 | RimShot | 69 | BrcSnare 3 | the wires on the head (a short follow) |
| 4 | BndDoum | 41 | BrcSnare 4 | the gut snares on the head (lightly) |
| 5 | BndTek | 66 | BrcSnare 5 | the gut snares on the head (lightly) |
| 6 | CajBass | 40 | BrcSnare 6 | the wires on the tapa |
| 7 | CajSlap | 65 | BrcSnare 7 | the wires on the tapa (lightly) |
| 8 | Berimbu | 46 | BrcWorld 1 | the wire stiff (two all-passes at 0.869); tuned through its loss filter |
| 9 | BerimHi | 48 | BrcWorld 2 | the wire stiff (one all-pass at 0.900); tuned through its loss filter |

Everything else in each preset — its rows, its modal bank, its knob mappings,
its code paths — is BrcSnare's or BrcWorld's, as copied (see Verification).

## What this unit adds

### The wires on the head

`HeadDrive` in `voice.h`.  A follower on the head — the modal bank and the
boom, as they enter the voice — with a 1 ms attack and the preset's release
(`head_rel_ms`) sets the level of a noise that is added to the three wire
bands' input, through the burst and after it.  The noise is white, blended
towards `brc::crackle` (`head_crackle`): uniform noise to the 50th power, a
sparse rattle where white noise is a hiss — the snares of mymembrane~, Miguel
Moreno's Pd instrument, whose noise is `noise~` through `pow~ 50`.  It has its
own generator, so the noise burst's sequence is BrcSnare's.  `head_follow` is
the drive's level; at 0 the voice runs BrcSnare's arithmetic exactly
(`process()`, not `process_driven()`).

Knobs: `Buzz` scales the drive as it scales the rattle; `BuzzLen` the
follower's release as it does the buzz tail (both anchored, exactly 1 at the
shipped value).

### The stiff wire

`filo::Wire` in `voice.h` is BrcWorld's `Wire` with `stages` first-order
all-passes (coefficient `stiff`) between the loss filter and the line:
`v = u + a·v[n−1]`, `y = −a·v + v[n−1]`.  A first-order all-pass delays the
low frequencies more than the high ones, so in the loop each higher partial
comes round a little sooner, i.e. sharp — Van Duyne & Smith's model of a stiff
string.  The line gives back the all-passes' delay at the fundamental, and the
loss filter's (BrcWorld's wire leaves that in: it plays 8 cents flat at its
root and a third of a tone flat three octaves up); here the wire is in tune
within 0.2 cents from the root to two octaves above it.  If the all-passes
would take more than half the period (a high note, a stiff setting), the
coefficient backs off.

`Tight` (BrcSnare's wire tightness, the VlMllStf role) scales the stiffness on
the berimbaus: it acts on 1 − a, since the stretch grows as 1 / (1 − a), ×¼ to
×4 (coefficient up to 0.97).  At −100 the wire is BrcWorld's harmonic one, at
+100 very stiff.  On the snares it is BrcSnare's `Tight`.

### Snare kinds

BrcSnare tests the preset index for AcSnare's swept boom, MrchSnr's and
RimShot's band A and BrshSnr's brush.  Here the table says it (`snare_kind`:
1 AcSnre, 2 MrchSnr, 3 BrshSnr, 4 RimShot, 0 none), so a preset the voicing
search appends takes the branches of the one it was copied from.  The real
presets take the same branches as before.

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all eleven Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**: BrcSnare's, with the same names and meanings on the snares ([BrcSnare's README](../brachetti_snare/README.md#knobs)).

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `Buzz` · `BuzzTon` · `Crack` · `Tight` |
| 4 | dedicated | `Punch` · `Model` · `Dkay` · `BuzzLen` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

On the snares, two knobs also reach the new layer: `Buzz` (the drive's level)
and `BuzzLen` (its release).  On the berimbaus the knobs keep BrcWorld's
meanings in their slots ([BrcWorld's README](../brachetti_world/README.md#knobs)):
`Buzz` is presence, `BuzzTon` density, `Crack` BrcWorld's `Rattle`, `Dkay` and
`BuzzLen` length, `Mterl` brightness, `Inharm` the gourd's bend, `TubRad` the
body; and `Tight` the stiffness.  One difference: `Crack` is host-owned, as on
BrcSnare (a Program change leaves it where it is), where BrcWorld's `Rattle`
loads with the preset.

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py):

```
=== brachetti_filo   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)
           Pitch    Poly Velocit    Buzz BuzzTon   Crack   Tight   Punch   Model    Dkay BuzzLen   Mterl  HitPos  Inharm  TubRad   NzMix   NzRes  NzFltr NzFltFr
AcSnre      :1.1       .    15.2    13.2     3.8    :0.4     1.9     6.5    :1.0     2.5    23.5    10.3     1.7    :0.9     5.6     1.6    10.4     5.3     4.7
MrchSnr      2.1       .    18.1    10.5     7.9    :0.4     2.3     7.8       .     4.7    27.7    :0.7     3.9    :1.5     2.7    :0.9    11.4     1.6    :1.3
BrshSnr     :1.1       .    14.9    19.5     4.8     2.1     1.7     9.5       .     7.3    23.6    17.6     2.7    :0.8     4.0     2.1     9.5     5.8     2.5
RimShot      1.8       .     7.8     5.5     3.7       .     5.3     2.7    :0.7     6.2    24.9    22.7     4.9    :0.9     8.9    :1.1    28.3    :0.5       .
BndDoum     11.7       .    12.0    13.9     6.0    :0.9     3.9     7.4     4.0    11.5    19.7    25.5     6.2     3.9    11.6     1.8    10.2     3.4     2.7
BndTek       9.7       .     7.2    13.9     3.8    :0.9     2.9     4.7     8.5     5.0    16.6    24.0     2.0     3.4     9.4     2.2    14.3     2.2    :1.4
CajBass      7.5       .     9.9    18.4     5.0    :0.9     3.2     6.0     4.8     8.7    16.9    23.3     4.0     2.4    10.4     2.8     4.7    :0.7    :0.5
CajSlap      8.1       .    10.4     9.6     2.7    :1.4     2.8     6.4     2.3     5.2    21.3    24.6     2.3     1.7     7.6     6.6    14.3    10.9     9.6
Berimbu      8.8    :0.5     9.0     9.7     3.9     6.4     1.7     6.5       .    14.7    16.1     2.9       .     5.5     3.9    :0.5     1.9       .       .
BerimHi      8.5    :0.6     8.3     9.3     3.6     5.6    :1.3     5.9       .    14.6    10.0     2.7       .     4.8     3.4       .       .       .       .
```

`Tight` reads small on the berimbaus because it moves partials in pitch, which
band energies barely see; measured on the partials instead, Berimbu's 6th
partial sits −2, +10 and +70 cents from harmonic at `Tight` −100, 0 and +50.

## How the new layers were voiced

With [`match_samples.py`](../tools/brachetti_equiv/match_samples.py) and the
`filo` specs of [`match_specs.py`](../tools/brachetti_equiv/match_specs.py),
against the references the presets were voiced to (the original's recordings
for the four classic snares, the Pelle e Legno samples for the rest).  Each
spec starts from the preset's own rows, read from `presets.h`, and frees only
the new values: `head_follow`, `head_rel_ms`, `head_crackle` on the snares,
`bw_stiff`, `bw_stages` on the berimbaus.  Two features came with it, both
opt-in per spec:

- **`sparsity`**: the kurtosis of the 2-8 kHz band in 20 ms frames — how
  impulsive the rattle is (Gaussian noise reads 3).  Band energies cannot tell
  a crackle from a hiss through the same band-pass.
- **`stretch`**: the cents offset of partials 2-12 from k times the
  fundamental.  Nothing else in the score sees a stiff wire.

The snares also compare each band's decay (`late`).  The stiffness's score is
narrow (two stages at 0.80 and at 0.90 both miss what 0.87 matches), so the
berimbaus start from the best of a grid of 2-4 stages × 0.5-0.9.  Trims by the
family's body-target method (`preset_probe calib`).

Measured, new layer off → on (velocity 110; the score includes the opt-in
features, lower is better):

| Preset | score | | measured | |
|---|---:|---|---|---|
| AcSnre | 2.813 → 2.187 | | kurtosis 3.05 → 3.05 (rec. 3.45) | t40 225 → 185 ms (rec. 225) |
| MrchSnr | 3.433 → 2.855 | | kurtosis 2.83 → 3.15 (rec. 4.53) | t40 215 → 220 ms (rec. 215) |
| BrshSnr | 1.465 → 1.239 | | kurtosis 2.86 → 2.82 (rec. 3.18) | t40 445 → 320 ms (rec. 285) |
| RimShot | 1.581 → 1.336 | | — | t40 40 → 50 ms (rec. 45) |
| BndDoum | 1.749 → 1.689 | | kurtosis 3.06 → 3.18 (rec. 9.71) | t40 235 → 235 ms (rec. 240) |
| BndTek | 0.619 → 0.578 | | kurtosis 3.24 → 3.02 (rec. 3.02) | t40 175 ms (rec. 180) |
| CajBass | 1.202 → 0.988 | | kurtosis 2.93 → 3.62 (rec. 4.30) | t40 180 ms (rec. 145) |
| CajSlap | 0.456 → 0.448 | | kurtosis 3.05 → 3.05 (rec. 3.25) | t40 140 ms (rec. 150) |
| Berimbu | 1.185 → 0.895 | | stretch off by 17.5 → 3.4 cents (mean, partials 2-12) | 12th partial +0 → +37 cents (sample +46) |
| BerimHi | 1.209 → 0.858 | | stretch off by 28.0 → 5.2 cents | 12th partial −1 → +40 cents (sample +52) |

Every preset scores better.  The berimbaus are the clear win: the stretch the
samples have, which BrcWorld's wire cannot make.  On the snares the gain is
real but modest, and one thing did not come: the bendir's gut snares are very
impulsive in the recording (kurtosis 9.7), and the head drive here barely
moves it — the drive goes through BrcSnare's wire resonators, whose ringing
smooths the crackle's spikes out.  BrcLippa's snares, a band-pass straight on
the crackle as in mymembrane~, do reach it (its Bendir measures 9.71).  RimShot
is too short for the measure.  The trims of the classic snares dropped (AcSnre
0.236 → 0.076): the drive adds sustained wire energy, and the trim keeps each
preset's body on the family's target.

## Build

```
build drumlogue/brachetti_filo
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_filo.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  Not yet tested on hardware.

## Verification

**The copy.**  Built with `-DBRC_FILO_AS_COPIED`, the new layers are off and
the presets play the trims they were copied with (`kCopiedTrim`); the unit is
then BrcSnare's eight snares and BrcWorld's two berimbaus, and
[`run_equiv.sh`](../tools/brachetti_equiv/run_equiv.sh) checks it against each,
and against the original:

```
cd ../tools/brachetti_equiv
EQUIV_CXXFLAGS=-DBRC_FILO_AS_COPIED BRACHETTI_ORIG=$PWD/../../brachetti_snare ./run_equiv.sh ../../brachetti_filo
EQUIV_CXXFLAGS=-DBRC_FILO_AS_COPIED BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti ./run_equiv.sh ../../brachetti_filo
EQUIV_CXXFLAGS=-DBRC_FILO_AS_COPIED EQUIV_ALIASES_FILE=world_aliases.txt BRACHETTI_ORIG=$PWD/../../brachetti_world ./run_equiv.sh ../../brachetti_filo
```

with `world_aliases.txt` holding `brachetti_filo Buzz=MlltRes`,
`brachetti_filo BuzzTon=MlltStif` and `brachetti_filo Tight=VlMllStf` (the
knobs' BrcWorld names).  Results: against BrcSnare IDENTICAL (1,610 scenarios,
3,916 renders); against the original IDENTICAL on its four snares (940, 2,578,
the same as BrcSnare's own check); against BrcWorld IDENTICAL on the
berimbaus (531 scenarios, 1,800 renders, and two more seeds with 300 random
sessions each: 712 and 715 scenarios, 3,146 and 3,247 renders).  Against
BrcWorld `Crack` is left out: as BrcWorld's `Rattle` it maps identically while
a preset plays (checked with it aliased: every knob scenario identical), but
it is host-owned here, so after a Program change it keeps its value where
`Rattle` resets.

**The new layers.**  A fuzz of 3,000 random sessions (every preset, every
knob anywhere in its range, random notes, velocities and stacked strikes):
every output sample finite and within ±0.99, the wire's state bounded.  The
berimbau's pitch: within 0.2 cents of the note from the root to two octaves
up.  `multi_load.sh`: eleven units, 503,100 display strings identical alone
and together, unit ids distinct.  `lib_uses.py --check`.

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets: BrcSnare's and BrcWorld's rows, the new columns, the snare kinds, the trims (and `kCopiedTrim`) |
| `voice.h` | one voice: BrcSnare's, plus `HeadDrive` and `filo::Wire` |
| `synth_engine.h` | BrcSnare's strike and render, the head drive's strike; BrcWorld's berimbau strike and render with the stiffness; the wires' delay lines |

## Credits

- **Miguel Moreno**: the snares that follow the drum, from
  [*Realistic drums with karplus-strong synthesis*](https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/)
  (2022), and the crackle, from his `mymembrane~` in
  [pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr) (BSD-3-Clause; the
  notice is in [BrcLippa's README](../brachetti_lippa/README.md#credits-and-sources)).
- **Scott A. Van Duyne and Julius O. Smith III**, "A Simplified Approach to
  Modeling Dispersion Caused by Stiffness in Strings and Plates", *Proc.
  International Computer Music Conference*, 1994 — all-passes in the loop for
  a stiff string.
- **David A. Jaffe and Julius O. Smith**, "Extensions of the Karplus-Strong
  Plucked-String Algorithm", *Computer Music Journal* 7(2), 1983 — tuning a
  loop through its filters' phase delays.
- BrcSnare's and BrcWorld's own sources, in their READMEs.

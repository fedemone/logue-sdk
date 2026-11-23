# Brachetti Pizzico (`BrcPizzico`)

> **Disclaimer:** Brachetti Pizzico is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../../README.md#disclaimer) for full terms.

The twelfth unit of the [Brachetti family](../../tools/brachetti_equiv/README.md):
five **plucked strings** from a Karplus-Strong delay loop (*pizzico*: a
pluck, the root of *pizzicato*).  The loop is [BrcLippa](../brachetti_lippa/README.md)'s
loop drum, unchanged: a delay line, a loss filter designed from two decay
times, a Schroeder all-pass, a DC blocker, a sine-cycle-and-noise excitation
and a tension glide.  In BrcLippa the all-pass pulls the loop's partials off
the harmonic series and the loop becomes a drum head; these five presets
were voiced there as drums, but they kept their partials close to harmonic
and rang as what Karplus and Strong built the loop for: a plucked string.
They sounded good as strings, so they are strings, in a unit of their own.

It runs on the family's shared modal engine (`brachetti_common/modal_synth.h`)
like BrcLippa: the same 24-knob layout, preset loading, voice life cycle,
Punch and master stage.

| | |
|---|---|
| Unit name on the drumlogue | `BrcPizzico` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272507A` ('BrPz') |
| Engine | Karplus-Strong loop + Schroeder all-pass, tension glide (BrcLippa's loop drum) |
| Presets | 5 strings, each at the exact pitch of its root |
| Knobs | 24 (the modal units' layout, six slots the unit's own) |
| ARM size (cross-built, `arm_size.sh`) | .text 19492 · .rodata 1508 · .data.rel.ro 68 · .data 4 · .bss 51824 |
| CPU (`arm_cpu.sh`) | 809.7 ARM instructions per rendered frame (an instruction count, for comparing this unit with itself) |

## Presets

| # | Name | Root | Sound | Voiced, in BrcLippa, after |
|---|---|---|---|---|
| 0 | Harp | 59 (B3) | harp: a gut string plucked; warm, half a second, a slight sag at the pluck | `Tom1-001-CloseRoom.wav` (a rack tom) |
| 1 | Oud | 52 (E3) | oud: a fretless lute's string; warm, a second, 20 % sharp on a hard pluck and settling | `Tom2-004-CloseRoom.wav` (a floor tom) |
| 2 | Fretls | 39 (D#2) | fretless bass: a low string thumbed hard, its pitch dropping from 35 % sharp into the note | `KickA-Hard-012-CloseRoom.wav` (a kick) |
| 3 | Guitar | 42 (F#2) | guitar: a low string; the brightest and the longest of the five | `Taiko-Hit.wav` (a taiko) |
| 4 | BassPiz | 34 (A#1) | double bass pizzicato: very dark, about 0.6 s | `027_SurdoOpen.wav` (a surdo; Pelle e Legno) |

Every note plays in equal temperament: the loop's first resonance at the
root and an octave up is within 1.3 cents of the note (Tuning, in BrcLippa's
README, has how the loop is tuned).  The names are only names: each is the
string a preset comes closest to, not a model of that instrument.

## Where the strings come from

BrcLippa's first presets were all voiced as drums, by measurement, against
recordings of drums: the original brachetti's reference recordings (its
`samples/` folder, on the `original_brachetti` branch) and the
[Pelle e Legno](../../pelle_legno/) samples ([`match_samples.py`](../../tools/brachetti_equiv/match_samples.py),
as BrcLippa's README, "How the presets were voiced", describes).  Three came
out as drums (BrcLippa's Snare, SnrHvy and Bendir).  Five, voiced after a
rack tom, a floor tom, a kick, a taiko and a surdo, rang like plucked
strings.  The measure is the periodicity of the ring — the largest
normalised autocorrelation of 50-350 ms of it at a lag of 1-25 ms
(`match_samples.periodicity`): a plucked string repeats itself every period,
a struck head does not.

| Preset (voiced after) | periodicity, preset / recording | score as voiced → at its note | t40, ms | strongest partial, Hz |
|---|---:|---:|---:|---:|
| Harp (rack tom) | 0.94 / 0.71 | 1.011 → 1.552 | 530 / 420 | 248 / 239 |
| Oud (floor tom) | 0.94 / 0.61 | 0.849 → 0.955 | 905 / 900 | 166 / 167 |
| Fretls (kick) | 0.87 / 0.23 | 0.832 → 0.941 | 420 / 360 | 79 / 79 |
| Guitar (taiko) | 0.94 / 0.20 | 0.890 → 0.995 | 1040 / 1080 | 93 / 89 |
| BassPiz (surdo) | 0.87 / 0.82 | 0.342 → 0.586 | 575 / 625 | 59 / 58 |

(The original's koto sample, a real plucked string, reads 0.85.  Velocity
110, the sequencer gate; the score is `match_samples.py`'s against the drum
recording, lower is closer.)

Miguel Moreno's own loop (`mymembrane~`, which BrcLippa ports) did not make
drums of them either: voiced again on it against the same recordings, their
periodicity came down only on two of the five, and every one matched its
recording worse (BrcLippa's README, "From drums to strings").  They sounded
good as they were, so they were kept as strings: each named for the string
it comes closest to, and its loop moved from the recording's pitch to the
exact pitch of its root — Harp +49 cents (from 240 Hz), Oud −13, Fretls −27,
Guitar +47, BassPiz +8 — so that every note plays in tune.  Nothing else
changed.  They were BrcLippa's presets 0, 1, 4, 5 and 7 (as strings from
commit `b1f547b`) and moved here byte for byte.

## The loop

The loop is BrcLippa's (`loop::Drum` in `voice.h`, verbatim; its strike
`strike_loop` in `synth_engine.h`, verbatim): BrcLippa's README, "The loop
drum" and "Tuning", has every part of it and its sources.  What the parts do
on a string:

- **The loss filter from two decay times** (Välimäki et al. 1996): the T60
  at the note and at 3 kHz, so the highs die in the time the preset says at
  every pitch, as a real string's do.
- **The all-pass** moves the partials a little off harmonic: a stiff string's
  stretch, or (`Inharm` up) a bell-like one.  Its gain ships between −0.05
  (Guitar) and 0.55 (Oud).
- **The excitation**: one sine cycle `exc_ratio` periods long plus filtered
  noise under a Hann window — the pluck.  `MlltStif` makes it shorter (a
  harder plectrum, brighter), and `HitPos` subtracts the same cycle later:
  the comb of the pluck position (Jaffe & Smith), towards the bridge thinner.
- **The glide**: both delays shortened at the pluck and relaxing (`bend`,
  `bend_ms`): the pitch starts sharp and settles, as a string's does when it
  is plucked hard.  `Bend` at its top starts a full-velocity pluck 1.65×
  (Harp) to 2.7× (Fretls) above the note.
- **The buzz** (BrcLippa's snares, `Buzz`): noise through a band-pass, at
  the level of the string's motion — a buzzing bridge.  No preset ships it;
  `Buzz` up adds one (3 kHz, Q 2).
- **Karplus & Strong's drum blend** (`Blend`): each trip round the loop flips
  sign at random — at 50 % the string becomes Karplus and Strong's own drum.
- **A restrike keeps the ring**: a voice plucked again while it rings keeps
  its delay lines, and the glide slews from where the old ring had it.

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all twelve Brachetti units.  Pages 3 onward hold this unit's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Bend` · `VlMllStf` |
| 4 | dedicated | `BendTm` · `Buzz` · `Dkay` · `Punch` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `ApDelay` |
| 6 | dedicated | `NzMix` · `Blend` · `BuzzTon` · `NzFltFrq` |

`Poly` (voice cap 1-4) is 2 by default, and also bounds a Program change: the
instrument ringing out gives its voices up as the new one is struck.

The common knobs are [`brachetti_common/common_knobs.h`](../../brachetti_common/common_knobs.h)
(`Pitch` is semitones from the preset's root: the loop follows the played
note, in tune).  Every dedicated mapping is REFERENCE-ANCHORED, as on every
Brachetti unit: exactly the identity at the preset's shipped value, so a
preset plays as voiced until a knob moves.  They are BrcLippa's, slot for
slot (its README, "Dedicated knobs", has their ranges), with the snares
renamed:

| Knob | On the string | Up turns it |
|---|---|---|
| `MlltRes` | the loop's level | louder |
| `MlltStif` | the pluck's cycle: shorter, brighter | harder plectrum |
| `Bend` *(own, VlMllRes' slot)* | the glide's depth; at its top it adds a 30 % glide where a preset has none | deeper |
| `VlMllStf` | velocity → a shorter cycle | brighter hard plucks |
| `BendTm` *(own, Partls' slot)* | the glide's time | slower |
| `Buzz` *(own, Model's slot)* | the buzz's level; at its top it adds one (3 kHz, Q 2) where a preset has none | louder |
| `Dkay` | the string's T60 | longer |
| `Punch` *(Rel's slot)* | the family's transient shaper (`brc::Punch`) | |
| `Mterl` | the T60 at 3 kHz against the note's | brighter, the highs ringing longer |
| `HitPos` | the pluck position's comb | towards the bridge, thinner |
| `Inharm` | the all-pass gain, an offset | further from harmonic |
| `ApDelay` *(TubRad's role)* | the all-pass delay against the period | a different spread |
| `NzMix` | the noise in the pluck | noisier |
| `Blend` *(own, NzRes' slot)* | Karplus & Strong's drum: the chance a trip flips sign | towards a drum |
| `BuzzTon` *(own, NzFltr's slot)* | the buzz's band, ± 1 octave | higher |
| `NzFltFrq` | the pluck noise's corner | brighter noise |

Measured with [`knob_liveness.py`](../../tools/brachetti_equiv/knob_liveness.py)
(each knob to both ends on every preset, mean dB change over bands and time):

```
=== brachetti_pizzico   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)
           Pitch    Poly Velocit MlltRes MlltSti    Bend VlMllSt  BendTm    Buzz    Dkay   Punch   Mterl  HitPos  Inharm ApDelay   NzMix   Blend BuzzTon NzFltFr
Harp        15.7       .     9.3    10.2     6.0     2.4     6.0       .     7.4    16.6     6.6     2.7     3.6     5.9    :1.5     6.2    17.8       .     4.8
Oud         18.7    :0.6    10.5    11.5     6.1     2.8     7.8    :1.4    10.8    20.2     7.7     4.6     6.5     3.5     3.8     5.4    22.2       .     3.1
Fretls      10.6       .    10.4    10.3     6.5     3.4     5.9    :1.4     7.8    14.8     6.5     3.3     2.3     5.9    :0.9     6.1    15.2       .     4.6
Guitar      17.0    :0.5    12.7    14.2     5.0     3.2     6.5    :0.8     6.5    27.5    10.6     2.1     3.8     4.9    :0.5     8.4    27.4       .    12.2
BassPiz     11.8       .     9.8    10.9     4.1     2.2     6.1       .    13.0    15.1     7.2     4.2     1.9     2.4     1.8     6.0    17.7       .     4.0
```

`BuzzTon` is silent until `Buzz` adds a buzz (no preset ships one), and
`BendTm` is small where a preset's glide is (raise `Bend` first).

## How the presets were voiced

As BrcLippa's: [`match_samples.py`](../../tools/brachetti_equiv/match_samples.py)
with the `pizzico` specs of [`match_specs.py`](../../tools/brachetti_equiv/match_specs.py).
They are `string_spec`s: voiced against the drum each preset began as, with
`lp_hz` pinned at the root's equal-tempered frequency, so a search cannot
detune a string.  The score is still against a drum, so a search would pull
a string back towards it: to make a string better, voice it by ear or
against a recording of a string, with a new spec.  The trims are the
family's body-target calibration (`preset_probe calib` reprints them).

```
BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti \
    python3 match_samples.py match_specs.py pizzico --report --state loop.json
```

## Build

```
build --drumlogue drumlogue/brachetti_split/brachetti_pizzico
```

(`--drumlogue` because the unit sits in `brachetti_split/`; `build drumlogue/brachetti_split`
builds all twelve family units, and the [`brachetti`](../../brachetti/README.md) unit plays
this family as one of its twelve presets.)

(see [the drumlogue README](../../README.md) for the Docker build environment).
The product is `brachetti_pizzico.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  Not yet tested on hardware.

## Verification

There is no original to compare with.  What was checked on the host:

- **The move**: every preset renders byte-identically to the same preset in
  BrcLippa before the move (velocities 110 and 60), and `match_samples.py
  --rewrite` rebuilds `presets.h` from the specs byte for byte.
- **Tuning**: each string's first resonance at its root and an octave up
  (Harp +0.5 / −0.2 cents, Oud −0.2 / −0.2, Fretls +0.8 / +0.8, Guitar −0.6 /
  −0.6, BassPiz −1.3 / −1.3).
- **Stability**: 3000 random sessions — every preset, every knob anywhere in
  its range, random notes and velocities, up to four stacked strikes,
  AllNoteOff — with every output sample finite and within the limiter's ±0.99.
- **Restrikes**: [`restrike_check.py`](../../tools/brachetti_equiv/restrike_check.py)
  (a second pluck on the voice still ringing, 30-600 ms after the first,
  against the same pluck on a silent voice plus the ring): at most 1.03×.
- **Units loaded together**: `multi_load.sh` (all twelve units, identical
  alone and together, no duplicated unit_id); `arm_size.sh` finds no unique
  symbol; `lib_uses.py --check`.

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, the unit's knob slots, each preset's loop settings and output trim |
| `voice.h` | one voice: the exciter, the modal bank and the loop (BrcLippa's `loop::Drum`, verbatim) |
| `synth_engine.h` | the strike (BrcLippa's `strike_loop`, verbatim) and the per-sample voice; the delay lines |

The loop is a copy of BrcLippa's, as BrcCordiera's engines are copies of
BrcSnare's and BrcWorld's: a unit holds its own engine, and only
[`brachetti_common`](../../brachetti_common/) is shared.  A fix to the loop in
one unit belongs in the other: diff `namespace loop` in the two `voice.h`.

## Credits and sources

- **Kevin Karplus and Alex Strong**, "Digital Synthesis of Plucked-String and
  Drum Timbres", *Computer Music Journal* 7(2), 1983 — the loop, and the drum
  blend (`Blend`).
- **David A. Jaffe and Julius O. Smith**, "Extensions of the Karplus-Strong
  Plucked-String Algorithm", *Computer Music Journal* 7(2), 1983 — the tuning
  by phase delay, the loss filter, the pluck-position comb (`HitPos`).
- **Vesa Välimäki, Jyri Huopaniemi, Matti Karjalainen and Zoltán Jánosy**,
  "Physical Modeling of Plucked String Instruments with Application to
  Real-Time Sound Synthesis", *Journal of the Audio Engineering Society*
  44(5), 1996 — the loss filter designed from decay times.
- **Julius O. Smith III**, *Physical Audio Signal Processing*, CCRMA,
  Stanford — "Extended Karplus-Strong Algorithm" and "Allpass from Two Combs".
- **Manfred R. Schroeder and Benjamin F. Logan**, "'Colorless' Artificial
  Reverberation", *IRE Transactions on Audio* AU-9(6), 1961 — the all-pass comb.
- **Miguel Moreno**, *Realistic drums with karplus-strong synthesis*, Mike
  Moreno DSP, 13 March 2022 —
  <https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/>
  — and his Pure Data instrument `[mymembrane~]` in
  [pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr): the loop's structure
  (the all-pass inside the Karplus-Strong loop, the pitch envelope on both
  delays, the sine cycle plus windowed noise, the snares that are this
  unit's buzz).  The loop's C++, its tuning and its voicing are this
  repository's.  The buzz's crackle (`brc::crackle`, `sn_crackle`: 0 on
  every preset here) is taken from mymembrane~'s snare layer; pd-mkmr is
  distributed under the 3-Clause BSD License, whose conditions apply to it:

> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
> 1. Redistributions of source code must retain the above copyright notice,
> this list of conditions and the following disclaimer.  2. Redistributions in
> binary form must reproduce the above copyright notice, this list of
> conditions and the following disclaimer in the documentation and/or other
> materials provided with the distribution.  3. Neither the name of the
> copyright holder nor the names of its contributors may be used to endorse or
> promote products derived from this software without specific prior written
> permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
> ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
> LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
> CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
> INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
> CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
> ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
> POSSIBILITY OF SUCH DAMAGE.

(The license file in pd-mkmr names no copyright holder; the patch's help file
reads "mymembrane~ v1, Miguel Moreno 2019".)

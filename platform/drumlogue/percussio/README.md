# Percussio

> **Disclaimer:** Percussio is an unofficial, independently developed unit, not
> affiliated with or supported by KORG. Provided "as is" with no guarantee of
> correct operation; the developer(s) and distributor(s) accept no liability for
> any damage, defect, or problem resulting from its use. See the
> [repository disclaimer](../../../README.md#disclaimer) for full terms.

A drumlogue synth unit that translates Stephen Dill's 1998 CCRMA Music 220a
project [**Percussion Synthesis**](https://ccrma.stanford.edu/~sdill/220A-project/drums.html)
into a playable instrument.

That project compares five ways of making percussion in Common Lisp Music —
subtractive, additive, FM, Karplus-Strong and granular — and publishes the exact
parameters for about fifty sounds, from brushed snares to a Chowning bell to an
electronic crash cymbal. Percussio implements all five as selectable engines and
ships each of those sounds as a preset carrying its original numbers, with the
`with-sound` call it came from in the comment beside it.

This is deliberately **not** another physical-modelling drum synth — for that,
see [Brachetti](../brachetti/) in this same directory. The engines here are the
textbook algorithms as the page describes them: a `randh` noise source into a
one-pole, a bank of sinusoids at frequencies measured off a real cymbal with an
FFT, Chowning's two oscillators, a probabilistic average of two delayed samples.
The character comes from that being what it is.

## Quick start

Preset 0 is `BrshSnr1`. Play it at **note 60** to hear a preset exactly as
published — the page's instruments take a frequency argument and this one takes a
note, so note 60 is where the two line up (see *Frequency becomes note* below).
Velocity scales amplitude; `Rand` on page 5 spreads pitch and level per hit,
which is what keeps a repeated sequencer step from sounding like one sample.

## The five engines

`Method` (page 1) picks the engine; `Model` picks the variant within it, and its
strings change with `Method`.

### Subtractive — `drum-subtract.ins`

```
randh (0.49 fs)  ->  one-pole | two-pole | one-zero  ->  amp env
```

Random-hold noise into one of three CLM filters. `Coef` is the one-pole's `b1`
or the one-zero's `a1`, and its **sign chooses the response**: on `OnePole`,
`b1 > 0` high-passes and `b1 < 0` low-passes (CLM's sign convention, `y[n] =
a0 x[n] - b1 y[n-1]`); on `OneZero` it is the other way round. `TwoPole` ignores
`Coef` and uses `Freq` and `Reso` instead.

`OnePole` and `OneZero` have no frequency of their own — the page's brushed
snares and rattles are unpitched by construction — so on those two the note
moves the **rate randh is held at** instead, which is the one thing that does
set their spectrum. Lower notes give a coarser, darker noise. On `TwoPole` the
note transposes the resonance and `NseRate` stays put.

### Additive — `drum-add.ins`

```
Bank -> up to 32 oscillators (or noise-driven resonators) -> amp env
```

`Bank` selects one of eleven published spectra: four found by ear (`Steel`,
`ClkBell`, `OddChm`, `EvnChm`, whose entries are partial numbers over `Freq`),
and seven measured in SND off recordings (`TubBl16/65/26`, `Gong`, `TCym26`,
`TCym16`, `NsyBass`, whose entries are frequencies in Hz at note 60). `Partial`
takes the first N of the bank, low to high, so it works as a brightness control.

`Model` is the page's four additive instruments:

| Model | instrument | what it adds |
|---|---|---|
| `Pure` | `add-partials` / `add-freqs` | oscillators only |
| `NseFlr` | `add-partials :noise-amp` | one wideband `randh` under the bank |
| `NsyFrq` | `add-noisy-freqs` | one `randh` per partial, at that partial's frequency |
| `TunedNs` | `add-noise` | `ppolar` resonators driven by noise **instead of** oscillators |

### FM — `drum-fm.ins`

```
mod osc -> index (Index1..Index2 under the mod env) -> carrier phase increment
```

Chowning's two-oscillator FM (JAES, September 1973) as the page's `fm`
instrument: `Freq` is the carrier, `Ratio` the modulator/carrier ratio, and the
index runs between `Index1` and `Index2` under a modulator envelope whose shape
`Model` selects — the three shapes the page's eight `fm` calls use:

| Model | modulator envelope | used by |
|---|---|---|
| `Dec .2` | `(0 1 <ModDcy> .2 100 0)` | Chowning's bell, metallic chime, marimba, brushed snare |
| `Dec 0` | `(0 1 <ModDcy> 0 100 0)` | wood drum, dance bass |
| `Rise` | `(0 0 <ModDcy> 1 100 0)` | sheet metal |

Peak deviation is `index × f_mod` in Hz, as Chowning defines it.

### Karplus-Strong — `drum-ks.ins`

```
X(t) = +1/2 [X(t-p) + X(t-p-1)]   or   -1/2 [...]
```

`Length` is the wavetable length p, `Blend` the blend factor b, and `Model`
chooses what the wavetable starts as (the page: "the initial wavetable can be
anything from a completely random signal to a sine wave to a constant"). b near
0 is a plucked string, b = 1/2 the page's snare, b near 1 a hollow metallic
plink or crash. The note transposes p.

### Granular — `drum-grani.ins`

```
source -> grains (density, size, grain envelope '(0 1 100 0)) -> amp env
```

The page granulates two recordings, a tubular bell and a Turkish cymbal. Both of
those are also in this unit as measured additive banks, because `add-freqs` uses
them — so the grain sources here are **Percussio's own renders of them**, one
second each, built once at load. Taking the cymbal apart and putting it back
together again, on a unit that ships no samples. `Model` picks the source and
the read direction.

## Parameters

Twenty-four, six pages of four. Several are read by more than one engine; the
table says which.

| # | name | page | read by | meaning |
|---|---|---|---|---|
| 0 | `Method` | 1 | all | Subtr / Addit / FM / KarplS / Granul |
| 1 | `Model` | 1 | all | variant; strings follow `Method` |
| 2 | `Decay` | 1 | all | the CLM duration argument, 5–10000 ms |
| 3 | `Level` | 1 | all | the CLM amplitude argument |
| 4 | `Attack` | 2 | all | envelope breakpoint, % of `Decay` |
| 5 | `Hold` | 2 | all | end of the plateau, % of `Decay` |
| 6 | `Curve` | 2 | all | CLM `env :base` |
| 7 | `Tune` | 2 | all | ±24 semitones on top of the note |
| 8 | `Coef` | 3 | Subtr `OnePole`/`OneZero` | filter coefficient ×100, signed |
| 9 | `Reso` | 3 | Subtr `TwoPole`, Addit `TunedNs` | pole radius r |
| 10 | `Freq` | 3 | Subtr `TwoPole`, Addit ratio banks, FM | reference frequency at note 60 |
| 11 | `NseRate` | 3 | Subtr, Addit `NseFlr` | `randh` rate as % of fs; the page uses 49 |
| 12 | `Ratio` | 4 | FM | modulator / carrier |
| 13 | `Index1` | 4 | FM | index floor |
| 14 | `Index2` | 4 | FM | index ceiling |
| 15 | `ModDcy` | 4 | FM | modulator envelope breakpoint, % |
| 16 | `Bank` | 5 | Addit | which published spectrum |
| 17 | `Partial` | 5 | Addit | how many of its partials, low to high |
| 18 | `NseAmp` | 5 | Addit `NseFlr`/`NsyFrq` | noise amplitude |
| 19 | `Rand` | 5 | all | per-hit spread of pitch (±6 st) and level |
| 20 | `Blend` | 6 | KarplS | blend factor b |
| 21 | `Length` | 6 | KarplS | wavetable length p at note 60 |
| 22 | `Densty` | 6 | Granul | grains per second |
| 23 | `Grain` | 6 | Granul | grain duration |

### The envelope

Every amplitude envelope on the page is one of

```
(0 S)  (Attack 1)  (Hold M)  (100 0)
```

so `Attack`, `Hold` and `Curve` reach all of them. `S` is 0 when there is an
attack segment and 1 otherwise; `M` is 1 (a plateau) on every engine but FM,
where it is 0.2 — every `fm` call on the page ends `50 .2 100 0`, and there `S`
is 0.8.

`Curve` is CLM's `:base`, and it is worth knowing which way it runs. CLM
interpolates a segment as `y = y0 + (y1-y0)(base^u - 1)/(base - 1)`, so on a
decay a base **above** 1 holds and then falls off a cliff, and a base **below**
1 drops fast and tails. The page's noise instruments default to `:amp-env-base
10` and go up to 1000000 — which is why a brushed snare there is a sizzle that
stops rather than a hit that decays. The sub-1 entries on the knob are the
ordinary percussive decay the page never uses.

## Presets

| # | preset | engine / model | LUFS | the call it comes from |
|---|---|---|---|---|
| 0 | `BrshSnr1` | Subtractive / OnePole | -10.70 | `(subtract-op 0 .2 .4 '(0 0 5 1 100 0) :b1 0.9 :amp-env-base 10)` |
| 1 | `BrshSnr2` | Subtractive / OnePole | -2.91 | `(subtract-op 0 .5 .6 '(0 0 5 1 100 0) :b1 0.9 :amp-env-base 1000)` |
| 2 | `BrshSnr3` | Subtractive / OnePole | -0.93 | `(subtract-op 0 1 .6 '(0 0 2 1 100 0) :b1 0.9 :amp-env-base 100000)` |
| 3 | `BrshSnr4` | Subtractive / OneZero | -8.26 | `(subtract-oz 0 .25 .4 '(0 0 5 1 100 0) :a1 -0.5 :amp-env-base 10)` |
| 4 | `BrshSnr5` | Subtractive / OneZero | -2.11 | `(subtract-oz 0 .5 .6 '(0 0 5 1 100 0) :a1 -0.5 :amp-env-base 1000)` |
| 5 | `SnareOP` | Subtractive / OnePole | -10.43 | `(subtract-op 0 .2 .3 '(0 1 100 0) :b1 0.2 :amp-env-base 100)` |
| 6 | `SnareOZ` | Subtractive / OneZero | -10.44 | `(subtract-oz 0 .2 .3 '(0 1 100 0) :a1 -0.2 :amp-env-base 100)` |
| 7 | `SnareLoP` | Subtractive / OnePole | -8.85 | `(subtract-op 0 .2 .3 '(0 1 100 0) :b1 -0.2 :amp-env-base 100)  -- low-pass` |
| 8 | `SnareLoZ` | Subtractive / OneZero | -8.80 | `(subtract-oz 0 .2 .3 '(0 1 100 0) :a1 0.2 :amp-env-base 100)   -- low-pass` |
| 9 | `SnarePP` | Subtractive / TwoPole | -13.44 | `(subtract-pp 0 .2 .2 '(0 1 100 0) :r 0.2 :frequency 400)` |
| 10 | `NsyBass1` | Subtractive / TwoPole | -3.14 | `(subtract-pp 0 .5 .02 '(0 0 5 1 100 0) :r .9 :frequency 100 :amp-env-base 1000)` |
| 11 | `NsyBass2` | Subtractive / TwoPole | -5.51 | `(subtract-pp 0 .5 .002 '(0 1 10 1 100 0) :r .99 :frequency 100 :amp-env-base 1000)` |
| 12 | `NsyBass3` | Subtractive / TwoPole | -4.49 | `(subtract-pp 0 .5 .0007 '(0 1 10 1 100 0) :r .999 :frequency 100 :amp-env-base 1000)` |
| 13 | `RattleP` | Subtractive / OnePole | -15.36 | `(subtract-op 0 .2 .2 '(0 0 30 1 50 1 100 0) :b1 0.5)` |
| 14 | `RattleZ` | Subtractive / OneZero | -14.99 | `(subtract-oz 0 .2 .2 '(0 0 30 1 50 1 100 0) :a1 -0.5)` |
| 15 | `BlwBottl` | Subtractive / TwoPole | -2.31 | `(subtract-pp 0 1 .0004 '(0 1 70 1 100 0) :amp-env-base 1000 :frequency 400 :r .9999)` |
| 16 | `FireCrkr` | Subtractive / TwoPole | 0.03 | `(subtract-pp 0 .4 .1 '(0 1 100 0) :r 0.7 :frequency 4000 :amp-env-base 1000000)` |
| 17 | `SteelDrm` | Additive / Pure | -12.82 | `(add-partials 0 .5 300 .5 steel-drum)   -- default amp-env (0 0 5 1 10 1 100 0)` |
| 18 | `ClockBel` | Additive / Pure | -10.28 | `(add-partials 0 2 300 .5 low-bell :amp-env '(0 1 10 1 90 .05 100 0)) which passes .03 where the page passes .05.` |
| 19 | `OddChime` | Additive / Pure | -8.81 | `(add-partials 0 1 1300 .5 odd-chime :amp-env '(0 1 10 1 100 0))` |
| 20 | `EvnChime` | Additive / Pure | -8.92 | `(add-partials 0 1 1300 .5 even-chime :amp-env '(0 1 10 1 100 0))` |
| 21 | `TubBell1` | Additive / Pure | -10.43 | `(add-freqs 0 2 .1 bell :amp-env '(0 1 10 1 100 0))   -- 16384-point FFT` |
| 22 | `TubBell2` | Additive / Pure | -10.56 | `(add-freqs 0 2 .1 bell ...)   -- 65536-point FFT, the page's favourite` |
| 23 | `TubBell3` | Additive / Pure | -10.02 | `(add-freqs 0 2 .1 bell ...)   -- 262144-point FFT` |
| 24 | `SmallGng` | Additive / Pure | -13.97 | `(add-freqs 0 1 .1 gong :amp-env '(0 0 1 1 10 1 100 0))` |
| 25 | `TurkCym1` | Additive / Pure | -16.72 | `(add-freqs 0 1 .1 cymbal :amp-env '(0 1 10 1 100 0))   -- 262144-point FFT` |
| 26 | `TurkCym2` | Additive / Pure | -14.22 | `(add-freqs 0 1 .1 cymbal ...)   -- 16384-point FFT` |
| 27 | `NsyTCym` | Additive / NsyFrq | -15.38 | `(add-noisy-freqs 0 1 .1 cymbal 1 :amp-env '(0 1 10 1 100 0))` |
| 28 | `NsyBsDrm` | Additive / TunedNs | -9.27 | `(add-noise 0 .5 .0005 '(100 1 200 .5 400 .2) :amp-env '(0 0 1 1 10 1 100 0) :r .99)` |
| 29 | `ChwnBell` | FM / Dec .2 | 0.28 | `(fm 0 10 1 200 1.4 :car-env '(0 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0) :mod-index2 1)` |
| 30 | `WoodDrum` | FM / Dec 0 | -5.94 | `(fm 0 .2 1 200 1.4 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0) :mod-index2 .2)` |
| 31 | `WoodDrm2` | FM / Dec 0 | -5.94 | `(fm 0 .2 1 200 0.6875 ...)   -- Ratio is thousandths, so 0.6875 stores as 0.688` |
| 32 | `MetlChim` | FM / Dec .2 | -7.12 | `(fm 0 1 .5 1000 2.005 :car-env '(0 1 50 .2 100 0) :mod-env '(0 1 25 .2 100 0) :mod-index2 1)` |
| 33 | `Marimba` | FM / Dec .2 | -5.96 | `(fm 0 .2 1 400 2.4 :car-env '(0 0.8 10 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0) :mod-index2 .2)` |
| 34 | `DnceBas1` | FM / Dec 0 | -8.15 | `(fm 0 .5 5 50 1.4 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0) :mod-index2 .2)   -- amplitude 5 clamps to 100%` |
| 35 | `DnceBas2` | FM / Dec 0 | -7.58 | `(fm i .4 5 80 0.6875 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 1 12 0 100 0) :mod-index2 2.5)` |
| 36 | `FMBrshSn` | FM / Dec .2 | -10.92 | `(fm 0 .3 .5 200 1.4 :car-env '(0 1 10 1 50 .2 100 0) :mod-env '(0 1 50 .2 100 0) :mod-index1 2 :mod-index2 1) the one page envelope where the two disagree.` |
| 37 | `ShetMetl` | FM / Rise | -4.79 | `(fm 0 1 1 40 2.2 :car-env '(0 .8 20 1 50 .2 100 0) :mod-env '(0 0 12 1 100 0) :mod-index2 2)` |
| 38 | `KSSnare1` | Karplus-Strong / Random | -13.09 | `(drum-ks 0 .5 :p 800 :b 0.5)         -- default duration 2, amp-env (0 1 100 1)` |
| 39 | `KSSnare2` | Karplus-Strong / Random | -12.07 | `(drum-ks 0 .5 :p 1000 :b 0.6)` |
| 40 | `KSCymbl1` | Karplus-Strong / Random | -5.67 | `(drum-ks 0 .5 :p 4000 :b 1 :amp-env '(0 1 90 1 100 0))` |
| 41 | `KSCymbl2` | Karplus-Strong / Random | -9.74 | `(drum-ks 0 .5 :p 2000 :b 1 :amp-env '(0 1 80 0 100 0))` |
| 42 | `MetPlnk1` | Karplus-Strong / Random | -17.12 | `(drum-ks 0 .5 :p 100 :b .98 :amp-env '(0 1 90 1 100 0))` |
| 43 | `MetPlnk2` | Karplus-Strong / Random | -21.19 | `(drum-ks 0 .5 :p 20 :b .997)` |
| 44 | `MetPlnk3` | Karplus-Strong / Random | -16.86 | `(drum-ks 0 .5 :p 50 :b .99)` |
| 45 | `MetPlnk4` | Karplus-Strong / Random | -16.53 | `(drum-ks 0 .5 :p 150 :b .99 :amp-env '(0 1 90 1 100 0))` |
| 46 | `MetPlnk5` | Karplus-Strong / Random | -22.19 | `(drum-ks 0 .5 :p 25 :b 1)` |
| 47 | `Pluck1` | Karplus-Strong / Random | -17.01 | `(drum-ks 0 .5 :p 40 :b 0 :amp-env '(0 1 90 1 100 0))` |
| 48 | `Pluck2` | Karplus-Strong / Random | -14.14 | `(drum-ks 0 1 :p 25 :b 0)` |
| 49 | `Pluck3` | Karplus-Strong / Random | -12.46 | `(drum-ks 0 .5 :p 200 :b 0 :amp-env '(0 1 50 1 100 0) :duration 2)` |
| 50 | `Pluck4` | Karplus-Strong / Random | -11.72 | `(drum-ks 0 .5 :p 400 :b 0 :amp-env '(0 1 50 1 100 0) :duration 2)` |
| 51 | `BrknCym1` | Granular / Cymbal | -15.83 | `(grani 0 2 20 "turkish-cymbal-1.snd" :grain-envelope '(0 1 100 0) :amp-envelope '(0 0 10 1 100 0))` |
| 52 | `BrknCym2` | Granular / CymRev | -15.33 | `(grani 0 4 5 "turkish-cymbal-1.snd" ... :grain-density 20 :reverse t)` |
| 53 | `RhytBel1` | Granular / BellRev | -23.10 | `(grani 0 2 10 "tubular-bell.snd" :grain-envelope '(0 1 100 0) :amp-envelope '(0 1 50 1 100 0) :grain-density 4 :reverse t)` |
| 54 | `RhytBel2` | Granular / Bell | -17.22 | `(grani 2 2 10 "tubular-bell.snd" ... :grain-density 8 :reverse t)` |

## What differs from the page, and why

Everything below is also marked at the point in the source where it happens.

### Frequency becomes note

CLM instruments take a frequency; a drumlogue synth takes a note. **Note 60
plays the page's value and every other note transposes it** — uniformly, for the
two-pole centre frequency, the FM carrier, the ratio-bank fundamental, the
Karplus-Strong wavetable length and the measured frequency tables alike. So a
preset sounds as published at C4 and stays playable off it.

This is not concert pitch: `Marimba` is 400 Hz at note 60, not 261.6. `Tune`
moves it if you want it in a key.

### The resonators are normalised

CLM's `two-pole` and `ppolar` are un-normalised, so their gain runs from about 1
at r = 0.2 to about 4·10⁵ at r = 0.9999, and the page compensates by hand in
every call — amplitude `.4` for a one-pole against `.0004` for the blown bottle.
It calls this out itself as a defect of `add-noise`: *"it is impossible to
control the peak amplitude, so it doesn't work with real sounds."*

`clm::TwoPole` normalises for **unit output RMS under white noise**, which is
the only thing it is ever fed here (`randh`, in both `subtract-pp` and
`add-noise`). Peak normalisation would have left `Reso` as a volume control: a
narrow band passes proportionally less noise power, a 25 dB drop across the
knob's range, which is exactly the spread the page was compensating for. Under
RMS normalisation `Reso` changes timbre and leaves level alone — measured at
**0.5 dB of level change across the whole range**, at every frequency — and in
`add-noise` each tuned-noise partial contributes in proportion to its own
amplitude, which is what a partial amplitude is supposed to mean.

The energy of the un-normalised impulse response has a closed form,

```
sum |h[n]|^2 = (1 + r^2) / ((1 - r^2) * A * B)
A = (1 - r)^2 + 4 r sin^2(theta/2)
B = (1 - r)^2 + 4 r cos^2(theta/2)
```

and `g` is its reciprocal square root. `A` and `B` are the factored form of
`(1 + r^2)^2 - 4 r^2 cos^2(theta)`; written that way it cancels catastrophically
in float32 near r = 1, and factored it does not.

Because of this, rows whose amplitude was pure gain compensation carry a plain
musical level instead. Where the page's amplitude is a musical choice — the
`.2`/`.3`/`.4`/`.6` spread across the one-pole and one-zero sounds, `.5` against
`1` on the FM instruments — it is kept as written. Amplitudes above 1 (the dance
basses ask for 5) clamp at 100%.

### The Karplus-Strong blend factor runs the other way

The page prints the recurrence with the **plus** branch taken with probability
b, but its prose and every one of its presets read the other way: *"b near 0
simply averages the samples, and produces string-like sounds"*, with b = 0 named
Plucked String, b = 1 named Cymbal and Metallic Plink, b = 1/2 the snare. Plain
averaging is the plucked string and a fixed inversion is the hollow metallic
one, so `Blend` here is the probability of **inverting**. That is what makes the
page's own preset values produce the sounds the page names for them.

The test suite pins this down by autocorrelation: b = 0 gives +0.99 at lag p
(period p, a string), b = 1 gives −0.99 at lag p and +0.98 at 2p (period
doubling, the hollow one), b = 0.5 gives 0.00 (maximum randomness, the snare).

### Karplus-Strong lengths are rate-scaled

CLM's default sample rate in 1998 was 22050 Hz. A wavetable length is in
samples, so the page's p = 4000 is a 5.5 Hz loop there and would be a 12 Hz one
at 48 kHz. The preset rows carry p scaled by 48000/22050 ≈ 2.177 so the timbre
is the one the page names; the unscaled number is in each row's comment.

### `randh` is bipolar

The page says its amplitudes are ~U[0,1]; CLM's `randh` is ~U[−amp, +amp], and
that is what is implemented. A unipolar source would put a large DC step into
the one-pole and two-pole filters for the output stage to spend headroom
removing.

### Small things

* **`ClockBel`'s envelope is approximated.** `(0 1 10 1 90 .05 100 0)` needs a
  breakpoint the three-segment shape does not have; a 0.1-base decay from 10%
  passes .03 where the page passes .05.
* **`FMBrshSn`'s carrier envelope starts at 0.8, not 1.0.** The start level
  follows the attack breakpoint, and this is the one envelope on the page where
  the two disagree — 0.2 of level over the first 10% of a 0.3 s sound.
* **`Ratio` is stored in thousandths**, so the wood drum's 0.6875 stores as
  0.688. That is 0.1 Hz on a 137 Hz modulator.
* **`(0 1 80 0 100 0)` is folded into `Decay`.** An envelope that reaches zero at
  80% of a 2 s duration is the same as a full-length envelope over 1.6 s, so
  `KSCymbl2` ships `Decay` 1600 — exact, not an approximation.
* **The last 2 ms of every voice are faded out.** CLM just stops writing samples
  at the end of the duration, and several of the page's envelopes are still at
  full amplitude there (`(0 1 100 1)` on `drum-ks`, `(0 1 90 1 100 0)` on the
  cymbals). On hardware that truncation is an audible click.
* **Sub-20 Hz table entries are kept but do not set the level.** The first entry
  of several measured banks (0.24 Hz at amplitude 3.34 on the 262144-point bell)
  is the analysing transform's DC bin, not a partial. It plays; a ~10 Hz DC
  blocker on the voice takes most of it, and bank normalisation ignores anything
  under 20 Hz so one FFT artefact cannot set the gain of a whole bell.
* **One typo is corrected.** The 16384-point cymbal table ends
  `4324 .342 4556 .808 .4587 .467`; a 0.4587 Hz partial next to a 4556 Hz one is
  not a measurement, so 4587 is stored.
* **The 262144-point cymbal table's order is preserved**, 2920 Hz before 2196 Hz
  as printed, because `Partial` takes the first N entries and the page's own
  ordering is what it takes.
* **Jan Mattox's `fm-drum` is not translated.** The page uses it for its bass
  drum, electronic snare and tom, but only ever shows the call — `(fm-drum 0 1
  40 .2 5)` — never the instrument, so there is nothing to transcribe.

## Levels

Measured with [`tools/level_meter`](../tools/level_meter/), all 55 presets, note
60, velocity 127:

```
loudest +0.28 LUFS, quietest -24.06 LUFS, mean -10.61 LUFS
```

which sits between EffeESP32 (−9.6) and Brachetti (−13.5). No preset clips: the
soft knee in [`common/output_stage.h`](../common/output_stage.h) is bounded by
0.995 by construction, so the peaks that read −0.06 dBFS are at that asymptote,
not at a clamp.

### Calibration

The output stage is a trim into that soft knee. Percussio moves the knee
threshold up from the shared 0.70 default to **0.95**, because most of this unit
is sustained tonal material — bells, chimes, a marimba, plucked strings — and a
memoryless waveshaper on a sustained tone is audible in a way it is not on a
drum transient.

To measure that, every preset was rendered against a linear reference (trim
−12 dB, knee never engaged), the best-fit linear gain removed, and the residual
energy taken as a fraction of the signal. That is the waveshaping the output
stage adds, with no assumption that the material is harmonic — which matters
here, because `harmonics.py` needs a fundamental to measure against and the
bells do not have one it can find.

| trim | knee thr | mean LUFS | mean residual | worst | over −40 dB |
|---|---|---|---|---|---|
| 0 dB | 0.70 | −16.08 | −55.2 dB | −25.2 | 7/28 |
| 0 dB | 0.85 | −16.04 | −57.9 dB | −30.9 | 7/28 |
| 3 dB | 0.70 | −13.31 | −51.9 dB | −16.6 | 8/28 |
| 3 dB | 0.95 | −13.18 | −53.1 dB | −18.4 | 8/28 |
| 6 dB | 0.70 | −10.75 | −38.7 dB | −12.3 | 15/28 |
| 6 dB | 0.85 | −10.62 | −44.3 dB | −12.5 | 10/28 |
| **6 dB** | **0.95** | **−10.57** | **−48.6 dB** | **−12.5** | **8/28** |
| 8 dB | 0.85 | −9.13 | −34.2 dB | −10.4 | 18/28 |

Raising the threshold is a **strict improvement at every trim** — cleaner *and*
marginally louder, because fewer presets touch the knee at all. It holds on the
attack window too (first 40 ms, mean over all 55 presets: −36.6 dB at 0.70,
−46.5 dB at 0.95). The sharper corner does put more residual above 8 kHz, but
only from −61 dB to −55 dB relative to the signal.

The worst preset is `ShetMetl` at −12.5 dB in every configuration, which is the
one sound on the page made of distortion to begin with.

Both are overridable for re-measurement without editing the source:

```sh
EXTRA_FLAGS="-DPERCUSSIO_TRIM_DB=8.0f -DPERCUSSIO_KNEE_THR=0.85f" \
    ./run.sh ../../percussio
```

## Build and test

```sh
# Unit, via the SDK docker container
docker/run_cmd.sh build drumlogue/percussio

# Host syntax check
g++ -std=gnu++14 -fsyntax-only -I. -I../common -U__ARM_NEON__ -U__ARM_NEON \
    -Wall -Wextra -Wno-strict-aliasing unit.cc

# DSP tests -- CLM primitives, all presets, full parameter sweep, CPU
g++ -std=gnu++14 -O2 -I. -I../common -U__ARM_NEON__ -U__ARM_NEON \
    -Wno-strict-aliasing test_dsp.cpp header.c -o /tmp/percussio_test
/tmp/percussio_test

# Levels
cd ../tools/level_meter && ./run.sh ../../percussio 60 127
```

`test_dsp.cpp` reads the real `unit_header`, so two things that only bite on
hardware cannot drift:

* **the header's parameter defaults must equal preset 0** — `Init()` calls
  `LoadPreset(0)`, but the OS then writes its own set on top from those default
  fields, so a mismatch boots the unit showing one preset and sounding like
  another;
* **the declared ranges must cover every value any preset stores** — the OS
  parameter store cannot represent an out-of-range value.

Both were learned on hardware; see [`../brachetti/CLAUDE.md`](../brachetti/CLAUDE.md).

## Measured

ARM cross-build (`-march=armv7-a -mtune=cortex-a7 -marm -Os`, gcc 14 rather than
the vendor's, so `.text` will move a little):

```
.text 10156   .rodata 2304   .data.rel.ro 3188   .data 4   .bss 397264
```

`.bss` is nearly all two things: four 12288-sample Karplus-Strong wavetables
(197 KB) and the two pre-rendered granular sources (192 KB, int16).

Cost per engine, four voices, worst case, as a host real-time factor. Absolute
numbers mean nothing on an x86 host; the ratios between engines do, and this is
where a regression that makes one engine an order of magnitude dearer than the
rest would show:

| engine | host RTF |
|---|---|
| Subtractive `TwoPole` | 729× |
| Additive `Pure` (28-partial gong) | 151× |
| Additive `NsyFrq` | 71× |
| Additive `TunedNs` | 142× |
| FM | 251× |
| Karplus-Strong | 730× |
| Granular | 267× |

## Notes for anyone editing this

* **`si_floorf()` in `float_math.h` is `(float)((uint32_t)x)`** and is therefore
  wrong for any negative argument. An FM deviation larger than the carrier
  increment makes the instantaneous increment negative — index 5 at ratio 1.4
  already does it — and every one of those samples came back NaN.
  `clm::Phasor::advance()` does its own signed wrap for that reason. The helper
  is left alone here because eight other units share copies of that file and
  changing its semantics is their call, not this unit's.
* **Voices are one-shot.** They run for `Decay` and free themselves; note-off
  and gate-off do nothing. That is what the CLM instruments do — every one takes
  a duration and stops — and it also sidesteps the drumlogue firing `gate_on`
  and `gate_off` in the same scheduler tick, which strands a gated envelope
  before it opens (Brachetti's pass 27).
* **A voice copies its settings at trigger.** Changing a knob or a preset while
  something is ringing does not reach into it.
* **Only the Karplus-Strong recursion flushes denormals.** A user unit is a
  shared object, so it never gets `crtfastmath.o`'s FPSCR setup and denormal
  cost is not knowable from here. `OnePole` and `TwoPole` do not need the check
  — they are driven by `randh` and nothing else, which is O(1) every sample — and
  it is not free: on the two-pole it measured 30% of an already tight loop.
* **`clm::Oscil` is the coupled form**, not `y[n] = 2cos(w)y[n-1] - y[n-2]`. The
  state matrix has determinant 1, so the amplitude cannot drift over the 2 s
  decays the bells need; the direct form does. The test checks the peak after
  2 s at four frequencies.

## Source

Stephen Dill, *Percussion Synthesis*, Music 220a (Introduction to Sound
Synthesis and Signal Processing), CCRMA, Stanford University, 1998.
<https://ccrma.stanford.edu/~sdill/220A-project/drums.html>

The two papers the page builds on: John Chowning, "The Synthesis of Complex
Audio Spectra by Means of Frequency Modulation", *JAES*, September 1973; Kevin
Karplus and Alex Strong, "Digital Synthesis of Plucked-String and Drum Timbres",
*Computer Music Journal*, September 1983.

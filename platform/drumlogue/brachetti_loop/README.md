# Brachetti Loop (`BrcLoop`)

> **Disclaimer:** Brachetti Loop is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The tenth unit of the [Brachetti family](../tools/brachetti_equiv/README.md),
and its first drum made from a **delay loop** rather than from modes: a
Karplus-Strong loop with a Schroeder all-pass filter inside it, after Miguel
Moreno's article [*Realistic drums with karplus-strong synthesis*](https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/)
(see [Credits](#credits-and-sources)).  The modal units build a drum from at
most six modes per voice, the kernel and the cymbal from dense resonator
banks, BrcNoise from shaped noise; a loop has a resonance at every trip round
it, up to the top of the spectrum, for the cost of two delay-line reads, so
it rings with many partials at little CPU.  The all-pass moves those partials off the harmonic
series, which is what turns a plucked string into a struck head.

It runs on the family's shared modal engine (`brachetti_common/modal_synth.h`)
like BrcWorld: the same 24-knob layout, preset loading, voice life cycle,
Punch and master stage, with the loop drum as its own layer.  Like BrcWorld it
was not split from the original `brachetti`: its presets were voiced after
recordings.

| | |
|---|---|
| Unit name on the drumlogue | `BrcLoop` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724C70` ('BrLp') |
| Engine | Karplus-Strong loop + Schroeder all-pass, tension glide, snares on the head's motion |
| Presets | 8 |
| Knobs | 24 (the modal units' layout, six slots the family's own) |
| ARM size (cross-built, `arm_size.sh`) | .text 19060 · .rodata 2144 · .data.rel.ro 80 · .data 4 · .bss 51776 |
| CPU (`arm_cpu.sh`) | 818.8 ARM instructions per rendered frame (an instruction count, for comparing this unit with itself) |

## Presets

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 0 | RackTom | 58 | rack tom: a 12-inch tom, close-miked; a slight sag at the strike | `Tom1-001-CloseRoom.wav` |
| 1 | FlrTom | 52 | floor tom: a 16-inch tom, starting 20 % sharp on a hard hit and settling | `Tom2-004-CloseRoom.wav` |
| 2 | Snare | 60 | acoustic snare: a short head, the wires on its motion | `acoustic-snare.wav` |
| 3 | SnrHvy | 47 | heavy snare: a deep, slack snare struck hard | `snare heavy.wav` |
| 4 | Kick | 40 | kick: a hard beater on a damped bass drum, a 35 % dive at the strike | `KickA-Hard-012-CloseRoom.wav` |
| 5 | Taiko | 42 | taiko: a big cowhide head, a heavy bachi; a long ring | `Taiko-Hit.wav` |
| 6 | Bendir | 41 | bendir: a frame drum with a snare string across the head | `020_BendirDoum.wav` (Pelle e Legno) |
| 7 | Surdo | 34 | surdo: the samba bass drum, open | `027_SurdoOpen.wav` (Pelle e Legno) |

The kit pieces (0-5) are voiced after the original brachetti's reference
recordings — its `samples/` folder, on the `original_brachetti` branch (the
same files its AcTom, AcSnre, Kick and Taiko were tuned to) — and the frame
drum and the surdo after the [Pelle e Legno](../pelle_legno/) samples.

## The loop drum

One voice is the family's exciter (a mallet and a noise burst, mostly silent
here: `exciter_gain`), an optional modal bank (no preset uses it), and the
loop drum (`loop::Drum` in `voice.h`):

```
            excitation e[n] ───────────────────────────────┐
                                                           ▼
  ┌──► delay N(t) ──► one-pole LP ──► DC block ──► × G (± flip) ──► all-pass M(t), g ──► (+) ──► y[n] ──► out
  │     (linear interp.)  loss filter                  K-S blend                     │
  └──────────────────────────────────────────────────────────────────────────────────┘
                                                      y[n] ──► follower ──► × noise ──► band-pass ──► + out
                                                                                (the snares)
```

- **The loop** (Karplus & Strong 1983; Jaffe & Smith 1983).  A delay line
  read with linear interpolation, a one-pole low-pass `lp += a·(x − lp)` as
  the loss filter, and a gain per trip `G` set from the preset's T60 at the
  note.  The loss filter's and the DC blocker's own attenuation at the note
  is given back, and `G` is capped at 0.9995: every filter in the loop has a
  gain of at most 1, so no knob can make the loop grow.
- **The loss filter from two decay times** (after Välimäki, Huopaniemi,
  Karjalainen & Jánosy 1996).  A preset gives the T60 at the note and, as a
  fraction of it (`lp_hf`), the T60 at 3 kHz; the strike solves for the
  one-pole that loses that much more at 3 kHz per trip.  With the ratio `r`
  of the two per-trip attenuations, `|H(ω₁)|² / |H(ω₀)|² = r²` is a quadratic
  in the pole: `b = B − √(B² − 1)`, `B = (cos ω₀ − r²·cos ω₁) / (1 − r²)`.
  A fixed coefficient (the article's "damping", and BrcWorld's wire) loses
  the same per TRIP, and a 58 Hz loop makes 58 trips a second against a
  240 Hz loop's 240: the first voicing search, with one, left the surdo's
  and the taiko's highs ringing for seconds like a steel string's.  Designed
  from times, the highs die in the time the preset says at every pitch.
- **The all-pass** (Schroeder; the "all-pass from two combs" of J. O. Smith's
  *Physical Audio Signal Processing*).  `v[n] = u[n] + g·v[n−M]`,
  `y[n] = −g·v[n] + v[n−M]`: flat in magnitude, but its delay depends on
  frequency, periodically in ωM.  Inside the loop it pulls each resonance a
  different distance off the harmonic series.  How they spread depends on
  the ratio of `M` to the loop's period (`ap_ratio`, the `ApDelay` knob; the
  article's "all-pass frequency") and on `g` (`ap_gain`, the `Inharm` knob;
  the article's "all-pass gain", ±0.95 here).
- **The DC blocker** (`x − lp₈ Hz(x)`, inside the loop).  A positive loop
  holds DC as its lowest "resonance", and the excitation's noise has some;
  without it a struck preset would drift on a slowly decaying offset.
- **The excitation** (the article's).  One sine cycle `exc_ratio` periods
  long (the article: one period) plus white noise, low-passed at
  `exc_noise_hz` and shaped by the cycle's Hann window, fed into the loop
  (and heard directly: the attack).  Both run on rotating phasors, so a
  strike costs no `sinf` per sample.  A stiffer stick (`MlltStif`, and
  `VlMllStf` with velocity) shortens the cycle: a brighter strike.  The
  strike position (`exc_pos`, `HitPos`) subtracts the same cycle `pos ×
  period` later — the comb filter of a plucked string's pick position
  (Jaffe & Smith) — which thins the fundamental the way a strike off the
  centre does.
- **The glide** (the article's "pitch bend").  Both delays, the loop's and
  the all-pass's, are scaled by `1 / (1 + bend·env)`, `env` falling from 1
  with time constant `bend_ms`: the strike starts `bend` above the note and
  settles, as a head's tension rises with a hard stroke and relaxes.  `bend`
  scales with velocity.  A fast, deep glide is also a chirp that sharpens the
  attack, which the article points out.
- **The snares** (the article's).  An envelope follower (attack `sn_att_ms`,
  release `sn_rel_ms`) on the loop's output sets the level of white noise
  through a two-pole band-pass (`sn_hz`, `sn_q`; 3 kHz in the article): the
  wires rattle as long as, and as hard as, the head moves.  (BrcSnare's wires
  ring on the exciter's noise burst instead; BrcWorld's balafon `Buzz` is
  the same idea as this, gated per sample by the bank's motion.)
- **The Karplus-Strong drum blend** (Karplus & Strong 1983, not in the
  article).  With probability `b` a trip round the loop flips sign: at
  `b = 0.5` the loop's tone turns into a pitched noise burst, Karplus and
  Strong's own drum.  `blend` in the table, and the `Blend` knob adds to it.

### Tuning

A Karplus-Strong loop sounds at `fs / (total delay)`, where the total is the
delay line plus every filter's phase delay at that frequency.  The all-pass
adds a lot, and differently at every frequency, so `strike_loop` sets the
delay line so that the loop's phase at the note's ω₀ is exactly one period:

```
N = 2π/ω₀ − P_ap(ω₀) − P_lp(ω₀) + Lead_dc(ω₀)

P_ap(ω) = (ωM + 2·atan2(g·sin ωM, 1 − g·cos ωM)) / ω        all-pass phase delay
P_lp(ω) = atan2(b·sin ω, 1 − b·cos ω) / ω,  b = 1 − a        loss filter
Lead_dc(ω) = ((π − ω)/2 − atan2(b·sin ω, 1 − b·cos ω)) / ω    DC blocker (a lead)
```

The loop's total phase rises monotonically with frequency (an all-pass's
phase always falls), so this is its first resonance and the only one at ω₀.
Where the all-pass alone would take more than a period (`M` long and `g`
near 1: its low-frequency delay is `M·(1+g)/(1−g)`), `M` is shortened until
the line keeps at least two samples.  Measured (velocity 127, after the
glide; `Inharm`, `ApDelay` and `Mterl` at both ends, three octaves of
notes): the first resonance within 2 cents of the note, except at `Inharm`
+100 (g = 0.95), where a crowd of neighbouring resonances makes the strongest
one near the note read up to 40 cents off on low notes.

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all ten Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Bend` · `VlMllStf` |
| 4 | dedicated | `BendTm` · `Snares` · `Dkay` · `Punch` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `ApDelay` |
| 6 | dedicated | `NzMix` · `Blend` · `SnrTone` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root: the loop follows the played note, exactly in tune (see Tuning) |
| `Poly` | voice cap 1-4; every strike stacks |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset.  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

Every mapping is REFERENCE-ANCHORED, as on every Brachetti unit: a factor of
exactly 1 (or an offset of exactly 0) at the preset's shipped value, so a
preset plays as voiced until a knob moves.  The standard knobs keep the
meaning they have on the other modal units and act on the loop with it; six
slots are BrcLoop's own (they ship 0):

| Knob | On the loop | Up turns it | Range |
|---|---|---|---|
| `MlltRes` | **presence**: the loop's level (as it is the bank's) | louder | 3.4 octaves per unit |
| `MlltStif` | **stick**: the excitation cycle's length (and the mallet's stiffness) | shorter, brighter | ×¼ .. ×4 |
| `Bend` *(own, VlMllRes' slot)* | the glide's depth; at its top it adds a 30 % glide where a preset has none | deeper | ×¼ .. ×4 |
| `VlMllStf` | velocity → stick: a harder hit shortens the cycle further (and stiffens the mallet) | more | 2 octaves per unit × velocity |
| `BendTm` *(own, Partls' slot)* | the glide's time | slower | ×¼ .. ×4 |
| `Snares` *(own, Model's slot)* | the snares' level; at its top it adds snares (3 kHz, Q 2) where a preset has none | louder | ×¼ .. ×4 |
| `Dkay` | **length**: the loop's T60 (as the bank's ring) | longer | 4.5 octaves per unit |
| `Punch` *(Rel's slot)* | the family's transient shaper (below) | | |
| `Mterl` | **brightness**: the T60 at 3 kHz against the note's (`lp_hf`) | brighter, the highs ringing longer | 3 octaves per unit |
| `HitPos` | **strike position**: the excitation's comb, from the centre towards the rim | thinner fundamental | half a period per unit |
| `Inharm` | **inharmonicity**: the all-pass gain, an offset | further from harmonic (− the other way) | ±0.9 per unit, clamped ±0.95 |
| `ApDelay` *(TubRad's role, relabelled)* | the all-pass delay against the period | longer delay: a different spread | ×0.35 .. ×2.8 |
| `NzMix` | the noise in the excitation (as it is the burst's) | noisier strike | 3 octaves per unit |
| `Blend` *(own, NzRes' slot)* | Karplus & Strong's drum: the chance a trip flips sign, added to the preset's | noisier tail | 0 .. +0.5 |
| `SnrTone` *(own, NzFltr's slot)* | the snares' band | higher | ±1 octave |
| `NzFltFrq` | the excitation noise's corner (as it is the burst's filter) | brighter noise | proportional |

The displaced standard knobs play at fixed values: VlMllRes 0, Partls 2
(the bank's own mode count), Model `Drumhd`, Rel each preset's shipped value,
NzRes 30 %, NzFltr low-pass.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike.  **Up**, the first 5 ms pass untouched and then the body drops (τ 15 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.  It never boosts the attack (the unit's peak limiter would take a boost straight back off).  It is `brc::Punch`, shared by all ten units.

### Knob review

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py)
(each knob to both ends on every preset, mean dB change over bands and time).
With the standard layout, four knobs did nothing on any preset: `VlMllStf`,
`NzRes`, `NzFltr` and `NzFltFrq` act on the noise burst, which these presets
barely use.  `NzRes` and `NzFltr` gave their slots to `Blend` and `SnrTone`;
`VlMllStf` and `NzFltFrq` kept their jobs and now reach the loop's
excitation too.  `Mterl` and `ApDelay` got steeper curves (they moved
under 1.5 dB on half the presets).  As shipped:

```
=== brachetti_loop   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)
           Pitch    Poly Velocit MlltRes MlltSti    Bend VlMllSt  BendTm  Snares    Dkay   Punch   Mterl  HitPos  Inharm ApDelay   NzMix   Blend SnrTone NzFltFr
RackTom     15.9    :0.4     9.3    10.4     5.5     2.4     6.0    :0.4     7.3    16.8     6.7     2.4     3.9     6.4    :1.3     6.5    18.0       .     4.9
FlrTom      18.5    :0.5    10.5    11.5     5.9     2.9     7.7    :1.5    10.7    20.1     7.7     4.5     6.5     3.5     3.7     5.3    22.2       .     3.2
Snare        8.5       .    10.0    11.4     3.6    :0.7     7.2       .     6.6    16.3     7.5    :0.9     3.1     6.5       .       .    17.4     2.8       .
SnrHvy       5.2       .     9.9    11.3     2.2     2.0     4.3       .     6.0    10.2     7.4    :1.2     3.0     3.2     4.3     3.6     9.8     3.1    :0.7
Kick        11.1    :0.3    10.3    10.2     6.8     3.4     6.0    :1.3     7.9    14.8     6.5     3.4     2.2     5.8    :0.9     6.2    15.3       .     4.6
Taiko       16.5    :0.6    12.5    14.4     5.0     3.2     6.4    :0.9     5.8    28.4    10.7     3.1     3.8     4.5    :1.5     8.4    27.8       .    12.6
Bendir       6.0    :0.5     9.4    10.5     3.6    :0.6     5.1       .     7.2    11.6     6.8     1.9     2.6     4.5     4.6     5.0    11.9     2.0     2.0
Surdo       11.9    :0.4     9.8    10.9     3.9     2.2     6.1       .    13.0    15.0     7.2     4.2     1.9     2.6     1.8     6.0    17.6       .     4.1
```

`BendTm` is small where a preset's glide is (it times a glide that is barely
there: raise `Bend` first), and `SnrTone` is silent on the presets without
snares (raise `Snares` first).  On Snare, `NzMix` and `NzFltFrq` scale an
excitation noise the preset barely uses, and `ApDelay` an all-pass whose
gain it ships near 0 (raise `Inharm` first).

## How the presets were voiced

As BrcWorld's and BrcTom's added presets:
[`match_samples.py`](../tools/brachetti_equiv/match_samples.py) with the
`loop` specs of [`match_specs.py`](../tools/brachetti_equiv/match_specs.py)
walks each preset's free values downhill against its recording (energy over
time, band balance over time, 40 dB decay, strongest partial), comparing the
voice before the master limiter; trims by the family's body-target method
(`preset_probe calib` reprints them).  Each preset starts with its loop at
the recording's strongest partial (its root the nearest note below) and its
T60 from the recording's decay.  Two changes to the tool came with it, both
inert for the other units:

- **References at 44.1 kHz** are resampled to 48 kHz (several of the
  original's recordings are 44.1 kHz).  `load` also scaled integer WAVs by
  1/32768 after converting them to float, which never applied; it does now
  (the features are level-independent, so no score moves).
- **A spec may ask for each band's decay** (`late`, `band_decay`): the
  energy of the six bands from 300 ms to 1 s against their own in the first
  100 ms, in decades, weighted 0.5.  The default features cannot see a body
  whose highs ring too long — their spectral windows end at 300 ms, and in a
  later window the band SHARES are the fundamental's in any drum — and the
  first search used that to open the loss filter: the highs rang on for a
  second like a steel string's, which the recordings' do not.  (The second
  half of the fix was the loss filter itself: see "The loop drum".)

```
BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti \
    python3 match_samples.py match_specs.py loop --state loop.json
```

Measured, preset vs recording (velocity 110, sequencer gate):

| Preset | score | t40, ms | strongest partial, Hz |
|---|---:|---:|---:|
| RackTom | 1.011 | 515 / 420 | 241 / 239 |
| FlrTom | 0.849 | 905 / 900 | 167 / 167 |
| Snare | 0.986 | 225 / 225 | 264 / 264 |
| SnrHvy | 0.730 | 190 / 190 | 127 / 127 |
| Kick | 0.832 | 400 / 360 | 80 / 79 |
| Taiko | 0.890 | 1080 / 1080 | 90 / 89 |
| Bendir | 0.698 | 240 / 240 | 101 / 202 |
| Surdo | 0.342 | 625 / 625 | 58 / 58 |

The score includes the band-decay term (above); without it the same
presets score 0.22-0.86, where BrcWorld's measure 0.41-2.50 against their
samples.  Bendir's loop sits at 101 Hz, between the recording's doum
(87 Hz) and half its strongest partial (202 Hz); the score counts an octave
as a near-miss.  Snare and SnrHvy match their decay and pitch but not their
energy over time (e30: 71 % in the first 30 ms against the recording's
92 %, 73 % against 33 % — SnrHvy's recording is cut at 0.25 s).

**What a loop cannot do.**  On the spectrograms the recordings' highs are a
dense, noise-like cloud; the loop's are discrete partials — one all-pass
spreads them, but a membrane's mode density grows with frequency and a
loop's does not.  The search uses the excitation's noise, the snares and the
two-T60 loss filter to make up for it in the first 100 ms; after that a
BrcLoop drum is a cleaner, more tonal drum than the recording.  That is the
character of the technique, and where it is useful beside the modal units:
tuned toms and kicks that glide, ring clean and cost little.

## Build

```
build drumlogue/brachetti_loop
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_loop.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  Not yet tested on hardware.

## Verification

There is no original to compare with.  What was checked on the host:

- **Tuning**: the first resonance against the note, `Inharm` and `ApDelay`
  at both ends, three octaves (see Tuning).
- **Stability**: 3000 random sessions — every preset, every knob anywhere in
  its range, random notes and velocities, up to four stacked strikes,
  AllNoteOff — with every output sample finite and within the limiter's
  ±0.99, and every loop's state bounded.
- **Identity at the anchors**: re-voicing the knob slots (the knob review)
  left every preset's score and trim unchanged to the last digit.
- **Units loaded together**: `multi_load.sh` (all ten units, 457,040 display
  strings, identical alone and together); `arm_size.sh` finds no unique
  symbol.
- `lib_uses.py --check`.
- **The other units**: the library changes that came with this unit are
  comments; `run_equiv.sh` finds the eight split units IDENTICAL to the
  original, and BrcWorld's renders are byte-identical to before.

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, the family's knob slots, each preset's loop settings and output trim |
| `voice.h` | one voice: the exciter, the modal bank and the loop drum |
| `synth_engine.h` | the strike (the loop tuned and set up from the table, the knobs on it) and the per-sample voice; the delay lines |

Shared with the other units: [`brachetti_common`](../brachetti_common/)
(`modal_synth.h` is the engine).

## Credits and sources

- **Miguel Moreno**, *Realistic drums with karplus-strong synthesis*, Mike
  Moreno DSP, 13 March 2022 —
  <https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/>
  — and its Pure Data instrument `[mymembrane~]` in
  [pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr).  The structure of this
  unit's drum is his: the all-pass inside the Karplus-Strong loop as the
  source of a drum's inharmonicity, the pitch envelope on both delays, the
  sine cycle plus windowed noise as the excitation, and the snares as noise
  following the head's envelope.  The article publishes no code or
  coefficients, and none were taken from the Pd patch: the C++ here, its
  tuning formula and the voicing are this repository's.  The article carries
  no licence beyond its copyright notice; what is used is its description of
  a technique, credited here.
- **Kevin Karplus and Alex Strong**, "Digital Synthesis of Plucked-String and
  Drum Timbres", *Computer Music Journal* 7(2), 1983 — the loop, and the drum
  blend (`Blend`).
- **Vesa Välimäki, Jyri Huopaniemi, Matti Karjalainen and Zoltán Jánosy**,
  "Physical Modeling of Plucked String Instruments with Application to
  Real-Time Sound Synthesis", *Journal of the Audio Engineering Society*
  44(5), 1996 — the loss filter designed from decay times.
- **David A. Jaffe and Julius O. Smith**, "Extensions of the Karplus-Strong
  Plucked-String Algorithm", *Computer Music Journal* 7(2), 1983 — the
  tuning by phase delay, the loss filter, the pick-position comb (`HitPos`).
- **Julius O. Smith III**, *Physical Audio Signal Processing*, CCRMA,
  Stanford — "Extended Karplus-Strong Algorithm" and "Allpass from Two
  Combs", the two pages the article cites.
- **Manfred R. Schroeder and Benjamin F. Logan**, "'Colorless' Artificial
  Reverberation", *IRE Transactions on Audio* AU-9(6), 1961, and
  **M. R. Schroeder**, "Natural Sounding Artificial Reverberation", *Journal
  of the Audio Engineering Society* 10(3), 1962 — the all-pass comb.
- Universal Audio, "Allpass Filters" (blog), cited by the article for the
  all-pass.

## What the article could give the other units

Considered, not done (each would change a unit that is voiced and verified):

- **BrcSnare**: its wires ring on the exciter's noise burst, so they stop
  when the burst does; the article's (and BrcLoop's) wires follow the head.
  A head-following drive would have to be off by default to keep its
  presets bit-identical to the original.
- **BrcWorld's `Wire`** (the berimbau) is a Karplus-Strong loop: the same
  all-pass would give the steel wire its stiffness (a slightly sharp
  upper series) at the cost of one more delay line per voice.
- **BrcTom / BrcKick glides**: they bend a modal bank; the loop's glide bends
  every partial at once for the price of one.  The kick's `Dive` already
  does what a deep loop glide would.

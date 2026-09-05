# drumlogue user unit level meter

Measures what a user synth unit actually puts on the bus, on the host, before it
goes anywhere near hardware.

## Why

A unit's *peak* level is not what decides whether it sits in a pattern. Loudness
is. The drumlogue's internal engines play back material that is already
peak-limited, so it carries a lot of RMS for its peak level. A user unit that
peaks at −0.1 dBFS but only averages −14 LUFS sounds completely fine when you
audition it on its own, and disappears under the drums the moment it is in a
pattern — which is exactly why this class of problem only shows up on hardware.

Once the peaks are at the ceiling, extra gain cannot fix that: it just moves the
clipping point down. The only way to raise loudness under a fixed ceiling is to
reduce the crest factor (see [`common/output_stage.h`](../../common/output_stage.h)).

## Requirements

```sh
sudo apt-get install g++-arm-linux-gnueabihf qemu-user
```

The unit is cross-compiled for Cortex-A7 with the same `-march`/`-mfpu` flags the
drumlogue Makefile uses and run under `qemu-arm`, so the NEON paths are the ones
that actually ship — no x86 fallback, no NEON shim.

## Use

```sh
./run.sh ../../EffeESP32                    # every preset, note 60, velocity 127
./run.sh ../../brachetti 60 127 8           # note, velocity, first 8 presets
./run.sh ../../EffeESP32 60 127 -1 - 0 59   # sweep parameter 0 in 59 steps
./run.sh ../../EffeMD 60 127 -1 ./wav       # also write one WAV per preset
```

Sweep mode (`<param-index> <steps>`) is for units whose sound is chosen by a
parameter rather than by a preset — `Instr` on EffeESP32 and EffeMD, `Prgrm` on
ScrutaAstri — and for mapping a gain control against loudness.

Each row is one preset, held for 2 s at the given note and velocity, then 1 s of
release:

| column | meaning |
| --- | --- |
| `peak dB` | sample peak. `0.00` means it is hard clipping. |
| `rms dB` | plain RMS over the window. |
| `LUFS` | gated ITU-R BS.1770-4 loudness. **This is the number that matters.** |
| `DC dB` | DC offset. Above roughly −40 dB it is eating headroom for nothing. |
| `crest` | peak minus RMS. |

`0 LUFS` is a full-scale sine on both channels. A drumlogue part wants roughly
**−9 LUFS** for one hit at velocity 127; that is where the units that sit
correctly in a pattern measure.

A `!! N non-finite samples` line under a row means the unit emitted NaN or Inf.

## Measured baseline

Every synth unit in this repository, all presets/instruments, note 60,
velocity 127:

| unit | peak dBFS | LUFS | mean LUFS | n |
| --- | --- | --- | --- | --- |
| ScrutaAstri *(before)* | −9.37 … **0.00** | −11.5 … +3.9 | **−0.7** | 24 programs |
| ScrutaAstri *(now)* | −11.29 … −0.97 | −13.5 … +2.0 | **−2.5** | 24 programs |
| EffeMD | −2.39 … −1.17 | −12.9 … −2.2 | **−8.6** | 13 instruments |
| Brachetti | −0.77 … −0.09 | −29.4 … −4.9 | **−13.5** | 40 presets |
| EffeESP32 *(before)* | −14.87 … **0.00** | −21.6 … −0.4 | **−13.5** | 59 instruments |
| EffeESP32 *(knee only)* | −9.86 … −0.16 | −16.6 … +1.9 | **−9.6** | 59 instruments |
| EffeESP32 *(bus limiter, gain 2.51)* | −9.86 … −0.24 | −20.6 … −2.2 | **−11.8** | 59 instruments |
| EffeESP32 *(now, gain 0.71)* | −20.6 … −0.35 | −27.6 … −6.0 | **−19.5** | 59 instruments |

Two things fall out of that table:

* The spread between units is about **13 LU**, and inside a single unit it is up
  to **25 LU**. There is no single global gain correction: ScrutaAstri is more
  than 12 LU *louder* than Brachetti, so anything added on top of it is pure
  distortion.
* In the *(before)* rows every unit but EffeESP32 already peaked within 0.8 dB of
  full scale, so a plain multiplier had nowhere to go. The deficit is crest
  factor, not level — which is what the output stage below addresses.

### Settled on hardware: do not chase this table with gain

The numbers above are worth having — they caught ScrutaAstri hard-clipping and
EffeESP32 sitting 4 LU low — but the *original* complaint they were gathered to
answer, that the synth track is quieter than the drumlogue's own instruments,
turned out not to be a unit-level problem at all. Measured with
[`levelref`](../../levelref) on hardware at matched faders:

* All four units were **indistinguishable in level by ear**, despite spanning
  11 LU in this table.
* **KORG's own official Nano synth is equally quiet.** A unit none of this code
  touches shows the same deficit, so the deficit is the drumlogue's user-synth
  track, not the units on it.
* The working fix is about a quarter turn more on the synth track's volume knob.

So use this table to stop a unit *wasting* the headroom it has — clipping, DC,
a dead volume control, an unintended 4 LU trim — and not to push a unit's mean
loudness upward. In particular **Brachetti is deliberately left alone.** Its
−29.4 … −4.9 LU spread reads like a defect and is not: every preset already
peaks within 0.8 dB of full scale, so the spread is crest factor, and the
high-crest presets are RimShot (42.1 dB), Wodblk (39.1), HHat-C (36.1) and
Cowbel (33.1) — clicks, which are supposed to be quiet on a loudness meter.
`output_stage.h` says the same thing in its own words: *"Do NOT normalise per
preset or per instrument: the level differences between a kick and a triangle
are musical."* The measured options for raising it are recorded below; all of
them buy an inaudible change at a measurable distortion cost.

## Harmonic cost — `harmonics.py`

Loudness says what a change to an output stage bought. It says nothing about
what it cost, and the two have to be read together: a memoryless waveshaper on a
low-frequency decay manufactures odd harmonics, which is the "brittle /
distorted long decay" failure mode, and no loudness metric shows it.

Render a baseline and a candidate to WAV and compare:

```sh
./run.sh ../../brachetti 60 127 3 /tmp/base
EXTRA_FLAGS=-DSOME_GAIN=2.0f ./run.sh ../../brachetti 60 127 3 /tmp/cand
./harmonics.py /tmp/base /tmp/cand
```

It locates the fundamental in the sustained body (skipping the first 40 ms so
the strike does not dominate), pins the candidate to the *baseline's*
fundamental so the two are comparable, and reports H2–H5 against it plus the
total energy above 250 Hz. `HARM_F0_BAND=25:300` overrides the search band.
Needs numpy.

## Calibrating a unit

1. `./run.sh ../../<unit>` and read the mean LUFS.
2. Route the unit's output through `dl::soft_knee()` / `dl::soft_knee_q()` from
   [`common/output_stage.h`](../../common/output_stage.h) instead of a hard clamp.
3. Raise the unit's output gain until the mean lands near −9 LUFS, then
   re-measure. Because the knee is bounded by 0.995 by construction, the trim is
   spent on RMS instead of on clipping.

Do **not** normalise per preset or per instrument. The level difference between
a kick and a triangle is musical; only the unit as a whole should be trimmed.

EffeESP32 was calibrated this way. `MASTER_GAIN` against the measured mean:

| `MASTER_GAIN` | mean LUFS | loudest preset LUFS |
| --- | --- | --- |
| 1.41 *(old)* | −13.6 | −0.9 |
| 2.00 | −11.0 | +0.9 |
| **2.51** *(shipped)* | **−9.6** | +1.9 |
| 3.16 | −8.2 | +2.6 |
| 3.98 | −7.0 | +3.3 |
| 5.01 | −6.0 | +3.9 |

At 2.51 the unit gained 3.9 LU **and** stopped clipping: 15 of its 59
instruments used to pin the hard clamp at 0.00 dBFS, and none do now.

**That table is superseded, and it is worth understanding why.** Read on its own
it says a memoryless knee bought 3.9 LU for free. It did not: it bought them by
waveshaping, and this meter cannot see the difference. At `MASTER_GAIN` 2.51 the
loudest EffeESP32 instruments were driving the knee up to 6.7× past full scale,
which is a distortion box, not a limiter — measured at 30–50 % THD+N on a
*single* Splash or ClHat hit, and worse on every stacked one. EffeESP32 now runs
a look-ahead limiter (`dl::PeakLimiter`) in front of the knee, which gives back
2.2 LU of the mean and 19–29 dB of distortion on the metallic instruments.
`MASTER_GAIN` has since gone the other way, to 0.71, for a reason this meter is
equally blind to. A limiter holds a signal at the ceiling for as long as the
signal is over it, so a hit arriving 6–17 dB past the ceiling comes out with its
envelope flat for as long as that takes to decay — a full second on a 6 s
cymbal. Loudness cannot see it; only the shape of the hit can. The measured
curve is in [EffeESP32's README](../../EffeESP32/README.md#output-stage).

The general lesson is the one this file already draws elsewhere: a loudness
number says what a change bought and never what it cost. Read it against
`harmonics.py` for single hits, and against
[`stack_meter`](../stack_meter) for polyphonic ones.
To try a different trim without editing the source:

```sh
EXTRA_FLAGS=-DMASTER_GAIN_OVERRIDE=3.16f ./run.sh ../../EffeESP32
```

ScrutaAstri went through the same stage. Its problem was the opposite one: the
double tanh ahead of the output is already bounded to ±0.9, and `Output_Gain_Boost`
drove that into a hard clamp at ±1.0, so 23 of 24 programs measured 0.00 dBFS at a
crest factor of 2.6–3.6 dB — a square wave. It also left 0.06–0.11 of DC on the bus
(−19 to −24 dBFS on nearly every program). With the DC blocker and the soft knee in
place nothing flat-tops any more (peaks −0.97 … −1.53 dBFS, crest 3.2–4.3 dB) and the
DC is down at −38 … −87 dBFS.

`Output_Gain_Boost` against the measured mean, if the level itself needs moving:

| `Output_Gain_Boost` | mean LUFS | loudest program LUFS |
| --- | --- | --- |
| **1.995** *(shipped)* | **−2.5** | +2.0 |
| 1.412 | −4.9 | −0.5 |
| 1.000 | −7.9 | −3.5 |
| 0.708 | −10.9 | −6.5 |
| 0.501 | −13.9 | −9.5 |

ScrutaAstri is a *continuous* drone, so it is not directly comparable with the
percussive units above: those measure the loudness of a single hit, which the
BS.1770 gate reports without the silence around it, while the drone is at its
measured level the whole time. A drone matched to a drum unit on this meter will
sit noticeably louder in a pattern, which is why it is left at the top of the
table rather than trimmed to the −9 LUFS house target.

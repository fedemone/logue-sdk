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
| ScrutaAstri | −9.37 … **0.00** | −11.5 … −0.3 | **−1.1** | 25 programs |
| EffeMD | −2.39 … **0.00** | −12.3 … −2.2 | **−8.3** | 13 instruments |
| Brachetti | −0.77 … −0.09 | −29.4 … −4.9 | **−13.5** | 40 presets |
| EffeESP32 *(before)* | −14.87 … **0.00** | −21.6 … −0.4 | **−13.5** | 59 instruments |
| EffeESP32 *(now)* | −9.86 … −0.16 | −16.6 … +1.9 | **−9.6** | 59 instruments |
| RipplerX | silent | — | — | 28 presets |

Two things fall out of that table:

* The spread between units is about **12 LU**, and inside a single unit it is up
  to **25 LU**. There is no single global gain correction: ScrutaAstri is more
  than 12 LU *louder* than Brachetti and already clipping, so anything added on
  top of it is pure distortion.
* Every unit except EffeESP32 already peaks within 0.8 dB of full scale, so a
  plain multiplier has nowhere to go. The deficit is crest factor, not level.

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
To try a different trim without editing the source:

```sh
EXTRA_FLAGS=-DMASTER_GAIN_OVERRIDE=3.16f ./run.sh ../../EffeESP32
```

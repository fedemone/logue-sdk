# drumlogue user unit polyphony meter

Measures what a user synth unit does when it is given more than one hit at a
time, on the host, before it goes anywhere near hardware.

## Why

[`level_meter`](../level_meter) next door asks what *one* hit puts on the bus.
That is the right question for calibrating a trim, and it is blind to the
failure this tool exists for.

A unit's output stage is usually calibrated against a single hit: pick a master
gain, run the result through a soft knee, read the loudness. A polyphonic unit
then breaks that calibration every time it plays a chord, because the bus level
is roughly proportional to how many voices are sounding. A memoryless
waveshaper — which is what a soft knee is — stops acting on peaks at that point
and starts bending the whole waveform.

How bad that sounds depends entirely on the material:

* One dominant low partial (a kick) gains harmonics of itself. That reads as
  "fat", and it is why the problem survives casual listening.
* A dense inharmonic spectrum (a cymbal, a gong, or simply several voices at
  once) gains an intermodulation product for **every pair of partials**. That is
  broadband and atonal, and it reads as harshness.

Cymbals are also the worst case for a second reason: their crest factor is tiny
(about 3 dB, against 14 dB for a kick), so the *entire* waveform sits above the
knee rather than just the transient.

None of this shows up in a loudness number. It shows up here.

## Requirements

```sh
sudo apt-get install g++-arm-linux-gnueabihf qemu-user
```

The unit is cross-compiled for Cortex-A7 with the same `-march`/`-mfpu` flags the
drumlogue Makefile uses and run under `qemu-arm`, so the NEON paths are the ones
that actually ship.

## Use

```sh
./run.sh ../../EffeESP32 poly 14 8          # 1..8 stacked voices of instrument 14
./run.sh ../../EffeESP32 roll 14 6 250      # 6 hits on one note, 250 ms apart
./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/a.f32
```

Arguments after the project directory are
`<mode> <instrument> <n> <gap-ms> <velocity> <seconds> [dump-path]`.
`SEL_PARAM` picks the parameter that selects the sound (default `0`, which is
`Instr` on EffeESP32 and EffeMD); `SEL_PARAM=-1` leaves every parameter at its
header default. `EXTRA_FLAGS` is appended to both compiles, for trying a
constant without editing the source.

### `poly` — stacking

`n` simultaneous note-ons on distinct notes, repeated for 1…`n` voices.

| column | meaning |
| --- | --- |
| `peak dB` / `rms dB` | of the whole render |
| `gain dB` | RMS delivered, against the one-voice render |
| `ideal dB` | `20·log10(n)`, what a linear mix would deliver |
| `dist dB` | distortion, **with level differences removed** |

`dist dB` is the number to read. A limiter legitimately turns a stack down, so
scoring the output against a fixed `n ×` reference would count a clean gain
reduction as if it were distortion. Instead the best scalar gain is fitted per
512-frame window (10.7 ms — long enough that the fit cannot absorb audio-rate
distortion, short enough to follow a limiter) and only the residual is counted.

Expect `gain dB` to fall short of `ideal dB` on any unit that limits: there is
no headroom above full scale, so a stack can only be delivered at the ceiling.
That is not a defect. `dist dB` climbing toward the signal is.

### `roll` — retriggering

`n` hits on the **same** note, `gap` ms apart. This is the drumlogue sequencer
path, where `unit_gate_on` fires the instrument's assigned note on every step.

Reports the RMS of the 1 ms either side of each retrigger, and their ratio. An
allocator that resets a voice while its tail is still loud steps straight to
silence and then climbs the new attack, so the ratio collapses; that step is the
click. A voice that is faded out instead keeps the ratio near 1.

Mono-per-note choke is deliberate on a drum part, so the tail RMS is expected to
stay put: what has to change is *how* the previous hit ended.

### `dump` + `compare.py` — absolute distortion

`poly` measures how distortion changes with voice count. It cannot see
distortion a unit already has at one voice, because the one-voice render is its
own reference. To measure that, render the same thing twice — once normally,
once with the master gain low enough that the output stage is linear — and
compare:

```sh
./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/proc.f32
EXTRA_FLAGS=-DMASTER_GAIN_OVERRIDE=0.01f \
  ./run.sh ../../EffeESP32 dump 14 4 0 127 2 /tmp/lin.f32
./compare.py /tmp/proc.f32 /tmp/lin.f32 251 32
```

`251` is `2.51/0.01`, the gain the reference was rendered at. `32` is the
look-ahead delay in frames the processed build adds, `0` if it has none — get
this wrong and the fit scores a phase error as distortion. A fifth argument sets
the fit window in frames (default 512); if a residual collapses when you shorten
it, the residual was gain movement rather than waveshaping.

The linear reference must come from a build with **no look-ahead stage**, or the
delay has to be accounted for on both sides.

## Measured: EffeESP32

The unit this tool was written for. Distortion against the linear voice mix,
velocity 127, 512-frame windows:

| instrument | voices | before | after |
| --- | ---: | ---: | ---: |
| Splash | 1 | −9.9 dB | **−38.8 dB** |
| Splash | 4 | −6.2 dB | **−37.6 dB** |
| ChinaCy | 1 | −11.7 dB | **−35.9 dB** |
| ChinaCy | 4 | −6.4 dB | **−35.6 dB** |
| Crash1 | 1 | −17.1 dB | **−36.2 dB** |
| Crash1 | 4 | −8.7 dB | **−35.5 dB** |
| ClHat | 1 | −9.8 dB | −17.5 dB |
| ClHat | 4 | −6.2 dB | −17.1 dB |
| Kick | 1 | −17.5 dB | −19.6 dB |
| Kick | 4 | −10.9 dB | −20.8 dB |
| SubKick | 4 | −12.0 dB | −25.8 dB |

Kick and ClHat look like the weak rows and are not: shortening the fit window to
64 frames moves the new Kick figure to −33.7 dB and ClHat to −27.3 dB while
leaving the *before* figures where they are (−26.3 and −10.3). The residual
there is the limiter's gain moving inside the window, not a bent waveform.
`level_meter/harmonics.py` confirms it from the other direction: on the Kick's
49.8 Hz decay the energy above 250 Hz — the "brittle distorted decay" signature —
drops by **39.1 dB**.

The cost was 2.2 LU of measured loudness (mean −9.57 → −11.75 LUFS), which is
the part of the old level that was manufactured distortion. Peaks went from
−0.16 … −2.7 dBFS to a uniform ceiling at −0.24 dBFS.

# LevelRef — calibrated reference signal for the drumlogue synth track

> **Disclaimer:** LevelRef is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

A test unit, not an instrument. It emits a signal of a **known loudness**, so
"the synth track is too quiet" can be turned into a number.

## Why

[`tools/level_meter`](../tools/level_meter) measures exactly what a unit puts on
the bus, on the host. What it cannot measure is the two things on the other side
of that bus:

* what the drumlogue does with a user synth unit's output afterwards, and
* how loud the drum tracks it is competing with actually are.

Those two unknowns are why raising a unit's gain is guesswork — and why the
guess can be wrong in both directions. ScrutaAstri used to peak at **0.00 dBFS
on 23 of its 24 programs** and still felt short against the drums; no amount of
gain fixes something that is already on the rails. LevelRef removes the unknowns
by putting a signal of known loudness on the synth track.

## Procedure

1. Copy `levelref.drmlgunit` to `Units/Synths/` and load it on the **synth
   track**.
2. `Signal = PinkNz`, `Mode = Drone`, `TgtLUFS = -20`. It sounds immediately —
   no note, no sequencer step required.
3. Put a drum pattern on another track. Set **both** track faders to the same
   position, and note where they are.
4. Turn `TgtLUFS` until the noise and the drums sound equally loud.

The value you land on **is the drum bus's loudness**, in the same units the
meter reports for every synth unit here. Then:

* If it lands near, say, −10 LUFS, the synth units measured below are genuinely
  short by the difference, and calibrating them is the fix.
* If it lands near a unit's measured value and the balance is *still* wrong at
  matched fader positions, the deficit is not inside the unit, and no amount of
  gain in the unit will recover it. That result is worth just as much: it stops
  the search in the right place.

Use `Sine100` instead of `PinkNz` to compare specifically against a kick — a
level problem that is really a low-frequency masking problem shows up as a large
disagreement between the two answers.

## Parameters

| # | Name | Range | Meaning |
|---|---|---|---|
| 0 | `Signal` | PinkNz / Sine1k / Sine100 / WhitNz / Silence | Which reference to generate. Pink noise is the default: it is the only one whose spectrum resembles the material it is being compared against, so it is the one to judge by ear |
| 1 | `TgtLUFS` | −40 … 0 | **The loudness the unit produces**, gated BS.1770. Calibrated per signal, so the number on the screen is the number a meter reads back |
| 2 | `Mode` | Drone / Gated | Drone sounds continuously from load; Gated follows note on/off |
| 3 | `PeakdB` | −99 … 0 | Read-out: the peak level the current setting produces, in dBFS |

`TgtLUFS` **stops** at the loudest value each signal can reach with its peak
still under full scale — a reference that distorts is worse than no reference.
That ceiling is the signal's crest factor, not a scaling choice:

| Signal | crest | highest exact `TgtLUFS` | peak there |
|---|---|---|---|
| PinkNz | 12.1 dB | **−10** | −0.9 dBFS |
| Sine1k | 3.0 dB | **0** | −0.01 dBFS |
| Sine100 | 3.0 dB | **−2** | −0.2 dBFS |
| WhitNz | 4.8 dB | **0** | −1.4 dBFS |

Velocity is deliberately ignored: a reference whose level depended on how the
sequencer was programmed would not be a reference.

## Verification

Measured with `tools/level_meter` (the unit cross-compiled for Cortex-A7 and run
under qemu, the same binary path that ships). Every reachable setting is exact:

```
$ cd ../tools/level_meter && ./run.sh ../../levelref 60 127 -1 - 1 9
prst name        peak dB    rms dB      LUFS     DC dB    crest
0    val=-40      -30.89    -42.96    -40.00    -73.38     12.1
1    val=-35      -25.89    -37.96    -35.00    -68.38     12.1
2    val=-30      -20.89    -32.96    -30.00    -63.38     12.1
3    val=-25      -15.89    -27.96    -25.00    -58.38     12.1
4    val=-20      -10.89    -22.96    -20.00    -53.38     12.1
5    val=-15       -5.89    -17.96    -15.00    -48.38     12.1
6    val=-10       -0.89    -12.96    -10.00    -43.38     12.1
```

and all four signals agree at a common target:

```
$ ./run.sh ../../levelref 60 127 -1 - 0 5
0    0:PinkNz     -10.89    -22.96    -20.00    -53.38     12.1
1    1:Sine1k     -20.01    -23.02    -20.00   -143.87      3.0
2    2:Sine100    -18.17    -21.18    -20.00   -132.01      3.0
3    3:WhitNz     -21.38    -26.14    -20.00    -81.91      4.8
```

The per-signal calibration constants in `synth.h` (`kSignalLufsAtUnity`,
`kSignalPeakAtUnityDb`) were **measured against this exact generator**, not
derived on paper. Re-measure both if a generator changes.

## What to compare against

Every synth unit in this repository, one hit at note 60 velocity 127, gated
BS.1770 (from [`tools/level_meter/README.md`](../tools/level_meter/README.md)):

| unit | mean LUFS | loudest preset | quietest preset |
|---|---|---|---|
| ScrutaAstri | −2.5 | +2.0 | −13.5 |
| EffeMD | −8.6 | −2.2 | −12.9 |
| EffeESP32 | −9.6 | +1.9 | −16.6 |
| Brachetti | −13.2 | −4.9 | −29.4 |

Two cautions when reading that table against LevelRef:

* Those are **one-shot** measurements. BS.1770 gating reports the loudness of
  the hit without the silence around it, so a percussive unit "at −9 LUFS" is
  only that loud during the hit, while LevelRef in Drone mode is at its level
  continuously. Expect a drone to need a *lower* number than a percussive unit
  to sound equally present.
* Brachetti's spread is not a gain problem. Every one of its 40 presets already
  peaks within 0.8 dB of full scale; the range from −4.9 to −29.4 LUFS is crest
  factor. Raising its bus gain from 2.3 to 6.5 (**+9 dB**) buys 2.3 LU of mean
  loudness and moves its quietest preset by 0.02 LU — the limiter absorbs the
  rest. Measured, not assumed.

## Building

```sh
cd platform/drumlogue/levelref
make            # via the SDK docker builder, or:
make CROSS_COMPILE=arm-linux-gnueabihf-
```

Produces `build/levelref.drmlgunit` → `Units/Synths/`.

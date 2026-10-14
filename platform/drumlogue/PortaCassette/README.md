# PortaCassette (`PortaK7`)

> **Disclaimer:** PortaCassette is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

A Tascam Portastudio / vinyl emulation master effect for the KORG drumlogue.
Four Portastudio voicings and a vinyl one, a console stage that clips like the
channel strip's op-amps, a 3-band parametric EQ, tape saturation that runs out
of treble first, wow & flutter, crosstalk, noise, and a dbx-style companding
noise-reduction loop that hears — and exaggerates — whatever the tape does to
the signal, as a real one does.

## Signal chain

Everything runs four frames at a time on NEON:

```
Trim -> Console clip (4x) -> 20 kHz -> PEQ (3-band) -> dbx encode
     -> Record EQ -> Drive / Saturation (4x) -> Playback EQ
     -> Hiss or Vinyl dust -> Head bump -> Tape LPF -> Wow & Flutter
     -> Crosstalk -> dbx decode -> Playback gain -> DC block -> Dry/Wet
     -> Soft ceiling
```

Everything between the encoder and the decoder is "the tape": its noise, its
saturation, its frequency response, its transport.  The decoder has its own
detector, so it hears all of that and reacts to it, which is where a dbx
Portastudio gets both its quiet and its pumping.

## Parameters

| Page | Parameter | Range | Notes |
| --- | --- | --- | --- |
| 1 | Age | 0–100 % | HF loss, wow/flutter depth, hiss floor, dust density, groove wear |
| 1 | Mix | 0–100 % | Bit-transparent at 0 |
| 1 | Trim | 0–100 % | 0 to +30 dB into the console op-amps, half-power fader make-up; see [Console](#console) |
| 1 | Drive | 0–100 % | 1x to 12x into the tape, exponential, half-power make-up |
| 2 | dbx NR | Active / EncOnly / Off / DecOnly | See [The dbx section](#the-dbx-section) |
| 2 | Model | T-244 / T-424 / T-488 / Vinyl / T-424N | See [The machines](#the-machines) |
| 2 | LowHz / LowGain | 20–400 Hz, ±12 dB | Peaking, Q 1.0 |
| 3 | MidHz / MidGain | 200–5000 Hz, ±12 dB | Peaking, Q 1.5 |
| 3 | HiHz / HiGain | 2–15 kHz, ±12 dB | Peaking, Q 1.0 |
| 4 | X-talk | 0–10 % | Treble-biased inter-track bleed; 1 is the published separation |
| 4 | Hiss | 0–100 % | Tape bias noise (vinyl model uses dust instead) |
| 4 | Attack | 0.1–100 ms | dbx gain ballistics, gain falling |
| 4 | Release | 10–2000 ms | dbx gain ballistics, gain rising |

A band sitting at exactly 0 dB is bypassed rather than run as a unity filter,
so the default flat patch costs nothing for the EQ.

New `dbx NR` and `Model` entries were appended, so a value saved by an earlier
version still selects what it did.  Two defaults moved: `X-talk` is 1 (it was
5, 17 dB more bleed than any of the machines), and `PreAmp` became `Trim`
(the default 20 % still gives the +3 dB it used to, but the range now reaches
the clip).

## The machines

The three Tascams share a tape format, a speed (9.5 cm/s) and a noise-reduction
system, so bandwidth is not what separates them — and the 424 is the *widest*
of the three, not the dullest. Published figures:

| | 244 (1982) | 424 (1990s) | 488 (1988) | 424 at NORMAL |
| --- | --- | --- | --- | --- |
| Speed | 9.5 cm/s | 9.5 cm/s | 9.5 cm/s | 4.8 cm/s |
| Response | 40 Hz–14 kHz | 40 Hz–16 kHz ±3 dB | 40 Hz–14 kHz ±3 dB | 40 Hz–10 kHz ±3 dB |
| Wow & flutter | 0.06 % | 0.05 % WRMS | 0.04 % WRMS | 0.06 % WRMS |
| THD | 1.5 % | 1.0 % (MkII, 1 kHz) | 1.3 % | — |
| S/N, no dbx | — | 55 dB unweighted | — | 54 dB unweighted |
| Separation | — | 55 dB or better | 50 dB @ 1 kHz, no dbx | — |

What that makes them:

* **T-244** — least stable transport and the most distortion. Most of why it
  reads as the lo-fi one.
* **T-424** — the later, cleaner machine: widest response, quietest, steadier
  than the 244.
* **T-488** — eight tracks in the same 3.81 mm of tape, so each is about half
  a four-track's width. That costs roughly 3 dB of signal-to-noise and puts
  the tracks close enough together to hit the 50 dB separation in the manual.
  Its transport is the best of the three.
* **T-424N** — the 424 switched to NORMAL speed.  Half the speed halves every
  wavelength: 10 kHz of bandwidth, a head bump an octave lower, wow and
  flutter at half the rate, and much less treble headroom (below).

Measured on the built unit at Age 10 %, dbx Off, Drive 0 % (`test/run.sh table
noise xtalk`; Hiss 15 % for the noise floor):

| model | 14 kHz re 1 kHz | noise floor | separation @ 1 kHz, X-talk 1 | W&F (RMS, unweighted) |
| --- | --- | --- | --- | --- |
| T-244 | −4.2 dB | −55.7 dBFS | 53.6 dB | 2453 ppm |
| T-424 | −2.1 dB | −58.4 dBFS | 55.0 dB | 2026 ppm |
| T-488 | −4.2 dB | −52.6 dBFS | 50.0 dB | 1619 ppm |
| T-424N | −11.3 dB | −57.4 dBFS | 55.0 dB | 2460 ppm |
| Vinyl | −7.2 dB | — | — | — |

The bandwidth spread is deliberately small, because the real machines' is.
The separation you hear is mostly noise, crosstalk, transport stability and,
now, how early each runs out of treble.

The wow & flutter column is not comparable with the published figures, which
are weighted (WRMS); it is also not something this revision touched.  Even
allowing for the weighting it runs well above the specifications, which makes
it a voicing choice rather than a measurement.

## Saturation

Cassette saturation has three properties this unit models, measured with
`test/run.sh calib onset alias track`.

**It is mostly third harmonic, and it comes on gradually.**  The saturator is
a rational fit to tanh (within 0.5 % of it): odd harmonics only, the third
rising 2 dB per dB of level, about 5 dB from 1 % to the 3 % that defines a
tape's maximum output level.  THD of the tape at the reference level (997 Hz,
−18 dBFS RMS reaching the tape, i.e. Trim 0 %; Drive 40 %, the default):

| | tape (dbx Off) | dbx Active | published THD |
| --- | --- | --- | --- |
| T-244 | 1.53 % | 1.09 % | 1.5 % |
| T-424 | 1.00 % | 0.71 % | 1.0 % (424 MkII) |
| T-488 | 1.25 % | 0.89 % | 1.3 % |

With dbx the third harmonic comes out lower: it is made on tape from a
pre-emphasised signal and then goes through the de-emphasis.  The default
Trim (20 %) adds 3 dB on the way in, so a −18 dBFS RMS input reaches the tape
at −15.

**It runs out of treble first.**  On Type II tape at 4.76 cm/s the 10 kHz
saturation level sits about 16 dB below the 400 Hz maximum output level.  The
deck's record EQ boosts the treble into the tape and its playback EQ takes it
out again; a quiet signal comes back flat, a loud one with its treble
squashed.  The unit does the same with a first-order pre-emphasis around the
saturator: about +10 dB at 10 kHz at HIGH speed (a 10 kHz wavelength at 9.5
cm/s is a 5 kHz one at 4.76 cm/s, which roughly halves the gap), +16 dB on the
T-424N, levelling off at +16 / +22 dB.  Input level at which each tone is compressed by 1 dB (T-244, Drive
40 %, Trim 0 %):

| | 61 Hz | 211 Hz | 997 Hz | 4993 Hz | 9973 Hz |
| --- | --- | --- | --- | --- | --- |
| before, dbx Off | −14.6 | −11.8 | −11.7 | −11.7 | −11.7 |
| now, dbx Off | −11.6 | −11.7 | −12.0 | −16.8 | −21.6 |
| now, dbx Active | −13.2 | −12.6 | −13.3 | −24.0 | −31.5 |
| now, T-424N, dbx Off | −9.7 | −9.8 | −11.1 | −19.9 | −25.3 |

The old unit saturated every frequency at the same level except the bass,
which went *first*: the head bump sat in front of the saturator and drove the
kick 3–5 dB harder.  The head bump is the playback head's response, so it now
comes after the tape.  (Its centre had also drifted: the coefficient code used
a fast cosine whose 1.3e-5 error moved the 60 Hz bump to 72 Hz; it uses libm
now.)

**It cannot alias.**  Tape's harmonics of treble lie above its own bandwidth.
A waveshaper at 48 kHz folds them back as inharmonic tones — a 10 kHz partial's
5th harmonic lands at 1.9 kHz.  The saturator now runs at 4x between polyphase
IIR halfband filters (`halfband.h`).  Strongest inharmonic product of a
9973 Hz tone at −12 dBFS peak, T-244:

| | before | now |
| --- | --- | --- |
| dbx Active, Drive 40 % | −39.7 dBc | −65.4 dBc |
| dbx Off, Drive 40 % | −36.6 dBc | −70.2 dBc |
| dbx Active, Drive 100 % | −14.4 dBc | −18.4 dBc |

Drive 100 % with a loud treble tone is still a fuzz box: the pre-emphasis puts
that tone 30 dB past the knee, the saturator returns something close to a
square wave, and its 19th harmonic folds back even at 4x.

Everything the unit adds between 100 Hz and 19 kHz to a top-octave tone at
−24 dBFS peak, default patch (dBFS):

| | before, dbx Active | now, dbx Active | before, dbx Off | now, dbx Off |
| --- | --- | --- | --- | --- |
| 14983 Hz | −49.5 | −77.6 | −61.9 | −94.1 |
| 16993 Hz | −49.3 | −65.6 | −61.9 | −85.1 |
| 19997 Hz | −55.1 | −62.7 | −63.7 | −96.6 |
| 21503 Hz | −62.2 | −46.2 | −70.7 | −86.8 |

The one row that got worse is an isolated tone above 20 kHz with dbx Active.
The encoder's detector is band-limited to 10 kHz, as Type II's is, so it
barely hears such a tone and gives it the full +12 dB; the record EQ adds 16,
and the result is driven hard enough to fold at 192 kHz.  Real programme
material always has something below 10 kHz to set the gain.

## Console

The Portastudio's channel strip is a chain of op-amp stages — Trim, EQ,
fader, buffer — and cranking the Trim makes it clip at the rails: a bright,
hard, crunchy distortion quite unlike the tape's (DIYRE's Distortastudio
teardown of a 464 describes the two as "bright, crunchy" against "dark,
gooshy").  `Trim` drives that stage from 0 to +30 dB.  The 424 MkII takes
+10 dBV at minimum Trim against a −10 dBV nominal level, so the clip sits
20 dB above nominal: here, a sine at +2 dBFS RMS after the Trim.  A half-power
fader make-up follows (the clean level climbs 15 dB over the range), then the
mixer's 20 kHz bandwidth, then the EQ.

997 Hz at −15 dBFS peak, dbx Off, Drive 0 %:

| Trim | gain into the op-amps | THD | H5 |
| --- | --- | --- | --- |
| 20 % (default) | +6 dB | 0.44 % | −86 dBc |
| 60 % | +18 dB | 1.64 % | −70 dBc |
| 70 % | +21 dB | 6.0 % | −37 dBc |
| 100 % | +30 dB | 31 % | −18 dBc |

Below about 65 % what you hear on this tone is the tape getting a hotter
signal; above it, the console.  At the default 20 % the console starts to
round off input peaks above about −3 dBFS.  The clip runs at 4x too, and a
5 kHz tone at full Trim leaves its worst alias at −61 dBc.

## Noise

Tape noise is injected after saturation (so Drive does not amplify it), before
the playback roll-off (so the machine band-limits it, as it does its own
noise), and before the decoder (so dbx reduces it). With dbx Active the floor
sits around −73 dBFS; switch to Off or EncOnly and you get the unreduced
−56 dBFS of a real cassette, which is the point of the mode.

## The dbx section

Modelled as a real compander rather than as a compressor bolted on:

* **Encoder and decoder** — a dbx encoder is the exact inverse of its decoder
  because both detectors read the same signal: the encoder's listens to its
  own compressed output (feedback), the decoder's to its input (feed-forward),
  and on tape those are the same thing.  So the two halves cancel without
  sharing any state, and whatever the tape does in between reaches the
  decoder.  This unit used to take the decode gain as 1/encode gain, which
  cancels exactly but hides the tape from the decoder.
* **Detector** — a fixed-window RMS average of the power, 12 ms wideband and
  3 ms for the Type II HF path (high-passed at 2 kHz, blended 70/30), after a
  30 Hz–10 kHz control-path band limit: Type II rolls its detector off at the
  band edges so that rumble and the treble a tape loses do not steer the gain.
  Fixed windows matter: putting a user-settable 3 ms attack straight onto the
  power makes the detector chase the ripple of a bass note and modulate its
  own gain at twice the note frequency.
* **Gain computer** — `gain = (env / ref)^(-1/2)` of the compressed signal's
  power, which in steady state is a true 2:1 in dB, referenced to a nominal
  −18 dBFS RMS rather than to full scale. Referencing to 0 dBFS means any
  realistic master-bus level asks for more than the encode ceiling, which
  turns the "compressor" into a fixed boost.  (The detector sums L and R, so
  for a centred source the unity point sits nearer −19.5 dBFS RMS per channel.)
* **Ballistics** — Attack/Release smooth the *gain*, serially per sample, in
  both halves alike.
* **Calibration** — the tape between them runs at unity gain for a quiet
  signal, as a calibrated deck does; Drive's make-up is applied after the
  decoder.  Otherwise the decoder would double it.  Vinyl's shaper, whose
  slope at the groove-wear bias is below one, divides that slope out inside
  the loop and gets it back after the decoder (`test/run.sh unity`: every
  model within 0.06 dB for a quiet tone, vinyl within 0.2 dB).

### What the decoder does with the tape

A 1:2 expander doubles in dB any level error the tape makes.  Pushed into
saturation, a dbx Portastudio squashes about twice as hard as its tape does on
its own (997 Hz, T-244, Drive 40 %):

| THD | compression, dbx Off | compression, dbx Active |
| --- | --- | --- |
| 1.5 % | 0.52 dB | 1.16 dB |
| 2.8 % | 0.99 dB | 2.26 dB |
| 5 % | 1.82 dB | 4.15 dB |

The same goes for the tape's frequency response: with dbx Active the head
bump is about +3.6 dB at 61 Hz (+1.6 dB with dbx Off), and the treble a loud
signal loses on tape it loses twice.  That is why the head bump in the machine
table is half what it was: on a tone, with dbx Active, the bump is where it
used to be.  On programme, bass near the bump also lifts the decoder's gain
for everything else (+1.7 dB on a 1 kHz tone under a −12 dBFS 61 Hz one, Drive 0 %).

Two consequences are audible.  The mechanism of both is real dbx mistracking;
how long the first one lasts here is the unit's Release setting:

* **Transients after silence.**  The encoder's gain rises to its +12 dB limit
  on silence, so the first milliseconds of a hit reach the tape hot and
  saturate.  The decoder hears less than the encoder sent, and comes back
  short: after a 997 Hz burst at the reference level the output dips about
  2.5 dB and recovers over the Release time (−1.1 dB at 128 ms; the old unit
  recovered in 16 ms).  Lower Drive or a shorter Release shrinks it.
* **Pumping.**  A kick that saturates the tape moves everything else with it.
  55 Hz kicks at −6 dBFS peak over a −36 dBFS 8 kHz tone, default patch: the
  8 kHz tone dips 15.0 dB with dbx Off (the tape's own cross-modulation) and
  17.3 dB with dbx Active, then swells 2 dB for a few hundred milliseconds as
  the decoder follows the kick down.  The old unit dipped 22.3 dB (Off) and
  19.4 dB (Active), mostly because its head bump drove the kick harder into
  saturation; the new one pumps less either way.

### Modes

| Mode | Record | Playback | Result |
| --- | --- | --- | --- |
| Active | encoder | decoder | quiet, with the pumping above |
| EncOnly | encoder | — | bright and squashed: the "makeshift aural exciter" |
| Off | — | — | the tape alone, −56 dBFS of hiss |
| DecOnly | — | decoder | dark and over-dynamic: "lots of bass and no high-end" |

EncOnly and DecOnly are the classic mismatched-dbx tricks; the quotes are
from a homerecording.com thread on exactly that.  Measured against Off (T-244,
Drive 0 %):

| input | EncOnly 1 kHz | EncOnly 10 kHz | DecOnly 1 kHz | DecOnly 10 kHz |
| --- | --- | --- | --- | --- |
| −45 dBFS | +6.2 dB | +11.7 dB | −6.2 dB | −11.7 dB |
| −15 dBFS | −0.7 dB | +2.3 dB | +1.3 dB | −7.1 dB |
| −3 dBFS | −5.1 dB | −0.1 dB | +5.9 dB | −0.4 dB |

Heard on their own, both halves are limited to ±6 dB of gain (±12 dB when
they run as a pair); a real encoder's range is much wider, which is what makes
a genuinely undecoded dbx tape close to unlistenable.

## Building

```
CROSS_COMPILE=arm-linux-gnueabihf- make
```

Or use the SDK's Docker image (see `platform/drumlogue/README.md`). The unit
builds to `build/portacassette.drmlgunit`.

Recent GCC rejects the SDK Makefile's `--param max-inline-insns-single`
value as too large; if you hit that, override `USE_COPT` / `USE_CXXOPT` on the
command line without the `-finline-limit` / `--param` entries.

The unit builds at the SDK default, `-Os`.  To check what a `.drmlgunit` was
built with:

```
strings portacassette.drmlgunit | grep "build:"
build: -Os (size), gcc 13.3.0
```

**CPU.** ARM instructions per 64-frame render at `-Os`, counted from a QEMU
trace with the parser reading every instruction of each translated block:

| | before | now |
| --- | --- | --- |
| dbx Active (default) | 22.6k | 47.2k |
| dbx Off | 17.7k | 37.3k |
| Vinyl, dbx Off | 19.9k | 36.9k |

Of the 24k added in the default patch, the two 4x oversamplers take about 14k,
the compander's second detector and the control-path filters about 5k, and the
record/playback EQ, the mixer band-limit and the stage calls the rest.  The
oversampled stages and the compander are out of line: inlined into
`ProcessQuad`, the oversamplers' state spilled to the stack around every
allpass section and cost twice as much.  The deepest call chain uses about
580 bytes of stack (`ProcessQuad` 256 + the tape stage 320).

**Input channels.** A drumlogue master effect receives four input channels
per frame: main L, main R, sidechain L, sidechain R.  This unit used to read
its input as stereo pairs, so on a four-channel bus it took each frame's
sidechain pair for the next frame and only got through half the buffer.  It
now steps through the input at the width the runtime declares (2 or 4) and
uses the main pair; measured, its output is identical either way.

## Testing

There is no on-device test harness. The DSP is plain C++ over ARM NEON
intrinsics, so `test/` cross-compiles it for the Cortex-A7 and runs it under
`qemu-arm`:

```
sudo apt-get install g++-arm-linux-gnueabihf qemu-user python3-numpy
test/run.sh                 # every measurement on this page
test/run.sh onset alias     # just these
SRC=/tmp/old test/run.sh    # the same measurements on another copy of the sources
```

`probe.cc` renders test tones through the unit (any parameter, two tones, a
gated start, a train of kick-like hits, one channel only for crosstalk);
`measure.py` analyses them.  Tone tests run with wow & flutter switched off and
through a build whose output ceiling is out of reach
(`-DPC_CEILING_KNEE_OVERRIDE`), so they describe the tape rather than the
limiter behind it.  Measured against an older copy of the sources, rows for a
model or dbx mode it does not have print `n/a`, and if it predates the
ceiling hook `run.sh` says so and measures with the ceiling in.

## Sources

* Tascam 424 MkII specifications: <https://homerecording.com/tas424specs.html>
* Tascam 244 specifications: <https://www.muzines.co.uk/articles/tascam-244/4323>
* Cassette MOL/SOL and record EQ: <https://en.wikipedia.org/wiki/Compact_Cassette_tape_types_and_formulations>
* dbx Type I/II: <https://en.wikipedia.org/wiki/Dbx_(noise_reduction)>
* Mismatched dbx (EncOnly / DecOnly): <https://homerecording.com/bbs/threads/dbx-makes-my-recordings-to-sound-like-underwater.392339/>, <https://www.tascamforums.com/threads/dbx-question.11180/>
* Portastudio console distortion: DIYRE's Distortastudio design posts, <https://www.diyrecordingequipment.com/blogs/news/18050732-distortastudio-colour-design-pt-2>
* Polyphase IIR halfband design: Laurent de Soras, HIIR

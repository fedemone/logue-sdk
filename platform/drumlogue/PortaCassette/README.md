# PortaCassette (`PortaK7`)

A Tascam Portastudio / vinyl emulation master effect for the KORG drumlogue.
Four machine voicings, a 3-band parametric EQ, tape saturation, wow & flutter,
crosstalk, noise, and a dbx-style companding noise-reduction loop that the rest
of the chain sits inside.

## Signal chain

Everything runs four frames at a time on NEON:

```
PreAmp → PEQ (3-band) → dbx encode → Head bump → Drive / Saturation
       → Tape LPF → Hiss or Vinyl dust → Wow & Flutter → Crosstalk
       → dbx decode → DC block → Dry/Wet → Soft ceiling
```

The noise sources sit **inside** the encode/decode loop deliberately. That is
where tape noise enters on a real machine, and it is what lets the decoder pull
it back down — with dbx Active the hiss floor measures about 17 dB below the
same setting with dbx Off, which is the whole point of the system.

## Parameters

| Page | Parameter | Range | Notes |
| --- | --- | --- | --- |
| 1 | Age | 0–100 % | HF loss, wow/flutter depth, hiss floor, dust density, groove wear |
| 1 | Mix | 0–100 % | Bit-transparent at 0 |
| 1 | PreAmp | 0–100 % | 1.0× to 3.0× into the EQ |
| 1 | Drive | 0–100 % | 1x to 12x into the saturator, exponential, half-power make-up |
| 2 | dbx NR | Active / EncOnly / Off | See below |
| 2 | Model | T-244 / T-424 / T-488 / Vinyl | See below |
| 2 | LowHz / LowGain | 20–400 Hz, ±12 dB | Peaking, Q 1.0 |
| 3 | MidHz / MidGain | 200–5000 Hz, ±12 dB | Peaking, Q 1.5 |
| 3 | HiHz / HiGain | 2–15 kHz, ±12 dB | Peaking, Q 1.0 |
| 4 | X-talk | 0–10 % | Treble-biased inter-track bleed |
| 4 | Hiss | 0–100 % | Tape bias noise (vinyl model uses dust instead) |
| 4 | Attack | 0.1–100 ms | dbx gain ballistics, gain falling |
| 4 | Release | 10–2000 ms | dbx gain ballistics, gain rising |

A band sitting at exactly 0 dB is bypassed rather than run as a unity filter,
so the default flat patch costs nothing for the EQ.

## The machines

The three Tascams share a tape format, a speed (9.5 cm/s) and a noise-reduction
system, so bandwidth is not what separates them — and the 424 is the *widest*
of the three, not the dullest. Published figures:

| | 244 (1982) | 424 (1990s) | 488 (1988) |
| --- | --- | --- | --- |
| Response | 40 Hz–14 kHz | 40 Hz–16 kHz ±3 dB | 40 Hz–14 kHz ±3 dB |
| Wow & flutter | 0.06 % | < 0.05 % WRMS | 0.04 % WRMS |
| THD | 1.5 % | — | 1.3 % |
| Crosstalk | — | — | 50 dB @ 1 kHz, no dbx |

What that makes them:

* **T-244** — least stable transport and the most distortion. Most of why it
  reads as the lo-fi one.
* **T-424** — the later, cleaner machine: widest response, quietest, steadier
  than the 244.
* **T-488** — eight tracks in the same 3.81 mm of tape, so each is about half
  a four-track's width. That costs roughly 3 dB of signal-to-noise and puts
  the tracks close enough together to hit the 50 dB separation in the manual.
  Its transport is the best of the three.

Measured on the built unit at Age 10 %, dbx off:

| model | 14 kHz | noise floor | crosstalk | wow |
| --- | --- | --- | --- | --- |
| T-244 | −2.3 dB | −55.7 dB | −26.3 dB | 3656 ppm |
| T-424 | −1.5 dB | −58.4 dB | −27.7 dB | 3037 ppm |
| T-488 | −2.7 dB | −52.6 dB | −19.4 dB | 2452 ppm |
| Vinyl | −6.8 dB | −64.3 dB | −24.0 dB | 4651 ppm |

The bandwidth spread is deliberately small, because the real machines' is.
The separation you hear is mostly noise, crosstalk and transport stability.

## Noise

Tape noise is injected after saturation (so Drive does not amplify it), before
the playback roll-off (so the machine band-limits it, as it does its own
noise), and before the decoder (so dbx reduces it). With dbx Active the floor
sits around −73 dBFS; switch to Off or EncOnly and you get the unreduced
−56 dBFS of a real cassette, which is the point of the mode.

## The dbx section

Modelled as a real compander rather than as a compressor bolted on:

* **Detector** — a fixed-window RMS average of the power, 12 ms wideband and
  3 ms for the Type II HF path (high-passed at 2 kHz, blended 70/30). Fixed
  windows matter: putting a user-settable 3 ms attack straight onto the power
  makes the detector chase the ripple of a bass note and modulate its own gain
  at twice the note frequency.
* **Gain computer** — `gain = (env / ref)^(-1/4)`, a true 2:1 slope in
  amplitude, referenced to −18 dBFS RMS rather than to full scale. Referencing
  to 0 dBFS means any realistic master-bus level asks for more than the encode
  ceiling, which turns the "compressor" into a fixed boost.
* **Ballistics** — Attack/Release smooth the *gain*, serially per sample.
* **Decode** — derived as `1 / encode`, so the pair is exactly unity and the
  audible effect is entirely in how hard the tape stage is driven and in how
  much of the injected noise survives.

`EncOnly` is the classic Portastudio abuse trick: encode to tape, play back
with the decoder out of circuit, and keep the bright, squashed result.

## Building

```
CROSS_COMPILE=arm-linux-gnueabihf- make
```

Or use the SDK's Docker image (see `platform/drumlogue/README.md`). The unit
builds to `build/portacassette.drmlgunit`.

Recent GCC rejects the SDK Makefile's `--param max-inline-insns-single`
value as too large; if you hit that, override `USE_COPT` / `USE_CXXOPT` on the
command line without the `-finline-limit` / `--param` entries.

## Testing

There is no on-device test harness. The DSP is plain C++ over ARM NEON
intrinsics, so it can be exercised on a host by cross-compiling
`PortaCassette.h` against `platform/drumlogue/common` and running the result
under `qemu-arm`:

```
arm-linux-gnueabihf-g++ -O2 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
    -std=gnu++14 -D__arm__ -I. -I../common probe.cc -o probe -lm
qemu-arm -L /usr/arm-linux-gnueabihf ./probe
```

That is how the numbers quoted above were measured.

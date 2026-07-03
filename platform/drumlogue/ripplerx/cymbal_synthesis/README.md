# Realistic cymbal synthesis (C++11)

This example ports Dan Stowell's SuperCollider cymbal approach to standard C++11: exponentially distributed resonators driven by low-pass and high-pass noise envelopes, direct broadband attack noise, and a short stick impulse. The implementation is suitable for embedded/logue-style audio because it uses fixed-size C arrays and performs no dynamic memory allocation.

Enhancements beyond the tutorial:

- `velocity`: scales level, stick attack, brightness, high-frequency shimmer, and effective decay.
- `muffle`: darkens and damps the cymbal for grabbed/choked hits.
- Four presets: crash, ride, splash, and gong.
- Extra pink/white noise utilities, advanced high-pass shimmer, optional stick impulse, DC blocking, and output limiting.
- Optional comb filtering and phase modulation controls for additional metallicity.

## Comb filtering evaluation

Short comb filters can add realistic clustered reflections and extra metallic density, especially for crash and gong presets. They are cheap and deterministic, but they can also impose obvious pitched delays if overused. In this implementation `RenderParams::comb` is intentionally a 0..1 enhancement mixed after the resonator bank; good values are usually `0.05..0.30`.

## Phase modulation evaluation

Small phase/gain modulation of the resonator drive can mimic nonlinear energy transfer between cymbal modes and keeps the tail from sounding like static filtered noise. It is useful for gong swells and hard crash hits. Large values make the sound synthetic or chorus-like, so `RenderParams::phaseMod` should usually stay below `0.25` unless an exaggerated effect is desired.

## Minimal usage

```cpp
#include "realistic_cymbals.h"

realistic_cymbals::CymbalSynth cym(48000.0f);
realistic_cymbals::RenderParams p;
p.preset = realistic_cymbals::PRESET_CRASH;
p.velocity = 0.9f;
p.muffle = 0.0f;
p.comb = 0.18f;
p.phaseMod = 0.12f;
cym.noteOn(p, 1234u);
float sample = cym.process();
```

Source inspiration: Dan Stowell's cymbal synthesis tutorial, which describes a real-time method using many `Ringz` resonators, filtered noise drivers, stick impulse, velocity, and muffle controls.

## Rendering evaluation WAV files

The renderer executable is `test_real_cymb.cpp`; it contains `main()` and writes one mono 48 kHz / 16-bit PCM WAV file per preset. The most reliable way to build it is from this example directory:

```sh
cd examples/realistic-cymbals
make render
```

If you prefer to call `g++` directly from this directory, use:

```sh
g++ -std=c++11 -Wall -Wextra -pedantic realistic_cymbals.cpp test_real_cymb.cpp -o test_real_cymb
./test_real_cymb
```

Or, from the repository root, use explicit paths:

```sh
g++ -std=c++11 -Wall -Wextra -pedantic \
  examples/realistic-cymbals/realistic_cymbals.cpp \
  examples/realistic-cymbals/test_real_cymb.cpp \
  -o /tmp/test_real_cymb
(cd /tmp && /tmp/test_real_cymb)
```

The test program writes `real_cymb_crash.wav`, `real_cymb_ride.wav`, `real_cymb_splash.wav`, and `real_cymb_gong.wav` in the working directory.

If the linker reports `undefined reference to main`, the command did not compile `test_real_cymb.cpp` from this example directory. Check that your command includes `examples/realistic-cymbals/test_real_cymb.cpp` when running from the repository root, or `cd examples/realistic-cymbals` before using the shorter command.

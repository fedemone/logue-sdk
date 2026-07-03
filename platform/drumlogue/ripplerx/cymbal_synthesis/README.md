# Realistic cymbal synthesis (C++11)

This example ports Dan Stowell's SuperCollider cymbal approach to standard C++11: exponentially distributed resonators driven by low-pass and high-pass noise envelopes, direct broadband attack noise, and a short stick impulse. The implementation is suitable for embedded/logue-style audio because it uses fixed-size C arrays and performs no dynamic memory allocation.

Enhancements beyond the tutorial:

- `velocity`: scales level, stick attack, brightness, high-frequency shimmer, and effective decay.
- `muffle`: darkens and damps the cymbal for grabbed/choked hits.
- Four presets: crash, ride, splash, and gong.
- Extra pink/white noise utilities, advanced high-pass shimmer, optional stick impulse, DC blocking, and output limiting.
- Optional comb filtering and phase modulation controls for additional metallicity.
- `CymbalKit`: a 4-voice polyphonic wrapper so repeated strikes stack (rolls, crash over ride tail) instead of choking the previous hit.

## Velocity behaviour (verified by the test suite)

A harder strike must be louder, brighter, and ring longer. Three mechanisms implement this, all measured by `test_real_cymb`:

1. **Level** — `velocityGain_ = 0.25 + 0.75·v^1.5` applied at the output (peak 0.09 → 0.59 across v=0.30 → 0.95).
2. **Brightness** — the resonator drive gets whiter with velocity (`whiteBlend = lerp(0.08, 0.45, v²)`), on top of the velocity-scaled low-pass cutoff and shimmer level. Early spectral centroid rises ~1220 → ~1870 Hz across the sweep.
3. **Tail** — resonator ring decay and driver decay both scale with velocity (T60 ≈ 1.2 s soft → 1.8 s hard for the crash). When `RenderParams::durationSec` is 0 (live use) the tail is **no longer capped** to the preset's `decaySec`; the natural velocity-scaled decay rules.

## Strike envelope → phase modulation

The nonlinear shimmer of a real cymbal is mode-coupling chaos whose strength follows the vibration energy: strongest right after the strike, settling into clean linear ringing as energy drains. Earlier revisions gated the phase modulation with the slow *driver* envelope (so the wobble persisted uniformly through the whole tail) and computed an amplitude ADSR (`ampEnv`) that was never used.

The current implementation drives the PM with an explicit **strike envelope**: near-instant attack, exponential decay with time constant `max(0.04 s, highDecaySec · lerp(0.5, 1.6, velocity))`, scaled by velocity. It gates both the PM depth (`pmGain`/`pmExciter`) and modulates the PM LFO *rates* (+60 % at the strike), so the chaotic regime is denser and faster at the attack and calms down with the energy — as on the instrument. The three PM LFO phases are re-seeded deterministically at every `noteOn`, so a given seed always produces the same attack shimmer. The dead `ampEnv` code was removed; its only defensible role (click-free ends of fixed-duration one-shot renders) is now an inline release fade.

## Splash preset

The original splash anchored all resonators above 3.2 kHz with 128 heavily detuned resonators — a dense continuum that measured (and sounded) like band-passed noise: early centroid ≈ 12.9 kHz, no pitched body. A real 8-12" splash still has a pitched modal body with its lowest plate modes around 1-3 kHz. The preset now:

- anchors from 1123 Hz upward (inharmonic spread to 17 kHz), `minHz` 1000;
- uses 72 resonators with slightly less jitter, so discrete modes read as metal;
- decays faster (`decaySec` 1.10 → 0.70, high band 0.62 → 0.25);
- is ~4× louder (`resonatorLevel` 2.60).

Measured result: early centroid ≈ 3.4 kHz, spectral flatness at noise-metric floor, T60 ≈ 1.0 s — a metallic "pssh" instead of hiss.

## Polyphony

`CymbalKit` holds 4 `CymbalSynth` voices, allocates round-robin (free voice preferred, oldest stolen otherwise), decorrelates per-voice seeds, and applies gentle soft-saturation headroom on the sum. The test suite verifies a 6-hit crash roll reaches 4 simultaneous voices with tails overlapping and the mixed crash/ride/splash/gong stack keeps earlier tails alive.

## Output level

The resonator bank is normalised by `1/sqrt(count)` instead of `1/count`: N
decorrelated ringing modes sum to ~sqrt(N) RMS, so the old scheme
over-attenuated the bank (crash peaked at 0.28, ride at 0.075 of full
scale). Per-preset `resonatorLevel` values were retuned on top of the new
normalisation so full-velocity strikes peak around 0.5-0.8: crash 0.59, ride
0.52, splash 0.54, gong 0.80. Muffling now also attenuates the output
(`lerp(1.0, 0.55, muffle)`): a shorter ring decay raises the resonators'
input gain `b0 = sqrt(1 - r^2)`, so an uncompensated choked hit actually
peaked *above* the open one. Stacked kit output keeps its soft-saturation
headroom (6-hit roll peaks at 0.49, mixed stack at 0.61).

## Optimisation and fast math (`../float_math.h`)

`process()` now runs with **zero libm calls** in the steady state (previously
~11 `expf`/`sinf` calls per sample — the dominant cost after the resonator
bank on a Cortex-A7 with software libm):

- All per-note constants (velocity curves, envelope time constants, cutoff
  ceilings, PM depths/rates, release fade) are folded once in `noteOn()`.
- The four exponential envelopes are geometric recursions (`state *= mul` per
  sample) with multipliers computed by exact `expf` at noteOn.
- The driver filter coefficients use `fastexpf` and the PM LFOs use
  `fastsinf` (phase folded to `[-pi, pi]`), both inline bit-trick
  approximations from KORG's `float_math.h`.

Where approximation is **not** tolerable, exact libm remains — and it only
runs at noteOn time: resonator pole placement (`cosf` — 1e-3 error shifts low
modes by hundreds of Hz), decay radii and envelope multipliers (`expf` with
|arg| ~ 1e-4 — fast variants warp T60 by 2x), and frequency ratios (`exp2f`).

**Warning — `float_math.h` `fastpow2f` bug**: the header's `fastpow2f`
computes `z = clipp - w + 1.f` unconditionally, but Mineiro's original
applies that +1 fractional offset only to negative inputs. Positive
non-integer exponents therefore come out badly high (measured:
`fastpow2f(0.5)` = 1.83 instead of 1.414, worst-case ~2x near integer
boundaries). Using it for the resonator frequency ratios silently detuned
every up-jittered mode sharp and cost ~30% of the bank's output level.
`fastexpf`/`fasterexpf` always pass *negative* arguments (where the
unconditional offset is correct), so they are unaffected — but do not call
`fastpow2f`/`fastpowf` with positive fractional exponents.

## ARM NEON (v7)

The resonator state is stored structure-of-arrays (`b0[]`, `a1[]`, `a2[]`,
`y1[]`, `y2[]`, `gain[]`) and the count padded to a multiple of 4 with silent
lanes, so the per-sample recursion runs 4 resonators per `float32x4_t` with
no scalar tail. The NEON path (`__ARM_NEON` guard, v7-safe intrinsics only:
`vmlaq_f32`, `vpadd_f32` horizontal sum — no `vaddvq`) is:

- syntax-checked against `armv7a-linux-gnueabihf` + `-mfpu=neon` (clang);
- functionally verified on the host through an intrinsics-emulation shim:
  the NEON branch renders bit-comparable output to the scalar branch
  (identical peak/RMS to 4 significant digits).

The SoA layout also lets the host compiler auto-vectorise the scalar branch
(`-O3`: ~96x realtime vs ~88x for the old array-of-structs code on x86).
On-target cycle counts still need to be measured on hardware.

## Comb filtering evaluation

Short comb filters can add realistic clustered reflections and extra metallic density, especially for crash and gong presets. They are cheap and deterministic, but they can also impose obvious pitched delays if overused. In this implementation `RenderParams::comb` is intentionally a 0..1 enhancement mixed after the resonator bank; good values are usually `0.05..0.30`.

## Phase modulation evaluation

Small phase/gain modulation of the resonator drive can mimic nonlinear energy transfer between cymbal modes and keeps the tail from sounding like static filtered noise. It is useful for gong swells and hard crash hits. Large values make the sound synthetic or chorus-like, so `RenderParams::phaseMod` should usually stay below `0.25` unless an exaggerated effect is desired.

## Minimal usage

```cpp
#include "realistic_cymbals.h"

// Monophonic voice
realistic_cymbals::CymbalSynth cym(48000.0f);
realistic_cymbals::RenderParams p;
p.preset = realistic_cymbals::PRESET_CRASH;
p.velocity = 0.9f;
p.muffle = 0.0f;
p.comb = 0.18f;
p.phaseMod = 0.12f;
p.durationSec = 0.0f; // 0 = natural velocity-scaled decay (live use)
cym.noteOn(p, 1234u);
float sample = cym.process();

// Polyphonic kit: strikes stack instead of retriggering
realistic_cymbals::CymbalKit kit(48000.0f);
kit.noteOn(p, 1234u);
// ... later, while the first hit still rings:
kit.noteOn(p, 5678u);
float mix = kit.process();
```

Source inspiration: Dan Stowell's cymbal synthesis tutorial, which describes a real-time method using many `Ringz` resonators, filtered noise drivers, stick impulse, velocity, and muffle controls.

## Building and running the test suite

The renderer/test executable is `test_real_cymb.cpp`; it renders one WAV per preset plus velocity-sweep, muffle, roll, and stacking test renders (mono 48 kHz / 16-bit PCM), prints per-render metrics (peak, rms, tail rms, T60, spectral-tilt brightness), and runs PASS/FAIL behaviour checks (non-zero exit on failure). Build it from this directory:

```sh
cd platform/drumlogue/ripplerx/cymbal_synthesis
make render
```

If you prefer to call `g++` directly from this directory, use:

```sh
g++ -std=c++11 -Wall -Wextra -pedantic realistic_cymbals.cpp test_real_cymb.cpp -o test_real_cymb
./test_real_cymb
```

The behaviour checks assert that:

- a harder crash is louder, brighter, rings longer, and leaves more tail energy;
- a muffled hit is actually choked;
- a 6-hit crash roll stacks ≥ 3 simultaneous voices without clipping;
- overlapping crash/ride/splash/gong strikes keep earlier tails alive.

## Next steps (feasibility notes for merging into the RipplerX unit)

1. **Optimize** — the per-sample cost is dominated by the resonator bank (72-112 biquad-like recursions) plus three `sinf` calls for the PM LFOs; the `sinf`/`expf` in the envelope path can move to per-block or recursive forms.
2. **ARM NEON (v7)** — the resonator loop is the obvious SIMD target: 4 resonators per `float32x4_t` lane (state `y1/y2` kept in registers, coefficients contiguous). The recursion is per-resonator independent, so it vectorises cleanly.
3. **Merge strategy** — option B (improve the existing ENGINE_PLATE path with this resonator-bank + strike-envelope-PM approach) is preferred: option A (wholesale replace) risks the HW-approved HHat-O, and option C (new instrument family) duplicates ~30 KB-constrained `.text`. Mind the RipplerX `.rodata`/`.data` placement rules (see `../CLAUDE.md`) when moving the preset tables.

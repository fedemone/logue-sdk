# Merge feasibility study: cymbal_synthesis → RipplerX unit

Decides how the prototype in this directory (Stowell-style dense resonator
bank, strike-envelope PM, velocity dynamics, 1/sqrt(N) level normalisation,
NEON v7 resonator loop) should reach the shipping RipplerX drumlogue unit.

**Recommendation: Option B — improve the existing `ENGINE_PLATE` path.**

## Constraints (from `../CLAUDE.md`)

| Constraint | Value |
|---|---|
| ARM `.text` (= text + rodata) | ≤ 28 KB safe margin (30 KB hard limit) |
| Preset tables | must be **non-static class members** so they land in `.data`, not `.rodata` |
| `.bss` | near 552 bytes |
| HHat-O | **HW-approved** — must not change audibly |
| DSP unit tests | 82/82 must stay green |
| Voices | `NUM_VOICES = 4`, per-voice `resA`/`resB` 2048-float delay buffers (FDN already lives in the KS-dead `resB` on plate presets) |

## Measured prototype resources

Compiled with `clang --target=armv7a-linux-gnueabihf -mfpu=neon -Os`:

| Resource | Cost |
|---|---|
| `.text` + `.rodata` of the full standalone engine | **~5.7 KB** (5312 text + 276 rodata + 352 data.rel.ro) |
| RAM per `CymbalSynth` voice | **36 KB** — of which 32 KB is the 4-tap comb (4×2048 floats) and 3 KB the 128-resonator SoA state |
| RAM for the 4-voice `CymbalKit` | 144 KB |
| Host render speed (x86 `-O3`, scalar SoA) | ~96× realtime per voice |
| Per-sample libm calls | 0 (envelopes recursive, `fastsinf`/`fastexpf` inline) |

## Option A — replace ENGINE_PLATE wholesale

Drop the existing plate modal bank + crash bank + FDN and run the prototype
engine for all plate presets.

- **Sound**: best match to the approved prototype renders for
  Cymbal/Gong/Ride/RideBell/Splash-type presets.
- **But ENGINE_PLATE serves 10 presets**, including HHat-O (HW-approved, a
  hard "do not break"), and Cowbell / Triangle / BellTree / Tick — *pitched*
  metal percussion the dense inharmonic resonator cloud is wrong for.
- `.text`: +5.7 KB in, minus an estimated 2–4 KB of removed plate/FDN code →
  net +2–4 KB against a budget that is already at its 28 KB safety margin.
- Throws away the pass-14→18 tuning history (reference-measured T60s,
  band-envelope coupling, FDN flatness fix) for presets that already pass HW.
- **Verdict: rejected** — risk concentrated exactly on the presets that are
  already approved.

## Option B — improve ENGINE_PLATE with the prototype's techniques (RECOMMENDED)

The existing crash bank (`synth_engine.h` ~line 2664) is 6 hand-unrolled
scalar resonators sharing one radius, reusing the `modal_k_*` pole
coefficients. The FDN (pass 18) already provides the dense broadband wash.
What the prototype adds on top is: many *independently tuned* modes,
strike-gated nonlinearity, velocity-dependent spectrum/decay, and correct
bank level scaling. Each ports as a bounded change:

1. **Enlarged NEON crash bank** — replace the 6 unrolled resonators with a
   24–32 resonator SoA bank driven through the existing nonlinear `exc`
   (pass-16 cascade), normalised by `resonatorLevel / sqrt(N)`.
   - State: 32 × 6 floats = 768 B per voice, inside `VoiceState` (heap
     `SynthState`, not `.bss`).
   - Code: the NEON loop + init is a few hundred bytes of `.text` (the whole
     prototype engine is 5.7 KB; the loop is a small fraction).
   - CPU: ~8 `float32x4_t` iterations/sample/voice ≈ 20 M cycles/s for 4
     voices at 48 kHz — small on the target core, and it *replaces* 6 scalar
     resonators.
   - Frequencies: per-preset anchor tables (16 floats each) with seeded
     jitter, exactly as the prototype. Tables go in as **non-static members**
     (`.data` rule). Init uses exact `cosf`/`expf`/`exp2f` (existing gotcha;
     also: `float_math.h` `fastpow2f` is broken for positive fractional
     exponents — see README).
2. **Strike-envelope-gated PM** — gate the pass-9 self-PM depth (and
   optionally the crash drive) with `velocity * expEnv(0.8 ms,
   highDecay * lerp(0.5, 1.6, vel))` as two recursive floats per voice, so
   the chaos blooms at the strike and settles, instead of riding the slow
   noise envelope. Negligible text/CPU.
3. **Velocity dynamics** — velocity-dependent white/pink drive blend and
   velocity-scaled ring/driver decays; the level curve
   `0.25 + 0.75·v^1.5`. Negligible cost.
4. **Splash-style anchored tuning** — data-only retune of preset anchors
   (the prototype's fix for "noise-like" presets was mostly *frequency
   placement*, not structure).
5. **Skip the comb section** — the FDN already supplies dense reflections;
   this is what makes the port fit (saves 32 KB/voice and the associated
   text).
6. **HHat-O protection** — every new behaviour is parameterised per preset
   and defaults to "off"/current values for HHat-O (same pattern as
   `crash_couple`); its NoteOn block is not touched.

**Verification gates per stage**: host syntax check → 82/82 DSP tests →
render all 37 presets (0 NaN/silent) → `refcmp.py` against references →
`arm-…-size` `.text` ≤ 28 KB on the next flash → HW listen.

## Option C — new instrument family

Add `ENGINE_CYMBAL2` alongside ENGINE_PLATE with its own presets.

- `.text`: +5.7 KB engine + preset-table growth + param-wiring code, with
  nothing removed → almost certainly pushes past the 28 KB margin.
- Duplicates voice management/polyphony the unit already has (pass 12).
- Leaves the existing "not that good" plate presets in place unimproved.
- **Verdict: rejected** (matches the original task note: "not recommended").

## Suggested merge order (Option B)

| Stage | Change | Risk | Gate |
|---|---|---|---|
| B1 | 1/sqrt(N) bank normalisation + level retune of existing 6-res bank | low | tests + refcmp levels |
| B2 | SoA/NEON bank enlargement (6 → 24–32 modes, anchor tables in `.data`) | medium | `.text` size + refcmp flatness/centroid |
| B3 | strike-envelope PM gating | low | HW listen (attack character) |
| B4 | velocity whiteBlend + decay scaling | low | velocity-sweep render metrics |
| B5 | anchor retunes for Cymbal/Ride/RideBell (+ any splash-type preset) | data-only | refcmp + HW listen |

Each stage is independently committable and revertible; HHat-O renders are
diffed (same seed) at every stage and must be bit-identical or audibly
unchanged.

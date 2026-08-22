# Percussion Spatializer / delay_tribal

## Hardware round 1: "almost dry only" (fixed)

First hardware test reported the effect as barely audible — the ensemble sat far
below the hit that produced it, and Angel showed no random spatial presence.
Measured wet peak for a unit impulse at 100% wet was **-15.7 dB (Tribal)**,
-12.9 dB (Military), -10.5 dB (Angel). Four compounding causes:

| Cause | Cost | Fix |
|-------|------|-----|
| **One-pole lowpass acting as an attenuator.** A one-pole has unity DC gain but its impulse peak is only `a`, and `a` was 0.22-0.50. Percussion is all transient, so every clone lost 5-9 dB | -5 to -9 dB | Makeup gain `1/sqrt(power gain)` baked into the pan gains, making the filter a tone control rather than a level control |
| **Power normalisation across clones.** `1/sqrt(sum of gains squared)` divided the ensemble by ~1.6 at ten clones — every drummer got quieter as drummers were added | -4 dB at 10 clones | Normalise to the loudest clone instead. The taps are separated in time, so they do not need sqrt(N) headroom reserved; the limiter handles peaks |
| **`hp_attn` was a gain cut, not a filter.** `1/(1 + hp_hz*0.0012)` stood in for a highpass that was never applied | up to -2.4 dB | Replaced with a mild follower tilt; the one-pole does the real shaping |
| **The leading clone was dulled.** Every clone started at `lp_base` (3.5 kHz in Tribal), so at 100% wet the stroke the player hears had its transient filtered off | — | The first clone now runs essentially open (15 kHz) and followers descend from there |

Wet peak is now **-3.2 / -2.9 / -2.8 dB** across the three modes: roughly +13 dB
in Tribal, and the ensemble lands within 6 dB of the hit that produced it.

## Transient detection was broken for half the drum kit (fixed)

Everything per-hit — velocity randomisation, timing scatter, Angel's
re-placement — hangs off transient detection, and it was comparing the raw
block-peak magnitude against a decaying copy of itself. A 4-sample block peak
tracks the *carrier*, not the envelope, so:

- **Long-decaying sources never triggered at all**: 0/40 on a 400 ms tom, 0/40
  on a 900 ms kick. Every per-hit behaviour was silently disabled for exactly
  the material that needs it most.
- Naively fixing that with a fast envelope release then produced the opposite
  failure — a 60 Hz kick retriggered **20 times per stroke**, re-rolling the
  whole ensemble mid-decay, because the release was shorter than the carrier's
  half-cycle.

Now a fast envelope (instant attack, ~28 ms release — comfortably past the
12.5 ms half-cycle of a 40 Hz fundamental) racing a slow lagging one, with a
30 ms refractory window. Measured **exactly 1.00 triggers per stroke** on kick,
snare, hat, tom and long kick, at both full and low level.

Note the slow envelope must lag in *both* directions. Giving it an instant
attack as well — the obvious first shape — makes it greater than or equal to the
fast envelope at all times and nothing ever triggers.

## Angel's spatial randomness was being cancelled by the balance trim (fixed)

The L/R balance trim ran on every Angel re-placement, forcing each individual
hit back to centre — precisely the hit-to-hit movement the mode exists to
produce. The trim now eases toward centre across hits instead of snapping per
hit, so the image is centred on average while each hit lands where it fell.

Per-hit stereo position over 40 hits:

| | before | after |
|---|---|---|
| Angel spread (sd) | 1.80 dB | **3.35 dB** |
| Angel range | -1.3 .. +6.7 dB | **-4.3 .. +8.2 dB** |
| Angel mean | +1.98 dB | +1.07 dB |

Tribal stays at sd 1.09 dB and Military at 0.65 dB, so the modes now differ
audibly in how much the image moves.

## Depth and Mix removed

Both are fixed at 100% internally. Depth always uses the widest arrival spread;
the unit is always fully wet, so the first clone is the leading stroke. Eight
parameters remain: Clones, Mode, Rate, Spread, Wobble, Scatter, SoftAtk, Gap.


## Stereo / timing bugs found by measurement (fixed)

Found by driving the real engine with impulses and measuring the output, not by
reading the code. All four predate the current work — verified against 61910f0.

| Bug | Measured symptom | Root cause | Fix |
|-----|------------------|-----------|-----|
| **Image leaned left** | +1.7 dB at 2 clones rising to **+6.4 dB at 10**; clone 0 (the loudest) had a right gain of exactly 0.00 | Clone gains roll off loudest-first (`gain_step^i`) while pan swept monotonically left-to-right, so the loudest clone was seated hard left and the quietest hard right | `centre_out_seat()` hands out the same set of positions walking outward from the centre, so loud clones sit near the middle; plus an L/R power trim in `place_clones()` |
| **Spread was a volume control** | Spread=0 **muted the wet path entirely**; wet level rose linearly with the knob | `pan_l/pan_r` were multiplied by `spread_` | Spread now collapses the pan *position* toward centre (`px * spread_`). Level is flat within 0.02 dB across the sweep; correlation goes 1.000 (mono) → 0.587 |
| **Scatter barely did anything** | Full-range Scatter moved timing by <1.5 ms against 70–130 ms taps, and L/R correlation by 0.007 | `profile_.scatter_amount` is a unitless 0–0.63 *pan fraction* but was multiplied by `ms_to_smp` as if it were milliseconds. `gap_detach` was also applied twice, compounding to 7.3x at Gap=100 | New `profile_.scatter_ms` carries the real time scale (~20 ms on the last clone at Scatter=100); `gap_detach` applied once |
| **Angel never re-scattered** | Angel's placement was static until a knob moved, despite the README's "scattered per hit" | Pan was only computed in `rebuild_profile()`, which runs on parameter change; `randomize_hit()` never touched it | `randomize_hit()` calls `place_clones()` in Angel mode |

### Output limiter

The cubic soft-clip (`x - x^3/3`, capped at 2/3) applied its curve from zero up:
0.8 dB of loss at half scale and **3.5 dB at full scale even with Mix at 0**, so
it coloured a fully dry signal. Replaced with a limiter that is transparent
below 0.9 and bends asymptotically to a ceiling of 1.0 (`y = th + h*u/(1+u)`,
unity value and slope at the threshold). Measured worst-case output at maximum
drive: 0.9932 — bounded, with nothing hard-clipping.

### Known residual: Angel channel balance

Angel splits the bands across channels — L lowpassed, R highpassed — so its
balance is program-dependent and no fixed gain trim can centre it for every
input. It measures +1.4 to +2.0 dB left on impulses. Tribal and Military are
within ±0.4 dB at every clone count. If this matters, the fix is to make Angel
dual-band *per clone* rather than per channel, which is a voicing change.

## Test suite

`test_sine_input.cpp` previously contained a standalone re-implementation of the
effect and never linked `PercussionSpatializer.cc` — it asserted only that the
mock agreed with itself, its constants had drifted from the engine, and it
failed on every recent commit. It now drives the real engine and locks in each
of the fixes above.


## Status

Signal path implemented and building for armv7-a. **Not yet tested on physical
drumlogue hardware.**

## Open TODOs

- [ ] Hardware testing on a physical drumlogue
- [ ] Verify all three spatial modes (Tribal/Military/Angel) at both extremes of
      Depth and Gap
- [ ] Re-check gain staging on hardware after the power normalisation and the
      equal-power mix law changed the output level
- [ ] Decide whether `filters.h` (biquads) should be wired into the engine or
      deleted along with `test_biquad_filters.cpp`

## Fixed

| Bug | Root cause | Fix |
|-----|-----------|-----|
| **Wet path ran at fs/4** | `render_block4` wrote all four input frames into the delay line before rendering any of them, so `delay_.write` was identical for every frame in the block and all four read the same delay-line position | Push and read one frame at a time in `render_frames()` |
| **Fractional tap quantised to 1/32 sample** | Read position was biased by `kLen * 8` (262144) before the int/frac split; a float resolves only 2^-5 at that magnitude, so the interpolation fraction — and with it the wobble modulation — was staircased | `delay_tap()` splits with integer arithmetic |
| **Gap unreachable** | `header.c` declared `num_params = 9` while 10 parameters are defined | `num_params = 10` |
| **Output clipped** | 10 clones summed to roughly 6-8x before the mix stage with no normalisation or limiting | Power normalisation (`clone_norm_`) in `rebuild_profile()` plus a cubic soft clip on the output |
| **Clones muted after raising the clone count** | `dynamic_gain_factor` is only set in `randomize_hit()` over the current `clone_count_`; clones newly in range had it at 0 | Default the per-hit multipliers in `rebuild_profile()` |
| **Wobble depth jumped** | `rebuild_profile()` used `0.15 + 1.8*w`, `update_clone_dynamics()` used `0.20 + 2.8*w`; whichever ran last won | Single shared law (`kWobbleMsBase` / `kWobbleMsRange`) |
| **Scalar tail behaved differently** | `render_scalar_frame` hardcoded `sample_idx_in_block = 0`, never advanced the wobble phase, and skipped smoothing and transient detection | All paths go through `prepare_block()` / `render_frames()` with a frame count of 1-4 |
| **Transient detection differed per build** | The aarch64 branch reduced with `vaddvq_f32` (sum) where the armv7 branch took a max | Both take the block peak |
| **`inverse_sample_rate_` never derived** | `Init()` set `sample_rate_` but left the reciprocal at its hardcoded default | Derived in `Init()` |
| **NEON uninitialized registers** | `float32x4_t s1, s2` passed to `vsetq_lane_f32` — ARM NEON reads the destination register before writing the lane | `vdupq_n_f32(0.0f)` initialisation |
| **Filter div-by-zero** | `q_factor` could reach 0 -> `alpha = sin_w0/0 = inf` -> NaN coefficients | `q_factor` floored at 0.01 in all three coefficient functions in `filters.h` |

## Signal path

Per clone, per sample:

1. **Fractional delay tap** into a shared 16384-sample stereo delay line,
   linearly interpolated. Position is `delay_samples + scatter_samples`
   (per-hit) plus a triangle-interpolated sine LFO scaled by
   `wobble_depth_samples`.
2. **One-pole lowpass**, `y += a * (x - y)`, with `a` derived from the clone's
   cutoff (`omega / (omega + 1)`, `omega = 2*pi*fc/fs`) and re-randomised per
   hit by a 0.7-1.3 factor. In Angel mode the right channel is turned into a
   highpass by `x - y`.
3. **Gain**: `base_gain` (exponential rolloff x scatter detachment) x pan gain
   (with the highpass tilt baked in) x per-hit accent x per-hit dynamic factor
   x gap boost x `clone_norm_`.

Clones are processed 4-wide (NEON), then 2-wide, then scalar, all sharing one
set of SoA state arrays. Highpass shaping is an amplitude tilt baked into the
pan gains, not a filter — there are no biquads in the render path.

## Humanisation

| Feature | Range | Trigger |
|---------|-------|---------|
| Velocity / dynamic gain | 0.7 - 1.3 | per detected transient |
| Per-hit accent on 1-4 random followers | 0.4 - 1.4 | per detected transient |
| Timing scatter | scaled by Scatter and Gap | per detected transient |
| Cutoff randomisation | 0.7 - 1.3 x base coefficient | per detected transient |
| Pitch wobble | 0.15 - 1.95 ms, per-clone rate stagger | continuous LFO |
| Attack softening | 0.82 - 1.0 gain on followers | continuous, from SoftAtk |

## Parameters

| ID | Name | Range | Default |
|----|------|-------|---------|
| 0 | Clones | 0-4 (2/4/6/8/10) | 0 |
| 1 | Mode | 0-2 | 0 |
| 2 | Depth | 0-100% | 50 |
| 3 | Rate | 0.0-10.0 Hz | 3.0 |
| 4 | Spread | 0-100% | 80 |
| 5 | Mix | 0-100% | 50 |
| 6 | Wobble | 0-100% | 30 |
| 7 | Scatter | 0-100% | 20 |
| 8 | SoftAtk | 0-100% | 20 |
| 9 | Gap | 0-100% | 20 |

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

*Superseded* by "Panning against each mode's intent" below. Angel now places
every voice at random, so there is no formation to trim. Its trim is 1, and it
is centred by symmetry rather than by easing.

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

## Panning against each mode's intent (fixed)

Checked against what each mode is meant to be: Tribal a ring of players,
Military a row across the front, Angel a random position per voice. Measured by
reading every clone's pan gains after each hit, and by windowing each clone's
own arrival in the output (`test_pan_formations()` locks all of it in).

| Bug | Measured | Root cause | Fix |
|-----|----------|-----------|-----|
| **The layout re-rolled whenever a knob moved** | Rate 30 -> 31 moved a clone by up to 0.28 (Tribal), 0.35 (Military), 0.72 (Angel) on a -1..+1 scale | `place_clones()` drew its own random offsets, and it runs on every rebuild: any knob move, plus the end of every smoothing window | The draws moved to `randomize_hit()` (`clone_t::pan_rand`); `place_clones()` is deterministic. 0.0000 now |
| **Random offsets were lopsided** | Scaled by 0.2 at the leftmost seat and 1.0 at the rightmost, so right players moved up to five times as far: over 400 rebuilds, Military's second seat from the left varied with sd 0.045 against 0.113 for its mirror | The weight was `seat` — the left-to-right position — where the arrival order was meant | Tribal's sway has one bound for every player; Angel's placement is symmetric by construction. Mirrored Tribal seats now sway within 1.27x of each other |
| **Military broke rank** | Same offsets as Tribal (`0.08 + 0.55 x Scatter`, +-0.33 at the default) against a seat spacing of 0.22 at 10 clones: the row was out of order on 274 of 400 layouts at the default Scatter, 395 of 400 at Scatter=100 | One formula for both modes | No random offset in Military: the row is fixed and symmetric, 0 of 400 out of order |
| **Tribal's ring was blurred, and never moved per hit** | Static offsets up to +-0.33 over the sin() spacing; zero movement between hits | As above | Exact ring positions, plus a per-hit sway along the ring of at most Scatter x half the angle to the neighbour: 0 neighbour swaps in 300 hits at Scatter=100, centre sway sd 0.11 |
| **Angel was the row plus jitter** | Each voice stayed near its row seat: the outer voices sat on their own side on 60 of 60 hits, mean ILD up to 15 dB; at Scatter=100, 24% of voices were clamped hard to one side | Row seat + two random terms, each clamped to +-1 | A random seat, then a uniform offset inside it, per hit. Every voice now has mean 0 and sd 0.52-0.60 (0.577 is uniform), is left of centre on 23-36 of 60 hits, and none is clamped |
| **Angel leaned left** (the old "known residual") | +1.4 to +1.6 dB on impulses, every clone count | The delay tap's linear interpolation is a lowpass the makeup gains ignored. Averaged over the fractional position it passes (2 + rho1)/3 of the power — `1 - a/3` for the lowpass, `2/3 - a/6` for the highpass — so Angel's highpassed R lost 1.4-1.7 dB more than L. With interpolation switched off the lean measured -0.01 dB | R's makeup carries that ratio (Angel only, so L and the other modes keep their level): +0.06 to +0.4 dB now, depending on Gap. *Superseded*: Angel now puts the same filter on both channels, and the correction is gone (see "Angel's low end stayed in the left speaker") |

The balance trim is still computed on rebuild from the nominal formation, and
kept across the per-hit placement so it never drags a hit back to centre.
Angel has no formation, and its expected image is centred by symmetry, so its
trim is 1. Per-hit image movement (L/R sd over 300 hits, 4 / 10 clones,
Scatter 45): Tribal 1.5 / 0.9 dB, Military 1.3 / 0.7 dB, Angel 3.3 / 3.6 dB. In
the two fixed formations it comes from the per-hit level humanisation, not from
pan.

## Angel's low end stayed in the left speaker (fixed)

Angel lowpassed L and highpassed R, so wherever a voice was placed, its body
came out of the left speaker and its top out of the right. Its random
placement only moved the top end and the level of each voice; the broadband
balance was centred, but only as an average over the bands. Two fixes were
prototyped: "the whole voice moves" (filter both channels alike, pan the voice
as a unit) and "the split follows the voice" (per voice, put the lowpass on its
own side). The brief was the one that costs less CPU.

**The whole voice moves.** Both channels now carry the coherent sum of what L
and R used to carry, v = alpha·LP + beta·HP, alpha and beta being the two
makeups, and the pan places all of it. Per voice, by band (10 clones, Spread
80, Gap 100, 80 hits x 10 voices, each voice windowed out of the output):

| | 20-300 Hz | 0.3-3 kHz | 3-16 kHz | broadband |
|---|---|---|---|---|
| mean L/R before | +31.0 dB | +11.4 dB | -1.0 dB | +0.8 dB |
| mean L/R after | **+0.0 dB** | **+0.0 dB** | **+0.0 dB** | +0.0 dB |
| corr. with the voice's position, before | +0.89 | +0.90 | +0.91 | +0.94 |
| corr. with the voice's position, after | **+1.00** | **+1.00** | **+1.00** | +1.00 |

`test_pan_formations()` (e) locks this in. On the old engine it fails at
+31.3 dB in 20-300 Hz. Its correlation bound is 0.97, because the old split
already scored +0.89 to +0.91: the level followed the placement even though
the body did not.

**Why this option.** Both were built and counted (instructions per 64-frame
render, Angel at 2 / 6 / 10 clones). "The whole voice moves" came in at
16040 / 34290 / 51024. "The split follows the voice" came in at
16459 / 34378 / 51181: it needs both highpasses and a per-voice select.

- **Level.** v is divided by sqrt(2), the mid of the old pair. On average over
  positions, the old pan sent half of each part's power to its channel. So a
  voice carried alpha^2/2 below its cutoff and beta^2/2 above it, and v/sqrt(2)
  carries exactly that at both ends. Only around the cutoff is it louder: LP
  and HP are not orthogonal (for a white impulse <lp, hp> = a(1-a)/(2-a) > 0),
  so there they add in phase. Against the old pair's L+R on the same hits:

  | 20-300 Hz | 0.3-3 kHz | 3-8 kHz | 8-16 kHz | 16-24 kHz |
  |---|---|---|---|---|
  | -0.05 dB | +0.06 dB | +0.55 dB | +1.10 dB | +1.43 dB |

  A mono kick-and-tone render below the limiter is -0.18 dB at 2 clones and
  -0.27 dB at 10. The obvious normalisation, matching the old pair's power on
  white noise, was tried first. It moved that power up into the overlap and
  left drums 0.9-1.3 dB quieter, Angel's body about 1 dB under its old level.
  (Full-scale renders read +0.7-0.9 dB louder than the old engine, which is
  no change in level: the old Angel ran its left channel into the limiter on
  a quarter to a third of samples, because all of its body was there.)
- **Timbre.** Each voice is a shelf that pivots at a = 0.5 (alpha = beta,
  about a 7.6 kHz cutoff), where it is the raw tap unfiltered. Voices above
  the pivot tilt bright, the leader included, and voices below it tilt dark.
  The leader's tilt is about 1.6 dB steeper than its old L+R had.
- **The interpolation correction is gone.** It lifted R's highpass because the
  delay tap's interpolation darkens the highpass more than the lowpass. With
  one filter on both channels there is nothing to balance, and kept as a tone
  control it would add another 1.4-1.7 dB to the top that the in-phase overlap
  already raises.
- **CPU.** Instructions per 64-frame render under `qemu-arm`, kick + tone
  input. "Knob moving" re-arms the 120-block smoothing window before every
  render, which is the unit's worst case:

  | | Angel 2 | Angel 6 | Angel 10 | Tribal 2 | Tribal 10 | Military 10 |
  |---|---|---|---|---|---|---|
  | before, steady | 17440 | 35438 | 52081 | 17669 | 52153 | 52083 |
  | after, steady | **15640** | **33956** | **50661** | 16646 | 51643 | 51572 |
  | before, knob moving | 19096 | 38438 | 56409 | | 56481 | |
  | after, knob moving | **17600** | **37724** | **56125** | | 56050 | |

  Angel now has its own render loop (`mix_angel_*`), and Render() picks the
  mode family once per call. Branching per frame instead spilled registers
  (Tribal 10 at 52369). Angel runs its lowpass on the state w = lp/a, so the
  update is a single multiply-add. Its four per-clone coefficients (two gains,
  the shared lowpass weight, the pole) come in with one `vld4q_f32`. During a
  smoothing window only the two gains change. The filter part waits on a dirty
  flag that `rebuild_profile()` and `randomize_hit()` set, in a loop of its
  own. Refreshing everything every block cost Angel 10 59096 while a knob
  moved.
- **Tribal and Military** match the old engine to within 2.4e-7 (-132 dBFS)
  at every clone count, and after an Angel round trip. That difference is
  rounding: under -ffast-math, bit-identity follows whichever code the
  compiler happens to emit, not the arithmetic.
- **Not measured on hardware.** The voice is mono per clone now; the width is
  in the placement alone. Listen for whether the ensemble still reads as airy
  and wide at Spread 80-100.

## Still open: a panned input pulls the formation to its side

Clone gains act as a balance on the stereo input (L in -> L out, R in -> R
out), not as a pan of the source. With a centred source that is the same
thing. Moving the same source power hard right drops the outer left clone by
13 dB (Tribal) to 17 dB (Military) at 4 clones and Spread 80, and the formation
folds to the right. Feeding the clones from (L+R)/2 would put every clone on
its seat whatever the source pan, and halve the delay-line reads. It would also stop the echoes of a panned part
from leaning its way, so it is left as a choice.

Note also that the seats alternate from the centre with each pair's earlier,
louder clone on the left. The trim balances the power, and as a send effect the
dry hit decides where the stroke is heard, so this reads as a ping-pong that
starts on the left. It is not corrected.

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
- [x] Decide Angel's band split: the whole voice moves (see "Angel's low end
      stayed in the left speaker")
- [ ] Listen to Angel's whole-voice placement on hardware (Spread 80 / 100)
- [ ] Decide mono feed vs. balance for panned inputs
- [ ] Listen to Tribal's per-hit sway on hardware (Scatter 45 / 100)

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
   hit by a 0.7-1.3 factor. In Angel mode each channel instead outputs the
   whole voice, `x + q*w`: the lowpass plus a highpass "air" part, alike on
   both channels, run on the state `w = y/a` so the update is
   `w = x + (1-a)*w`.
3. **Gain**: `base_gain` (exponential rolloff x scatter detachment) x pan gain
   (with the filter makeup and follower tilt baked in) x per-hit accent x
   per-hit dynamic factor x gap boost x `clone_norm_`.

Clones are processed 4-wide (NEON), then 2-wide, then scalar, all sharing one
set of SoA state arrays; Angel has its own set of the three (`mix_angel_*`),
chosen once per `Render()` call. The follower tilt (`hp_attn`) is an amplitude
tilt baked into the pan gains, not a filter — there are no biquads in the
render path.

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

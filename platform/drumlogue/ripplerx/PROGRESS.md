# Project Status Tracker

## Phase 20: Progressive Silence Bug, Allpass Formula Fix & Reference Preset [COMPLETED]

Hardware test revealed two confirmed bugs causing progressive silence and pitch error.
T34 added; **82/82 tests pass**. Guitar String reference preset added for model validation.

### Hardware Symptoms Reported

**Init preset:**
- Press 1: high-pitched synth + ~8 quick beats
- Press 2: same but shorter synth
- Press 4: beats only, no synth
- Press 8: complete silence

**Preset 5 (Timpani):**
- Press 1: whining synth sound (no beats)
- Press 2+: nothing

### BUG 1 — CRITICAL: Delay buffer not cleared on NoteOn (progressive silence)

**Root cause:** `NoteOn()` did not clear the waveguide delay buffer (`resA.buffer`,
`resB.buffer`), the LP filter state (`z1`), or the write pointer (`write_ptr`) when
reusing a voice slot. `Reset()` correctly zeros all of these, but that only runs at
cold start. After 4 NoteOn calls, round-robin wraps back to slot 1, which still
holds residual oscillation from the previous note.

**Mechanism:** The residual oscillation is at an arbitrary phase relative to the new
mallet impulse. Destructive interference reduces the perceived amplitude. Constructive
interference is possible but rare; on average each successive press is shorter and
quieter. After 8+ presses the accumulated interference silences the note entirely.

**Diagnosis of symptom progression:**
- Press 1–4: uses fresh slots (cleared by cold-start Reset) → normal sound
- Press 5: slot 1 reused; buffer has residual → shorter, quieter sound
- Press 8: second full cycle; residual from 2nd use overlaps 3rd → silence

**Fix added to `NoteOn()`:**
```cpp
memset(v.resA.buffer, 0, sizeof(v.resA.buffer));
memset(v.resB.buffer, 0, sizeof(v.resB.buffer));
v.resA.z1 = 0.0f;
v.resB.z1 = 0.0f;
v.resA.write_ptr = 0;
v.resB.write_ptr = 0;
```

**Performance:** `memset` of 32 KB (two 16 KB delay buffers) takes ~8–16 µs on ARM
Cortex-A5 — well under one audio sample (20.8 µs). No audible glitch.

**T34 confirms the fix:** 8 consecutive presses all produce identical peak amplitude
(5.173) with no progressive decay. Before the fix, presses 5–8 would produce
decreasing amplitude due to phase cancellation.

### BUG 2 — MEDIUM: Allpass group delay formula wrong → pitch slightly sharp

**Root cause:** The pitch compensation formula for the allpass filter used
`τ_AP = (1+c)/(1-c)`, which is the correct formula for
`H(z) = (−c + z⁻¹) / (1 − c·z⁻¹)`. However, the implemented allpass is
`H(z) = (c + z⁻¹) / (1 + c·z⁻¹)`, whose DC group delay is `(1−c)/(1+c)` — the
reciprocal.

**Effect:** The old formula over-compensates by `(1+c)/(1-c) − (1-c)/(1+c) = 4c/(1−c²)`:

| ap_coeff (InHm) | Over-compensation | Pitch error at C4 |
|---|---|---|
| 0.01 (InHm=20) | 0.04 samples | < 0.1 cents |
| 0.15 (InHm=300, Init) | 0.61 samples | ~6 cents sharp |
| 0.50 (InHm=1000) | 2.67 samples | ~25 cents sharp |
| 0.90 (InHm=1800) | 18.95 samples | ~2 semitones sharp |

With the old formula the delay line was SHORTER than intended → pitch SHARP.
The fix makes notes FLATTER (closer to correct pitch). Existing presets with high
InHm were significantly detuned; low-InHm presets had negligible error.

**Fix in `NoteOn()`:**
```cpp
// Was: float ap_del_A = (1.0f + ca) / (1.0f - ca);  // WRONG (other AP variant)
float ap_del_A = (1.0f - ca) / (1.0f + ca);  // Correct for H=(c+z⁻¹)/(1+c·z⁻¹)
```

**T19a updated** to use the corrected formula in the pitch round-trip check.
**T27b updated**: old test expected delay clamped to 2 (a consequence of the wrong
enormous ap_del); correct behavior is delay near 183 samples for C4.

### "8 Quick Beats" Investigation

The "8 beats" heard with Init preset are most likely from the **PCM sample playback**.
Init preset has `Smp=1` → loads sample bank 0, sample index 0. If the user's drum
kit has a sample loaded there (a drum fill, clap, or noise burst with multiple peaks),
it plays on every trigger alongside the waveguide.

**To isolate waveguide-only sound:** Set `Smp=0` in the Init preset (or use the new
Guitar String preset 28 which has Smp=0). If the beats disappear, the sample was
the source. If they remain, further diagnosis needed.

### Reference Preset 28: Guitar String (Karplus-Strong Validation)

Added preset 28 "GtrStr" specifically for physical model validation:

| Parameter | Value | Meaning |
|---|---|---|
| Note | 69 (A4) | 440 Hz — standard pitch reference (can verify with a tuner app) |
| Dkay | 195 | g≈0.997, T_60≈5.2 s at A4 — realistic guitar string sustain |
| Mterl | 28 | lowpass_coeff≈0.97 — bright string, minimal high-frequency loss |
| TubRad | 15 | slight tube resonance |
| Partls | 0 | single resonator — no coupling complexity |
| Model | 0 | String — positive feedback, harmonically pure |
| NzMix | 0 | no noise — pure plucked string excitation |
| Smp | 0 | no sample — waveguide exciter only |
| InHm | 50 | ap_coeff=0.025 — slight stiffness (piano-like) |

**Expected behaviour:**
1. **Pitch:** 440 Hz ± 5 Hz — verifiable with any guitar tuner app (should read "A4")
2. **Sustain:** note clearly audible 5 seconds after strike (T_60≈5.2 s)
3. **Timbre:** bright attack, gradually darkening (1-pole LP rolls off harmonics)
4. **Harmonic content:** no flutter or beating (single resonator, no coupling)
5. **Decay law:** if you record and measure, amplitude should follow A₀ × g^n where
   n = round trips completed and g=0.997

This preset serves as the reference point for "does the physical model work?" —
if it sounds like a plucked string with 5-second sustain at A4, the model is correct.

### New Test T34

**T34a:** 8 consecutive GateOn+GateOff presses all produce nonzero output.
**T34b:** Second slot cycle (presses 5–8) amplitude within 10% of first cycle.
Result: all 8 presses produce peak=5.173 (identical) → PASS.

**Total: 82/82 tests pass.**

---

## Phase 19: Hardware Testing, SVF Stability Fix & Predictive Sound Design [COMPLETED]

Hardware load confirmed working (RenderStage 1–4). Tests T24–T33 added;
**80/80 tests pass** after two critical fixes described below.

### Hardware Test Results (2026-03-30)

Loaded unit was verified as the new build (renamed "program" parameter, confirmed
sample slot change was audible). Sound IS generated, but:

- Default Init preset always produces **short percussive output** (snare or kick thud)
  regardless of Model setting
- Noise-related parameters (NzMix, NzFltr) have clearly audible effect
- Model changes have subtle but real effect when listening carefully
- String model does not produce long sustained notes with Init preset defaults

**This is expected behaviour — see "Sound character" section below.**

### Root Cause Analysis: ARM -ffast-math vs x86 IEEE 754

The hardware sounds different from x86 unit tests because of a fundamental
IEEE 754 semantic difference:

| Platform | `0 × Inf` | result |
|---|---|---|
| x86 strict IEEE 754 | `0 × NaN` chain | **NaN** written to delay line |
| ARM `-ffast-math` | 0 (fast-math flush) | **0** (no contamination) |

On x86 the Init preset noise_filter was diverging to Inf (see SVF bug below),
then `Inf × noise_decay_coeff(=0)` = NaN was being written into the waveguide
delay line. `fmaxf(-0.99f, fminf(0.99f, NaN)) = 0.99` (IEEE NaN comparison
semantics), so every test appeared to pass but was actually measuring clamped
NaN. On ARM with `-ffast-math`, `0 × Inf = 0` so the hardware never produced
NaN and waveguide resonance genuinely worked — just with very short decay due
to Init preset Dkay=25 (T_60 ≈ 189ms).

### BUG — Chamberlin SVF Stability Violation in Init Preset

**Root cause:** The Init preset sets NzFltFrq=1200 (stored) → `freq = 1200 × 10 = 12000 Hz`.
The SVF tuning formula gives `f = 2·sin(π·12000/48000) = √2 ≈ 1.414`.
The Init preset Resonance Q=0.707 → `q = √2`.

The true Chamberlin SVF stability condition (derived from eigenvalue analysis) is:

```
f < √(4 + q²) − q
```

For q=√2: stability requires `f < √6 − √2 ≈ 1.035`, but f=1.414 → **36% above the limit**.
The filter diverges exponentially to ±Inf after ~130 samples.

On x86: `Inf × noise_decay_coeff(=0) = NaN` (IEEE 754 strict). NaN contaminates
the waveguide at ~311 samples and persists indefinitely. All unit tests measuring
waveguide amplitude were measuring clamped NaN (0.99), not genuine waveguide sustain.

**Fix applied to `filter.h` `set_coeffs()`:**
```cpp
float f_stable_max = sqrtf(4.0f + q * q) - q;
f = fminf(f, f_stable_max * 0.999f);
```
This clamps f to just below the stability limit at all Q values, not just the
naive f < 2 bound. The fix applies to both the noise_filter and master_filter.

**Why the old f < 2 bound was insufficient:** The correct stability condition
is f < √(4+q²)−q, which is strictly less than 2 for all q > 0. The naive
bound f < 2 is only safe when q = 0 (no resonance), which is never the case
in practice.

### Sound Character Guide — Predictive Formulas

With the SVF fix applied, parameter values now have predictable acoustic meaning.

#### Dkay → T_60 (60 dB decay time)

```
feedback_gain = 0.85 + (Dkay / 200) × 0.149
T_60 = −3·ln(10) / (f₀ · ln(feedback_gain))   [seconds]
     ≈ 6.908 / (f₀ · |ln(feedback_gain)|)
```

| Dkay | feedback_gain | T_60 at C4 (261.63 Hz) | Character |
|------|--------------|----------------------|-----------|
| 0    | 0.850        | 9 ms                 | instant dead thud |
| 25   | 0.869        | 189 ms               | short kick/thump (Init preset) |
| 100  | 0.925        | 850 ms               | moderate tom |
| 150  | 0.962        | 2.2 s                | sustained mallet |
| 200  | 0.999        | 26.4 s               | near-infinite string sustain |

**Init preset is percussive by design.** Dkay=25 → T_60=189ms is appropriate
for a kick/tom sound. The "short kick or snare" hardware behaviour is correct.

#### Achieving a 1-Second String Sustain

Set these parameters for a genuine Karplus-Strong string sound:

```
Dkay   = 200  (feedback_gain=0.999 → T_60≈26s at C4)
Mterl  = 30   (lowpass_coeff=1.0: no high-frequency loss)
TubRad = 20   (adds tube-like resonance, pulls coeff toward 1.0)
NzMix  = 0    (pure waveguide, no noise excitation)
Model  = 0    (String: positive feedback, in-phase reflections)
```

Use `GateOn` without `GateOff` (sustained hold). With g=0.999 and 261 round
trips per second at C4, amplitude after 1 second ≈ 0.999^261 ≈ 0.77 (−2.3 dB).
The waveguide will sustain for ~26 seconds at −60 dB.

**Why Init preset always sounds percussive:** T_60=189ms at Dkay=25 means the
resonance is near inaudible by ~400ms regardless of Model selection. Model
changes ARE present but require Dkay≥100 to be clearly audible.

### Test Design Lessons

**Probe timing:** Measuring `ut_delay_read` at a single exact sample point
(e.g. frame 14400) can land at a zero crossing of the sinusoidal waveguide
signal. C4 has period ≈181.5 samples; a single-sample probe at an unlucky
phase reads near zero even when amplitude is ~0.9. Fix: measure the **peak**
over a window of ≥2 full periods (~400 frames) around the target time.
T32 was updated to use peak-over-window.

**Limiter masking NaN:** On x86 before the SVF fix, T31/T32 "passed" by
measuring 0.99 (limiter-clamped NaN). After the fix, T31 still passes because
genuine high-amplitude waveguide sustain is clamped by the limiter at 0.99.
T32 now measures the genuine pre-limiter waveguide amplitude via `ut_delay_read`.

### New Tests Added (T24–T33)

| Test | Description | Result |
|------|-------------|--------|
| T24a | 4 simultaneous NoteOns → 4 voices active | PASS |
| T24b | Round-robin assignment: notes 36/48/60/72 in distinct voice slots | PASS |
| T25a | AllNoteOff → all 4 voices enter is_releasing state | PASS |
| T25b | AllNoteOff → voices still is_active (not immediately killed) | PASS |
| T26 | MIDI note extremes (0 and 127): delay_length clamped, no NaN | PASS |
| T27 | Max Inharm (ap_coeff=0.9995): stable, no NaN over 500 blocks | PASS |
| T28a | Preset change mid-note preserves voice activity | PASS |
| T28b | No NaN after preset change | PASS |
| T28c | New GateOn after preset change produces sound | PASS |
| T29a | Soft hit (vel=1) produces nonzero pre-limiter output | PASS |
| T29b | Hard/soft ratio = 127.0 (confirms linear velocity scaling) | PASS |
| T30 | Dkay=0 still triggers audible output (fastest gate, g=0.85) | PASS |
| T31a | Dkay=200 sets feedback_gain ≥ 0.998 | PASS |
| T31b | String audible at 0.5 s with Dkay=200, Mterl=30 | PASS |
| T31c | String audible at 1.0 s — T_60 ≈ 26 s confirmed | PASS |
| T32a | Dkay=200 peak waveguide amplitude > 0.5 at 300 ms | PASS |
| T32b | Dkay=25 amplitude < Dkay=200 by > 10× ratio at 300 ms | PASS |
| T33 | Dkay→feedback_gain anchor points: 0→0.850, 100→0.9245, 200→0.999 | PASS |

**Total: 80/80 tests pass.**

---

## Phase 1 to 9: [COMPLETED]
- Core DSP, sample loading, structural physical modeling, and OS lifecycles finished.

---

## Phase 10: Bug-Fix Session — Root Cause of Hardware Silence [COMPLETED]

A full code audit was performed after confirming zero audio output on hardware.
Four confirmed bugs were found and fixed.

### BUG 1 — CRITICAL (Primary cause of silence): Init() leaves all DSP params at zero

**Root cause:** `Init()` calls `Reset()` which does `memset(&state, 0, ...)`.
This zeroes every field of `SynthState`, including inside all four `VoiceState` structs:

| Field zeroed | Effect |
|---|---|
| `mallet_stiffness = 0.0f` | Mallet exciter outputs exactly 0.0 — zero energy enters the waveguide |
| `feedback_gain = 0.0f` | No signal circulates in the delay loop — resonator is dead |
| `lowpass_coeff = 0.0f` | The 1-pole loss filter holds its state at 0 permanently — silences feedback |

With all three zeroed, the mallet produces no impulse, and even if any energy leaked
in, the feedback loop would be dead. The result is complete silence, every note.

**Fix:** Call `LoadPreset(0)` at the end of `Init()`, after the sample-function
pointers are stored. This runs `setParameter` for every slot, setting
`mallet_stiffness = 0.5f`, `feedback_gain ≈ 0.87f`, `lowpass_coeff ≈ 0.5f`, etc.

### BUG 2 — Chamberlin SVF formula catastrophically wrong (filter always near-Nyquist)

**Root cause:** `filter.h` `set_coeffs()` used `fastercosfullf`, which takes
**radians** (it is implemented as `fastersinfullf(x + π/2)`), not a [0,1]-normalised
fraction as the comment claimed. The formula:
```cpp
f = 2.0f * fastercosfullf(0.25f - (safe_cutoff / (2.0f * srate)));
```
actually computes `2·cos(0.25_rad − cutoff/(2·srate))`. Because 0.25 radians
is only slightly less than the cosine peak, the argument stays near 0 for all
audio frequencies and `f` is always in [1.91, 1.97] — regardless of cutoff.

The Chamberlin SVF stability limit is `f < 2`. With f ≈ 1.91, the filter has
an eigenvalue of ≈ 4.85, causing a ×5 blow-up every 3 samples. Both the
**master_filter** (LowCut) and the **noise_filter** (NzFltr/NzFltFrq) diverge
to NaN within ~60 samples of receiving any nonzero audio. The brickwall limiter
(`fmaxf(-0.99, fminf(0.99, NaN)) = 0.99`) silently masked the failure so the
synth appeared to work — every note immediately hard-clipped to ±0.99.

A previous partial fix changed the divisor from `srate` to `2*srate` (based on
the incorrect assumption about `fastercosfullf`'s input domain). That fix had
no meaningful effect: both versions produce f near 1.91 for all audio
frequencies and are equally unstable.

**Fix:** Replace the broken formula with `sinf()`, which is unambiguous and
only runs in the UI thread (setParameter) so its cost is negligible:
```cpp
f = 2.0f * sinf(M_PI * safe_cutoff / srate);
```
Correct f values for reference: 10 Hz → 0.0013, 1 kHz → 0.131,
5 kHz → 0.643, 10 kHz → 1.218, 20 kHz (clamped) → 1.975. All < 2, stable.

### BUG 3 — Reset() does not restore mix_ab default

**Root cause:** `Reset()` restores `master_gain = 1.0f` and `master_drive = 1.0f`
after the memset, but not `mix_ab`. The `SynthState` struct declares `mix_ab = 0.5f`
as its default, but memset overwrites that to 0. With `mix_ab = 0`, the voice
output formula `outA*(1−mix_ab) + outB*mix_ab` becomes `outA*1 + outB*0`, so
Resonator B is completely dropped from the mix regardless of the HitPos parameter.

**Fix:** Added `state.mix_ab = 0.5f` restoration inside `Reset()`.

### BUG 4 — Stale `read_pos` variable in process_waveguide()

**Root cause:** `process_waveguide()` computed `read_pos` (same formula as `read_idx`)
and never used it. Dead code — harmless but misleading.

**Fix:** Removed the unused variable.

---

## Phase 11: Independent Resonator B Control — Partls-selector strategy [COMPLETED]

The **Partls** parameter (index 8, range 0–6) now serves a dual role:
- Values **0–4**: set the active partials count ("4", "8", "16", "32", "64")
  and display with an A/B suffix so the user sees which resonator is targeted.
- Value **5**: switch edit context to **Resonator A** for subsequent parameter changes.
- Value **6**: switch edit context to **Resonator B** for subsequent parameter changes.

**Dkay**, **Mterl**, and **Inharm** now route to `resA` or `resB` independently,
determined by `m_is_resonator_a`. **Model** is also per-resonator (`m_model_a` /
`m_model_b`), updating `phase_mult` for each independently.

**Preset loading** always initialises both resonators symmetrically:
`LoadPreset` forces ResA context for the main parameter loop, then explicitly
mirrors Dkay/Mterl/Inharm to ResB, then restores the user's edit context.

**Reset** resets `m_is_resonator_a = true` so a cold start or Suspend always
leaves the engine in a clean, deterministic ResA-first state.

### Phase 11 bug-fix review [COMPLETED]
The following bugs were found and fixed after the initial Partls-selector commit:

- **Compile error**: `True` → `true` (C++ boolean literal).
- **Compile error**: Missing `}` closing the `if (index == k_paramModel)` block in
  `getParameterValue`, leaving `return m_params[index]` unreachable.
- **Compile error**: Missing `;` after `model_names_a[]` initialiser in
  `getParameterStrValue`.
- **Bug**: `getParameterStrValue` for Model used `model_names_a` for both
  resonators — now uses `model_names_b` correctly for ResB.
- **Bug**: `getParameterStrValue` for Partls values 5 and 6 fell through to "---"
  — now shows "-> ResA" / "-> ResB".
- **Bug**: `getParameterStrValue` for Partls used `m_active_partials` as the
  array index instead of the function's `value` argument — fixed.
- **Bug**: `getParameterStrValue` for Bank, NzFltr, and Program indexed into their
  arrays with stored state variables instead of the `value` argument — fixed.
  The function argument is always the value being *browsed*, not the stored state.
- **Bug**: `k_paramLowCut` handler lost its `/ 1000.0f` divisor for the resonance
  Q, passing 707–4000 to `set_coeffs` instead of 0.707–4.0. SVF was near-
  unstable. Fixed.

---

## Phase 12: Unused Parameters

### Implemented [COMPLETED]

* **`VlMllStf` (Index 7):** In `NoteOn`, overrides `mallet_stiffness` on the
  triggered voice: `stiffness = base_stiff + (VlMllStf/100) * velocity`.
  Harder hits produce a stiffer (brighter) mallet strike per-note without
  affecting concurrently ringing voices.
* **`VlMllRes` (Index 6):** In `NoteOn`, overrides `noise_env.attack_rate` on
  the triggered voice: positive values shorten the attack at high velocity
  so accent hits have a sharper transient.  Formula:
  `attack = base_attack + (VlMllRes/100) * velocity * 0.5`.
* **`MlltRes` (Index 4):** Wired as a second 1-pole LP cascaded after the
  existing `mallet_stiffness` LP.  High value → mallet energy passes through
  quickly (bright), low value → extra roll-off (dark/woody body).
  Both poles' state variables (`mallet_lp` and `mallet_lp2`) are reset to 0
  on each `NoteOn` to prevent clicks from polyphonic overlap.

### Previously Pending — now completed in Phase 13

* ~~**`Tone` (Index 12):** Unmapped.~~ → Tilt EQ (Phase 13).
* ~~**`Partls` values 0–4:** Only gated ResB on/off.~~ → Bidirectional coupling depth (Phase 13).
* ~~**`NzFltr` & `NzFltFrq` (Indices 21 & 22):** No dedicated noise filter.~~ → `FastSVF noise_filter` in ExciterState (Phase 13).
* ~~**`TubRad` (Index 17):** Unmapped.~~ → Combined with Mterl for `lowpass_coeff` (Phase 13).

---

## Phase 14: Code-Audit Bug-Fix Session [COMPLETED]

A second full audit was performed after the first round of UTs passed.
Six issues were found and fixed.

### FIX 1 — Critical: Squelch prematurely kills voice during delay-line transit

**Root cause:** The old squelch checked `fabsf(voice_out) < 0.0001f` while
`is_releasing` was set. For note 60 (C4) the delay-line round-trip takes ~183
samples (~3.8 ms). Any GateOff within that window produced silence because
`voice_out` was genuinely zero (the wave hadn't reflected yet) and the voice
was killed before a single audible sample emerged.

**Fix:** Replaced amplitude-threshold squelch with a **damper-pedal envelope**
approach. `master_env` is configured in `NoteOn` with `sustain_level=1.0` and
`decay_rate=0`, so it holds at **1.0×** during gate-on with no audible effect.
On GateOff/NoteOff it fades to 0 at the `k_paramRel` rate. The voice is marked
inactive only when `master_env.state == ENV_IDLE` — time-based, not
amplitude-based.

**Side-effect fix:** `k_paramRel` now audibly controls the physical-tail fade.
Previously the Release knob had no effect because `master_env` output was
commented out.

### FIX 2 — High: `Reset()` did not clear filter states `z1`, `ap_x1`, `ap_y1`

**Root cause:** `Reset()` zeroed `buffer[]` and `write_ptr` but left the 1-pole
LP state (`z1`) and allpass states (`ap_x1`, `ap_y1`) intact. A `Reset()` called
mid-play (OS pattern change) left non-zero filter memory, causing a click or DC
transient at the start of the next note.

**Fix:** Added explicit zeroing of all six filter-state fields inside the
`Reset()` per-voice loop.

### FIX 3 — Medium: FastSVF resonance lower-clamp mismatch

**Root cause:** `set_coeffs` clamped `resonance` to a minimum of `1.0`. The UI
`Resnc` bottom is `707` → `0.707` after `/1000`. The Butterworth flat-response
Q (0.707) was silently raised to Q=1.0 and the bottom half of the Resnc knob
travel had no effect.

**Fix:** Changed the lower clamp from `1.0f` to `0.5f`. The UI minimum 0.707
now passes through unchanged, giving a true Butterworth response at the
minimum knob position.

### FIX 4 — Low: Duplicate `is_active`/`is_releasing` assignment in `NoteOn`

**Root cause:** `is_active = true; is_releasing = false;` appeared twice —
once before and once after the sample-loading block that was inserted between
two existing copies of the same assignment.

**Fix:** Removed the redundant second copy.

### FIX 5 — Low: `m_params[25]` over-allocated by one element

**Root cause:** Declared with 25 slots despite only 24 parameters (0–23).
Index 24 was never read or written.

**Fix:** Corrected to `m_params[24]`.

### FIX 6 — Low: `k_paramMterl` guard missing lower-bound check

**Root cause:** `if (value <= 30)` accepted values below −10, though the inner
`fmaxf` clamped them safely. The asymmetric guard was inconsistent with the
`header.c` range.

**Fix:** Changed to `if (value >= -10 && value <= 30)`.

### Test fixes (test_hw_debug.cpp)

- **T9 hardcoded voice index:** `voices[1].resA` → `voices[s.state.next_voice_idx].resA`.
- **T9 missing from runner:** `test_delay_roundtrip()` added to `main()`.

---

## Phase 13: Parameter Activation & SVF Fix [COMPLETED]

Four parameters stored in `m_params` but previously producing no DSP effect
have been wired up, and the underlying SVF formula bug that made them unusable
has been corrected.

### SVF formula fix (filter.h)
`fastercosfullf` takes **radians**, not a [0,1]-normalised fraction (see BUG 2
above). The formula was producing f ≈ 1.91–1.97 for all audio frequencies,
making both the master filter and the noise filter catastrophically unstable
(eigenvalue ≈ 4.85, ×5 blow-up per 3 samples → NaN within 60 samples, silently
masked by the brickwall limiter). Fixed with `f = 2·sinf(π·fc/srate)`.

### Dedicated Noise SVF (NzFltr / NzFltFrq)
Added `FastSVF noise_filter` to `ExciterState` (inside `ENABLE_PHASE_6_FILTERS`
guard). `k_paramNzFltr` sets its mode (0=LP, 1=BP, 2=HP). `k_paramNzFltFrq`
sets its cutoff (clamped 20–20000 Hz). Applied to the noise burst in
`process_exciter()` before the noise scales by `noise_decay_coeff`.
Defaults on Reset(): LP mode, 12 kHz cutoff.

### TubRad + Mterl combined (k_paramTubRad / k_paramMterl)
The two parameters now share a `case k_paramMterl: case k_paramTubRad:` block.
Either change recalculates `lowpass_coeff` from both stored values:
- Mterl (−10 to +30) → base material brightness (0.01 = dull wood, 1.0 = lossless metal)
- TubRad (0 to 20) → pulls the coefficient towards 1.0 (wider tube = brighter)

### Partls as bidirectional coupling depth (k_paramPartls values 0–4)
UI values 0–4 (previously only gating ResB on/off) now also set cross-coupling
depth (0.0–1.0). ResA receives `exciter + resB_out_prev × depth × 0.5`;
ResB receives `exciter + outA × depth × 0.5`. The `m_active_partials >= 16`
CPU gate for ResB is preserved. `resB_out_prev` is reset on NoteOn and in the
`else` branch to prevent artefacts when ResB is inactive.

### Tone tilt EQ (k_paramTone, −10 to +30)
Per-voice 1-pole LP (coefficient 0.3, cutoff ≈ 2.7 kHz) extracts the low
component. Negative Tone blends toward LP (darker); positive Tone boosts the HP
component (brighter, up to 2× high-shelf gain at Tone=30). Applied after the
velocity scale, before the master envelope/limiter. `tone_lp` state is reset on
NoteOn.

### Unit tests (T10–T15)
- **T10** (structural): verifies NzFltr and NzFltFrq route to `noise_filter.mode`
  and `noise_filter.f` on all 4 voices. Structural (not audio) because sustained
  noise into the feedback resonator overflows float before a round-trip completes.
- **T11**: verifies TubRad raises `lowpass_coeff` above the Mterl-only value but
  stays below 1.0 (damping still present).
- **T12**: verifies both Partls=0 (ResA only) and Partls=4 (dual + coupling)
  produce sound.
- **T13**: uses pre-limiter `ut_voice_out` tap to verify Tone=30 gives a higher
  peak than Tone=−10.
- **T14**: verifies that `noise_filter.lp` and `.bp` are exactly 0.0 after both
  `Reset()` and `NoteOn()` even after noise injection has driven those states to
  NaN (confirmed by T14a: lp = −nan before the fix was applied).
- **T15**: verifies that `Partls=5` (select ResA) and `Partls=6` (select ResB)
  do not overwrite `m_coupling_depth`, which would silently enable full coupling
  whenever the user entered editor mode.

### Post-activation bug review (code review pass)
Two additional bugs found and fixed after the initial Phase 13 commit:

**BUG: noise_filter SVF state (lp/bp/hp) not cleared on Reset() or NoteOn()**
- `FastSVF.set_coeffs()` only updates `f` and `q`; it never zeroes `lp`, `bp`,
  or `hp`. Reset() called set_coeffs() and nothing else. NoteOn() didn't touch
  the noise filter state at all.
- Result: on the very first note the states start at 0 (in-class init), but
  every subsequent note reuses stale (and eventually NaN) filter memory, causing
  an audible click/pop on retrigger.
- Fix: both Reset() and NoteOn() now explicitly zero `lp`, `bp`, and `hp` inside
  the `ENABLE_PHASE_6_FILTERS` guard.

**BUG: Partls=5/6 (editor-select mode) silently set coupling depth to 1.0**
- The processBlock coupling formula read `m_params[k_paramPartls]` directly and
  clamped it: `fmaxf(0, fminf(4, value)) / 4`. When the user set Partls=5 or 6
  to enter the resonator-edit menu, `m_params` stored 5/6 and the clamp produced
  4/4 = 1.0 — full coupling — regardless of what the user had previously set.
- Fix: added `float m_coupling_depth` (private member, updated only when
  Partls < 5) and changed processBlock to use `m_coupling_depth` directly.

---

## Phase 15: Dynamic Squelch, Pitch Bend, Tone Cache & fasterpowf Fix [COMPLETED]

### Dynamic Energy Squelch

Per-voice 1-pole RMS follower (α = 0.01, τ ≈ 2 ms at 48 kHz) tracks `voice_out`
**before** the master-envelope fade. When a releasing voice's RMS drops below
`0.0001f` (≈ −80 dB) the voice is immediately set `is_active = false`, freeing
the CPU slot without waiting for the full release envelope to expire.

A 1000-sample guard (~20 ms) prevents the squelch from firing during the
delay-line round-trip, where `voice_out` is genuinely zero even though the
waveguide has energy in transit. Data fields added to `VoiceState`:
- `float rms_env = 0.0f` — RMS envelope follower state
- `float base_delay_A/B = 0.0f` — pre-bend root delay lengths (see Pitch Bend)

### Pitch Bend (MIDI 0–16383, ±2 semitone range)

`PitchBend(uint16_t bend)` maps centre=8192 → 0 semitones, full-up=16383 →
+2 st, full-down=0 → −2 st. The delay multiplier is `2^(−semitones/12)` (note
negative: higher pitch = shorter delay).

`base_delay_A/B` are stored at NoteOn time (before applying any held bend) so
that `PitchBend()` can always re-derive from the root pitch without accumulating
error across successive bend messages.

**BUG: `fasterpowf(2.0f, 0.0f)` ≈ 0.9714, not 1.0**
`fasterpow2f(p)` = `(uint32_t)(8388608 * (p + 126.94269504))` interpreted as
float. The constant 126.94269504 is slightly below 127, so at p=0 the result
decodes to ≈0.9714 instead of 1.0. Every centre-bend would silently detune the
voice downward by ~50 cents. Fixed by using an exact special-case for bend=8192:
```cpp
if (bend == 8192) {
    m_pitch_bend_mult = 1.0f;
} else {
    float semitones = ((float)bend - 8192.0f) * (2.0f / 8192.0f);
    m_pitch_bend_mult = powf(2.0f, -semitones / 12.0f);
}
```
`powf` is used in place of `fasterpowf` throughout PitchBend because this
function is called from the MIDI thread (not the audio loop) and accuracy
matters more than speed here.

`unit.cc` stub wired: `unit_pitch_bend` now calls `s_synth.PitchBend(bend)`.

Buffer-overflow guard: all delay_length assignments in NoteOn and PitchBend are
clamped to `[2, DELAY_BUFFER_SIZE−2]` to prevent out-of-bounds reads on low
notes bent downward (e.g. MIDI note 0 − 2 st → delay ≈ 6585 > 4096).

### Tone Parameter — audio-thread race condition fix

Reading `m_params[k_paramTone]` in `processBlock` on every sample was a data
race with the UI thread calling `setParameter`. Fixed by adding `float tone`
to `SynthState` (updated only in `setParameter`) and hoisting
`const float tone_val = state.tone` once per block before the voice loop.

### Unit tests T16–T18

- **T16a**: low-`feedback_gain` voice is killed within a bounded frame count
  after GateOff (energy squelch fires when RMS < −80 dB).
- **T16b**: a sustaining voice (no GateOff) stays active for 200 frames.
- **T17a/b**: PitchBend up/down proportionally shortens/lengthens delay_length.
- **T17c**: PitchBend(8192) (centre) restores delay_length to within 0.1
  samples of the pre-bend value (exact, because bend == 8192 → mult = 1.0f).
- **T18a**: a note struck while bend is held at max-up inherits the shorter
  delay immediately.
- **T18b**: centre-bend after a bent NoteOn restores the root pitch delay
  (base_delay_A stored correctly in NoteOn).

All 45 tests pass.

### Remaining future work

* **True Stereo Master Filter:** `master_filter` is a single mono `FastSVF`.
  Add a second instance to `SynthState` for the right channel so future stereo
  effects (chorus, panning) pass through a properly independent filter.
* **Voice 0 skipped on first note:** `NoteOn` increments `next_voice_idx`
  before assigning, so the first ever note goes to voice 1. Minor cosmetic issue.

---

## Phase 17: Partls AB/A/B Editor Selection + Bug Fixes [COMPLETED]

### User change: Partls extended from 0..6 to 0..7

`k_paramPartls` now has four distinct value bands:

| Value | UI label   | Effect |
|-------|-----------|--------|
| 0–4   | AB:N / A:N / B:N | Set partial count (4/8/16/32/64) and coupling depth |
| 5     | → ResA+B  | Route subsequent parameter edits to **both** resonators |
| 6     | → ResA    | Route subsequent parameter edits to ResA only |
| 7     | → ResB    | Route subsequent parameter edits to ResB only |

`m_is_resonator_b` added as second routing flag (alongside `m_is_resonator_a`).
All per-resonator `setParameter` handlers changed from `else` → `if (m_is_resonator_b)`
to support the independent-flag routing.  The partial-count and model display strings
now have three variants: `A:N`, `B:N`, `AB:N`.

### Bug fixes applied in this review pass

**1. LP group delay formula was wrong (Phase 16 regression)**

The Phase 16 pitch compensation used `(1+pa)/(1-pa)` for the 1-pole LP group
delay.  This is the allpass formula, not the LP formula.

Correct derivation: for H(z) = α/(1-pa·z⁻¹), phase φ = -arctan(pa·sinω/(1-pa·cosω)).
Group delay τ = -dφ/dω; at DC: τ_LP = pa·(1-pa)/(1-pa)² = **pa/(1-pa)** = (1-α)/α.

Sanity check: pa=0 (α=1, passthrough) → τ=0 ✓; pa→1 (dark) → τ→∞ ✓.
The old formula gave τ=1 for the passthrough case, which is wrong.

**2. `tables.h` used `fasterpowf` for pitch table generation (startup bug)**

`fasterpowf` has a ~3% systematic approximation error (the constant `fasterpow2(0) ≈
0.9714` offset makes every note about 50 cents flat before any filter compensation).
Since `generate()` runs once in `Init()` — not in the audio loop — there is no
performance justification for the approximation.  Changed to `powf`.

Combined effect of fixes 1+2: for the default preset at C4, the compensated delay
changes from 186.1 to 181.46 samples, and T19 confirms the effective loop period
is **183.4683 samples = 48000/261.626 Hz → pitch error 0.00 cents**.

**3. Partial name string typos**

`partial_names_a[]`, `partial_names_b[]`, and `partial_names_ab[]` had truncated
two-digit numbers at indices 2–4:
- Index 2: `"A:6"` → `"A:16"`
- Index 3: `"A:2"` → `"A:32"`
- Index 4: `"A:4"` → `"A:64"`

**4. `m_is_resonator_b` not saved/restored in `LoadPreset`**

After a preset load `m_is_resonator_b` was always left as `true` (from the ResB
application block), overriding whatever editing mode the user had selected.  Now
both `m_is_resonator_a` and `m_is_resonator_b` are saved and restored around the
preset application, preserving the user's editing context.

### New test: T19 — pitch compensation accuracy

Verifies that `delay_length + τ_LP + τ_AP` equals the equal-temperament period
for C4 within 2 cents.  With the corrected LP formula and `powf` table the error
is 0.00 cents.  46/46 tests pass.

---

## Phase 18: Hardware Silence — Same-tick GateOn+GateOff Fix [COMPLETED]

Two bugs caused complete silence on hardware even after all Phase 10–17 fixes.
Root-cause analysis confirmed by T20 (peak=0.837, added to the test suite).

### BUG 1 — CRITICAL: master_env killed at value=0 on same-tick GateOff

**Root cause:** The Drumlogue uses a one-shot drum trigger model: `gate_on` and
`gate_off` both fire in the same scheduler tick, before any audio block is
rendered. The sequence was:

1. `NoteOn` → `trigger()` → `value=0, state=ENV_ATTACK`
2. `GateOff` → `release()` → `state=ENV_RELEASE, value still 0`
3. First `processBlock` → `process()` sees `ENV_RELEASE, value=0` → threshold
   `value <= 0.001f` is immediately true → `state=ENV_IDLE`, returns 0.0f
4. `voice_out *= 0.0f` → permanent silence on every note

**Fix:** In `NoteOn`, call `v.exciter.master_env.process()` once immediately
after `trigger()`. With `attack_rate=0.99`, one call advances `value: 0 → 1.0`
and transitions `state: ENV_ATTACK → ENV_DECAY`. If `release()` fires before
the first audio block, it finds `value=1.0` and the note fades out normally
through `ENV_RELEASE`.

**Code safety:** The pre-advance call has no unintended side effects. The
`master_env` is freshly configured (attack/decay/sustain set, trigger called)
immediately before, so there is no stale state to pick up. The `ENV_DECAY`
state with `decay_rate=0.0f` and `sustain_level=1.0f` is a fixed point —
`process()` returns 1.0f on every subsequent call until `release()` is
called — so the one pre-advance sample is indistinguishable from the steady
hold state. The feedback loop and pitch are unaffected.

### BUG 2 — HIGH: process_waveguide returned delay_out instead of new_val

**Root cause:** A previous commit changed `return new_val` to `return delay_out`
in `process_waveguide()`. This caused 4 ms of silence before the first
audible sound on every note (the delay-line round-trip at ~182 samples for
note 60). With `return delay_out`, the output tap is placed at the read point
of the delay line, so nothing is heard until the exciter signal has propagated
all the way around the loop and been read back.

**Fix:** Restored `return new_val`. The exciter signal (sample + mallet) passes
through immediately on frame 0. Both PCM sample audio and mallet percussion
benefit equally. The fundamental pitch is still determined by `delay_length`;
changing the output tap from the read point to the write point does not affect
the feedback loop or its stability.

**Physical model invariant:** The delay line feedback path is:
`read → AP → LP → (× feedback_gain × phase_mult) + exciter → write`.
`new_val` is the value written into the buffer. Returning `new_val` is
equivalent to placing the output tap at the write head — standard in many
waveguide synthesis designs. The loop gain and stability are determined solely
by `feedback_gain` and `lowpass_coeff`, which are unchanged.

### New test: T20 — same-tick GateOn + GateOff

Directly validates the hardware trigger model: `GateOn` is called immediately
followed by `GateOff` with no audio frames in between. The test then renders
300 frames and checks that peak output > 1e-4f.

Result: peak = 0.837, PASS. **47/47 tests pass.**

---

## Phase 16: Physical Model Review & DSP Correctness Pass [COMPLETED]

Full audit of the digital waveguide physical model against known physical
acoustics theory. Eight improvements applied; 45/45 tests pass.

### 1. Coupling symmetry fix (correctness)

The old coupling fed ResA with ResB's previous-sample output (1-sample delay)
but fed ResB with ResA's current-sample output (zero delay). This asymmetry made
the two resonators physically non-reciprocal — ResB always "heard" ResA one
sample ahead — creating a subtle formant artefact at high coupling depths.

Fix: added `float resA_out_prev` to `VoiceState`; both resonators now use the
previous sample's output of the other, making the coupling symmetric and
physically correct.

### 2. Membrane inharmonicity ratio (correctness)

`resB.delay_length = base_delay * 0.68f` gave a second mode ratio of 1/0.68 ≈
1.47. Real circular membranes have overtone ratios determined by zeros of the
Bessel function J_mn. The dominant second mode (1,1) has ratio ≈ 1.5926 →
1/1.5926 ≈ **0.628**.

Fix: changed multiplier from `0.68f` to `0.628f`.

### 3. Filter order in the waveguide loop (correctness)

Old order: LP → AP → write. The AP (dispersion) operated on an already
frequency-attenuated signal, reducing its audible inharmonicity at high
frequencies.

Physical order: AP models in-medium wave propagation (stiffness); LP models
boundary absorption (reflection loss). Correct order: **AP → LP → write**.

Fix: swapped the two filter stages in `process_waveguide()`. The feedback write
now uses `filtered_out` (LP output) rather than `ap_out` (AP output).

### 4. `while` → `if` for delay-line read pointer (performance)

`delay_length` is clamped to [2, 4094], so `read_idx ≥ −4094`. One addition
of `DELAY_BUFFER_SIZE` (4096) always puts it in range. The `while` loop body
can execute at most once; using `while` added an unnecessary backward branch in
the innermost hot loop.

### 5. Linear interpolation Horner form (performance)

`(a * (1-f)) + (b * f)` → `a + f * (b - a)`: one multiply + two adds instead
of two multiplies + one add. Saves one multiply per sample per active resonator.

### 6. Mallet LP gate — denormal prevention (performance)

The two cascaded mallet-shaping LP filters ran every sample for the full voice
lifetime even though their state decays to ≈ sub-denormal within ~250 samples.
Added a gate (`mallet_lp2 > 1e-6f`) that skips both LP updates and the
`* 15.0f` add once the mallet has fully settled. Eliminates wasted CPU and
prevents denormal stalls on non-FTZ hardware.

### 7. `rms_env` renamed `mag_env` (naming accuracy)

The envelope follower smooths `|x|` (mean absolute value), not `x²` (which
would be RMS). Renamed to `mag_env` throughout (`dsp_core.h`,
`synth_engine.h`). Behaviour is identical; only the name reflects what the
code actually computes.

### 8. Loop filter pitch compensation (tuning accuracy)

The 1-pole LP and allpass both add group delay at the fundamental frequency ω₀,
making the actual loop period longer than `delay_length` samples and the pitch
correspondingly flat. For the default preset (lowpass_coeff = 0.604), the error
was ≈ 35 cents flat at C4.

Using the DC-limit approximation (valid for all MIDI notes at 48 kHz since
ω₀ ≪ 1 rad/sample): τ_LP ≈ (1 + pole)/(1 − pole) and τ_AP ≈ (1 + c)/(1 − c).

Fix: after computing the nominal delay from the lookup table, NoteOn subtracts
(τ_LP + τ_AP) from each resonator's `delay_length` before storing it as the
base for PitchBend. The corrected delay for C4 changes from 189.8 to 186.1
samples — 3.7 samples shorter — bringing pitch error from −35 cents to < 1 cent.

### Pre-existing compile errors also fixed

Two `static constexpr` declarations were missing their `float` type keyword
(introduced in a user commit). The `ProgramIndex` enum had enum values with
spaces in their names and no commas — both invalid C++. Fixed: added `float`,
replaced spaces with camelCase, added commas, renamed the count marker from
`k_ProgramIndex` to `k_NumPrograms` to avoid shadowing confusion.

---

## Phase 1: Core DSP Structures [COMPLETED]
- [x] Define flat, cache-friendly C++ structures (`Waveguide`, `Exciter`, `Voice`).
- [x] Establish memory bounds (4096-sample delay line for safe sub-bass).

## Phase 2: API & UI Binding [COMPLETED]
- [x] Connect `header.c` parameter indices to the engine.
- [x] Write `setParameter` translation logic.
- [x] Fix `MlltStif` buffer overflow bug in UI header.

## Phase 3: The Audio Processing Loop [COMPLETED]
- [x] Write the per-sample DSP loop.
- [x] Implement linear interpolation for accurate pitch tuning.

## Phase 4: Architectural Refactor & Sample Management [COMPLETED]
- [x] JIT sample loading inside `NoteOn` safely implemented.
- [x] Hardware "Hello World" successfully compiled and tested on Drumlogue!

## Phase 5: Envelopes & Exciters [COMPLETED]
- [x] `envelope.h` and `noise.h` implemented and tested on hardware.
- [x] Noise mix and PCM sample triggering confirmed working.

## Phase 6: Filters & Master FX [COMPLETED]
- [x] `filter.h`: Implement a 2-pole Chamberlin State Variable Filter (SVF).
- [x] Tie `header.c` LowCut and Resonance parameters to the Master SVF.
- [x] Integrate SVF incrementally into the audio loop.

## Phase 7: Waveguide Models & Tables [COMPLETED]
- [x] `tables.h`: Defined fast-math lookup tables for MIDI-to-Delay-Length conversion.
- [x] Implemented branchless Tube physics (Inverting Feedback via `phase_mult`).
- [x] Implemented Membrane physics (Inharmonic irrational detuning of Resonator B).
- [x] Linked UI `Model` parameter to physical topologies.

## Phase 8: Preset Design & Acoustic Tuning [PENDING]
- [ ] Reverse-engineer waveguide parameters for legacy 28 presets.
- [ ] Derive coefficients for new instruments: Timpani, Djambé, Taiko, Marching Snare, Tam Tam, Koto.

## Phase 9: UI Polish & Missing SDK Hooks [COMPLETED]
- [x] Preset Management: `LoadPreset()`, `getPresetIndex()`, `getPresetName()`.
- [x] State Reporting: `getParameterValue()` for OLED display sync.
- [x] Parameter Linkage: all core `header.c` knobs wired in `setParameter`.
- [x] Release Phase Logic: `NoteOff`, `GateOff`, `AllNoteOff`, master envelope VCA.
- [x] Free Parameter Decision: Gain slot → overdrive drive multiplier (1×–21×).

## Phase 10: Bug-Fix Session [COMPLETED]
- [x] Fix Init/Reset silence: `LoadPreset(0)` called at end of `Init()`.
- [x] Fix Chamberlin SVF formula: divisor corrected to `2*srate`.
- [x] Fix `Reset()` not restoring `mix_ab = 0.5f`.
- [x] Remove dead `read_pos` variable from `process_waveguide()`.
- [x] Document mono-filter intentional L-copy pattern with TODO for true stereo.

## Phase 11: Independent Resonator B Control — Partls-selector [COMPLETED]
- [x] Partls 0–4 = partial count; 5 = select ResA edit; 6 = select ResB edit.
- [x] Dkay, Mterl, Inharm route to resA or resB based on m_is_resonator_a.
- [x] Model is per-resonator (m_model_a / m_model_b), phase_mult updated independently.
- [x] LoadPreset: forces ResA context, mirrors Dkay/Mterl/Inharm to ResB, restores context.
- [x] Reset: resets m_is_resonator_a = true for deterministic cold start.
- [x] Fix: True → true (compile error).
- [x] Fix: Missing } in getParameterValue (compile error / unreachable return).
- [x] Fix: Missing ; after model_names_a[] init (compile error).
- [x] Fix: Model B showing model_names_a instead of model_names_b.
- [x] Fix: Partls values 5/6 showing "---" — now "-> ResA" / "-> ResB".
- [x] Fix: getParameterStrValue used state vars instead of value arg (Bank, NzFltr, Program, Partls).
- [x] Fix: k_paramLowCut dropped /1000.0f for Q — SVF near-unstable. Restored.

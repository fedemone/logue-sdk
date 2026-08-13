# Brachetti (Drumlogue Bare-Metal DSP)

> Formerly **RipplerX-Waveguide**; renamed in honour of quick-change
> performer Arturo Brachetti (July 2026).  `dev_id`/`unit_id` are unchanged,
> so the renamed unit replaces an installed RipplerX build in place.

## Overview
Polyphonic Physical Modeling synthesizer for the Korg Drumlogue. Strictly **Data-Oriented Design**: fixed memory, branchless math, ARM NEON SIMD, respects the ~20 µs RTOS audio deadline. **41 presets** spanning strings, bars, membranes, metallic plates, cymbals, snares, and idiophones.

Six engine families route each preset to its own signal path
(`kPresetEngine[]` in `synth_engine.h` is the authority):

| Engine | Path | Examples |
|---|---|---|
| `ENGINE_KS` | Karplus-Strong waveguide (+ modal overtones) | GtrStr, Koto |
| `ENGINE_BAR` | Mallet exciter → bar modal bank | Marimba, Kalimba, Claves |
| `ENGINE_MEMBRANE` | Strike → membrane modal bank + boom osc | Kick family, Djambe, Conga |
| `ENGINE_SNARE` | Membrane body + 3-band snare-wire resonators | AcSnre, MrchSnr, BrshSnr, RimShot |
| `ENGINE_NOISE` | Shaped noise burst (+ AM gating) | Clap, Shaker, HHat-C |
| `ENGINE_CYMBAL` | Dense inharmonic resonator bank (Stowell-style) | Cymbal, Gong, Ride, RidBel, HHat-O, Splash |

Timpani and Taiko additionally bypass the voice loop entirely and render
through the dense coupled-resonator **ModalDrumKernel**
(`modal_drum_kernel.h`, ~280 modes + recorded knock + noise wedge — see the
appendix), which owns its own master stage with a transparent peak limiter.

---

## Signal Flow Architecture (legacy voice path)

The diagram below is the KS/BAR/MEMBRANE/SNARE/NOISE voice path.
`ENGINE_CYMBAL` presets replace it with the self-contained dense-resonator
cymbal voice (`CymbalVoice` in `dsp_core.h`), and Timpani/Taiko with the
`ModalDrumKernel` — both still pass through the tilt EQ and master chain
(the kernel runs its own master stage).

```text
NoteOn / Gate trigger
         │
         ▼
┌──────────────────────────────────────────────────────────────┐
│ EXCITER STAGE                                                │
│  ├─ Dual-noise burst: LP-filtered low + unfiltered high,    │
│  │   blended by noise_band_mix per preset                   │
│  ├─ Mallet: two cascaded LP pulses, velocity-scaled,        │
│  │   gated after decay (denormal prevention)                │
│  ├─ Snare wire rattle: 3-band (lo/mid/hi) resonator,        │
│  │   velocity-dependent Q, body-coupled excitation          │
│  ├─ Metallic FM chirp: per-voice transient frequency sweep  │
│  │   (Cymbal, Gong, HHat, Ride, Triangle, BellTree)         │
│  ├─ Boom oscillator: low-body sine envelope                 │
│  │   (Kick, Timpani, AcTom, AcSnare)                        │
│  └─ Stage-2 modal bank: 2–6 decaying oscillators via        │
│     2nd-order recursion (y[n]=k·y[n-1]−y[n-2])             │
│     with T60-style per-mode decay; coupled to FM env        │
└──────────────────────────┬───────────────────────────────────┘
                           │
          ┌────────────────┴────────────────┐
          │ (A/B Split, optional coupling)  │
          ▼                                 ▼
┌──────────────────────┐       ┌──────────────────────┐
│ RESONATOR A          │◄─────►│ RESONATOR B          │
│ ├─ 4096-sample delay │       │ ├─ 4096-sample delay  │
│ ├─ Allpass dispersion│       │ ├─ Allpass dispersion │
│ ├─ 1-pole LP loss    │       │ ├─ 1-pole LP loss     │
│ ├─ loss_g_dc / hf   │       │ └─ Optional (Partls≥1)│
│ │  split sustain vs  │       └──────────────────────┘
│ │  brightness         │
│ └─ Pitch compensation│
│    (LP + AP group    │
│     delay subtracted)│
└──────────┬───────────┘
           │
┌──────────▼─────────────────────────────────────────┐
│ MASTER SHAPING                                     │
│  ├─ Tilt EQ (Tone param: LP/HP blend)              │
│  ├─ Master envelope VCA (damper-pedal model)       │
│  ├─ Magnitude squelch (−80 dB threshold)           │
│  └─ Brickwall limiter (NaN safety net)             │
└──────────────────────────┬─────────────────────────┘
                           ▼
                    TO DRUMLOGUE OS
```

---

## Key Architectural Decisions & Quirks

### Allpass formula — critical sign convention
The allpass is `H(z) = (c + z⁻¹) / (1 + c·z⁻¹)`.  
DC group delay = `(1 − c) / (1 + c)`, **not** `(1 + c) / (1 − c)`.  
Getting this wrong (as happened in Phase 16, fixed in Phase 17) causes systematic pitch sharpness proportional to the InHm setting.

### SVF: TPT (Zavalishin), not Chamberlin
`filter.h` implements a TPT (topology-preserving transform) SVF — the bilinear-transform discretisation of the analog SVF, unconditionally stable and frequency-accurate to Nyquist.  The previous Chamberlin SVF had a stability bound `f < √(4 + q²) − q` that capped the usable cutoff at ~8.2 kHz (Q = 0.707): every cutoff above that was clamped onto the stability boundary, freezing the filter into a lightly-damped ~8 kHz resonator whose output got LOUDER as the cutoff was raised — the "LowCut/NzFltFrq work in reverse" hardware report.  Note the Chamberlin BP near Nyquist also had a real centroid of ~18 kHz instead of fc; hat presets tuned against that behaviour were recalibrated when the TPT landed (HHat-C hat HP@6 kHz, HHat-O hat BP@12 kHz).

### Dual-band noise split is post-filter
Both noise bands (slow body via `noise_env`, fast click via `noise_env_hi`) derive from the SVF-coloured noise; `noise_hi_lp_coeff` (per-preset) only sets the body/sizzle split corner.  Historically the high band was split from the UNFILTERED source with the corner tied to 2.2×NzFltFrq — so the dominant sizzle branch ignored the user filter and raising the cutoff *removed* sizzle (reversed response).  Hat-family presets (`use_hat_filter`, engaged when `noise_band_mix > 0.8`) keep their dedicated `hat_filter` on the unfiltered source for centroid control.

### Modal-engine parameter wiring (REFERENCE-ANCHOR)
For BAR/MEMBRANE/SNARE/PLATE engines the following UI parameters reshape the modal bank, all pivoted at the preset's shipped knob values (captured in `LoadPreset`) so the calibrated `modal_preset_configs` sound is bit-identical at defaults:
- **Model** → swaps mode-frequency ratios for the selected physical model's template (`kModelModalRatios`: harmonic string, free-free bar, Chladni square plate, Bessel membrane, thick circular plate, kettledrum quasi-harmonics, tuned bar 1:4:10, tube harmonics).
- **Partls** (0–4) → mode count offset around the shipped count, clamped [2, 6]; missing ratios/T60s/envelopes fall back to the model template and geometric decay.
- **Inharm** → overtone spread `ratio' = 1 + (ratio − 1) × spread` around the fundamental.
- **Mterl** → material damping of modes ≥ 2 (`T60 × 2^(1.5·Δ)`): metal sustains overtones, wood damps them.  Mode 1 stays Dkay's job.
- **HitPos** → strike-position excitation tilt: rim strikes feed the upper modes, centre strikes the fundamental.
Remaining KS-only parameters: TubRad, MlltRes (exciter-transient only on modal engines), Rel (noise tail only).

### `fasterpow2(0) ≈ 0.9714` — not 1.0
The fast-power approximation has a ~3% systematic error at `p = 0`. Every center-bend pitch message was 50 cents flat before the fix. `PitchBend()` uses an exact `if (bend == 8192) mult = 1.0f` special case. `tables.h` uses `powf` (not `fasterpowf`) for the MIDI-to-delay lookup table.

### Pitch compensation at NoteOn
Both the 1-pole LP loss filter and the allpass dispersion add group delay at the fundamental, making actual pitch flat. NoteOn subtracts `τ_LP + τ_AP` from `delay_length`:
- `τ_LP = pa / (1 − pa)` where `pa = 1 − lowpass_coeff`
- `τ_AP = (1 − c) / (1 + c)` for the allpass `H(z) = (c + z⁻¹)/(1 + c·z⁻¹)`

At C4 this correction is ~3.5 samples; without it pitch error is ~35 cents flat.

### KS loss split: `loss_g_dc` / `loss_g_hf`
The waveguide feedback uses two independently controlled coefficients:
- `loss_g_dc` — low-frequency sustain (DC loop gain)
- `loss_g_hf` — high-frequency brightness (HF branch decay)

This lets sustain and spectral evolution be tuned independently, which is essential for metallic sounds where HF dies much faster than the fundamental.

### Stage-2 modal bank
Parallel to the main KS loop, a bank of 2–6 decaying oscillators (preset-specific) uses the 2nd-order harmonic recursion `y[n] = k·y[n-1] − y[n-2]` where `k = 2·cos(ω)`. State is initialized from `modal_preset_configs[]` (inharmonic frequency ratios + T60 per mode). Modal mix is boosted during the transient window by the metallic FM envelope for a stronger attack "opening" that naturally decays to the steady-state mix.

### Boom oscillator (Kick, Timpani, AcTom, AcSnare)
A sine oscillator with a fast-decay envelope injects low-body energy (40–100 Hz) on NoteOn. This is separate from the KS loop and avoids the high-cut bias of the 1-pole LP at low frequencies. Essential for kick/tom thud character that the waveguide alone under-produces.

### Kick "thump" (beater-impact punch, July 2026)
The boom gives the kick's *sub*, but the mallet knobs did almost nothing on
the kick family (Kick2, 808Sub, KickDrum) — the mallet click is a tiny high
tick, not a mid punch — so there was no way to dial in "thump". A dedicated
`thump_*` layer adds a fast pitch-dropping mid sine (~300→115 Hz, T60 ≈ 58 ms)
over the boom, wired to the mallet knobs the user reaches for:
- **`MlltRes`** → thump **amount** (also still boosts modal body presence)
- **`MlltStif`** → thump **snap / pitch** (higher = a brighter knock)
- **`VlMllRes`** → **velocity-weighted** thump prominence (accents punch harder)
- **`VlMllStf`** → **velocity-weighted** beater snap (adds its own knock, lifts
  the thump pitch and shortens its ring as you hit harder)

Reference-anchored: at the shipped knob values the thump is exactly zero, so
the shipped kicks render bit-identical ("perfect boom" preserved) and only
turning the mallet knobs *up* adds the punch. Because the master chain is
loudness-maximized (soft-clip), the thump reshapes the attack timbre rather
than raising the peak (added-signal RMS ≈ 0.10–0.19 over the first 100 ms).

#### Thump without boom

`Mterl` is the kick's boom **weight**, and turning it *down* now fades the boom
all the way to silence at the knob floor (the downward curve is quadratic in
`Mterl/ref`, continuous with ×1 at the shipped value; upward keeps the gentle
`2^(1.2·Δ)` lift). So a **thump-only kick** — a dry beater knock with almost no
sub — is `Mterl` fully down + `MlltRes` (and/or `VlMllRes`) up:

| Setting | Result (808Sub, measured) |
|---------|---------------------------|
| shipped | total RMS 0.215, boom tail (0.2–1 s) 0.084 |
| `Mterl` = −10 | total RMS 0.010, boom tail **0.000** — boom gone |
| `Mterl` = −10, `MlltRes` = 1000 | attack RMS 0.192, tail 0.000 — **all thump, no boom** |

### Per-family knob wiring for the body-shaping params (July 2026)

An empirical audit (`param_audit.cpp`) found the body-shaping knobs — `Dkay`,
`Mterl`, `HitPos`, `Rel`, `Inharm`, `TubRad`, `Resnc` — were **dead on two
whole families** because those engines don't use the shared modal bank the
knobs feed: the **kick** voice is the boom oscillator, the **cymbal** family
is the self-contained dense-resonator port, and **Timpani/Taiko** run the
separate dense drum-kernel. Each family now maps the dead knobs to a natural
property of *its own* engine, all **reference-anchored** (delta from the
shipped knob value → the 40 shipped presets stay byte-identical; only knob
movement bites):

| Knob | Kick (boom) | Cymbal (dense resonator) | Kernel drums (Timpani/Taiko) | Legacy membrane / bar / plate |
|------|-------------|--------------------------|------------------------------|-------------------------------|
| Dkay | boom decay length (coarse) | overall ring decay | T60 (coarse) | modal T60 (coarse) |
| Rel | boom decay length (fine) | sizzle/wash tail | T60 (fine) | modal T60 (fine) |
| Mterl | boom body weight | metal brightness (HF tilt + ceiling) | material HF tilt (widened) | upper-mode material damping |
| HitPos | beater click (bright tick) | edge↔bell (wash vs stick ping) | knock click | strike-position mode tilt |
| Inharm | pitch-dive depth (808Sub) | jitter spread / shimmer density | upper-mode stretch | overtone spread |
| TubRad | boom base tune (shell size) | instrument size (spectrum transpose) | body size (longer + darker) | whole-body ring length |
| Resnc | master-LP Q¹ | master-LP Q¹ | master-LP Q¹ | master-LP Q¹ |

¹ `Resnc` is the **master low-pass resonance** (`master_filter` Q).  It only
becomes audible when you also bring `Cutoff` down so the resonant peak sits in
the audible band; with `Cutoff` fully open — the shipped default on **every**
preset — the peak sits up near 16 kHz and `Resnc` does effectively nothing.
That is why it reads as "no effect" on every preset until you close the filter.
`Inharm` on the kick drives the 808-style pitch dive, so it is strongest on
`808Sub` (the sweep kick) and deliberately light on Kick2/KickDrum, whose
calibrated boom is left untouched.

### Encoder-step coarsening & polyphony (July 2026)

- **`Inharm` step 1 → 10.** Like `Dkay`/`Resnc`/`MlltStif`, `Inharm` is now
  stored ÷10 (range 0–199, display ×10 = 0–1990) so the encoder dials the
  useful range ~10× faster.  All shipped presets render byte-identical except
  `Koto`, the only KS preset with a non-zero shipped `Inharm` (1 → 0 after the
  ÷10 rounding): its string-diffusion `ap_coeff` shifts 0.0005 → 0, an
  inaudible −59 dB change.
- **MEMBRANE / SNARE / NOISE now stack.**  Fast repeats used to choke a single
  voice (mono retrigger); they now round-robin across the 4 voices so kick,
  tom, conga, bongo, snare and clap hits overlap (roll tails, flams).  Heavy
  engines keep their guards: `ENGINE_CYMBAL` is bounded by the `Poly` knob and
  a resonator budget (see below), the Timpani/Taiko kernel runs its own
  2-kettle path, and `ENGINE_KS` stays mono (avoids same-pitch string beating).
  Peaks across all presets remain limiter-bounded (rapid 4-hit stacks peak
  < 0.84).

#### Cymbal / gong stacking — the 2-voice cap and the inverted steal

HW: *"multiple gong hits are not stacking correctly."*  Two independent bugs
compounded, and both were in the `ENGINE_CYMBAL` voice policy:

1. **The family was hard-capped at 2 voices** (`m_cym_poly = min(Poly, 2)`), a
   CPU guard from before the magnitude squelch existed.  A gong — the one
   instrument in the unit whose hits are *supposed* to pile up — could never
   have more than two strikes sounding.
2. **The steal rule inverted itself on every slow-swelling cymbal.**  At the cap
   the voice with the smallest `magEnv` was stolen.  `magEnv` is a ~10 ms
   one-pole of `|out|` that starts at 0, and the gong's driver takes
   `lowAttackSec` = 0.25 s to open — so for the first third of a second the
   NEWEST hit is by far the quietest voice in the bank.  The third strike
   therefore killed the second one mid-bloom, and repeated hits ping-ponged
   between two slots instead of accumulating.

The fix, in three parts:

| Change | Effect |
|---|---|
| `m_cym_poly = m_poly` | the cymbal family honours the full `Poly` range (1-4) |
| `kCymStealProtectSec` (0.60 s) | voices younger than this are off-limits; inside the window the OLDEST is stolen, never the freshest.  The window is deliberately wider than the 0.25 s attack — `magEnv` averages a dense inharmonic wash, so for the first half-second age is the reliable ordering and level is not |
| `kCymResonatorBudget` (240) | hard ceiling on the TOTAL resonator count across simultaneous cymbal voices.  Bounding the aggregate is a *stronger* CPU guarantee than the old voice cap, which is what makes 4 voices affordable; already-ringing banks keep their size and the newest voice absorbs the trim |

A restruck or stolen cymbal voice also **keeps its ring** now (`retainRing` in
`cymbal_note_on`, retained state scaled ×0.75 for headroom): metal does not
reset, and zeroing `resY1/resY2` is what made a stacked hit read as "the
previous one disappears".  A fresh voice still starts from silence, so the
shipped single-hit renders are unaffected.

Measured (Gong, 4 hits): distinct voices 2 → **4**; passage RMS vs a single hit
1.62× → **1.84×** at 150 ms spacing and 1.90× → **2.02×** at 1 s.  Eight hits at
150 ms reach 2.24×.  Peak stays limiter-bounded at 0.83.  Worst aggregate bank
measured 208 resonators at max density (then `Rsntrs` = 60, now `Partls` = 7;
budget 240).  Regression-tested as
**T36** in `test_hw_debug.cpp` (T36a fails against the pre-fix code).

#### Roll fusion — the exception to stacking

Stacking is right for flams and roll *tails* and wrong for a **pressed roll**.
At 15–25 strokes/s an unconditional round-robin gave every stroke its own voice,
so up to `Poly` complete drum bodies — each with its own crack / slap / beater
burst — piled up: HW reported *"pressed rolls feel less smooth, Djambe
especially is muddy."*  It also made the snare **buzz-roll wire continuity**
unreachable, since that mechanism can only fire when the slot being reused is
the one still rattling.

`NoteOn` therefore fuses fast repeats before the round-robin advances:

| Condition | Behaviour |
|---|---|
| Same note, drum family, gap < `kRollFuseSec` (80 ms) | **reuse** the last voice — one continuous roll |
| Gap ≥ `kRollFuseSec` | **stack** (round-robin, as above) |
| Different note | **stack** — a fast pitched figure is not a roll |
| `ENGINE_CYMBAL`, `ENGINE_BAR`, `ENGINE_PLATE` | **always stack** — for a cymbal swell or marimba roll the overlap *is* the sound |

`kRollFuseSec` is a single constant shared by the fusion test and the snare
wire-state restore, so the two can never disagree about what counts as a roll.
Measured (12 strokes, Djambe): 45 ms apart → 1 voice, passage RMS 0.444 → 0.408;
AcTom 0.535 → 0.421.  Everything at or beyond 85 ms is **bit-identical** to the
unfused behaviour.  Regression-tested as **T35** in `test_hw_debug.cpp`.

### Knob depth pass — "from subtle to live" (July 2026)

Every reference-anchored knob mapping in the unit had its response coefficient
widened and its clamps opened.  The anchoring is untouched, so **all 40 shipped
presets still render byte-identical** — only the travel either side of each
preset's shipped value grew.

| Area | What changed |
|---|---|
| Modal engines (bar / membrane / snare / plate) | `Dkay` 3.5→4.5 and `Rel` 2.5→3.2 on the T60 scale, `Mterl` 2.5→3.4, `Inharm` spread 1.6→2.4, `TubRad` 1.2→1.9, `MlltRes` presence 2.6→3.4, `MlltStif` tilt 2.4→3.4, `HitPos` mode tilt ×1.5 |
| Kernel drums (Timpani / Taiko) | `Dkay`/`Rel` 3.5/2.5→4.5/3.2, `Mterl` 2.0→3.0, `TubRad` 1.2→1.9, `Inharm` 1.6→2.4, `MlltStif` 2.0→3.0, knock 2.6/1.2/2.2→3.4/1.9/3.0 |
| Cymbal family | `Dkay` 2.0→3.0, `Rel` 1.6→2.4, `Mterl` 2.5→3.6 (+ ceiling 0.7→1.3), `HitPos` 1.5/0.8/−1.0→2.4/1.4/−1.7, `TubRad` 0.7→1.2 |
| Snare wire | `Rel` −3.0→−4.0, `MlltRes` 2.0→3.0, `MlltStif` 1.0→1.6, `VlMllStf` Q 0.09→0.12, `VlMllRes` crack 3.0→4.0, `TubRad` body −1.3→−1.9 (the asymmetric ×1.15 thinning cap is unchanged — it exists because HW called the up-shift "toy-ish") |
| Kick | boom length 2.2/1.6→3.0/2.2, weight 1.2→1.9, tune −0.6→−1.0, dive 3.0→4.5, click 2.5→3.8, thump amount 0.90/0.55→1.35/0.85 and pitch 1.2/0.9→1.8/1.4 |
| Tom slap | `VlMllRes` 2.0→3.0, standalone slap burst 0.5+1.3·v → 0.8+1.9·v |

**Two knobs that were fully dead are now live:**

- **`MlltRes` on the cymbal family → RING PRESENCE.**  The dense-resonator port
  has no mallet exciter at all, so this audited `NO EFFECT`.  Up = the bank
  speaks over the noise bed (a gong that *rings*); down = the wash dominates
  (all air, no metal).
- **`MlltStif` on the cymbal family → BEATER HARDNESS.**  The static counterpart
  of `VlMllStf`, which was the only stick control the family had.  Hard stick =
  short bright contact ping and a fast bite; soft mallet = a slow dark swell —
  a gong struck with a stick vs one bloomed in with a felt beater.
- **`MlltStif` below the shipped value on the kick family** was also dead (the
  thump clamps at 0 from below, and Kick2/KickDrum ship it at 0.60/0.70, so the
  whole lower half of the knob did nothing).  Turning it down now stretches the
  boom's onset ramp — a rounded felt-mallet "whoomp".  Measured onset ratio at
  the knob floor: KickDrum 0.90 → **0.60**, 808Sub 0.71 → **0.54**.

**Measured** (`param_audit.cpp`, relative lo-vs-hi swing): 43 knob/family
combinations widened, 2 went from dead to live, 1 narrowed by 5 % (SNARE
`Inharm`, within measurement noise).  Verdict changes: **5 improvements, 0
regressions**.

**One knob was deliberately NOT widened.**  Cymbal `Inharm` (jitter spread) is
already past its useful depth: pushed from 2.0 to 2.4 and 3.0, the detune smears
wide enough that the bank's high modes pile onto the `fLo`/Nyquist clamps and
the measured HF swing across the knob actually *shrank* (`ok` → `weak`).  It
stays at 2.0.

#### `param_audit.cpp` has a second blind spot: the PRNG

The README already warns that the audit's 2 s RMS dilutes short transients ~50×.
It had a second, worse one: `FastNoise::seed` is deliberately free-running (a hit
must not replay the same noise) and neither `Reset()` nor `PartialReset()` touches
it, so every render started from wherever the previous one left the generator.
Noise-dominated presets (Clap, Shaker, the hats) therefore drifted a few percent
between runs and **flipped their own verdicts** — this pass initially reported a
"Partls ok → weak regression" on Clap for a knob that provably does nothing on
that preset (`is_modal_engine` is false for `ENGINE_NOISE`; measured identical
before and after).  The audit now pins the seed per render, and the before/after
comparison is reproducible.

On the kick family, prefer a **band** metric over RMS: the master chain is
loudness-maximised, so a thump reshapes the attack spectrum without raising
level.  Measuring the 100-400 Hz "thump" band against the sub band over the
first 100 ms shows `TubRad` swinging 29 % → **48 %** where plain RMS showed
almost nothing.

### Per-preset knob activity — the "which knobs are live" matrix

Because each knob is wired into a specific engine mechanism, a knob does
**nothing** when the current preset's engine has no matching mechanism — this
is by design, not a bug.  Rather than mark dead knobs with an "X" on the
hardware (which would need every affected knob converted to a text display),
here is the intended-wiring matrix.  Legend: **●** live · **◐** subtle /
context-dependent · **○** inert by design.

> **Maintenance rule — do not hand-write this table.**  The first version of
> this matrix was written from memory and got three cells wrong (it marked
> `VlMllRes`/`VlMllStf` inert on the Timpani/Taiko kernel and `VlMllRes` inert
> on cymbals, when `RefreshKernelMods` and the cymbal `stickLevel` block have
> always read them).  A wrong "inert by design" note is worse than no note: it
> hides a real regression behind an intended one.  **Always re-derive the
> table from `param_audit.cpp`** (which sweeps every knob per family exemplar
> and prints NO EFFECT / weak / ok) and cross-check against the code before
> editing a cell.  Note the audit's own blind spot: its 2-second RMS metric
> dilutes short transients ~50×, so a knob that adds a 40 ms attack can audit
> `weak` while being plainly audible — confirm those with a windowed probe
> before marking anything inert.

| Knob | Kick | Tom¹ | Kernel² | Snare | Bar³ | Plate⁴ | Cymbal⁵ | Noise⁶ | String⁷ |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Poly | ● | ● | ◐ | ● | ● | ● | ◐ | ● | ○ |
| Velocity | ● | ● | ● | ● | ● | ● | ● | ● | ● |
| MlltRes | ● | ◐ | ○ | ● | ● | ● | ● | ◐ | ◐ |
| MlltStif | ● | ◐ | ○ | ● | ● | ◐ | ● | ○ | ◐ |
| VlMllRes | ● | ● | ● | ● | ○ | ◐ | ● | ◐ | ○ |
| VlMllStf | ● | ● | ● | ● | ◐ | ◐ | ● | ◐ | ◐ |
| Partls | ○ | ● | ○ | ○ | ● | ● | ●⁸ | ○ | ● |
| Model | ○ | ● | ○ | ○ | ● | ● | ○ | ○ | ● |
| Dkay | ● | ● | ● | ◐ | ● | ● | ● | ◐ | ● |
| Mterl | ◐ | ● | ● | ○ | ● | ● | ● | ○ | ◐ |
| Tone | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ |
| HitPos | ● | ● | ● | ○ | ● | ● | ● | ○ | ◐ |
| Rel | ● | ● | ● | ● | ● | ● | ● | ● | ● |
| Inharm | ◐ | ● | ● | ○ | ● | ◐ | ● | ○ | ● |
| TubRad | ● | ● | ● | ● | ● | ◐ | ● | ○ | ◐ |
| Resnc | ○ | ○ | ○ | ○ | ○ | ○ | ◐ | ◐ | ○ |

¹ Tom = membrane toms/congas/bongos/djembe/handpan/Taiko2 · ² Kernel =
Timpani/Taiko (dense drum-kernel) · ³ Bar = marimba/vibes/kalimba/… (mallet
bars) · ⁴ Plate = cowbell/triangle/belltree/tick · ⁵ Cymbal = crash/ride/
hats/gong/splash (dense resonator) · ⁶ Noise = clap/shaker/HHatClosed · ⁷
String = Koto/GuitarStr (Karplus-Strong) · ⁸ on the cymbal family **Partls is
the resonator-bank density** (the ex-`Rsntrs` control), not a mode count.  It
is marked live on the strength of **T40**, which measures the bank directly
(32 → 60 resonators across the knob); `param_audit.cpp` reads it `weak`,
because the bank is 1/√N level-normalised so a density change moves texture
without moving RMS — exactly the blind spot the maintenance rule above warns
about.  `Velocity` needs no such caveat: it audits `ok` on all eight family
exemplars.

Notes on the recurring "no effect (expected)" cases:

- **Velocity** and **Poly** are the two **global** performance controls (they
  are skipped by `LoadPreset`, so they survive a preset change).  Velocity is
  live on every engine because it biases the strike itself — see the section
  below.  Poly is the voice cap (1-4); the cymbal family honours the full range
  with its CPU bounded by `kCymCostBudget` instead, while the Timpani/Taiko
  kernel runs 2 kettles and the string engine is mono, so the display shows the
  effective value there, e.g. `4(2)` / `4(1)`.
- **Resnc** is the master low-pass **Q** — see footnote ¹ above; it needs
  `Cutoff` brought down to be heard.
- **Model / Partls** reshape the **shared modal bank**.  The kick (boom osc),
  cymbal (dense resonator) and kernel drums bypass that bank, so `Model` is
  inert there — the same structural reason the body knobs were dead before the
  July-2026 per-family wiring.  `Partls` is the exception: on the cymbal family
  it now carries the resonator density instead (see below).
- **Tone** is a master tilt-EQ: it works on everything but is gentle, and reads
  as "little effect" on sounds with little energy in the tilt band.
- **Mterl / HitPos** are inert on the snare (its voice is the wire buzz, not a
  struck modal body); **TubRad** on the snare is the *body-depth* control
  (July 2026, Band-A centre) so it is live there.
- **`MlltRes` / `MlltStif` on the cymbal family were ◐ and are now ●** — the
  July-2026 depth pass wired them to ring presence and beater hardness (they
  had audited `NO EFFECT`), and `MlltStif` on the kick gained a live lower half.
  Re-derived from `param_audit.cpp` with the PRNG pinned, per the maintenance
  rule above.

#### Careful: `MlltRes`/`MlltStif` vs `VlMllRes`/`VlMllStf`

These are **four different knobs** and the similar names cause real confusion:

| Knob | Meaning |
|------|---------|
| `MlltRes` | mallet **resonance** — static amount (kick: thump amount) |
| `MlltStif` | mallet **stiffness** — static hardness (kick: thump snap/pitch) |
| `VlMllRes` | **velocity→**mallet-resonance — how much *harder hits* add hit prominence |
| `VlMllStf` | **velocity→**mallet-stiffness — how much *harder hits* sharpen the strike |

The `Vl…` pair is the **velocity-sensitivity** of the pair above it, so their
audible effect grows with how hard you strike.

#### `VlMllRes` / `VlMllStf` per family (July 2026)

Previously these two only drove the shared mallet exciter and the noise-envelope
attack, so they were inaudible wherever that exciter is masked (kick boom, tom
body) — while being *fully live* on the snare (wire crack), the Timpani/Taiko
kernel (knock prominence / velocity-sharpness) and the cymbal stick.  They are
now wired on every family that lacked them, all **reference-anchored**:

| Family | `VlMllRes` | `VlMllStf` |
|--------|------------|------------|
| Kick | velocity-weighted **thump prominence** (works standalone — no need to dial `MlltRes` first) | velocity-weighted **beater snap** (adds its own knock, raises thump pitch, shortens its ring) |
| Tom | velocity-weighted **slap prominence** (scales a shipped stick layer, or raises a hand-slap burst on presets without one) | **slap sharpness** (brighter + shorter up, rounder + longer down) |
| Kernel | knock prominence + flattens the velocity curve (already live) | velocity→impulse sharpness (already live) |
| Snare | crack / snap (already live) | buzz tightness / Q (already live) |
| Cymbal | stick "tang" level + attack (already live) | **stick stiffness** — faster bite, shorter ping, brighter strike |

### The Velocity knob, and cymbal density on `Partls` (August 2026)

Ask: *"can Rsntrs move to Partls for cymbals, so we can spare a parameter and
use it for velocity — ghost notes low, big wham high?"*  Yes, and the swap is
free in both directions, because each knob was dead exactly where the other
one lives:

- **`Rsntrs`** (resonator-bank density) was a *cymbal-only* control that did
  nothing on the other 34 presets, yet occupied one of 24 GUI slots.
- **`Partls`** (mode count + ResA/ResB coupling) is inert on the six
  `ENGINE_CYMBAL` presets, because the dense-resonator cymbal bypasses the
  shared modal bank that `Partls` reshapes.

So the density moved onto `Partls` for the cymbal family, and slot 3 became
**`Velocity`**, the dynamics control the unit never had — until now the only
way in was per-step velocity on the sequencer.

#### `Partls` on the cymbal family = bank density

`density % = 25 + 5 × Partls`, i.e. the 8 knob positions span the old
`Rsntrs` range 25-60 % exactly.  Unlike `Rsntrs` this is a **normal per-preset
column**, so each cymbal row now carries its own bank size; the six shipped
rows store `Partls = 3` = the 40 % the old knob defaulted to, and all 40
renders stay **byte-identical**.  Measured bank sizes (`T40`):

| Preset | Partls 0 | Partls 3 (shipped) | Partls 7 |
|---|--:|--:|--:|
| Cymbal | 32 | 40 | 60 |
| Ride / RidBel | 32 | 36 | 52 |
| Gong | 32 | 32 | 48 |
| HHat-O / Splash | 32 | 32 | 40 |

The floor is `kCymMinResonators` (32 — below that a cymbal reads as a chord),
which is why the low end of the knob flattens on the smaller-bank presets.
The display follows the meaning: on a cymbal preset the knob reads `Rs40%`
instead of `AB:32`.

**One trap worth knowing about**: `Partls` positions 5-7 are the *ResA/ResB
edit selector* on the legacy engines, and `LoadPreset` deliberately saves and
restores that selector across a preset change.  If the cymbal density fell
through to it, dialling 60 % density on a gong would leave `Model`/`Dkay`/
`Mterl`/`Inharm` writing to **ResB only** on the next drum loaded — a knob
half-dead for reasons nobody could trace back.  On a cymbal preset the
selector branch is skipped entirely; **T40c** asserts it.

#### `Velocity` — ghost notes ⇄ wham

Bipolar, `-100 … 0 … +100`, matching `VlMllRes`/`VlMllStf`:

```
v' = clamp( v × 2^(2.4·knob) , 0.02 , 1 + 0.30·knob )     knob = -1 … +1
```

`knob = 0` returns the incoming velocity **exactly** (`knob_exp2(0)` is exactly
1.0 — see the gotcha), so the default is a true no-op and every shipped preset
is untouched.  It is applied **once**, at the top of `NoteOn`, before either
strike path reads it, which is why it is live on all nine families in the
matrix above: velocity is this unit's whole dynamics axis — mallet stiffness,
noise attack, wire mix and buzz decay, boom weight, cymbal drive and decay,
kernel impulse sharpness — and biasing it there gives all of that for free.

Measured (250 ms RMS, PRNG pinned, `velocity_probe.cpp`):

| Preset | ghost (−100) | neutral | wham at vel 64 | wham at vel 127 |
|---|--:|--:|--:|--:|
| Kick2 | −4.5 dB | — | +0.8 dB | +0.1 dB |
| AcSnare | −11.7 dB | — | +6.6 dB | +0.7 dB |
| Cymbal | −18.3 dB | — | +11.1 dB | **+4.1 dB** |
| Timpani | −8.6 dB | — | +1.5 dB | +0.1 dB |
| Marimba | −5.7 dB | — | +1.1 dB | +0.1 dB |

**The wham has an honest ceiling, and it is the master stage.**  Above neutral
the knob may push a strike up to ×1.30 past a MIDI 127 (`kVelWhamMax`) — the
downstream shaping terms clamp at their calibrated maxima, so the over-range
reads as energy, not as a new timbre, which is right: a stick cannot get harder
than hard.  That over-range is plainly audible where the preset has headroom
(Cymbal +4.1 dB) and **inaudible on the presets that already pin the limiter at
full velocity** (Kick2, Marimba: −38 dB difference-RMS).  This is the same wall
pass 30 documented — *a limited bus cannot give you level, but balance is
free*.  So the knob's up direction is for **lifting weak strokes to full
force**, which it does everywhere (a velocity-64 stroke reaches the
velocity-127 render on all five exemplars, **T39d**); if you want more than a
full-force hit on a kick or a bar, that is `VlMllRes`/`VlMllStf` (character)
and `Gain` (drive), not this knob.

The ghost direction has no such wall and is strong everywhere: −4.5 to −18.3 dB
with the timbre softening as it goes, because the same velocity axis carries
both.  A floor of 0.02 keeps a ghosted note a *note* — below that the −80 dB
magnitude squelch can retire the voice before it speaks, which reads as a
dropped step rather than a ghost.

Regression tests: **T39** (a-e) for the knob, **T40** (a-c) for the density.
`velocity_probe.cpp` maps the knob across any preset; it reports both plain RMS
and difference-RMS in dB, because a total-RMS metric is fooled by a limiter
holding the level while the character changes (the blind spot that hid three
kick knobs in pass 30).

### Note assignment per instrument — see `NOTE_AUDIT.md`

`note_audit.cpp` + `note_audit.py` render every preset **at its own shipped
Note** through `GateOn()` (the sequencer's path, so the note under test is
always the preset's own) and measure what comes out.  Headlines:

- All **8 engine anchors match** their preset's Note exactly (six cymbal
  `ref_note`s, both kernel `root_note`s), so no preset plays a transposed
  version of the spectrum it was calibrated on.
- **Note is inert on 8 presets** — 808Sub and KickDrum (boom oscillator, no
  modal body), the three snares (the wire bands are absolute Hz), and the three
  NOISE presets.  On the two kicks that means the screen says 65 Hz while you
  hear 45 / 57 Hz; `TubRad` is their real tune control.
- **Five presets are pitched oddly for the instrument** (Bongo at 147 Hz,
  Woodblock at 131 Hz, the snares at 73 Hz = the *General MIDI drum-map number*
  used as a pitch, Triangle at 440 Hz, Claves 4 semitones below the one clave
  reference in `samples/`).  None has been changed — each alters an approved
  sound — see `NOTE_AUDIT.md` §4 for the recommendation per preset.
- `render_presets.cpp` carries its own note list and disagrees with the shipped
  column on exactly one preset: **Bongo, scored at 220 Hz, shipped at 147 Hz**.

### Snare wire rattle
A 3-band parallel resonator (low-body ≈ 2 kHz, mid-crack ≈ 4.5 kHz, high-hiss ≈ 7 kHz) replaces the older single 2-pole resonator. Band weights are velocity-dependent (harder hit → tighter/brighter crack). Body-coupled excitation input (not just white noise) makes the wire respond to shell dynamics.

Snare-family runtime rules (restored/added July 2026 — see `REALISM_REVIEW.md`):
- Wire parameters are restored per-NoteOn **after** `PartialReset()` (which zeroes them); for 18 HW passes the wire path was silently dead on live hits.
- `ENGINE_SNARE` skips the noise-envelope release on NoteOff, so the buzz tail is governed by NzRs (calibrated to the reference samples) rather than Rel — real wires ring freely once the stick leaves the head, and the Rel-rate release choked the buzz to ~26 ms. This began as a workaround for the Drumlogue's same-tick gate_off and is now a voicing choice: see [Same-tick GateOn + GateOff](#same-tick-gateon--gateoff-drumlogue-one-shot-model).
- Velocity scales wire mix and buzz decay (ghost-note physics), anchored so full velocity plays the calibrated table sound.
- Preset 38 `BrshSnr` is a brush-swept snare: slow swish onset, 6.5 Hz enveloped swirl AM, diffuse low-Q wire bands.

#### Snare param-design layer (July 2026)

The exciter/resonator knobs were near-inaudible on the snare family: the
wire buzz (not the ~10 % modal body) is a snare's defining voice, so several
otherwise-dead knobs are wired to the wire in `NoteOn` for `ENGINE_SNARE`.
Every mapping is a **delta from the preset's shipped knob value**, so shipped
presets render bit-identical and only knob *movement* bites:

| Knob | Snare role | Mechanism |
|------|-----------|-----------|
| `Rel` | buzz **tail length** | scales `noise_env.decay_rate` (Rel was dead — snare skips the noise-env release) |
| `MlltRes` | buzz **amount** | scales `snare_wire_mix` (±~4×) |
| `MlltStif` | buzz **brightness** | shifts wire band centre frequencies (±1 oct) |
| `VlMllStf` | buzz **tightness / Q** | shifts wire pole radius (clamped < 0.995) |
| `VlMllRes` | **crack / snap** | scales the onset crack burst via `snare_crack_gain` (was dead — its attack target is overridden) |
| `TubRad` | body **depth** | shifts **Band A only** (the low body band, ~2.8 kHz on AcSnare) — see the asymmetry note below |

**`TubRad` is deliberately asymmetric.**  Turning it *up* is a shell getting
bigger, and there is no limit to how deep that reads, so the down-shift keeps
its full −1.3 octave range.  Turning it *down* is not the mirror image: pushing
Band A far *above* the shipped body pitch stops reading as a shallow piccolo
snare and starts reading as a toy (HW: *"TubRad body-depth sounds a bit toy-ish
at lower values"* — on a preset shipping `TubRad`=20 the original symmetric
curve reached ×2.46 ≈ +1.3 oct at the knob floor).  So the up-shift runs at
**half rate and caps at ×1.15** (≈ +2.4 semitones): audibly tighter, still a
snare.  Δ = 0 takes the deepening branch, where `knob_exp2(0)` is exactly 1.0,
so shipped presets stay byte-identical.

The body-shaping knobs (`Model`, `Partls`, `Inharm`, `Mterl`, `TubRad`,
`Dkay`, `HitPos`) still reshape the snare's modal head tone through the
existing modal-engine mappings; on a snare that body is secondary to the wire
by design, so those knobs are the fine head-tuning and the five above are the
primary character controls. Verified stable across 256 extreme-value knob
combinations (pole-radius clamp holds).

### Metallic low-loss clamp (Phase 53)
At NoteOn, `Cymbal`, `Gong`, `HHat-O`, `Ride`, `RidBel`, and `Trngle` get `loss_g_hf` and `lowpass_coeff` floors raised so the KS loop retains upper partials longer. Transient LP jitter is also limited to prevent over-darkening the attack — a known architecture-coupled failure mode for metallic rods/bars.

### Same-tick GateOn + GateOff (Drumlogue one-shot model)
The Drumlogue fires `gate_on` and `gate_off` in the same scheduler tick, **before any audio block**. So every `release()` can land on an envelope that `trigger()` has just zeroed and `process()` has never advanced — and `ENV_RELEASE` from `value = 0` computes `value += (0 - value) * release_rate`, which is still 0, trips the `value <= 0.001f` cutoff and goes to `ENV_IDLE`. The envelope dies before it opens and the voice is silent.

This is the single most expensive class of bug in this project, because it is **invisible to every host tool**: `render_presets.cpp` and `param_audit.cpp` hold the gate 50 ms / 20 ms and sound perfect, and `stability_sweep.cpp` only checks peaks and NaN, so a voice that silences *itself* passes. It reached hardware three times:

| Site | Found | Original per-site fix |
|------|-------|----------------------|
| `master_env` (all engines) | pass ~12, test T20 | direct `value = 1.0f; state = ENV_DECAY;` in `NoteOn` (not `trigger()` + `process()` — ARM `-ffast-math` may emit an FMA that leaves `value` fractionally under `0.99f`) |
| `ENGINE_SNARE` noise envs | pass 19 | `NoteOff` skips the release — buzz was choked to ~26 ms |
| `ENGINE_NOISE` noise envs | pass 27 (review) | none — Clap/Shaker/HHat-C emitted a ~20 ms blip and nothing else on device |

Since pass 27 the **mechanism** is fixed instead: `FastEnvelope::release()` called during `ENV_ATTACK` defers into `ENV_ATTACK_REL`, the attack completes, and the release then runs from the top of it. An envelope that is already open is untouched (all 40 presets stay byte-identical), and `Rel` keeps shaping the tail — which the "skip the release" approach does not. **New release sites need no workaround of their own.** `samegate_probe.cpp` prints same-tick vs held-gate RMS per 25 ms for any preset; **T37** is the regression test.

### Coupled resonator beating
Setting `Partls > 0` couples ResA and ResB at the same nominal pitch. Two coupled identical oscillators split into two normal modes at `ω ± δ`, producing beats at `2δ`. This is physically correct but perceptually surprising. Use `Partls = 0` (single resonator) for clean sustained tones.

### ARM `-ffast-math` vs x86 IEEE 754
On ARM, `0 × Inf = 0`. On x86, `0 × Inf = NaN`. A diverging SVF or unbounded modal oscillator will contaminate the delay line on x86 but silently flush to zero on hardware. The brickwall limiter then masks NaN as ±0.99 on x86, making the synth appear to "work" while all sustain is gone. Always verify audio quality via `render_presets` (x86 binary), not only unit tests.

---

## Pre-Hardware Tuning Toolchain

### Scoring pipeline
```
render_presets (x86 C++ binary)
      │  renders each preset to a WAV at a fixed note/duration
      ▼
pre_hw_analysis.py
      │  pairwise comparison: f0, attack_time, T60, spectral centroid/rolloff/
      │  flatness/flux, inharmonicity, MRSTFT distance, timbre vector distance
      ▼
batch_tune_runner.py          auto_tune.py
      │  batch scoring +            │  greedy per-preset
      │  acceptance routing         │  parameter search
      ▼                             ▼
batch_reports/              rendered_tune/ (authoritative scores)
```

**`auto_tune.py`** is the primary optimization tool. It re-renders on every trial (always fresh), implements architecture-aware routing (skips out-of-scope and arch-blocked presets by default), and requires a minimum improvement of 0.25 points before accepting a change.

**`batch_tune_runner.py`** reads from `rendered_batch/` which may be stale. Use `--run-render` to refresh, or compare only within `rendered_tune/` after an `auto_tune` run.

### Acceptance routing
Every preset falls into one of three states:
- **`tunable_in_scope`** — active parameter tuning applies; auto-tune will run
- **`architecture_backlog`** — model limit reached; needs DSP change, not parameter change
- **`out_of_scope_trace`** — wind instruments (Clrint, Flute); excluded unless `--include-out-of-scope`

### Score metric
`class_weighted_score` = weighted sum of ~15 pairwise metrics, dominated by:
- `mrstft_log_l1` (weight 8.0) — multi-resolution STFT L1; most reliable perceptual proxy
- `f0_pct` (0.16), `attack_pct` (0.14), `t60_pct` (0.18) — temporal and pitch matching
- PERCUSSIVE bonus: `+0.12 × (flatness_pct + flux_pct)` for percussive preset families

### Known architectural score floors

| Preset | Permanent floor | Cause |
|--------|----------------|-------|
| Cymbal (CrashA) | `inharm_pct = 100%` | ref_inharm = 0, ren_inharm > 0; pct_diff always 100% |
| Cymbal | `f0_pct ≈ 91%` | ref f0 at 4000 Hz, render at note 65 (349 Hz); gap too large |
| Triangle C# | `f0_pct ≈ 90%` | C# sample is C#8 ≈ 4434 Hz; ~40 semitones above render range |
| Triangle C# | `attack_pct ≈ 98%` | Triangle bell has instant metal-strike onset; KS ramp attack cannot match |
| Gong | `attack_pct = 100%` | Both gong samples have much faster onset than the render |
| Gong | `f0_pct ≈ 97%` | Inharmonic spectrum confounds f0 detector; apparent 70-semitone mismatch |

---

## Parameter Reference (preset columns in synth_engine.h)

```
{preset_idx, note, bank, smp, mallet_stif, mallet_res, ..., Dkay, Mterl, ..., NzMx, NzRs, NzFq, vel}
```
Key tunable parameters in `auto_tune.py`:

| Param | Column | Range | Effect |
|-------|--------|-------|--------|
| Dkay | 10 | 0–200 | Feedback gain → T60; 200 = near-lossless |
| Mterl | 11 | −10–30 | Material LP coefficient (0=dull wood, 30=bright metal) |
| NzMx | 19 | 0–100 | Noise mix (caution: Cymbal attack couples to this) |
| NzRs | 20 | 0–1000 | Noise envelope length |
| NzFq | 22 | 0–1999 | Noise filter cutoff |
| MlSt | 14 | 0–500 | Mallet stiffness (attack brightness) |
| InHm | 15 | 0–2000 | Allpass dispersion coefficient |
| TbRd | 17 | 0–20 | Tube radius (raises LP cutoff toward 1.0) |

Model params (in `model_param_presets[]`, tuned via `M.` prefix in auto_tune):
- `NzMixB` (col 9), `NzHi` (col 11), `MdlMx` (col 29), snare params (col 0–8, 12–28)

---

## Dkay → T60 Quick Reference

```
feedback_gain = 0.85 + (Dkay / 200) × 0.149
T60 = 6.908 / (f0_Hz × |ln(feedback_gain)|)
```

| Dkay | g      | T60 @ C4 (261 Hz) |
|------|--------|-------------------|
| 0    | 0.850  | 9 ms (dead thud)  |
| 25   | 0.869  | 189 ms (kick)     |
| 100  | 0.925  | 850 ms (tom)      |
| 150  | 0.962  | 2.2 s (mallet)    |
| 200  | 0.999  | 26 s (string)     |

---

## Reference / Literature / Inspiration Links

### Timbre analysis, similarity, and representation
- Timbre Models of Musical Sounds: From the Model of One Sound to the Model of One Instrument
  https://www.academia.edu/1051621/Timbre_models_of_musical_sounds_from_the_model_of_one_sound_to_the_model_of_one_instrument
- Aucouturier et al. (timbre representation context PDF mirror)
  https://www.francoispachet.fr/wp-content/uploads/2021/01/aucouturier-06a-1.pdf
- ISMIR 2019 paper reference used for descriptor/classification inspiration
  https://archives.ismir.net/ismir2019/paper/000091.pdf
- Brightness perception / spectral centroid relation (reference link)
  https://scispace.com/pdf/brightness-perception-for-musical-instrument-sounds-relation-13u09obfoq.pdf

### Damping / decay modeling
- Three decaying modes with equal and unequal energies and reverberation times
  https://www.researchgate.net/publication/371112063_Three_decaying_modes_with_equal_and_unequal_energies_and_reverberation_times
- Tonazzi et al. postprint (material linked in discussion)
  https://iris.uniroma1.it/retrieve/08f9d8c1-3060-409c-8997-817e882b8e13/Tonazzi_Postprint_Material_2024.pdf
- T20/T30/T60 measurement references shared during tuning discussion
  https://download.spsc.tugraz.at/thesis/PhD_Balint_20201203.pdf
  https://amslaurea.unibo.it/id/eprint/348/1/tesi_formattata.pdf

### Digital instrument modeling / discrete parametrization
- Discrete-time modelling of musical instruments
  https://www.researchgate.net/publication/228667658_Discrete-time_modelling_of_musical_instruments
- Dissertation reference shared for discrete model context
  http://lib.tkk.fi/Dipl/2007/urn009585.pdf
- Sensors article link shared for additional modeling context
  https://www.mdpi.com/1424-8220/25/11/3469
- Musical instrument recognition reference for discrete parametrization guidance
  https://www.nature.com/articles/s41598-025-09493-y

### Oscillator / recursion / filter coefficient references
- Harmonic quadrature oscillator recursion (Vicanek)
  https://vicanek.de/articles/QuadOsc.pdf
- Digital sine oscillator design notes
  https://www.njohnson.co.uk/pdf/drdes/Chap7.pdf
- Biquad and coefficient calculation references
  https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_10.pdf
  https://www.ti.com/lit/an/slaa447/slaa447.pdf
- Minimal sinusoidal oscillator implementation (VCII paper)
  https://www.mdpi.com/2079-9268/11/3/30

### Advanced mathematical modeling (exploratory)
- HAL preprint shared as thought-provoking modeling reference
  https://hal.science/hal-03178044v1

### Cymbal / gong modal modelling references used for Stage-2+ design
- Chaigne, C. & Doutaut, V. — Numerical simulations of xylophones and cymbals (plate modal context)
  https://ensta.hal.science/hal-01135295/file/ACCTOT.pdf
- Chaigne et al. / Touzé related plate-vibration nonlinear modal interaction reference
  https://perso.ensta.fr/~touze/PDF/ISMA04.pdf
- Chalmers publication (plate/cymbal vibro-acoustic modelling reference)
  https://publications.lib.chalmers.se/records/fulltext/5768.pdf
- Vibrating plates mode visualisation/intuition reference
  https://mdphys.org/plates.html
- Cymbal harmonics-informed design method
  https://ord.npust.edu.tw/wp-content/uploads/2023/07/Cymbals-with-Harmonics-Sound-a-Method-for-Design-the-Cymbals-and-Percussion-Instruments-with-Cymbals.pdf
- Acoustical Science and Technology article (cymbal/percussion acoustic analysis context)
  https://www.jstage.jst.go.jp/article/ast/42/6/42_E2087/_pdf/-char/en

### Snare-wire / resonant-noise & filter-complexity references
- Avnell Das thesis (snare/percussion synthesis and implementation context)
  https://www.diva-portal.org/smash/get/diva2:833643/FULLTEXT01.pdf
- University of Sydney review (drum/percussion modelling notes)
  https://ses.library.usyd.edu.au/bitstream/handle/2123/9178/Jarad%20Avnell%20Das%20Final%20Review.pdf?sequence=2&isAllowed=y
- IIR approximately-linear-phase complexity reference (for low-cost resonant shaping discussions)
  https://www.researchgate.net/publication/333784589_A_Complexity_Analysis_of_IIR_Filters_with_an_Approximately_Linear_Phase

### Non-physical / hybrid and broader modeling context
- Frontiers (2025) signal-processing reference shared for non-physical modelling context
  https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1715792/full
- The NESS Project: Physical Modeling Algorithms and Sound Synthesis
  https://www.researchgate.net/publication/337399991_The_NESS_Project_Physical_Modeling_Algorithms_and_Sound_Synthesis
- CCRMA overview of sound modeling
  https://ccrma.stanford.edu/overview/modeling.html
- CCRMA CLM/physical-modeling tutorial notes
  https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/pm.html

### Additional collection / paywalled candidate
- AIP collection: Modeling of Musical Instruments
  https://pubs.aip.org/collection/1314/Modeling-of-Musical-Instruments

## Pre-HW tuning workflow: Step 2 + Step 3

For the deterministic note-lock workflow, use `note_map_priority.json` and run:

```bash
./run_phase23_tuning.sh
```

This executes:
- Step 2: pitch-only validation pass with locked notes over classics + guard presets.
- Step 3A: classics-first iterative tuning pass (`AcSnare,Kick,HHat-C,HHat-O,Timpani,AcTom`).
- Step 3B: guard-set pass (`Flute,Clrint,Tick,Clap,Kalimba`) to check regression risk.

Outputs are written under:
- `batch_reports/phase2_pitch_validation`
- `batch_reports/phase3_classics`
- `batch_reports/phase3_guard`

### Readiness check before Step 2/3 runs

Use the helper below to verify whether the target set has sample coverage:

```bash
./phase23_readiness.py
```

If any target presets are listed as missing, provide curated sample files (or explicit mapping overrides) before running long tuning passes.

---

# Appendix — Modal Drum Engine (improved Timpani/Taiko port)

This appendix documents the design of a standalone modal-synthesis engine whose
Timpani and Taiko reproductions are being ported into Brachetti. It is folded in as
reference for the improved presets and for the parameter-strengthening work. The
guiding principle: **every change was driven by a measurement and verified against
the sample with a reciprocal plot/metric**, not tuned by ear alone.

## Goal & the two instruments
Reproduce the *character* of a struck drumhead — strike, bloom, ringing body, and
how the spectrum decays over time — while staying cheap on the ARMv7-A + NEON DSP.
The two drums are acoustic opposites and stress the engine in opposite ways:

| | Timpani (kettle) | Taiko |
|---|---|---|
| Pitch | Definite, ~131/165 Hz tonal series (2:3:4:5:6 over a missing fundamental) | Deep ~87 Hz, **inharmonic** cluster |
| Decay | Long (rings for seconds) | Short (10% RMS by ~400 ms) |
| Attack | Soft mallet, low thump (HF ~0.3%) | Stick "DOON", low thud (HF ~10%) |
| Crest factor | 4.08 | **6.39** (very punchy) |
| Tail texture | **Tonal** (flatness ~0.11) | **Noise-dominated** (flatness ~0.53) |

The same engine produces both; only the **settings invert**.

## Architecture (standalone engine)
`extract_modes.py` (FFT + per-band peak-pick → modes freq/amp/decay, residual →
broadband attack transient) → composable config-shaping scripts (`densify`,
`hishape`, `tune`, `freqdecay`, `densefill`, `reshape_transient`) → the COUPLED
`ResonatorDrumSynth` engine → render.

**Per-sample signal flow** (maps onto Brachetti's MEMBRANE engine + boom + noise):
```
1. EXCITATION   e = half-sine impulse (length & amplitude scale with velocity)
2. RESONATORS   y0 = a1·y1 − a2·y2 + g·e  per mode;  body = Σ y0   (2-pole)
                a1=2r·cosθ, a2=r², r=exp(−α·dscale/fs), θ=2πf/fs, g=amp·sinθ
3. NOISE WEDGE  white noise → 4 cascaded 1-poles (−24 dB/oct) cutoff SWEEPS
                bright→dark; added to the membrane (the taiko grain)
4. BLOOM        membrane (body+noise) ×= raised-cosine swell (floor→1 over ~ms)
5. TRANSIENT    + sharp broadband "click" (added AFTER the bloom, stays sharp)
6. LIMITER      transparent below 0.85, tanh soft-knee only on true peaks
7. PITCH GLIDE  per-block resonator retune (a small downward "whoomp")
```
Key idea vs Brachetti's 6-mode banks: a **dense membrane fill** (~220 jittered
low-level resonators) gives the timpani a continuous tonal "wedge", and a
**sweeping-cutoff noise wedge** gives the taiko its grainy broadband body — the
two textures the discrete 6-mode bank cannot make.

## Measurement toolbox
| Tool | Measures | Used for |
|---|---|---|
| `wham.py` | crest, atk0-5, atk/tail, HF0-10 | strike punch |
| `atkshape.py` | 0.25 ms-bin attack envelope | bloom shape |
| `wedge2/3.py` | −60/−80 dB top edge per frame | the spectrogram wedge |
| `tailflat.py` | flatness profile over the tail | snare check / texture |
| `density.py` | partials-within-20 dB + AM-depth | tonal density |
| `specenv.py` | dB-vs-freq envelope in a window | high-end roll-off |

## Per-instrument recipes (standalone engine)
- **Timpani** (tonal wedge, long ring, soft low bloom): dense fill + bloom 4 ms from
  silence, noise off. Match: crest 4.02 (samp 4.08), atk0-5 0.466 (0.478), wedge 7.3k→1.5k.
- **Taiko** (noise wedge, short body, big partial bloom from a floor, low thud): noise
  wedge on, fast body (dscale 10), bloom 16 ms floor 0.12. Match: crest 6.46 (samp 6.39),
  atk0-5 0.340 (0.326), HF 0.112 (0.103), late flatness 0.185 (0.189), wedge 11k→2k.

## NEON vectorization (resonator bank)
The hot path is `y0 = a1·y1 − a2·y2 + g·e` per mode. State is structure-of-arrays,
**fixed-size `alignas(16)` (no heap, no std::vector)**, padded to a multiple of 4, so
NEON runs 4 modes/iteration (`vmulq`/`vmlaq`/`vmlsq` + ARMv7 `vadd`/`vpadd` horizontal
sum — not AArch64 `vaddvq`). Verified on cross-compiled ARMv7 under qemu: scalar
refactor bit-identical to the original; NEON vs scalar = 2–4 LSB / 70–78 dB (FP
reordering only, inaudible). Kernel speedup on real hardware (x86 SSE proxy): **2.76×**
(798 → 2202 M mode-updates/s). At 48 kHz, 280 modes ≈ 13.4 M updates/s/voice.

# RipplerX-Waveguide (Drumlogue Bare-Metal DSP)

## Overview
Polyphonic Physical Modeling synthesizer for the Korg Drumlogue. Strictly **Data-Oriented Design**: fixed memory, branchless math, ARM NEON SIMD, respects the ~20 µs RTOS audio deadline. 39 presets spanning strings, bars, membranes, metallic plates, and idiophones.

---

## Signal Flow Architecture (current)

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

### SVF stability limit
The Chamberlin SVF true stability condition is `f < √(4 + q²) − q`, which is strictly less than 2 for all `q > 0`. The naive bound `f < 2` is only safe at zero resonance. `set_coeffs()` in `filter.h` clamps `f` to `0.999 × (√(4+q²) − q)`.

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

### Snare wire rattle
A 3-band parallel resonator (low-body ≈ 2 kHz, mid-crack ≈ 4.5 kHz, high-hiss ≈ 7 kHz) replaces the older single 2-pole resonator. Band weights are velocity-dependent (harder hit → tighter/brighter crack). Body-coupled excitation input (not just white noise) makes the wire respond to shell dynamics.

### Metallic low-loss clamp (Phase 53)
At NoteOn, `Cymbal`, `Gong`, `HHat-O`, `Ride`, `RidBel`, and `Trngle` get `loss_g_hf` and `lowpass_coeff` floors raised so the KS loop retains upper partials longer. Transient LP jitter is also limited to prevent over-darkening the attack — a known architecture-coupled failure mode for metallic rods/bars.

### Same-tick GateOn + GateOff (Drumlogue one-shot model)
The Drumlogue fires `gate_on` and `gate_off` in the same scheduler tick before any audio block. Without a fix, `master_env` sees `ENV_RELEASE, value = 0` on the first `processBlock` call and immediately kills the voice. Fix: call `v.exciter.master_env.process()` once in `NoteOn` to pre-advance the envelope to `value = 1.0` before any `GateOff` can arrive.

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

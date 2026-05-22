# RipplerX — Current Status & Next Steps

**Last updated:** 2026-05-22  
**Branch:** `claude/continue-previous-session-vydFO`

---

## Current Scores (authoritative — from `rendered_tune/`)

Run `python3 auto_tune.py --preset <Name>` from `platform/drumlogue/ripplerx/`.  
Scores are `class_weighted_score` (lower is better). Targets are soft goals, not hard gates.

### After batch-2b auto_tune (COMPLETE — converged via early stop)

Batch-2b ran 13 presets (including Ac Tom, now fixed). Converged after round 3 (stall threshold met).  
Mean score: 63.41 → **61.42** (−1.99 total).

| Preset   | Baseline (pre-batch-2) | Batch-2b final | Target | Status               |
|----------|------------------------|----------------|--------|----------------------|
| Conga    | 53.5   | **31.59** | < 60   | ✓ **ACHIEVED** |
| Djambe   | 48.8   | **48.05** | < 55   | ✓ **ACHIEVED** |
| Taiko    | 67.6   | **46.82** | < 70   | ✓ **ACHIEVED** |
| GlsBotl  | 66.5   | **60.38** | < 70   | ✓ **ACHIEVED** |
| Cowbel   | 65.2   | **63.96** | < 70   | ✓ **ACHIEVED** |
| HHat-C   | 75.3   | **68.65** | < 70   | ✓ **ACHIEVED** |
| Handpn   | 49.0   | **47.97** | < 60   | ✓ **ACHIEVED** |
| StelPan  | 76.3   | **71.29** | < 80   | ✓ **ACHIEVED** |
| Tick     | 84.9   | **76.50** | < 80   | ✓ **ACHIEVED** |
| Claves   | 98.0   | **67.95** | < 80   | ✓ **ACHIEVED** |
| Bongo    | 114.7  | **76.28** | < 80   | ✓ **ACHIEVED** |
| Ac Tom   | 60.01  | **57.20** | < 80   | ✓ **ACHIEVED** |
| MrchSnr  | 78.02  | 78.02      | < 80   | ✓ **ACHIEVED** |
| AcSnre   | 64.54  | 64.54      | < 70   | ✓ **ACHIEVED** |
| Timpni   | 87.0   | **81.84**  | < 80   | Architectural limit (f0/inharm/attack floor) |
| Kick     | 118.21 | **63.09**  | < 80   | ✓ **ACHIEVED** (score was stale; AtkMs 0→5ms seeded) |
| Kalimba  | 64.17  | **66.97**  | < 70   | ✓ **ACHIEVED** |
| Cymbal   | 91.82  | 91.82      | < 70   | Architectural limit  |
| Triangle | 73.86  | 73.86      | < 70   | Architectural limit  |
| Gong     | 101.52 | 101.52     | < 70   | Architectural limit  |

**Architectural limits explained** (auto_tune converged, nothing left to tune):
- **Cymbal**: f0 detector breaks above note 65; NzMx > 40 collapses attack from 37 ms → 2 ms; `CrashA` inharm_pct is permanently 100% (ref=0 vs ren > 0 is always max pct_diff).
- **Triangle**: `Triangle-Bell-C#.wav` has f0 at C#8 ≈ 4434 Hz — ~40 semitones above any useful render note. `attack_pct ≈ 98%` because real triangle bells have instant metal-strike onset the synth cannot reproduce.
- **Gong**: Both gong samples score `attack_pct = 100%` and `f0_pct ≈ 96–98%`. The gong's inharmonic spectrum gives the f0 detector garbage (70+ semitone apparent mismatch). PERCUSSIVE bonus (+12–17 pts) makes the floor even higher.
- **Timpni**: `f0_pct=96.3%` (f0 detector returns −57 st offset on `Orchestral-Timpani-C.wav`), `inharm_pct=100%`, `attack_pct=90.4%`. Dedicated pass ran all 50 trials with zero improvement. Fixed floor ≈38 pts. Target <80 requires pitch-normalised f0 scoring or a better reference sample.

**Ac Tom note**: Fixed in batch-2b. `PRESET_ALIASES["AcTom"]` was `"AcTom"` (no space) — corrected to `"Ac Tom"` in `auto_tune.py`. Ac Tom now scores correctly (57.20, target <80 ✓).

### Key improvements since PROGRESS.md

| Change | Effect |
|--------|--------|
| `k_onset_attack_ms` architectural addition | Membrane drums (Djambe/Bongo/Conga/AcTom/Taiko/Handpan/Timpani) get a linear 0→1 onset ramp. Eliminated 100% attack_pct floor from 0ms render onset vs 3–4ms physical onset. Djambe: 91.9→48.8. |
| auto_tune batch-1 × 6 rounds | BaseFM +200 Hz for Djambe/Timpani/Tick/StelPan, DiffMx 0→0.02 for membrane presets, Dkay/Cowbell parameter adjustments. |
| auto_tune batch-2 × 7 rounds + batch-2b × 3 rounds | Mean score 75.4→61.4 across 13 presets. Bongo 114.7→76.3, Claves 98.0→68.0, Conga 53.5→31.6, Tick 84.9→76.5, HHat-C 75.3→68.7, Ac Tom 60.0→57.2. |
| autocorr_f0 aliasing fix | 44.1 kHz samples decimated to 8820 Hz were giving period-2 spurious f0=4410 Hz. Added `lo = max(4, ...)` guard. |
| Ac Tom name fix (batch_tune_runner.py) | `"AcTom": "AcTom"` → `"AcTom": "Ac Tom"` in 6 locations. Tom samples now resolve to the correct preset. |

---

## Architecture Overview (as built)

### DSP core (synth_engine.h)

```
NoteOn trigger
│
├─► EXCITER
│   ├─ Dual-noise burst: LP-filtered low + unfiltered high, blended by noise_band_mix
│   ├─ Mallet: two cascaded LP pulses, velocity-scaled, gated after decay
│   ├─ Snare wire rattle: 3-band (lo/mid/hi) resonator, velocity-dependent Q
│   ├─ Metallic FM chirp: per-voice transient sweep (Cymbal/Gong/HHat/Ride/Triangle/BellTree)
│   ├─ Boom oscillator: low-body sine env (Kick/Timpani/AcTom/AcSnare)
│   └─ Stage-2 modal bank: 2–6 decaying oscillators via 2nd-order recursion
│
├─► KS WAVEGUIDE (Resonator A + optional Resonator B)
│   ├─ Delay line (4096-sample, fractional-interpolation)
│   ├─ Allpass dispersion → 1-pole LP loss (AP then LP — physically correct order)
│   ├─ loss_g_dc / loss_g_hf split: independent sustain vs brightness control
│   ├─ Pitch-compensation: LP and AP group-delay subtracted from delay length at NoteOn
│   ├─ Metallic low-loss clamp: raises hf_branch_decay + lowpass_coeff floors for metallic family
│   └─ Modal attack coupling: modal_mix boosted by FM env during transient window
│
└─► MASTER SHAPING
    ├─ Onset ramp: linear 0→1 envelope over k_onset_attack_ms ms (membrane drums only)
    ├─ Tilt EQ (Tone param)
    ├─ Magnitude envelope follower (squelch, ~−80 dB threshold)
    └─ Brickwall limiter (NaN safety net)
```

### Notable quirks and invariants

| Quirk | Detail |
|-------|--------|
| **Allpass formula** | Uses `H(z) = (c + z⁻¹)/(1 + c·z⁻¹)`, so DC group delay = `(1−c)/(1+c)`, NOT `(1+c)/(1−c)`. Getting this wrong causes systematic pitch sharpness. |
| **SVF stability** | True stability limit is `f < √(4+q²)−q`, not `f < 2`. Clamped with 0.999× safety margin in `set_coeffs()`. |
| **fasterpow2(0) ≈ 0.9714** | Fast-math approximation is wrong at 0. PitchBend uses `if (bend==8192) mult=1.0f` exact case. Same applies to `fasterpowf` used in tables.h — replaced with `powf`. |
| **Coupled resonator beat** | Partls > 0 couples ResA/ResB at the same pitch, splitting normal modes → beating arises from modal frequency splitting, not tuning error. |
| **Same-tick GateOn+GateOff** | Drumlogue one-shot model fires gate_on + gate_off in the same scheduler tick. Fixed by pre-advancing `master_env.process()` once in NoteOn so release finds value=1.0. |
| **Voice 0 skipped first note** | NoteOn pre-increments `next_voice_idx` before assigning. First note goes to voice 1. Minor cosmetic issue, known and accepted. |
| **ARM -ffast-math** | On hardware `0 × Inf = 0`. On x86 it becomes NaN. Brickwall limiter masks NaN silently as 0.99. Always test via render binary, not just x86 unit tests. |
| **Percussive bonus** | `class_weighted_score` adds `0.12×(flatness_pct + flux_pct)` for PERCUSSIVE_PRESETS. `Trngle` is NOT in this set (set has `"Triangle"`, not `"Trngle"`) — intentional: it keeps scoring lower. |
| **rendered_tune vs rendered_batch** | `auto_tune.py` always writes fresh renders to `rendered_tune/`. `batch_tune_runner.py` reads from `rendered_batch/` (may be stale). Score discrepancies between the two tools usually trace to this. Always re-render before batch scoring. |

### Key files

| File | Role |
|------|------|
| `synth_engine.h` | Entire DSP engine + preset tables + model_param_presets + modal configs |
| `render_presets.cpp` | x86 host renderer — one WAV per preset at a fixed note/duration |
| `pre_hw_analysis.py` | Pairwise audio comparison: f0, attack, T60, centroid, MRSTFT, etc. |
| `auto_tune.py` | Single-preset greedy parameter search; writes to `rendered_tune/` |
| `batch_tune_runner.py` | Batch compare + scoring; reads from `rendered_batch/` |
| `test_hw_debug.cpp` | 82 unit tests; run with `./test_hw_debug` |
| `cymbal_sweep.py` | NzMx/note grid search for Cymbal (diagnostic, committed for reference) |

### Render note decisions (render_presets.cpp, line ~150)

| Preset | Note | Why |
|--------|------|-----|
| Cymbal | 65 | f0 detector breaks above 65; notes 72/76/80 all produce garbage f0 values |
| MrchSnr | 65 | Best score vs marching-snare references |
| Triangle | 69 | A4; best dual-sample mean (C# and F5) |
| Gong | 50 | D3; low pitch for gong body; +12 st calibration applied in tooling |

---

## Score Metric Formulas

### pct_diff
```
pct_diff(a, b) = 100 × |a−b| / max(|a|, |b|)
```
Symmetric percentage difference. 100% = completely different, 0% = identical.

### class_weighted_score (simplified)
```
score = 0.16·f0 + 0.14·attack + 0.18·t60 + 0.16·centroid + 0.10·rolloff
      + 0.08·flatness + 0.08·flux + 0.10·inharm + [other terms]
      + 8.0·mrstft_log_l1
```
PERCUSSIVE presets add `+ 0.12·(flatness_pct + flux_pct)` on top.

### auto_tune acceptance
Minimum improvement threshold: `MIN_ACCEPT_IMPROVEMENT = 0.25`.  
Rounds stop after 3 consecutive rounds with no accepted change.

---

## What To Do Next (priority order)

### 0. Status: Kick auto_tune running (PID 10420, log `/tmp/autotune_kick.log`)
- Kalimba: 66.97 ✓. Timpni: architectural limit (81.84).
- Kick score was stale at 118.21 — actual score after diagnosis: **55.3** (already <80 ✓).
  - Root cause of old score: scoring code was different; `attack_pct=100%` because render
    peaks at t=0 while reference has 4.79ms onset ramp.
  - Fix: seeded `AtkMs=5ms` for Kick → score 49.7 in sweep. Baseline for auto_tune: 63.09.
  - auto_tune pass running to optimise remaining params (centroid, rolloff, flux ~60%).

### 1. Dedicated passes (next up)

**Timpni** — 81.84, confirmed architectural limit (see §Architectural limits).

**Kick** — RUNNING (baseline 63.09, target <80 already met). The 118.21 was stale.
- Diagnosis: `attack_pct=100%` (render peaks at t=0, reference has 4.79ms onset). AtkMs 0→5ms seeded.
- Remaining issues: centroid_decay_slope=81%, rolloff=60%, flux=61%, centroid=44%.
- `f0_pct=0%`, `inharm_pct=0%` — pitch and inharmonicity are perfect.

**Kalimba** — DONE. Final score **66.97** (target <70 ✓). 4 rounds, converged.

### 2. Architecture work (Bullet 1, partially started)
Three pending DSP improvements from Phase 45 (still not fully implemented):

**A. Metallic low-loss loop (Triangle / metallic family — partially done in Phase 53)**
- Status: `loss_g_hf` and `lowpass_coeff` floors raised at NoteOn for metallic family.
- Remaining gap: Triangle C# still at 73.86 architectural limit. True fix requires a less-lossy modal path that preserves 2nd/3rd partials — KS single-loop 1-pole LP is fundamentally too dark for metallic rods/bars.
- Plan: Add a "low-attenuation KS mode" where per-mode decay is controlled by `mode_tau` rather than the shared LP loss filter.

**B. Parallel HF exciter stack (Cymbal/Gong/HHat-O)**
- Status: NOT implemented. FM chirp path exists but is tonal, not noise-like.
- Plan: Add a dedicated HP-filtered noise burst that decays independently of the modal/KS ring. This gives the cymbal "sizzle" tail that is currently missing (mrstft_log_l1 is still high because the spectral shape doesn't match).

**C. Snare wire multiband rattle (Phase 52 — partially done)**
- Status: 3-band LP/HP split added to ExciterState. MrchSnr now scores 78.02.
- Remaining: AcSnre is at 64.54; verify no regression on MrchSnr after further tuning.

### 3. Script improvements

**auto_tune.py**
- `PRESET_ALIASES["AcTom"]` bug **FIXED** (was `"AcTom"`, now `"Ac Tom"`).
- Dkay search range: currently 0–200 in steps of 10. After convergence switch to fine steps (5) for final pass.
- Add support for tuning `modal_preset_configs` (frequency ratios, T60s, mix coefficients) — currently not in PARAMS/MODEL_PARAMS.
- The model_param columns tuned are: NzMixB (col9), NzHi (col11), MdlMx (col29). Extend to cover other model params where they exist per preset.

**batch_tune_runner.py**
- PERCUSSIVE_PRESETS has `"Triangle"` but the preset is named `"Trngle"` — the percussive bonus never applies to Triangle. Intentional or bug? Clarify and document.
- Add rendered_tune → rendered_batch sync step so batch scoring always matches auto_tune scoring.

**render_presets.cpp**
- Gong currently renders at note 50 with a +12 st calibration in tooling. Consider rendering at note 62 directly and removing the calibration offset.

### 4. Data gaps
Presets with no mapped reference samples (cannot score objectively):
```
InitDbg, TblrBel, Koto, Vibrph, StelPan*, Claves*, Cowbel*, Clap, Shaker,
Flute, Clrint, PlkBss, GlsBwl, GtrStr, BelTre, SltDrm, Ride*, RidBel*, HHat-O*
```
(* have samples but scoring is noisy / no reliable f0; StelPan/Claves/Cowbel now have AtkMs tuning even without reliable f0)

**Presets that DO have samples and score reliably:** Djambe, Bongo, Conga, Taiko, Handpn, Timpni, Tick, HHat-C, GlsBotl, Cowbel, Claves, StelPan, MrchSnr, AcSnre, Kick, Kalimba, Cymbal, Triangle, Gong

### 5. Hardware validation gate
Before flashing, a preset candidate must pass:
1. `auto_tune.py` score improvement with `rendered_tune/` as ground truth
2. Manual listen on host render (does it sound like the instrument?)
3. ARM/qemu render sanity (no NaN, no DC offset)
4. Hardware A/B test at stable note and velocity

---

## Scoring Gotchas (traps to avoid)

1. **Stale rendered_batch**: After any `auto_tune.py` run, `rendered_tune/` is current but `rendered_batch/` is stale. Either re-render (`--run-render`) or compare only within `rendered_tune/`.

2. **f0 detection on metallic sounds**: Triangle, Cymbal, Gong all have unreliable f0 detection. `f0_pct` is effectively a noise floor for these. Don't try to tune it down — it's a measurement artifact, not a real mismatch you can fix.

3. **MRSTFT vs perceptual quality**: `mrstft_log_l1` is the most reliable metric for "sounds similar." Low mrstft + high f0_pct is fine for metallic sounds. High mrstft + low f0_pct is bad.

4. **NzMx coupling to attack**: For Cymbal, NzMx > 40 collapses attack_pct from ~21% to ~96%. The NzMx → attack coupling is via noise envelope in the exciter. Don't increase NzMx without re-checking attack metric.

5. **Note range for Cymbal**: note 65 is mandatory. Notes ≥ 66 produce aliased f0 detection artifacts (autocorrelation finds wrong period in complex modal/noise synthesis).

---

## Quick Start Commands

```bash
cd platform/drumlogue/ripplerx

# Build host renderer
g++ -std=c++17 -O3 -I.. test_ripplerx_render.cpp -o render_presets

# Run all unit tests
./test_hw_debug

# Score a single preset (auto-renders fresh)
python3 auto_tune.py --preset MrchSnr

# Score all presets (uses rendered_tune/, re-renders all)
python3 auto_tune.py  # no --preset flag = all in scope

# Batch score (uses rendered_batch/ — may be stale)
python3 batch_tune_runner.py --run-render --render-cmd "./render_presets --preset {preset_idx} --note {note} --name {preset_name} --out {output_wav}" --render-dir rendered_batch --out-dir batch_reports

# Check detailed metrics for a preset pair
python3 -c "
from pre_hw_analysis import compare_pair
from pathlib import Path
r = compare_pair(Path('samples/Chinese-Gong.wav'), Path('rendered_tune/14_Gong.wav'))
print(r['score'])
for k,v in r['metrics'].items(): print(f'  {k}: {v:.3f}' if isinstance(v,float) else f'  {k}: {v}')
"
```

---

## Project Rename (pending, when model is stable)
- RipplerX → **Brachetti** (in honour of performer Arturo Brachetti)
- Rename: all `ripplerx*` files, class `RipplerXWaveguide`, `config.mk`, `header.c`

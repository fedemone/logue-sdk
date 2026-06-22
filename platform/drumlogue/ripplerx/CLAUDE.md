# RipplerX – Session Brief

## Dev Branch

`claude/eager-galileo-2fho84` on `fedemone/logue-sdk`.

Always rebuild and check `arm-unknown-linux-gnueabihf-size ripplerx.elf`:
- `.text` (= text + .rodata) must stay below **28 KB** (safe margin below 30 KB limit).
- `.bss` must stay near **552 bytes**.

## Current Working State (commit 081e82e)

- Unit **loads on hardware**, all 37 presets render clean (0 NaN/silent).
- DSP unit tests: **82/82 PASS** (exit 0).
- Host syntax-check (g++ -fsyntax-only): **clean**.
- HHat-O **HW-approved** ("ok now" — do not break).
- ARM .text ≤ 28 KB: **must be confirmed on next flash** (cannot verify without toolchain).

### Analysis tool: `modal_extract.py`

Newly added (commit 081e82e). Implements the analysis half of DAFx2020 "Advanced
Fourier Decomposition for Realistic Drum Synthesis" (Werner et al.) — high-res
spectral peak track + per-mode STFT T60 fit on any reference WAV:

```
python3 modal_extract.py samples/Orchestral-Timpani-C.wav
python3 modal_extract.py samples/Taiko-Hit.wav --nmodes 10 --fmax 4000
```

Output (ratio, freq, amp, T60ms) maps **directly** onto `modal_preset_configs[]`
fields (ratio2..6, t60_1..4, env1..6). Use this whenever a membrane/bar preset
needs its modes calibrated — measure first, guess last.

---

## HW Pass History (most recent first)

### Pass 17 — Data-driven modal tuning from reference samples (commit 081e82e)

Applied `modal_extract.py` to fix two standing HW complaints:

**Timpani (HW: "explosion + rough ripple, not clean bright ringing")**
- **Measured** `Orchestral-Timpani-C.wav` → series `1 : 1.495 : 1.980 : 2.601 : 3.414 : 4.010`
  (air-loaded Rossing family). Measured upper-mode amps: 0.14/0.08/0.05 (quiet).
- **Root cause found by measurement**: old top pair (3.02/3.55) sat only ~0.5 ratio apart
  AND was held loud (env 0.45/0.32) → beat against mode 4 = the "rough ripple".
- **Fix**: ratios snapped to measured (3.41/4.01 — wider, quieter cluster); upper modes
  tamed to near-measured levels (env4-6 = 0.30/0.18/0.12); bright pitched modes 2/3
  extended to 1.8/1.5 s for a clean sustained ring.
- **Result**: T60 2.19 s (ref 2.37), early centroid 668 Hz (ref 811), late centroid 295 Hz
  (was ~140 Hz dark hum).

**Taiko (HW: "still TUNNN not TAAAN")**
- **Measured** `Taiko-Hit.wav` → inharmonic series `1 : 1.377 : 1.746 : 2.100 : 2.423 : 2.754`
  (open wooden-stave shell — NOT Bessel) PLUS a strong bright partial at **ratio 16.86 ≈ 1472 Hz**
  (the open "AAN" vowel; matches enum comment "~1582 Hz").
- **Root cause found by measurement**: the bright 1472 Hz partial was **not being synthesized**
  at all (old config stopped at ratio 2.756, 4 modes) and low fundamental dominated.
- **Fix**: 6 modes on measured inharmonic ratios; dominance shifted off 87 Hz fundamental onto
  212 Hz mid (env4=1.00); mode 6 = 1472 Hz partial (env 0.60), sustained ~600 ms;
  attack click brightened (NzMx 26→36, NzFq 280→360 ≈ 3.6 kHz).
- **Result**: early centroid 421→539 Hz (ref 1856 Hz — remaining gap is close-mic stick
  transient in the reference; pushing noise click much harder risks hiss over the wood "tak").

**NOT adopted from the paper**: offline RDFT/ESPRIT pipeline and the alternate NEON
exp-decay phasor engine (full rewrite, .text-budget risk, no benefit over existing modal ringers).

---

### Pass 16 — Nonlinear modal→wash energy cascade (commit a296a0d)

**Problem (recurring, across 4 passes)**: "noise and ring not modulating each other — just
mixed together separately" (Cymbal, Gong, Ride, RidBel).

**Root cause (structural)**: crash resonator bank was driven by an **independent noise
generator**; could only ever sound juxtaposed regardless of level tuning.

**Fix — von Kármán nonlinear cascade** (Chaigne/Touzé plate theory, cited in README):
In a real cymbal the broadband crash is high-mode energy pumped FROM the low struck modes
by geometric nonlinearity. New excitation formula in processBlock:

```cpp
const float m  = voice.modal_out_prev;
const float am = fabsf(m);
const float floor_n = 0.30f;
float exc = voice.exciter.noise_out_sample * (floor_n + (1.0f - floor_n) * am)
          + (m * am) * voice.crash_couple;
```

- First term: noise blooms and dies **with the ring** (not independently).
- Second term: ring's own energy injected into the crash wash (geometric nonlinearity).
- `crash_couple` (new VoiceState field, default 0.0) set per-preset in NoteOn.

**Per-preset coupling** (split by family):
| Preset     | crash_couple | Rationale |
|------------|-------------|-----------|
| Cymbal     | 0.30        | Light — stays bright/airy, just breathes with ring |
| HHat-O     | 0.22        | Light — approved, do not increase |
| Ride       | 0.35        | Light + Ride/RidBel crackle bed also gated by ring |
| RideBell   | 0.50        | Stronger for bell-modal blend |
| Gong       | 0.60        | Strong — metallic shimmer pulling energy into partials |

**Bed-gate** (Ride/RidBel only): raw crackle noise gated by `floor_n + (1-floor_n)*am`
so the crackle bed also blooms with the bell rather than rattling independently.

**Measured band-envelope correlations** (after fix, vs references):
Cymbal +0.99, Gong +0.73, Ride +0.84, RidBel +0.64, HHat-O +0.93
(Reference crash: +0.76, high-band peak ~470 ms after strike).

**Other changes this pass**:
- **Timpani**: removed membrane-noise bed added in pass 15 (user: "rough ripple over");
  instead brightened upper modes for clean bright ringing; attack 2→7 ms softer.
- **Taiko**: earlier version of mode-lifting brightness fix (superseded by pass 17).
- **Gong**: upper modes lifted/extended modestly for metallic shimmer without clangy attack.

---

### Pass 15 — Timpani option C: de-synthesize modal body

Mode rebalancing + membrane noise bed. The noise bed was subsequently reported as
"rough ripple" on HW → **removed in pass 16**. (Documented for history only.)

---

### Pass 14 — DATA-DRIVEN from reference samples (refcmp.py)

**KEY FINDING that reverses passes 10-13**: a real cymbal/ride/hat is **BRIGHT (~11 kHz
centroid) and NOISY (spectral flatness ~0.55), sustained 0.6-3 s** — NOT a dark tonal
wash. "Crash too predominant" = crash was DARK + FLUTTERING, not that noise shouldn't
dominate. A crash IS mostly bright smooth noise.

- `modal_engine_gain` crash factor 0.95→**0.12** (ring = faint metallic undertone).
- Noise high-passed bright: NzFltr=HP, NzFq→12-13 kHz; Cymbal BP moved 4.5→11 kHz.
- Crash bank cut to light broad colouring (drive ~0.3–0.9).
- Noise releases = **measured reference T60s** (Cymbal 2.3s, Ride 2.9s, RidBel 3.3s,
  HHat-O 0.6s) with BOTH bands slow so sizzle lasts the whole tail.
- Result: Ride 10839/11032 vs ref 11167/11075 ✓; RidBel ~11k ✓ 3.9s; HHat-O 11218 ✓.
- **`refcmp.py`** (host tool): compares a render vs reference (centroid early/late,
  flatness, T60). Run after copying renders to `/tmp/rc/` (`cp rendered/*.wav /tmp/rc/`).

---

### Pass 13 — Timpani/Taiko attack-vs-sustain; metallic ring-dominant rebalance

- Timpani/Taiko "bass guitar + audible vibration": mode 1 dominant + LONG (1.3s/1.5s);
  upper modes fast-decay (300/210/150ms Timp, 260/175/120 Taiko) — colour only attack.
  Centroid early 406Hz → late 140Hz (Timp) = bright attack, dark sustain.
- Metallic crash rebalance: `modal_engine_gain` crash factor 0.60→0.95; crash_base cut
  2-3×; crash_r broadened to ~0.965–0.985 (overlapping resonators = continuous sizzle).

---

### Pass 12 — Voice-stacking polyphony fix (cymbal rolls)

- **Bug**: `GateOff()` forced `next_voice_idx = NUM_VOICES-1`; since Drumlogue fires
  gate_on+gate_off in the same tick, every repeated hit reused voice 0 → no stacking.
- **Fix**: GateOff resets only for short/percussive engines (MEMBRANE/SNARE/NOISE) and
  KS. PLATE and BAR keep round-robin → fast hits stack/overlap across 4 voices.

---

### Pass 11 — De-regress Timpani; crash decay; full param coverage

- Timpani "bass guitar" regression: ratios slightly stretched (1.5/2.03/2.49/3.02/3.55,
  not exact 0.5 steps) + T60 cut ~850 ms → percussive.
- Cymbal "continues while held": modal T60 3000→1800 ms, cymbal noise decay ~6→1.2 s.
- Gong "still an explosion": crash_base 4→1.5, bloom 0.3→0.2.
- HHat-O / Ride "shaking ~28 Hz": `parallel_noise_gain` raised, crash_r broadened →
  continuous broadband-noise-dominant instead of sparse beating resonators.
- EVERY-KNOB-DOES-SOMETHING wiring via `param_audit.cpp`: Rel→ring-length,
  MlltRes→modal presence (crash plates: crash intensity), Partls→mode count + richness.

---

### Pass 10 — Crash rebalance; Timpani harmonic modes; Shaker swell

- Crash recipe was complete but over-pushed: crash_base ~halved; ring raised
  (`modal_engine_gain` 0.45→0.60); crash_ring_tap raised (Ride 0.15→0.40 etc.).
- Timpani: ratios 1:1.5:2:2.5:3:3.5 (dropped 1.742 mode that caused critical-band beating).
- Shaker: noise-env attack slowed to ~15 ms (was instant hit); 17 Hz rattle retained.

---

### Pass 9 — Self-PM "dynamic bloom" (cymbal density)

- Added self-Phase-Modulation: metallic bus written to KS `resA.buffer` (reused — dead
  on plate engines), read back at amplitude-modulated offset → self-FM.
- Cymbal spectral density 1138→2337 bins; HHat-O 497→4722.
- Ride/RidBel crash_r lowered ~0.9965 (broad dense wash vs razor-Q sparse tones).
- `noise_am_decay = 1.0` for Shaker: 17 Hz rattle persists the full tail. Rel 18→19.

---

### Pass 7 — Crash-resonator bank

- Added 6 constant-peak-gain 2-pole bandpass resonators per voice (PLATE only),
  tuned to the same mode frequencies as the struck modal bank (reuses `modal_k_*`),
  driven by enveloped noise: `y[n] = r·k·y1 − r²·y2 + (1−r²)·noise`.
- Two prerequisites: (1) `modal_engine_gain ×0.45` on crash presets so wash competes;
  (2) noise release overridden slow (~2.4s T60) so wash isn't cut by near-instant gate-off.
- MlltRes → crash intensity on ENGINE_PLATE (REFERENCE-ANCHORED).

---

### Pass 6 — Modal tuning precision fix

- `fastercosfullf` has ~1e-3 absolute error; near w→0 Timpani's 82/124/144/165 Hz
  landed at 86/121/139/157 Hz — compressed, ~17 Hz gaps → slow beating.
- Fix: exact `cosf`/`exp2f` in `init_modal_modes` (NoteOn-time, accuracy > speed).
- Ring-mod gate reshaped: `(1−d) + d·modal` (true bipolar mix).
- Taiko velocity split: hard → boom (×0.25..×1.75 by vel²), soft → bright mid modal mode.

---

### Passes 1-5 (foundations)

- Engine routing scaffold (KS bypass for non-string presets).
- ENGINE_BAR — Marimba exemplar (Phase 2 kill-switch).
- ENGINE_MEMBRANE — Kick, Timpani, Taiko, etc.
- ENGINE_SNARE — AcSnare, MarchSnare.
- ENGINE_NOISE — Clap, Shaker.
- ENGINE_PLATE — Cymbal, Gong, Hi-hat, Ride, etc.
- Preset list: 37 entries (Flute/Clarinet removed; Taiko2 = "DeepBs"; Tick added).
- Master filter → LOWPASS "Cutoff" (old "LowCut" HP read reversed on HW three times).
- TPT (Zavalishin) SVF: fixed Chamberlin instability that froze cutoff above ~8.2 kHz.

---

## Host Build / Test Commands

```bash
# Syntax check (host — ARM cross-compiler required for actual flash build)
g++ -std=c++14 -fsyntax-only -I. -I../common -U__ARM_NEON__ -U__ARM_NEON \
    -Wno-strict-aliasing -Wno-unused-parameter unit.cc

# DSP unit tests
g++ -std=c++17 -O2 -I.. -I. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    test_dsp.cpp -o /tmp/run_test && /tmp/run_test

# Render all 37 presets to WAV
g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \
    -DRUNTIME_COMMON_H_ render_presets.cpp -o /tmp/render_presets
/tmp/render_presets rendered/

# Sanity check renders (NaN / silent)
python3 -c "
import numpy as np, scipy.io.wavfile as wav, glob
bad=0
for f in sorted(glob.glob('rendered/*.wav')):
    sr,x=wav.read(f); x=x.astype(np.float64)
    if np.any(~np.isfinite(x)) or np.max(np.abs(x))<1e-7:
        print('BAD:',f); bad+=1
print(f'{bad} problems / {len(glob.glob(\"rendered/*.wav\"))} presets')
"

# Modal parameter extraction from a reference sample (DAFx2020 method)
python3 modal_extract.py samples/Orchestral-Timpani-C.wav
python3 modal_extract.py samples/Taiko-Hit.wav --nmodes 10

# Compare render vs reference (centroid, flatness, T60)
# Requires copying renders first: cp rendered/*.wav /tmp/rc/
python3 refcmp.py
```

---

## Critical .rodata / .data Constraint — Do NOT break this

The drumlogue firmware checks `.text segment` size (= `.text + .rodata + .init + .fini`)
per unit. Limit ≈ 30 KB. The preset tables (~7 KB) **must stay in `.data`**.

**Working fix (a49e2f4):** The large preset arrays —
`kDefaultModalPresetConfig`, `modal_preset_configs[]`, `model_param_presets[][]`,
`kPresetEngine[]` — are declared as **non-static** class members (no `static`,
no `const`, no `constexpr`). This places their initial values in `.data`.

**Broken patterns to avoid:**
- `static constexpr T arr[] = {...}` → goes to `.rodata` → text-size check fails
- `static const T arr[] = {...}` → same problem
- `static T arr[] = {...}` **inside a class body** → GCC 6.5 rejects it

See `config.mk` `USE_LTO := no`.

---

## Known Architectural Floors (not worth chasing)

| Issue | Cause | Decision |
|-------|-------|----------|
| Cymbal spectral flatness ~0.03-0.23 vs ref ~0.55 | 6-resonator bank is fundamentally tonal | Accepted; optimized for blending instead |
| Taiko early centroid ~539 Hz vs ref ~1856 Hz | Ref has prominent close-mic stick transient | Light coupling preferred over turning "tak" into hiss |
| Ride 34 Hz correlated AM | Sparse 6-resonator bank beating | Addressed via broadband noise dominance |

---

## Key Architectural Bugs (fixed — keep as gotcha reference)

### GOTCHA: modal mix lives in TWO tables

`ModalPresetConfig.mix` is used only by `LoadPreset`.  `NoteOn` re-inits the modal bank
using `model_param_presets[preset][k_modal_mix]`, which **overrides** it.  Keep both in sync.
Always edit the `model_param_presets` `k_modal_mix` column to change the audible mix.

### GOTCHA: Plate Ratios vs. Membrane Ratios

HiHatClosed, HiHatOpen, Cowbell = ENGINE_PLATE.  If `modal_preset_configs` uses membrane
Bessel ratios (1.000/1.594/2.136/2.296) → sounds like a wood drum.  Use plate ratios
(2.92/6.37/11.75) for PLATE presets.

### GOTCHA: Modes 5/6 inherit T60_4

`init_modal_modes` computes:
```
modal_decay_5 = powf(modal_decay_4, 1/0.85)   // T60_5 = 0.85 × T60_4
modal_decay_6 = powf(modal_decay_4, 1/0.70)   // T60_6 = 0.70 × T60_4
```
There is no independent t60_5/t60_6 field — only env5/env6 are free. Modes 5/6 always
decay faster than mode 4.

### GOTCHA: KS pitch_env T60 gotcha

When pitch_env_amt>0, KS delay starts short → injects zeros into feedback path → shortens
T60. Fix: use τ≤21ms (pitch_env_decay≥0.9990) so sweep completes in attack transient.
**NEVER use τ>50ms for KS pitch_env.**

### fasterexpf catastrophic inaccuracy

`fasterexpf` catastrophically wrong for |x| < ~0.001.  `modal_decay` uses arg ~-0.00012
at T60=1.2s → `fasterexpf` returns ~0.971 (implying T60≈5ms) instead of 0.99988.
**Always use standard `expf`/`powf` in `init_modal_modes`** (NoteOn-time, once per hit).

### fastercosfullf frequency error

~1e-3 absolute error near w→0 shifts low-frequency mode ratios (Timpani ~17 Hz gaps →
slow beating). **Always use exact `cosf` in `init_modal_modes`.**

### REFERENCE-ANCHOR pattern

Any param→modal mapping MUST pivot at a captured reference (`m_modal_*_ref`, set in
`LoadPreset`) so default sound is unchanged and only knob *movement* alters it.
Anchoring at an absolute endpoint silently detunes every preset.

### ENGINE_NOISE lifetime

`sustain_level=1.0f` for NOISE engines; `NoteOff` skips `master_env.release()`.
The `noise_env` (Rel knob) fully controls Clap/Shaker tail.

### ENGINE_PLATE: noise_ring_gate

`VoiceState::noise_ring_gate` reset to 1.0 on NoteOn; in processBlock:
```cpp
parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
voice.noise_ring_gate *= voice.modal_decay_1;
```
Floor of 0.15 keeps faint sustained shimmer. Without this: noise stays full while ring
dies → "juxtaposed" sound.

---

## Engine Architecture

### Engine Types

| Engine | Signal path | Presets |
|--------|-------------|---------|
| `ENGINE_KS` | Karplus-Strong delay + modal additive | GuitarStr, Koto |
| `ENGINE_BAR` | Mallet exciter → bar modal bank | Marimba, Vibraphone, Kalimba, SteelPan, Woodblock, Claves, TubularBell, GlassBowl, GlassBottle, SlitDrum, Tick |
| `ENGINE_MEMBRANE` | Strike exciter → circular membrane modal bank + boom osc | Kick2, 808Sub, Timpani, Djambe, Taiko, AcTom, KickDrum, Conga, Handpan, Bongo, Taiko2 |
| `ENGINE_SNARE` | Membrane body (short) + snare-wire resonators | AcSnare, MarchSnare |
| `ENGINE_PLATE` | Strike → inharmonic plate modes + metallic noise + crash bank | Cymbal, Gong, HHatOpen, HHatClosed*, Ride, RideBell, BellTree, Cowbell, Triangle, Tick |
| `ENGINE_NOISE` | Noise burst (+ optional modal body / AM gating) | Clap, Shaker, HHatClosed |

### Preset → Engine Mapping

```
k_Kick2(0)        ENGINE_MEMBRANE  ← ex-Timpani body, kick voice
k_Marimba(1)      ENGINE_BAR
k_808Sub(2)       ENGINE_MEMBRANE
k_AcSnare(3)      ENGINE_SNARE
k_TubularBell(4)  ENGINE_BAR
k_Timpani(5)      ENGINE_MEMBRANE  ← data-driven from Orchestral-Timpani-C.wav
k_Djambe(6)       ENGINE_MEMBRANE
k_Taiko(7)        ENGINE_MEMBRANE  ← data-driven from Taiko-Hit.wav; 6 modes incl. 1472Hz
k_MarchSnare(8)   ENGINE_SNARE
k_Koto(9)         ENGINE_KS
k_Vibraphone(10)  ENGINE_BAR
k_Woodblock(11)   ENGINE_BAR
k_AcousticTom(12) ENGINE_MEMBRANE
k_Cymbal(13)      ENGINE_PLATE
k_Gong(14)        ENGINE_PLATE
k_Kalimba(15)     ENGINE_BAR
k_SteelPan(16)    ENGINE_BAR
k_Claves(17)      ENGINE_BAR
k_Cowbell(18)     ENGINE_PLATE
k_Triangle(19)    ENGINE_PLATE
k_KickDrum(20)    ENGINE_MEMBRANE
k_Clap(21)        ENGINE_NOISE
k_Shaker(22)      ENGINE_NOISE     ← grain-pulse AM + woodblock body
k_Taiko2(23)      ENGINE_MEMBRANE  ← ex-Taiko deep membrane ("DeepBs")
k_GlassBowl(24)   ENGINE_BAR
k_GuitarStr(25)   ENGINE_KS
k_HiHatClosed(26) ENGINE_NOISE     ← ex-Shaker noise voice
k_HiHatOpen(27)   ENGINE_PLATE     ← HW-approved, do not break
k_Conga(28)       ENGINE_MEMBRANE
k_Handpan(29)     ENGINE_MEMBRANE
k_BellTree(30)    ENGINE_PLATE
k_SlitDrum(31)    ENGINE_BAR
k_Ride(32)        ENGINE_PLATE
k_RideBell(33)    ENGINE_PLATE
k_Bongo(34)       ENGINE_MEMBRANE
k_GlassBottle(35) ENGINE_BAR
k_Tick(36)        ENGINE_PLATE
```

### ModalPresetConfig struct fields (synth_engine.h ~line 223)

```cpp
struct ModalPresetConfig {
    float ratio2, ratio3, ratio4;            // mode freq ratios (mode1 = 1.0)
    float t60_1_ms, t60_2_ms, t60_3_ms, t60_4_ms;  // T60 per mode (ms)
    float mix;                               // modal mix (LoadPreset only; see GOTCHA)
    float env1, env2, env3, env4;            // per-mode amplitude weights
    uint8_t mode_count;                      // 2..6
    float ratio5, ratio6;                    // mode 5/6 ratios (0 = fallback formula)
    float env5, env6;                        // per-mode amplitude weights for 5/6
};
// Modes 5/6 T60: always 0.85× and 0.70× of t60_4 (no independent T60 fields).
```

### Parameter → modal-engine mapping

| Knob | Effect | Notes |
|------|--------|-------|
| Dkay | Modal T60 scale: `2^(3*(norm-ref))` | REFERENCE-ANCHORED at shipped Dkay |
| MlltStif / VlMllStf | Upper-mode brightness tilt | REFERENCE-ANCHORED |
| Rel | Ring-length (folded into t60_scale) on modal; noise tail on NOISE | |
| MlltRes | Crash intensity on ENGINE_PLATE; modal presence on non-crash | REFERENCE-ANCHORED |
| Partls (0-4) | Mode count ± around shipped count; env3-6 also scaled | Clamped [2, 6] |
| Model | Modal ratio template swap (9 physical models) | `kModelModalRatios` |
| Inharm | Overtone spread around fundamental | |
| Mterl | Upper-mode material damping | `2^(1.5·Δ)` on modes ≥ 2 |
| HitPos | Strike-position excitation tilt (rim→upper, centre→mode1) | |

---

## TODOs (documented, not started)

- **Tambourine**: bright short jingle modes + light crash + grain AM (basis exists).
- **Shaker**: improved/continuous variant.
- Await next HW listening test on pass 17 before further iteration.

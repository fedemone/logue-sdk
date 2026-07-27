# Brachetti – Session Brief

> Project renamed **RipplerX → Brachetti** (July 2026, branch
> `claude/brachetti-review-rename-rhad77`): directory is now
> `platform/drumlogue/brachetti/`, class `BrachettiSynth`, unit name
> `"Brachetti"`.  `dev_id`/`unit_id` unchanged.

## Dev Branch

`claude/brachetti-review-rename-rhad77` on `fedemone/logue-sdk`
(previous work landed from `claude/snare-drum-realism-optimization-y4hrhf`
and `claude/eager-galileo-2fho84`).

Always rebuild and check `arm-unknown-linux-gnueabihf-size brachetti.elf`:
- `.text` (= text + .rodata) must stay below **28 KB** (safe margin below 30 KB limit).
- `.bss` must stay near **552 bytes**.

## Current Working State

- Unit **loads on hardware** (as of 081e82e); all **40** presets render clean (0 NaN/silent).
- DSP unit tests: **PASS** (exit 0); `test_hw_debug` **90/90**.
- **Listen to Clap (21), Shaker (22) and HHat-C (26) on the next flash.**  Pass 27
  fixed a same-tick gating bug that made all three near-silent *on device only*;
  host renders were always correct, so these presets may never have been heard as
  designed and could want retuning now that they are audible.
  **Counter-evidence to resolve while listening:** HHat-C was once called "a
  perfect closed hi-hat" on hardware (`PROGRESS2.md`, `REALISM_REVIEW.md:461`),
  which does not fit a 20 ms 0.017-RMS blip.  Most likely that listen used MIDI
  note-on/off with a real gate length rather than the internal sequencer — worth
  confirming, because it decides whether the two paths now agree.
- Host syntax-check (g++ -fsyntax-only): **clean**.
- HHat-O **HW-approved** ("ok now" — do not break).
- ARM .text ≤ 28 KB: **must be confirmed on next flash** (cannot verify without toolchain).
- Per-family realism findings + ranked backlog: see `REALISM_REVIEW.md`.

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

### Pass 28 — Preset change during a ringing voice: fade instead of re-excite

Review finding 1, fixed.  Turning the Program knob while anything was sounding
either **burst** or **hard-cut**, measured with `switch_probe.cpp`:

| switch | peak, first 25 ms after ÷ last 25 ms before | after |
|---|---|---|
| Cymbal → Clap | **9.0×** (RMS 19×, still 0.67 at +75 ms) | 0.51 |
| Gong → Kick2 | 1.55× | 0.38 |
| Gong → GtrStr | 1.23× | 0.38 |
| GtrStr → Gong | 1.15× | 0.95 |

Three compounding causes, all the pass-26/27 shape — *state read where it is not
valid*:
- `processBlock` read `kPresetEngine[m_preset_idx]` **live, every block**, so a
  voice struck under one engine was rendered by another engine's per-sample code.
- `LoadPreset` retired ringing voices **only** on the Timpani/Taiko branch.
- The `ENGINE_CYMBAL` branch never calls `process_exciter`, so a cymbal voice sits
  at `current_frame == 0` holding an **unstarted** envelope for its whole ring
  (verified directly).  Switching to a legacy-path preset therefore fired a
  complete, never-played attack — mallet impulse and noise burst — at the ringing
  voice's velocity.  That was the 9× burst.

Fix: `VoiceState::engine` latches the engine at NoteOn and `processBlock` routes
on it; a real preset change arms a ~10 ms exponential fade
(`kPresetFadeTauSec` = 1.5 ms, 6.9 τ to −60 dB) so the orphan retires click-free
**under its own engine**; `LoadPreset` skips its per-voice state writes for a
still-sounding voice (they would retune it mid-fade and `init_modal_modes` would
re-strike its modal bank at full amplitude); and the cymbal soft-headroom stage
is keyed on a cymbal voice having actually rendered rather than on the live
preset index.  `fade_mul` is exactly 1.0f in normal play and the fade branch is
skipped, so **40/40 renders stay byte-identical**.

**Two dead ends worth not repeating.**  (a) It is *not* `LoadPreset`'s
`init_modal_modes` call — skipping that alone leaves the burst intact (tested).
(b) Snapshotting and restoring the fading voice's resonator coefficients across
the setParameter storm changes nothing measurable (tested, then removed): in the
KS branch `lowpass_coeff`/`ap_coeff` are rewritten from `transient_*_base_*`
every sample anyway.  The real second-order problem was **master Gain**: it is a
master parameter, so the incoming preset's drive hit the outgoing tail —
GtrStr (Gain 0, drive 1.0) → Gong (Gain 20, drive 5.0) drove a fading string into
the soft clip at `x/(1+|x|)` = 0.83 against the 0.52 tail it replaced.
Pre-scaling the voice by the drive ratio **cannot** fix that: the ±0.99 clamp
sits between the voice and the drive, so attenuating the voice merely moves it
out from under the clamp and it ends up louder still (measured 1.39×).  The drive
is now **deferred** (`m_pending_drive`) until the last fade retires, which keeps
the outgoing chain bit-for-bit unchanged; an explicit Gain turn overrides it.

Not covered: switches **into** Timpani/Taiko still retire legacy voices outright
(the kernel path bypasses the voice loop entirely, so a fading voice would never
be rendered and would hold its slot forever), and switching **away** from a
kernel preset still hard-stops the kernel.  Both are unchanged from before.

Verified: 40/40 byte-identical, test_dsp exit 0, test_hw_debug **92/92** (new
**T38**; both halves fail against the pre-fix build at 9.0× and 0.67 residual),
host syntax check clean, `stability_sweep` 4096 combos + 480 rolls, worst |peak|
0.9900, 0 problems.  **ARM `.text` still unverified.**

### Open findings — full code review (July 2026), NOT yet fixed

A read of every file that ships to the device (`unit.cc`, `synth_engine.h`,
`dsp_core.h`, `modal_drum_kernel.h`, `envelope.h`, `filter.h`, `noise.h`,
`tables.h`, `header.c`).  Nothing below is fixed — each needs a decision.
Ordered by severity.

**1. Changing the Program knob while a voice is ringing re-excites it.**
**FIXED — see Pass 28 below.**

**2. `LoadPreset` stores the index before bounds-checking it.**
`m_preset_idx = idx;` runs at the top; `if (idx >= k_NumPrograms) return;` is
~65 lines later.  An out-of-range index is therefore *retained*, and
`kPresetEngine[m_preset_idx]`, `modal_preset_configs[idx]` and
`model_param_presets[m_preset_idx]` (all sized 40) are then read out of bounds on
the next NoteOn.  `header.c` caps Program at 39 so a well-behaved OS cannot reach
it — but the guard exists precisely for a misbehaving one, and it does not work.
One-line fix: move the check above the assignment.

**3. `partial_counts[value]` can be indexed negatively.**  In
`setParameter(k_paramPartls)` only `value < 5` is checked, not `value >= 0`.
`header.c` min is 0, so again not OS-reachable.  Same one-line class of fix.

**4. The `.rodata` budget rule and the shipped code disagree by ~35 KB.**
This file states the firmware limit is ≈30 KB for text + `.rodata` and that
`static const T arr[] = {…}` is a "broken pattern to avoid".  The code uses
exactly that pattern for, at minimum:

| symbol | bytes |
|---|---|
| `kTimpaniTransient48[3360]` | 13,440 |
| `kTaikoTransient48[3360]` | 13,440 |
| `kTimpaniModes[280]` | 3,360 |
| `LoadPreset::presets[40][24]` | 3,840 |
| `kTaikoModes[71]` | 852 |

≈**34.9 KB of `.rodata` before a single instruction** (sizes are float/int32
arrays, so architecture-independent; measured with `size -A` on a host `-O2`
build of `unit.cc`).  Since the unit demonstrably loads on hardware, the
**documented budget is probably wrong or stale** — but it is the rule that drove
the non-static-class-member workaround for the preset tables, so it needs to be
corrected or explained before it misleads another pass.  Cannot be settled
without the ARM toolchain.

**5. `noise.h` has no include guard** (every other header does; `float_math.h`
uses `#ifndef __float_math_h`).  Latent only — it is included exactly once, from
`dsp_core.h`.

**6. `header.c` comment is stale**: the Poly parameter still says "Cymbals stay
internally capped at 2 for the CPU budget", which pass 26 removed.

Reviewed and found **correct**, for the record: the TPT SVF core matches
Zavalishin ch. 4 exactly; `process_waveguide` masks both interpolation indices
and clamps `delay_length`; the cymbal resonator-budget clamp can drive `rcount`
negative but the `< 32` floor restores it before `sqrtf(count)` divides, so
`resonatorNorm` is never inf/NaN; `ModalDrumKernel::RebuildBase` clamps the pole
radius to 0.99999 and guards `decay_mult`; the kernel's mode-stretch `log2f` is
guarded against both the 0×−inf and the log(0) cases; `getParameterStrValue`
bounds-checks every table index.  One accepted soft race: `FastSVF::set_coeffs`
writes `a1/a2/a3/q` from the UI thread while `process()` reads them on the audio
thread — a torn update mixes two valid coefficient sets for one sample, and TPT
is unconditionally stable for any `g > 0`, so it cannot diverge.

### Pass 27 — Review pass: Clap/Shaker/HHat-C were silent on hardware

Not an HW report — found by **reviewing the whole codebase for the defect class
behind the pass-26 gong bug**: *a decision reads a quantity that is not yet valid
at read time.*  The gong stole the voice with the smallest `magEnv` while
`magEnv` was still 0 from the attack.  The same shape, one severity higher:

**`FastEnvelope::release()` was applied to envelopes still sitting at
`value == 0` in `ENV_ATTACK`**, which sends them to `ENV_IDLE` on the very first
`process()` (`value += (0-value)*rate` stays 0 → trips the `<= 0.001f` cutoff).
Since the Drumlogue fires `gate_on`+`gate_off` in one tick, **the three
`ENGINE_NOISE` presets — Clap (21), Shaker (22), HHat-C (26) — produced a ~20 ms
blip and nothing else on device.**  Measured same-tick RMS per 25 ms:
`0.018` then all zeros, against `0.256 / 0.430 / 0.281` with the gate held.
Every host tool held the gate 20-50 ms, so all 40 renders, `param_audit`,
`stability_sweep` and 26 HW passes reported these presets healthy.

Fixed **at the mechanism**, not the site: `release()` during the attack defers
into the new `ENV_ATTACK_REL` state, the attack completes, then the release runs
from the top of it.  An already-open envelope is bit-for-bit untouched, so all
**40/40 renders stay byte-identical** — and unlike the pass-19 "skip the
release" patch, `Rel` still shapes the tail.  Restored same-tick RMS:
Clap `0.359 0.166 0.059 0.024`, Shaker `0.430 0.213 0.158`, HHat-C
`0.357 0.054 0.006`.  The pass-19 `ENGINE_SNARE` skip is left in place as the
voicing choice it now is.

**Process lesson — this bug had been fixed twice before, per-site** (`master_env`
in T20, `ENGINE_SNARE` in pass 19), each time as a local workaround, so the third
site was simply never covered.  Both earlier fixes are still described in the
code as same-tick workarounds; **when a gotcha needs a second per-site
workaround, fix the mechanism.**  And note what no existing tool could catch:
`render_presets`/`param_audit` hold the gate, and `stability_sweep` only checks
peaks and NaN, so a voice that silences *itself* passes everything.  **T37** now
asserts the same-tick path directly (both halves fail against the pre-fix build);
`samegate_probe.cpp` prints same-tick vs held RMS side by side for any preset.

Verified: 40/40 renders byte-identical, test_dsp exit 0, test_hw_debug **90/90**,
host syntax check clean, `stability_sweep` 4096 combos + 480 rolls, worst |peak|
0.9900, 0 problems — unchanged from pass 26.  `.bss` unchanged (the new state is
an extra enumerator, no new fields).  **ARM `.text` still NOT verified** — no
cross-compiler in this session; the change is a handful of instructions in an
inlined switch.

### Pass 26 — Gong/cymbal stacking + "subtle → live" knob depth pass

Two HW notes.

**"Multiple gong hits are not stacking correctly."**  Two compounding bugs in
the `ENGINE_CYMBAL` voice policy:
- The family was hard-capped at **2 voices** (`m_cym_poly = min(Poly, 2)`), a CPU
  guard predating the magnitude squelch — on the one instrument whose hits are
  meant to pile up.
- The steal rule **inverted itself**: it took the smallest `magEnv`, which is a
  ~10 ms average of `|out|` starting at 0, while the gong's driver needs
  `lowAttackSec` = 0.25 s to open.  For the first third of a second the NEWEST
  hit is the quietest voice in the bank, so the third strike killed the second
  one mid-bloom and repeats ping-ponged between two slots.

Fix: `m_cym_poly = m_poly` (full 1-4); `kCymStealProtectSec` = 0.60 s protects
young voices and falls back to stealing the **oldest** (age is the reliable
ordering inside that window — `magEnv` on a dense inharmonic wash is not);
`kCymResonatorBudget` = 240 caps the TOTAL bank across simultaneous cymbal
voices, which is a stronger CPU guarantee than a voice count and is what makes 4
voices affordable.  A restruck/stolen voice also keeps its ring (`retainRing`,
×0.75) instead of zeroing `resY1/resY2` — metal does not reset.
Measured: Gong 4 hits → distinct voices 2 → **4**, passage RMS 1.62× → 1.84×
(150 ms) and 1.90× → 2.02× (1 s); 8 hits at 150 ms reach 2.24×; peak stays
0.83 (limiter-bounded); worst aggregate bank 208 resonators at `Rsntrs` = 60.
New **T36** in `test_hw_debug.cpp` asserts all three properties — T36a fails
against the pre-fix code.

**"Increase the effect of the parameters from subtle to live, without breaking
the presets."**  Every reference-anchored mapping had its coefficient widened
and clamps opened, anchoring untouched → **40/40 byte-identical**.  Two dead
knobs wired: cymbal `MlltRes` → ring presence, cymbal `MlltStif` → beater
hardness (both audited `NO EFFECT`); kick `MlltStif` gained a live lower half
(down = slower boom onset = felt beater; it was clamped to 0 from below and
Kick2/KickDrum ship it at 0.60/0.70).  Measured: **43** knob/family swings
widened, 2 dead → live, 1 narrowed 5 % (noise); verdict changes **5 improved, 0
regressed**.  Cymbal `Inharm` deliberately left at 2.0 — widening it *shrank*
the measured HF swing (the jitter piles modes onto the `fLo`/Nyquist clamps).

**Tooling lesson (`param_audit.cpp`).**  Beyond the documented 2-s-RMS transient
blind spot, the audit had a worse one: `FastNoise::seed` is free-running and no
reset touches it, so consecutive renders start from a different noise state.
Clap/Shaker/hats drifted a few percent between runs and flipped their own
verdicts — this pass first "found" a Partls regression on Clap for a knob that
provably does nothing there (`is_modal_engine` is false for `ENGINE_NOISE`).
The audit now pins the seed per render.  Also: on the kick, use a **band**
metric, not RMS — the master chain is loudness-maximised, so a thump reshapes
the attack spectrum without raising level (`TubRad` reads 29 % → 48 % on a
100-400 Hz vs sub band metric and almost nothing on plain RMS).

Verified: 40/40 renders byte-identical, test_dsp exit 0, test_hw_debug **88/88**,
host syntax check clean, `stability_sweep` **4096** combos across 16 presets +
**480** rolls across 40 presets with 0 problems and worst |peak| 0.9900 (the
brickwall limit — bounded, and identical to the pre-change baseline despite the
much wider decay clamps).  **ARM `.text` is NOT verified** — no cross-compiler
in the session that made this pass; confirm against the 28 KB budget on the next
flash, since the pass adds code (cymbal MlltRes/MlltStif blocks, the steal
ranking and the resonator budget).

### Pass 25 — Roll fusion (pass-23 regression) + asymmetric snare TubRad

Two HW notes, both regressions from pass 23/24:

- **"Pressed rolls feel less smooth, Djambe especially is muddy."**  Pass 23's
  unconditional stacking gave every stroke of a 15-25/s pressed roll its own
  voice, so up to `Poly` complete drum bodies (each with its own crack/slap/
  beater burst) piled up — and it silently disabled the pass-19 snare buzz-roll
  wire continuity, which can only fire when the reused slot is the one still
  rattling.  `NoteOn` now **fuses** repeats before the round-robin advances:
  same note + drum family (MEMBRANE/SNARE/NOISE) + gap < `kRollFuseSec` (80 ms)
  → reuse the last voice; anything wider stacks; different note stacks;
  ENGINE_CYMBAL/BAR/PLATE always stack (HW: "important for cymbals").
  `kRollFuseSec` is **one constant** shared with the wire-restore test so the two
  can't drift.  Measured: Djambe 45 ms roll 4 voices → 1, passage RMS
  0.444 → 0.408; AcTom 0.535 → 0.421; ≥85 ms bit-identical to unfused.
- **"TubRad body-depth sounds a bit toy-ish at lower values."**  The pass-23
  mapping was symmetric, so on a preset shipping TubRad=20 the knob floor pushed
  Band A ×2.46 (+1.3 oct) — a toy snare, not a shallow one.  Deepening keeps the
  full −1.3 oct; **thinning now runs at half rate and caps at ×1.15**.  Δ=0 takes
  the deepening branch (`knob_exp2(0)` == 1.0 exactly) → still byte-identical.

**Test-design lesson (T18).**  Adding roll fusion broke T18b, and the failure was
*correct*: T18 hard-coded `voices[2]` as "the slot the second NoteOn takes".
Worse, T18a had been passing **spuriously** — it probed an untouched voice reading
0.0 and asserted `0 < nominal`.  Tests must read `state.next_voice_idx` rather
than assume an allocation order.  New **T35** asserts the fusion policy itself
(fused / stacked / sustained-excluded) so it can't be rediscovered on hardware a
third time, and `stability_sweep.cpp` gained a **phase 2 roll stress** (every
preset × 3 gaps × same/alternating notes × shipped/extreme knobs) because phase 1
leaves 3 s between hits and never exercised any roll path.

Verified: **40/40 renders byte-identical**, test_dsp exit 0, test_hw_debug
**85/85**, snare TubRad still audits `ok`.

### Pass 24 — Poly made real, VlMll* live everywhere, thump-only kick, knob_exp2

HW batch on Kick2/AcSnare. All five items landed:

- **Poly knob was a lie**: it read 1-2, was cymbal-only (`m_cym_poly`) and always
  displayed "2".  Now a **global** cap (header 1-4, default 4, `type_strings`):
  new `m_poly` bounds the NoteOn round-robin (`next_voice_idx % cap`), the
  cymbal family clamps to `min(Poly,2)` for CPU, and `getParameterStrValue`
  prints the **effective** polyphony for the current preset — `4(2)` on a
  cymbal/kernel, `4(1)` on a string.  Verified: Kick2 Poly=2 → 2 simultaneous
  voices, Poly=4 → 4.  ENGINE_KS keeps the full-range increment (GateOff pins
  it to one slot; capping there would move that slot).
- **`VlMllRes`/`VlMllStf` wired on Kick + Tom, `VlMllStf` on Cymbal.**  They had
  only ever driven the shared mallet exciter + noise attack, which the kick boom
  and tom body mask.  Kick: VlMllRes → velocity-weighted thump prominence
  (standalone — no MlltRes needed), VlMllStf → its own knock + thump pitch/decay
  snap.  Tom: VlMllRes → slap prominence (scales a shipped `trans_*` layer, or
  raises a hand-slap burst on presets without one), VlMllStf → slap sharpness.
  Cymbal: VlMllStf → stick stiffness (`lowAttackSec`/`thwackSec`/`hfTilt`).
  All anchored on the shipped values (0 everywhere) → **40/40 byte-identical**.
- **Thump-only kick** (HW: "high thump, almost no boom"): the `Mterl` → boom
  weight curve now reaches **zero** at the knob floor (quadratic in `kmn/ref`
  below the anchor, continuous ×1 at it; unchanged `2^(1.2Δ)` above).  Measured
  on 808Sub: shipped total RMS 0.215 / tail 0.084 → `Mterl=-10` 0.010 / 0.000 →
  `+MlltRes=1000` attack 0.192 / tail 0.000.
- **`knob_exp2` (fasterpow2f) for knob curves** — see the new gotcha below.
- **README matrix corrected** (3 wrong cells) + a maintenance rule.

**Doc-accuracy incident (prevention).**  The Pass-23 "which knobs are live"
matrix was written by hand from memory and marked `VlMllRes`/`VlMllStf` inert on
the Timpani/Taiko kernel and `VlMllRes` inert on cymbals — all three have ALWAYS
been live (`RefreshKernelMods` reads both; the cymbal `stickLevel` block reads
VlMllRes).  A wrong "inert by design" label is worse than none: it disguises a
regression as an intention.  **Rule: re-derive that table from `param_audit.cpp`
+ the code, never from memory**, and remember the audit's blind spot — its 2 s
RMS metric dilutes a 40 ms transient ~50×, so a knob adding an attack can audit
`weak` while being clearly audible (confirm with a windowed probe).
(Separately: the HW report read the README's kick-thump section, which documents
`MlltRes`/`MlltStif`, against the matrix rows for `VlMllRes`/`VlMllStf` — four
different knobs.  README now has an explicit disambiguation table.)

Verified: 40/40 byte-identical, test_dsp exit 0, test_hw_debug 82/82, stability
**4096** combos (7 body knobs + a combined VlMllRes/VlMllStf extreme) across 16
presets, 0 problems, worst |peak| 0.9900 = the brickwall limit (bounded).

### Pass 23 — Inharm step-10 + membrane/snare/noise polyphony (HW feedback)

HW test on Kick2 + AcSnare exemplars produced a batch of feedback.  Two items
were unambiguous and landed this pass:

- **`Inharm` encoder step 1 → 10.**  Adopted the `Dkay`/`Resnc` ÷10 pattern:
  header range 0-1999 → **0-199** (`type_strings`, display ×10), all Inharm
  consumers `×0.0005 → ×0.005`, guard `value<=1999 → <=199`, and the preset
  `InHm` column rescaled ÷10 (round-half-up) by a one-shot script.  **39/40
  presets byte-identical**; only `Koto` (sole KS preset with non-zero shipped
  Inharm, 1→0) shifts its `ap_coeff` 0.0005→0 = **−59 dB, inaudible** (render
  diff confirmed).  T27a updated (max Inharm 199 → ap_coeff 0.995).
- **MEMBRANE/SNARE/NOISE polyphony.**  `GateOff` stacking predicate widened
  from `PLATE||BAR||CYMBAL` to **`e != ENGINE_KS`** — kick/tom/conga/bongo/
  snare/clap now round-robin the 4 voices (fast repeats stack instead of
  choking one voice).  Guards kept: cymbal capped by `m_cym_poly` (Poly knob),
  Timpani/Taiko on the 2-kettle kernel path, KS mono (string-beating).  Poly
  stress test: Kick2/808Sub/AcSnare/Conga/Bongo reach 4 simultaneous voices,
  worst peak 0.84, no NaN.  **NOTE for HW:** this supersedes the mono-voice
  buzz-roll continuity (pass 19 r2) for snares — fast pressed rolls now spread
  across voices (each with its own crack) rather than merging into one voice;
  confirm the roll feel on HW.

Follow-up items from the same HW batch (user picked, second commit):

- **Snare TubRad → body depth** (only-TubRad, per user).  New reference-anchored
  mapping in the snare NoteOn block: `sn_body = clamp(exp2(-1.3·Δtubrad))`
  applied to **Band A only** (the low body band, ~2.8 kHz on AcSnare), so
  MlltStif (all bands = brightness) and TubRad (Band A = body depth) stay
  independent.  Shipped snares byte-identical; AcSnare hi-energy fraction sweeps
  0.55 (tight) → 0.44 (shipped) → 0.30 (deep) across TubRad 0→20.  Dkay left
  subtle on the snare (user chose not to overlap Rel).
- **Kick poly kept at 4** (user), **VlMllRes zap kept aggressive** (user) — no
  code change.
- **"X-for-unwired" → documented instead of on-device** (user).  Added the
  per-preset knob-activity matrix to the README, and **corrected** the Pass-22
  mislabel: `Resnc` is the **master-LP Q** (needs `Cutoff` down), not a
  "noise-driver Q"; `Rsntrs`/`Poly` are cymbal-only performance controls;
  Model/Partls are inert wherever the engine bypasses the shared modal bank.
- **Velocity/hit-hardness**: answered (MIDI note-on velocity, set per-step on
  the Drumlogue sequencer; no dedicated knob — 24/24 param slots used).

### Pass 22 — All-family body-knob wiring (branch brachetti-review-rename)

`param_audit.cpp` (extended to a CYMBAL + MEMB-KICK exemplar) confirmed the
body-shaping knobs `Dkay/Mterl/HitPos/Rel/Inharm/TubRad/Resnc` were dead on
**two whole families**, because those engines don't use the shared modal bank
the knobs feed:
- **Kick** (Kick2/808Sub/KickDrum): 808Sub/KickDrum use the empty default
  modal config (`mode_count 0`) so the shared modal routing is skipped
  entirely; the audible voice is the boom oscillator.
- **Cymbal** (Cymbal/Gong/HHatOpen/Ride/RideBell/Splash): the dense-resonator
  port (`cymbal_note_on`) bypasses the modal bank.
- **Timpani/Taiko**: the separate dense **drum-kernel** (`m_drum_kernel`)
  bypasses the legacy voice loop; its knobs route through `RefreshKernelMods`.

Each family now maps the dead knobs to a natural property of its own engine,
all reference-anchored → **40 shipped presets byte-identical** (verified by
render diff):
- **Kick**: Dkay+Rel → boom decay length; Mterl → boom weight; TubRad → boom
  base tune (new `VoiceState::boom_tune`, applied in the per-sample sweep
  formulas since KickDrum/808Sub recompute `boom_inc`); Inharm → pitch-dive
  depth (808Sub); HitPos → beater click (borrows the `trans_*` burst).
- **Cymbal** (into `CymbalConfig` before `cymbal_note_on`): Dkay → ring decay,
  Rel → sizzle tail, Mterl → hfTilt+maxHz brightness, HitPos → edge/bell
  (stick vs wash), Inharm → jitterSemis spread, TubRad → size (folds into
  `pitch_ratio`), Resnc already lived (noise-driver Q).
- **Kernel drums**: added TubRad → `decay_mult`↑ + `hf_decay_tilt`↓ (bigger =
  longer+darker); widened Mterl 1.2→2.0.
- **Legacy membrane/bar/plate**: widened the existing modal TubRad from t1-only
  to the whole body (t1..t4), and extended Mterl's onset tilt to mode 2.

Verified with a pitch/tail probe (`verify_blind.cpp`, the 2s-RMS audit is blind
to pitch shifts and >2s decays): kick TubRad drops boom 81→53 Hz; Timpani
kernel ring 6.7→52 (×1e3 tail); Conga/AcTom rings lengthen; cymbal Dkay/Rel
lengthen the tail and TubRad shifts the HF fraction 0.49→0.23.  Stability:
extreme 7-knob corner combos across all affected presets NaN/blow-up free;
test_dsp PASS, 82/82 test_hw_debug PASS, 0 NaN/silence / 40 renders.
`Resnc`/`TubRad` remain inert-by-design where the engine has no matching
mechanism (Resnc needs a noise bed; TubRad on a plain kick = tune only).

### Pass 21 — Kick "thump" + snare-sweep corruption check (branch brachetti-review-rename)

- **Snare param sweeps verified non-corrupting**: swept each rewired snare
  knob lo/mid/hi across all 4 snares — Rel t40 spans ~100→850 ms, MlltRes /
  VlMllStf ~2× the buzz energy, VlMllRes adds onset snap; peaks bounded well
  under the 0.99 limiter, no NaN/silence, all moves smooth and musical.
- **Kick family**: the audit confirmed the mallet knobs (MlltStif/VlMllRes/
  VlMllStf/Mterl/Inharm/HitPos) were NO EFFECT/weak — the kick is boom +
  (quiet) modal body, and the mallet click is a tiny high tick, not a mid
  punch. Added a dedicated `VoiceState::thump_*` layer: a fast pitch-dropping
  mid sine (~300→115 Hz, T60 ≈ 58 ms) over the boom, wired to `MlltRes`
  (amount) + `MlltStif` (snap/pitch), reference-anchored → shipped kicks
  bit-identical, knob-up adds real punch (added-signal RMS 0.10-0.19 over the
  first 100 ms; the soft-clip means it reshapes attack timbre, not peak).
  Stable across 48 extreme kick knob combos.

### Pass 20 — Snare param-design layer + Resnc granularity (branch brachetti-review-rename)

The snare family had ~13 UI knobs that were inaudible (mallet masked by noise;
VlMllRes' attack target overridden; Rel dead because ENGINE_SNARE skips the
noise-env release). Empirical `param_audit` (snare variant) + code trace
confirmed it. Fix: a **reference-anchored snare param-design layer** in
`NoteOn` that wires the dead/weak knobs to the wire buzz (the snare's defining
voice), each a delta from the shipped value so shipped presets stay
bit-identical:
- `Rel` → buzz **tail length**, `MlltRes` → buzz **amount**, `MlltStif` →
  buzz **brightness**, `VlMllStf` → buzz **tightness/Q**, `VlMllRes` →
  **crack/snap** (new `ExciterState::snare_crack_gain`, scales the onset crack
  burst). See the README "Snare param-design layer" table.
- Verified: all 40 shipped renders byte-identical; the five knobs now swing
  strongly (audit) and 256 extreme-value combos are NaN/blow-up free (wire
  pole radius clamped < 0.995).
- **Resnc** encoder granularity coarsened from step 1 (707–4000, 3293 steps)
  to step 10 (stored ÷10, 71–400; display shows 710–4000 like MlltStif/Dkay).
  The Q map is `0.707 + (v−71)·0.01` so the shipped default (71) hits Q 0.707
  exactly → bit-identical.

### Pass 19 — Snare family revival + BrshSnr preset + CPU pass (branch snare-drum-realism-optimization)

Structural snare fixes (all measured; details in `REALISM_REVIEW.md`):
- **Wire path was dead on every live hit**: `NoteOn → PartialReset()` zeroes
  `snare_wire_mix` (only `LoadPreset` ever set it), and the velocity-tuned wire
  band coefficients were written BEFORE `PartialReset` and clobbered.  Wire
  params are now restored per-NoteOn and the band setup runs after the reset.
  Restore is gated to ENGINE_SNARE so presets HW-approved with the wire silent
  (e.g. KickDrum's dormant 0.03 mix) are unchanged.
- **Same-tick gate-off choked the buzz to ~26 ms**: ENGINE_SNARE now skips the
  noise-env release; the tail decays at the natural NzRs rate (AcSnare NzRs
  740→950, MrchSnr 800→940 — calibrated to ref t40s).  Renders and hardware now
  behave identically for snares.
- **Velocity → buzz physics**: soft hits get less wire mix + up to 2× faster
  decay, anchored at full velocity (calibrated sound unchanged at vel=127).
- Result: MrchSnr centroid 4705 Hz vs ref 4683, t40 230 vs 220 ms; AcSnare
  t40 270 vs 280 ms.
- **New preset 38 "BrshSnr"** (ENGINE_SNARE): brush sweep — ~30 ms swish
  onset, 6.5 Hz enveloped swirl AM (noise_am machinery), diffuse low-Q wire
  bands 2.2/3.6/6.3 kHz, BP noise ≈4.2 kHz, long tail.  Awaiting HW listen.
- **CPU**: idle render cost cut ~99% (idle guard skips master chain when no
  voice is active, after a 320 ms flush); Tone=0 tilt-EQ skip; several
  per-sample invariants hoisted.  All 39 renders byte-identical.
- Also fixed: HEAD did not compile (orphaned `pre_clip_trim` from the cymbal
  port merge).

**Round 2 (HW feedback on pass 19):**
- BrshSnr "too fast, hit too hard" → velocity compressed (0.30-0.72), swish
  onset ~100 ms, wires resting on head (`k_wire_onset_env=1` → no crack
  burst), 4.2 Hz swirl, tail T60≈0.64 s.  HW may supply a brush reference
  sample for calibration.
- **RimShot approved & added** (preset 39, ENGINE_SNARE): hard stick, rim-ring
  mode cluster (2.42/3.38 rings longer than the head), tight 70 ms buzz.
- **Buzz-roll continuity approved & added**: snare retriggers < 80 ms restore
  the wire resonator states across PartialReset and skip the crack burst.
- **All 7 per-family recommendations approved & applied** — measured results in
  `REALISM_REVIEW.md` "Round-2 results".  Headline: Cymbal/Ride/RidBel body
  centroid fixed via `CymbalConfig.hfTilt` HF-weighted resGain (Cymbal
  927→6932 Hz vs ref 7366; Ride 6121 vs 6087).  Kalimba/Cowbell/Conga/AcTom/
  Triangle/Clap table retunes.  Stereo idea discarded per HW.

### Pass 18 — FDN dense wash (cymbals) + strike transient layer (Timpani/Taiko)

HW after pass 17: "sound improved, but still the very same problems as before."
Diagnosis: pass 16's coupling matched the *temporal* band-envelope (corr +0.99) but
not spectral **density** — ~8 resonators produce ~8 tones, so the cymbal wash still
read as "tones + a separate noise bed". And for Timpani/Taiko the modal *tail* is
now correct; the remaining gap is the broadband **attack** the modal bank cannot make.
Two structural additions (both pure DSP, no samples, no `.text` table growth):

**Cymbals — Feedback Delay Network (FDN) dense wash**
- 4-line Hadamard FDN hosted in the **KS-dead `resB.buffer`** (zero new RAM; resB is
  unused on `ENGINE_PLATE`). 4 mutually-prime delay lengths (281/359/419/487) in
  4×512-sample partitions → hundreds of dense inharmonic modes the resonator bank
  can't make. Lossless orthonormal Hadamard × sub-unity gain ⇒ guaranteed stable.
  Per-line one-pole HF damping. Driven by the same nonlinear crash `exc`, so it blooms
  with the strike and feeds the bloom self-PM too.
- Per-preset params in NoteOn crash block (`fdn_g/damp/drive/mix`); `resB.buffer`
  cleared on each strike. Bright crashes long+light; open hat short; Gong/RidBel light
  mix so their pitched character stays foreground.
- **Result (measured, body spectral flatness)**: Cymbal 0.2→**0.70**, Ride →**0.71**,
  HHat-O →**0.58**, RidBel →**0.62** (ref ~0.55); Gong kept tonal 0.10 by design.
  The wash now has real broadband density — the documented floor is **lifted**.

**Timpani/Taiko — strike transient layer**
- Short velocity-scaled band-passed white-noise burst (difference-of-one-poles BP)
  layered over the modal body = the stick-slap / mallet-contact attack. State on
  `VoiceState` (`trans_env/decay/gain/a_lo/a_hi/lp_*`); fires in NoteOn, runs per-sample
  before the boom block.
- Taiko: ~2-6 kHz, T60≈18 ms, gain 2.9 → early centroid **539→1806 Hz** (ref 3374;
  remaining gap is the ref's close-mic stick, deliberately not chased to avoid hiss).
- Timpani: ~0.6-2.6 kHz, T60≈12 ms, felt-soft (centroid barely shifts because the ~82 Hz
  fundamental dominates the energy metric — by design; a clicky attack would be wrong).

Tests: 82/82 PASS (incl. T27 all-37 in-bounds, T28 voice-cycle no-NaN, T21a heavy-strike).
**ARM `.text` ≤ 28 KB must be confirmed on next flash** (added code is small; verify).

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
- ENGINE_SNARE — AcSnare, MarchSnare (BrushSnare added in pass 19).
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

# Render all presets to WAV
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
| ~~Cymbal spectral flatness ~0.03-0.23 vs ref ~0.55~~ | ~~6-resonator bank is fundamentally tonal~~ | **RESOLVED in pass 18** — FDN dense wash lifts flatness to ~0.6-0.7 |
| Taiko early centroid 1806 Hz vs ref 3374 Hz | Ref has prominent close-mic stick transient | Improved 3.4× via pass-18 transient layer; full ref not chased (would add hiss over the wood "tak") |
| Ride 34 Hz correlated AM | Sparse 6-resonator bank beating | Addressed via broadband noise dominance + pass-18 FDN density |

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

### knob_exp2 — fast 2^x for knob curves, but GUARD THE ANCHOR

Reference-anchored knob mappings don't need exact math (±0.3 % on a response
curve is inaudible), so they use `fasterpow2f` via the `knob_exp2` helper in
`synth_engine.h`.  **The naive swap is a trap**: `fasterpow2f(0.0f) ≈ 0.9614`,
not 1.0 — and every anchored mapping evaluates at exactly Δ=0 for its shipped
preset, where the factor MUST be exactly 1.0 or the byte-identical guarantee
breaks on all 40 presets at once (silently: everything just gets ~4 % shorter
/ quieter).  `knob_exp2` returns 1.0 for x==0 and `fasterpow2f` otherwise.

```cpp
inline float knob_exp2(float x) { return (x == 0.0f) ? 1.0f : fasterpow2f(x); }
```

Use it for knob-response curves only.  **Keep exact `exp2f`** for tuning and
timing: note→frequency ratios, `cymbal pitch_ratio`, the kernel trigger ratio —
same reasoning as the `fasterexpf` / `fastercosfullf` gotchas above.

### REFERENCE-ANCHOR pattern

Any param→modal mapping MUST pivot at a captured reference (`m_modal_*_ref`, set in
`LoadPreset`) so default sound is unchanged and only knob *movement* alters it.
Anchoring at an absolute endpoint silently detunes every preset.

### GOTCHA: NoteOn ordering vs PartialReset

`NoteOn` calls `v.PartialReset()` roughly mid-way through.  Any per-voice
exciter/voice state written BEFORE that call that PartialReset touches is
silently clobbered (this killed the snare wire for 18 HW passes).  Rule:
per-hit DSP state setup belongs AFTER `PartialReset()`, in the restoration
section next to the boom/onset params.

### ENGINE_SNARE lifetime

NoteOff does NOT release the snare noise envelopes.  The buzz decays at the
natural NzRs-governed ENV_DECAY rate; Rel therefore does not shape the snare
noise tail.  This is a **voicing** choice (real wires ring freely once the stick
leaves the head, and the Rel-rate release choked the buzz to ~26 ms), not the
same-tick workaround it started life as — see the same-tick gotcha below.

### ENGINE_NOISE lifetime

`sustain_level=1.0f` for NOISE engines; `NoteOff` skips `master_env.release()`.
The `noise_env` (Rel knob) fully controls Clap/Shaker tail.

### GOTCHA: a release that arrives DURING the attack (the same-tick killer)

The Drumlogue fires `gate_on` **and** `gate_off` in one scheduler tick, before
any audio block.  So every `release()` in this codebase can land on an envelope
that `trigger()` has just zeroed and `process()` has never advanced.  Naively
that is fatal — `ENV_RELEASE` computes `value += (0 - value)*release_rate`,
which is still 0, trips the `value <= 0.001f` cutoff and goes `ENV_IDLE`: the
envelope dies before it opens and the voice is silent **on hardware only**,
because every host render holds the gate 20-50 ms and looks perfect.

This bug was found and fixed **three times, per-site**, before the mechanism
itself was fixed:
1. `master_env`, pass ~12 (T20) — force-set to `1.0`/`ENV_DECAY` in `NoteOn`.
2. `ENGINE_SNARE` noise envs, pass 19 — `NoteOff` skips the release ("choked
   the buzz to ~26 ms").
3. `ENGINE_NOISE` noise envs, pass 27 — **never covered by either**, so Clap,
   Shaker and HHat-C were emitting a 20 ms blip and nothing else on device.

`FastEnvelope::release()` now defers into **`ENV_ATTACK_REL`** when it is called
during `ENV_ATTACK`: the attack completes, then the release runs from the top of
it.  An envelope that is already open is untouched (40/40 renders byte-identical),
and `Rel` keeps shaping the tail — which the pass-19-style "skip the release"
patch does not.  **New sites do not need their own workaround.**  Regression
test: **T37** (both halves fail against the pre-fix build).

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
| `ENGINE_SNARE` | Membrane body (short) + snare-wire resonators | AcSnare, MarchSnare, BrushSnare |
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
k_Splash(37)      ENGINE_CYMBAL    ← small pitched splash (dense-resonator engine)
k_BrushSnare(38)  ENGINE_SNARE     ← "BrshSnr": brush sweep, swirl AM + diffuse wires
k_RimShot(39)     ENGINE_SNARE     ← "RimShot": stick crack + rim-ring ping + tight buzz
```

NOTE: Cymbal(13), Gong(14), HHatOpen(27), Ride(32), RideBell(33) were moved to
`ENGINE_CYMBAL` (dense-resonator port) in commit 6e28c0a — the table above shows
the historical PLATE rows for context; `kPresetEngine[]` in synth_engine.h is
the authority.

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

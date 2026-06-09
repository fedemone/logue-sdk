# RipplerX – Session Brief

## Working State (branch tip — see git log for exact commit)

- Unit **loads on hardware** and all non-KS presets now produce inharmonic modal
  sounds instead of strings.
- Marimba ring bug is **fixed** — ring now lasts ~1.2s as configured (Phase 2 complete).
- Flute and Clarinet are silenced (ENGINE_REMOVED).
- **Dkay controls modal T60** for BAR/MEMBRANE/SNARE/PLATE engines, anchored at the
  preset's shipped Dkay (`m_modal_dkay_ref`): `t60_scale = 2^(3*(norm - ref))`.
  Calibrated config T60 always plays at the default Dkay (no regression); knob trims
  around it.  Uses `exp2f` (NOT `fasterpow2f` — that returns 0.971 at scale 1.0).
- **Mallet stiffness → modal brightness** (`m_modal_stiff_ref` pivot).  MlltStif and
  VlMllStf (velocity) now tilt the higher modal modes' initial energy: stiffer = brighter.
  Neutral at the shipped MlltStif so no default-sound regression.
- **Noise-ring coupling (ENGINE_PLATE):** `noise_ring_gate` in VoiceState tracks the
  modal fundamental decay (`modal_decay_1`) each sample. `parallel_noise_gain` is
  multiplied by `fmax(0.15, noise_ring_gate)` so noise amplitude follows the ring — no
  more "juxtaposed" independent noise on metallic instruments.
- **ENGINE_NOISE duration fix:** `sustain_level=1.0f` for NOISE engines (not 0); NoteOff
  skips `master_env.release()` for NOISE. The `noise_env` (Rel knob) now fully controls
  Clap/Shaker tail. Old behaviour: master_env auto-decayed in ~11 ms at default Dkay,
  killing the voice before `noise_env` could produce any tail.
- **Rel → noise_env.release_rate** for ENGINE_NOISE:
  `rel_rate = 0.00005 + (1-norm)*0.01`. At Rel=18 (norm=0.90): ~200 ms tail.
- **HiHat/Cowbell modal ratios:** switched from membrane Bessel (1.479/1.932/2.332) to
  plate ratios (2.92/6.37, 2.00/2.68/4.30) — Bessel ratios produced "wood-like pop"
  instead of metallic character.
- **Gong k_modal_mix was 0 (silent body):** Only FM chirp + noise played ("sci-fi zap").
  Fixed to 0.20 in model_param_presets. FM depth halved (0.16→0.08).

### Parameter → modal-engine mapping status (HW-reported, 2nd pass)
- **Working on modal engines:** Dkay (T60), MlltStif/VlMllStf (brightness), Tone, LowCut,
  Gain, NzMix, and the noise-resonance metallic character.
- **Rel works for ENGINE_NOISE** (Clap/Shaker) — controls noise_env release tail.
- **No effect on modal engines (expected — KS-only params):** MlltRes, Model, Partials,
  Inharm, HitPos, TubRad.  These drive the bypassed KS waveguide.  Wiring Model →
  modal ratio-set and Partls → mode-count is a candidate future task.

### REFERENCE-ANCHOR pattern (important)
Calibrated modal configs are tuned assuming the preset's *shipped* knob value.  Any new
param→modal mapping MUST pivot at a captured reference (`m_modal_*_ref`, set in LoadPreset)
so the default sound is unchanged and only knob *movement* alters it.  Anchoring at an
absolute endpoint (e.g. Dkay=200) silently detunes every preset — that was the
"string-like" Marimba regression.

### GOTCHA: modal mix lives in TWO tables
`ModalPresetConfig.mix` is used only by `LoadPreset`.  `NoteOn` re-inits the modal bank
using `model_param_presets[preset][k_modal_mix]`, which **overrides** it.  Keep both in
sync.  MarchSnare shipped with config.mix=0.14 but `k_modal_mix`=0 → silent body, pure
noise.  Always edit the `model_param_presets` `k_modal_mix` column to change the audible mix.

## Critical .rodata / .data Constraint — Do NOT break this

The drumlogue firmware checks `.text segment` size (= `.text + .rodata + .init + .fini`) per unit. Limit ≈ 30 KB. The preset tables (~7 KB) must stay in `.data`.

**Working fix (a49e2f4):** The large preset arrays —
`kDefaultModalPresetConfig`, `modal_preset_configs[]`, `model_param_presets[][]`,
`kPresetEngine[]` — are declared as **non-static** class members (no `static`,
no `const`, no `constexpr`). This makes them part of the global `s_synth`
object layout and places their initial values in `.data`.

**Broken patterns to avoid:**
- `static constexpr T arr[] = {...}` → goes to `.rodata` → text-size check fails
- `static const T arr[] = {...}` → same problem
- `static T arr[] = {...}` **inside a class body** → GCC 6.5 rejects it (out-of-line
  definition required for non-const static members, and constexpr causes .rodata)

See `config.mk` `USE_LTO := no` and the comment block for background.

## Root Cause of Marimba "Click, No Ring" Bug (fixed — see current commit)

`fasterexpf` from the fast-math library is catastrophically inaccurate for
arguments with |x| < ~0.001. `modal_decay` is computed as
`expf(ln(0.001) / (T60_s × srate))`. For T60=1.2s the argument is -0.000120,
for T60=5.0s it is -0.0000288 — both fall in the broken range.
`fasterexpf(-0.000120)` returns ~0.971 (implying T60≈5ms) instead of 0.99988.
**Fix:** use standard `expf` (and `powf`) in `init_modal_modes` for
`modal_decay_1..6`. These are computed once at NoteOn time, so accuracy
dominates over the ~10 ns saved by the fast approximation.

## Root Cause of Marimba "1/3-Second Ring" Bug (fixed commit 859a2a4)

`master_env` in `NoteOn` was set to `sustain_level=0.0f`, causing it to
auto-decay from 1.0 → 0 at `decay_rate = master_rate × 0.3`. At default
Dkay (stored=25, t_s≈1.94 s), `decay_rate≈0.000446` → `ENV_IDLE` in ~323 ms.
The processBlock squelch `!is_releasing && master_env.state==ENV_IDLE` then
killed the voice at exactly the "1/3 second" the user heard, regardless of
modal T60 (Marimba mode-1 T60=1200 ms). Dkay=2000 didn't help because the
*release_rate* (not decay_rate) also cuts the voice in ~97 ms after gate-off.

**Fix:** For non-KS engines (including NOISE), `NoteOn` sets `sustain_level=1.0f`
so `master_env` holds at 1.0 and never auto-decays. `NoteOff` skips
`master_env.release()` for non-KS engines. Modal voices deactivate when
`mag_env < kSquelchThreshold` (ring naturally dies); NOISE voices deactivate when
`noise_env` reaches ENV_IDLE. See commits 859a2a4 and 94952f8.

## Root Cause of "Always String-Like" Sound

The KS waveguide produces a harmonic series (f, 2f, 3f…) regardless of modal preset.
The modal bank runs **in addition** to KS at a low mix (15–30%), so KS always
dominates. Fix: bypass KS for non-string presets; use the modal bank as the sole
tonal resonator.

## Engine Architecture (6-type redesign)

Each preset is assigned one `EngineType`. `processBlock` routes via a switch/if on
`kPresetEngine[m_preset_idx]`.

### Engine Types

| Engine | Signal path | Presets |
|--------|-------------|---------|
| `ENGINE_KS` | Karplus-Strong delay + modal additive | Init, 808Sub, Koto, GuitarStr, PluckBass |
| `ENGINE_BAR` | Mallet exciter → bar modal bank (ratios 1:2.756:5.404) | Marimba, Vibraphone, Kalimba, SteelPan, Woodblock, Claves, TubularBell, GlassBowl, GlassBottle, SlitDrum, Tick |
| `ENGINE_MEMBRANE` | Strike exciter → circular membrane modal bank (Bessel ratios 1:1.594:2.136:2.296) + boom osc | Timpani, Djembe, Taiko, AcTom, Conga, Bongo, Handpan, KickDrum |
| `ENGINE_SNARE` | Membrane body (short, no KS) + snare-wire resonators | AcSnare, MarchSnare |
| `ENGINE_PLATE` | Strike → dense inharmonic plate modes + metallic noise | Cymbal, Gong, HiHatClosed, HiHatOpen, Ride, RideBell, BellTree, Cowbell, Triangle |
| `ENGINE_NOISE` | Noise burst only; no pitched resonator | Clap, Shaker |
| `ENGINE_REMOVED` | Silent (preset reserved for future use) | Flute, Clarinet |

### Preset → Engine Mapping

```
k_Init(0)         ENGINE_KS
k_Marimba(1)      ENGINE_BAR      ← Phase 2 exemplar
k_808Sub(2)       ENGINE_KS
k_AcSnare(3)      ENGINE_SNARE
k_TubularBell(4)  ENGINE_BAR
k_Timpani(5)      ENGINE_MEMBRANE
k_Djambe(6)       ENGINE_MEMBRANE
k_Taiko(7)        ENGINE_MEMBRANE
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
k_Shaker(22)      ENGINE_NOISE
k_Flute(23)       ENGINE_REMOVED
k_Clarinet(24)    ENGINE_REMOVED
k_PluckBass(25)   ENGINE_KS
k_GlassBowl(26)   ENGINE_BAR
k_GuitarStr(27)   ENGINE_KS
k_HiHatClosed(28) ENGINE_PLATE
k_HiHatOpen(29)   ENGINE_PLATE
k_Conga(30)       ENGINE_MEMBRANE
k_Handpan(31)     ENGINE_MEMBRANE
k_BellTree(32)    ENGINE_PLATE
k_SlitDrum(33)    ENGINE_BAR
k_Ride(34)        ENGINE_PLATE
k_RideBell(35)    ENGINE_PLATE
k_Bongo(36)       ENGINE_MEMBRANE
k_GlassBottle(37) ENGINE_BAR
k_Tick(38)        ENGINE_BAR
```

## Implementation Phases

| Phase | Task | Effort | Risk | Validation |
|-------|------|--------|------|------------|
| 1 | Engine routing scaffold — KS bypass for non-string presets, ENGINE_REMOVED silence | Low | Low | All presets audible; strings unchanged |
| 2 | ENGINE_BAR — Marimba (kill-switch) | Medium | Low | Marimba sounds like a marimba, not a string |
| 3 | ENGINE_MEMBRANE — Kick, Timpani, Taiko, etc. | Medium | Medium | Kick thumps, Timpani rings inharmonically |
| 4 | ENGINE_SNARE — AcSnare, MarchSnare | Medium | Medium | Snare has sharp crack + sizzle, not ring |
| 5 | ENGINE_NOISE — Clap, Shaker | Low | Low | Clean noise bursts |
| 6 (optional) | ENGINE_PLATE — Cymbal, Gong, Hi-hat, etc. | High | High | Replace with samples if effort/result too high |
| 7 | Final tuning + binary size check | Low | Low | Size within budget, all engines pass HW test |

**Phase 2 is the kill-switch.** If Marimba does not sound convincingly like a
marimba after Phase 2, stop and re-evaluate the approach before continuing.

**ENGINE_PLATE is optional.** If effort/result ratio is too high, replace plate
presets with sampled content rather than synthetic DSP.

## ENGINE_PLATE: Noise-Ring Coupling (noise_ring_gate)

Metallic instruments need noise to decay *with* the ring, not independently.

`VoiceState::noise_ring_gate` (float, default 1.0) is reset to 1.0 on every NoteOn.
In `processBlock`, for `ENGINE_PLATE` with `modal_pilot_enabled`:
```cpp
parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
voice.noise_ring_gate *= voice.modal_decay_1;
```
`modal_decay_1` (the per-sample decay factor for mode 1) also decays the noise envelope.
The floor of 0.15 keeps a faint sustained noise bed (metallic shimmer) even after the
ring has mostly decayed. Without this, noise stays at full gain while the ring dies →
"juxtaposed" rather than integrated metallic sound.

## ENGINE_NOISE: Voice Lifetime

Clap and Shaker use `ENGINE_NOISE`. The voice lifecycle:
1. NoteOn: `master_env.trigger()`, `sustain_level=1.0f` → master_env holds at 1.0 forever.
2. NoteOn: `noise_env.trigger()` with short attack; `noise_env.release_rate` set by Rel.
3. NoteOff: **nothing** — no `master_env.release()` for NOISE engines.
4. `noise_env` decays from 1.0 → 0 under its own release; when it reaches ENV_IDLE the
   processBlock squelch fires and deactivates the voice.

The Rel formula: `release_rate = 0.00005 + (1.0 - norm) * 0.01`.  
At Rel=0: rate≈0.01 (~200 samples = 4 ms). At Rel=18 (norm=0.90): rate≈0.00105 (~200 ms).
At Rel=127 (norm=1.0): rate≈0.00005 (~4 s). Shipped Clap/Shaker at Rel=18.

## GOTCHA: Plate Ratios vs. Membrane Ratios

HiHatClosed, HiHatOpen, and Cowbell are ENGINE_PLATE. If their modal_preset_configs
use membrane Bessel ratios (1.000 / 1.594 / 2.136 / 2.296), they sound like a wood drum,
not metal. Plate ratios (2.92 / 6.37 / 11.75) produce the inharmonic metallic character.
Always use plate-appropriate ratios for PLATE presets.

## Modal Bank — Key Parameters

`modal_preset_configs[k_NumPrograms]` in `synth_engine.h` holds per-preset mode
ratios and T60 values. These are already calibrated and use inharmonic Bessel/bar
ratios. The table is `static` (not constexpr) per the .rodata rule above.

For **non-KS engines**, the modal bank is the **primary** tonal source (KS is
bypassed). The modal mix values (~0.18–0.32) were calibrated relative to KS, so
they must be scaled up by `kModalEngineGain ≈ 5.0` when running without KS.

## Dev Branch

`claude/continue-previous-session-vydFO` on `fedemone/logue-sdk`.

Always rebuild and check `arm-unknown-linux-gnueabihf-size ripplerx.elf`:
- `.text` (= text + .rodata) must stay below **28 KB** (safe margin below 30 KB limit).
- `.bss` must stay near **552 bytes**.

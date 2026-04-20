# FM Percussion Synth — Progress Report

## Current Direction

The project has been refocused from a 5-engine / voice-allocation instrument into a **fixed 4-engine percussion synth** with a clearer performance-oriented control model.

Current instrument identity:

- **4 fixed engines**: Kick, Snare, Metal, Perc
- **2 controls per engine**: Attack / Body
- **3 reclaimed globals**: HitShape / BodyTilt / Drive
- **Probability-triggered voices**
- **Euclidean tuning support**
- **ARM NEON first**
- **No heap allocation**

The resonant engine is **kept in code only** for the moment, but it is not part of the active performance path.

---

## What We Have Now

### 1) Control layer

#### `synth.h`
- rewritten as the top-level control / routing layer
- contains the new 24-parameter contract
- keeps:
  - `setParameter()`
  - preset loading
  - note handling
  - render entry point
- still needs the final wiring from the new global controls into the actual processing path

#### `header.c`
- moved to the fixed 4-engine naming model
- the intended UI parameter names are now:
  - `KProb`, `SProb`, `MProb`, `PProb`
  - `KAtk`, `KBody`, `SAtk`, `SBody`
  - `MAtk`, `MBody`, `PAtk`, `PBody`
  - `HitShp`, `BodyTilt`, `Drive`
- voice allocation has been removed from the UI budget

#### `engine_mapping.h`
- new control semantics layer
- maps UI parameters into macro targets such as:
  - excitation gain
  - attack click
  - attack brightness
  - pitch drop depth
  - FM index attack/body
  - noise amount
  - decay scale
  - stability
  - dynamic filter open
  - drive amount
  - ratio bias
  - variation bias
- this is now the main reference for the redesigned instrument behavior

---

### 2) DSP / voice files already present

- `kick_engine.h`
- `snare_engine.h`
- `metal_engine.h`
- `perc_engine.h`
- `resonant_synthesis.h`

These engines are still in the codebase, but their internal behavior is not yet fully aligned to the new parameter semantics.

---

### 3) Shared helper files present

- `envelope_rom.h`
- `lfo_enhanced.h`
- `lfo_smoothing.h`
- `fm_voices.h`
- `constants.h`
- `float_math.h`
- `prng.h` via the included engine files / synth layer

---

### 4) Runtime entry point

- `unit.cc`
  - still serves as the Drumlogue SDK bridge
  - routes parameter changes and rendering into `Synth`

---

### 5) Preset files present

- `fm_presets.h`
- `fm_presets.cc`

These still reflect the older architecture and must be revised to match the new 4-engine model.

---

## What Is Still Missing

### 1) Final wiring of the three global controls

The new controls:

- `HitShape`
- `BodyTilt`
- `Drive`

are defined in the control model, but they still need to be fully propagated into the actual render path.

The missing step is to make them influence:

- per-engine macro values
- transient shape
- body weight
- bus saturation / glue
- the final behavior of `fm_perc_synth_process`

---

### 2) Final `fm_perc_synth_process` integration

The processing function still needs to be completed so it:

- advances the envelope
- advances LFOs
- applies the mapping layer
- runs the active engines
- mixes them consistently
- applies global saturation / gain
- returns a mono sample

---

### 3) `setParameter()` final behavior

`setParameter()` now stores values, but it still needs the complete update path so that the new globals and engine mappings are reflected immediately in the processing state.

At the moment, the control model exists, but `HitShape`, `BodyTilt`, and `Drive` are not yet fully connected into the audio path.

---

### 4) Engine re-desing

complete new design of the `snare_engine.h`, `kick_engine.h`, `metal_engine.h`, `perc_engine.h` based on both original files and some ideas borrowed from these other open source projects:
- https://github.com/ryukau/UhhyouWebSynthesizers
- https://github.com/copych/ESP32-S3_FM_Drum_Synth?tab=readme-ov-file
- https://github.com/ngeiswei/deicsonze
- https://github.com/reales/retromulator (For the Yamaha FM synth but also for Nord Lead synth which has been said to have great drum synthesis)


---

### 5) Preset redesign

`fm_presets.h` and `fm_presets.cc` still need to be refactored away from:

- voice allocation
- resonant mode / morph focus
- old 5-engine thinking

and rebuilt around the new perceptual families:

- Tight
- Heavy
- Crunchy
- Metallic
- Dry
- Industrial
- Body-rich
- Digital

---

### 6) Legacy architecture cleanup

The older architecture still leaves behind references that should be removed or minimized:

- voice allocation as a user control
- resonant mode / resonant morph as active parameters
- old naming based on `Voice1/2/3/4`

The new naming should stay:
- `Kick`
- `Snare`
- `Metal`
- `Perc`

with the three reclaimed globals.

---

## What Is Already Decided

### Keep
- 4 fixed engines
- fixed voice positions
- probability triggering
- Euclidean tuning
- NEON-first implementation
- resonant engine in code only for now

### Remove from the active UI
- voice allocation control
- resonant mode control
- resonant morph control

### Add / keep as active controls
- `HitShape`
- `BodyTilt`
- `Drive`

---

## Next Steps

### Step 1
Wire `HitShape`, `BodyTilt`, and `Drive` from `setParameter()` into runtime mapping and audio processing.

### Step 2
Finish `fm_perc_synth_process` so it consumes the macro targets and returns a consistent mono output.

### Step 3
Rewrite `fm_presets.cc` and `fm_presets.h` around the new 4-engine behavior families.

### Step 4
Remove remaining voice-allocation UI logic and legacy resonant control assumptions from the active path.

### Step 5
Add consistency tests for:
- monotonic behavior of the new controls
- transient/body response
- preset coherence
- output stability

---

## Working Principle

The parameter model is now the design anchor.

The order of work is:

1. parameter semantics
2. mapping layer
3. engine behavior
4. process integration
5. presets
6. tests

That sequence keeps the instrument coherent under the 24-parameter limit.


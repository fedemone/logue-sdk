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

### Core control and architecture files

- `synth.h`
  - rewritten as the top-level control / routing layer
  - contains the new 24-parameter contract
  - keeps `setParameter()`, preset loading, note handling, and render entry points
  - still needs the final connection from the new global controls into the actual processing path

- `engine_mapping.h`
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
  - currently acts as the main design reference for the new instrument model

- `header.c`
  - new UI parameter layout is defined conceptually
  - needs to be aligned in the branch with the renamed controls:
    - `KProb`, `SProb`, `MProb`, `PProb`
    - `KAtk`, `KBody`, `SAtk`, `SBody`
    - `MAtk`, `MBody`, `PAtk`, `PBody`
    - `HitShp`, `BodyTilt`, `Drive`

### DSP / voice files already present

- `kick_engine.h`
- `snare_engine.h`
- `metal_engine.h`
- `perc_engine.h`
- `resonant_synthesis.h`

These engines are still in the codebase, but their internal behavior is not yet fully aligned to the new parameter semantics.

### Shared helper files present

- `envelope_rom.h`
- `lfo_enhanced.h`
- `lfo_smoothing.h`
- `fm_voices.h`
- `constants.h`
- `float_math.h`
- `prng.h` via the included engine files / synth layer

### Runtime entry point

- `unit.cc`
  - still serves as the Drumlogue SDK bridge
  - currently routes parameter changes and rendering into `Synth`

### Preset files present

- `fm_presets.h`
- `fm_presets.cc`

These still reflect the older architecture and must be revised to match the new 4-engine model.

---

## What Is Still Missing

### 1. Final wiring of the three global controls

The new controls:

- `HitShape`
- `BodyTilt`
- `Drive`

are defined in the control model, but they still need to be fully propagated into the actual render path in a consistent way.

The missing step is to make them influence:

- per-engine macro values
- transient shape
- body weight
- bus saturation / glue
- the final behavior of `fm_perc_synth_process`

### 2. Final `fm_perc_synth_process` integration

The processing function still needs to be completed so it:

- advances the envelope
- advances LFOs
- applies the mapping layer
- runs the active engines
- mixes them consistently
- applies global saturation / gain
- returns a mono sample

### 3. `setParameter()` final behavior

`setParameter()` now stores values, but it still needs the complete update path so that the new globals and engine mappings are reflected immediately in the processing state.

### 4. Preset redesign

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

### 5. Legacy architecture cleanup

The older architecture still leaves behind references that should be removed or minimized:

- voice allocation as a user control
- resonant mode / resonant morph as active parameters
- old naming based on `Voice1/2/3/4`

The new naming should be engine-based:

- Kick
- Snare
- Metal
- Perc

---

## File-Level Status

### Ready / mostly in place
- `engine_mapping.h`
- `synth.h` control-layer rewrite
- `kick_engine.h`
- `snare_engine.h`
- `metal_engine.h`
- `perc_engine.h`
- `unit.cc`
- `constants.h`
- `envelope_rom.h`
- `lfo_enhanced.h`
- `lfo_smoothing.h`
- `fm_voices.h`

### Needs alignment / rewrite
- `header.c`
- `fm_presets.h`
- `fm_presets.cc`
- final `fm_perc_synth_process` implementation
- note-on / tuning integration with the new fixed voice design

### Legacy / should be demoted
- voice allocation as a user-visible control
- resonant engine as part of the active instrument
- old preset logic built around the 5-engine structure

---

## Functional Gaps

### Present
- fixed 4-engine concept
- global macro mapping concept
- per-engine attack/body controls
- probability trigger path
- Euclidean tuning idea
- envelope and LFO infrastructure

### Missing
- the mapping values are not yet fully exercised in the process path
- the new globals still need to affect sound in a measurable way
- presets are still shaped around the previous design
- the final process function still needs to be completed and verified

---

## Next Steps

### Step 1
Finish `fm_perc_synth_process` so it consumes the new macro model.

### Step 2
Wire `HitShape`, `BodyTilt`, and `Drive` through `setParameter()` and into the runtime render logic.

### Step 3
Rewrite `header.c` to the fixed 4-engine naming and remove the voice allocation control.

### Step 4
Rewrite `fm_presets.h` / `fm_presets.cc` around curated perceptual families, not technical snapshots.

### Step 5
Add a test scaffold for the new mapping layer:
- monotonic behavior
- stability
- transient/body consistency
- preset coherence

### Step 6
Once the core model is stable, simplify or retire the legacy resonant path for the active instrument branch.

---

## Design Constraint Reminder

The current design must remain:

- static allocation only
- NEON-friendly
- deterministic
- low overhead
- playable with a very small parameter budget

This means the redesign should improve:
- punch
- body
- clarity
- performance

without adding new UI complexity.

---

## Current Decision Summary

The current working decision is:

- keep the active instrument at **4 engines**
- keep the resonant engine **in code but not called**
- remove **voice allocation** from the active parameter set
- rename controls to **Kick / Snare / Metal / Perc**
- use the 3 reclaimed controls as **HitShape / BodyTilt / Drive**
- finish the control-to-sound mapping before deeper engine edits

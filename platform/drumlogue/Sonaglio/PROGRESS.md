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
- `HitShape`, `BodyTilt`, and `Drive` remain stored in `params[]`
- those three globals are now intended to be read directly during processing

#### `header.c`
- moved to the fixed 4-engine naming model
- UI parameter names are now intended to be:
  - `KProb`, `SProb`, `MProb`, `PProb`
  - `KAtk`, `KBody`, `SAtk`, `SBody`
  - `MAtk`, `MBody`, `PAtk`, `PBody`
  - `HitShp`, `BodyTilt`, `Drive`
- voice allocation has been removed from the UI budget

#### `engine_mapping.h`
- simplified stateless mapping layer
- no nested macro cache
- `HitShape`, `BodyTilt`, and `Drive` are read from `params[]`
- provides helpers for:
  - transient shaping
  - body shaping
  - drive gain
  - soft clipping
  - direct parameter normalization

---

### 2) DSP / voice files

#### Rewritten
- `kick_engine.h`
- `snare_engine.h`
- `metal_engine.h`
- `perc_engine.h`

These now interpret their parameters as:

- **Param1 = Attack / Energy / Brightness**
- **Param2 = Body / Decay / Stability**

#### Still in code
- `resonant_synthesis.h`

This remains available but is not part of the active 4-engine instrument path.

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

### 5) Preset files

- `fm_presets.h`
- `fm_presets.cc`

These have been rewritten around the new 4-engine model and new parameter meaning.
The old resonant / voice-allocation data has been removed from the active preset model.

---

## What Is Still Missing

### 1) Final wiring of the three global controls

The new controls:

- `HitShape`
- `BodyTilt`
- `Drive`

must be fully consumed by the live render path.

The missing step is to make them influence:

- transient shape
- body weight
- final bus drive / soft clipping
- the final behavior of `fm_perc_synth_process`

### 2) Final `fm_perc_synth_process` integration

The processing function still needs to be finalized so it:

- advances the shared envelope
- advances / smooths the LFOs
- reads the mapped control values
- runs the active engines
- applies global transient/body/drive shaping
- returns the final mono output

### 3) Engine consistency pass

The four engines have been rewritten, but they still need a consistency pass after the control path is fully locked.

This pass should verify:
- the new control semantics are audible
- transient/body behavior is consistent across engines
- parameter ranges behave monotonically

### 4) Test update and execution

The current tests were written for the old model and need to be updated.

The new tests should validate:
- parameter monotonicity
- transient/body consistency
- safe output range / no NaNs
- preset-family coherence
- note-trigger consistency with probability gating and Euclidean tuning

### 5) Reference tuning pass

After the redesign stabilizes, the next useful step is to compare rendered results against a small set of reference families and tune the presets toward those behaviors.

Good reference sources can be:
- Nord Drum 2 factory / signature bank families
- other FM drum synth reference tones
- classic tuned percussion / synthetic drum reference sets

The goal is not exact cloning. The goal is to tune:
- attack hardness
- body weight
- decay character
- brightness / aggression
- preset family separation

---

## Next Steps

### Step 1 — Final wiring of globals
Wire `HitShape`, `BodyTilt`, and `Drive` into:
- `fm_perc_synth_process()`
- the final mix / saturation stage

### Step 2 — Keep engines stable while wiring
Do **not** redesign the engines again immediately.

The goal is to first confirm the control path:
- Parameters → Behavior → Processing

### Step 3 — Preset rewrite is done
The preset bank has been rebuilt around the new parameter meaning.

### Step 4 — Test suite update
Rewrite the tests around the new control model and run them.

### Step 5 — Reference tuning pass
Use curated reference families to tune the presets and validate the sonic direction.

---

## Design Rule for the Refactor

For a constrained system like this, the correct flow is:

> **Parameters → Behavior → Engine implementation**

That means parameter design must come first, then engine behavior must follow the mapped controls.

---

## Current Priority Order

1. Punch
2. Body
3. Aggression
4. Variety

Variety remains optional and should not weaken the core instrument identity.

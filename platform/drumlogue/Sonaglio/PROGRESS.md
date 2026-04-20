# FM Percussion Synth — Progress

## Current status

The project has moved from engine-by-engine tuning toward a control-model redesign.

The main decisions are now:

- keep **4 fixed engines**
- remove **voice allocation** from the user parameter set
- reuse freed parameter slots as global macro controls
- keep the resonant engine in the repository, but do not call it in the current instrument
- redesign parameter meaning before further DSP edits

---

## Confirmed design direction

### Core identity
This is a **4-voice FM percussion synth** for Drumlogue.

### Priority order
1. Punch
2. Body
3. Aggression
4. Variety

### Voice mapping
- Kick
- Snare
- Metal
- Perc

### Macro controls
- HitShape
- BodyTilt
- Drive

---

## What has been diagnosed

### Strong points
- NEON-friendly structure
- deterministic timing
- clean voice packing
- probability triggering works well
- snare architecture already has a convincing tone/noise split

### Weak points
- no shared excitation model
- weak transient impact
- weak low-mid body
- over-deterministic FM behavior
- parameter semantics are too technical
- presets are not yet curated around behavior

---

## What has been decided

### Keep in current project
- Kick, Snare, Metal, Perc
- fixed voice positions
- probability triggering
- Euclidean pitch distribution
- envelope ROM
- LFO support as secondary modulation

### Remove from current user model
- voice allocation parameter
- Voice1 / Voice2 / Voice3 / Voice4 naming

### Retain in code, but do not call
- resonant engine

---

## Improvement plan

### Tier 1 — immediate and low risk
- parameter remapping to perceptual controls
- shared envelope domains
- dynamic filtering
- mild saturation
- velocity as drive
- curated preset families
- envelope ROM expansion

### Tier 2 — structural
- per-engine transient/body separation
- controlled micro-instability during attack
- reduce reliance on LFO for core sound shaping

### Tier 3 — future
- separate resonant / modal percussion project sharing the same 4-voice runtime model

---

## Testing plan

The redesign must be validated with mapping-level tests before engine rewrites.

### Required checks
- parameter monotonicity
- transient behavior consistency
- body behavior consistency
- output stability
- preset coherence

### Test strategy
Use scalar mapping helpers to verify behavior first, then mirror the same equations in the NEON runtime.

---

## Next implementation steps

1. Add the parameter model as the source of truth.
2. Introduce the mapping layer in code.
3. Add mapping tests.
4. Rename user-facing controls to engine names.
5. Remove voice allocation from the active control surface.
6. Tune engine behavior to satisfy the mapping contract.
7. Rebuild presets around perceptual families.

---

## Deferred ideas

- new engines
- alternate resonant project
- more complex modulation topologies
- expanded algorithm families

These are valid future directions, but they are not the next step for this instrument.

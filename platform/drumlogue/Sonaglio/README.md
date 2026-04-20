# FM Percussion Synth — Drumlogue SDK

## Overview

FM Percussion Synth is a **4-voice, probability-triggered percussion instrument** for Korg Drumlogue.

The current design goal is to maximize:

- **Punch**
- **Body**
- **Aggression**
- **Performative control under a strict parameter budget**

This project is optimized for ARM NEON and static allocation. No heap allocation is used.

---

## Instrument identity

The current instrument is intentionally narrow:

- **4 fixed engines**
- **4 fixed voice positions**
- **2 parameters per engine**
- **3 global macro controls**
- **resonant engine retained in code but not called**

The instrument is not a general-purpose synth. It is a percussion instrument with a clear control surface.

---

## Engine layout

| Voice | Engine |
|------|--------|
| 0 | Kick |
| 1 | Snare |
| 2 | Metal |
| 3 | Perc |

Voice allocation is not exposed to the user. The fixed mapping improves predictability and frees control space for better sound design.

---

## Control surface

### Engine parameters
Each engine has two macro parameters:

- **Param1 = Attack / Energy / Brightness**
- **Param2 = Body / Decay / Stability**

This semantic contract is shared across all engines.

### Global macro controls
Three freed parameter slots are used as global controls:

- **HitShape** — transient hardness
- **BodyTilt** — low-mid weight
- **Drive** — nonlinear aggression

---

## Synthesis model

The engine family is FM-based, but the control model is designed around percussive behavior:

- transient
- body
- tail

Each engine internally maps the control surface to these behaviors in a different way, but the user-facing semantics remain stable.

---

## Envelope system

The envelope ROM is used as a shared shaping source.

Internally, the envelope is interpreted as:

- `attack_env` for transient energy
- `body_env` for tonal body
- `tail_env` for residual decay

This makes the instrument easier to steer toward punch and weight.

---

## Voice triggering

The instrument uses:
- per-voice probability triggering
- velocity
- Euclidean pitch distribution

Velocity is not just a level control. It also drives excitation strength and nonlinear behavior.

---

## Output behavior

The output chain is intentionally lightweight:

- per-engine spectral shaping
- fixed voice-to-engine mapping
- optional nonlinear drive
- final mix

This keeps the synth stable on the target hardware while improving punch and perceived loudness.

---

## Resonant engine status

A resonant engine exists in the codebase, but it is **not called in the current instrument**.

It is kept for:
- reference
- reuse
- a future second project with a different sonic identity

---

## Design priorities

The project is prioritized as:

1. Punch
2. Body
3. Aggression
4. Variety

Variety is useful, but it is secondary to playability and impact.

---

## Non-goals

- Dynamic voice allocation
- Heavy physical modeling
- Additional per-engine controls
- Heap allocation
- Runtime graph construction

---

## Project direction

The likely long-term structure is:

- this project: 4-engine FM percussion synth
- future project: resonant / tonal / modal percussion using the same Drumlogue framework

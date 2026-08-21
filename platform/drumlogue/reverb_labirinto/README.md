# NeonLabirinto – Labyrinthine Resonant Reverb for drumlogue

**NeonLabirinto** is a character-rich, highly optimized Feedback Delay Network (FDN) reverb for the KORG drumlogue. Moving far beyond transparent room simulation, it transforms ordinary sounds into immersive, evolving textures using physical material resonances, chaotic spatial routing, and exotic microtonal shimmering.

## Core Architecture

- **8-Channel FDN** mixed via a **Fully Vectorized Fast Walsh-Hadamard Transform (FWHT)**. This O(N log N) algorithm distributes energy perfectly across all 8 channels using zero multiplications, executing in a fraction of a microsecond.
- **Vectorized Interleaved Delay Line**: Stores all 8 FDN channels in a single time-aligned frame to enable `vld4q_f32` gathering—resulting in 3× faster delay line reads.
- **Active Partial Counting (APC)**: A CPU-saving algorithm that continuously monitors the decay envelope. When the reverb tail drops below -100 dBFS, the heavy FDN calculations are instantly bypassed while preserving dry signal flow.
- **Tape-Style Interpolated Pre-Delay**: A 1-pole slew limiter wraps the pre-delay read head. Adjusting the pre-delay acts like physically speeding up or slowing down magnetic tape, bending the pitch seamlessly without zipper noise or clicks, while linear interpolation provides natural tape-head high-frequency damping. The FDN delay lengths are slewed the same way, so changing the ping-pong bounce time glides instead of clicking.

## The DSP Features

### 1. Material Body Resonance (Double Filters)
Instead of standard 1-pole high-frequency damping, NeonLabirinto utilizes true 2nd-order Direct-Form II Transposed Biquad filters inside the feedback loop to emulate the physical body resonance of different acoustic materials:
* **Wood** (Preset 0 *foresta*): Warm, highly-damped low-mid resonance.
* **Stone** (Preset 1 *tempio*): Dark, heavy, and highly reflective.
* **Metal** (Preset 2 *labirinto*): Glassy, inharmonic ringing with high-frequency retention.
* **Crystal** (Preset 3 *esotico*): Bright allpass-like shimmer; medium-Q bandpass for microtonal shimmer character.
* **Noise** (Preset 4 *stellare*): Internal noise generator acting as the acoustic resonator source.

The material filter is selected automatically when loading a preset; it cannot be changed independently of preset selection.

Every colour stage is normalised to unity broadband gain, so the material changes the *timbre* of the tail without changing how long it lasts — TIME means the same thing in all five modes.

### 2. Frequency-Dependent Decay
TIME sets a real mid-band RT60. Inside the feedback loop the signal is split at the **DAMP** crossover into a low and a high band, each with its own feedback gain, so **LOW** and **HIGH** lengthen or shorten their band relative to the mid. Both are neutral at their centre position (50).

### 3. True Ping-Pong Routing (PILL = 1)
The 8 channels are split into a left bank (0–3) and a right bank (4–7). Each bank is mixed with its own orthonormal 4-point Hadamard and then written into the **opposite** bank's delay lines, so energy physically crosses the stereo field once per bounce and returns one bounce later. The input is injected into the left bank only, so the first echo is hard left and the tail alternates from there.

**BNCE** sets the bounce time directly in milliseconds (60–500 ms). A small bank-to-bank bleed keeps the quiet side from dropping out entirely between bounces; it is scaled with BNCE so the leakage per second stays constant and a fast bounce reads as crisply as a slow one.

**SYNC** locks the bounce to the host tempo instead: 1/16, 1/8T, 1/8, 1/8. or 1/4. On a drum machine this is usually what you want — a bounce at a note division is part of the pattern, a bounce at some millisecond value drifts against it. Dotted eighth is the classic ping-pong delay setting, landing off the beat and pulling against the groove. While SYNC is on BNCE is ignored, and turning SYNC back off restores it. Changing the bounce time glides rather than clicking, so tempo changes bend the tail like tape.

A division longer than the delay lines can hold simply clamps at 500 ms — a quarter note at 60 BPM would be 1000 ms — so the bounce stops tracking rather than folding to something arbitrary. All five presets ship with SYNC off, so a preset sounds the same whatever the project tempo is; locking to the transport is opt-in.

### 4. Coloured Noise Injection
When the filter mode is set to **Noise** (*stellare* preset), the reverb acts as an acoustic resonator for an internal pseudo-random noise generator. The noise color sweeps smoothly from deep Brown, through Pink and Grey (notched), up to harsh Violet. Use **DFSN** (diffusion) to shape the noise density and **DAMP** to control the noise injection gain.

### 5. Cochrane 18-EDO Microtonal Shimmer
The *esotico* preset subjects the 8 delay lines to deep, independent Doppler pitch-shifts locked to an 18-EDO (Equal Division of the Octave) microtonal scale. When these echoes collide in the Hadamard matrix, they generate massive, non-Western acoustic beating and dense harmonic interference. Both *esotico* and *stellare* additionally use PILL = 4, which ring-modulates a copy of the wet signal back into the last two channels; **SHMR** sets that modulation frequency (3–55 Hz). SHMR has no effect in the other routing modes.

### 6. Output Ceiling
This is a send effect, so the wet output passes through a soft limiter with a hard knee at 0.8: exactly transparent below that level, gently compressing above it, and bounded at ~0.93 whatever TIME, DAMP and DFSN are set to.

## Parameter Guide

NeonLabirinto has **13 parameters** across 4 pages.

### Page 1: Main Controls

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 0 | Preset | 0–4 | Loads a factory preset (see Presets section) |
| 1 | TIME | 1–100 | Mid-frequency RT60, roughly 0.5 s to 8 s (exponential) |
| 2 | LOW | 1–100 | Low-frequency RT60 multiplier, 0.51–1.50 (neutral at 50) |
| 3 | HIGH | 1–100 | High-frequency RT60 multiplier, 0.41–1.60 (neutral at 50) |

### Page 2: Advanced Controls

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 4 | DAMP | 20–1000 | Crossover between the LOW and HIGH decay bands (×10 → 200–10000 Hz) |
| 5 | WIDE | 0–200% | Stereo width of the reverb tail (no effect at PILL=0, which folds to mono) |
| 6 | DFSN | 0–100% | Diffusion / complexity — depth of the delay-line modulation |
| 7 | PILL | 0–4 | Routing macro: 0=sparse(2ch), 1=ping-pong, 2=stone(6ch), 3=full(8ch), 4=shimmer |

### Page 3: Routing & Shimmer

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 8 | SHMR | 0–100 | Shimmer frequency, 3–55 Hz (PILL=4 only) |
| 9 | PDLY | 0–200 ms | Slew-limited pre-delay (tape-style interpolation avoids zipper noise) |
| 10 | VIBR | 1–30 | LFO speed for random diffusion matrix modulation (×0.1 → 0.1–3.0 Hz) |
| 11 | BNCE | 60–500 ms | Ping-pong bounce time (PILL=1 only, ignored while SYNC is on) |

### Page 4: Tempo

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 12 | SYNC | OFF, 1/16, 1/8T, 1/8, 1/8., 1/4 | Locks the bounce to a note division of the host tempo instead of BNCE |

## Factory Presets

| # | Name | Filter | PILL | Character |
|---|------|--------|------|-----------|
| 0 | foresta | Wood | 3 (full) | Warm, mellow room; short decay, moderate diffusion |
| 1 | tempio | Stone | 2 (stone) | Dark, heavy; long lows, tight highs, wide stereo |
| 2 | labirinto | Metal | 1 (ping-pong) | Glassy tail bouncing between the speakers every 190 ms |
| 3 | esotico | Crystal | 4 (shimmer) | Microtonal shimmer; bright, exotic, non-Western character |
| 4 | stellare | Noise | 4 (shimmer) | Long, spacey; noise-seeded reverb with deep shimmer tail |

> **Note:** `num_presets` is 0 in `header.c`, so the presets are not exposed through the SDK's preset-recall slots. All five are selected with the **Preset** parameter (ID 0) instead.

## Technical & Build Notes

- **Scalar vs. Vector Segregation:** While 90% of the DSP (delay reading/writing, mixdown, modulation) runs in parallel via ARM NEON intrinsics, Infinite Impulse Response (IIR) states like the material biquads and noise filters are calculated channel-parallel via a transpose to avoid NEON comb-filtering artifacts.
- **One DSP path:** The engine exists only in its 4-wide form. The SDK permits render calls shorter than `frames_per_buffer`, so a frame count that is not a multiple of 4 is zero-padded to a full block and the write heads are then wound back to advance by exactly the frames requested — the surplus is overwritten by the next call, and the reverb cannot run fast. There is deliberately no second, scalar implementation of the network to drift out of step with this one.
- **Branchless DSP:** Buffer wrapping and phase accumulations utilize float/bitwise arithmetic rather than `while` loops, completely eliminating Cortex-A7 branch-prediction pipeline stalls.
- **Denormal Safety:** The engine forces `Flush-to-Zero` and `Default-NaN` in the ARM FPSCR to guarantee CPU usage never spikes when the reverb tail decays into subnormal values.
- **Stability:** Both feedback matrices are orthonormal, and the per-pass gain is derived from the requested RT60 divided by everything else in the loop that is not unity (cross-feedback, resonant colour), capped so the round trip never exceeds 0.985.
- **Delay memory:** The ring buffer is 2¹⁵ frames × 8 channels = 1 MB, sized against the longest delay the engine can request (one ping-pong bank at BNCE 500 ms × 1.147 ≈ 0.57 s) with room for the modulation. A `static_assert` holds the two in step, so the buffer cannot be shrunk below what BNCE can reach without the build failing.
- **Nothing steps.** A knob is a staircase — the drumlogue sends one value per encoder detent — so every quantity that lands on live signal is glided once per block rather than assigned: the injection and feedback gains, the stereo width, the wet trim, the filter cutoff, the band-split crossover, the modulation depth, the cross-feedback amount and the per-channel mixdown weights. The things that switch rather than move are crossfaded over the same ~50 ms: the two feedback matrices, the input injection pattern, the cross-feedback wiring, and the whole material colour stage, which runs the outgoing filter alongside the incoming one for the duration. Delay lengths get a rate limit on top of their glide, capping the read pointer at half speed to one-and-a-half so a large move bends like tape instead of stalling and running backwards.
- **Except when nothing is playing.** All of that exists to avoid bending audio that is already sounding. On the first block after silence there is none, so the engine snaps every glide to its target — a preset chosen while the pattern is stopped is fully in force on the first hit, rather than sliding into place underneath it.

## Building for drumlogue

Place `NeonAdvancedLabirinto.h`, `unit.cc`, and `header.c` in your SDK project. Ensure `float_math.h` is available. Compile with `-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` (or appropriate for drumlogue's ARM processor).

## Tests

`test/` builds the real engine header on the development machine — no cross toolchain, no hardware — by putting a scalar stand-in for `<arm_neon.h>` and `float_math.h` ahead of the SDK ones on the include path:

```sh
cd test && make
```

It checks that both feedback matrices are energy-preserving, that the limiter is transparent below its knee and bounded above it, that RT60 tracks TIME, that PILL=1 produces a periodic left/right bounce at the period BNCE asks for (and that the diffuse modes do not), and that no preset can diverge or exceed the output ceiling at extreme settings.

It also pins four things that had drifted from what the panel claims: that VIBR delivers the rate in Hz that it advertises, that the output does not depend on the phase of the internal filter-coefficient update cycle (nothing may ride along on that flag), that PILL and DFSN commute — setting them in either order must leave the same modulation depth, shimmer gain and modulation rate — and that the reverb does not depend on the host's block size, checked by rendering every preset one frame per call and comparing the energy against whole buffers.

For SYNC it checks that each note division actually bounces at its note length, that the tempo moves the bounce when locked and cannot reach it when not, that SYNC and the tempo may arrive in either order, that a division past the buffer clamps, and that BNCE goes inert while synced and resumes afterwards.

Finally it hunts for clicks. A steady tone keeps the tail alive, a control is moved mid-ring, and the worst |y[n] − 2y[n−1] + y[n−2]| in the 400 ms that follow is compared against the level of the tail it lands on — a click is a step, and that is what the second difference is largest on. Every knob and all twenty preset transitions must stay near the 0.1% roughness the engine shows when nobody touches anything; before the de-clicking work these read 12% for TIME, 9% for PDLY, 28% for the Preset knob off *labirinto* and 167% for *stellare* to *esotico*. Two more checks cover the mechanism rather than the symptom: that no delay length ever moves faster than the rate cap (which is what keeps the read pointer from reversing), and that a preset chosen while the engine is silent is fully in force on the first block that carries signal.

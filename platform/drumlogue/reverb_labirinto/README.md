# NeonLabirinto – Labyrinthine Resonant Reverb for drumlogue

**NeonLabirinto** is a character-rich, highly optimized Feedback Delay Network (FDN) reverb for the KORG drumlogue. Moving far beyond transparent room simulation, it transforms ordinary sounds into immersive, evolving textures using physical material resonances, chaotic spatial routing, and exotic microtonal shimmering.

## Core Architecture

- **8-Channel FDN** mixed via a **Fully Vectorized Fast Walsh-Hadamard Transform (FWHT)**. This O(N log N) algorithm distributes energy perfectly across all 8 channels using zero multiplications, executing in a fraction of a microsecond.
- **Vectorized Interleaved Delay Line**: Stores all 8 FDN channels in a single time-aligned frame to enable `vld4q_f32` gathering—resulting in 3× faster delay line reads.
- **Active Partial Counting (APC)**: A CPU-saving algorithm that continuously monitors the decay envelope. When the reverb tail drops below -100 dBFS, the heavy FDN calculations are instantly bypassed while preserving dry signal flow.
- **Tape-Style Interpolated Pre-Delay**: A 1-pole slew limiter wraps the pre-delay read head. Adjusting the pre-delay acts like physically speeding up or slowing down magnetic tape, bending the pitch seamlessly without zipper noise or clicks, while linear interpolation provides natural tape-head high-frequency damping.

## The DSP Features

### 1. Material Body Resonance (Double Filters)
Instead of standard 1-pole high-frequency damping, NeonLabirinto utilizes true 2nd-order Direct-Form II Transposed Biquad filters inside the feedback loop to emulate the physical body resonance of different acoustic materials. To prevent SIMD comb-filtering artifacts, these operate in mathematically perfect scalar loops:
* **Wood:** Warm, highly-damped low-mid resonance.
* **Stone:** Dark, heavy, and highly reflective.
* **Metal:** Glassy, inharmonic ringing with high-frequency retention.

### 2. Coloured Noise Injection
When the filter mode is set to **Noise**, the reverb acts as an acoustic resonator for an internal pseudo-random noise generator. The noise color sweeps smoothly from deep Brown, through Pink and Grey (notched), up to harsh Violet.

### 3. Cochrane 18-EDO Microtonal Shimmer (PILL = 4)
Instead of relying on synthetic ring modulation, the *Stellare* mode implements a genuine **Cochrane Shimmer**. The 8 delay lines are subjected to deep, independent Doppler pitch-shifts locked to an 18-EDO (Equal Division of the Octave) microtonal scale. When these echoes collide in the Hadamard matrix, they generate massive, non-Western acoustic beating and dense harmonic interference.

## Parameter Guide

* **PILL (Pillar):** Selects the routing macro. Lower values are standard spaces; Pillar 4 engages the 18-EDO Cochrane Shimmer.
* **MATR (Material):** Selects the acoustic filter (Wood, Stone, Metal, or Noise).
* **TIME:** The physical size of the delay network (up to 2.0 seconds).
* **DAMP:** Controls the feedback coefficient (decay time).
* **COMP:** Shifts the cutoff frequency of the physical material filters.
* **PDLY:** Slew-limited pre-delay time (0 to ~340ms).
* **SHMR:** In *Stellare* mode, controls the depth of the 18-EDO microtonal Doppler stretching.
* **VIBR:** Controls the speed of the Xorshift LFO. Higher speeds cause faster random fluctuations in the room's diffusion matrix.
* **Noise Mode (Preset 3):** Use **COMP** to sweep seamlessly through the noise colors. Use **DAMP** to control the overall noise injection gain.

## Technical & Build Notes

- **Scalar vs. Vector Segregation:** While 90% of the DSP (delay reading/writing, mixdown, modulation) runs in parallel via ARM NEON intrinsics, Infinite Impulse Response (IIR) states like the material biquads and noise filters are calculated via optimized scalar loops to prevent NEON comb-filtering artifacts.
- **Branchless DSP:** Buffer wrapping and phase accumulations utilize float/bitwise arithmetic rather than `while` loops, completely eliminating Cortex-A7 branch-prediction pipeline stalls.
- **Denormal Safety:** The engine forces `Flush-to-Zero` and `Default-NaN` in the ARM FPSCR to guarantee CPU usage never spikes when the reverb tail decays into subnormal values.

## Building for drumlogue

Place `NeonAdvancedLabirinto.h`, `unit.cc`, and `header.c` in your SDK project. Ensure `float_math.h` is available. Compile with `-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` (or appropriate for drumlogue’s ARM processor).
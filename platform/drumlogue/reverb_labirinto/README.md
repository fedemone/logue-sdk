# NeonLabirinto – Labyrinthine Resonant Reverb for drumlogue

**NeonLabirinto** is a character-rich, NEON-optimized Feedback Delay Network (FDN) reverb for the KORG drumlogue. Moving far beyond transparent room simulation, it transforms ordinary sounds into immersive, evolving textures using physical material resonances, chaotic spatial routing, and exotic undertone shimmering.

## Core Architecture

- **8-Channel FDN** with a Hadamard mixing matrix for perfectly balanced, mathematical energy distribution.
- **Vectorized Interleaved Delay Line**: Stores all 8 FDN channels in a single time-aligned frame to enable `vld4q_f32` gathering—resulting in 3× faster delay line reads.
- **Active Partial Counting (APC)**: A CPU-saving algorithm that continuously monitors the decay envelope. When the reverb tail drops below -100 dBFS, the FDN matrix calculation is instantly bypassed while preserving dry signal flow and preventing DC offsets.

## The DSP Features

### 1. Material Body Resonance (Double Filters)
Instead of standard 1-pole high-frequency damping, NeonLabirinto utilizes true 2nd-order Biquad filters (`filterState1` / `filterState2`) inside the feedback loop to emulate the physical body resonance of different acoustic materials:
* **Wood:** Warm, highly-damped low-mid resonance.
* **Stone:** Dark, heavy, and highly reflective.
* **Metal:** Glassy, inharmonic ringing with high-frequency retention.

### 2. Coloured Noise Injection
When the filter mode is set to **Noise**, the reverb injects spectrally shaped noise directly into the delay lines, smearing transients into lush, synthetic tails. The color is shapeable:
* **Brown / Pink:** Warm, low-frequency weighted rumble.
* **Grey:** Psychoacoustically notched for a transparent, ethereal sizzle.
* **Blue / Violet:** High-frequency weighted, icy digital breath.

### 3. Labyrinthine Ping-Pong & Random LFO
By mapping the spatial assignment to a 16-step `pingRandomMap` and driving it with a fast, real-time Xorshift LFO, the reverb creates a chaotic, non-repeating "Labyrinth" effect.
* Echoes bounce irregularly between the left and right channels, preventing the predictable "metronome" effect of standard ping-pong delays.
* A pseudo-random LFO modulates the FDN diffusion coefficients, causing the room size to "breathe" dynamically.

### 4. Exotic Shimmer (Frequency-Shifted Feedback)
Setting the pillar count to 4 (**stellare**) engages an exotic, low-frequency ring-modulated shimmer inside the feedback network.
* *Literature Reference:* This technique is rooted in **Frequency-Shifted Feedback (FSF)** networks, originally pioneered by Harald Bode and later adapted for reverberation by Jon Dattorro (*"Effect Design Part 1: Reverberator and Other Filters"*, J. Audio Eng. Soc., 1997). Microntonal beating idea from Rich Cochrane's **Double Modes and Microtonal Shimmer** , refer to https://cochranemusic.com/node/340
* By applying amplitude/ring modulation *inside* the recursive loop, it generates cascading, sub-harmonic undertones that bloom continuously as the reverb decays, rather than the standard pitch-shifted "upward" shimmer.

## Character Enhancements

| Feature | Description |
|---------|-------------|
| **Resonant Filters** | Four presets select different filter characters: **Wood** (warm low‑mid resonance), **Stone** (dark, heavy), **Metal** (glassy, ringing), **Noise** (coloured noise injection). |
| **Cross‑channel Feedback** | Creates chaotic, irregular echoes. Strength and routing depend on **PILL** count – fewer pillars = more chaos. |
| **Randomised Ping‑pong** | For PILL=1, stereo assignment of the four active channels follows a pseudo‑random cyclic map – unpredictable, labyrinthine motion. |
| **Modulated Diffusion** | Diffusion coefficient is modulated by a pseudo‑random LFO. Modulation depth is **inversely proportional** to pillar count (counter‑intuitive, more characterful). |
| **VIBR (ID 11)** | Controls the speed of the random LFO (0.1–3 Hz). Creates slowly evolving, organic shifts in the reverb tail. |
| **Coloured Noise** (Preset 3) | Instead of a resonant filter, injects brown, pink, grey, blue, or violet noise. Colour selected by **COMP** (ID 7), quantity by **DAMP** (ID 5). |

## Parameters

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 0 | Preset | 0–3 | foresta / tempio / labirinto / stellare |
| 1 | MIX | 0–100% | Wet/dry blend |
| 2 | TIME | 0.1–10.0 s | Mid‑frequency RT60 |
| 3 | LOW | 0.1–10.0 s | Low‑frequency RT60 multiplier |
| 4 | HIGH | 0.1–10.0 s | High‑frequency RT60 multiplier |
| 5 | DAMP | 200–10000 Hz | Damping crossover (also noise quantity for preset 3) |
| 6 | WIDE | 0–200% | Stereo width |
| 7 | COMP | 0–100% | Diffusion / complexity (also noise colour for preset 3) |
| 8 | PILL | 0–4 | Routing mode: sparse(2ch), ping‑pong(4ch), stone(6ch), full(8ch), shimmer(8ch+ring‑mod) |
| 9 | PL4FRQ | 3–55 Hz | Shimmer frequency (microtonal low pitch) |
| 10 | PDLY | 0–340 ms | Pre‑delay time |
| 11 | VIBR | 0.1–3.0 Hz | Random LFO speed (affects diffusion modulation) |

## Presets

| Name | Character |
|------|-----------|
| **foresta** | Warm, woody, moderate decay – resonant wood filter |
| **tempio** | Dark, heavy, long decay – resonant stone filter |
| **labirinto** | Metallic, ringing, ping‑pong with randomised stereo – resonant metal filter |
| **esotico** | Metallic, gamelan like chorus effect – resonant metal filter |
| **stellare** | Shimmer + coloured noise (brown/pink/grey/blue/violet selectable via COMP) |

---

## User Guide & Parameter Mapping

* **PRESET = foresta:** A natural, lush room. Turn up **COMP** to increase FDN diffusion (smearing). Increase **TIME** for massive, blooming tails.
* **PRESET = labirinto:** Engages the chaotic LFO spatial routing. The randomized ping-pong will throw echoes around the stereo field in unpredictable paths.
* **PILL = 4 (stellare):** Activates the exotic FSF Shimmer. Adjust **SHMR** to tune the ring-modulation rate. Low values create a haunting, descending abyss of undertones.
* **VIBR:** Controls the speed of the Xorshift LFO. Higher speeds cause faster random fluctuations in the room's diffusion matrix.
* **Noise Mode (Preset 3):** Use **COMP** to sweep seamlessly through the noise colors (Brown → Pink → Grey → Blue → Violet). Use **DAMP** to control the overall noise injection gain.

## Technical & Build Notes

- **Scalar vs. Vector Segregation:** While 90% of the DSP (mixing, delays, modulation) runs in parallel via ARM NEON intrinsics, Infinite Impulse Response (IIR) states like the material biquads and noise filters are calculated via optimized scalar loops to prevent NEON comb-filtering artifacts.
- **Cross-Channel Feedback:** FDN cross-feedback gains are kept deliberately small (`< 0.15`) and are strictly clamped by the global decay (`< 0.995`) to guarantee absolute mathematical stability without DAC clipping.
- **Building for drumlogue:** Place `NeonAdvancedLabirinto.h`, `unit.cc`, and `header.c` in your SDK project directory. Ensure `float_math.h` is available. Compile using `make` with the standard Korg ARMv7 toolchain.
## Building for drumlogue

Place `NeonAdvancedLabirinto.h`, `unit.cc`, and `header.c` in your SDK project. Ensure `float_math.h` is available. Compile with `-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` (or appropriate for drumlogue’s ARM processor).

---

*NeonLabirinto – a maze of resonant reflections.*
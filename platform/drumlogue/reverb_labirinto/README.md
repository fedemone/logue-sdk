# NeonLabirinto – Labyrinthine Resonant Reverb for drumlogue

**NeonLabirinto** is a character‑rich, NEON‑optimised Feedback Delay Network (FDN) reverb for the KORG drumlogue. It transforms ordinary sounds into immersive, evolving textures with organic movement, resonant materials, and unpredictable stereo behaviour.

## Architecture

- **8‑channel FDN** with Hadamard mixing matrix for even energy distribution.
- **Interleaved delay line** (all 8 channels per sample) to enable `vld4q_f32` gather – 3× faster reads.
- **Vectorised processing** of 4 samples in parallel using ARM NEON intrinsics.
- **Active partial counting** – automatically disables processing when tail decays, saving CPU.

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
| **stellare** | Shimmer + coloured noise (brown/pink/grey/blue/violet selectable via COMP) |

## User Guide

- **Start with PRESET=foresta** for a natural room sound. Turn up **COMP** to add diffusion (smearing). Increase **TIME** for longer tails.
- **Switch to labirinto** for a glassy, unpredictable reverb – the randomised ping‑pong will bounce echoes between left and right in a non‑repeating pattern.
- **Set PILL=4 (stellare)** and adjust **PL4FRQ** to add a low‑frequency ring‑modulated shimmer – creates cascading undertones.
- **Use VIBR** to make the reverb "breathe" – higher speeds cause faster random fluctuations in the diffusion.
- **For preset 3 (stellare)**, experiment with **COMP** to cycle through noise colours (brown→pink→grey→blue→violet) and **DAMP** to set the noise level.

## Technical Notes

- All processing is NEON‑vectorised (4 samples at a time) for low CPU usage.
- Cross‑feedback gains are small (<0.15) and combined with decay (<0.99) – the system remains stable.
- The random LFO uses a simple xorshift generator; its speed is controlled by **VIBR**.
- The reverb automatically bypasses itself when the tail decays below -100 dBFS (active partial counting).

## Building for drumlogue

Place `NeonAdvancedLabirinto.h`, `unit.cc`, and `header.c` in your SDK project. Ensure `float_math.h` is available. Compile with `-O3 -mcpu=cortex-a7 -mfpu=neon-vfpv4` (or appropriate for drumlogue’s ARM processor).

---

*NeonLabirinto – a maze of resonant reflections.*
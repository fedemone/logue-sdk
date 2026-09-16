# ScrutaAstri

> **Disclaimer:** ScrutaAstri is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

A Korg drumlogue port of the Moffenzeef Stargazer drone synthesizer, supercharged with audio-rate modulations, morphing filters, and a chaotic Polivoks emulation.

## Overview
ScrutaAstri (Italian for "Stargazer") transforms the Korg drumlogue into a continuous, evolving drone machine. It replicates the core architecture of the original hardware—utilizing wavetable synthesis, dual resonant filters, and aggressive LoFi distortion—while extending its capabilities with Drumlogue-exclusive features like audio-rate LFOs, bidirectional wavetable scanning, and an extensive modulation matrix.

## The Authentic Signal Path
By analyzing the original Teensy Audio patches from the hardware's source code, ScrutaAstri replicates the exact "Crush Sandwich" routing of the Stargazer:
1. **Linear Detuned Oscillators:** Osc 1 and Osc 2 are mixed. Osc 2 detunes linearly (+/- 5Hz) for constant-speed phase beating.
2. **Filter 1 (Pre-Crush):** Lowpass SVF, modulated by LFO 1.
3. **The Crush Sandwich:** Sample Rate Reduction (SRR) and Bit Rate Reduction (BRR) process the filtered signal.
4. **Filter 2 (Post-Crush):** Lowpass SVF, modulated by LFO 2.
5. **Master VCA:** LFO 3 acts as a Tremolo (DC offset + LFO) on the final output.
6. **CMOS Distortion:** Emulated with an extreme mathematical soft-clipper.

## Drumlogue-Exclusive Enhancements
* **The Morphing Filter:** The `CMOSDist` parameter morphs Filter 1 smoothly from a Clean SVF (0-33) -> Moog-style symmetrical saturation (34-65) -> Sherman Filterbank asymmetrical chaos (66-100).
* **Polivoks Chaos:** A secondary filter model aggressively replicates the Formanta Polivoks topology, utilizing non-linear mathematical saturation inside the resonance feedback loop and integrators to achieve its signature unstable howling.
* **Audio-Rate Wavetable Asymmetry:** In the Sherman territory, the raw shape of Wavetable 1 is injected into Filter 1 at audio rates (48kHz), physically ripping the filter's DC offset apart based on the audio source.
* **Bidirectional Wavetable Scanning:** Wavetable playback direction is dictated by the active preset program range.
  * `0-23`: Forward scanning.
  * `24-47`: Oscillator 1 plays in reverse.
  * `48-71`: Oscillator 2 plays in reverse.
  * `72-95`: Both oscillators play in reverse.
* **Exponential LFOs:** LFO rates are exponentially mapped from glacial 100-second cycles (0.01Hz) up to audio-rate FM screams (1000Hz).
* **Percussive Shapes:** Three of the eleven LFO shapes are envelopes rather than waveforms — see [LFO Shapes](#lfo-shapes).
* **Per-Oscillator AM:** Presets `12` and `15` (and their modulo-24 equivalents) ring LFO 1 into Osc 1's amplitude and LFO 2 into Osc 2's — see [Amplitude Modulation](#amplitude-modulation-presets-12-and-15).

## LFO Shapes

`L1Wave` / `L2Wave` / `L3Wave` select one of eleven shapes:

| # | Shape | Notes |
|---|-------|-------|
| 0 | Triangle | |
| 1 | Saw down | |
| 2 | Saw up | |
| 3 | Square | |
| 4 | Random (S&H) | |
| 5 | Exp decay | Decays across the **whole** cycle, so it never reaches silence |
| 6 | Sine | |
| 7 | Pulse 25% | |
| 8 | Smooth random | |
| 9 | **AR strike** | Attack by 2% of the cycle, cubic decay done by 30%, **silent for the remaining 70%** |
| 10 | **ADSR staccato** | Attack 4%, decay to a 0.5 sustain by 16%, sustain to 55%, release to zero by 75%, **silent for the last 25%** |

Shapes 9 and 10 are the rhythmic ones. Shape 5 decays but never rests, so repeating it reads as a wobble; 9 and 10 go to zero and stay there, and that gap is what makes a repeat read as a beat.

Their segments are fractions of the cycle rather than fixed milliseconds, so one shape covers the whole exponential rate range: at 4 Hz the AR strike is a 75 ms click, at 0.5 Hz it is a swell, at audio rate it becomes an AM waveform in its own right. Existing shape numbers are unchanged, so saved patches keep the shape they were written with.

## The Drone / Sequencer Concept
Unlike typical drumlogue synthesizers, ScrutaAstri operates in **Infinite Sustain** mode. The audio thread continuously calculates and outputs the drone. The step sequencer is primarily used for **Motion Sequencing**—automating LFO rates, filters, and distortion parameters per step while the drone screams continuously in the background.

## Hardware Bug Fixes Applied

| Bug | Symptom | Fix |
|-----|---------|-----|
| CMOS dead zone | `CMOSDist` values 1–32 produced no output change — the SVF `f` coefficient mapped to a range that left the filter frozen | Dead zone removed; coefficient mapping now covers the full meaningful range continuously from 0 through to Sherman chaos territory |
| LFO always modulating filter | Filter 1 was being modulated by LFO 1 every render call regardless of the LFO target assignment | LFO application is now gated on target — only applied when LFO target is `Filter1` or equivalent; LFO3 (Master VCA tremolo) remains unconditional as designed |
| Volume knob dead above 33 | `Volume` mapped to `value/100 × 3`, feeding `m_master_vol = clamp(base + mod, 0, 1)`. Anything from 34 to 100 gave the same level, and at the default of 80 the base sat at 2.4 — so the LFO 3 master-VCA tremolo (`\|mod\| ≤ 1`, added to the base) could never pull it back under the clamp and did nothing | Mapped linearly to 0..1. The knob now works over its whole travel and the tremolo is audible again |
| Output hard-clipped | The double tanh is already bounded to ±0.9, and `Output_Gain_Boost` drove it into a `±1.0` safety clamp: 23 of 24 programs measured **0.00 dBFS** with a crest factor of 2.6–3.6 dB — a square wave | The clamp is replaced by the soft knee from [`common/output_stage.h`](../common/output_stage.h), which is bounded by 0.995 for any finite input. Peaks are now −0.97…−1.53 dBFS at crest 3.2–4.3 dB; nothing flat-tops |
| Filter mode latched out of reach | Presets `12` / `15` hijacked the `L1Wave` / `L2Wave` knobs as a filter-mode selector. The mode it wrote outlived the program change, so a filter left in Highpass stayed there with nothing on the panel showing it, and the only way back was to return to preset 12, set the wave to `0`, and leave again | The hijack is gone from both the program-change handler and the audio path. Both filters are lowpass, as on the original hardware, and those two presets now do [amplitude modulation](#amplitude-modulation-presets-12-and-15) instead |
| DC on the output | The asymmetric Sherman/Polivoks path through the double tanh left 0.06–0.11 of DC on the bus (−19 to −24 dBFS on nearly every program) — inaudible, but it offsets the DAC and costs ~1 dB of headroom | One-pole DC blocker at ~10 Hz ahead of the output stage. DC is now −38…−87 dBFS |

## The Modulation Matrix & Operational Quirks
ScrutaAstri utilizes a heavily optimized Active Partial Counting matrix to route modulations without exhausting the Drumlogue's CPU. The modulation target is defined by the active preset: `Target = Program % 24`.

### Amplitude Modulation (presets 12 and 15)

Preset `12` (and 36, 60, 84) multiplies **Osc 1** by **LFO 1**; preset `15` (and 39, 63, 87) multiplies **Osc 2** by **LFO 2**. `L1Dpth` / `L2Dpth` set how deep:

```
gain = 1 - depth/2 * (1 - lfo)
```

At depth `0` the gain is exactly 1.0 and the oscillator is untouched, so the presets are a no-op until you turn the depth up. At depth `100` the gain tracks the LFO from silence to unity. The fold is bounded by 1.0 at every depth, so AM can only remove level — it never pushes the output stage harder.

With a plain LFO shape this is tremolo or, at audio rate, sideband AM. With shape `9` or `10` it is a rhythm: the oscillator is struck and then held silent until the next cycle. Running both presets is not possible at once (one program selects one target), but `L1Rate` and `L2Rate` are independent, so pitting one against the other at, say, a 3:2 ratio is available the moment you have two programs' worth of motion sequencing.

**To get a strike with real silence between hits, filter 2 has to stop howling.** The Polivoks emulation self-oscillates by design (see *Polivoks Chaos* above), and that limit cycle is downstream of both oscillators — it keeps sounding no matter how hard the AM gates the oscillators feeding it. Measured at the bus, gate depth on preset 12 with Osc 2 muted:

| Patch | Gate depth |
|---|---|
| `F2Cut` 6100 Hz, `F2Res` 0 (header defaults) | −3 dB — the howl floors it |
| `F2Cut` 6100 Hz, `F2Res` 60 | silence between strikes |
| `F2Cut` 1200 Hz, `F2Res` 0 | silence between strikes |
| `F2Cut` 1200 Hz, `F2Res` 40, `F1Cut` 2000 Hz | silence between strikes |

Counter-intuitively it is **raising** `F2Res` that settles the filter, because the knob maps to the inverse of the internal damping coefficient. Lowering `F2Cut` works at any resonance.

Preset 15 is a different case: gating Osc 2 cannot silence Osc 1, which has no level control of its own, so it tops out around −10 dB of swing — deep tremolo on the Osc 2 layer rather than a gate.

# ScrutaAstri
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
* **Percussive Shapes:** A new `LFO_EXP_DECAY` shape provides ADSR-like percussive strikes for the Master VCA.

## The Drone / Sequencer Concept
Unlike typical drumlogue synthesizers, ScrutaAstri operates in **Infinite Sustain** mode. The audio thread continuously calculates and outputs the drone. The step sequencer is primarily used for **Motion Sequencing**—automating LFO rates, filters, and distortion parameters per step while the drone screams continuously in the background.

## Hardware Bug Fixes Applied

| Bug | Symptom | Fix |
|-----|---------|-----|
| CMOS dead zone | `CMOSDist` values 1–32 produced no output change — the SVF `f` coefficient mapped to a range that left the filter frozen | Dead zone removed; coefficient mapping now covers the full meaningful range continuously from 0 through to Sherman chaos territory |
| LFO always modulating filter | Filter 1 was being modulated by LFO 1 every render call regardless of the LFO target assignment | LFO application is now gated on target — only applied when LFO target is `Filter1` or equivalent; LFO3 (Master VCA tremolo) remains unconditional as designed |

## The Modulation Matrix & Operational Quirks
ScrutaAstri utilizes a heavily optimized Active Partial Counting matrix to route modulations without exhausting the Drumlogue's CPU. The modulation target is defined by the active preset: `Target = Program % 24`.

**Important UI Quirk (Filter Modes via Proxy):**
Because the Drumlogue lacks a dedicated "Filter Mode" knob, preset `12` (and its modulo equivalents like 36, 60, 84) hijacks the **LFO 1 Wave** parameter to act as a static Filter Mode selector.
* When Preset 12 is active, changing the LFO 1 shape will immediately snap Filter 1 to a new mode (0=Lowpass, 1=Highpass, 2=Bandpass, 3=Notch).
* *The filter mode is a latched state.* If you set the filter to Highpass using Preset 12 and then change to a different preset (e.g., Preset 13), the filter will remain locked in Highpass mode, while the LFO 1 Wave parameter reverts to controlling the actual LFO shape.
* To restore the original Lowpass state, you must return to Preset 12 or equivalent (15 or equivalent for filter 2), manually set the LFO 1 Wave parameter to `0` (Triangle), and then switch back to your desired preset.
# LuceAlNeon – Parallel Multi-Effects Reverb for drumlogue

**LuceAlNeon** (Neon Light) is a highly unconventional, true-stereo reverb plugin for the KORG drumlogue. Moving away from standard room simulation, it utilizes a barebones acoustic delay matrix that splits into five entirely independent, parallel DSP pathways. Instead of simply filtering a reverb tail, LuceAlNeon synthesizes sub-harmonics, granular pops, and harmonic distortion directly from the acoustic space.

## Architectural & DSP Design Choices

The core engine is a true-stereo, 8-channel Feedback Delay Network (FDN) utilizing prime-number delay lengths and a Hadamard mixing matrix. To preserve the purity of the acoustic delays, all modulation and filtering have been extracted from the recursive loop. The raw reverb tail is then fed into five parallel processing blocks, completely avoiding the phase-cancellation issues common in series-EQ reverb designs.

### 1. DARK (Dual-Head Granular Pitch Shifter)
It creates a small delay buffer where the "read heads" play back the audio at exactly 50% speed. To hide the "tape-looping" click when the read head reaches the end of its window, we use two read heads offset by 180 degrees and smoothly crossfade between them.

### 2. GLOW (Direct-Modulation Chamberlin SVF)
To create a lush, breathing swirl without exhausting the Cortex-A9 CPU, Glow utilizes a Directly-Modulated Chamberlin State Variable Filter (SVF). By modulating the filter coefficient directly (rather than calculating audio-rate trigonometric Hz-to-coefficient conversions), the filter operates extremely fast. The right channel's LFO is offset by exactly 90 degrees from the left, creating a psychoacoustic, widening "swirl" across the stereo field.

### 3. BRIGHT (Harmonic Exciter)
Standard parallel high-pass filters create comb-filtering (phaser effects) when mixed back with the dry signal. To solve this, the Bright path acts as a true Harmonic Exciter. It isolates frequencies above 5kHz using a steep 2nd-order Butterworth High-Pass filter, then drives them into a polynomial soft-clipper ($x - x^3/3$). This synthesizes entirely *new* upper harmonics that did not exist in the original signal, producing a glassy, expensive "air" that sits perfectly on top of the mix.

### 4. COLOR (Visual Spectrum Resonators)
The Color path runs the reverb tail through six parallel, High-Q bandpass Biquad filters. These resonators are mathematically tuned to frequencies corresponding to the visual light spectrum: **4.1, 5.0, 5.2, 5.8, 6.6, and 7.2 kHz**. This imparts a distinctly metallic, synthetic, and "illuminated" resonance to the upper-midrange.

### 5. SPARKLE (Granular Sample & Hold)
A dedicated 100ms micro-buffer continuously records the reverb tail. Based on a Xorshift pseudo-random number generator, this block randomly "grabs" slices of the audio and plays them back at $2\times$ or $4\times$ speed (pitching the reverb up $+12$ or $+24$ semitones). Each granular "pop" is assigned a randomized stereo pan, creating an effervescent, bubbling effect like neon sparks.

---

## User Guide

LuceAlNeon operates using a parallel mixing philosophy. A value of `0%` on any of the five main effect knobs completely mutes that specific characteristic, leaving the dry drum signal and the other parallel paths untouched.

### Parameters

**Page 1: The Spectrum**
* **NAME (0):** Selects the starting preset (`StanzaNeon`, `VicoBuio`, `Strobo`, `Bruciato`).
* **DARK (1):** Blends in the -2 Octave sub-harmonic rumble. Great for turning kicks and toms into massive, cinematic impacts.
* **BRIG (2):** Controls the Harmonic Exciter. Turn up to add synthesized, distortion-based "Air" and sizzle to hi-hats and snares.
* **GLOW (3):** Controls the level and speed of the stereo Chamberlin SVF modulation. Higher values increase both the wet mix of the sweeping filter and the LFO rate.

**Page 2: Mechanics**
* **COLR (4):** Blends in the 6 high-Q visual-spectrum resonators. Higher values make the reverb sound more synthetic and ringing.
* **SPRK (5):** Controls the density of the granular Sample & Hold. Higher values lower the probability threshold, resulting in a dense shower of pitched-up, auto-panned bubbles.
* **SIZE (6):** Scales the FDN delay lines (0.1x to 2.0x) to change the physical size of the acoustic space.
* **PDLY (7):** Pre-delay time (0 to ~330ms). Separates the dry drum transient from the onset of the neon reverb explosion.

## Technical Notes & Building
* **CPU Optimization:** NEON intrinsics are used wherever vectorization across channels is mathematically possible. IIR filters (like the Color Biquads and Bright Exciter) are strictly executed in scalar loops to prevent the comb-filtering artifacts inherent in vectorized feedback topologies.
* **Dependencies:** Requires the KORG drumlogue SDK and the custom `float_math.h` library containing the `fastersinfullf` phase-normalized fast math functions.
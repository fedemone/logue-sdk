# LuceAlNeon – Parallel Multi-Effects Reverb for drumlogue

> **Disclaimer:** LuceAlNeon is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

**LuceAlNeon** (Neon Light) is a highly unconventional, true-stereo reverb plugin for the KORG drumlogue. Moving away from standard room simulation, it utilizes a barebones acoustic delay matrix that splits into five entirely independent, parallel DSP pathways. Instead of simply filtering a reverb tail, LuceAlNeon synthesizes sub-harmonics, granular pops, and harmonic distortion directly from the acoustic space.

## Architectural & DSP Design Choices

The core engine is a true-stereo, 8-channel Feedback Delay Network (FDN) utilizing prime-number delay lengths and a Hadamard mixing matrix. To preserve the purity of the acoustic delays, all modulation and filtering have been extracted from the recursive loop. The raw reverb tail is then fed into five parallel processing blocks, completely avoiding the phase-cancellation issues common in series-EQ reverb designs.

### 1. DARK (Dual-Head Granular Pitch Shifter)
It creates a small delay buffer where the "read heads" play back the audio at exactly 50% speed (one octave down). To hide the "tape-looping" click when the read head reaches the end of its window, we use two read heads offset by 180 degrees and smoothly crossfade between them.

### 2. GLOW (Direct-Modulation Chamberlin SVF)
To create a lush, breathing swirl without exhausting the Cortex-A9 CPU, Glow utilizes a Directly-Modulated Chamberlin State Variable Filter (SVF). By modulating the filter coefficient directly (rather than calculating audio-rate trigonometric Hz-to-coefficient conversions), the filter operates extremely fast. The right channel's LFO is offset by exactly 90 degrees from the left, creating a psychoacoustic, widening "swirl" across the stereo field. The LFO (RATE, 0.05-3.2 Hz) is evaluated once per 64-frame block and interpolated in between, which is exact to 1.4e-4 at its fastest.

### 3. BRIGHT (Harmonic Exciter)
Standard parallel high-pass filters create comb-filtering (phaser effects) when mixed back with the dry signal. To solve this, the Bright path acts as a true Harmonic Exciter. It isolates frequencies above 5kHz using a steep 2nd-order Butterworth High-Pass filter, then drives them into a polynomial soft-clipper ($x - x^3/3$). This synthesizes entirely *new* upper harmonics that did not exist in the original signal, producing a glassy, expensive "air" that sits perfectly on top of the mix.

### 4. COLOR (Visual Spectrum Resonators)
The Color path runs the reverb tail through six parallel, High-Q bandpass Biquad filters. These resonators are mathematically tuned to frequencies corresponding to the visual light spectrum: **4.1, 5.0, 5.2, 5.8, 6.6, and 7.2 kHz**. This imparts a distinctly metallic, synthetic, and "illuminated" resonance to the upper-midrange.

### 5. SPARKLE (Granular Sample & Hold)
A dedicated 85 ms micro-buffer continuously records the reverb tail. Based on a Xorshift pseudo-random number generator, this block randomly "grabs" 5-15 ms slices of the audio and plays them back faster (pitching them up $+5$, $+7$, $+12$, $+19$ or $+24$ semitones). Each granular "pop" is assigned a randomized stereo pan, creating an effervescent, bubbling effect like neon sparks. A grain starts far enough back in the buffer that, playing faster than the tail is recorded, it never catches up with the write head.

### 6. IRID (Granular Iridescence: drone to octave-up)
IRID sets both the level and the character of the halo:

| IRID | Character |
|------|-----------|
| 0-50% | **Drone**: two crossfading grain heads (about 47 ms windows) read the tail at roughly a tenth of its speed, three octaves and more down |
| 50-80% | The drone fades out as the octave-up shimmer fades in (an equal-power crossfade, so the loudness holds) |
| 80-100% | **Octave up**: the same two-head layout reading the tail at twice its speed, a bright shimmer an octave above the reverb |

Both share the swirl: each head favours one side, so the halo moves across the stereo image as the heads fade in and out, with a slight speed wobble from the GLOW LFO. The drone goes through an asymmetric soft saturation and a low-pass at about 1.2 kHz; the shimmer through the same saturation and a much higher low-pass (4.4 kHz left, 5.3 kHz right), and +3.2 dB of make-up, measured, so it sits level with the drone on a broadband tail.

The right channel of the drone used to weight each grain head by the other head's envelope, so it heard both heads at the instant they jumped back a grain: a click train at about 42 per second, and a strong leak of the input's own pitch (-62 dB against -81 dB now, on a 220 Hz tone). Each head now carries its own envelope on both sides.

---

## User Guide

LuceAlNeon operates using a parallel mixing philosophy. A value of `0%` on any of the five main effect knobs completely mutes that specific characteristic, leaving the dry drum signal and the other parallel paths untouched. It has **14 parameters** across 4 pages.

### Parameters

**Page 1: The Spectrum**
* **NAME (0):** Selects the starting preset (`StanzaNeon`, `VicoBuio`, `Strobo`, `Bruciato`).
* **DARK (1):** Blends in the one-octave-down sub-harmonic rumble. Great for turning kicks and toms into massive, cinematic impacts.
* **BRIG (2):** Controls the Harmonic Exciter. Turn up to add synthesized, distortion-based "Air" and sizzle to hi-hats and snares.
* **GLOW (3):** Controls the level and speed of the stereo Chamberlin SVF modulation. Higher values increase both the wet mix of the sweeping filter and the LFO rate.

**Page 2: Mechanics**
* **COLR (4):** Blends in the 6 high-Q visual-spectrum resonators. Higher values make the reverb sound more synthetic and ringing.
* **SPRK (5):** Controls the density of the granular Sample & Hold. Higher values lower the probability threshold, resulting in a dense shower of pitched-up, auto-panned bubbles.
* **SIZE (6):** Scales the FDN delay lines (0.1x to 2.0x) to change the physical size of the acoustic space. Moving it bends the tail like tape (at most an octave down or a fifth up while it glides) instead of clicking.
* **PDLY (7):** Pre-delay time (0 to ~333 ms). Separates the dry drum transient from the onset of the neon reverb explosion. Glides like SIZE.

**Page 3: Decay**
* **DCAY (8):** FDN feedback coefficient controlling overall decay length (tail length / RT60). Low values yield a tight, short reverb; high values yield a long, dense wash.
* **BASS (9):** Per-channel HPF inside the FDN feedback loop. Controls how much low-frequency energy is preserved in the tail. Low values keep the bass full; high values thin out the tail, preventing muddiness on kicks and toms.
* **CLRQ (10):** Shifts the COLR resonators by up to one octave down (-100%) or up (+100%).
* **RATE (11):** GLOW LFO speed, 0.05 to 3.2 Hz (exponential).

**Page 4: Space**
* **IRID (12):** The granular iridescence halo: a deep drone up to 50%, morphing into an octave-up shimmer between 50% and 80%, shimmer alone above that.
* **WDTH (13):** Stereo width of the wet signal: 0% mono, 50% as is, 100% extra wide.

With high BASS the in-loop HPF lifts the very top of the tail slightly above unity, so DCAY is held where the loop's highest gain reaches that of DCAY 100% at BASS 0%. Below that (every preset, and any DCAY at BASS 0%) DCAY is exactly what it was; above it (DCAY over 92% at BASS 100%, for example) DCAY gives the longest stable tail instead of one that slowly grows without bound.

### Factory Presets

| # | Name | Character |
|---|------|-----------|
| 0 | StanzaNeon | Balanced, medium room with gentle resonators |
| 1 | VicoBuio | Dark, long decay — heavy DARK sub-harmonic |
| 2 | Strobo | Bright, short, heavily sparkled |
| 3 | Bruciato | Dense, saturated with maximum COLOR resonators |

## Technical Notes & Building
* **CPU Optimization:** NEON intrinsics are used wherever vectorization across channels is mathematically possible. IIR filters (like the Color Biquads and Bright Exciter) are strictly executed in scalar loops to prevent the comb-filtering artifacts inherent in vectorized feedback topologies. The eight delay-line reads run four channels per NEON vector, each interpolation pair coming from one 2-float load.
* **Dependencies:** Requires the KORG drumlogue SDK and the custom `float_math.h` library containing the `fastersinfullf` phase-normalized fast math functions.
* **FPU mode:** each render turns on flush-to-zero and default-NaN and puts the caller's mode back afterwards. It used to set bit 22 (round towards plus infinity, not default-NaN) and never restore it, which switched every other unit on the drumlogue's audio thread to rounding up.
* **Optimisation level:** `-Os`, the SDK default. `config.mk` used to ask for `-O3` through `UDEFS`, which a later `UDEFS =` reset, so the unit always shipped at `-Os` -- and measured, that is the fastest level for it (below). Objects depend on `config.mk`, so setting `OPTIM` there rebuilds them; a tree built before that change needs one clean build. To check what a `.drmlgunit` was built with:

  ```
  strings luce_al_neon.drmlgunit | grep "build:"
  build: -Os (size), gcc 13.3.0
  ```

### CPU

ARM instructions per 64-frame render (qemu-arm, kick + tone input):

| Build | Default settings | All six paths at 100% |
|-------|-----------------:|----------------------:|
| as shipped (-Os) | 51.8k | 63.3k |
| shipped code at -O3 | 55.6k | 67.4k |
| now, -O3 | 44.2k | 54.0k |
| **now, -Os** | **38.5k** | **49.8k** |

IRID between 50% and 80%, where drone and octave-up both run, adds about 19k to the default figure.

(An earlier version of this table read 11.2k -> 6.2k and recommended `-O3`: the script that read QEMU's trace counted at most eight instructions per basic block, which undercounts long straight-line code most. Recounted, `-O3` makes this unit slower, not faster.)
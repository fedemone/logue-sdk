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
* **The Morphing Filter:** The `CMOSDist` parameter morphs Filter 1 smoothly from a Clean SVF (0-33) -> Moog-style symmetrical saturation (34-65) -> Sherman Filterbank asymmetrical chaos (66-100). Its screaming is on demand too, from the top third of `F1Res` — see [Filter Self-Oscillation](#filter-self-oscillation).
* **Polivoks Chaos:** A secondary filter model aggressively replicates the Formanta Polivoks topology, utilizing non-linear mathematical saturation inside the resonance feedback loop and integrators to achieve its signature unstable howling — on demand, from the top third of `F2Res`. See [Filter Self-Oscillation](#filter-self-oscillation).
* **Audio-Rate Wavetable Asymmetry:** In the Sherman territory, the raw shape of Wavetable 1 is injected into Filter 1 at audio rates (48kHz), physically ripping the filter's DC offset apart based on the audio source.
* **Bidirectional Wavetable Scanning:** Wavetable playback direction is dictated by the active preset program range — see [The Program Layout](#the-program-layout).
* **Filter Modes from the Program:** Highpass, bandpass and notch live in program bands `96-239`, on either filter, never as latched state.
* **Exponential LFOs:** LFO rates are exponentially mapped from glacial 100-second cycles (0.01Hz) up to audio-rate FM screams (1000Hz).
* **Percussive Shapes:** Three of the eleven LFO shapes are envelopes rather than waveforms — see [LFO Shapes](#lfo-shapes).
* **Per-Oscillator AM:** Presets `12` and `15` (and their modulo-24 equivalents) ring LFO 1 into Osc 1's amplitude and LFO 2 into Osc 2's — see [Amplitude Modulation](#amplitude-modulation-presets-12-and-15).
* **Rhythmic Add-Ons:** Eight extras that arm themselves once an LFO carries a strike shape — see [Rhythmic Add-Ons](#rhythmic-add-ons).

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
| Filter mode latched out of reach | Presets `12` / `15` hijacked the `L1Wave` / `L2Wave` knobs as a filter-mode selector. The mode it wrote outlived the program change, so a filter left in Highpass stayed there with nothing on the panel showing it, and the only way back was to return to preset 12, set the wave to `0`, and leave again | The hijack is gone. Filter modes are now a pure function of the program number ([program bands 96-239](#the-program-layout)), recomputed on every change, so leaving a band always undoes it. Presets 12 / 15 do [amplitude modulation](#amplitude-modulation-presets-12-and-15) instead |
| Both filters always self-oscillating | Each filter's Euler-forward guard used the linear condition `f² + 2fq < 4`, but both run their integrators through `fast_tanh`, whose small-signal gain is 1.5 — so both guards were 2.25× too loose. Fed literal silence, filter 2 still put out ~0.45 indefinitely at any high cutoff with low resonance, and filter 1 did the same on its saturated path, which is any patch with `CMOS` or `F1Res` above 0. Since they sit downstream of both oscillators, nothing upstream could ever make the unit quiet | Each guard corrected for its own topology (see [Filter Self-Oscillation](#filter-self-oscillation)); together they predict every measured case. Self-oscillation is now opt-in on `F1Res` / `F2Res`, amplitude-limited rather than clamp-limited |
| Osc 1 never reached filter 2 | The routing line for filter 2 read `out_osc2` in both terms, so Osc 1's path to filter 2 was dropped and Osc 2 was counted twice | First term corrected to `out_osc1`. Every band is about 1 dB quieter and the per-oscillator balance is the one the routing describes |
| Resonance modulation walked away | Modulation targets `9` / `11` did `m_f1_q += lfo * 0.1` once per APC cycle with nothing ever resetting it — a random walk with a step of 0.1 every 4 samples. Within a second the resonance had wandered off the knob and never came back. Target `11` also wrote filter **1**'s resonance, so filter 2's was unreachable | Both are now an offset from the knob position, `q = q_base + lfo × 2.0`, floored at the stability limit; target `11` writes `m_f2_q` |
| DC on the output | The asymmetric Sherman/Polivoks path through the double tanh left 0.06–0.11 of DC on the bus (−19 to −24 dBFS on nearly every program) — inaudible, but it offsets the DAC and costs ~1 dB of headroom | One-pole DC blocker at ~10 Hz ahead of the output stage. DC is now −38…−87 dBFS |

## The Program Layout

There are 242 programs. Every one of them fully determines three things — modulation target, scan direction and both filter modes — and all three are recomputed from the program number on each change. Nothing is remembered from the program you came from, which is what keeps filter modes out of the latched limbo the old `L1Wave` hijack put them in.

`Target = Program % 24` throughout, so the modulation matrix is the same in every band.

| Programs | Scan direction | Filter 1 | Filter 2 |
|---|---|---|---|
| `0-23` | forward | lowpass | lowpass |
| `24-47` | Osc 1 reversed | lowpass | lowpass |
| `48-71` | Osc 2 reversed | lowpass | lowpass |
| `72-95` | both reversed | lowpass | lowpass |
| `96-119` | forward | **highpass** | lowpass |
| `120-143` | forward | **bandpass** | lowpass |
| `144-167` | forward | **notch** | lowpass |
| `168-191` | forward | lowpass | **highpass** |
| `192-215` | forward | lowpass | **bandpass** |
| `216-239` | forward | lowpass | **notch** |
| `240` | — | crystal drone | |
| `241` | — | metal drone | |

Programs `0-95` behave exactly as they did before the filter-mode bands existed. The two drone engines moved from `95` / `96` to `240` / `241`.

The `Prgrm` display spells out what each number selects rather than leaving 242 of them to be memorised: `0 Fwd LP`, `3 R1 LP`, `12 F1Hi`, `Drn Metal`.

**The filter-2 highpass and bandpass bands need `F2Cut` brought down.** A highpass at the default 800 Hz removes a 65 Hz drone's fundamental, and those bands measure −16 to −27 LUFS as a result. At `F2Cut` 200 Hz the highpass band is back to −4.7 dB RMS, level with the lowpass bands.

## Filter Self-Oscillation

`F1Res` and `F2Res` above **67** hand their filter over to self-oscillation. It takes off around 72 and gets louder from there, so the whole top third of each knob is the ramp. Below 67 both filters are provably stable and decay to actual silence on a silent input.

This matters beyond the noise it makes: both filters sit downstream of both oscillators, so while either sings, nothing upstream of it can make the unit quiet. Every gate, strike and add-on in this document depends on them being able to stop.

### Why they used to sing on their own

`fast_tanh` has a small-signal gain of 1.5, and both filters run their integrators through it. The Euler-forward guard in each `set_coeffs` used the linear condition `f² + 2fq < 4`, which is 2.25× too loose for a loop with that much extra gain. The two filters are different loops, so they need different limits:

| Path | Real condition | `f_max` |
|---|---|---|
| Linear integrators (filter 1, clean and wavefolder branches) | `f² + 2fq < 4` | `√(q²+4) − q` |
| tanh integrators, linear damping (filter 1, saturated branch) | `2.25f² + 3fq < 4` | `⅔·(√(q²+4) − q)` |
| tanh integrators and tanh damping (filter 2, always) | `1.5f² + 3fq < 4` | `√(q² + 8/3) − q` |

Each formula predicts every measured case — which filter sings, at which cutoff, at which resonance. Filter 1 picks its limit from `drive` and `sherman_asym`, since those decide which branch runs, so `set_coeffs` is called after them in the render loop rather than before.

### How the howl works

Van der Pol style: the resonance feedback is negative while the filter is quiet, so the oscillation grows, and climbs back through zero as the state grows, so it settles at a fixed amplitude. Simply inverting the damping does not work — `fast_tanh` saturates the feedback at a constant instead of reducing it, so the states ramp until something else stops them. The first attempt at this pinned at amplitude 6.6 against a state clamp; the damping law settles between 0.6 and 1.3 instead, and the clamps are left as insurance.

Measured at the bus, as the gate depth of an AR strike on preset 12 with Osc 2 muted:

| | `0` | `40` | `67` | `80` | `100` |
|---|---|---|---|---|---|
| `F2Res` | silent | silent | silent | −0.8 dB | −0.7 dB |
| `F1Res` | silent | silent | silent | −0.8 dB | −0.5 dB |

`CMOS` at 100 with both resonances at 0 still gates to silence: distortion drives the filters hard, but it no longer sets them off.

## The Modulation Matrix
ScrutaAstri utilizes a heavily optimized Active Partial Counting matrix to route modulations without exhausting the Drumlogue's CPU. The modulation target is defined by the active preset: `Target = Program % 24`.

### Amplitude Modulation (presets 12 and 15)

Preset `12` (and 36, 60, 84) multiplies **Osc 1** by **LFO 1**; preset `15` (and 39, 63, 87) multiplies **Osc 2** by **LFO 2**. `L1Dpth` / `L2Dpth` set how deep:

```
gain = 1 - depth/2 * (1 - lfo)
```

At depth `0` the gain is exactly 1.0 and the oscillator is untouched, so the presets are a no-op until you turn the depth up. At depth `100` the gain tracks the LFO from silence to unity. The fold is bounded by 1.0 at every depth, so AM can only remove level — it never pushes the output stage harder.

With a plain LFO shape this is tremolo or, at audio rate, sideband AM. With shape `9` or `10` it is a rhythm: the oscillator is struck and then held silent until the next cycle. Running both presets is not possible at once (one program selects one target), but `L1Rate` and `L2Rate` are independent, so pitting one against the other at, say, a 3:2 ratio is available the moment you have two programs' worth of motion sequencing.

Measured at the bus on preset 12 with Osc 2 muted, the strike gates to full silence at every cutoff, resonance and distortion setting — as long as `F1Res` and `F2Res` stay under the [howl threshold](#filter-self-oscillation). Crank either past 67 and that filter sings through the gaps, which is a choice rather than its resting state.

Preset 15 is a different case: gating Osc 2 cannot silence Osc 1, which has no level control of its own, so it tops out around −7 dB of swing — deep tremolo on the Osc 2 layer rather than a gate.

## Rhythmic Add-Ons

Eight extras that only exist while **LFO 1 or LFO 2 carries a strike shape** (`9` AR strike or `10` ADSR staccato). Without a strike there is no rhythm for them to hang on, so they stay out of the way entirely — with a plain waveform on both, none of the knobs below do anything.

Once armed, which add-ons are live is read from three knobs. Nothing is stored, so it is always visible from the panel:

| Knob | Picks |
|---|---|
| `L3Wave % 8` | the primary add-on, always |
| `O1Wave` | a second — **only when parked below 8** |
| `O2Wave` | a third — same condition |

The three picks are OR'd, so they stack: aim them at the same number for one add-on, spread them for three. Gating the oscillator wave knobs on *below 8* is what keeps them usable as timbre controls — the other 248 wavetables select nothing, so choosing a waveform does not silently rearrange the rhythm. (A plain `% 8` on the full 0–255 range would make every wavetable choice a rhythm choice too; that is a one-line change in `updateRhythmMask()` if you want it.)

| # | Add-on | What it does |
|---|---|---|
| 0 | **SeqSync** | Restarts all three LFO phases on every sequencer step. Free-running strikes drift against the pattern; this is the difference between a beat that lands and one that is merely nearby |
| 1 | **Euclid** | A 16-step pattern skips strikes, so a repeat is a rhythm rather than a metronome. The pattern is chosen by `Program % 8`, so motion sequencing can change it |
| 2 | **SrrZap** | Each strike drags the sample rate down. The aliasing that comes back up is a percussive timbre change, and unlike an amplitude gate it survives whatever filter 2 is doing |
| 3 | **BitSnap** | Each strike collapses the bit depth toward 3 bits |
| 4 | **FiltPluck** | Each strike lifts filter 1 by up to three octaves — the 808-tom trick, a pitched thump out of a cutoff sweep |
| 5 | **NoiseBurst** | Each strike fires white noise into filter 1, which shapes it into a hat or a snare |
| 6 | **SHPitch** | Each strike picks a new pitch, ±6 semitones |
| 7 | **Accent** | Strikes cycle loud / soft over four steps |

Euclid and Accent are folded back into the driving LFO's own value, so a skipped step is skipped everywhere that LFO goes — the AM included — rather than only in the add-ons hanging off it.

Depth comes from the driving LFO's own depth knob (`L1Dpth` or `L2Dpth`), the same control the AM uses.

# README.md - Updated with Resonant Mode & Voice Allocation

## Project Overview

A **4-voice FM percussion synthesizer** for KORG drumlogue with **5 synthesis engines** and **intelligent voice allocation**. Now featuring a resonant synthesis mode based on Lazzarini's summation formulae (2017, Section 4.10.3), allowing one voice to be dynamically assigned to resonant synthesis while maintaining the original 4-voice structure.

## Key Features

- **5 Synthesis Engines**: Kick, Snare, Metal, Perc, and Resonant
- **Voice Allocation**: Single-parameter control for which voice uses resonant synthesis
- **Resonant Modes**: Low-pass, Band-pass, High-pass, Notch, Peak
- **LFO Targets**: 10 targets including resonant parameters, noise mix, and cross-LFO modulation
- **Envelope ROM**: 128 ADR curves optimized for percussion
- **Metal Character System**: EnvShape selects between DX7-style cymbal (ratios 1.0/1.483/1.932/2.546) and Gong/tam-tam character (ratios 1.0/2.756/3.752/5.404) via bit 7
- **NEON Optimization**: Fully vectorized for ARMv7

## Parameter Page Layout (v2.0)

```
Page 1: Voice Probabilities (NEW)
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  Voice1Prob │  Voice2Prob │  Voice3Prob │  Voice4Prob │
│   (0-100%)  │   (0-100%)  │   (0-100%)  │   (0-100%)  │
└─────────────┴─────────────┴─────────────┴─────────────┘

Page 2: Kick + Snare Parameters
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  KSweep     │  KDecay     │  SNoise     │  SBody      │
│   (0-100%)  │   (0-100%)  │   (0-100%)  │   (0-100%)  │
└─────────────┴─────────────┴─────────────┴─────────────┘

Page 3: Metal + Perc Parameters
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  MInharm    │  MBrght     │  PRatio     │  PVar       │
│   (0-100%)  │   (0-100%)  │   (0-100%)  │   (0-100%)  │
└─────────────┴─────────────┴─────────────┴─────────────┘

Page 4: LFO1
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  L1Shape    │  L1Rate     │  L1Dest     │  L1Depth    │
│   (0-8)     │   (0-100%)  │   (0-7)     │  (-100-100) │
└─────────────┴─────────────┴─────────────┴─────────────┘

Page 5: LFO2
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  L2Shape    │  L2Rate     │  L2Dest     │  L2Depth    │
│   (0-8)     │   (0-100%)  │   (0-7)     │  (-100-100) │
└─────────────┴─────────────┴─────────────┴─────────────┘

Page 6: Envelope + Voice + Resonant
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  EnvShape   │  VoiceAlloc │  ResMode    │  ResMorph   │
│   (0-255)   │   (0-11)    │   (0-4)     │   (0-100%)  │
└─────────────┴─────────────┴─────────────┴─────────────┘
EnvShape encoding: bit 7 = metal character (0=Cymbal/DX7, 1=Gong), bits[6:0] = envelope curve 0-127
```

## Voice Allocation (Param 21)

Single parameter controls the engine assignment for all four voices. R = Resonant engine.

| Value | Display | Voice 0 | Voice 1 | Voice 2 | Voice 3 |
|-------|---------|---------|---------|---------|---------|
| 0 | "K-S-M-P" | Kick | Snare | Metal | Perc |
| 1 | "K-S-M-R" | Kick | Snare | Metal | **Resonant** |
| 2 | "K-S-R-P" | Kick | Snare | **Resonant** | Perc |
| 3 | "K-R-M-P" | Kick | **Resonant** | Metal | Perc |
| 4 | "R-S-M-P" | **Resonant** | Snare | Metal | Perc |
| 5 | "K-S-R-M" | Kick | Snare | **Resonant** | Metal |
| 6 | "K-R-S-P" | Kick | **Resonant** | Snare | Perc |
| 7 | "R-K-M-P" | **Resonant** | Kick | Metal | Perc |
| 8 | "R-S-K-P" | **Resonant** | Snare | Kick | Perc |
| 9 | "M-R-K-P" | Metal | **Resonant** | Kick | Perc |
| 10 | "P-R-K-M" | Perc | **Resonant** | Kick | Metal |
| 11 | "M-P-R-K" | Metal | Perc | **Resonant** | Kick |

**Design Philosophy**: At most one voice is Resonant at a time. Values 5–11 also shift the non-resonant engine assignments, enabling unusual layering combinations.

## Resonant Synthesis Engine

Based on Lazzarini's summation formulae (2017, Section 4.10.3), the resonant engine combines single-sided and double-sided responses:

### Mathematical Foundation

```
Single-sided (low-pass):  s(t) = sin(ωt) / (1 - 2a cos(θ) + a²)
Double-sided (band-pass): s(t) = (1 - a²) sin(ωt) / (1 - 2a cos(θ) + a²)

Where:
- ω = 2πf₀ (fundamental frequency)
- θ = 2πf_c (resonance center frequency)
- a controls resonance (0 ≤ a < 1)
```

### Resonant Modes (Param 22)

| Value | Mode | Description |
|-------|------|-------------|
| 0 | LowPass | Single-sided response, low-pass character |
| 1 | BandPass | Mixed response, original resonant synthesis |
| 2 | HighPass | Derived high-pass response |
| 3 | Notch | Notch filter character |
| 4 | Peak | Boosted band-pass with resonance |

### Resonant Frequency (Param 23)

Maps 0-100% to **50 Hz - 8000 Hz** center frequency for the resonant peak.

## LFO Targets (0–10)

| Value | Display | Target | Percussion Character |
|-------|---------|--------|----------------------|
| 0 | None | LFO disabled | — |
| 1 | Pitch | Oscillator frequency | Percussive at any rate (pitch sweep, vibrato, FM crunch) |
| 2 | ModIdx | FM modulation index / brightness | Percussive — controls spectral density |
| 3 | Env | Envelope shape / level | Percussive at fast rates (tremolo/AM); can shift toward melodic synth at slow rates |
| 4 | LFO2Ph | LFO1 modulates LFO2's phase increment | Meta-modulation — character depends on both LFOs |
| 5 | LFO1Ph | LFO2 modulates LFO1's phase increment | Meta-modulation — character depends on both LFOs |
| 6 | ResFreq | Resonant engine center frequency | Silent unless a Resonant voice is active in VoiceAlloc |
| 7 | Resonance | Resonant engine Q amount | Silent unless a Resonant voice is active in VoiceAlloc |
| 8 | NoiseMx | Snare noise mix (additive offset); Metal brightness_add | Percussive — adds texture sweep to snare and metal |
| 9 | ResMrph | Resonant engine morph (crossfade between filter modes) | Silent unless a Resonant voice is active in VoiceAlloc |
| 10 | MetalGate | Metal engine amplitude gate — open/closed hi-hat control | Fully open (no effect) at depth 0%; positive depth + Ramp shape = hi-hat gate |

> **Note**: LFO targets 6, 7, and 9 are silent when no Resonant voice is active in the current VoiceAlloc setting. This is by design — use them with VoiceAlloc values 1–11.

> **Open/Closed Hi-Hat** (target 10 MetalGate): Set L1Shape = Ramp+Ramp, L1Target = MetalGate, L1Depth = +50% to +100%. LFO phase resets on every trigger, so the ramp fires once per hit. High L1Rate → gate closes fast → **closed hi-hat** character. Low L1Rate → gate closes slowly → **open hi-hat** character. L1Depth = 0% disables the gate entirely (fully open).

## Metal Engine Character System

The **EnvShape** parameter (0–255) encodes two independent values:
- **Bit 7** (value ≥ 128): selects metal oscillator character
- **Bits 6:0** (value & 0x7F): selects envelope ROM curve (0–127)

| EnvShape Range | Metal Character | Ratios Used |
|----------------|-----------------|-------------|
| 0–127 | Cymbal / DX7-style | 1.0 / 1.483 / 1.932 / 2.546 — classic hi-hat / crash FM spectrum |
| 128–255 | Gong / tam-tam | 1.0 / 2.756 / 3.752 / 5.404 — widely-spaced inharmonic partials |

The **MInharm** parameter (0–100%) spreads the ratios away from unison: at 0% all operators play the same frequency; at 100% they spread to full ratio separation. The **MBrght** parameter controls how much of the higher operators (Op2/3/4) mix into the output — low brightness yields a clean fundamental; high brightness yields a dense metallic cluster.

## Architecture (Updated with Resonant Mode)

```
┌─────────────────────────────────────────────────────────────┐
│                    MIDI Trigger Input                       │
└─────────────────────────┬───────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│              Probability Gate (4 parallel PRNGs)            │
│         Page 1: Kick/Snare/Metal/Perc probabilities         │
└─────────────────────────┬───────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────┐
│               NEON Parallel Voice Processing                │
│         (4 voices with engine masks for efficiency)         │
└─────────────────────────┬───────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        ENGINE ARRAY (5 engines)                       │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐       │
│  │  KICK   │ │  SNARE  │ │  METAL  │ │  PERC   │ │RESONANT│       │
│  │2 params │ │2 params │ │2 params │ │2 params │ │3 params │       │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘       │
└─────────┼──────────┼──────────┼──────────┼───────────┼───────────────┘
          ▼          ▼          ▼          ▼           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      VOICE ALLOCATION MATRIX                         │
│            (Param 21 determines mapping to voices)                   │
│         Voice 0 → Engine A, Voice 1 → Engine B, etc.                 │
└─────────────────────────────────┬───────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    ENVELOPE ROM (Page 6)                             │
│              128 predefined ADR curves, selected by param 20         │
└─────────────────────────────────┬───────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    LFO MODULATION MATRIX (Pages 4-5)                 │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ LFO1: Shape(0-8) + Rate + Target(0-10) + Depth(-100-100)    │   │
│  │ LFO2: Shape(0-8) + Rate + Target(0-10) + Depth(-100-100)    │   │
│  └─────────────────────────────────────────────────────────────┘   │
│         ↓            ↓            ↓            ↓            ↓       │
│    Pitch Mod  Index Mod   Env Mod  Res/NoiseMx  MetalGate          │
└─────────────────────────────────┬───────────────────────────────────┘
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      AUDIO OUTPUT (Stereo)                           │
│                     Mixed 4-voice sum                                │
└─────────────────────────────────────────────────────────────────────┘
```

## NEON Optimization Strategy

All engines process 4 voices in parallel using NEON intrinsics:

```c
// Process 4 voices simultaneously
float32x4_t phases = vld1q_f32(&voice[0].phase);
float32x4_t freqs = vld1q_f32(&voice[0].freq);
float32x4_t outputs = neon_sin(phases);
```

The voice allocation system uses **engine masks** for efficient parallel processing:

```c
// Each engine processes only the voices assigned to it
float32x4_t kick_out = kick_engine_process(&synth->kick, env,
                                           synth->engine_mask[ENGINE_MODE_KICK]);
```

## Literature References

1. **Lazzarini, V. (2017).** *Computer Music Instruments: Foundations, Design and Development*. Springer.
   - **Section 4.10.3**: Resonant synthesis using summation formulae
   - **Key insight**: Single-sided and double-sided combinations model resonant filters

2. **Chowning, J. (1973).** "The Synthesis of Complex Audio Spectra by Means of Frequency Modulation." *Journal of the Audio Engineering Society*.
   - **Key insight**: FM percussion fundamentals

3. **Kirby, T. & Sandler, M. (2020).** "Advanced Fourier Decomposition for Realistic Drum Synthesis." *DAFx Conference*.
   - **Key insight**: RDFT-based drum analysis and synthesis

## File Structure

```
your_project/
├── header.c              # Parameter definitions (updated with voice allocation)
├── unit.cc               # SDK glue code
├── synth.h               # Integration class
├── config.mk             # NEON compiler flags
├── fm_perc_synth.h       # Main synth with voice allocation
├── fm_presets.h          # 23 presets (8 original + 4 resonant + 7 LFO-feature + 3 Gong-character)
├── resonant_synthesis.h  # NEW: Resonant engine
├── kick_engine.h         # Kick engine
├── snare_engine.h        # Snare engine
├── metal_engine.h        # Metal engine
├── perc_engine.h         # Perc engine
├── lfo_enhanced.h        # LFO system with 10 targets (0-10)
├── envelope_rom.h        # 128 envelope shapes
├── prng.h                # NEON PRNG
├── sine_neon.h           # NEON sine approximation
├── smoothing.h           # Parameter smoothing
├── fm_voices.h           # Voice structures
├── midi_handler.h        # MIDI handling
├── constants.h           # Central constants
├── tests.h               # Unit tests
└── benchmark.h           # Performance benchmarks
```

## Memory Usage Estimate

| Component | Size |
|-----------|------|
| FM Engines (5) | ~2.5 KB |
| LFO System | ~0.5 KB |
| Envelope ROM | ~1 KB |
| Parameter storage | ~0.5 KB |
| **Total State** | **~4.5 KB** |
| Code Size | ~10-12 KB |
| Stack Usage | ~1 KB |

**Total**: Well within drumlogue's limits (< 64 KB)

--

# Testing
## The test suite now covers:
1. Voice Allocation - No duplicates, resonant appears at most once
2. Probability - Statistical accuracy of PRNG
3. Morph Parameter - Correct range mapping per mode
4. Engine Ranges - Parameter validation
5. Integration - Full system coordination

## The benchmark suite measures:
1. Division Operations - Comparing direct vs reciprocal methods
2. Engine Performance - Cycle estimates vs targets
3. Memory Usage - ROM/RAM estimates
4. Allocation Overhead - Confirming negligible per-sample cost

### Run all tests
./run_unit_tests.sh all

### Run specific test
./run_unit_tests.sh alloc      # Voice allocation tests only
./run_unit_tests.sh prob       # Probability tests only
./run_unit_tests.sh morph      # Morph parameter tests only

### Run all benchmarks
./run_benchmarks.sh all

### Run specific benchmark
./run_benchmarks.sh division   # Division operation benchmarks
./run_benchmarks.sh engines    # Engine performance estimates
./run_benchmarks.sh memory     # Memory usage estimates

### Using make targets
make test           # Run all tests
make bench          # Run all benchmarks
make test-alloc     # Run allocation tests only
make bench-division # Run division benchmarks only
# README.md - OmniPress Master Compressor for KORG drumlogue

## Overview

**OmniPress** is a character master bus compressor for the KORG drumlogue, loosely inspired by the **Eventide Omnipressor** and **Empirical Labs EL8 Distressor**. It features **three distinct compression modes** in a single unit, taking advantage of the drumlogue's 4-channel master effect input for external sidechain processing.

### Key Features

| Feature | Description |
|---------|-------------|
| **3 Compression Modes** | Standard, Distressor, Multiband |
| **External Sidechain** | 4-channel input for ducking/pumping — add 4 to the DETECT parameter (ID 11) to key from SC L/R instead of the main bus. In Multiband the key is split by its own crossover, so it ducks the bands the key actually occupies |
| **Drive/Wavefolder** | 5 distortion modes from soft clip to sub-octave |
| **Overlord EQ** | 3-band semi-parametric EQ (Bass/Treble/Presence) in the dynamics chain |
| **NEON Optimization** | Fully vectorized for ARM Cortex-A7 |
| **22 User Parameters** | Spread across 6 control pages |
| **24dB/oct Crossover** | Linkwitz-Riley filters for multiband mode |

---

## Compression Modes

### Mode 0: Standard Compressor
A versatile, clean compressor with all the essentials:
- **Threshold**: -60 to 0 dB
- **Ratio**: 1:1 to 20:1
- **Attack**: 0.1 to 100 ms
- **Release**: 10 to 2000 ms
- **Soft/Hard Knee** selectable
- **Peak/RMS detection** with blend

### Mode 1: Distressor Mode
Emulates the Empirical Labs EL8 Distressor with its unique character:

| Feature | Implementation |
|---------|---------------|
| **8 Ratios** | 1:1 (warm), 2:1, 3:1, 4:1, 6:1, 10:1 (opto), 20:1, NUKE |
| **Distortion Modes** | Dist 2 (2nd harmonic), Dist 3 (3rd harmonic), Both |
| **Opto Mode** | Extended release times up to 20 seconds |
| **NUKE Mode** | Brick-wall limiting with 40dB+ reduction |
| **1:1 Warm Mode** | Harmonic enhancement without compression |

### Mode 2: Multiband Compressor
3-band compression with independent controls:

| Band | Frequency Range | Independent Controls |
|------|----------------|---------------------|
| **Low** | 20 - 250 Hz | Threshold, Ratio, Makeup |
| **Mid** | 250 - 2500 Hz | Threshold, Ratio, Makeup |
| **High** | 2500 - 20 kHz | Threshold, Ratio, Makeup |
| **All** | Full range | Global adjustments |

Features:
- **Linkwitz-Riley 24dB/oct** crossovers (phase neutral)
- **Solo/Mute** per band
- **Independent attack/release** per band
- **Gain reduction meters** per band (future)

---

## Drive/Wavefolder (5 Modes)

The drive stage offers 5 distinct character modes, controllable via the DRIVE parameter (0-100%):

| Mode | Name | Description | Character |
|------|------|-------------|-----------|
| **0** | Soft Clip | Tanh approximation | Tube-like saturation |
| **1** | Hard Clip | Brick-wall limiter | Digital distortion |
| **2** | Triangle Folder | Wavefolding | Synth-like aggression |
| **3** | Sine Folder | Sinusoidal folding | Smooth, complex harmonics |
| **4** | Sub-Octave | Zero-crossing square wave | Gritty, synth bass |

The drive amount controls both the input gain to the waveshaper and the dry/wet blend for parallel processing.

---

## Parameter Reference

OmniPress has **22 parameters** across 6 pages.

### Page 1: Core Dynamics

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 0 | THRESH | -60.0 to 0.0 dB | Compression threshold (x0.1 dB resolution) |
| 1 | RATIO | 1.0 to 20.0 | Compression ratio (x0.1 resolution) |
| 2 | ATTACK | 0.1 to 100.0 ms | Attack time (x0.1 ms resolution) |
| 3 | RELEASE | 10 to 2000 ms | Release time |

### Page 2: Character & Output

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 4 | MAKEUP | 0.0 to 24.0 dB | Output makeup gain (x0.1 dB resolution) |
| 5 | DRIVE | 0 to 100% | Drive/wavefolder amount |
| 6 | MIX | -100 to +100 | Dry/Wet balance (-100=dry, 0=balanced, +100=wet) |
| 7 | SC HPF | 20 to 500 Hz | Sidechain high-pass filter cutoff |

### Page 3: Mode & Overlord EQ

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 8 | COMP MODE | 0–2 | 0=Standard, 1=Distressor, 2=Multiband |
| 9 | BASS | 0–100% | Overlord EQ: low-shelf gain/cut |
| 10 | TREBLE | 0–100% | Overlord EQ: high-shelf gain/cut |
| 11 | PRESENCE | 0–100% | Overlord EQ: upper-mid presence boost |

> **Overlord EQ** is a 3-band semi-parametric EQ applied in the dynamics chain. At 50% each band is flat (unity). Below 50% cuts, above 50% boosts.

### Page 4: Distressor Parameters

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 12 | DstrDIST | 0–4 | Distressor harmonic distortion: 0=None, 1=Dist2 (2nd harm), 2=Dist3 (3rd harm), 3=Both, 4=Wave |
| 13 | DstrRATIO | 0–7 | Distressor ratio selection: 0=1:1 (warm), 1=2:1, 2=3:1, 3=4:1, 4=6:1, 5=10:1 (opto), 6=20:1, 7=NUKE |

### Page 5: Multiband Band Controls

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 14 | MBand | 0–6 | Band selector: 0=Low, 1=Mid, 2=High, 3=Low+Mid, 4=Low+High, 5=Mid+High, 6=All |
| 15 | MBndThr | -60.0 to 0.0 dB | Per-band threshold (x0.1 dB resolution) |
| 16 | MBndRto | 1.0 to 20.0 | Per-band compression ratio |
| 17 | MBndAtk | 0.1 to 100.0 ms | Per-band attack time |

### Page 6: Multiband Output Controls

| Param | Name | Range | Description |
|-------|------|-------|-------------|
| 18 | MBndRtoRel | 10 to 2000 ms | Per-band release time |
| 19 | MBndMkp | 0.0 to 24.0 dB | Per-band makeup gain |
| 20 | MBndMut | 0–1 | Mute selected band (0=active, 1=muted) |
| 21 | MBndSOl | 0–1 | Solo selected band (0=normal, 1=soloed) |

---

## Signal Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         INPUT (4-channel)                         │
│              [Main L, Main R, Sidechain L, Sidechain R]           │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      SIDECHAIN SELECT                             │
│              External (SC L/R) or Internal (Main L/R)             │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      SIDECHAIN HPF (20-500 Hz)                    │
│                    12dB/oct Bessel filter                         │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     ENVELOPE DETECTOR                             │
│              Peak / RMS / Blend with attack/release               │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                        MODE SELECTOR                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │  STANDARD    │  │  DISTRESSOR  │  │  MULTIBAND   │           │
│  │  • Ratio     │  │  • 8 Ratios  │  │  • Crossover │           │
│  │  • Knee      │  │  • Opto      │  │  • 3 Bands   │           │
│  │  • Smoothing │  │  • Harmonics │  │  • Indep Comp│           │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘           │
└─────────┼─────────────────┼─────────────────┼─────────────────────┘
          ▼                 ▼                 ▼
    Gain Reduction    Gain Reduction    Band Gains
          └─────────────┬─────────────────┘
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│                   OVERLORD EQ (BASS/TREBLE/PRESENCE)              │
│              3-band semi-parametric tonal shaping                 │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     DRIVE / WAVEFOLDER (5 modes)                  │
│          Soft Clip │ Hard Clip │ Triangle │ Sine │ SubOctave      │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      DRY/WET MIX (Parallel)                       │
│                   Blend processed with original                    │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                       MAKEUP GAIN (0-24 dB)                       │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                         OUTPUT (Stereo)                            │
│                      Compressed and character-rich                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## NEON Optimization Strategy

All processing is vectorized to process **4 samples simultaneously**:

```c
// Load 4 stereo frames with sidechain
float32x4x4_t interleaved = vld4q_f32(in_p);
float32x4_t main_l = interleaved.val[0];  // L0, L1, L2, L3
float32x4_t main_r = interleaved.val[1];  // R0, R1, R2, R3
float32x4_t sc_l   = interleaved.val[2];  // SC L0, SC L1, SC L2, SC L3
float32x4_t sc_r   = interleaved.val[3];  // SC R0, SC R1, SC R2, SC R3

// Process all 4 samples in parallel
float32x4_t envelope = envelope_detect(&envelope_, main_l, sidechain);
float32x4_t gain_db = gain_computer_process(&gain_comp_, envelope_db, thresh_db_, ratio_);
```

Performance target: **< 200 cycles per sample** (< 2% CPU on 1GHz ARM Cortex-A7)

---

## Parameter String Display

| Parameter | Values Displayed |
|-----------|------------------|
| COMP MODE | "Standard", "Distressor", "Multiband" |
| DstrDIST | "None", "Dist 2", "Dist 3", "Both", "Wave" |
| DstrRATIO | "1:1 Warm", "2:1", "3:1", "4:1", "6:1", "Opto", "20:1", "NUKE" |
| MBand | "Low", "Mid", "High", "Low+Mid", "Low+High", "Mid+High", "All" |
| MIX | "DRY" (-100), "BAL" (0), "WET" (+100) |

---

## Bug Fixes Applied

| Bug | Symptom | Fix |
|-----|---------|-----|
| Multiband gain-reduction polarity inverted | Multiband mode acted as a downward expander (attenuated quiet signals, passed loud ones) | `excess = env_dB − threshold_dB` with clamp to ≥ 0; was `thresh − env` |
| ratio=0 hard-limit returned +100 dB | NUKE mode at ratio=0 blew up output | `gain_red = 100.0f` before negation; was `-100.0f` |
| Multiband 7 dB quieter than the other modes | Switching COMP MODE to Multiband dropped the level | `MASTER_SUM_SCALING` 0.45 → 1.0; the Linkwitz-Riley tree already reconstructs to unity |
| Detector fed `L+R` instead of `0.5*(L+R)` | Threshold 6 dB optimistic on mono material, and different from Multiband's per-band detector | Average the sidechain in `process_block` |
| MAKEUP built from `fasterpowf` | 0.25 dB insertion loss at MAKEUP=0, 24.0 dB delivered as 23.69 dB | `e_expff(dB * INV_DB_COEFF)` — 0.033 dB worst case (master, per-band, and the shelving-filter gain) |
| `linear_to_db` interpolated the raw mantissa | 0.52 dB of error landing straight on the Standard/Distressor threshold; Multiband duplicated the same bit-trick at 0.17 dB | Shared `neon_log2q_f32` in float_math.h, minimax cubic, 0.005 dB |
| Wavefolder applied the drive gain twice | Total gain `(1+19d)²` = +52 dB at DRIVE=100, pinning everything to the output limiter | Makeup is `Q_rsqrt(g)`, so net small-signal gain is `sqrt(g)` (+13 dB across the knob) |
| Triangle folder inverted polarity | `y = -x` throughout the linear region, so the wet path cancelled the dry one at partial MIX (-37 dB at BAL) | Return `1 - |…|` instead of `|…| - 1` |
| Triangle folder never folded negative peaks | `vcvtq_s32_f32` truncates toward zero, so the modulo went negative; mode was indistinguishable from Hard clip | Floor the quotient before the modulo |
| Sine folder used a 2-term Taylor series over ±π | Returned -2.03 instead of 0 at the fold; fundamental collapsed above DRIVE 15 | `sin_ps` from float_math.h, whose Cephes range reduction lets the fold keep folding — no clamp needed |
| Distortion types not level-matched | Sine ran +3.9 dB hot, SubOct 2 dB quiet, at DRIVE=0 | Sine scaled by 2/π, SubOct mix renormalised — every shaper now has unity small-signal gain |
| DRIVE=1 did nothing; DRIVE=2 stepped ~2 dB | Gate `drive_ > 0.01f`, plus the Overlord EQ never ran below DRIVE=2 | DRIVE=0 takes the EQ-only path; the tube blend fades in over the bottom tenth of the knob |
| SLOPE evaluated against the previous COMP MODE | A host replaying parameters in ID order sets SLOPE (ID 1) before COMP MODE (ID 8), so the Distressor ratio stayed at its 4:1 init | `k_compressor_mode` re-applies the stored SLOPE |
| DstrDist max was 9 | Value 9 is rejected by `setParameter` and has no display string | Max is 8 in `header.c` |
| Peak detector latched | Hold counter incremented once per 4-sample block, so the "10 ms hold" was 417 ms and expiry applied a single 0.999 step — ~0.009 dB of decay per 417 ms. Gain reduction never recovered after a transient and RELEASE did nothing in Peak mode, the default | Rectify, then let the attack/release one-pole provide ballistics, with the intended 10 ms hold implemented in samples |
| Blend detection was 0.3 × RMS | Its branch read `peak_hold`, which only the Peak branch wrote — and a `switch` runs one branch, so peak stayed 0 and the envelope sat 10.5 dB low, pivoting into upward gain | Blend derives peak locally |
| Detector and Distressor gain smoother ran 4× slow | State was `float32x4_t`, giving each lane its own history advanced once per block, so per-sample coefficients were applied at 12 kHz | Sequential scalar state, as the crossovers and `standard_process` already use |
| External sidechain unreachable | `use_external_sc_` was only ever assigned 0; the 4-channel input the unit asks for could not be selected | DETECT + 4 selects it (all 24 SDK parameter slots were already taken). Multiband splits the key through its own mono crossover so it ducks per band |

Verified by `test_levels.cpp`, which drives the real `MasterFX::Process()` loop:

```
g++ -std=c++14 -O2 -I test_portable -I . -I ../common -o test_levels test_levels.cpp -lm
./test_levels
```

All three modes now measure 0.00 dB insertion gain, and all nine distortion
types sit within 0.9 dB of each other at DRIVE=0.

## Future Expansion

The architecture supports easy addition of:

1. **More band parameters** (Attack, Release per band)
2. **Knee control** (0-100% softness)
3. **Detection mode** (Peak/RMS/Blend)
4. **Stereo link** adjustment
5. **Lookahead** (up to 10ms)
6. **Sidechain listen** mode
7. **8 factory presets** for common use cases

---

## Technical Specifications

| Specification | Value |
|---------------|-------|
| Sample Rate | 48 kHz fixed |
| Input Channels | 4 (Main L/R + Sidechain L/R) |
| Output Channels | 2 (Stereo) |
| Parameters | 22 |
| CPU Target | < 2% @ 1GHz |
| Memory | ~4 KB |
| Crossover | Linkwitz-Riley 24dB/oct |
| Drive Modes | 5 types |

---

## Credits & Inspiration

- **Eventide Omnipressor** (1970s) - Reverse compression concept
- **Empirical Labs EL8 Distressor** - Ratio modes and harmonic distortion
- **SSL Console** - Multiband architecture
- **SHARC Audio Elements** - DSP building block patterns

---

**OmniPress** brings studio-grade dynamics processing to the KORG drumlogue, with three compressors in one and enough character to satisfy any genre.
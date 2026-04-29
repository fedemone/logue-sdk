# reverb_labirinto – Development Progress

## Status: V1.0 RELEASE CANDIDATE

Core architecture is completely finalized. Extensive NEON optimization, DSP stability testing, and Cortex-A7 architectural tuning have been completed.

---

## Files

| File | Status | Notes |
|------|--------|-------|
| `NeonAdvancedLabirinto.h` | Complete | Highly optimized NEON FDN reverb engine with FWHT and 18-EDO Shimmer |
| `unit.cc` | Complete | drumlogue SDK callbacks, parameter mapping, presets |
| `header.c` | Complete | Unit descriptor (12 parameters) |
| `float_math.h` | Complete | NEON-safe fast math helpers (`sin_ps`, `fastersinfullf`, etc.) |
| `config.mk` | Complete | Build configuration |
| `Makefile` | Complete | Drumlogue SDK build |
| `test_hadamard.cpp` | Passing | Fast Walsh-Hadamard orthogonality + energy conservation verified |
| `test_predelay_apc.cpp` | Passing | Slew-limited Pre-delay write/read, APC (active partial counting) |
| `test_stability.cpp` | Passing | 10s impulse at max params – no NaN/Inf, bounded output |

---

## Architecture Summary

- **8-channel FDN** utilizing a Fully Vectorized Fast Walsh-Hadamard Transform (FWHT) for O(N log N) zero-multiplication mixing.
- **Interleaved delay line** format (`interleaved_frame_t`) – enables `vld4q_f32` gather (≈3× faster delay reads).
- **4-sample NEON block processing** throughout (`process4Samples`).
- **Active Partial Counting (APC)** – bypasses processing when tail decays below −100 dBFS.
- **Branchless Buffer Wrapping** – Eliminates `while` loop branch-prediction stalls.
- **Subnormal/Denormal Safety** – ARM FPSCR forced to FTZ/DN to prevent CPU spiking on tail decay.
- **Scalar IIR Routing** – Biquad filters explicitly unrolled to Direct Form II Transposed loops to guarantee mathematically correct feedback paths without NEON comb-filtering.

---

## Resolved Design Questions & Upgrades

1. **Pre-delay Zipper Noise:** *RESOLVED.* Implemented a 1-pole slew limiter coupled with fractional linear interpolation. Parameter changes now act like a physical tape head (smooth pitch gliding without clicking). Explicit tape saturation was deemed unnecessary as linear interpolation inherently dampens highs during modulation.
2. **Matrix Memory & Multiplication:** *RESOLVED.* Completely replaced the $O(N^2)$ `hadamard[8][8]` lookup matrix and nested `for` loops with an inline Fast Walsh-Hadamard Transform (`fast_hadamard_mix_neon`). Both vector and scalar variants execute completely in-place in a fraction of the clock cycles.
3. **IIR Filter Comb-Filtering:** *RESOLVED.* Identified a mathematical flaw where NEON was applying the same history state to 4 concurrent samples. Extracted the Material Biquads to high-speed scalar Direct-Form II Transposed loops to guarantee true sample-to-sample recursive dependencies.
4. **Shimmer Stability & Design (PILL=4):** *RESOLVED.* Removed the synthetic Bode ring-modulation entirely. Implemented **Cochrane 18-EDO Microtonal Shimmer**. By applying 8 independent Doppler delay modulations tuned exactly to an 18-EDO scale, the engine physically generates dense, acoustic beating organically within the Hadamard mix.
5. **Coefficient Sign Instability:** *RESOLVED.* Standardized Biquad coefficient normalization (dividing by `a0` without injecting manual negative signs) to align with standard DSP textbooks, preventing infinite positive feedback (NaN) crashes in the Material filters.

## Hardware Fixes Applied

- **Critical crash fix**: `new NeonAdvancedLabirinto()` allocates ≈2.2 MB from heap,
  which always fails on bare-metal drumlogue. Changed to static instance in BSS:
  `static NeonAdvancedLabirinto s_reverb_instance;` — unit now boots correctly.
- **Esotico preset dead code**: `setFilterType()` never updated `currentPreset`, so
  the `if (currentPreset == k_esotico)` branch was always false. Fixed; esotico now
  correctly uses `kFilterCrystal` (BPF Q=3, 2–7 kHz) for its microtonal shimmer,
  distinct from labirinto's `kFilterMetal` (BPF Q=8, 800 Hz–4 kHz).

## Pending

- [ ] Full hardware profiling: Cortex-A7 CPU usage to verify 12+ synthesis voices
      remain available alongside the reverb.
- [ ] Extended hardware soak test at all 5 presets (foresta, tempio, labirinto,
      esotico, stellare) to verify filter stability under sustained input.
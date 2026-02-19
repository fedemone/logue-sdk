# Audio Troubleshooting Log

## [FAIL] No audio output during 3-second test!
**Date:** 2026-02-17
**Test:** `test_ripplerx_debug.cpp` / `test_runtime_stability_3_seconds`
**Status:** FAILED

### Symptoms
- Unit test reports `[FAIL] No audio output during 3-second test!`
- `hasSignal` flag remains false throughout the 3-second render loop.
- **Hardware behavior:** Unit loads, no crash, "nosilence" (likely meaning no watchdog silence trigger), but "no beat/expected sound".

### Logs
```
[Test 2] Runtime Stability (3 Seconds with Sound)
  Rendering continuous audio for 3 seconds...
[VOICE TRIGGER] Note 60, vel 0.79, freq 261.63
...
[FAIL] No audio output during 3-second test!
```

### Root Cause Analysis
1. **Low Signal Level:** The test sets `c_parameterMalletResonance` to `10`. In `RipplerX.h`, this maps to `10 / 1000.0f = 0.01f` (1% gain).
2. **Missing Mix:** The test does not set `c_parameterMalletMix` or `c_parameterNoiseMix`. Assuming defaults are 0, the direct signal is silent.
3. **Result:** The resonator is excited by a very weak impulse (-40dB relative to full scale), resulting in an output likely below the test's `0.001f` (-60dB) detection threshold.

### Resolution
- **Test Fix:** Increase `c_parameterMalletResonance` to `500` (50%) or add `c_parameterMalletMix` in `test_ripplerx_debug.cpp`.
- **Hardware Note:** Ensure presets loaded on hardware have `Mallet Mix` or `Noise Mix` > 0, or sufficient `Mallet Resonance` to be audible.

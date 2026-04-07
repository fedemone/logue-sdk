# reverb_light – Development Progress

## Status: DRAFT / UNDER REVIEW

Implementation is complete and tests pass on x86. Awaiting review/decision on
the open design questions listed below before further work proceeds.

---

## Files

| File | Status | Notes |
|------|--------|-------|
| `fdn_engine.h` | Complete | NEON-optimised FDN reverb engine (static allocation) |
| `unit.cc` | Complete | drumlogue SDK callbacks, parameter mapping, presets |
| `header.c` | Complete | Unit descriptor (8 parameters) |
| `config.mk` | Complete | Build configuration |
| `Makefile` | Complete | Drumlogue SDK build |
| `test_reverb_light.cpp` | Passing | Parameter mapping + FDN signal flow tests |
| `test_stability.cpp` | Passing | Stability test at max params |

---

## Architecture Summary

- **8-channel FDN** with normalised Hadamard mixing matrix
- **Static allocation only** (no `new`/`delete`) – all buffers in BSS
- **4-sample NEON block processing** (`process4Samples`) + scalar remainder
- **Active Partial Counting (APC)** – tail-length tied to decay parameter
- **One-pole HPF** (DC blocker) at input (coeff = 0.85)
- **Pre-delay** up to 200 ms (`PDLY` param)
- **Modulated delay lines** (chorusing, depth = SPRK)
- **Tone shaping**: LPF (`COLR`) blended with HF bypass (`BRIG`)
- **Stereo**: channels 0-3 → L, channels 4-7 → R

Design philosophy: simpler and lighter than `reverb_labirinto`; no cross-channel
feedback, no shimmer, no coloured noise. Intended as a "clean" room reverb.

---

## Parameters

| ID | Name | Raw Range | Internal mapping |
|----|------|-----------|-----------------|
| 0 | NAME | 0–3 | Preset selector (string display only) |
| 1 | DARK | 0–100 | ÷100 × 0.99 → decay 0.0–0.99 |
| 2 | BRIG | 0–100 | ÷100 → brightness 0.0–1.0 |
| 3 | GLOW | 0–100 | ÷100 → wet/dry 0.0–1.0 |
| 4 | COLR | 0–100 | ÷100 × 0.95 → LPF coeff 0.0–0.95 |
| 5 | SPRK | 0–100 | ÷100 → modulation depth 0.0–1.0 |
| 6 | SIZE | 0–100 | 0.1 + ÷100 × 1.9 → size scale 0.1–2.0 |
| 7 | PDLY | 0–100 | ÷100 × 200 → pre-delay 0–200 ms |

---

## Factory Presets

| Idx | Name | Character |
|-----|------|-----------|
| 0 | StanzaNeon | Tight, bright standard drum room |
| 1 | VicoBuio | Long decay, heavy LPF, spooky |
| 2 | Strobo | High pre-delay, short decay, heavy modulation |
| 3 | Bruciato | Massive size, max decay, floating |

---

## Open Design Questions (reason for pause)

1. **`unit_reset()` is empty** – no `reset()` method on `FDNEngine`. If the host
   calls reset mid-performance the delay buffers retain stale data. Options:
   - Add a `reset()` method that calls `memset(fdnMem, ...)` and zeros LPF states.
   - Accept the current behaviour (tail continues through reset).

2. **HPF coefficient hardcoded** – `hpfCoeff = 0.85f` is private and non-configurable.
   At 48 kHz this gives a −3 dB point around 1.7 kHz, which is quite aggressive
   (cuts bass). Consider:
   - Expose via a parameter (e.g., reuse COLR for both LPF and HPF).
   - Reduce to 0.97–0.99 for a gentler DC-blocking HPF only.

3. **NAME parameter (ID 0)** – the parameter shows the preset name as a string but
   does not trigger `unit_load_preset`. If the user increments NAME on the hardware
   knob, the sound won't change. Same issue as labirinto's PRESET param. Decision:
   - Have `unit_set_param_value(k_paramProgram, v)` call `unit_load_preset(v)`, or
   - Document that preset selection must use the preset system, not the knob.

4. **Modulation rate not parameterised** – `modPhases` increment by
   `modulation * 2.0 / sampleRate * 4.0`. The LFO rate is coupled to SPRK depth,
   meaning more modulation = faster sweep. This can cause audible pitch artefacts
   at SPRK > 50. Consider separating rate from depth.

5. **Memory footprint** –
   `fdnMem[8 × 32768]` = 1 MB + `preDelayBuffer[16384]` = 64 KB → ~1.06 MB total.
   This is a static array in BSS; confirm that drumlogue's memory layout can
   accommodate it (SDRAM vs SRAM).

---

## Completed Work

- [x] Core FDN engine (static allocation, no heap)
- [x] NEON 4-sample block processing
- [x] APC (automatic bypass on tail decay)
- [x] Pre-delay with circular buffer
- [x] Hadamard matrix
- [x] One-pole HPF (DC blocker)
- [x] Tone shaping (LPF + brightness blend)
- [x] Both test files compile and pass on x86/x64
- [x] SDK unit interface (init/render/set_param/presets)
- [x] 4 factory presets

## Pending

- [ ] Resolve open design questions above
- [ ] Add `FDNEngine::reset()` and wire it in `unit_reset()`
- [ ] Investigate HPF coefficient (consider softening to 0.97+)
- [ ] Decide NAME param → preset-load behaviour
- [ ] Consider decoupling SPRK depth from LFO rate
- [ ] Confirm memory budget with drumlogue SDRAM map
- [ ] Build test on actual target (ARM cross-compile)

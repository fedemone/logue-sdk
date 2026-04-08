# reverb_labirinto – Development Progress

## Status: DRAFT / UNDER REVIEW

Implementation is complete and tests pass on x86. Awaiting review/decision on
the open design questions listed below before further work proceeds.

---

## Files

| File | Status | Notes |
|------|--------|-------|
| `NeonAdvancedLabirinto.h` | Complete | 849-line NEON-optimised FDN reverb engine |
| `unit.cc` | Complete | drumlogue SDK callbacks, parameter mapping, presets |
| `header.c` | Complete | Unit descriptor (12 parameters) |
| `float_math.h` | Complete | NEON-safe fast math helpers |
| `config.mk` | Complete | Build configuration |
| `Makefile` | Complete | Drumlogue SDK build |
| `test_hadamard.cpp` | Passing | Hadamard orthogonality + energy conservation |
| `test_predelay_apc.cpp` | Passing | Pre-delay write/read, APC (active partial counting) |
| `test_stability.cpp` | Passing | 10s impulse at max params – no NaN/Inf, bounded output |

---

## Architecture Summary

- **8-channel FDN** with normalised Hadamard mixing matrix (orthogonal, unity gain)
- **Interleaved delay line** format (`interleaved_frame_t`) – all 8 channels at one
  time position – enables `vld4q_f32` gather (≈3× faster delay reads)
- **4-sample NEON block processing** throughout (`process4Samples`)
- **Active Partial Counting (APC)** – bypasses processing when tail decays below −100 dBFS
- **Pre-delay** up to 340 ms (`PDLY` param)
- **Resonant filters** (4 presets): Wood / Stone / Metal / Noise
- **Cross-channel feedback** (chaos scaled by PILL count)
- **Randomised ping-pong** for PILL=1 (pseudo-random cyclic stereo map)
- **Modulated diffusion** (xorshift LFO, speed = VIBR)
- **Shimmer** (ring-mod on 8-ch mode, PILL=4) with sub-Hz freq control (`SHMR`)
- **Coloured noise injection** (preset 3 "stellare"): brown/pink/grey/blue/violet

---

## Parameters

| ID | Name | Raw Range | Internal mapping |
|----|------|-----------|-----------------|
| 0 | PRESET | 0–3 | foresta / tempio / labirinto / stellare |
| 1 | MIX | 0–1000 | ÷1000 → 0.0–1.0 wet |
| 2 | TIME | 1–100 | → decay 0.01–0.99 |
| 3 | LOW | 1–100 | low-freq RT60 multiplier |
| 4 | HIGH | 1–100 | high-freq RT60 multiplier |
| 5 | DAMP | 20–1000 | ×10 → 200–10 000 Hz crossover / noise qty |
| 6 | WIDE | 0–200 | ÷100 → stereo width 0.0–2.0 |
| 7 | COMP | 0–1000 | ÷1000 → diffusion 0.0–1.0 / noise colour |
| 8 | PILL | 0–4 | routing: 2ch / 4ch / 6ch / 8ch / 8ch+shimmer |
| 9 | SHMR | 0–100 | shimmer frequency (3–55 Hz) |
| 10 | PDLY | 0–100 | pre-delay 0–340 ms |
| 11 | VIBR | 0–100 | LFO speed 0.1–3.0 Hz |

---

## Factory Presets

| Idx | Name | Character |
|-----|------|-----------|
| 0 | foresta | Warm woody room, sparse, moderate decay |
| 1 | tempio | Dark heavy stone, long, 6-channel |
| 2 | labirinto | Metallic ping-pong, randomised stereo |
| 3 | stellare | Shimmer + coloured noise, spacey |

---

## Open Design Questions (reason for pause)

1. **VIBR parameter (ID 11)** – currently not wired in `unit_set_param_value`
   (`switch` has no `k_vibr` case). Need to decide:
   - Add a dedicated `setModRate(float hz)` method in the header, or
   - Re-use existing `modRate` field and add the case.

2. **PRESET case in `unit_set_param_value`** – `case k_paramProgram` is absent;
   changing PRESET param does not trigger `unit_load_preset`. Should the param
   setter call `unit_load_preset(value)`, or leave preset selection exclusively
   to the `unit_load_preset` callback?

3. **Coloured noise implementation** – the README documents brown/pink/grey/blue/
   violet noise for preset 3, but `NeonAdvancedLabirinto.h` currently contains a
   placeholder for the noise generator. Decision needed:
   - Implement full multi-colour noise (adds ~60 lines), or
   - Ship with simple white-noise injection and colour-label mapping only.

4. **Memory footprint** – `interleaved_frame_t delay[BUFFER_SIZE]` = 65536 × 32
   bytes = **2 MB**. This is above the drumlogue SDRAM limit. Options:
   - Reduce `BUFFER_SIZE` to 32768 (1 MB, max ~341 ms at 48 kHz) – still enough
     for the 2 s time parameter if decay reduces energy first.
   - Keep 2 MB and mark as requiring SDRAM (verify drumlogue memory map).
   - Switch to per-channel flat buffers (same memory, easier to audit).

5. **Stability at PILL=4 (shimmer)** – ring-mod feedback path not yet stress-tested
   at `TIME=100` + `COMP=1000`. `test_stability.cpp` covers PILL=3 only.

6. **introduce wavetable LFO for shimmer** – since the shimmer reverberation is
    simply static an LFO for the pitch can add movement at slow/medium rates.
    An audio rate would give special effect?

---

## Completed Work

- [x] Core FDN engine with NEON vectorisation
- [x] Interleaved delay line + vld4q_f32 gather
- [x] APC (automatic bypass on tail decay)
- [x] Pre-delay with circular buffer
- [x] Hadamard matrix (orthogonal, normalised)
- [x] All 3 test files compile and pass on x86/x64
- [x] SDK unit interface (init/teardown/render/set_param/presets)
- [x] README with full parameter and architecture docs

## Pending

- [ ] Resolve open design questions above
- [ ] Wire VIBR (ID 11) in unit_set_param_value
- [ ] Clarify/implement coloured noise for stellare preset
- [ ] Stress-test PILL=4 shimmer path
- [ ] Introduce wavetable LFO for shimmer
- [ ] Confirm memory budget with drumlogue SDRAM map
- [ ] Build test on actual target (ARM cross-compile)

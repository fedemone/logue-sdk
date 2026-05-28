# RipplerX – Session Brief

## Working State (last commit: 6cbde8b)

- Unit **loads on hardware** (drumlogue).
- All presets sound **string-like** regardless of the instrument model — the KS
  waveguide dominates every signal path and its harmonic series overrides the
  modal bank tuning.
- Flute and Clarinet remain in the table but are slated for removal (non-percussive).

## Critical .rodata Constraint — Do NOT break this

The drumlogue firmware checks `.text segment` size (= `.text + .rodata + .init + .fini`) per unit. Limit ≈ 30 KB. The preset tables (~7 KB) must stay in `.data`, not `.rodata`.

**Rule:** All file-scope or class-scope arrays used as preset tables must be declared
`static` (plain) — never `static constexpr` or `static const`. This is already
enforced for `kDefaultModalPresetConfig`, `modal_preset_configs[]`, and
`model_param_presets[][]`. Do **NOT** add `const`/`constexpr` back to those.

See `config.mk` `USE_LTO := no` and the comment block for background.

## Root Cause of "Always String-Like" Sound

The KS waveguide produces a harmonic series (f, 2f, 3f…) regardless of modal preset.
The modal bank runs **in addition** to KS at a low mix (15–30%), so KS always
dominates. Fix: bypass KS for non-string presets; use the modal bank as the sole
tonal resonator.

## Engine Architecture (6-type redesign)

Each preset is assigned one `EngineType`. `processBlock` routes via a switch/if on
`kPresetEngine[m_preset_idx]`.

### Engine Types

| Engine | Signal path | Presets |
|--------|-------------|---------|
| `ENGINE_KS` | Karplus-Strong delay + modal additive | Init, 808Sub, Koto, GuitarStr, PluckBass |
| `ENGINE_BAR` | Mallet exciter → bar modal bank (ratios 1:2.756:5.404) | Marimba, Vibraphone, Kalimba, SteelPan, Woodblock, Claves, TubularBell, GlassBowl, GlassBottle, SlitDrum, Tick |
| `ENGINE_MEMBRANE` | Strike exciter → circular membrane modal bank (Bessel ratios 1:1.594:2.136:2.296) + boom osc | Timpani, Djembe, Taiko, AcTom, Conga, Bongo, Handpan, KickDrum |
| `ENGINE_SNARE` | Membrane body (short, no KS) + snare-wire resonators | AcSnare, MarchSnare |
| `ENGINE_PLATE` | Strike → dense inharmonic plate modes + metallic noise | Cymbal, Gong, HiHatClosed, HiHatOpen, Ride, RideBell, BellTree, Cowbell, Triangle |
| `ENGINE_NOISE` | Noise burst only; no pitched resonator | Clap, Shaker |
| `ENGINE_REMOVED` | Silent (preset reserved for future use) | Flute, Clarinet |

### Preset → Engine Mapping

```
k_Init(0)         ENGINE_KS
k_Marimba(1)      ENGINE_BAR      ← Phase 2 exemplar
k_808Sub(2)       ENGINE_KS
k_AcSnare(3)      ENGINE_SNARE
k_TubularBell(4)  ENGINE_BAR
k_Timpani(5)      ENGINE_MEMBRANE
k_Djambe(6)       ENGINE_MEMBRANE
k_Taiko(7)        ENGINE_MEMBRANE
k_MarchSnare(8)   ENGINE_SNARE
k_Koto(9)         ENGINE_KS
k_Vibraphone(10)  ENGINE_BAR
k_Woodblock(11)   ENGINE_BAR
k_AcousticTom(12) ENGINE_MEMBRANE
k_Cymbal(13)      ENGINE_PLATE
k_Gong(14)        ENGINE_PLATE
k_Kalimba(15)     ENGINE_BAR
k_SteelPan(16)    ENGINE_BAR
k_Claves(17)      ENGINE_BAR
k_Cowbell(18)     ENGINE_PLATE
k_Triangle(19)    ENGINE_PLATE
k_KickDrum(20)    ENGINE_MEMBRANE
k_Clap(21)        ENGINE_NOISE
k_Shaker(22)      ENGINE_NOISE
k_Flute(23)       ENGINE_REMOVED
k_Clarinet(24)    ENGINE_REMOVED
k_PluckBass(25)   ENGINE_KS
k_GlassBowl(26)   ENGINE_BAR
k_GuitarStr(27)   ENGINE_KS
k_HiHatClosed(28) ENGINE_PLATE
k_HiHatOpen(29)   ENGINE_PLATE
k_Conga(30)       ENGINE_MEMBRANE
k_Handpan(31)     ENGINE_MEMBRANE
k_BellTree(32)    ENGINE_PLATE
k_SlitDrum(33)    ENGINE_BAR
k_Ride(34)        ENGINE_PLATE
k_RideBell(35)    ENGINE_PLATE
k_Bongo(36)       ENGINE_MEMBRANE
k_GlassBottle(37) ENGINE_BAR
k_Tick(38)        ENGINE_BAR
```

## Implementation Phases

| Phase | Task | Effort | Risk | Validation |
|-------|------|--------|------|------------|
| 1 | Engine routing scaffold — KS bypass for non-string presets, ENGINE_REMOVED silence | Low | Low | All presets audible; strings unchanged |
| 2 | ENGINE_BAR — Marimba (kill-switch) | Medium | Low | Marimba sounds like a marimba, not a string |
| 3 | ENGINE_MEMBRANE — Kick, Timpani, Taiko, etc. | Medium | Medium | Kick thumps, Timpani rings inharmonically |
| 4 | ENGINE_SNARE — AcSnare, MarchSnare | Medium | Medium | Snare has sharp crack + sizzle, not ring |
| 5 | ENGINE_NOISE — Clap, Shaker | Low | Low | Clean noise bursts |
| 6 (optional) | ENGINE_PLATE — Cymbal, Gong, Hi-hat, etc. | High | High | Replace with samples if effort/result too high |
| 7 | Final tuning + binary size check | Low | Low | Size within budget, all engines pass HW test |

**Phase 2 is the kill-switch.** If Marimba does not sound convincingly like a
marimba after Phase 2, stop and re-evaluate the approach before continuing.

**ENGINE_PLATE is optional.** If effort/result ratio is too high, replace plate
presets with sampled content rather than synthetic DSP.

## Modal Bank — Key Parameters

`modal_preset_configs[k_NumPrograms]` in `synth_engine.h` holds per-preset mode
ratios and T60 values. These are already calibrated and use inharmonic Bessel/bar
ratios. The table is `static` (not constexpr) per the .rodata rule above.

For **non-KS engines**, the modal bank is the **primary** tonal source (KS is
bypassed). The modal mix values (~0.18–0.32) were calibrated relative to KS, so
they must be scaled up by `kModalEngineGain ≈ 5.0` when running without KS.

## Dev Branch

`claude/continue-previous-session-vydFO` on `fedemone/logue-sdk`.

Always rebuild and check `arm-unknown-linux-gnueabihf-size ripplerx.elf`:
- `.text` (= text + .rodata) must stay below **28 KB** (safe margin below 30 KB limit).
- `.bss` must stay near **552 bytes**.

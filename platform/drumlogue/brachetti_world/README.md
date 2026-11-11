# Brachetti World (`BrcWorld`)

> **Disclaimer:** Brachetti World is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The ninth unit of the [Brachetti family](../tools/brachetti_equiv/README.md),
for the instruments no other family engine can make: the cuíca, the
berimbau, the balafon, the riq, the pandeiro, the shekere and the rainstick.
It is the family's shared modal engine (`brachetti_common/modal_synth.h`)
plus five layers of its own, each switched on by a preset's table.  Unlike
the other eight it was not split from the original `brachetti`: its presets
were voiced after the [Pelle e Legno](../pelle_legno/) samples.

| | |
|---|---|
| Unit name on the drumlogue | `BrcWorld` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42725764` ('BrWd') |
| Engine | modal bank + friction voice, wire, buzz, jingles, particles |
| Presets | 9 |
| Knobs | 24 (the modal units' layout) |
| ARM size (cross-built, `arm_size.sh`) | .text 17836 · .rodata 3504 · .data 4 · .bss 21200 |

## Presets

| # | Name | Root | Layers | Sound | Voiced after |
|---|---|---|---|---|---|
| 0 | Cuica | 69 | friction | cuica: the stick-slip voice, the thumb bending it up and back (the 'laugh') | `031_Cuica.wav` |
| 1 | Berimbu | 46 | wire | berimbau: a struck steel wire, its gourd swung away and back -- the 'wah' | `060_Berimbau.wav` |
| 2 | BerimHi | 48 | wire | berimbau with the coin pressed on the wire: a whole tone up, shorter | `061_BerimbauHi.wav` |
| 3 | BalafnC | 60 | bank + buzz | balafon, C4: a rosewood bar over a gourd with a buzzing mirliton | `057_BalafonC4.wav` |
| 4 | BalafnG | 67 | bank + buzz | balafon, G4 | `058_BalafonG4.wav` |
| 5 | Riq | 63 | bank + jingles | riq: a finger 'tek' on the small head, five pairs of brass jingles bouncing | `022_Riq.wav` |
| 6 | Pandeir | 53 | bank + jingles | pandeiro: the thumb on the edge, then the cupped platinelas | `030_Pandeiro.wav` |
| 7 | Shekere | 54 | bank + particles | shekere: the heel on the gourd's base, the bead net thrown | `038_ShekereHit.wav` |
| 8 | Rainstk | 72 | particles | rainstick: pebbles cascading through the thorns of a dried cactus tube | `040_Rainstick.wav` |

## The layers

All in `voice.h`; each is idle until its level in the preset's voicing row
is above 0, and its pitched frequencies follow the played note from the
preset's root (so `Pitch` and MIDI notes transpose them).

| Layer | What it models | How |
|---|---|---|
| **Friction** | the cuíca: a wet cloth rubbed along a stick fixed to the head, which drives it by stick-slip | a band-limited (polyBLEP) sawtooth whose pitch two S-curves (`brc::GlideCurve`) bend up and back — the thumb pressing, then releasing — through two head formants, with a little rubbing noise |
| **Wire** | the berimbau: a steel wire struck by a thin stick, its gourd swung away from the belly and back | a Karplus-Strong loop (one-pole loss, linear-interpolated period) struck by a Hann-windowed noise burst, under a gourd resonance swept up and back by two S-curves |
| **Buzz** | the balafon's mirliton (a spider-egg-case membrane on the gourd) rattling against the vibrating air | noise gated by how far the modal bank's own output rises above a fraction of its level at the strike, ringing a resonance: it buzzes at the bar's period and dies before the tone |
| **Jingles** | the riq's and pandeiro's pairs of brass discs | five pairs, three plate modes each drawn at every strike from the preset's band; each pair is struck by an impulse when its discs meet and bounces every few ms, quieter each time, with a short noise scrape beside it |
| **Particles** | the shekere's bead net, the rainstick's pebbles | random collisions under an attack / hold / decay envelope, each a short noise click through two shell resonances (PhISEM-style, after Perry Cook) |

A voice whose layer is still building up (the rainstick's pebbles start
slowly) is not retired by the family's level squelch while the layer plays.

## Knobs

| Page | Knobs |
|---|---|
| 1 | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | `MlltRes` · `MlltStif` · `VlMllRes` · `VlMllStf` |
| 3 | `Partls` · `Model` · `Dkay` · `Mterl` |
| 4 | `Tone` · `HitPos` · `Rel` · `Inharm` |
| 5 | `Cutoff` · `TubRad` · `Gain` · `NzMix` |
| 6 | `NzRes` · `NzFltr` · `NzFltFrq` · `Resnc` |

The 24-knob layout of the modal units, with the same mappings
([BrcTom's README](../brachetti_tom/README.md#knobs) describes each), all
REFERENCE-ANCHORED on the preset's shipped values.  On this unit:

- `Pitch`, `Velocity`, `Tone`, `Cutoff` / `Resnc` and `Gain` act on every preset (`Pitch` moves the layers' pitch too).
- The bank and exciter knobs (`MlltRes`, `MlltStif`, `Partls`, `Model`, `Dkay`, `Mterl`, `HitPos`, `Rel`, `Inharm`, `TubRad`, the `Nz*` knobs) act where a preset has a modal bank or an exciter: the balafons, riq, pandeiro and shekere.  On Cuica, Rainstk and the berimbaus (no bank, and the exciter turned down to nothing or almost: the wire's own strike is the stick) they do nothing audible.
- The layers' own settings have no knob: they are the preset.

## How the presets were voiced

As BrcTom's added presets ([its README](../brachetti_tom/README.md#how-the-added-presets-were-voiced)):
`tools/brachetti_equiv/match_samples.py match_specs.py world` walks each
preset's free values downhill against its Pelle e Legno sample (energy over
time, band balance, 40 dB decay, strongest partial and, on the cuíca, the
pitch track), comparing the voice before the master limiter; trims by the
family's body-target method (`preset_probe calib` reprints them).

Measured, preset vs sample (velocity 110, sequencer gate):

| Preset | t40, ms | strongest partial, Hz | note |
|---|---:|---:|---|
| Cuica | 375 / 380 | 746 / 744 | pitch track 488/685/747/673 vs 501/679/743/674 Hz |
| Berimbu | 425 / 420 | 467 / 589 | band balance matches; the gourd peak sits lower |
| BerimHi | 240 / 215 | 525 / 531 | |
| BalafnC | 385 / 370 | 261 / 261 | |
| BalafnG | 365 / 360 | 392 / 392 | |
| Riq | 150 / 150 | 3067 / 5950 | jingles in the right band, their strongest mode lower |
| Pandeir | 140 / 135 | 175 / 366 | the head leads where the sample's jingles do |
| Shekere | 125 / 155 | 185 / 171 | |
| Rainstk | 955 / 925 | 4016 / 4087 | |

## Build

```
build drumlogue/brachetti_world
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_world.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  Not yet tested on hardware; its CPU per block has not
been measured on ARM (`arm_cpu.sh` needs qemu).

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, their modal banks and each one's layer settings and output trim |
| `voice.h` | one voice: the exciter, the modal bank and the five layers |
| `synth_engine.h` | the strike (each layer set up from the table) and the per-sample voice; the wires' delay lines |

Shared with the other units: [`brachetti_common`](../brachetti_common/)
(`modal_synth.h` is the engine; `GlideCurve` the S-curve the friction voice
and the gourd use).

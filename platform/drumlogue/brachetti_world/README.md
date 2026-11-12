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
| ARM size (cross-built, `arm_size.sh`) | .text 18920 · .rodata 3504 · .data.rel.ro 84 · .data 4 · .bss 21200 |

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

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all nine Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Rattle` · `VlMllStf` |
| 4 | dedicated | `Partls` · `Model` · `Dkay` · `Punch` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `TubRad` |
| 6 | dedicated | `NzMix` · `NzRes` · `NzFltr` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root: the bank and every pitched layer (the cuica, the wire, the mirliton, the jingles' band, the beads' resonances) follow the played note |
| `Poly` | voice cap 1-4; every strike stacks |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset.  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

The modal engine's knobs act on the bank and the exciter as on the other modal units (see [BrcTom's README](../brachetti_tom/README.md#knobs)): on the balafons, riq, pandeiro and shekere.  Six of them also move **the layers**, each with the meaning it already has on the bank, and `Rattle`, a knob of BrcWorld's own in `VlMllRes`' slot, moves only the layers.  That is what makes them useful on Cuica, Rainstk and the berimbaus, which have no bank and almost no exciter.  At the preset's shipped value each mapping is a factor of exactly 1 (`Rattle` ships 0), so a preset sounds as voiced until a knob moves:

| Knob | On the layers | Up turns it |
|---|---|---|
| `Dkay` | **length**: the cuica's stroke (hold, decay, the thumb's timing); the wire's ring and the gourd's swing; the jingles' ring and the spread of their bounces; the beads' cascade (the rainstick's fall) | longer |
| `Inharm` | **bend**: the cuica's rise and fall, the depth of the berimbau gourd's wah | deeper |
| `TubRad` | **body**: the cuica's formants, the gourd, the shells, the mirliton, the jingles' band | lower: a bigger body |
| `Mterl` | **material**: the wire's brightness, the beads' click length, the cuica stick's rub noise | brighter, drier |
| `MlltStif` | **density**: the beads' collision rate, the jingles' bounce speed; the attack of the cuica's stroke and of the berimbau's stick | denser, harder |
| `MlltRes` | **presence**: the layers' level, as it is the bank's | louder |
| `Rattle` | **contact noise**: the balafons' mirliton buzz (it buzzes from a lower level), the jingles' clash (louder, longer), the beads' and the wire's dry contact (added where a preset ships none), the cuica stick's rub | rattlier; down cleaner (×¼ .. ×4) |

(`Rel`, a second and finer ring length beside `Dkay`, gave its slot to `Punch`; it plays at each preset's shipped value.)

`Inharm` has nothing to bend on the riq, the pandeiro, the shekere and the rainstick (jingles and beads have no glide).  `Partls`, `Model`, `VlMllStf` and `HitPos` act on the bank and the exciter only, so they do nothing on Cuica, the berimbaus and Rainstk.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike (a drum synth knows the sample its transient starts on, so nothing has to be detected).  **Up**, the first 5 ms pass untouched and then the body drops (τ 15 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.

It never boosts the attack.  The unit ends in a peak limiter that already takes 5-25 dB off its hard strikes and would take a boost straight back off; lowering what follows the strike is the contrast it cannot undo.  So more `Punch` is also a quieter part: raise the drumlogue's part level to compensate.  Measured at velocity 110, `Punch` +100 lowers the body by 11-15 dB with the strike's peak unchanged.  It is `brc::Punch`, shared by all nine units.

### Knob review

`VlMllRes` (velocity → noise-attack speed) did nothing on any preset: the layers have no noise attack for it to speed up, and the bank presets ship little noise.  Its slot is now `Rattle`.  `Pitch` did not reach the rainstick at all, and moved only the bank of the riq, pandeiro and shekere: the jingles, beads and mirliton now follow the played note too (at the root, nothing changes).  Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py), which moves each knob to both ends on every preset and scores how much the sound changes.

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

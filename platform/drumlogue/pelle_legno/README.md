# Pelle e Legno — organic, skin and ethnic percussion for the drumlogue

> **Disclaimer:** Pelle e Legno is an unofficial, independently developed sample kit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

*Pelle e legno*, "skin and wood": **61 one-shot percussion samples** for the
drumlogue's USER sample bank. There are hand drums from four continents, seeds,
gourds and wood, and clay, metal and wire instruments. Copy them onto the unit
over USB and they play on its six sample parts.

No sound here was recorded. Every sample is **computed** by
[`render_kit.py`](render_kit.py) from a physical model of the instrument, so no
third-party recording is inside them, and the whole kit can be regenerated,
retuned or extended from that one script. Where a reference recording was
available, the model was calibrated against it by measurement (see
[Calibration](#calibration)).

| | |
|---|---|
| Samples | 61: 32 skin, 18 organic, 11 ethnic |
| Format | 48 kHz, 32-bit float, mono WAV (the drumlogue's native format, so it converts nothing at boot) |
| Size | 6.8 MB, about 20 % of the drumlogue's 32 MB shared storage |
| Slots | `001`–`061` of the USER bank by default; any other range with `--first-index` |
| Longest | 2.6 s (Rainstick); the unit plays up to 4 s |

---

## What "a kit" can mean on the drumlogue

In USB storage mode the drumlogue exposes two things: **`Samples/`** and
**`Units/`**. Kits and programs stay inside the unit and **cannot be copied
over as files**. So this kit is delivered in two parts:

1. **The samples**, which go straight onto the unit.
2. **[Kit recipes](#kit-recipes)**, which tell you which sample goes on which
   part. Building a kit from one takes a few minutes on the unit; then save it
   with KIT SAVE.

Which parts can play these samples (owner's manual, *Sound source configuration*):

| Parts | Can play user samples? | Role in these kits |
|---|---|---|
| CH, OH, RS, CP, SP1, SP2 (digital) | **yes**, from BANK `USER` | the six voices of every recipe |
| BD, SD, LT, HT (analog) | no (only factory transient/layer PCM) | tuned low and short, they work as dundun, surdo or a second bass drum |
| MULTI (VPM / NOISE / USER) | no; it hosts one user **synth** | [Brachetti](../brachetti/) in this repo is a physical-modelling synth with `Djambe`, `Conga`, `Bongo`, `Taiko`, `Handpn`, `Kalimba`, `Marmba`, `SltDrm` and `Wodblk` presets, which makes it a seventh voice from the same families |

---

## Install

1. With the drumlogue **off**, connect its USB (TO HOST) port to the computer.
2. Hold **REC** while switching it on. It mounts as a USB drive.
3. Open the drive's `Samples/` folder and **check which slots are already
   used**. The three digits in front of each file are its slot. This kit uses
   `001`–`061` by default. If any of those are taken, render the kit into free
   slots instead of overwriting them (see [Regenerating](#regenerating-and-customising)):
   `python3 render_kit.py --first-index 41` puts it at `041`–`101`.
4. Copy **the files inside** this folder's [`Samples/`](Samples/) into the
   drive's `Samples/` folder. Copy the files themselves, not the folder.
5. Eject the drive and press **PLAY**.
6. On any of CH, OH, RS, CP, SP1 or SP2, open **SOUND SELECT**, set
   **BANK = USER** and turn **SAMPLE** to the slot number.

> The drumlogue **deletes** any file in `Samples/` whose name breaks its rules
> (`XXX_name.wav`, `XXX` = 001–128, ASCII letters, digits and
> `-_!?#$%&'()*+,:;<=>@` only). Every file here follows them.
> `python3 check_kit.py` checks a folder against those rules and against
> slots you already use (`--existing 001 002 …`) before you copy anything.

You don't have to install all 61. Each file is independent, so copy only the
instruments you want if storage is tight.

---

## The samples

Pitches are where an instrument has one. Every sample part has **TUNE ±24
semitones**, so one stroke covers a whole family of drum sizes.

### Skin: hand drums (001–032)

| Slot | Name | What it is |
|---:|---|---|
| 001 | `DjembeBass` | palm in the centre: the goblet's 74 Hz air resonance pushed by the head |
| 002 | `DjembeTone` | flat fingers at the edge: the head's ~280 Hz pitch and its 1–1.5 kHz ring |
| 003 | `DjembeSlap` | fingers whipped onto the edge: skin crack over a 1.07 kHz ring |
| 004 | `CongaOpen` | open tone, 271 Hz with partials at 1.20 / 1.71 / 2.37× |
| 005 | `CongaMute` | muted tone, 652 Hz, closed by the hand |
| 006 | `CongaSlap` | open slap |
| 007 | `CongaHeel` | heel of the palm, pressed: a closed low thud |
| 008 | `TumbaOpen` | open tone of the big drum, 196 Hz (G3) |
| 009 | `BongoMacho` | small bongo |
| 010 | `BongoHembra` | large bongo |
| 011 | `TablaNa` | dayan, *Na*: ringing 2nd/3rd harmonics, C#4 |
| 012 | `TablaTin` | dayan, *Tin*: the 3rd harmonic leads, A#3 |
| 013 | `TablaTun` | dayan, *Tun*: open centre stroke, fundamental rings, C4 |
| 014 | `TablaGe` | bayan, *Ge*: open bass, G2 |
| 015 | `TablaGeBend` | bayan *Ge* with the heel sliding: about +5 semitones in 300 ms |
| 016 | `TablaKe` | bayan, *Ke*: flat palm, closed |
| 017 | `DarbukaDoum` | centre stroke: the goblet's ~96 Hz air |
| 018 | `DarbukaTek` | ring finger on the rim: bright edge ring |
| 019 | `DarbukaKa` | the other hand's rim stroke: a softer *tek* |
| 020 | `BendirDoum` | North-African frame drum, centre, with its snare strings buzzing |
| 021 | `BendirTek` | the same drum, edge |
| 022 | `Riq` | Arabic tambourine: finger on the head, five pairs of jingles |
| 023 | `CajonBass` | box bass: the port's air and the tapa's low plate modes |
| 024 | `CajonSlap` | tapa slap with the snare wires |
| 025 | `TalkDrumUp` | talking drum squeezed while it rings: ~D3, up about a fifth |
| 026 | `TalkDrumDn` | talking drum released: down about a fifth |
| 027 | `SurdoOpen` | Brazilian bass drum, felt mallet, ~A#1 |
| 028 | `SurdoMute` | the same, damped by the hand |
| 029 | `Tamborim` | small tight frame, stick |
| 030 | `Pandeiro` | thumb on the edge plus the platinelas |
| 031 | `Cuica` | friction drum: the stick-slip "laugh", rising and falling |
| 032 | `Taiko` | large drum, 88 Hz (F2), dense two-head cluster, 2.2 s |

### Organic: seeds, beads, gourds, wood (033–050)

| Slot | Name | What it is |
|---:|---|---|
| 033 | `ShakerFwd` | egg/tube shaker, strong stroke: seeds hit the end wall on the beat |
| 034 | `ShakerBack` | the weak stroke: softer hit, more slide |
| 035 | `Maracas` | a pair, bright seed cloud |
| 036 | `Caxixi` | woven basket rattle: seeds on its hard gourd base |
| 037 | `Cabasa` | steel bead chain twisted on a ridged cylinder |
| 038 | `ShekereHit` | bead-netted gourd struck on its base |
| 039 | `SeedPods` | nut-shell / chajchas rattle |
| 040 | `Rainstick` | pebbles cascading through a cactus tube, 2.6 s |
| 041 | `GuiroShort` | one quick scrape across the ridges |
| 042 | `GuiroLong` | the classic long stroke down, quick flick back |
| 043 | `Claves` | rosewood, 2.64 kHz, with the cupped hand's resonance |
| 044 | `WoodBlockHi` | wood block, 1.15 kHz |
| 045 | `WoodBlockLo` | wood block, 849 Hz |
| 046 | `TempleBlock` | carved hollow block (mokugyo) |
| 047 | `SlitDrumHi` | log drum tongue, E4, rubber mallet |
| 048 | `SlitDrumLo` | log drum tongue, A3 |
| 049 | `BambooKnock` | bamboo tube knocked with a stick |
| 050 | `Coconut` | coconut shells clapped |

### Ethnic: clay, metal, wire, tuned bars (051–061)

| Slot | Name | What it is |
|---:|---|---|
| 051 | `UduBass` | clay pot, palm over the side hole: the "bloop" rising 85 → 112 Hz |
| 052 | `UduTone` | fingertip on the clay belly |
| 053 | `AgogoHi` | cone bell, A#5 |
| 054 | `AgogoLo` | cone bell, F5 |
| 055 | `KalimbaE4` | thumb-piano tine, with its 6.37× "ping" |
| 056 | `KalimbaA4` | thumb-piano tine |
| 057 | `BalafonC4` | West-African xylophone bar over a gourd with a buzzing mirliton |
| 058 | `BalafonG4` | the same, G4 |
| 059 | `Zills` | finger cymbals clashed, 2.1 s |
| 060 | `Berimbau` | musical bow struck with a stick, gourd swung open: ~A#2 with a "wah" |
| 061 | `BerimbauHi` | the same with the coin pressed: a whole tone up, shorter |

---

## Kit recipes

Each recipe fills the six sample parts. Set each part to **BANK = USER** and the
**SAMPLE** shown (the numbers assume the default slots `001`–`061`). Then save
with KIT SAVE.

**Choke** (SHIFT + button 2, *CHOKE*): strokes **of the same drum** belong in one choke
group. A new stroke damps the ring of the last one, which is how a real drum
behaves and keeps tone/slap/bass patterns clean. CH and OH holding two strokes
of one shaker makes the shaker alternate naturally.

### A. Djembe circle (West Africa)

| CH | OH | RS | CP | SP1 | SP2 |
|---|---|---|---|---|---|
| 033 ShakerFwd | 034 ShakerBack | 003 DjembeSlap | 002 DjembeTone | 001 DjembeBass | 053 AgogoHi |

Choke RS + CP + SP1 together (one djembe). Analog **BD/LT/HT** tuned low with
short decay play dundun / sangban / kenkeni. **MULTI**: Brachetti `Djambe`.

### B. Rumba (Afro-Cuban)

| CH | OH | RS | CP | SP1 | SP2 |
|---|---|---|---|---|---|
| 035 Maracas | 042 GuiroLong | 043 Claves | 006 CongaSlap | 004 CongaOpen | 008 TumbaOpen |

Choke CP + SP1 (one conga). Swap SP2 for 005 CongaMute for a single-drum
tumbao. **MULTI**: Brachetti `Bongo` or `Conga`.

### C. Tabla & darbuka (North India, Middle East)

| CH | OH | RS | CP | SP1 | SP2 |
|---|---|---|---|---|---|
| 022 Riq | 059 Zills | 018 DarbukaTek | 011 TablaNa | 015 TablaGeBend | 017 DarbukaDoum |

Tune CP (Na, C#4) to the key of the song. Na, Tin and Tun are true pitched
harmonic series. Replace SP1 with 014 TablaGe for an unbent bass. Zills ring
2.1 s, so raise OH's DECAY. **MULTI**: Brachetti `Handpn`.

### D. Batucada (Brazil)

| CH | OH | RS | CP | SP1 | SP2 |
|---|---|---|---|---|---|
| 036 Caxixi | 037 Cabasa | 029 Tamborim | 030 Pandeiro | 053 AgogoHi | 027 SurdoOpen |

A second agogo (054 AgogoLo) or 031 Cuica can replace SP1. The analog **BD**,
tuned low with a long decay, is the second surdo. Choke SP2 with BD if you
want the pair to answer each other.

### E. Wood & seeds (organic)

| CH | OH | RS | CP | SP1 | SP2 |
|---|---|---|---|---|---|
| 039 SeedPods | 040 Rainstick | 044 WoodBlockHi | 046 TempleBlock | 047 SlitDrumHi | 051 UduBass |

The rainstick is 2.6 s long: OH's DECAY must be near maximum, and START trims
its slow swell if you want it on the beat. **MULTI**: Brachetti `Marmba`, `Kalimba` or `SltDrm`.

### Playing notes

- **Length.** A sample part's envelope (attack + decay) lasts at most 4 s, and
  a short DECAY cuts a long sample. That matters for Rainstick (2.6 s), the two
  Kalimbas (2.4 s), Taiko (2.2 s), Zills (2.1 s) and the tablas (~1.4 s). Give
  them DECAY to taste.
- **On the step.** Every sample starts sounding at its first sample. The
  rattles put their accent within ~20 ms, so they land on the beat (a shaker
  whose seeds hit the wall 70 ms in would always play late). Low drums peak
  within a cycle or two of their fundamental. The cuíca, rainstick and long
  guiro stroke are gestures: their loudest point comes later by nature.
- **Levels.** Every hit peaks at −1 dBFS. Sustained, tonal sounds (kalimba,
  zills, agogo, balafon, cuíca) are held back to a common loudness so they do
  not tower over the hits. The rest is the part's LEVEL knob.
- **One velocity layer.** The sample parts play the same file at every
  velocity. ACCENT, TUNE and the part's filter are the dynamics you have.

---

## How the sounds are made

`render_kit.py` builds every instrument from a few mechanisms (each function
is commented with the physics it stands for):

- **Membranes:** circular-membrane modes from the Bessel zeros. A strike
  position (centre ↔ rim) and a contact patch (palm, fingers, stick) decide
  which modes a stroke reaches. The contact time sets how much top end gets
  through (convolution with a force pulse). Each mode decays with its own T60,
  and degenerate mode pairs are split so a head beats slowly like a real one.
  Tension modulation makes a hard hit start sharp and settle. Goblet and box
  drums add their Helmholtz air resonance, and slaps add a skin "crack".
- **Two-stage decay:** real hand drums drop their first 20 dB fast and then
  ring on. The conga, tabla and taiko render the fast part explicitly, which is
  what the references measured.
- **Tabla:** the syahi tunes the dayan's modes onto a harmonic series (Raman,
  1934). The stroke chooses the harmonics, and the *kinar* (bare rim) adds a
  short bright cloud.
- **Rattles:** stochastic particle collisions after Perry Cook's PhISEM. Each
  seed or bead click excites the shell's resonances, under a gesture envelope.
- **Guiro:** one click per ridge, at the stroke's speed, over unevenly
  hand-cut ridges, ringing the gourd at ~1 kHz.
- **Bars, blocks, bells, tines:** free-free bar ratios (claves 1 : 2.756 :
  5.404), cantilever tines (kalimba ~6.3×), cone-bell inharmonic modes, and
  hollow-block cavities.
- **Contact rattles:** bendir snare strings, cajón wires and the balafon's
  mirliton are noise gated by how far the vibrating surface pushes past rest,
  so they buzz at the drum's own period and die before the tone.
- **Others:** the cuíca's stick-slip voice through head formants, the
  talking drum's squeezed glide, the udu's rising Helmholtz "bloop", and the
  berimbau's stiff wire under a gourd resonance that sweeps.

Every sample then gets a little diffuse close-room air (no discrete echoes,
which would comb-filter a hit), a 25 Hz high-pass, a fade-out once it has
decayed 60 dB below its peak (or at its maximum length), and its level (peak
ceiling −1 dBFS, BS.1770 loudness ceiling −15 LUFS momentary).

### Calibration

Thirteen of the instruments have a counterpart among the reference recordings
in [`../brachetti/samples/`](../brachetti/samples/). Those recordings were only
**measured**. Nothing from them is in these samples. Rendered vs reference:

| Sample | Reference | strongest partial, Hz | energy in first 30 ms, % | t40, ms | energy in 1–3 kHz, % |
|---|---|---:|---:|---:|---:|
| `DjembeTone` | `Djambe-A3.wav` | 1069 / 586 | 76 / 65 | 195 / — | 29 / 35 |
| `DjembeSlap` | `Djambe-B3.wav` | 1066 / 1066 | 80 / 43 | 250 / — | 95 / 61 |
| `CongaOpen` | `Bongo_Conga2.wav` | 271 / 271 | 73 / 81 | 265 / 259 | 2 / 1 |
| `CongaMute` | `Bongo_Conga_Mute4.wav` | 658 / 652 | 95 / 99 | 120 / 80 | 31 / 22 |
| `TablaNa` | `percussion-one-shot-tabla-3_C_major.wav` | 552 / 551 | 70 / 76 | 650 / 773 | 27 / 31 |
| `TablaTin` | `Tabla-Drum-Hit-D4_.wav` | 696 / 697 | 67 / 74 | 510 / 758 | 44 / 48 |
| `Taiko` | `Taiko-Hit.wav` | 88 / 88 | 28 / 45 | 1005 / 1177 | 9 / 5 |
| `Maracas` | `MaracasPair.wav` | 5411 / 5584 | 85 / 69 | 170 / 229 | 1 / 0 |
| `SeedPods` | `ChaChaNut2.wav` | 3365 / 3306 | 67 / 50 | 200 / 289 | 11 / 6 |
| `GuiroShort` | `Guiro1.wav` | 1078 / 1036 | 78 / 62 | 130 / 90 | 15 / 19 |
| `Claves` | `wetclave.wav` | 2639 / 2688 | 81 / 90 | 180 / 180 | 98 / 97 |
| `WoodBlockLo` | `WoodBlock1.wav` | 849 / 849 | 95 / 86 | 140 / 125 | 6 / 3 |
| `KalimbaE4` | `kalimba-e_E.wav` | 330 / 329 | 23 / 32 | 1865 / 2285 | 18 / 27 |

(t40 = time to fall 40 dB from the loudest 5 ms frame. "—" means the reference
file is cut off before it gets there.)

The calibration loop measured **energy over time per band**, not only a
spectrum, and that is what moved the models most. Three quarters of a tabla
stroke is spent in its first 30 ms, mostly in 1–3 kHz. A taiko's 120–250 Hz
cluster, not its 88 Hz fundamental, holds two thirds of the energy for the
whole ring. A conga loses its first 20 dB in 60 ms and then rings for 400.

**Known gaps.** `DjembeSlap` is more front-loaded than its reference: the
recording's 1 kHz ring holds almost half its energy in 30–100 ms, ours a sixth.
`DjembeTone`'s strongest partial is its 1 kHz ring, where the recording's is
586 Hz, though its band balance is close. The remaining 48 instruments have no
reference here. They are built from the instruments' physics and playing
range, checked by spectrogram, pitch tracking (the udu, tabla bend, talking
drum and cuíca glides measure as designed) and the rules above.
**None of the kit has been heard on hardware yet.**

---

## Regenerating and customising

Requires Python 3 with `numpy` and `scipy`.

```bash
python3 render_kit.py                     # render all 61 into ./Samples (slots 001-061)
python3 render_kit.py --first-index 41    # same kit at slots 041-101
python3 render_kit.py --only TablaNa Taiko --out /tmp/try
python3 render_kit.py --list              # slot map only
python3 check_kit.py                      # check ./Samples against the drumlogue rules
python3 check_kit.py Samples --existing 001 002 003   # ...and against slots in use
```

Rendering is deterministic: every random draw is seeded from the instrument's
name, so the same command writes bit-identical files. Instruments are plain
functions listed in the `KIT` table at the bottom of `render_kit.py` (name,
family, function, maximum length, room). To retune one, change its numbers,
re-render it with `--only`, and run `check_kit.py`. To add one, write a
function and add a `KIT` row. Names are limited to 12 characters so they fit
the display.

## Files

| File | What it is |
|---|---|
| [`Samples/`](Samples/) | the 61 rendered samples, ready to copy |
| [`render_kit.py`](render_kit.py) | the models, the kit table and the renderer |
| [`check_kit.py`](check_kit.py) | checks any folder of WAVs against the drumlogue's USER-sample rules |

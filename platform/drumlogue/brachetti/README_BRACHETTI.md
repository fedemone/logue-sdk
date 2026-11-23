# Brachetti (all twelve families in one unit)

> **Disclaimer:** Brachetti is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

Physical-modelling percussion: the twelve Brachetti instrument families —
114 instruments — in **one** drumlogue synth unit.  The drumlogue holds only
so many units, so this is the one to install for the whole set; the twelve
single-family units it is built from stay available in
[`brachetti_split`](../brachetti_split/) for anyone who wants just one family.

- **The unit's presets are the families.**  Pick one in the drumlogue's
  preset menu and its instruments are on the `Program` knob, which then shows
  the instrument actually playing (Kick2, Djambe, Marmba, …).  The drumlogue
  shows the preset name only in that menu, so the family is not on the panel
  while you play; the table below is where the families are written down.
- **Every family plays exactly as its own unit.**  The engines are the
  family units' code, compiled in unchanged (`families.h`), and
  [`brachetti_check.sh`](../tools/brachetti_equiv/brachetti_check.sh) checks
  that every instrument, at every knob setting it tries, renders bit for bit
  as in the family unit (below).

| | |
|---|---|
| Unit name on the drumlogue | `Brachetti` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x4272416C` ('BrAl') — a new id, so it installs next to the twelve family units and to the original 40-preset `Brachetti` ('Reso') |
| Presets | 12, one per family |
| Instruments | 114, on `Program` |
| Knobs | 24 |
| ARM size (cross-built, `arm_size.sh`) | .text 116,944 · .rodata 62,200 · .data.rel.ro 2,368 · .data 46,016 · .bss 353,056 (every engine twice: *Ringing on*) |

## The presets: one per family

| # | Preset (family) | Single-family unit | Instruments on `Program` |
|---|---|---|---|
| 0 | Kick | [`BrcKick`](../brachetti_split/brachetti_kick/README.md) | Kick2, 808Sub, Kick |
| 1 | Tom | [`BrcTom`](../brachetti_split/brachetti_tom/README.md) | Djambe, Ac Tom, DeepBs, Conga, Handpn, Bongo, RackTom, DjmTone, DjmSlap, DjmBass, CngOpen, CngMute, CngSlap, CngHeel, Tumba, BngMach, BngHmbr, TblaNa, TblaTin, TblaTun, TblaGe, TblGeBd, TblaKe, DrbDoum, DrbTek, DrbKa, TalkUp, TalkDn, SurdOpn, SurdMut, Tambrim, UduBass |
| 2 | Kernel | [`BrcKernel`](../brachetti_split/brachetti_kernel/README.md) | Timpni, Taiko |
| 3 | Snare | [`BrcSnare`](../brachetti_split/brachetti_snare/README.md) | AcSnre, MrchSnr, BrshSnr, RimShot, BndDoum, BndTek, CajBass, CajSlap |
| 4 | Bar | [`BrcBar`](../brachetti_split/brachetti_bar/README.md) | Marmba, TblrBel, Vibrph, Wodblk, Kalimba, StelPan, Claves, GlsBwl, SltDrm, GlsBotl, WdBlkHi, TmplBlk, SltDrHi, SltDrLo, Bamboo, Coconut, UduTone, KlmbE4, KlmbA4 |
| 5 | Plate | [`BrcPlate`](../brachetti_split/brachetti_plate/README.md) | Cowbel, Trngle, BelTre, Tick, AgogoHi, AgogoLo, Zills |
| 6 | Cymbal | [`BrcCymbal`](../brachetti_split/brachetti_cymbal/README.md) | Cymbal, Gong, HHat-O, Ride, RidBel, Splash |
| 7 | Noise | [`BrcNoise`](../brachetti_split/brachetti_noise/README.md) | Clap, Shaker, HHat-C, ShkFwd, ShkBack, Maracas, Caxixi, Cabasa |
| 8 | World | [`BrcWorld`](../brachetti_split/brachetti_world/README.md) | Cuica, Berimbu, BerimHi, BalafnC, BalafnG, Riq, Pandeir, Shekere, Rainstk |
| 9 | Lippa | [`BrcLippa`](../brachetti_split/brachetti_lippa/README.md) | Snare, SnrHvy, Bendir, MmSnare, MmKick, MmSnr2, MmTom, MmFloor |
| 10 | Pizzico | [`BrcPizzico`](../brachetti_split/brachetti_pizzico/README.md) | Harp, Oud, Fretls, Guitar, BassPiz |
| 11 | Cordiera | [`BrcCordiera`](../brachetti_split/brachetti_cordiera/README.md) | Tabor, Daf, MbiraBz, TomBuzz, Marranz, Spring, Gopichd |

What each instrument is, how it was voiced and what its knobs do in detail
is in its family unit's README (linked above); the engines are summarised in
the [repository README](../../../README.md#brachetti-documents).

### Choosing a family and an instrument

- **Loading a preset** switches to that family on its first instrument and
  puts that instrument's own knob values on the panel (the drumlogue re-reads
  every knob after a preset load).  The performance and master knobs —
  `Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff`, `Resnc` — keep where you
  left them; `Gain` and the dedicated knobs (pages 3-6) load with the
  instrument.  What the family left behind was playing **rings on** until it
  dies away, under its own settings, while the new family plays.
- **Re-loading the preset already selected** goes back to the family's first
  instrument.
- **`Program`** picks the instrument inside the selected family, exactly as
  on the family unit.  **A sound still ringing rings on:** the next strike
  plays the new instrument and the ringing one dies away under its own
  instrument and knobs (see *Ringing on*, below).  Its range is 0-31 (Tom has 32 instruments, the most);
  in a smaller family the positions past its last instrument play, and show,
  the last one.
- **Saved with a project:** the drumlogue stores the preset (the family) and
  every knob, and on loading applies the preset first and the knobs after
  it (SDK README, *Presets*), so a part comes back on its family, instrument
  and knob settings.
- As on the family units, a `Program` change rewrites the dedicated knobs
  inside the unit, but the panel shows the new values only after you switch
  page (the drumlogue re-reads knobs when it draws a page).  Loading the
  family preset refreshes the panel at once.

## The knobs

The drumlogue reads the knobs' names and ranges once, from `header.c`, so
they are the same whatever the family.

**Pages 1-2 — common** (the same slots, ranges and meanings on every family
unit): `Program` · `Pitch` · `Poly` · `Velocity`, then `Tone` · `Cutoff` ·
`Resnc` · `Gain`.  Noise has no `Pitch` (nothing in a noise voice follows
the note) and Kernel no `Poly` (it always has its two kettles): there the
knob does nothing (`Poly` shows `---`).

**Pages 3-6 — the family's dedicated knobs.**  Each slot carries the
standard Brachetti knob of its name; a family that relabels or re-purposes a
knob keeps it in its slot, and the three families with fewer knobs (Kernel,
Cymbal, Noise) have each knob in the slot of its kind.  The display always
shows **the family knob's own value, formatted as on the family unit** — so
on Tom the `Rel` slot shows the Punch amount, on Snare `MlltRes` shows the
Buzz.

- **Number slots** (MlltRes, VlMllRes, VlMllStf, Mterl, HitPos, Inharm,
  TubRad, NzMix): every family's knob there has the same range and type, so
  the slot is declared with it and the drumlogue shows and steps it exactly
  as on the family unit.  In a family without that knob it shows a number
  but does nothing (Noise's Mterl, HitPos, Inharm; Cymbal's NzMix).
- **Strings slots** (MlltStif, Partls, Model, Dkay, Rel, NzRes, NzFltr,
  NzFltFrq, and Program, Poly, Cutoff, Resnc): the unit formats the value,
  or shows `---` where the family has no knob.  The drumlogue passes a
  strings knob's value as an unsigned 16-bit number (a −1 arrives as 65535),
  so these slots never go negative: Partls, Model, Rel and NzFltr run 0..200.
  Where a family knob's range differs from the slot's (marked † below), its
  values are spread evenly over the slot's travel — Partls 0-4 sits at 0, 50,
  100, 150, 200; a bipolar −100..100 knob is the slot minus 100; a 0-20 Rel
  takes ten knob steps per value — so every value has its own position and
  preset values come back exactly.
- **Cutoff and NzFltFrq step 0.1 kHz**, here and on the family units alike
  ([`knob_steps.h`](../brachetti_common/knob_steps.h)): the engine keeps its
  10 Hz values, and a preset whose noise-filter frequency is off the 0.1 kHz
  grid plays it exactly, but shows (and returns from a saved project as) the
  nearest step, at most 50 Hz away; every preset's Cutoff is fully open, on
  the grid.

| Page 3 | `MlltRes` (0..1000, number) | `MlltStif` (0..50, strings) | `VlMllRes` (-100..100, number) | `VlMllStf` (-100..100, number) |
|---|---|---|---|---|
| Kick | Thump | Snap | VelThmp | VelSnap |
| Tom | MlltRes | MlltStif | Slap | SlapSnp |
| Kernel | Knock | MlltStif | VlMllRes | VlMllStf |
| Snare | Buzz | BuzzTon | Crack | Tight |
| Bar | MlltRes | MlltStif | Punch | VlMllStf |
| Plate | MlltRes | MlltStif | Chirp | VlMllStf |
| Cymbal | MlltRes | MlltStif | Tang | VlMllStf |
| Noise | GrnDpth | GrnRate | GrnFade | Swell |
| World | MlltRes | MlltStif | Rattle | VlMllStf |
| Lippa | MlltRes | MlltStif | Bend | VlMllStf |
| Pizzico | MlltRes | MlltStif | Bend | VlMllStf |
| Cordiera | Buzz | BuzzTon | Crack | Tight |

| Page 4 | `Partls` (0..200, strings) | `Model` (0..200, strings) | `Dkay` (0..200, strings) | `Rel` (0..200, strings) |
|---|---|---|---|---|
| Kick | DiveTm (-100..100) † | Punch (-100..100) † | Dkay | Rel (0..20) † |
| Tom | Partls (0..4) † | Model (0..8) † | Dkay | Punch (-100..100) † |
| Kernel | Fill (0..4) † | Punch (-100..100) † | Dkay | Rel (0..20) † |
| Snare | Punch (-100..100) † | Model (0..8) † | Dkay | BuzzLen (0..20) † |
| Bar | Partls (0..4) † | Model (0..8) † | Dkay | Rel (0..20) † |
| Plate | Partls (0..4) † | Model (0..8) † | Dkay | Punch (-100..100) † |
| Cymbal | Density (0..7) † | Punch (-100..100) † | Dkay | Rel (0..20) † |
| Noise | Band (-100..100) † | VelTone (-100..100) † | — | Punch (-100..100) † |
| World | Partls (0..4) † | Model (0..8) † | Dkay | Punch (-100..100) † |
| Lippa | BendTm (-100..100) † | Snares (-100..100) † | Dkay | Punch (-100..100) † |
| Pizzico | BendTm (-100..100) † | Buzz (-100..100) † | Dkay | Punch (-100..100) † |
| Cordiera | Punch (-100..100) † | Model (0..8) † | Dkay | BuzzLen (0..20) † |

| Page 5 | `Mterl` (-10..30, number) | `HitPos` (-98..98, number) | `Inharm` (-100..100, number) | `TubRad` (0..20, number) |
|---|---|---|---|---|
| Kick | Weight | Click | Dive | Tune |
| Tom | Mterl | HitPos | Inharm | TubRad |
| Kernel | Mterl | HitPos | Inharm | TubRad |
| Snare | Mterl | HitPos | Inharm | TubRad |
| Bar | Mterl | HitPos | Inharm | TubRad |
| Plate | Mterl | HitPos | Inharm | TubRad |
| Cymbal | Mterl | HitPos | Inharm | TubRad |
| Noise | — | — | — | Rel |
| World | Mterl | HitPos | Inharm | TubRad |
| Lippa | Mterl | HitPos | Inharm | ApDelay |
| Pizzico | Mterl | HitPos | Inharm | ApDelay |
| Cordiera | Mterl | HitPos | Inharm | TubRad |

| Page 6 | `NzMix` (0..100, number) | `NzRes` (0..1000, strings) | `NzFltr` (0..200, strings) | `NzFltFrq` (1..200, strings 0.1 kHz steps) |
|---|---|---|---|---|
| Kick | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Tom | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Kernel | NzMix | — | — | NzFltFrq |
| Snare | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Bar | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Plate | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Cymbal | — | — | — | — |
| Noise | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| World | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |
| Lippa | NzMix | Blend (0..100) † | SnrTone (-100..100) † | NzFltFrq |
| Pizzico | NzMix | Blend (0..100) † | BuzzTon (-100..100) † | NzFltFrq |
| Cordiera | NzMix | NzRes | NzFltr (0..2) † | NzFltFrq |

What every knob does in a family is in that family unit's README (*Knobs*).

## How it is built

| File | What it holds |
|---|---|
| `header.c` | the unit header: 12 presets, the 24 knobs |
| `unit.cc` | the drumlogue callbacks, routed to `brachetti::Synth` |
| `brachetti_synth.h` | the routing: the family table (`kFamilies`: preset name, engine, which family knob sits in each slot), the knob translation, the family switch and its fade |
| `families.h` | the twelve family engines, each family unit's `synth_engine.h` included inside a namespace of its own (`fam_kick` … `fam_cordiera`) |
| `family_headers.c` | each family unit's own `header.c`, compiled under another name (`brc_hdr_<family>`): the knob ranges, types and names the translation reads |

Nothing of a family is copied: a change in a family unit's folder (or in
[`brachetti_common`](../brachetti_common/README.md)) reaches Brachetti with
the next build, and `brachetti_check.sh` then says whether Brachetti still
plays it exactly as the family unit.

All twelve engines are resident and initialised at start-up: a family
switch constructs nothing and allocates nothing.  Only the selected family
renders, plus whatever is still ringing from before a change, so the CPU
cost of an instrument is the same as on its family unit, and twice that
only while two instruments overlap.  The memory is the twelve units'
together (most of `.bss` is BrcLippa's and BrcPizzico's loop delay lines,
BrcCordiera's and BrcWorld's wires, the kernel's modes).

### Ringing on

A family engine cannot keep a voice through a Program change: its voices
read the current instrument's tables while they ring.  So every family (in
Brachetti and in each single-family unit alike) runs as **two copies** of
its engine, [`brc::RingOver`](../brachetti_common/ring_over.h).  New strikes
and the knobs go to the current copy.  A Program change while the current
copy still sounds hands the new instrument, with the panel's knobs, to the
other copy; the first rings out untouched — its instrument, knobs and master
stage — until it is silent.  A held gate is released on the copy it struck.
A change while nothing rings is exactly the old Program change.  At most two
instruments of a family sound at once: a third change while both still ring
fades the older one in ~10 ms, as before.  A family switch works the same
way one level up: the family left behind keeps rendering until it is silent.
The price is memory: the engines and their delay lines are there twice.

The drumlogue may call the knob and preset callbacks from another thread
than the audio: a preset load sets the new engine up completely and then
publishes it with one atomic store; the audio thread picks it up at its next
block, so no engine renders half set up.

## Build

```
build drumlogue/brachetti
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  **Tested on hardware** (the version before ringing
on, 106 KB of code: it loads and plays every family).  With every engine
twice it is 117 KB of code and about 400 KB of memory (`.data` + `.bss`).

## Verification

```
cd ../tools/brachetti_equiv
./brachetti_check.sh          # host, bit-exact
./arm_brachetti_check.sh      # Cortex-A7 build under qemu-arm, NEON paths
./multi_load.sh               # loaded next to the twelve family units
./arm_size.sh ../../brachetti # sizes; fails on a GNU-unique symbol
```

`brachetti_check.sh` builds Brachetti and the twelve family units as host
libraries, loads them as the drumlogue does, and for every family and every
instrument checks that every knob reads back as the family's own value with
its display text, and that gate and MIDI strikes at several velocities render
bit for bit as on the family unit — at the shipped settings and with every
dedicated knob at its ends, centre and two random points.  It also checks the
header defaults (what the first preset loads), that every slot is wide enough
for its knobs and loses none of their values, that every family knob is
placed exactly once, that `Program` stops at a family's last instrument,
that a sound still ringing rings on through a Program change and a family
switch (two instances of each unit with the same history, one of them
changed while a held gate rings: the outputs must stay identical, and the
next strike must play the new instrument) and
that the common knobs survive a family switch.  Result:

```
brachetti: IDENTICAL — 12 families, 35736 renders, 210086 knob read-backs compared with the family units, 36 ring-over cases
```

`arm_brachetti_check.sh` runs the same check on the ARM build (`-Os
-ffast-math`, NEON) under qemu: there the compiler may fuse float operations
differently in a different translation unit, so samples are compared within
dB limit per render and the worst render is printed; knob read-backs and
display strings must still match exactly.  Result (each dedicated knob at its two ends; `-90 dB` limit per render):
14,564 of 14,568 renders agree within −94 dB of their signal (float
reordering); the 4 that do not are the Gong with `Tang` (the cymbal's
`VlMllRes` slot) at +100, at −38 dB from the first sample.  That is the
cymbal engine's own `-ffast-math` sensitivity (BrcCymbal against the
original measures −51 dB in its worst render, [arm_equiv](../tools/brachetti_equiv/README.md#the-neon-paths-on-arm)):
built for ARM without `-ffast-math`, Brachetti is **bit-identical** to the
family units on all 14,568 renders, the Gong included.

**`-ffp-contract=off`.**  This check first found every modal family
differing on ARM by −30…−50 dB from the first sample: with GCC's default
contraction, `a*b+c` is fused into one VFMA wherever inlining puts the two
together, and Brachetti's single translation unit inlines differently from a
family unit's.  Some coefficients are sensitive to that rounding.  All
thirteen units (Brachetti and the twelve family units, `config.mk`) are now
built with `-ffp-contract=off`, which brought them back to agreement and
keeps the ARM builds close to the host renders the presets were voiced
against; it costs +0.1–0.3 % instructions (`arm_cpu.sh`: BrcBar 712.0 →
714.4, BrcTom 858.7 → 861.2, BrcLippa 945.2 → 946.2).

`multi_load.sh` (Brachetti with the twelve family units, each family preset
walked): `independent: 13 units loaded together show the same 1196872 display
strings as alone`.

## Credits

The instruments, engines and voicing are the Brachetti family units' — see
[the Brachetti family](../tools/brachetti_equiv/README.md) and each family
unit's README, which credit their sources (Miguel Moreno's Karplus-Strong
drums and mymembrane~ for BrcLippa, the Pelle e Legno samples the added
presets were voiced after, …).

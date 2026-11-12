# Brachetti family — one unit per instrument family, and the tools that check them

> **Disclaimer:** the Brachetti units are unofficial, independently developed units, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from their use. See the [repository disclaimer](../../../../README.md#disclaimer) for full terms.

**Documents, top-down** (the repository README's [documentation map](../../../../README.md#documentation-map) has them all):
this file → each unit's `README.md` (presets, knobs) and `CLAUDE.md` (rules
for changing it) → [`brachetti_common/README.md`](../../brachetti_common/README.md)
(the shared library and its helper index) → the original's documents (the
voicing history).

The 40-preset [`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) unit grew one synth engine with
six sound models, a cymbal port and a separate drum kernel inside it, and every
knob ended up wired into every model.  Its percussion is split here into
**eight units, one per instrument family**, so each can be browsed, used and
improved on its own.  The original unit is untouched and still builds and
ships as before.

| Unit (directory) | On the drumlogue | Presets | Knobs | Engine |
|---|---|---|---|---|
| [`brachetti_kick`](../../brachetti_kick/) | `BrcKick` | Kick2, 808Sub, Kick | 24 | membrane modes + boom + thump |
| [`brachetti_tom`](../../brachetti_tom/) | `BrcTom` | Djambe, Ac Tom, DeepBs, Conga, Handpn, Bongo, RackTom; added: DjmTone, DjmSlap, DjmBass, CngOpen, CngMute, CngSlap, CngHeel, Tumba, BngMach, BngHmbr, TblaNa, TblaTin, TblaTun, TblaGe, TblGeBd, TblaKe, DrbDoum, DrbTek, DrbKa, TalkUp, TalkDn, SurdOpn, SurdMut, Tambrim, UduBass | 24 | membrane modes + boom + strike transient |
| [`brachetti_kernel`](../../brachetti_kernel/) | `BrcKernel` | Timpni, Taiko | 21 (+1 blank) | dense coupled-resonator drum kernel |
| [`brachetti_snare`](../../brachetti_snare/) | `BrcSnare` | AcSnre, MrchSnr, BrshSnr, RimShot; added: BndDoum, BndTek, CajBass, CajSlap | 24 | modal head + snare-wire resonators |
| [`brachetti_bar`](../../brachetti_bar/) | `BrcBar` | Marmba, TblrBel, Vibrph, Wodblk, Kalimba, StelPan, Claves, GlsBwl, SltDrm, GlsBotl; added: WdBlkHi, TmplBlk, SltDrHi, SltDrLo, Bamboo, Coconut, UduTone, KlmbE4, KlmbA4 | 24 | free-bar modes + mallet |
| [`brachetti_plate`](../../brachetti_plate/) | `BrcPlate` | Cowbel, Trngle, BelTre, Tick; added: AgogoHi, AgogoLo, Zills | 24 | plate modes + ring-coupled noise |
| [`brachetti_cymbal`](../../brachetti_cymbal/) | `BrcCymbal` | Cymbal, Gong, HHat-O, Ride, RidBel, Splash | 20 | dense-resonator cymbal |
| [`brachetti_noise`](../../brachetti_noise/) | `BrcNoise` | Clap, Shaker, HHat-C; added: ShkFwd, ShkBack, Maracas, Caxixi, Cabasa | 19 (+1 blank) | shaped noise + AM gating |
| [`brachetti_world`](../../brachetti_world/) | `BrcWorld` | Cuica, Berimbu, BerimHi, BalafnC, BalafnG, Riq, Pandeir, Shekere, Rainstk (all added) | 24 | modal bank + friction voice, wire, buzz, jingles, particles |

All nine are `dev_id` 'FeDe' with their own `unit_id` ('BrKk', 'BrTm', 'BrKn',
'BrSn', 'BrBr', 'BrPl', 'BrCy', 'BrNz', 'BrWd'), so they install next to the
original and next to each other.  The first eight were split from the
original; the ninth, **BrcWorld**, was built after the split for the
instruments no family engine can make (the cuica's friction voice, the
berimbau's wire, the balafon's buzz, bouncing jingles, bead and pebble
cascades), on the same modal engine and library.  The families are the ones the original's README
already used for its "which knobs are live" matrix.

**Not split out: the Koto.**  The original's one string preset (a dual
Karplus-Strong waveguide) is outside the percussion these units focus on, so
it has no family unit; it stays playable in the original.  The waveguide
engine lives on there too.  (The berimbau did get a struck wire: BrcWorld's
`Wire` layer, a single Karplus-Strong loop written for it.)

## Presets added after the split

46 presets have been added to five of the units, and BrcWorld's 9 built with it, all voiced after the
[Pelle e Legno](../../pelle_legno/) samples (organic, skin and ethnic
percussion): 25 hand-drum strokes in BrcTom, 9 in BrcBar, 3 in BrcPlate, 5 in
BrcNoise and 4 in BrcSnare.  They have no counterpart in the original, so
`run_equiv.sh` and `arm_equiv.sh` name them and compare every other preset,
which still renders bit-identically.  How they were voiced (measured modal
banks, the attack searched against each sample, trims by the original's
method) is in [BrcTom's README](../../brachetti_tom/README.md#how-the-added-presets-were-voiced);
the tools are [`match_samples.py`](match_samples.py), its specs
[`match_specs.py`](match_specs.py) and [`preset_probe.cpp`](preset_probe.cpp)
(render / match / calib for any family unit — the family's replacement for
the original's `calib_probe.cpp`).

The original is no longer on main: it lives on the `original_brachetti`
branch.  The scripts find it through `BRACHETTI_ORIG`:

```
git worktree add /tmp/brachetti_orig origin/original_brachetti
BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti ./run_equiv.sh ../../brachetti_tom
```

## How the units were derived

Each unit is the original's code with everything its family never reaches
taken out, and nothing else changed in what it renders:

- **Other engines.**  Each family runs exactly one engine, so the engine
  routing (`kPresetEngine`, the per-voice engine latch) is gone, and so is
  every other engine's code and per-voice state: the Karplus-Strong waveguide
  (64 KB of delay lines), the dense-resonator cymbal, the Timpani/Taiko kernel
  and its 31 KB of mode tables.
- **Other families' branches.**  Every `if (preset == …)` branch for a preset
  the unit does not hold.
- **Layers no preset of the family can switch on.**  E.g. the boom oscillator
  on the bars (every bar preset ships `boom_env` 0), the snare wires outside
  the snares, the metallic FM chirp on the snares and noise voices.  The table
  columns that only fed them went with them.
- **Knobs that do nothing on the family** (below).

The knob names (`k_param*`) and the REFERENCE-ANCHOR design are kept.  The
comments were then rewritten per unit: each describes its own family only,
and the history of how the sounds were voiced stays in the original's
`README.md` and `CLAUDE.md`.  Each unit has its own `README.md` (presets, knob
pages, what every knob does there, its files) and `CLAUDE.md` (the rules for
changing it).

### Knobs removed

Measured with [`dead_params.py`](dead_params.py) on the original: every knob,
on every preset of a family, swept across its whole range (nine points) in ten
contexts — the shipped settings, noise opened up, the master filter closed,
Partls / Model / Dkay moved, Velocity at both ends, random knob states — and
each session renders a gate strike, a held MIDI note and an AllNoteOff.  A knob
is removed when every render was bit-identical.

| Unit | Removed | Why there is nothing for it to do |
|---|---|---|
| kernel | Poly, Model, NzRes, NzFltr | the kernel always has two kettles and no modal template or exciter noise path |
| cymbal | Model, NzMix, NzRes, NzFltr, NzFltFrq | the cymbal configures its own noise drivers per preset; no modal template |
| noise | Pitch, Partls, Dkay, Mterl, Inharm, TubRad | no modal body, nothing depends on the note; Dkay only set how fast AllNoteOff fades a voice, kept exact as a per-preset constant |
| kick, tom, snare, bar, plate | — | every knob is live (the original wired each knob into each family on purpose) |

`dead_params.py` also flags Poly on the cymbals: one strike per session cannot
show a voice cap.  Poly is live there (it decides when a different note gets
its own voice) and is kept.  On kick, tom, snare, bar and plate `Partls` keeps
only positions 0-4: in the original, 5-7 select which resonator of the Koto
string the knobs edit.

### Knobs: common, then dedicated

Each unit puts the same eight **common knobs** on its first two pages —
`Program` · `Pitch` · `Poly` · `Velocity`, then `Tone` · `Cutoff` · `Resnc` ·
`Gain` — and its family's **dedicated knobs** from page 3 on
([`common_knobs.h`](../../brachetti_common/common_knobs.h); a unit without
one of the common knobs leaves its slot blank, so they are in the same place
on all nine).  The dedicated knobs were then reviewed family by family with
[`knob_liveness.py`](knob_liveness.py), which moves every knob to both ends on
every preset and scores how much the sound changes.  Where a knob did nothing
(or next to nothing) on most of a family, its slot got a job of that family's;
where a standard name hid what a knob does in a family, it was relabelled:

| Unit | New knobs (in the slot of) | Punch (in the slot of) | Relabelled |
|---|---|---|---|
| kick | `DiveTm` (Partls); `Dive` (Inharm) now dives all three kicks | Model (with the kick's onset and boom rise) | `Thump` `Snap` `VelThmp` `VelSnap` `Weight` `Click` `Tune` |
| tom | — | Rel | `Slap` `SlapSnp` |
| snare | `Crack` (VlMllRes) now lengthens the crack and adds one where a preset has none | Partls | `Buzz` `BuzzTon` `Tight` `BuzzLen` |
| bar | — | VlMllRes (with the bar's onset swell) | — |
| plate | `Chirp` (VlMllRes) | Rel | — |
| noise | `GrnFade` (HitPos), `Swell` (VlMllStf), `Band` (Model), `VelTone` (VlMllRes); `GrnDpth` adds grains | a slot of its own | `GrnRate` |
| world | `Rattle` (VlMllRes); `Pitch` now moves the jingles, beads and mirliton too | Rel | — |
| cymbal | — | a slot of its own | `Tang` `Density` |
| kernel | — | a slot of its own | `Knock` `Fill` |

**Punch** is the transient shaper all nine share (`brc::Punch`): a gain curve
from each strike that lets the attack through and lowers the body after it
(up), or softens the attack (down), off at 0.  It never boosts the attack:
each unit ends in a peak limiter that takes 5-25 dB off its hard strikes
(measured, velocity 127: 808Sub −23 dB, Marmba −18 dB, TblaNa −21 dB; the
body untouched), so the contrast has to come from what follows the strike.
Where a 24-knob unit had no free slot, Punch took the weakest knob: Rel, a
second, finer ring length beside Dkay (tom, plate, world), Partls on the snare
(the head's mode count, under 0.5 dB on the classic snares), and the kick's
and bar's Attack knobs grew into it.

Every new knob is REFERENCE-ANCHORED and ships at the identity, and the
standard knob it displaced keeps playing at each preset's shipped value, so
every preset renders exactly as before; each unit's README has its knob
table and review.  For `run_equiv.sh` a relabelled knob is the same knob:
[`aliases.txt`](aliases.txt) names it, and the original's driver finds it
under its old name (`EQUIV_ALIASES`).  A knob with a new job has no
counterpart in the original: the script leaves it out and names it, as it
does the added presets.

### Then restructured onto a common library

With each unit holding only its family, what the eight still had in common —
the same knob mappings, exciter, voice life cycle and master stage, and for
the five modal units the same engine — was written out eight times.  It now
lives once, in [`brachetti_common`](../../brachetti_common/), and each unit
keeps only its family:

- `presets.h` — the knob layout, the presets and their voicing tables;
- `voice.h` — one voice and the layers the family uses;
- `synth_engine.h` — the family's strike recipe, knob mappings and render
  (the five modal units derive it from the shared engine, `modal_synth.h`).

The kernel keeps its drum in `modal_drum_kernel.h` in place of `voice.h`.

Long functions were split into named steps in their original order — every
unit's `NoteOn` into voice choice, shaping and the strike; the cymbal voice's
per-sample function into envelopes, control rate, drivers, subsonic guard,
bank and output stage; the kernel's block render into rebuild, retirement,
glide, one kettle and the silence gate — and code nothing reached was
removed.  The library's [README](../../brachetti_common/README.md) lists what
is where, the rules for changing it, the quirks kept on purpose and what was
removed (and where it still lives).

Every step was checked with `run_equiv.sh`: the restructured units render
bit-identically to the original exactly as before (below).  On ARM the render
costs are within 0.2 % of before or below (CPU), and the code is smaller in
six of the eight; every unit now also carries 288 bytes of display buffers of
its own (Units loaded together).

## Verification

### Bit-identical on the host

[`run_equiv.sh`](run_equiv.sh) builds [`equiv_driver.cpp`](equiv_driver.cpp)
against the original and a family unit, writes a script from the family
unit's own preset and knob listing ([`gen_scenarios.py`](gen_scenarios.py)) and
plays it through both.  Everything is addressed by name, so the same script
drives the 40-preset original and the renumbered family unit.  The script
covers every preset at four velocities, a held gate, MIDI notes and chords,
every knob at its ends, centre, default and random points (set before the
strike and again while the voice rings), stacked strokes at roll, flam and slow
spacings with Poly and Pitch moving, Program changes over ringing voices,
Reset / Suspend / AllNoteOff / pitch bend mid-ring, and 120 random sessions.
Both builds are host builds without `-ffast-math`, so any difference at all in
the output is a real one.

```
./run_equiv.sh ../../brachetti_kick        # or any of the eight
```

The work directory (binaries, script, hashes) is removed after an IDENTICAL
run; `KEEP_WORK=1` keeps it, and a difference always does.

Result for the restructured tree: **IDENTICAL on all eight** — 9,023
scenarios, 23,248 rendered spans with the default seed, and again with three
more seeds and 300 random sessions each (`--seed 7 / 2024 / 31337 --fuzz 300
--knob-points 2`): 24 of 24 runs identical, 29,421 more scenarios and
102,827 more rendered spans.

With the common / dedicated layout, the knobs of the families' own left out
(see above) and the added presets named, the default seed gives:

| Unit | Scenarios | Renders | Left out (the family's own) |
|---|---|---|---|
| kick | 695 | 2,174 | DiveTm, Punch, Dive |
| tom | 1,600 | 3,887 | Punch |
| kernel | 483 | 1,627 | Punch |
| snare | 940 | 2,578 | Crack, Punch |
| bar | 2,196 | 4,963 | Punch |
| plate | 922 | 2,586 | Chirp, Punch |
| cymbal | 1,185 | 3,165 | Punch |
| noise | 488 | 1,828 | GrnDpth, GrnFade, Swell, Band, VelTone, Punch |

all IDENTICAL.

### Known differences

Two, both reachable only by **re-selecting the preset that is already
selected while one of its voices still rings** — the original's `LoadPreset`
then rewrites that ringing voice's per-voice state from the preset tables:

- **Kick:** the original writes Kick's `snare_wire_mix` (0.03) into the
  ringing voice, which switches on a faint snare-wire layer on a kick drum.
  The kick unit has no snare-wire code.
- **Shaker:** the original re-strikes the ringing voice's modal bank at its
  config mix, bringing back the woodblock "tok" the preset removed.  The noise
  unit has no modal bank.

`run_equiv.sh` therefore never re-selects Kick or Shaker while they ring.  To
show that these writes are the ONLY differences, a scratch copy of the
original was made with just those two writes skipped (snare-wire writes only
for snare presets, no modal re-strike on noise presets); the kick and noise
units are bit-identical to it on every render with re-selects included.

### The NEON paths, on ARM

The host build never compiles the `__ARM_NEON` code (the modal bank, the
cymbal bank, the kernel).  [`arm_equiv.sh`](arm_equiv.sh) cross-builds both
units for the Cortex-A7 with the SDK Makefile's flags (`-Os -ffast-math`,
NEON-VFPv4, its inlining limits), runs the same kind of script under
`qemu-arm` and compares the
samples ([`compare_dumps.py`](compare_dumps.py)).  Under `-ffast-math` the
compiler may fuse and reorder float operations differently around different
code, so bit-identity is not expected; float-rounding-level differences are.

```
./arm_equiv.sh ../../brachetti_kick --fuzz 20 --knob-points 1
```

| Unit | max abs diff | diff / signal (overall, worst render) |
|---|---|---|
| kick | 1.0e-06 | −142 dB, −118 dB |
| tom | 4.8e-07 | −144 dB, −113 dB |
| kernel | 6.0e-07 | −149 dB, −111 dB |
| snare | 1.1e-06 | −135 dB, −110 dB |
| bar | 4.2e-07 | −151 dB, −119 dB |
| plate | 3.6e-07 | −152 dB, −122 dB |
| cymbal | 1.8e-03 | −77 dB, −51 dB |
| noise | 6.0e-07 | −139 dB, −118 dB |

The cymbal is the exception, and it is the compiler's: its source arithmetic
is unchanged (the host build is bit-identical), but in the original's bank
build GCC merges `exp2f(detune / 12) · exp2f(octave · 0.17)` into one
`exp2f(a + b)` under `-ffast-math`, and in the restructured build it does
not.  The second-octave lanes' frequencies then differ by 1-3 ULP, which over
a long tail is a slow phase drift — what the −77 dB measures; inaudible.
Forcing the merged form in the source would break the host bit-identity
(the original computes the product).  The kernel shows the same mechanism
can bite: with its sample rate a compile-time constant, GCC turned every
`x / fs` into `x · (1/fs)` and the kernel measured −82 dB, so it keeps the
rate in a variable (`m_sr`) and measures −149 dB, as before the
restructuring.

### Units loaded together

The drumlogue runs several units in one process.  [`multi_load.sh`](multi_load.sh)
builds the units as host shared libraries and prints every display string —
every "strings" parameter at every value, for every preset — once with each
unit loaded alone and once with all of them loaded together, each with
`RTLD_LOCAL` as a plugin host loads them; the two must be identical.  It
found the family units' first hardware bug: each unit's preset-name table was
a static local of an inline function, which GCC makes `STB_GNU_UNIQUE`, and
the loader keeps one copy of such a symbol across libraries — so every unit
showed the first-loaded unit's Program names (BrcKernel read "Marmba",
"TblrBel" for Timpni and Taiko), and the display buffers were shared too.
Each table is now private to its unit, and `arm_size.sh` fails any unit with a
unique symbol.

```
./multi_load.sh          # all nine; exit 1 and the differing strings if any
```

Result: 420,458 display strings identical alone and together (nine units,
with the presets added after the split).

### Sizes

[`arm_size.sh`](arm_size.sh) cross-compiles a unit the way the SDK Makefile
does and prints its sections (gcc 13 here, not the vendor gcc 6.5, so `.text`
moves a little between toolchains; data sections are exact):

| Unit | .text | .rodata | .data | .bss | total, before → after the restructuring |
|---|---|---|---|---|---|
| brachetti (original) | 53,628 | 34,508 | 4 | 108,124 | |
| kick | 15,560 | 912 | 1,952 | 292 | 19,266 → 18,776 |
| tom | 14,152 | 1,644 | 1,872 | 292 | 18,768 → 18,036 |
| kernel | 10,724 | 31,248 | 4 | 15,664 | 57,216 → 57,784 |
| snare | 15,492 | 1,288 | 2,000 | 292 | 19,844 → 19,136 |
| bar | 12,476 | 1,840 | 1,584 | 292 | 16,918 → 16,280 |
| plate | 12,996 | 1,080 | 1,808 | 292 | 16,650 → 16,240 |
| cymbal | 11,136 | 684 | 11,936 | 292 | 24,420 → 24,552 |
| noise | 7,500 | 300 | 992 | 292 | 8,672 → 9,144 |

(These are the restructuring's figures.  With the presets added after the
split, each unit's README carries its current sizes; BrcWorld measures
.text 20,496 · .rodata 3,516 · .data 4 · .bss 21,296, the 16 KB of wire
delay lines in `.bss`.)

(The totals include `.data.rel.ro`.)  The restructuring moved the preset
tables from `.data` to `.rodata` (`static const`): the original's notes
record 34.5 KB of `.rodata` loading on hardware, so the old rule of keeping
tables out of `.rodata` is not needed.

The synth object sits in `.data` in seven of the eight (every member has a
constant initialiser).  In the original, and in the kernel unit, it sits in
`.bss`: the kernel's mode arrays have no initialiser.  The original's own
notes record both placements loading on hardware.  None of the eight has run
on a drumlogue yet.

### CPU

[`arm_cpu.sh`](arm_cpu.sh) counts the ARM instructions a unit executes per
rendered frame: it builds the driver with the SDK flags, strikes every preset
as a four-note stack and renders 25 ms of it and of the idle path after it,
under `qemu-arm` with an instruction trace, and subtracts a run of the same
script without the renders (start-up and preset loading cancel out).  It
counts instructions, not cycles, so it is a regression check — compare a
unit with itself, never two units with each other.

```
./arm_cpu.sh ../../brachetti_kick
```

| Unit | before the restructuring | after |
|---|---|---|
| kick | 752.0 | 747.1 |
| tom | 903.7 | 892.2 |
| kernel | 1009.7 | 1006.6 |
| snare | 893.5 | 887.6 |
| bar | 698.5 | 693.4 |
| plate | 926.0 | 926.7 |
| cymbal | 653.6 | 627.5 |
| noise | 525.5 | 526.4 |

The driver's `render_only` command renders without hashing or dumping, so the
count is the unit's own work.

## Maintaining the units

A unit's family code is its own: a change in its folder cannot reach another
unit.  A change in [`brachetti_common`](../../brachetti_common/) reaches all
nine, so check them all.  BrcWorld has no original to compare with: check it
against itself (render its presets with `preset_probe render` before and
after, and compare), and the other eight with:

```
for u in kick tom kernel snare bar plate cymbal noise; do ./run_equiv.sh ../../brachetti_$u; done
```

Each unit file opens with a `From brachetti_common` block listing the library
helpers it uses; [`lib_uses.py`](lib_uses.py) keeps those blocks true
(`--write` after using or dropping a helper, `--check` before a commit,
`--index` for helper → units).  The library's
[README](../../brachetti_common/README.md#helper-index) indexes every helper:
look there before writing one.

Before a refactor, `run_equiv.sh` is the check that it changed nothing; once a
unit's voicing is changed on purpose it will (rightly) start to differ from
the original, and the script then shows exactly which presets and knobs
moved.  The original keeps its own code, so a fix that also applies to it has
to be made there by hand.

[`dead_params.py`](dead_params.py) works on any unit's driver, so it can show
whether a knob has become dead after a change; [`knob_liveness.py`](knob_liveness.py)
shows how much each knob moves each preset (`./knob_liveness.py tom noise`).
A knob a unit relabels goes in [`aliases.txt`](aliases.txt), so `run_equiv.sh`
keeps comparing it.  Python 3 with numpy is needed
for `compare_dumps.py`; the ARM scripts need
`apt-get install g++-arm-linux-gnueabihf qemu-user`.

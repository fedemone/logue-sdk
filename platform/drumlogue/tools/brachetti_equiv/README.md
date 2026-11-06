# Brachetti family — one unit per instrument family, and the tools that check them

> **Disclaimer:** the Brachetti units are unofficial, independently developed units, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from their use. See the [repository disclaimer](../../../../README.md#disclaimer) for full terms.

The 40-preset [`brachetti`](../../brachetti/) unit grew one synth engine with
six sound models, a cymbal port and a separate drum kernel inside it, and every
knob ended up wired into every model.  Its percussion is split here into
**eight units, one per instrument family**, so each can be browsed, used and
improved on its own.  The original unit is untouched and still builds and
ships as before.

| Unit (directory) | On the drumlogue | Presets | Knobs | Engine |
|---|---|---|---|---|
| [`brachetti_kick`](../../brachetti_kick/) | `BrcKick` | Kick2, 808Sub, Kick | 24 | membrane modes + boom + thump |
| [`brachetti_tom`](../../brachetti_tom/) | `BrcTom` | Djambe, Ac Tom, DeepBs, Conga, Handpn, Bongo, RackTom | 24 | membrane modes + boom + strike transient |
| [`brachetti_kernel`](../../brachetti_kernel/) | `BrcKernel` | Timpni, Taiko | 20 | dense coupled-resonator drum kernel |
| [`brachetti_snare`](../../brachetti_snare/) | `BrcSnare` | AcSnre, MrchSnr, BrshSnr, RimShot | 24 | modal head + snare-wire resonators |
| [`brachetti_bar`](../../brachetti_bar/) | `BrcBar` | Marmba, TblrBel, Vibrph, Wodblk, Kalimba, StelPan, Claves, GlsBwl, SltDrm, GlsBotl | 24 | free-bar modes + mallet |
| [`brachetti_plate`](../../brachetti_plate/) | `BrcPlate` | Cowbel, Trngle, BelTre, Tick | 24 | plate modes + ring-coupled noise |
| [`brachetti_cymbal`](../../brachetti_cymbal/) | `BrcCymbal` | Cymbal, Gong, HHat-O, Ride, RidBel, Splash | 19 | dense-resonator cymbal |
| [`brachetti_noise`](../../brachetti_noise/) | `BrcNoise` | Clap, Shaker, HHat-C | 18 | shaped noise + AM gating |

All eight are `dev_id` 'FeDe' with their own `unit_id` ('BrKk', 'BrTm', 'BrKn',
'BrSn', 'BrBr', 'BrPl', 'BrCy', 'BrNz'), so they install next to the original
and next to each other.  The families are the ones the original's README
already used for its "which knobs are live" matrix.

**Not split out: the Koto.**  The original's one string preset (a dual
Karplus-Strong waveguide) is outside the percussion these units focus on, so
it has no family unit; it stays playable in the original.  The waveguide
engine lives on there too, should a struck-string percussion family (a
hammered dulcimer, a berimbau) ever be worth deriving from it.

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

Identifiers (`m_modal_*_ref`, `k_param*`) and the REFERENCE-ANCHOR design are
kept, so a fix can still be ported between a unit and the original.  The
comments were then rewritten per unit: each describes its own family only,
and the history of how the sounds were voiced stays in the original's
`README.md` and `CLAUDE.md`.  Each unit has its own `README.md` (presets, knob
pages, what every knob does there) and `CLAUDE.md` (the rules for changing
it).

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

Result for the shipped tree: **IDENTICAL on all eight** — 9,023 scenarios,
23,248 rendered spans with the default seed, and again with three more seeds
and 300 random sessions each (`--seed 7 / 2024 / 31337 --fuzz 300
--knob-points 2`): 24 of 24 runs identical, 29,421 more scenarios and
102,827 more rendered spans.

| Unit | Scenarios | Renders |
|---|---|---|
| kick | 767 | 2,209 |
| tom | 1,670 | 3,878 |
| kernel | 486 | 1,613 |
| snare | 1,010 | 2,647 |
| bar | 2,288 | 5,158 |
| plate | 999 | 2,636 |
| cymbal | 1,182 | 3,121 |
| noise | 621 | 1,986 |

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
units for the Cortex-A7 with the units' own flags (`-Os -ffast-math`,
NEON-VFPv4), runs the same kind of script under `qemu-arm` and compares the
samples ([`compare_dumps.py`](compare_dumps.py)).  Under `-ffast-math` the
compiler may fuse and reorder float operations differently around different
code, so bit-identity is not expected; float-rounding-level differences are.

```
./arm_equiv.sh ../../brachetti_kick --fuzz 20 --knob-points 1
```

| Unit | max abs diff | diff / signal (overall, worst render) |
|---|---|---|
| kick | 4.2e-07 | −156 dB, −130 dB |
| tom | 4.2e-07 | −149 dB, −113 dB |
| kernel | 6.0e-07 | −149 dB, −108 dB |
| snare | 1.1e-06 | −135 dB, −110 dB |
| bar | 4.2e-07 | −152 dB, −119 dB |
| plate | 4.2e-07 | −152 dB, −122 dB |
| cymbal | 3.1e-06 | −130 dB, −108 dB |
| noise | 6.0e-07 | −139 dB, −109 dB |

### Sizes

[`arm_size.sh`](arm_size.sh) cross-compiles a unit the way the SDK Makefile
does and prints its sections (gcc 13 here, not the vendor gcc 6.5, so `.text`
moves a little between toolchains; data sections are exact):

| Unit | .text | .rodata | .data | .bss |
|---|---|---|---|---|
| brachetti (original) | 53,628 | 34,508 | 4 | 108,124 |
| kick | 16,220 | 298 | 2,624 | 64 |
| tom | 15,100 | 520 | 3,008 | 64 |
| kernel | 10,412 | 31,244 | 4 | 15,420 |
| snare | 16,464 | 372 | 2,880 | 64 |
| bar | 13,324 | 690 | 2,752 | 64 |
| plate | 13,704 | 354 | 2,464 | 64 |
| cymbal | 11,704 | 368 | 12,272 | 52 |
| noise | 7,292 | 264 | 1,008 | 48 |

The synth object sits in `.data` in seven of the eight (every member has a
constant initialiser).  In the original, and in the kernel unit, it sits in
`.bss`: the kernel's mode arrays have no initialiser.  The original's own
notes record both placements loading on hardware.  None of the eight has run
on a drumlogue yet.

## Maintaining the units

Each unit is now independent: a change to one cannot reach another.  The
price is that a fix which applies to several has to be made in each — the code
is still recognisably the original's, so a fix usually ports by search and
replace.  Before a refactor of a unit, `run_equiv.sh` is the check that it
changed nothing; once a unit's voicing is changed on purpose it will (rightly)
start to differ from the original, and the script then shows exactly which
presets and knobs moved.

[`dead_params.py`](dead_params.py) works on any unit's driver, so it can show
whether a knob has become dead after a change.  Python 3 with numpy is needed
for `compare_dumps.py`; the ARM scripts need
`apt-get install g++-arm-linux-gnueabihf qemu-user`.

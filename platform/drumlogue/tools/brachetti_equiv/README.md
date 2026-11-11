# Brachetti family — one unit per instrument family, and the tools that check them

> **Disclaimer:** the Brachetti units are unofficial, independently developed units, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from their use. See the [repository disclaimer](../../../../README.md#disclaimer) for full terms.

**Documents, top-down** (the repository README's [documentation map](../../../../README.md#documentation-map) has them all):
this file → each unit's `README.md` (presets, knobs) and `CLAUDE.md` (rules
for changing it) → [`brachetti_common/README.md`](../../brachetti_common/README.md)
(the shared library and its helper index) → the original's documents (the
voicing history).

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

### Presets on the drumlogue

The family units expose their presets to the drumlogue's preset menu
(`.num_presets` is the unit's preset count) as well as through the `Program`
parameter.  They first inherited the original's arrangement — `.num_presets`
0 (its pass 47), Program the only preset control — and on hardware the panel
then kept showing the previous preset's knob values (at boot, preset 0's)
whatever Program selected: the drumlogue owns
the values its panel draws, re-reads them after `unit_load_preset()`, and has
no way for a unit to report that another parameter moved them (EffeESP32
found the same: [Instrument selection and the panel](../../EffeESP32/README.md#instrument-selection-and-the-panel)).
Program is kept for what only a parameter has: it is stored with the pattern
and can be sequenced and motion-recorded.  Both call the same `LoadPreset()`,
and `unit_get_preset_index()` reports the preset loaded either way.  The
equivalence driver refuses to run a unit whose `.num_presets` or Program
range disagrees with its preset table.

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
bit-identically to the original exactly as before (below).  On ARM six of the
eight are smaller than before (the kernel and noise units grew by about 300
bytes) and the render costs are within 0.2 % of before or below (Sizes, CPU).

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

### Sizes

[`arm_size.sh`](arm_size.sh) cross-compiles a unit the way the SDK Makefile
does and prints its sections (gcc 13 here, not the vendor gcc 6.5, so `.text`
moves a little between toolchains; data sections are exact):

| Unit | .text | .rodata | .data | .bss | total, before → after the restructuring |
|---|---|---|---|---|---|
| brachetti (original) | 53,628 | 34,508 | 4 | 108,124 | |
| kick | 15,568 | 912 | 1,952 | 64 | 19,266 → 18,556 |
| tom | 14,156 | 1,644 | 1,872 | 64 | 18,768 → 17,812 |
| kernel | 10,716 | 31,248 | 4 | 15,428 | 57,216 → 57,540 |
| snare | 15,504 | 1,288 | 2,000 | 64 | 19,844 → 18,920 |
| bar | 12,484 | 1,840 | 1,584 | 64 | 16,918 → 16,060 |
| plate | 13,004 | 1,080 | 1,808 | 64 | 16,650 → 16,020 |
| cymbal | 11,120 | 684 | 11,936 | 52 | 24,420 → 24,296 |
| noise | 7,520 | 300 | 992 | 48 | 8,672 → 8,920 |

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
eight, so check all eight:

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
whether a knob has become dead after a change.  Python 3 with numpy is needed
for `compare_dumps.py`; the ARM scripts need
`apt-get install g++-arm-linux-gnueabihf qemu-user`.

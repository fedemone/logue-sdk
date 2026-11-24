# Braquetti — plan

Braquetti is Brachetti with a parallel sequencer built in: three lanes of 16
steps, each lane playing one Brachetti instrument family, each step one
instrument (Program) of that family.  The sequencer follows the drumlogue's
own sequencer through the Multi-engine part's gates.

This file holds what the hardware showed (SeqProbe, two runs; Braquetti,
two), the design that follows from it, and the open items.  The unit is
built and host-tested ([README.md](README.md) is its user guide), and ran on
a drumlogue twice; it is not yet stable there (findings 14–15).

## What the drumlogue does (SeqProbe)

| # | Measured | Consequence |
|---|---|---|
| 1 | The panel never redraws by itself.  A knob's text or bitmap is redrawn when a knob on that page turns, and when the page is entered (Clock, Blink, Grid playhead; polling rate 1 Hz, 0 knobs polled) | No blinking cursor, no live playhead.  Every display shows the state at its last redraw |
| 2 | The panel shows about 8 characters per value | Every display text fits in 8 characters |
| 3 | The panel reads the unit's values (`unit_get_param_value`) when a program loads and after a preset loads (Owned 70 at start, 90 after preset ProbeB), not when a page is entered (Owned stayed 70 while the unit held 30).  It counts a turn from its own number (71) | A knob's stored number is not the step's content.  Step knobs are read as turns (+1 / -1) applied to what the step holds: confirmed by the lane prototype (one name per click; `---` → `AcSnre` after a lane change) |
| 4 | A program load sets all 24 knobs at once | The restore guard (a burst of sets is not an edit) is needed |
| 5 | Gates come with velocity 64; an accented trig comes with 127 (run 2: `v64-127`).  The drumlogue's sequencer has accents, not per-step velocity | An accent on step 1 marks the bar: the clock re-aligns there |
| 6 | Counting gates mod 16 drifted: Gate showed step 15 on the first bar, one less each bar.  It is not resets: run 2 had 154 gates, 5 resets, 0 gates between the last two (resets come in pairs, on stop) | Cause unknown; the accent re-alignment (5) makes the count right at every bar anyway |
| 7 | Stop sends `unit_reset` (in pairs), Play `unit_resume` (Event `Rst>Res`); reset runs on the audio thread, gates, knobs and texts on other threads.  The audio thread runs under scheduling class 6, SCHED_DEADLINE (`pol6 0`) | Edits and gates reach the audio thread through lock-free hand-offs |
| 8 | Notes played live did not reach the unit (Event showed only `Rst>Res`) | No step keys as buttons: knobs are the only input |
| 9 | `unit_init` runs when the unit is first used: 18 s after power-on in run 1 (it was in the power-on program), 74 s in run 2 (selected later; file test skipped as `late`).  At boot a file in `Programs/` can be read and written, and it survives a power cycle (4 ms) | Files can be read only when Braquetti is in the program loaded at power-on.  Nothing can be saved while the drumlogue runs |
| 10 | The lane view works: page 4 shows four steps of the lane chosen on page 5, each knob shows and edits its own step | The step pages below |

## What Braquetti did on the drumlogue (first run)

| # | Seen | Consequence |
|---|---|---|
| 11 | It loads and runs, and the knobs work.  At 120 BPM: lane A Kernel, Timpani on steps 1–4, Taiko on 7; lane B Noise, Clap on 3 and 4, HHat-C on 6 and 7, Shaker on 9, ShkBack on 12; lane C Lippa, Snare on 2, 4, 10, 11, 13, 15, 16.  It played fine | The design holds; four Timpani in a row, next to the other lanes, ran |
| 12 | The overview (one 16×16 bitmap for all 48 steps) was too small to use | Four bitmaps, steps 1–4 … 13–16, a 3×3 square per step |
| 13 | Moving lane B onto Lippa, lane C's family, stopped the audio at once (silence, not clicks) | One family per lane (below) |

What 13 was is not established.  The CPU model saw no overload: the
pattern averaged ~1,421 instructions per frame before the move and ~1,466
after, and its heaviest block was ~4,856 before and ~4,951 after
(`test/cpu_blocks.sh`, before the strikes were staggered).  The host build
under the address and undefined-behaviour sanitizers found no fault and no
NaN.  Either the device ran out of time where the model does not show it,
or one engine pair playing two lanes' programs went wrong there.  Either
way, one family per lane removes it.

What the model showed when it was asked why:

- **A strike's first block is the costliest.**  A Timpani's is ~3,250
  instructions per frame, then ~1,650 for the blocks after it.  The
  pattern's heaviest blocks were steps where the Timpani struck with other
  lanes in the same block (steps 2–4).  Since then the lanes of a step
  strike one per block (1.3 ms apart), and the limits play the Timpani
  alone.  The pattern as they now play it peaks at ~4,340, on step 1, where
  older voices are still fading for the Timpani, and ~3,400 on steps 2–4.
  A run of Timpani does not grow the peak: every strike costs the same, the
  first to the eighth (Timpani alone on steps 1–8).
- **Measuring every instrument found two faults**, both fixed:
  - the Rainstk never sounded.  Its sound starts 9 ms after the strike,
    Brachetti's ring-over marks a copy silent after an all-zero block, and
    Braquetti rendered only families marked sounding;
  - the budget counted every voice at its family's heaviest instrument
    (the Cymbal family at the Gong's cost), so Kick2, AcSnre and HHat-O on
    one step faded the kick 2.7 ms after its strike.  It now counts each
    instrument's own cost, and the step limits keep a step within the
    budget.

## What Braquetti did on the drumlogue (second run)

The build with the step limits (one family per lane, the Timpani alone and
at most four in a row, a step within the budget), at 120 BPM.

| # | Seen | Consequence |
|---|---|---|
| 14 | Kernel: Timpani on steps 1–4, Taiko on 5–8.  Cymbal: Cymbal on 5, Gong on 9, Ride on 12, 13, 16.  Pizzico: Harp on 9 and 11, Oud on 12 and 16, Fretls on 14 and 15.  Some clicks after the four Timpani; it ran a minute or two, then silence | The limits did not keep it running |
| 15 | Bar: Marmba on 1–4, TblrBel on 7.  Cordiera: Tabor on 5, Daf on 7, TomBuzz on 8, Marranz on 9, Tabor on 10 and 11, MbiraBz on 12.  Snare: a snare on 15 of the 16 steps, changing instrument on most (AcSnre, BrshSnr, MrchSnr, CajBass, RimShot, CajSlap).  The audio stopped after a few seconds | Not explained by the CPU model (below) |
| 16 | Lane A's Family knob showed `!!!` on Snare and Noise before anything was placed | As designed: lanes B and C start on them (one family per lane) |

**The CPU model does not explain 14 and 15.**  Measured as the build
played them (`cpu_cycles.sh`, `cpu_blocks.sh`), against the first run's
pattern (11), which ran:

| Pattern | Average, cycles per frame | Mean, instructions per frame | Heaviest block | On the drumlogue |
|---|---|---|---|---|
| 11 (Kernel, Noise, Lippa) | ~1,421 | ~1,337 | ~4,856 | ran |
| 14 (Kernel, Cymbal, Pizzico) | ~1,743 | ~1,728 | ~4,956 | clicks, silence after 1–2 min |
| 15 (Bar, Cordiera, Snare) | ~1,453 | ~1,443 | ~3,030 | silence after seconds |

15 is lighter than 11 on both counts, yet failed fastest.  Nor did anything
else show: both patterns ran three minutes on the host (the real build's
`-ffast-math` included) and two under qemu-arm as the ARM build, with no
NaN, no infinity, no subnormal output and no render time growing.  What
the model cannot see: the drumlogue's caches (three families' code and
data per block, where Brachetti renders one), what else ran on the
drumlogue during the test, and calls from two threads at once.  What 14
and 15 share and 11 lacks: a delay-line family (Pizzico, Cordiera) changing
instrument on most of its steps, so both of its ring-over copies stay busy;
11's Lippa played one instrument.

What this build does about it:

- **One voice per lane** (every family's Poly at 1): a lane's new hit
  fades its previous one, and an instrument change keeps the old copy
  ringing only for its 10 ms fade.  On the model, 15's average falls by
  about a quarter, 14's and 11's by about a tenth.
- **A Limit knob** (page 6, 50–100 % of the budget), which re-checks every
  step when it moves, and **a CPU meter** (page 6): the time Braquetti
  takes per audio block, measured on the drumlogue, its heaviest block,
  late blocks, each lane's share.  The next run measures instead of
  guessing (README.md, "The next hardware test").
- **A NaN guard**: a family whose output is not finite is silenced for
  that block and reset, and counted on page 6, so a NaN can never reach
  the drumlogue's mix and effects.
- **The gate release no longer goes through the hit queue**, which has one
  producer: SeqProbe did not tell whether gate-on and gate-off come on the
  same thread.  It is an atomic mask, applied once the gate's strikes are
  done.

If the meter shows the CPU is the cause, the budget and the costs get
calibrated to it; if the limit is low, two lanes (the user's fallback).

## Design

### Knob pages

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| 1–4 | Step 1, 5, 9, 13 | Step 2, 6, 10, 14 | Step 3, 7, 11, 15 | Step 4, 8, 12, 16 |
| 5 | Lane (A / B / C) | Family of that lane | Level of that lane | Layer: what the steps edit (Inst, Prob, Accent), and clearing |
| 6 | CPU meter | Late blocks, NaN guard | The lane's CPU | Limit (50–100 % of the budget) |

- Pages 1–4 show the 16 steps of the edit lane, one beat per page.  A step
  shows its instrument's name or `---`; a turn moves it one instrument on
  from what it holds.  The family is the lane's, so a step's list is that
  family's Programs.
- Page 5 is "the selected lane": Lane picks it, and every other knob there
  is that lane's (its family — Brachetti's presets once main has them —, its
  level…).  Turning a knob redraws the whole page (finding 1: Clock and Grid
  followed any turn), so the family shown is the new lane's at once; the
  step pages show it when entered.  One family per lane is chosen; the steps
  pick the instrument.
- The Layer knob (absolute, like Lane) chooses what pages 1–4 edit: the
  instrument, the probability (25–100 %) or the accent (velocity 127, the
  level Brachetti voices its instruments at; a normal trig is 64).  Its
  last two values are actions, clear the lane and clear all lanes, which
  run only on a single click made 0.3 s or more after the one before and
  never in a program load.  Buttons do not reach a unit (finding 8), so an
  edit layer has to be a knob.
- Page 6 is kept for loading and saving patterns.  For now it holds the
  CPU meter and the Limit.  The overview that was there (four static
  bitmaps, finding 12) stays in the code: `-DBRQ_OVERVIEW` builds it back.
  A knob cannot switch between a picture and text (a knob's type is fixed
  in header.c), so an "overview on / off" knob that turns page 6 from the
  overview into load/save knobs is not possible.
- A family change keeps the steps' positions: a step that is on stays on,
  on the same program number if the new family has it, else on its last.

### Engines: Brachetti's family engines, one pool

Brachetti on main is one unit holding the twelve families (its presets) and
their 114 instruments (`Program`).  Two ways to sequence it were weighed:

- **The whole unit, `brachetti::Synth`, once per lane: not possible.**  Its
  engines and its family table are file-scope statics in
  `brachetti_synth.h`, so a process has exactly one; and four families
  (Lippa, Pizzico, World, Cordiera) keep their delay lines at file scope,
  sized for one ring-over pair.  As it is, it would put all three lanes on
  one family (no kick + snare + hat).
- **The single families, through Brachetti's family table: chosen.**
  `brachetti::kFamilies` gives, per family, its ring-over engine pair and
  calls for Program, strike, render, its instruments' names.  Braquetti
  includes it (`catalog.h`) and routes each lane's hits to its family's
  engine: one engine pool, as in Brachetti, so the memory is Brachetti's
  (about 400 KB, known to load on the drumlogue) and every instrument plays
  bit for bit as in Brachetti and its family unit.  A family or instrument
  added to Brachetti reaches Braquetti with the next build.

What that means:

- **One family per lane** (finding 13).  Two lanes on one family shared its
  engine pair, and that stopped the audio on the drumlogue, so the Family
  knob shows `!!!` on a family another lane plays and the lane keeps its
  own.  Lanes on different families are fully independent.  Two lanes on
  one family would take one pool per lane (three times the memory, and the
  four delay-line families made to hold six copies instead of two), and a
  hardware test of why 13 stopped.
- **A strike** sets the family's `Program` to the step's instrument, then
  gates it, exactly as Brachetti's Program knob and gate do; with Poly at 1
  (one voice per lane, finding 15) the previous hit fades in ~10 ms.  It
  strikes at 64, or at 127 where the step is accented (the Accent layer);
  the gate's own accent only marks the bar.  The instruments play at their
  shipped knob values but Poly (no sound design in Braquetti).  Every
  engine call happens on the audio thread, at the start of a block, from
  the hit queue, one strike per block.
- **Render**: every family struck and still sounding or holding a live
  voice renders into a scratch buffer and is summed (as Brachetti renders a
  family left ringing); a family whose output is not finite is silenced and
  reset; one limiter on the sum.  A lane's level scales its family's output.
- **CPU** (`limits.h`): Brachetti's guarantee is one instrument's voices
  per family; three lanes can sound three families at once.  Every
  instrument's cost was measured (one strike alone, its first 32 ms:
  `test/instrument_costs.sh`), the Timpani 1,530, the Gong 1,000, Riq and
  Pandeir 810, every other 240–660.  Before a strike, older voices fade until
  the live voices, each at its instrument's cost, fit a budget of 1,800; a
  hit of the same step is never faded.  The step limits keep a step within
  that budget, and the Timpani — the heaviest block of all is its strike —
  alone on its step and at most four in a row (finding 11 ran four).  The
  lanes of a step strike one per block.  README.md, "CPU", has the
  measurements.

### Clock

- The Multi part carries a trig on every step, step 1 accented; each gate is
  one step, an accented gate (velocity above 64) is step 1.
  `unit_reset` (sent on stop) restarts at step 1.

### Keeping a pattern

The drumlogue saves the 24 knob numbers with each program, but the numbers
do not follow the steps (finding 3).  Candidates:

1. **A store gesture.**  After a preset load the panel re-reads every value
   (finding 3).  A preset named for it would let the unit hand back its
   pattern encoded in the knobs' numbers, and saving the program would keep
   it.  Test first: does a program saved after that preset load keep those
   numbers, and does loading it hand them back?  The encoding is open again:
   Tom now has 32 instruments, so a step of one lane takes 6 bits and a step
   of all three lanes (18 bits) no longer fits one knob's number.
2. **Patterns read at boot** from files prepared on a computer (finding 9).
   Read-only: edits made on the drumlogue are lost at power-off.

With 1, the drumlogue's own program save, naming, browser and chain become
the pattern store and the song mode.

## TODO

- [x] SeqProbe, second run: accent = velocity 127; no notes from the step keys; resets only on stop.
- [ ] Test the store gesture (Keeping a pattern, 1).
- [x] Align with Brachetti on main: the catalog is Brachetti's family table (12 families, 114 instruments).
- [x] Sequencer core, host-tested: the pattern, step turns, the restore guard, the clock (below).
- [x] The unit: header.c, the callbacks, hits routed to the family engines, the mix, the CPU budget; its size (Brachetti's plus 1.4 KB) and CPU (README.md).
- [x] Hardware, first run: it loads, plays in time, the knobs work (findings 11–13).
- [x] Four overview knobs; one family per lane; the step limits; one strike per block; the budget per instrument.
- [x] Hardware, second run: two patterns stopped the audio (findings 14–15), unexplained by the model.
- [x] One voice per lane, the Limit knob, the CPU meter, the NaN guard, the release mask; the Layer knob (probability, accent, clearing); the overview hidden.
- [ ] Hardware, third run, with the meter (README.md, "The next hardware test"): what else runs; patterns 15, 14 and 11 at Limit 100, 80, 60 %, reading CPU, Late and LaneCPU.  Then calibrate the budget and the costs to the drumlogue, or go to two lanes.
- [ ] Page 6: loading and saving patterns, and perhaps two sound knobs (attack, release), once the CPU is settled.
- [ ] Song mode through the drumlogue's chain, if the store gesture works.

## Code so far

| File | What it holds |
|---|---|
| `seq_core.h` | `brq::Sequencer<Catalog, Policy>`: three lanes × 16 steps, each with an instrument, a probability and an accent; the knobs (steps, Family, Level read as turns; Lane and Layer absolute; one family per lane, a step the Policy refuses, clearing, the four overview bitmaps, 8-character texts); the restore guard (a burst of sets, or sets right after a preset load, edits nothing and undoes the turn it started with); the clock (one gate per step, an accent or the first gate after a reset is step 1); `HitQueue`, the lock-free hand-off from gates to the audio thread.  No constructor, no heap, no clock of its own |
| `limits.h` | `brq::CpuLimits`, the Policy: each instrument's measured cost (`kCost`), the budget and the Limit, the three step limits |
| `catalog.h` | The lanes' families and instruments, read from Brachetti's family table (`../brachetti/brachetti_synth.h`); including it brings Brachetti's engines in (the pool above; link `../brachetti/family_headers.c`).  The core reads only `kFamilies`, `kMaxPrograms`, `family_name`, `programs`, `program_name`, `default_family`.  Defaults: lane A Kick, B Snare, C Noise |
| `test/catalog_test.cc` | The catalog with Brachetti's engines compiled in: the 12 families and their counts (114), every name within 8 characters, the core driving Tom to its 32nd instrument and a family change on the real table |
| `unit.cc`, `header.c`, `family_headers.c` | The unit (README.md, "Files") |
| `test/unit_test.cc` | The whole unit through its callbacks, against Brachetti's engines (README.md, "Tests") |
| `test/cpu_bench.cpp`, `test/cpu_cycles.sh`, `test/cpu_blocks.sh`, `test/instrument_costs.sh` | The CPU estimate: the average, the heaviest blocks, each instrument's cost |
| `test/core_test.cc`, `test/run_tests.sh` | Host test, on a small catalog of its own (no engines): defaults, step turns and their ends, turns counted from the panel's number and from the unit's answer, resync, lane switch, family change keeping positions, level, a program load, a burst that starts with a turn (family change undone), the clock (wrap, accent, reset), the overview, the queue, one family per lane, a Policy refusing a step, `limits.h`'s three rules (and that they hold after 400 random edits), and gates on one thread while knobs edit on another.  `./run_tests.sh` runs all three tests (ASan/UBSan); `ARM=1 ./run_tests.sh` the ARM builds under qemu-arm |

Next: the second hardware test (TODO).

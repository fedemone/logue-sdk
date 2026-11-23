# Braquetti — plan

Braquetti is Brachetti with a parallel sequencer built in: three lanes of 16
steps, each lane playing one Brachetti instrument family, each step one
instrument (Program) of that family.  The sequencer follows the drumlogue's
own sequencer through the Multi-engine part's gates.

This file holds what the hardware showed (SeqProbe, two runs), the design
that follows from it, and the open items.  The sequencer core is built and
host-tested; the unit around it is not.

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

## Design

### Knob pages

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| 1–4 | Step 1, 5, 9, 13 | Step 2, 6, 10, 14 | Step 3, 7, 11, 15 | Step 4, 8, 12, 16 |
| 5 | Lane (A / B / C) | Family of that lane | Level of that lane | open (pitch, decay… of that lane) |
| 6 | Overview: a bitmap of all 3 lanes × 16 steps | open (length, pattern…) | | |

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
- Page 6's overview is a static bitmap (finding 1): it shows the pattern as
  it was when the page was entered.
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

- **Two lanes on the same family share its engine pair**: its two copies
  let two of its instruments sound at once (the ring-over); a third strike
  while both ring fades the older (~10 ms).  Lanes on different families are
  fully independent.  If that limit bites, the way out is one pool per lane
  (three times the memory, and the four delay-line families made to hold six
  copies instead of two).
- **A strike** sets the family's `Program` to the step's instrument, then
  gates it at the step's velocity: the family's ring-over lets the previous
  instrument ring on.  The instruments play at their shipped knob values
  (no sound design in Braquetti).
- **Render**: every family still sounding renders into a scratch buffer and
  is summed (as Brachetti renders a family left ringing); one limiter on the
  sum.  A lane's level scales its family's output while that lane owns it.
- **CPU**: Brachetti's guarantee is one instrument's voices per family.
  Three lanes on three families can sound three families at once, so each
  family's `Poly` is set to 1 in Braquetti (a new hit fades the previous
  one, a drum machine's choke): three voices in all, plus what rings over.
  Measure with `tools/brachetti_equiv/unit_cycles.sh` before calling it safe,
  the Kernel (two kettles, no Poly) on all three lanes first.

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
- [ ] The unit: header.c, the callbacks, hits routed to the family engines, the mix; sizes (`arm_size.sh`) and CPU (`unit_cycles.sh`), then hardware.
- [ ] Song mode through the drumlogue's chain, if the store gesture works.

## Code so far

| File | What it holds |
|---|---|
| `seq_core.h` | `brq::Sequencer<Catalog>`: three lanes × 16 steps; the knobs (steps, Lane, Family, Level read as turns, the overview bitmap, 8-character texts); the restore guard (a burst of sets, or sets right after a preset load, edits nothing and undoes the turn it started with); the clock (one gate per step, an accent or the first gate after a reset is step 1); `HitQueue`, the lock-free hand-off from gates to the audio thread.  No constructor, no heap, no clock of its own |
| `catalog.h` | The lanes' families and instruments, read from Brachetti's family table (`../brachetti/brachetti_synth.h`); including it brings Brachetti's engines in (the pool above; link `../brachetti/family_headers.c`).  The core reads only `kFamilies`, `kMaxPrograms`, `family_name`, `programs`, `program_name`, `default_family`.  Defaults: lane A Kick, B Snare, C Noise |
| `test/catalog_test.cc` | The catalog with Brachetti's engines compiled in: the 12 families and their counts (114), every name within 8 characters, the core driving Tom to its 32nd instrument and a family change on the real table |
| `test/core_test.cc`, `test/run_core_test.sh` | Host test, on a small catalog of its own (no engines): defaults, step turns and their ends, turns counted from the panel's number and from the unit's answer, resync, lane switch, family change keeping positions, level, a program load, a burst that starts with a turn (family change undone), the clock (wrap, accent, reset), the overview, the queue, and gates on one thread while knobs edit on another.  `./run_core_test.sh` runs both (ASan/UBSan); `ARM=1 ./run_core_test.sh` the ARM builds under qemu-arm |

Next: the unit around it — header.c with the knob layout above, the SDK
callbacks, hits routed to Brachetti's family engines (Engines, above), the
mix; then its size and CPU, then hardware.

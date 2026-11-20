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

### Sound

- One engine instance per lane: a Brachetti engine fades every voice of its
  instance on a program change, so two lanes cannot share one.
- One voice per lane, the next hit cutting the previous one off.
- Each instance renders to its own buffer; one limiter on the sum, and a
  level per lane.
- A CPU budget across lanes: on hardware the original Brachetti stopped the
  audio at about twice its known-good render cost, and one Timpani kettle
  costs about half of that.  Measure before allowing the heavy families on
  every lane.

### Clock

- The Multi part carries a trig on every step, step 1 accented; each gate is
  one step, an accented gate (velocity above 64) is step 1.
  `unit_reset` (sent on stop) restarts at step 1.

### Keeping a pattern

The drumlogue saves the 24 knob numbers with each program, but the numbers
do not follow the steps (finding 3).  Candidates:

1. **A store gesture.**  After a preset load the panel re-reads every value
   (finding 3).  A preset named for it would let the unit hand back its
   pattern encoded in the step knobs' numbers (3 lanes × 5 bits in one
   16-bit number per step), and saving the program would keep it.  Test
   first: does a program saved after that preset load keep those numbers,
   and does loading it hand them back?
2. **Patterns read at boot** from files prepared on a computer (finding 9).
   Read-only: edits made on the drumlogue are lost at power-off.

With 1, the drumlogue's own program save, naming, browser and chain become
the pattern store and the song mode.

## TODO

- [x] SeqProbe, second run: accent = velocity 127; no notes from the step keys; resets only on stop.
- [ ] Test the store gesture (Keeping a pattern, 1).
- [ ] Align with Brachetti on main once its families are presets and its instruments the Program values.
- [x] Sequencer core, host-tested: the pattern, step turns, the restore guard, the clock (below).
- [ ] One engine per lane, the mix, the CPU budget, measured on hardware.
- [ ] Song mode through the drumlogue's chain, if the store gesture works.

## Code so far

| File | What it holds |
|---|---|
| `seq_core.h` | `brq::Sequencer<Catalog>`: three lanes × 16 steps; the knobs (steps, Lane, Family, Level read as turns, the overview bitmap, 8-character texts); the restore guard (a burst of sets, or sets right after a preset load, edits nothing and undoes the turn it started with); the clock (one gate per step, an accent or the first gate after a reset is step 1); `pack_column` / `pack_families` for the store gesture; `HitQueue`, the lock-free hand-off from gates to the audio thread.  No constructor, no heap, no clock of its own |
| `catalog.h` | The eight Brachetti families and their programs as the family units name them today; the core reads only `kFamilies`, `family_name`, `programs`, `program_name`, `default_family`.  Follows Brachetti on main when its families become presets |
| `test/core_test.cc`, `test/run_core_test.sh` | Host test: defaults, step turns and their ends, turns counted from the panel's number and from the unit's answer, resync, lane switch, family change keeping positions, level, a program load, a burst that starts with a turn (family change undone), the clock (wrap, accent, reset), the overview, packing, the queue, every name within 8 characters, and gates on one thread while knobs edit on another.  `./run_core_test.sh` (ASan/UBSan) and `ARM=1 ./run_core_test.sh` (qemu-arm) |

Next: the unit around it — header.c with the knob layout above, the SDK
callbacks, one Brachetti engine per lane playing the hits — once Brachetti
on main has its families as presets (or on today's family units, to start
measuring CPU).

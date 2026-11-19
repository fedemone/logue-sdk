# Braquetti — plan

Braquetti is Brachetti with a parallel sequencer built in: three lanes of 16
steps, each lane playing one Brachetti instrument family, each step one
instrument (Program) of that family.  The sequencer follows the drumlogue's
own sequencer through the Multi-engine part's gates.

This file holds what the hardware showed (SeqProbe, first run), the design
that follows from it, and the open items.  Nothing is built yet.

## What the drumlogue does (SeqProbe, first run)

| # | Measured | Consequence |
|---|---|---|
| 1 | The panel never redraws by itself.  A knob's text or bitmap is redrawn when a knob on that page turns, and when the page is entered (Clock, Blink, Grid playhead; polling rate 1 Hz, 0 knobs polled) | No blinking cursor, no live playhead.  Every display shows the state at its last redraw |
| 2 | The panel shows about 8 characters per value | Every display text fits in 8 characters |
| 3 | The panel reads the unit's values (`unit_get_param_value`) when a program loads and after a preset loads (Owned 70 at start, 90 after preset ProbeB), not when a page is entered (Owned stayed 70 while the unit held 30).  It counts a turn from its own number (71) | A knob's stored number is not the step's content.  Step knobs are read as turns (+1 / -1) applied to what the step holds: confirmed by the lane prototype (one name per click; `---` → `AcSnre` after a lane change) |
| 4 | A program load sets all 24 knobs at once | The restore guard (a burst of sets is not an edit) is needed |
| 5 | Gates come with velocity 64.  The drumlogue's sequencer has accents, not per-step velocity | Step 1 cannot be marked by velocity unless an accent raises it (open) |
| 6 | Counting gates mod 16 drifted: Gate showed step 15 on the first bar, one less each bar | Open: SeqProbe's Results 13–16 now count gates between resets, resets, all-notes-offs and the velocity range |
| 7 | Stop sends `unit_reset` (twice, apparently), on the audio thread; gates, knobs and texts arrive on other threads | Edits and gates reach the audio thread through lock-free hand-offs |
| 8 | Notes played live did not show in Event | Probably no step keys as buttons (the display was cut off: to confirm) |
| 9 | `unit_init` runs at boot (18 s after power-on, first init).  At boot a file in `Programs/` can be read and written, and it survives a power cycle (4 ms) | Pattern or sound libraries can be read at boot.  Nothing can be saved while the drumlogue runs |
| 10 | The lane view works: page 4 shows four steps of the lane chosen on page 5, each knob shows and edits its own step | The step pages below |

## Design

### Knob pages

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| 1–4 | Step 1, 5, 9, 13 | Step 2, 6, 10, 14 | Step 3, 7, 11, 15 | Step 4, 8, 12, 16 |
| 5 | Lane A family | Lane B family | Lane C family | Edit lane (A / B / C) |
| 6 | open (level, length, pattern…) | | | |

- Pages 1–4 show the 16 steps of the edit lane, one beat per page.  A step
  shows its instrument's name or `---`; a turn moves it one instrument on
  from what it holds.  The family is the lane's, so a step's list is that
  family's Programs.
- Page 5 chooses each lane's family (Brachetti's presets once main has them)
  and which lane pages 1–4 edit.  Changing page refreshes the step pages, so
  nothing on screen is stale after a lane change (finding 1).
- A family change on a lane must say what happens to its steps (cleared, or
  kept by position): to decide.

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

- The Multi part carries a trig on every step; each gate is one step.
  `unit_reset` (sent on stop) restarts at step 1.
- Pending finding 6: if a gate goes missing or the drumlogue resets per bar,
  the count must re-align on whatever marks the bar.

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

- [ ] SeqProbe, second run: Results 13–16 with an accent on step 1 (finding 6), Event with live notes (finding 8).
- [ ] Test the store gesture (Keeping a pattern, 1).
- [ ] Align with Brachetti on main once its families are presets and its instruments the Program values.
- [ ] Sequencer core, host-tested: the pattern, step turns, the restore guard, the clock.
- [ ] One engine per lane, the mix, the CPU budget, measured on hardware.
- [ ] Song mode through the drumlogue's chain, if the store gesture works.

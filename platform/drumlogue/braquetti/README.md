# Braquetti — a three-lane step sequencer playing Brachetti

> **Disclaimer:** Braquetti is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

Braquetti is a drumlogue synth unit for the Multi-engine part.  It plays
three lanes of 16 steps, in time with the drumlogue's own sequencer.  Each lane plays one
[Brachetti](../brachetti/README.md) family (Kick, Tom, Snare, Noise…), and
each step one of that family's instruments, or nothing.  All twelve families
and their 114 instruments are there, playing exactly as in Brachetti.

**Tested on a drumlogue once**: it loads, plays in time and its knobs work.
That test also found the limits below: two lanes on one family stopped the
audio.  [PLAN.md](PLAN.md) has what the hardware showed, the design and the
open items.

| | |
|---|---|
| Unit name on the drumlogue | `Braquetti` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42725174` ('BrQt') |
| Presets | none |
| Knobs | 23 (pages 1–6) |
| ARM size (SDK Makefile) | .text 156,808 · .rodata 62,936 · .data.rel.ro 2,564 · .data 46,688 · .bss 354,336: Brachetti's, plus 2.9 KB of code and 0.8 KB of tables |

## Setting it up

1. Build it and copy it to the drumlogue (below), restart, and select
   **Braquetti** on the Multi-engine part.
2. Put a trig on **all 16 steps** of the Multi part: each trig is one step of
   Braquetti, so the Multi part's trigs are its clock.
3. Put an **accent on step 1** of the Multi part.  An accented trig is
   Braquetti's step 1, so the lanes stay on the bar whatever happens to a
   trig.  The accent itself does not sound louder: every step strikes as a
   normal trig.
4. Press Play.  Stop resets: the next Play starts on step 1.

Without the accent it still starts on step 1 after every Stop; the accent
only keeps it there.

## The knobs

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| 1 | Step 1 | Step 2 | Step 3 | Step 4 |
| 2 | Step 5 | Step 6 | Step 7 | Step 8 |
| 3 | Step 9 | Step 10 | Step 11 | Step 12 |
| 4 | Step 13 | Step 14 | Step 15 | Step 16 |
| 5 | Lane | Family | Level | — |
| 6 | Steps 1–4 | Steps 5–8 | Steps 9–12 | Steps 13–16 |

- **Lane** (A, B, C) chooses the lane that pages 1–4 and page 5 show and edit.
- **Family** is the selected lane's family: Kick, Tom, Kernel, Snare, Bar,
  Plate, Cymbal, Noise, World, Lippa, Pizzico, Cordiera
  ([Brachetti's table](../brachetti/README.md#the-presets-one-per-family)
  lists their instruments).  Out of the box lane A is Kick, B Snare, C Noise.
  **Each family plays on one lane at most**: a family another lane plays
  shows `!!!` (below).
- **Level** is the selected lane's level, 0–100 % (80 % to start).
- **Steps 1–16** show the selected lane's instrument on each step, or `---`
  (no hit), or `!!!` (refused, below).  Each click moves one instrument:
  right from `---` to the family's first instrument and on, left back down
  to `---`.
- **Page 6, the overview**, shows all three lanes, four steps per knob:

  ![The overview: lane A Timpani on steps 1-4 and a Taiko on 7; lane B and
  lane C with their hits, four of them refused (outlined)](overview.png)

  One row per lane, A at the top.  Each step is a square: **filled**, it
  plays; **outlined**, it is refused (`!!!`); a **dot**, it is empty.  The
  selected lane's empty steps are dashes, which tell you which lane pages
  1–5 edit.  (Above: the hardware test's pattern, as the limits below now
  play it.)

How the knobs behave:

- **A knob always shows the lane's content, never the knob's own number**,
  and a turn moves on from what it shows.  The drumlogue keeps its own
  number for every knob, but Braquetti counts only the clicks.
- **Changing a lane's family keeps its steps**: a step that is on stays on,
  on the same instrument number if the new family has it, otherwise on the
  family's last instrument.  Each is checked against the limits again.
- **The screen does not redraw by itself** (SeqProbe measured it).  After
  changing Lane, change page to see the new lane's steps.  The overview shows
  the lanes as they were when you opened page 6.
- **Loading a program does not edit the lanes**.  The drumlogue sets every
  knob at once when it loads a program, and Braquetti recognises that as a
  load, not as 24 turns.

## When the panel shows `!!!`

`!!!` means Braquetti refused what you just chose, to keep the drumlogue's
audio running.  The drumlogue stops a synth unit's audio when it asks for
too much CPU, and three lanes can ask for three families' worth.  These are
restrictions you will hit; they are deliberate.

### On the Family knob: one family per lane

Browsing past a family that another lane plays shows `!!!`, and the lane
keeps playing the family it had.  Turn on to a free family and the lane
takes it.  Selecting the lane again shows the family it plays.

Why: in the hardware test, moving a second lane onto Lippa (lane C's
family) stopped the audio at once.

### On a step: too heavy with the other lanes

A step shows `!!!` when the instrument chosen there is too heavy next to
what the other lanes play on that step.  The step then plays nothing.  The
knob still points at the refused instrument, so one more click either way
chooses again, and is checked again.  The rules:

1. **A Timpani plays alone on its step.**  On a step where one lane plays
   the Timpani (Kernel's `Timpni`), the other lanes play nothing.  This
   works both ways: a Timpani is refused on a step where another lane
   already plays.  The Taiko, Kernel's other instrument, is not limited.
2. **At most four Timpani in a row.**  The fifth Timpani in a row is
   refused.  The 16 steps loop, so steps 15, 16, 1, 2, 3 are five in a row.
3. **A step's three instruments must fit the CPU budget together.**  Each
   instrument has a measured cost (`limits.h`, below).  In practice:
   - any three instruments fit, except the Timpani, the Gong (Cymbal), Riq
     and Pandeir (World);
   - the Gong leaves room for one more instrument, or two light ones (a
     kick and a closed hat; not a kick and a clap);
   - Riq or Pandeir leaves room for a kick and a clap, not a kick and a
     snare;
   - Gong and Riq (or Pandeir) never share a step.

What breaks a rule is always the instrument you choose last; what already
plays is never changed.  The same goes for a family change: the lane's
steps are checked again, in order from step 1, and a step that no longer
fits shows `!!!`.

**A refused step stays refused** until you turn its knob, even when what
blocked it is gone.  For example, remove the Timpani from step 3 and lane
B's refused clap there still shows `!!!`.  Turn it one click away and back
to bring it in.

## The sound

- **Every instrument plays exactly as in Brachetti**, at its shipped
  settings.  Braquetti strikes Brachetti's own engines, built in, and its
  host test checks a hit sample for sample against Brachetti's engine, at
  the lane's level.
- **A new instrument lets the previous one ring on**: Brachetti's ring-over.
  Within one family, two instruments can sound at once.
- **The lanes of one step strike 1.3 ms apart**, lane A first.  The first
  audio block a strike sounds in costs far more than the next ones (a
  Timpani's about twice), so Braquetti strikes one instrument per block
  rather than three in one.
- **A CPU budget across families.**  Every voice counts at its instrument's
  measured cost.  Before a strike that would go over the budget, older
  voices fade, the costliest first (in about 10 ms).  A hit of the same
  step is never faded, as the step limits keep each step within the
  budget.  In a busy pattern some tails are shortened (a Timpani's most of
  all, being the costliest).  A light beat is unaffected.
- **Stop silences everything at once**; the gate's length shapes the noise
  release as it does on Brachetti.
- MIDI notes, pitch bend and tempo are not used: the lanes play the gates.

## What it does not do yet

- **Patterns are not kept.**  The drumlogue saves the knob numbers with a
  program, but they are not the steps, so a pattern is lost when the unit is
  reloaded or the drumlogue switched off.  PLAN.md, "Keeping a pattern", has
  the candidates.
- **No song mode.**  It depends on keeping patterns.

## CPU

All figures here are estimates.  The CPU was measured under qemu-arm with
the SDK's build flags: a model for comparing cases, not the device's clock.
Two scripts count instructions:

- `test/cpu_cycles.sh` gives the average cost per frame, weighted as
  [`unit_cycles.sh`](../tools/brachetti_equiv/README.md) weights it.
- `test/cpu_blocks.sh` gives the heaviest 64-frame blocks, which matter
  because the drumlogue must finish every block in time.

On this model, BrcKernel's Timpani struck twice (~2,080 on average) runs on
the drumlogue, and Brachetti's first ring-over build (~3,400) stopped its
audio.

**What each instrument costs** (`kCost` in `limits.h`).  Each of the 114
instruments was struck once, alone (`test/instrument_costs.sh`).  Its cost
is the instructions per frame of its first 32 ms, less the unit's own 25.
A voice costs about that while it sounds:

| Instruments | Cost |
|---|---|
| Timpni (Kernel) | **1,530**: the only heavy one |
| Gong (Cymbal) | **1,000** |
| Riq, Pandeir (World) | **810** |
| Cymbal's others (Cymbal, Ride, RidBel, HHat-O, Splash) | 610–660 |
| Taiko, toms, kicks, snares, plates, Lippa, Cordiera, Shekere | 360–560 |
| Bar, Pizzico, World's others | 320–410 |
| Noise (Clap, hats, shakers) | 240–300 |

**The budget** (`kCpuBudget`) is 1,800, for the live voices together.  It
leaves room below the ~2,080 known to run for what it cannot see:
- voices that keep rendering for ~10 ms while they fade;
- the Kernel's skeleton, which rings on after a kettle gives up its fill.

The step limits keep what one step strikes within the budget, so making
room only ever fades older voices.

**Playing**, each lane on its instrument on every step, as the limits play
it (a refused step plays nothing).  These are averages over a bar, in
cycles per frame (`cpu_cycles.sh`):

| Lanes A / B / C | 16ths at | Cycles per frame |
|---|---|---|
| Kick2 / AcSnre / HHat-C | 120 / 240 BPM | ~1,359 / ~1,312 |
| Kick2 / AcSnre / HHat-O | 240 BPM | ~1,728 |
| Cymbal / Taiko / TomBuzz | 240 BPM | ~1,872 |
| Gong / Kick2 / HHat-C | 240 BPM | ~1,951 |
| Riq / Cymbal / Clap | 240 BPM | ~1,981 |
| Timpni alone | 120 BPM | ~1,458 |
| Timpni / Gong / MmKick: the Timpani on 12 steps (1–4, 6–9, 11–14), Gong and MmKick on the other 4 | 120 / 240 BPM | ~1,673 / ~1,800 |
| The hardware test's pattern, as the limits play it | 120 BPM | ~1,404 |

The heaviest mixes the limits allow stay below the ~2,080 known to run.
Before the limits, Timpni / Gong / MmFloor on every step was ~2,755 at
240 BPM, and the hardware test's pattern ~1,421 at 120 BPM.

**The heaviest blocks.**  A strike's first block is the costliest moment of
a voice, and the drumlogue must finish every block in time, so the heaviest
single block matters as much as the average.  These are instructions per
frame (`cpu_blocks.sh`):

| Pattern | Mean | Heaviest block |
|---|---|---|
| The hardware test's pattern, before the limits, the Timpani striking with the other lanes: **ran** | ~1,337 | ~4,856 (step 4) |
| The same pattern, as the limits play it | ~1,400 | ~4,341 (step 1's Timpani, older voices fading) |
| Timpni / Gong / MmKick, 240 BPM | ~1,768 | ~4,657 (a Timpani after a Gong step) |
| Gong / Kick2 / HHat-C, 240 BPM | ~1,895 | ~3,142 |
| Cymbal / Taiko / TomBuzz, 240 BPM | ~1,838 | ~3,004 |
| Kick2 / AcSnre / HHat-O, 240 BPM | ~1,694 | ~2,328 |

Every heaviest block is a Timpani strike or a strike while older voices
fade.  All of them stay below the ~4,856 the hardware test ran.  The
second hardware test (PLAN.md, TODO) plays these mixes.  If they all run,
the limits can be relaxed.

## Build

```
build drumlogue/braquetti
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `braquetti.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder (the [SeqProbe README](../seq_probe/README.md#build-and-install)
has the steps, and what to check if a unit does not show up).

## Files

| File | What it holds |
|---|---|
| `unit.cc` | The drumlogue callbacks: gates to hits, hits to Brachetti's engines on the audio thread (one strike per block), the mix, the CPU budget, the limiter |
| `header.c` | The knobs |
| `seq_core.h` | The sequencer: lanes, steps, the knobs that edit them (one family per lane, a refused step), the overview, the guard against program loads, the clock |
| `limits.h` | What each instrument costs, the budget, the step limits |
| `catalog.h` | The families and instruments, read from Brachetti's family table (which brings Brachetti's engines in) |
| `family_headers.c` | Brachetti's `family_headers.c`, which the table points to |
| `config.mk`, `Makefile` | The build, as Brachetti's |
| `overview.png` | The overview picture above, drawn from the unit's own bitmaps |
| `PLAN.md` | The hardware findings, the design, the open items |
| `test/run_tests.sh` | The host tests (below) |
| `test/cpu_bench.cpp` | Braquetti playing a pattern, for the three scripts below |
| `test/cpu_cycles.sh`, `test/cpu_blocks.sh` | The CPU estimate: the average, the heaviest blocks |
| `test/instrument_costs.sh` | Measures every instrument's cost: `limits.h`'s table (re-run it when Brachetti's instruments change) |

## Tests

```
test/run_tests.sh          # host, address and undefined-behaviour sanitizers
ARM=1 test/run_tests.sh    # ARM build (the SDK's flags), under qemu-arm
```

- `core_test`: the sequencer on a small catalog of its own:
  - step turns and their ends;
  - turns counted from either number the panel may hold;
  - lane and family changes, one family per lane (`!!!`);
  - a step refused by a policy;
  - the three step limits of `limits.h` (Timpani alone, four in a row, the
    budget), and that they hold on the whole pattern after 400 random edits;
  - level;
  - program loads, including a burst that starts with a turn;
  - the clock (wrap, accent, reset);
  - the overview;
  - gates and knobs on two threads.
- `catalog_test`: the catalog against Brachetti's engines: 12 families, 114
  instruments, every name within 8 characters, Tom's 32nd instrument; a
  cost in `limits.h` for every instrument, the Timpani the only heavy one,
  and the claims of "When the panel shows `!!!`" above.
- `unit_test`: the whole unit through its drumlogue callbacks:
  - a hit sample for sample against Brachetti's engine with the same history,
    at 80 % and 50 % level and on an accent (on ARM, with `-ffast-math`,
    within −90 dB per render: two source files may round a float
    differently);
  - the Rainstk, whose sound starts 9 ms after its strike, sample for sample;
  - silence right after a reset;
  - one family per lane;
  - three lanes on one step (Kick2, AcSnre, HHat-O), none faded;
  - the CPU load within its budget after every strike, in a busy pattern and
    with the heaviest families (Timpani, Cymbal, a Lippa drum) under the
    limits: the Timpani refused where the others play, four in a row, the
    fifth refused (and outlined on the overview);
  - gates, knobs and audio on three threads at once.

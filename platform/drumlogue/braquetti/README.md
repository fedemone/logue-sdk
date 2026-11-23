# Braquetti — a three-lane step sequencer playing Brachetti

> **Disclaimer:** Braquetti is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

Braquetti is a drumlogue synth unit for the Multi-engine part.  It plays
three lanes of 16 steps, in time with the drumlogue's own sequencer.  Each lane plays one
[Brachetti](../brachetti/README.md) family (Kick, Tom, Snare, Noise…), and
each step one of that family's instruments, or nothing.  All twelve families
and their 114 instruments are there, playing exactly as in Brachetti.

**Not yet tested on a drumlogue.**  Its design rests on what
[SeqProbe](../seq_probe/README.md) measured on one; [PLAN.md](PLAN.md) has
those findings, the design and the open items.

| | |
|---|---|
| Unit name on the drumlogue | `Braquetti` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42725174` ('BrQt') |
| Presets | none |
| Knobs | 20 (pages 1–6) |
| ARM size (SDK Makefile) | .text 155,288 · .rodata 62,208 · .data 46,688 · .bss 354,080: Brachetti's, plus 1.4 KB of code |

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
| 6 | Overview | — | — | — |

- **Lane** (A, B, C) chooses the lane that pages 1–4 and page 5 show and edit.
- **Family** is the selected lane's family: Kick, Tom, Kernel, Snare, Bar,
  Plate, Cymbal, Noise, World, Lippa, Pizzico, Cordiera
  ([Brachetti's table](../brachetti/README.md#the-presets-one-per-family)
  lists their instruments).  Out of the box lane A is Kick, B Snare, C Noise.
- **Level** is the selected lane's level, 0–100 % (80 % to start).
- **Steps 1–16** show the selected lane's instrument on each step, or `---`
  (no hit).  Each click moves one instrument: right from `---` to the
  family's first instrument and on, left back down to `---`.
- **Overview** draws the three lanes, one row each, 16 steps across, with
  dots under the selected lane.

How the knobs behave:

- **A knob always shows the lane's content, never the knob's own number**,
  and a turn moves on from what it shows.  The drumlogue keeps its own
  number for every knob, but Braquetti counts only the clicks.
- **Changing a lane's family keeps its steps**: a step that is on stays on,
  on the same instrument number if the new family has it, otherwise on the
  family's last instrument.
- **The screen does not redraw by itself** (SeqProbe measured it).  After
  changing Lane, change page to see the new lane's steps.  The Overview shows
  the lanes as they were when you opened page 6.
- **Loading a program does not edit the lanes**.  The drumlogue sets every
  knob at once when it loads a program, and Braquetti recognises that as a
  load, not as 24 turns.

## The sound

- **Every instrument plays exactly as in Brachetti**, at its shipped
  settings.  Braquetti strikes Brachetti's own engines, built in, and its
  host test checks a hit sample for sample against Brachetti's engine, at
  the lane's level.
- **A new instrument lets the previous one ring on**: Brachetti's ring-over.
  Within one family, two instruments can sound at once.
- **Two lanes on the same family share it**.  Lane A on Tom for a Djambe and
  lane B on Tom for a Conga both sound, but a third Tom hit while both still
  ring fades the oldest (in about 10 ms).  Lanes on different families are
  independent.
- **A CPU budget across families.**  Voices cost very different amounts (a
  Timpani kettle three times a snare), so each family's voices count at its
  measured cost.  Before a hit that would go over the budget, the costliest
  other family fades its oldest voice (in about 10 ms).  In a busy pattern
  that shortens some tails; a light beat is unaffected (below).
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

The drumlogue's audio stops when a synth unit needs too much CPU, so this
was measured before any hardware test: estimated Cortex-A7 cycles per
rendered frame, with `test/cpu_cycles.sh`.  It uses the method of
[`unit_cycles.sh`](../tools/brachetti_equiv/README.md): a model to compare
cases, not the device's clock.  On that model, BrcKernel's Timpani struck
twice (~2,080) is known to run on the drumlogue.  Brachetti's first
ring-over build (~3,400) stopped its audio.  Brachetti's Snare AcSnre ×4 is
~1,850.

**What one voice costs**, per family: the heaviest of the instruments
measured (35 in all), over the first 32 ms after a strike, which dense
playing repeats.  These are `kVoiceCost` in `unit.cc`:

| Kick | Tom | Kernel | Snare | Bar | Plate | Cymbal | Noise | World | Lippa | Pizzico | Cordiera |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 590 | 550 | **1680** (Timpni) | 570 | 450 | 570 | **1040** (Gong) | 390 | **940** (Riq, Pandeir) | 660 | 470 | 580 |

**The budget** (`kCpuBudget`) is 1,800.  It counts live voices only, and
what it cannot see adds about a quarter in the heaviest mixes:
- voices that keep rendering for ~10 ms while they fade;
- the Kernel's skeleton, which rings on after a kettle gives up its fill.

**Playing**, every lane on every step:

| Lanes A / B / C | 16ths at | Cycles per frame |
|---|---|---|
| Kick2 / AcSnre / HHat-C | 120 BPM | ~982 |
| Kick2 / AcSnre / HHat-C | 240 BPM | ~969 |
| Djambe / Conga (both Tom) / Marmba | 240 BPM | ~1,396 |
| Rainstk / MbiraBz / Harp | 240 BPM | ~1,298 |
| Pandeir / Riq (both World) / Gong | 240 BPM | ~1,540 |
| Taiko / Gong / MmFloor | 240 BPM | ~2,195 |
| Timpni / Taiko (both Kernel) / Cymbal | 240 BPM | ~2,220 |
| Timpni / Gong / MmFloor | 120 BPM | ~2,374 |
| Timpni / Gong / MmFloor | 240 BPM | ~2,755 |
| Timpni alone | 120 / 240 BPM | ~1,618 / ~1,656 |

**The Timpani is the exception.**  Whenever it sounds it costs about 1,600,
skeleton included, and the budget can neither see nor fade that.  Next to
it, the other lanes can only lose their tails, not their strikes.  So
mixes with the Timpani and other heavy families land between the level
known to run and the level known to stop the audio, and only the
drumlogue can tell which side.  The hardware test (PLAN.md, TODO) plays
that mix.  If it clicks, the remedy is to skip the other lanes' hits while
a Timpani rings.  That changes the music, so it waits for the hardware's
answer.

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
| `unit.cc` | The drumlogue callbacks: gates to hits, hits to Brachetti's engines on the audio thread, the mix, the CPU budget, the limiter |
| `header.c` | The knobs |
| `seq_core.h` | The sequencer: lanes, steps, the knobs that edit them, the guard against program loads, the clock |
| `catalog.h` | The families and instruments, read from Brachetti's family table (which brings Brachetti's engines in) |
| `family_headers.c` | Brachetti's `family_headers.c`, which the table points to |
| `config.mk`, `Makefile` | The build, as Brachetti's |
| `PLAN.md` | The hardware findings, the design, the open items |
| `test/run_tests.sh` | The host tests (below) |
| `test/cpu_bench.cpp`, `test/cpu_cycles.sh` | The CPU estimate |

## Tests

```
test/run_tests.sh          # host, address and undefined-behaviour sanitizers
ARM=1 test/run_tests.sh    # ARM build (the SDK's flags), under qemu-arm
```

- `core_test`: the sequencer on a small catalog of its own:
  - step turns and their ends;
  - turns counted from either number the panel may hold;
  - lane and family changes;
  - level;
  - program loads, including a burst that starts with a turn;
  - the clock (wrap, accent, reset);
  - gates and knobs on two threads.
- `catalog_test`: the catalog against Brachetti's engines: 12 families, 114
  instruments, every name within 8 characters, Tom's 32nd instrument.
- `unit_test`: the whole unit through its drumlogue callbacks:
  - a hit sample for sample against Brachetti's engine with the same history,
    at 80 % and 50 % level and on an accent (on ARM, with `-ffast-math`,
    within −90 dB per render: two source files may round a float
    differently);
  - silence right after a reset;
  - two lanes sounding on one family;
  - the CPU load within its budget after every hit, in a busy pattern and with the heaviest families (Timpani, Cymbal, a Lippa drum) on every step;
  - gates, knobs and audio on three threads at once.

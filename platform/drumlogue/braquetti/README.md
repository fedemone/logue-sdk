# Braquetti — a three-lane step sequencer playing Brachetti

> **Disclaimer:** Braquetti is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

Braquetti is a drumlogue synth unit for the Multi-engine part.  It plays
three lanes of 16 steps, in time with the drumlogue's own sequencer.  Each lane plays one
[Brachetti](../brachetti/README.md) family (Kick, Tom, Snare, Noise…), and
each step one of that family's instruments, or nothing.  All twelve families
and their 114 instruments are there, playing exactly as in Brachetti.

**Tested on a drumlogue twice, and not yet stable.**  It loads, plays in
time and its knobs work.  But in the second test two patterns stopped the
audio, one after a few seconds, one after a minute or two.  The CPU model
used so far does not explain why.  This build adds restrictions (one voice
per lane) and a CPU meter with a Limit knob, to find out on the drumlogue
itself: [the next test](#the-next-hardware-test).  [PLAN.md](PLAN.md) has
what the hardware showed, the design and the open items.

| | |
|---|---|
| Unit name on the drumlogue | `Braquetti` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42725174` ('BrQt') |
| Presets | none |
| Knobs | 24 (pages 1–6) |
| ARM size (SDK Makefile) | .text 159,852 · .rodata 63,032 · .data.rel.ro 2,588 · .data 46,688 · .bss 354,784: Brachetti's, plus 6 KB of code and 1.2 KB of data |

## Setting it up

1. Build it and copy it to the drumlogue (below), restart, and select
   **Braquetti** on the Multi-engine part.
2. Put a trig on **all 16 steps** of the Multi part: each trig is one step of
   Braquetti, so the Multi part's trigs are its clock.
3. Put an **accent on step 1** of the Multi part.  An accented trig is
   Braquetti's step 1, so the lanes stay on the bar whatever happens to a
   trig.  It does not make step 1 louder; accents are set per step in
   Braquetti (the Accent layer, below).
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
| 5 | Lane | Family | Level | Layer |
| 6 | CPU | Late | LaneCPU | Limit |

- **Lane** (A, B, C) chooses the lane that pages 1–4 and page 5 show and edit.
- **Family** is the selected lane's family: Kick, Tom, Kernel, Snare, Bar,
  Plate, Cymbal, Noise, World, Lippa, Pizzico, Cordiera
  ([Brachetti's table](../brachetti/README.md#the-presets-one-per-family)
  lists their instruments).  Out of the box lane A is Kick, B Snare, C Noise.
  **Each family plays on one lane at most**: a family another lane plays
  shows `!!!` (below).  So from the start, lane A finds Snare and Noise
  taken, until lanes B and C move elsewhere.
- **Level** is the selected lane's level, 0–100 % (80 % to start).
- **Layer** chooses what the step knobs edit, for the selected lane:

  | Layer | Each step knob shows and edits |
  |---|---|
  | `Inst` | the step's instrument, or `---` (no hit).  Each click moves one instrument: right from `---` to the family's first and on, left back down to `---` |
  | `Prob` | how often the step plays: `100%`, `75%`, `50%`, `25%` (a new step plays every time) |
  | `Accent` | `Accent` or `Normal`: an accented step strikes at full velocity (127), the level its instrument was voiced at; a normal one at the drumlogue's normal 64 |
  | `Clear?`, `ClrLane`, `ClrAll` | two actions, below; the step knobs show the instruments |

  In every layer a step with no instrument shows `---`, and a refused step
  `!!!`.  A new step plays every time and is not accented; turning a step
  off and on again keeps its probability and accent.
- **Clearing**: turn Layer to `Clear?`, pause, then one more click right.
  `ClrLane` clears the selected lane (every step off, at 100 %, not
  accented; the family and level stay) and shows `Cleared`.  Pause again
  and one more click: `ClrAll` clears all three lanes and shows `AllClear`.
  A fast spin of the knob or a program load never clears anything: an
  action needs a single click made at least 0.3 s after the one before.
- **Page 6** is the CPU meter and the Limit (below).  It is meant for
  loading and saving patterns later.  The overview that was there (four
  pictures of the three lanes) is still in the code: a build with
  `-DBRQ_OVERVIEW` in `config.mk` puts it back on page 6, in place of the
  meter.

How the knobs behave:

- **A knob always shows the lane's content, never the knob's own number**,
  and a turn moves on from what it shows.  The drumlogue keeps its own
  number for every knob, but Braquetti counts only the clicks.  (Lane,
  Layer and Limit are the exceptions: their number is what they show.)
- **Changing a lane's family keeps its steps**: a step that is on stays on,
  on the same instrument number if the new family has it, otherwise on the
  family's last instrument.  Each is checked against the limits again.
- **The screen does not redraw by itself** (SeqProbe measured it).  After
  changing Lane or Layer, change page to see the steps as they now are.
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

Why: in the first hardware test, moving a second lane onto Lippa (lane C's
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
3. **A step's instruments must fit the CPU budget together.**  Each
   instrument has a measured cost (`limits.h`, below), and the budget is
   1,800 at Limit 100 %.  One instrument alone always plays.  At 100 %:
   - any three instruments fit, except the Timpani, the Gong, Riq, Pandeir
     and the Cymbal family's;
   - a cymbal, a ride or an open hat fits with a kick and a snare;
   - the Gong fits with a closed hat and a clap, not with a kick and a
     snare;
   - Riq or Pandeir fits with a kick and a clap, not with a kick and a
     cymbal.

What breaks a rule is always the instrument you choose last; what already
plays is never changed.  The same goes for a family change: the lane's
steps are checked again, in order from step 1, and a step that no longer
fits shows `!!!`.

**A refused step stays refused** until you turn its knob, even when what
blocked it is gone.  For example, remove the Timpani from step 3 and lane
B's refused clap there still shows `!!!`.  Turn it one click away and back
to bring it in.  The one exception is the Limit knob: changing it checks
every step again (lane A first, step 1 first), so lowering it refuses
steps until the pattern fits, and raising it brings them back.

## The sound

- **Every instrument plays as in Brachetti**, at its shipped settings,
  apart from Poly (below).  Braquetti strikes Brachetti's own engines,
  built in, and its host test checks a hit sample for sample against
  Brachetti's engine, at the lane's level.
- **One voice per lane.**  A lane's new hit fades its previous one (in
  about 10 ms), as Brachetti does with its Poly knob at 1: each lane is
  like a drum machine's track.  A long tail (a gong, a timpani, a ride)
  rings until the lane's next hit.
- **A new instrument lets the previous one ring on** for those 10 ms only:
  Brachetti's ring-over, cut short by the one voice.
- **The lanes of one step strike 1.3 ms apart**, lane A first.  The first
  audio block a strike sounds in costs far more than the next ones (a
  Timpani's about twice), so Braquetti strikes one instrument per block
  rather than three in one.
- **A CPU budget across lanes.**  Every voice counts at its instrument's
  measured cost.  Before a strike that would go over the budget, the other
  lanes' voices fade, the costliest first (in about 10 ms).  A hit of the
  same step is never faded, as the step limits keep each step within the
  budget.
- **A safety net**: if an engine ever produced a NaN or an infinity, its
  output for that block is silenced and the engine starts afresh, so it
  cannot reach the drumlogue's mix (page 6 counts it).  The host and ARM
  tests have never seen one.
- **Stop silences everything at once**; the gate's length shapes the noise
  release as it does on Brachetti.
- MIDI notes, pitch bend and tempo are not used: the lanes play the gates.

## What it does not do yet

- **Patterns are not kept.**  The drumlogue saves the knob numbers with a
  program, but they are not the steps, so a pattern is lost when the unit is
  reloaded or the drumlogue switched off.  PLAN.md, "Keeping a pattern", has
  the candidates; page 6 is kept for it.
- **No song mode.**  It depends on keeping patterns.

## The CPU meter (page 6)

Braquetti times every audio block it renders with the drumlogue's clock.
The panel reads a knob's text only when a knob of that page turns or the
page is entered, so **turn any page 6 knob to read the meter again**.  The
first three knobs show; they edit nothing.

| Knob | Shows | Example |
|---|---|---|
| CPU | the time Braquetti took over the last second, then its heaviest single block since Stop, both in per cent of an audio block's time (1.33 ms) | `34/87%` |
| Late | `L`: audio blocks that started more than half a block late since Stop, a sign the drumlogue fell behind; `N`: engine outputs that were a NaN or an infinity and were silenced (since the unit loaded) | `L0 N0` |
| LaneCPU | the selected lane's share of the last second | `B 12%` |
| Limit | the CPU budget the lanes may use, 50–100 % (100 % = 1,800) | `100%` |

If the audio thread stops calling Braquetti, CPU shows `stalled`; without
the clock, `noclk`.  Stop zeroes the heaviest block and `L`.  The per cent
are of the whole block's time: the drumlogue needs part of it for itself,
and how much is what the next test should tell.

## The next hardware test

The goal is to learn where the drumlogue's limit is, so the restrictions
can be set from measurements instead of guesses.  The second test ran with
the drumlogue's own sequencer and parts playing, and those take time from
the same processor, so this test separates the two.

**Setup**: this build; 120 BPM; the Multi part with a trig on all 16 steps
and an accent on step 1; Limit 100 %.  Pick one pattern for the
drumlogue's other parts and keep it for every run.

**Each run**: play it for two minutes, or until the audio stops.  Turn a
page 6 knob to read CPU and Late at the start and every 20 seconds or so.
For LaneCPU, choose the lane on page 5 first.  If the audio stops, read all
four knobs straight away, before anything else.  Run it first with the
other parts **muted**, then **playing**.

The runs go from light to heavy, so a crash (which needs a restart and the
pattern entered again) comes as late as possible:

| Run | Braquetti plays | What it tells |
|---|---|---|
| 0 | nothing (empty lanes) | what Braquetti costs idle |
| 1 | lane C Snare: AcSnre on every step | one light lane |
| 2 | lane B Cordiera: Tabor on every step | a delay-line family, one instrument |
| 3 | lane B Cordiera: Tabor, Daf, TomBuzz, Marranz, MbiraBz, then again, on every step | the same, changing instrument at every step (what the failing patterns had) |
| 4 | the second test's pattern 2 (Bar, Cordiera, Snare) | the pattern that failed fastest |
| 5 | if 4 stopped: pattern 2 at Limit 70 %, then 50 % | how much less is enough |
| 6 | the second test's pattern 1 (Kernel, Cymbal, Pizzico) | the Timpani, and its clicks |

Per run, note: muted or playing; CPU (`average/peak`); Late (`L`, `N`);
LaneCPU of each lane that plays; clicks; and when the audio stopped, if it
did.

What it will tell:
- **CPU near or over 100 %, `L` climbing before the stop**: the CPU, and at
  which per cent; the budget and the costs get calibrated to it.
- **Runs 2 and 3 far apart**: changing instrument on a delay-line family
  costs far more on the drumlogue than the model says.
- **Muted and playing far apart**: the drumlogue's own parts leave
  Braquetti less time than assumed.
- **CPU well under 100 % and `L` at 0 when it stops**: not the CPU.  Then
  `N` above 0 points at an engine, and `stalled` at the drumlogue no longer
  calling Braquetti.

## CPU (the model)

All figures here are estimates.  The CPU was measured under qemu-arm with
the SDK's build flags: a model for comparing cases, not the device's clock.
Two scripts count instructions:

- `test/cpu_cycles.sh` gives the average cost per frame, weighted as
  [`unit_cycles.sh`](../tools/brachetti_equiv/README.md) weights it.
- `test/cpu_blocks.sh` gives the heaviest 64-frame blocks, which matter
  because the drumlogue must finish every block in time.

On this model, BrcKernel's Timpani struck twice (~2,080 on average) runs on
the drumlogue, and Brachetti's first ring-over build (~3,400) stopped its
audio.  **The second hardware test contradicts it**: its pattern 2 is
lighter on this model than the first test's pattern, which ran, yet it
stopped the audio within seconds (below).

**What each instrument costs** (`kCost` in `limits.h`).  Each of the 114
instruments was struck once, alone (`test/instrument_costs.sh`).  Its cost
is the instructions per frame of its first 32 ms, less the unit's own 38.
A voice costs about that while it sounds:

| Instruments | Cost |
|---|---|
| Timpni (Kernel) | **1,550**: the only heavy one |
| Gong (Cymbal) | **1,020** |
| Riq, Pandeir (World) | **830** |
| Cymbal's others (Cymbal, Ride, RidBel, HHat-O, Splash) | 620–670 |
| Taiko, toms, kicks, snares, plates, Lippa, Cordiera, Shekere | 370–580 |
| Bar, Pizzico, World's others | 330–430 |
| Noise (Clap, hats, shakers) | 260–320 |

**The budget** (`kCpuBudget`) is 1,800 at Limit 100 %, for the live voices
together.  It leaves room below the ~2,080 known to run for what it cannot
see:
- voices that keep rendering for ~10 ms while they fade;
- the Kernel's skeleton, which rings on after a kettle gives up its fill.

**The hardware tests' patterns** at 120 BPM, as this build plays them (one
voice per lane): the average in cycles per frame (`cpu_cycles.sh`), the
mean and heaviest block in instructions per frame (`cpu_blocks.sh`):

| Pattern | On the drumlogue | Average | Mean | Heaviest block |
|---|---|---|---|---|
| First test: Kernel (Timpani ×4, Taiko), Noise, Lippa (Snare) | ran (an earlier build) | ~1,270 | ~1,265 | ~4,015 |
| Second test, 1: Kernel (Timpani ×4, Taiko ×4), Cymbal, Pizzico | clicks, then silence after 1–2 min | ~1,613 | ~1,600 | ~4,649 |
| Second test, 2: Bar, Cordiera, Snare | silence after a few seconds | ~1,076 | ~1,069 | ~2,691 |

As the second test's build played them (two voices per family), they were
~1,743 and ~1,453 on average, against ~1,421 for the first test's pattern
on its build.  The heaviest blocks are Timpani strikes made while older
voices fade.

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
| `unit.cc` | The drumlogue callbacks: gates to hits, hits to Brachetti's engines on the audio thread (one strike per block), one voice per lane, the mix, the CPU budget, the NaN guard, the limiter, the CPU meter |
| `header.c` | The knobs |
| `seq_core.h` | The sequencer: lanes, steps and their layers (instrument, probability, accent), the knobs that edit them (one family per lane, a refused step, clearing), the overview, the guard against program loads, the clock |
| `limits.h` | What each instrument costs, the budget and the Limit, the step limits |
| `catalog.h` | The families and instruments, read from Brachetti's family table (which brings Brachetti's engines in) |
| `family_headers.c` | Brachetti's `family_headers.c`, which the table points to |
| `config.mk`, `Makefile` | The build, as Brachetti's (`-DBRQ_OVERVIEW`: the overview on page 6) |
| `overview.png` | What the hidden overview drew: the first hardware test's pattern under the limits |
| `PLAN.md` | The hardware findings, the design, the open items |
| `test/run_tests.sh` | The host tests (below) |
| `test/cpu_bench.cpp` | Braquetti playing a pattern, for the three scripts below |
| `test/cpu_cycles.sh`, `test/cpu_blocks.sh` | The CPU model: the average, the heaviest blocks |
| `test/instrument_costs.sh` | Measures every instrument's cost: `limits.h`'s table (re-run it when Brachetti's instruments or `unit.cc`'s render change) |

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
    budget), the Limit and its re-check of every step, and that the rules
    hold on the whole pattern after 400 random edits;
  - the layers: probability (a 25 % step plays about 1,000 bars in 4,000),
    accent (velocity 127), and clearing, which a quick click, a jump or a
    program load never triggers;
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
  - a hit sample for sample against Brachetti's engine with the same history
    (Poly 1, as the unit), at 80 % and 50 % level, on the bar's accent and
    accented in the Accent layer (velocity 127) (on ARM, with
    `-ffast-math`, within −90 dB per render: two source files may round a
    float differently);
  - the Rainstk, whose sound starts 9 ms after its strike, sample for sample;
  - one voice per lane;
  - silence right after a reset;
  - one family per lane;
  - three lanes on one step (Kick2, AcSnre, HHat-O), none faded;
  - the CPU load within its budget after every strike, in a busy pattern, at
    Limit 100 % and 70 %, and with the heaviest families (Timpani, Cymbal,
    a Lippa drum) under the limits: the Timpani refused where the others
    play, four in a row, the fifth refused;
  - page 6's texts within 8 characters, idle and playing;
  - gates, knobs and audio on three threads at once, and no render needing
    the NaN guard.

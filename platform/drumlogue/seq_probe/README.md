# SeqProbe — a test unit for the parallel sequencer

> **Disclaimer:** SeqProbe is an unofficial, independently developed test unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

SeqProbe is a synth unit that makes almost no music.  It answers, on the
drumlogue, the questions the SDK documentation leaves open and the planned
parallel sequencer depends on:

| Question | Why the sequencer needs it |
|---|---|
| Does the panel redraw a bitmap or string by itself, or only when its knob turns, or when a page is entered? | Whether a step's real content, a blinking cursor and a playhead can be shown |
| Does the panel take the number `unit_get_param_value()` returns? | How a knob turn must be read; whether a pattern can live in the knob numbers a program saves |
| What does loading a program send? | The guard that keeps a program load from writing into steps |
| What does the sequencer send: gates, per-step velocity, stop? | The clock |
| Which callbacks run on the audio thread? | Where pattern edits and playback may run |
| When does `unit_init` run, and can files be read and written then? | Whether songs and sounds can be loaded at boot |
| Do the step keys reach the unit as notes? | Whether real buttons are possible |

Every answer is on the panel: pages 1–3 live, and page 6 holds the full list
(**Result**).  Pages 4–5 are a working prototype of the proposed sequencer
layout: 3 lanes, and each knob shows one step's real content.

## Files: at boot only

The drumlogue allows file handling only before the sequencer and the synths
start.  SeqProbe touches files in one place, called from `unit_init` only, and
only when that runs at boot: the first `unit_init` since power-on, within 60 s
of the system starting.  A later one (the unit selected while the drumlogue
runs) touches nothing, and **Files** shows `late`.

At boot it lists the `Programs` folder you see over USB (on the device:
`/var/lib/drumlogued/userfs/Programs`, as
[rgrvmusic/borg](https://github.com/rgrvmusic/borg) names it) and reads back the
file the previous boot wrote.  Then it writes `seqprobe-save.txt` the way a
song save would: written as `seqprobe-save.tmp`, flushed to storage, renamed,
read back.  It touches nothing else.  After the test, delete the unit and that
file.

## Build and install

Build with the SDK's Docker image, not a desktop cross-compiler: its toolchain
matches the drumlogue's C library.

```
build drumlogue/seq_probe
```

Copy `seq_probe.drmlgunit` to `Units/Synths/`.  Select **SeqProbe** on the
Multi-engine part and save the program, so it is the one loaded at power-on.
Then restart: the boot-time file test needs SeqProbe to be loaded at boot.

## The test, step by step

**1. Page 1, hands off, about 10 seconds.**

| Knob | If it changes by itself | If it does not |
|---|---|---|
| Blink: square, top left | bitmaps redraw on their own (blink, playhead possible) | bitmaps redraw only on a turn or a page change |
| Follow: its square, in the other phase | also the bitmaps of knobs you are not touching | only some are redrawn |
| Clock: `t12`, `t13`… | strings redraw on their own | strings only on a turn or a page change |

**2. Turn knob 1 (Blink) slowly.**  Follow and FollowS show knob 1's value.
Do they follow at once, a moment later, only after you change page, or never?

**3. Page 2: whose number?**
- Owned starts at 50 in the header; the unit answers 70 when asked (20 or 90 once preset ProbeA or ProbeB has loaded).  **Shows 70, 20 or 90**: the panel reads values back from the unit, and 20 or 90 also means the program load loaded a preset.  **Shows 50**: it keeps its own.
- Turn Shadow to 3 (the unit's answer becomes 30), go to page 1 and back.  Does Owned show 30 now?  OwnedS shows `h<the number the panel passes> u<the unit's value>`.
- Turn Owned one click right.  **31**: the panel counts from the unit's number.  **51** (or 71): from its own.

**4. Load a program** (this one again, or any other that uses SeqProbe), then
look at page 2: **Burst** shows how many knobs the load set and how fast
(`24/3ms`).  Then pick preset ProbeA or ProbeB from the preset menu: Owned
should show 20 or 90 if the panel re-reads values after a preset.

**5. Page 3: the sequencer.**
- Put a trig on all 16 steps of the Multi part.  Give step 1 a different velocity from the others, then press Play.  **Gate** shows the step and the velocity that arrived (`01 v127`, `02 v100`…).  **Grid** shows a playhead on its bottom rows.  You hear a tick on every gate, plus a three-lane demo pattern: a low thump, a noise hit, a tone.
- Press Stop.  **Event** shows the last two things stop sent (`Aoff>Rset`: all-notes-off, then reset).
- Play the Multi part from the step keys (a keyboard or chromatic mode, if the drumlogue has one), or from MIDI.  **Event** shows `N60` and similar if the notes reach the unit.
- **Files** sums up the boot-time file test:
  - `rw ok`: read and written at boot.
  - `rd e13`, `wr e30`…: what failed (13 = no permission, 30 = read-only; other numbers are system error codes).
  - `late`: this `unit_init` was not at boot, so nothing was touched.

**6. Pages 4–5: the lane-view prototype.**  Three lanes of 16 steps.  Page 4
shows steps 1–4 of the lane chosen on page 5.  Each knob shows its step's
content and edits it.
- Page 4 starts on lane A: `Kick2 --- --- ---`.  Turn Step 2 right: `Kick2`, `808Sub`, `Kick`; turn it left back to `---`.
- Page 5: set Lane to `B Snare`, then go back to page 4.  Does it show lane B (`--- --- --- ---`)?  If yes, entering a page refreshes it.
- Turn a step one click.  It should move one program on from what it shows, whatever the knob did before.
- Page 3's Grid shows the lanes and your edits (one row of 2 pixels per lane).

**7. Page 6: the Result list.**  Turn **Result** through its 12 positions
and note what each shows (table below).  The polling rates (8–10) and the
adoption count (11) only fill in after steps 1–6.  Then power-cycle once and
read position 4 again: `prev ok` means a file written at boot survived.

## The Result list

| # | Shows | Means |
|---|---|---|
| 1 | `b+14s #1` | `unit_init` ran 14 s after the system started, and it is the first init since power-on.  A large number or `#2` means init comes when the unit is selected, not at boot |
| 2 | `rd ok 23` | At boot the `Programs` folder could be listed (23 entries).  `rd e13`: it could not, with that error.  `late` / `noclk`: the file test did not run (not at boot / no clock to tell) |
| 3 | `wr ok` | At boot a file could be written, flushed, renamed and read back.  `wr e30`: the error.  `wr diff`: it read back different |
| 4 | `prev ok` | The file the previous boot wrote reads back (`prev none` on the first boot) |
| 5 | `io 3.2ms` | How long the boot-time file work took |
| 6 | `aud GA` | Callbacks that ran on the audio thread: **G** gate on, **g** gate off, **N** note on, **S** knob set, **V** knob value asked, **T** text asked, **B** bitmap asked, **P** preset load, **A** all-notes-off, **R** reset, **M** tempo.  `aud none`: none of them.  `aud ?`: no audio yet |
| 7 | `FIFO 80` | The audio thread's scheduling class and priority |
| 8 | `str 30/s` | The most times the panel asked for one knob's text in one second while no knob turned.  0–2: only when a page is entered; 10 or more: it polls |
| 9 | `bmp 30/s` | The same for bitmaps |
| 10 | `knobs 4` | The most knobs polled (asked 3+ times) in one second.  4 or fewer: only the page on screen; more: other pages too |
| 11 | `unit2 own0` | Step turns made after the panel had asked for the value.  **unit**: counted from the unit's number (the panel takes it); **own**: from its own |
| 12 | `bpm 120.0` | The last tempo received |

## Files

| File | What it holds |
|---|---|
| `unit.cc` | The callbacks, the measurements, the boot-time file test, the lane prototype |
| `header.c` | The knobs (pages 1–6) and the two presets |
| `probe_draw.h` | 16×16 bitmaps in the drumlogue format, and a 3×5 digit font |
| `test/host_test.cc`, `test/run_host_test.sh` | The host test |

## Host test

```
test/run_host_test.sh          # host, address and undefined-behaviour sanitizers
ARM=1 test/run_host_test.sh    # ARM build (the SDK's -march/-mfpu), under qemu-arm
```

It drives the callbacks the way the drumlogue would:
- a real-time render thread, with panel calls from another;
- a program load;
- knob turns from a panel that keeps its own number and from one that takes the unit's;
- a burst that starts with a turn;
- gates, including one from the audio thread;
- stop, a MIDI note, tempo, a polling panel.

It then checks every display and every Result.  It also enforces the file
rule: the folder must be unchanged from the moment `unit_init` returns, and a
second, late `unit_init` must touch nothing.  It runs in a few seconds.
`ARM=1` needs `g++-arm-linux-gnueabihf` and `qemu-user`.

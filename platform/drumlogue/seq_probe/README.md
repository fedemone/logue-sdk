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

There are two units.  **SeqProbe** is the full probe.
[**SeqProbeLite**](../seq_probe_lite/) is the same probe without the two
things no other unit in this repository uses: bitmap knobs (its Blink,
Follow and Grid show text) and file access.  Install both: if SeqProbe does
not appear on the drumlogue, one boot tells whether one of those is why.

1. Build both with the SDK's Docker image, not a desktop cross-compiler: its
   toolchain matches the drumlogue's C library.

   ```
   build drumlogue/seq_probe
   build drumlogue/seq_probe_lite
   ```

   Each build ends with a size line and `Deploying to …/seq_probe.drmlgunit`
   (or `…/seq_probe_lite.drmlgunit`), a file of 13–20 KB in the project folder.
   If a build printed an error, the file there is an older one or missing.
2. Switch the drumlogue off.  Hold **REC** and switch it on: it shows up on the
   computer as a USB drive.
3. Copy both files into `Units/Synths/` on that drive, the folder that holds
   your other synth units.  Name them `00_seq_probe.drmlgunit` and
   `00_seq_probe_lite.drmlgunit`.  The drumlogue loads units in alphabetical
   order, and the prefix puts these first, ahead of any limit on how many
   synth units it loads.  The name must end in `.drmlgunit`.
4. Eject the drive on the computer and wait until it says the drive is safe
   to remove.  Only then switch the drumlogue off: a copy still in the
   computer's cache can be lost.
5. Switch the drumlogue on normally.  On the Multi-engine part, look for
   **SeqProbe** and **SeqProbeLite** where your other user synths appear.
6. Select SeqProbe (or SeqProbeLite, below) and save the program, so it is the
   one loaded at power-on.  Then restart: the boot-time file test needs
   SeqProbe to be loaded at boot.

### If SeqProbe does not appear

| What you see | What it means | What to do |
|---|---|---|
| Neither unit, nor any of your other user synths | The drumlogue did not read the folder | Check the copy went to `Units/Synths/`, then eject before switching off |
| Neither unit, but your other synths are there | The copy did not land, or the drumlogue refuses both | Repeat steps 2–5.  Then check the firmware version in the drumlogue's system menu, and tell me the version and how many `.drmlgunit` files `Units/Synths/` holds |
| Only SeqProbeLite | Bitmap knobs or file access at boot keep SeqProbe off | Tell me the firmware version: bitmap knobs need 1.1.0 or later.  Run the test with SeqProbeLite: it shows the same answers, as text |
| Both | The earlier copy did not land | Run the test with SeqProbe |

SeqProbeLite answers every question except the boot-time file test: its
**Files** and Result positions 2–4 show `lite`.

## How to use it

Most knobs here are displays, not controls: turning them changes nothing,
and what they show changes by itself.  Only the knobs marked **turn** do
something.

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| 1 | Blink — **turn** (0–15) | Follow — shows knob 1 | Clock — seconds since start | FollowS — shows knob 1 |
| 2 | Owned — **turn** | OwnedS — shows numbers | Shadow — **turn** (0–10) | Burst — last program load |
| 3 | Grid — **turn** (cursor step) | Gate — step and velocity | Event — last two events | Files — boot file test |
| 4 | Step 1 — **turn** | Step 2 — **turn** | Step 3 — **turn** | Step 4 — **turn** |
| 5 | Lane — **turn** (A, B, C) | | | |
| 6 | Result — **turn** (16 positions) | | | |

**Clock only ever counts up**: it is the seconds since the unit started,
not a knob value.  The knob itself does nothing.  What matters is *when* the
number changes (step 1).

Work through the steps in order and write down each answer; at the end,
send the answers and the 12 Result values.

**Step 1 — does the screen redraw by itself?**  Go to page 1 and do not touch
anything for 10 seconds.
- A: does the small square in **Blink** blink?  (SeqProbeLite: does the `*` / `-` change?)
- B: does the square in **Follow** blink too?
- C: does **Clock** count up (`t12`, `t13`…) without you touching anything?

**Step 2 — does turning one knob redraw the others?**  Turn knob 1 (Blink)
a few clicks.
- D: do **Follow** and **FollowS** show the new number at once, a moment later, only after you leave the page and come back, or never?

**Step 3 — whose number?**  Go to page 2.
- E: what number does **Owned** show (50, 70, 20 or 90)?
- Turn **Shadow** to 3.  Go to page 1, then back to page 2.
- F: what does **Owned** show now?  And **OwnedS** (`h.. u..`)?
- Turn **Owned** one click right.
- G: what number does it show now?

**Step 4 — what does loading a program send?**  Load a program that uses
SeqProbe (this one again is fine).  Go to page 2.
- H: what does **Burst** show?  (`24:3ms` = 24 knobs set within 3 ms.)
- Open the preset menu and pick **ProbeB**.  Go to page 2.
- I: what does **Owned** show (90 = the screen re-reads after a preset)?

**Step 5 — what does the sequencer send?**  Put a trig on all 16 steps of
the Multi part.  Give step 1 a different velocity from the others.  Press
Play and go to page 3.  You should hear a tick on every step, plus a
small demo pattern.
- J: what does **Gate** show while playing?  Does the velocity (`v…`) differ on step 1?
- K: does **Grid** show a moving dot near the bottom (the playhead)?  (SeqProbeLite: does `p..` count?)
- Press Stop.
- L: what does **Event** show?  (`Aof>Rst` = all-notes-off, then reset; `Rst>Rst` = two resets.)
- If you can play the Multi part from the step keys, or from MIDI, play a few notes.
- M: does **Event** show `N60` or similar?
- N: what does **Files** show?  (`rw ok`, `late`, `lite`, or an error such as `wr e30`.)

**Step 6 — the lane view.**  Go to page 4: it shows steps 1–4 of lane A
(`Kick2 --- --- ---`).
- Turn **Step 2** right a few clicks, then left back to `---`.
- O: does each click move one name (`---`, `Kick2`, `808Sub`, `Kick`)?
- Go to page 5, set **Lane** to `B Snare`, go back to page 4.
- P: does page 4 now show lane B (`--- --- --- ---`)?
- Turn **Step 1** one click.
- Q: does it start from what it showed (`---` → `AcSnre`), or jump?

**Step 7 — the Result list.**  Go to page 6 and turn **Result** through its
16 positions.  Write down what each shows (the table below says what each
means).  Then switch the drumlogue off and on once, and read position 4 again.

## The Result list

| # | Shows | Means |
|---|---|---|
| 1 | `14s #1` | `unit_init` ran 14 s after the system started, and it is the first init since power-on.  A large number or `#2` means init comes when the unit is selected, not at boot |
| 2 | `rd 23` | At boot the `Programs` folder could be listed (23 entries).  `rd e13`: it could not, with that error.  `late` / `noclk`: the file test did not run (not at boot / no clock to tell) |
| 3 | `wr ok` | At boot a file could be written, flushed, renamed and read back.  `wr e30`: the error.  `wr diff`: it read back different |
| 4 | `prev ok` | The file the previous boot wrote reads back (`prev none` on the first boot) |
| 5 | `io 3.2ms` | How long the boot-time file work took |
| 6 | `aud GA` | Callbacks that ran on the audio thread: **G** gate on, **g** gate off, **N** note on, **S** knob set, **V** knob value asked, **T** text asked, **B** bitmap asked, **P** preset load, **A** all-notes-off, **R** reset, **M** tempo.  `aud none`: none of them.  `aud ?`: no audio yet |
| 7 | `FIFO 80` | The audio thread's scheduling class and priority (`pol6 0`: a class with no name here, 3 = BATCH, 5 = IDLE, 6 = DEADLINE) |
| 8 | `str 30Hz` | The most times the panel asked for one knob's text in one second while no knob turned.  0–2: only when a page is entered; 10 or more: it polls |
| 9 | `bmp 30Hz` | The same for bitmaps (always 0 on SeqProbeLite, which has none) |
| 10 | `knobs 4` | The most knobs polled (asked 3+ times) in one second.  4 or fewer: only the page on screen; more: other pages too |
| 11 | `u2 o0` | Step turns made after the panel had asked for the value.  **u**: counted from the unit's number (the panel takes it); **o**: from its own |
| 12 | `120.0bpm` | The last tempo received |
| 13 | `g1234` | Gates received since the unit started |
| 14 | `gpr 16` | Gates between the last two resets.  If the drumlogue resets the unit once per bar, this is the gates per bar |
| 15 | `r12 a3` | Resets and all-notes-offs received since the unit started |
| 16 | `v64-127` | The lowest and highest gate velocity seen (an accent should raise the highest) |

Every value fits in 8 characters, about what the panel shows before it cuts
the text off.

## Files

| File | What it holds |
|---|---|
| `unit.cc` | The callbacks, the measurements, the boot-time file test, the lane prototype |
| `header.c` | The knobs (pages 1–6) and the two presets |
| `probe_draw.h` | 16×16 bitmaps in the drumlogue format, and a 3×5 digit font |
| `test/host_test.cc`, `test/run_host_test.sh` | The host test |
| [`../seq_probe_lite/`](../seq_probe_lite/) | SeqProbeLite: a `header.c` and `unit.cc` that set `PROBE_LITE` and include these.  They are separate files, not paths to this folder in its `config.mk`: the SDK Makefile adds each source's folder to make's `VPATH`, and make would then find this project's `build/` and objects and never build its own |

## Host test

```
test/run_host_test.sh          # host, address and undefined-behaviour sanitizers
ARM=1 test/run_host_test.sh    # ARM build (the SDK's -march/-mfpu), under qemu-arm
LITE=1 test/run_host_test.sh   # SeqProbeLite (with ARM=1 too)
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

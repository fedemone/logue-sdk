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
(`24 in 3ms`).  Then pick preset ProbeA or ProbeB from the preset menu: Owned
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
| 8 | `str 30Hz` | The most times the panel asked for one knob's text in one second while no knob turned.  0–2: only when a page is entered; 10 or more: it polls |
| 9 | `bmp 30Hz` | The same for bitmaps (always 0 on SeqProbeLite, which has none) |
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

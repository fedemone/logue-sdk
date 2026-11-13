# SeqProbe — a test unit for the parallel sequencer

> **Disclaimer:** SeqProbe is an unofficial, independently developed test unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

SeqProbe is a synth unit that makes almost no music.  It answers, on the
drumlogue, the questions the SDK documentation leaves open and the planned
parallel sequencer depends on:

| Question | Why the sequencer needs it |
|---|---|
| Does the panel redraw a bitmap or string by itself, or only when its knob turns, or when a page is entered? | Whether a step's real content, a blinking cursor and a playhead can be shown |
| Does the panel take the number `unit_get_param_value()` returns? | How a knob turn must be read to edit a step from what it holds |
| What does loading a program send? | The guard that keeps a program load from writing into steps |
| What does the sequencer send: gates, per-step velocity, stop? | The clock |
| Which callback runs on which thread? | Where pattern edits, playback and file I/O may run |
| May a unit start a thread and write files? | Song and sound files |
| Do the step keys reach the unit as notes? | Whether real buttons are possible |

Every answer goes into a log file; pages 1–3 show the same answers on the
panel in case the file cannot be written.  Pages 4–5 are a working prototype
of the proposed sequencer layout: 3 lanes, and each knob shows one step's real
content.

## What it writes to your drumlogue

Only into the `Programs` folder you see over USB (on the device:
`/var/lib/drumlogued/userfs/Programs`, the folder
[rgrvmusic/borg](https://github.com/rgrvmusic/borg) writes its logs to):

- `seqprobe.log`: appended to each session, and never grown past 2 MB.
- `seqprobe-save.txt`: the save test (written as `seqprobe-save.tmp`, then renamed).

It also reads the folder's file list into the log.  It touches nothing else.
Writing files is outside the SDK: if the drumlogue refuses, the unit runs
on and **Files** shows the error.  To stop writing, turn **Files** (page 3, knob 4)
to off.  After the test, delete the unit and the two files.

## Build and install

Build with the SDK's Docker image, not a desktop cross-compiler: its toolchain
matches the drumlogue's C library.

```
build drumlogue/seq_probe
```

Copy `seq_probe.drmlgunit` to `Units/Synths/`, restart, and select **SeqProbe**
on the Multi-engine part.

## The test, step by step

The log answers every point exactly; what you see on the panel is the backup.

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
- **Files**:
  - `ok T`: files work, with the unit's own thread.
  - `ok -`: files work, without a thread.
  - `e13`: no permission. `e30`: read-only. Other numbers are system error codes.
  - `full`: the log reached 2 MB.

**6. Pages 4–5: the lane-view prototype.**  Three lanes of 16 steps.  Page 4
shows steps 1–4 of the lane chosen on page 5.  Each knob shows its step's
content and edits it.
- Page 4 starts on lane A: `Kick2 --- --- ---`.  Turn Step 2 right: `Kick2`, `808Sub`, `Kick`; turn it left back to `---`.
- Page 5: set Lane to `B Snare`, then go back to page 4.  Does it show lane B (`--- --- --- ---`)?  If yes, entering a page refreshes it.
- Turn a step one click.  It should move one program on from what it shows, whatever the knob did before.
- Page 3's Grid shows the lanes and your edits (one row of 2 pixels per lane).

**7. Before power-off**, turn Files off and wait a second: the last lines are
written.  Then copy `Programs/seqprobe.log` over USB.

## Reading the log

```
     2.163 f=96000     tid=1246  poll P02 per second: str=42 bmp=0 get=0
```

The columns are:
1. seconds since `unit_init`
2. audio frames rendered so far
3. the thread that made the call
4. what happened

The lines that answer the questions:

| Line | Tells |
|---|---|
| `THREAD render runs here: policy=FIFO priority=…` | One line per callback the first time it runs.  Compare `tid=`: which callbacks share the audio thread |
| `str P02 v=0 (+33 ms since its last call)`, `poll P02 per second: str=30 …` | Whether and how often the panel asks.  The P numbers it asks for show the page on screen |
| `set P05 = 50 (restore)`, `RESTORE detected at set …` | What a program load sends, in what order, how fast |
| `edit P12 … counted from the unit's get answer` | The panel took the unit's number when it drew the page |
| `edit P12 … counted from the host's last set` | It kept its own number |
| `gate_on vel=127 -> step 1`, `all_note_off`, `reset`, `tempo 120.000 bpm` | The clock and what stop sends |
| `FILE save: …`, `FILE dir: …`, `FILE fs: … read-only no` | Whether a song save can work, what `Programs` holds, the free space |
| `worker thread started` or `worker: …` | Whether the unit may run its own thread |

## Files

| File | What it holds |
|---|---|
| `unit.cc` | The callbacks, the measurements, the lane prototype, the log writer and its thread |
| `header.c` | The 17 knobs (pages 1–5) and the two presets |
| `probe_log.h` | The lock-free event ring the callbacks write into |
| `probe_draw.h` | 16×16 bitmaps in the drumlogue format, and a 3×5 digit font |
| `test/host_test.cc`, `test/run_host_test.sh` | The host test |

## Host test

```
test/run_host_test.sh               # host, address and undefined-behaviour sanitizers
NO_THREAD=1 test/run_host_test.sh   # as if the drumlogue refused a thread
ARM=1 test/run_host_test.sh         # ARM build (the SDK's -march/-mfpu), under qemu-arm
```

It drives the callbacks the way the drumlogue would: a real-time render thread
and panel calls from another, a program load, knob turns from a panel that
keeps its own number and from one that takes the unit's, a burst that starts
with a turn, gates, stop, a MIDI note and a polling panel.  Then it checks
every display and the log.  It runs in a few seconds.  `ARM=1` needs
`g++-arm-linux-gnueabihf` and `qemu-user`.

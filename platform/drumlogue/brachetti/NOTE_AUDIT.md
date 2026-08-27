# Note-assignment audit (August 2026)

What the **Note** parameter does on each of the 40 presets, measured rather
than assumed.  Reproduce with:

```bash
g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    note_audit.cpp -o note_audit
./note_audit /tmp/na > /tmp/na.csv
python3 note_audit.py /tmp/na.csv
```

`note_audit.cpp` exists because `render_presets.cpp` passes its **own**
hard-coded note per preset, which is not necessarily the Note the preset
ships — and the shipped column is what the drumlogue plays.  The audit drives
`GateOn()`, the sequencer's own path, so the note under test is always
`m_ui_note` = the preset's Note parameter.

---

## 1. Engine anchors: all 8 match (nothing plays a transposed calibration)

Two engines measure the note against an internal reference rather than
synthesising it directly, so a mismatch there would mean the shipped preset
does not play the spectrum it was calibrated on:

| Preset | Shipped Note | Engine anchor | Δ |
|---|---|---|---|
| Cymbal | 65 | `ref_note` 65 | **0** |
| Gong | 50 | `ref_note` 50 | **0** |
| HHat-O | 79 | `ref_note` 79 | **0** |
| Ride | 69 | `ref_note` 69 | **0** |
| RidBel | 60 | `ref_note` 60 | **0** |
| Splash | 76 | `ref_note` 76 | **0** |
| Timpani | 52 | `kTimpaniRecipe.root_note` 52 | **0** |
| Taiko | 41 | `kTaikoRecipe.root_note` 41 | **0** |

Every one is exact.  `note_audit.cpp` reads the kernel anchors straight out of
`modal_drum_data.h`, so this table cannot silently rot; the six cymbal
`ref_note`s are still hand-copied from the `NoteOn` config switch and are the
one pair of values to re-check if that switch is edited.

## 2. Note is INERT on 8 presets — the display names a pitch you cannot hear

Measured by raising Note an octave and re-measuring: these eight move by
**exactly 0.0 semitones**, on both the spectral peak and the centroid.

| Preset | Engine | Why |
|---|---|---|
| 808Sub (2), KickDrum (20) | MEMBRANE | Both use the empty default modal config (`mode_count` 0), so the audible voice is the boom oscillator — and `boom_inc` is a preset constant, not a note. `TubRad` is the kick's tune control instead. |
| AcSnare (3), MrchSnr (8), ~~BrshSnr (37)~~ | SNARE | The modal head is ~10 % of a snare's voice and the three wire bands are absolute Hz. The note moves only the head, and not measurably. |
| Clap (21), Shaker (22), HHat-C (25) | NOISE | Shaped noise bursts — no pitched element at all. By design. |

**BrshSnr (37) left this list in pass 44** — it shipped `k_modal_mix = 0.0`, so
its head was not merely quiet, it was mixed in at ZERO.  Giving it a real head
(0.20) makes the Note live: measured 146.7 / 196.2 / 261.9 / 392.4 Hz against
nominals of 146.8 / 196.0 / 261.6 / 392.0 at notes 50 / 55 / 60 / 67.

**`note_audit` still reports it `+12c +0.0`, and that is the audit's blind spot,
not a mistuning** — its peak and centroid metrics are both dominated by the
2.1 kHz wire band, which is absolute Hz and cannot move.  Measure the head in a
120-500 Hz window to see it.  So the eight-preset count above is now seven by
mechanism and eight by this tool's metric; trust the mechanism.

RimShot (38) is the exception among the snares: it tracks a full **+12.0**,
because its rim-ring mode cluster is loud (note 69 anchors the 877 Hz honk at
ratio 2.0, as its preset comment says).

Consequence for the two kicks: what you hear is **44.7 Hz** on 808Sub and
**56.7 Hz** on KickDrum, while their Note column says 36 = 65.4 Hz.  Nothing
is broken — but the number on screen is not the pitch, and turning it does
nothing.

## 3. Cymbal-family tracking reads as nonsense on a peak metric

Peak-picking a dense inharmonic wash latches onto a *different anchor* after a
transpose, so the naive measurement reports Ride at "+56.7" and Splash at
"−63.8" semitones for a one-octave move.  On the **centroid** the same renders
move +8.0 to +15.4 semitones, i.e. the whole spectrum transposes as designed.
Read the `+12c` column for that family; `note_audit.py` prints both.

## 4. Pitch-vs-instrument: five presets worth a second opinion

These all track their note correctly — the question is whether the note names
the right register for the instrument.  **Nothing here has been changed**:
each one alters an approved sound, so they are listed for a decision.

| Preset | Ships | Sounds at | Real instrument | Suggested |
|---|---|---|---|---|
| **Bongo (33)** | 50 (D3) | 146.7 Hz | bongo open tones sit roughly 250-600 Hz; 147 Hz is conga/tumba register | 57-62 — and see §5 |
| **Wodblk (11)** | 48 (C3) | 130.7 Hz | a woodblock's dominant partial is ~0.8-2.5 kHz; 131 Hz is a large slit drum (which preset 31 already is) | 79-84 |
| **AcSnare (3)** | 38 (D2) | 73.4 Hz nominal | a snare head sounds ~180-220 Hz. **38 is the General MIDI drum-map number for Acoustic Snare**, used here as a pitch | 55-57, display only (§2: inert) |
| ~~**BrshSnr (37)**~~ | ~~38~~ → **55 (G3)** | **196.0 Hz** | **DONE in pass 44** — the recommendation above was applied when the preset got an audible head. It was free to apply *because* the note had been inert; now it is the head's pitch and no longer cosmetic. | — |
| **Trngle (19)** | 69 (A4) | 440.0 Hz | a triangle's lowest strong mode is ~1-2 kHz — this is the "Triangle C# ≈ 4434 Hz, ~40 semitones above render range" floor already in the README | 88-96 |
| **Claves (17)** | 79 (G5) | 784.0 Hz | the one clave reference in `samples/` peaks at **955-975 Hz** (centroid 1750 Hz) — 4 semitones above the preset | 83, minor |

The GM-note observation explains the snare row and is worth stating plainly:
**Brachetti's Note is a pitch, not a kit slot.** Kick2/KickDrum at 36 come from
the same GM habit, but 36 = 65 Hz is genuinely a kick fundamental, so they
land right by luck. Cowbell (18) at 392 Hz is mildly below the usual 500-800 Hz
cowbell register and is the weakest of these calls.

## 5. `render_presets.cpp` disagrees with one preset

The render harness carries its own note list, used for every calibration score
in `batch_reports/` and `rendered_tune/`.  It matches the shipped column on 40
of 40 presets:

| Preset | Preset column | render_presets.cpp |
|---|---|---|
| **Bongo (33)** | **50** (146.8 Hz) | **57** (220.0 Hz) |

So Bongo was *scored* at 220 Hz and *ships* at 147 Hz.  That makes the Bongo
row in §4 the strongest of the five: the tuning work and the shipped preset are
already a fifth apart, and 220 Hz is the closer of the two to a real bongo.
Fixing it means changing one of the two numbers — a voicing decision, so it is
left for a listen rather than applied here.

## 6. Everything else

Added in pass 33 and clean on both counts: **RackTom (39)** ships note 53,
nominal 174.6 Hz, measures **174.7 Hz (Δ +0.0 semitones)** and tracks a +12
transpose at **+12.0** — and `render_presets.cpp` scores it at the same note 53,
so it does not join Bongo in §5.

The remaining 25 presets track their note exactly (peak within ±0.1 semitone of
nominal) and sit in a defensible register for the instrument: Kick2 65 Hz,
Timpani 165 Hz (documented E3 = the dominant sustained partial), Taiko 87 Hz,
Djambe 131 Hz, AcTom 110 Hz, Conga 293 Hz, Handpan 293 Hz, Marimba/Vibraphone
523 Hz, Kalimba 349 Hz, SteelPan 262 Hz,
GlassBowl 659 Hz, GlassBottle 1319 Hz, BellTree 1047 Hz, SlitDrum 262 Hz,
Gong 147 Hz, TubularBell 523 Hz (its 1442 Hz strike note is the 4:5:6 bell
partial series doing what bells do, not a mistuning).

**Koto (9) reports `peak/nom +12.0` since pass 38 — that is the OCTAVE, not a
mistuning.**  Enabling its pluck bend redistributes energy up the harmonic
series (2nd partial 0.771 → 0.970 of the fundamental), so the audit's peak
picker now lands on 523 Hz.  A high-resolution spectrum of the 300 ms-1.5 s
window puts the fundamental at **261.75 Hz against a 261.63 Hz nominal**, with
the series exact to three decimals (1.000 / 2.000 / 3.000 / 4.000), and the
+12 transpose still tracks at **+12.0**.  Do not "correct" the tuning on the
strength of the peak column alone.

## 7. BrshSnr's Note is live, and the harness follows it (pass 44)

Both numbers moved together, so §5's one-preset disagreement does not gain a
second row: the preset column is 55 and `render_presets.cpp` scores BrshSnr at
55.  **When a preset's Note changes, change both** — that is the whole point of
§5, and Bongo is the standing example of what happens when only one moves.

## 8. GtrStr removed (pass 41)

Preset 25 `GtrStr` was removed at the user's request; every index above 25
shifted down by one, so HHat-C is 25, BrshSnr 37, RimShot 38, RackTom 39.
Koto (9) is now the only `ENGINE_KS` preset and is the KS reference in
`test_hw_debug`, `param_audit`, `samegate_probe` and `switch_probe`.

# Brachetti – Session Brief

> Project renamed **RipplerX → Brachetti** (July 2026, branch
> `claude/brachetti-review-rename-rhad77`): directory is now
> `platform/drumlogue/brachetti/`, class `BrachettiSynth`, unit name
> `"Brachetti"`.  `dev_id`/`unit_id` unchanged.

## Dev Branch

`claude/gong-preset-stacking-bqiqyw` on `fedemone/logue-sdk`
(previous work landed from `claude/brachetti-cymbal-velocity-2y8gsi`,
`claude/brachetti-review-rename-rhad77`,
`claude/snare-drum-realism-optimization-y4hrhf` and
`claude/eager-galileo-2fho84`).

Always rebuild and check the ARM section sizes — pass 32 added a cross-build
that works in-session, so this is a real check now, not a note-to-self (command
under "Host Build / Test Commands"; discussion under the constraint section).
Current shipping tree: `.text` 52,964 · `.rodata` 34,320 · `.data.rel.ro` 468 ·
`.bss` 107,948.  (Pass 45: `.text` +288 B, `.bss` +64 B — four new floats per
cymbal voice; `.rodata`/`.data.rel.ro` unchanged.)  **The "28 KB / 30 KB" limit this file used to assert here was
never true** — see the constraint section. Watch the numbers for regressions;
do not contort code to hit an imaginary ceiling.
(Pass 32 listed the third figure as `.data`; the section actually carrying the
472 bytes is `.data.rel.ro` — plain `.data` is 4 bytes.  `size -A` prints both,
so read the row name, not the row order.)

## Current Working State

- Unit **loads on hardware** (as of 081e82e); all **40** presets render clean (0 NaN/silent).
- DSP unit tests: **PASS** (exit 0); `test_hw_debug` **108/108**.
- **Pass 45 (the six ENGINE_CYMBAL presets changed — the Gong a LOT, needs a
  listen):** the HW report "multiple gong hits do not stack correctly and the
  sound is muddy" came back after passes 26 and 30 both aimed at it, and both
  were measuring the wrong thing.  Three real defects, all measured — see the
  pass entry.  Listen for: is the Gong now too bright (its `Mterl` knob still
  moves the tilt both ways from the new setting, 1.4 to 5.0), is its new level
  right against the rest of the kit, and does a repeated strike read as ONE
  gong being driven harder rather than several being restarted?  **HHat-O is
  the one to check for damage** — it is flagged "do not break", and although
  its shipped voicing is untouched, the two new filters take it 0.5 dB down and
  its sub-100 Hz energy to zero.  New tool `gong_probe`; new test T43.
- **Pass 44 (BrshSnr changed — needs a listen):** its head was mixed in at
  **zero** (`k_modal_mix = 0.0`), so the "resonance" the HW report asked for
  did not exist; it now has one, at **Note 55 (196 Hz)** instead of 38 (73 Hz,
  the GM drum-map number used as a pitch).  Velocity now moves character, not
  just level — its mappings were reading a curve that only spans 0.08..0.72.
  Listen for: is the head now too tom-like (turn `modal_mix` down: 0.14 → 8.5 dB
  of resonance, 0.10 → 5.3 dB), and does a soft stroke read as a sweep?
- **Pass 43 (Timpani + Taiko changed by one LSB — listen to a NOTE CHANGE):**
  the reported "changing note → clicks / audio interface stops" was a **CPU**
  fault, not a signal one: a note change ran two full 280-mode banks and put
  the unit at the level that crashed the hardware in pass 29.  A note change
  now costs **1.20x** a single kettle instead of 1.97x.  Listen for: does the
  previous drum thin out too much when you change note?  Both kettles still
  sound — the old one's dense wash is damped over ~40 ms, its pitched skeleton
  rings on.  Single hits are unchanged to within one 16-bit LSB.
- **HW-confirmed closed (pass 42 follow-up):** SteelPan and Triangle clicks are
  no longer reproducible.  The Timpani note-change fault was still live at that
  point and is what pass 43 addresses.
- **Pass 39 (no sound change, 41/41 byte-identical):** `VlMllStf` stopped
  saturating its clamp (20-60 % of that knob was dead on Marimba/Timpani/Clap/
  Handpan, at TWO separate sites), and **`HitPos` is now bipolar −98..98** —
  25 of 41 presets shipped it on the exact floor, so on most of the library it
  could only ever push toward the rim.  Nothing to listen for on the shipped
  sound, but both knobs should feel different (and useful) where they used to
  go numb.  New tool `plateau_probe` found all of it.
- **Pass 38 (AcSnare + Koto changed — needs a listen):** the dormant `pitch_env`
  data on both is now live, by request.  **Koto is the big one** — a pluck bend
  starting 1.5 semitones sharp, difference-RMS **+2.7 dB** (i.e. the change is
  louder than the original signal: a genuinely different sound).  AcSnare is
  subtle by comparison at **−17.8 dB**, because its own boom attack ramp hides
  most of the bend.  Listen for: is Koto's bend too much, and is AcSnare's
  worth keeping at all?  **A second, older bug fell out of this** — the KS
  sweep used `fasterpowf` for a TUNING ratio and left the string a permanent
  **half semitone sharp**; fixed.  39/41 byte-identical.
- **Pass 37 (no sound change):** the modal **overtone spread** was floored the
  same way the dive was — Inharm −40…−100 all rendered *identically*, collapsing
  every partial onto the fundamental.  Now exponential and live across the whole
  range.  Audited all five Inharm consumers: 4 live both ways, the KS allpass
  inert below centre by design.  41/41 byte-identical.
- **Pass 36 (no sound change):** `Inharm` is now **bipolar −100..100**, centre 0.
  All 41 presets shipped it at 0..3, i.e. on the floor of the old 0..199 range,
  so the knob could only ever push one way on every preset; it now cuts as well
  as boosts.  41/41 renders byte-identical.  Nothing to listen for — but the
  knob should feel different (and useful) below centre on any preset.
- **Pass 35 (808Sub changed — needs a listen):** 808Sub's documented 160→45 Hz
  pitch dive had **never fired** (the `pitch_env` clobber); it is now live.
  Listen for: is the dive too much?  It is the preset's designed behaviour, but
  the HW "perfect" verdict was given to the *flat* version, so this is a new
  sound.  Level is unchanged (RMS 0.4622 → 0.4617).  Pass 22's Inharm →
  dive-depth knob works again as a side effect — and was **re-centred** so it
  now trims as well as deepens: `Inharm` spans flat (≈51 Hz start, the old
  sound) → shipped 160 → 480 Hz, with the shipped render byte-identical.
  1 of 41 renders changed.
- **Pass 34 (fixes the pass-33 "sdeng") — needs a listen:** RackTom's mode
  cluster turned out to be a **pitch glide** misread by the FFT; it is now a
  swept boom (238→179 Hz) over a quiet, well-separated modal bank.  Energy in
  the first 25 ms **6.4 % → 34.0 %**; the swell and the "wow" notch are gone.
  Listen for: does it read as a thump now, and is the glide too much / too
  little?  **Also found: `pitch_env` is cleared per-hit and never restored, so
  808Sub's documented 160→45 Hz sweep has never fired** — left unfixed on
  purpose (it is HW-approved as it sounds); say if you want it.
- **Pass 33 (new preset — needs a listen):** preset **40 `RackTom`**, a mounted
  rack tom at note 53 (F3, 174.6 Hz) — the high drum to Ac Tom's low one.
  Built from membrane physics, **not yet calibrated against a reference sample**
  (the user is supplying one; `samples/` currently holds a single clave mp3, so
  nothing could be measured).  Listen for: does it read as the *same kit* as
  Ac Tom, and is the stick contact too bright?  The 40 older presets are
  byte-identical.  **Retuned against `samples/rock-rack-tom-1.wav` in the same
  pass** — envelope now matches the reference (t60 617 ms vs 613).  Note 53 is
  a **decided** deviation from the reference's own 113 Hz (which would collide
  with Ac Tom's 110) — see pass 33; don't "correct" it.
- **Pass 32 (code review, no sound change):** one real bug fixed — `Reset()`
  used to leave a deferred master drive queued, so a suspend caught mid-fade
  came back **14 dB loud** (T41).  Plus dead-code and size work; `.rodata` is
  1920 B smaller.  Nothing to listen for; 40/40 renders byte-identical.
- **Pass 31 (new GUI layout — say if the knob placement is wrong):** slot 3 is
  now **`Velocity`** (-100 ghost / 0 as-played / +100 wham) and the cymbal
  resonator density moved onto **`Partls`** (shows `Rs40%` on a cymbal preset).
  Listen for: ghost notes at the bottom of Velocity on snare/cymbal rolls; the
  wham forcing weak steps to full force.  Expect the wham to do **little at
  velocity 127 on Kick2/Marimba** — those pin the limiter — and a clear +4 dB
  on the cymbals.  Density: `Partls` 0-7 = 25-60 %, shipped rows are 3.
- **Note assignment audited** (`NOTE_AUDIT.md`): 8 anchors all match, Note is
  inert on 8 presets, and **five presets need a pitch decision** (Bongo,
  Woodblock, the two snares, Triangle, Claves).  Nothing changed — these are
  yours to call.  Bongo is the strongest: it is *scored* at 220 Hz by
  `render_presets.cpp` and *ships* at 147 Hz.
- HHat-C (26) **HW-confirmed good** after the pass-27 same-tick fix.  Clap (21)
  and Shaker (22) share that fix and are still unlistened — check them.
- **Listen for on the next flash (pass 30):**
  - Cymbal/Gong under **repeated fast hits** — the crash should be gone and
    stacking should reach 2 voices, not 4.  Poly above 2 no longer adds cymbal
    voices (the cost budget refuses them); it still applies to every other family.
  - **Cymbal/Ride/RidBel/Splash are now 5-11 dB louder.**  Gong and HHat-O were
    deliberately NOT touched — HHat-O measures as quiet as the others but is
    flagged "do not break", so say whether it should come up too.
  - **Kick boom on long decays** — the hard clipper in front of the master stage
    is gone and the limiter no longer waveshapes; crest is now better than the
    pre-29 build that was called "perfect".
  - Overall level is **1.4 dB below pass 29** and 1.65 dB above pre-29.  That is
    the price of removing the distortion; say if you want it back and it has to
    come from re-voicing presets rather than from the master stage.
- Still weak by measurement, awaiting a priority call: `VlMllRes` reads **zero**
  change on Marimba/Vibrph/Kalimba/Shaker/BrshSnr, and `VlMllStf` is inert on
  Clap/Shaker/HHat-C.  Run `knobaud.cpp` for the full 22-preset map.
- Host syntax-check (g++ -fsyntax-only): **clean**.
- HHat-O **HW-approved** ("ok now" — do not break).
- ~~ARM .text ≤ 28 KB: must be confirmed on next flash (cannot verify without
  toolchain).~~ **Superseded in pass 32** — there *is* a usable cross-compiler,
  the sizes are measured above, and the 28 KB figure was wrong.
- Per-family realism findings + ranked backlog: see `REALISM_REVIEW.md`.

### Analysis tool: `modal_extract.py`

Newly added (commit 081e82e). Implements the analysis half of DAFx2020 "Advanced
Fourier Decomposition for Realistic Drum Synthesis" (Werner et al.) — high-res
spectral peak track + per-mode STFT T60 fit on any reference WAV:

```
python3 modal_extract.py samples/Orchestral-Timpani-C.wav
python3 modal_extract.py samples/Taiko-Hit.wav --nmodes 10 --fmax 4000
python3 modal_extract.py samples/rock-rack-tom-1.wav --nmodes 10
```

**Two cross-checks it does not do for you, both of which mattered in pass 33.**
(a) Its amplitude ranking is not the sustained spectrum's: on
`rock-rack-tom-1.wav` it reports the 144 Hz partial at amp 1.00 and the 113 Hz
fundamental at 0.72, while a high-resolution FFT of the 50-550 ms window has
113 Hz at 1.00 and 144 Hz at **0.124**.  Confirm the ranking against a plain
sustained-window FFT before believing which mode is dominant.
(b) It only sees SUSTAINED partials, so a band that lives entirely in the
attack is invisible to it — window the reference in time (0-30 / 30-100 /
100-300 ms) to decide whether missing energy belongs in the mode table or in
the `trans_*` burst.

Output (ratio, freq, amp, T60ms) maps **directly** onto `modal_preset_configs[]`
fields (ratio2..6, t60_1..4, env1..6). Use this whenever a membrane/bar preset
needs its modes calibrated — measure first, guess last.

---

## HW Pass History (most recent first)

### Pass 45 — the gong: it was not stacking, and the mud was being synthesised

Third pass on one HW report — *"multiple hits do not stack correctly and the
sound is muddy"* — after pass 26 rewrote the steal ranking for it and pass 30
re-costed the CPU budget for it.  Both of those measured voice indices and
microseconds.  Neither ever rendered the passage, and the passage is where all
three defects live.  New tool `gong_probe.cpp` renders it and prints a voice
ledger, a strike-over-ring ratio and a band split.

**1. It really was not stacking, and the ledger says so in one line.**  Eight
gong strikes 300 ms apart allocate slots `0 1 0 1 0 1 0 1`.  Two voices at the
default density cost `2 x (124 + 32) = 312` of the 368-lane budget, so from the
third strike on nothing is ever affordable and every strike steals; inside the
0.6 s protect window the steal takes the OLDEST, which is the voice from two
strikes ago.  So the instrument is permanently two gongs being restarted in
alternation.  Measured strike-over-ring (peak in the 30 ms after a strike over
the level just before it): **1.5-2.3**.

The missing rule is the one `ENGINE_KS` has had since pass 41 for a re-plucked
string, and it is the same physics: **a gong is one piece of metal**.  A
re-strike on the SAME note now re-excites its own voice; only a DIFFERENT note
asks for a slot of its own.  Strike-over-ring **13-18**, one voice instead of
two, and `cym_cpu_probe` puts repeated strikes at **14.1 µs/block against
29.0** — the gesture that has been the CPU worry since pass 29 now costs half
of what it did.

**2. A re-strike was zeroing the wash it was landing on.**  `retainRing`
(pass 26) kept `resY1/resY2` but `cymbal_note_on` still reset the two driver
envelopes to `atk = dec = 1`, i.e. **env = 0**, and cleared `lpState`,
`hpLowState`, `dcState` and the pink-noise state.  A gong's low driver takes
`lowAttackSec` 0.25 s and its high driver 0.50 s to reopen, so every re-strike
killed the noise bed dead and spent the next third of a second climbing back.
At any normal playing rate it never got there: traced across 8 strikes, the low
driver sat at 0.59 and the high driver never reached its own attack.  The
envelopes are now placed so the level is **exactly continuous** across the
strike (`atk = 1 - env_held`, `dec = 1`, which reproduces `atk = dec = 1`
bit-for-bit on a fresh voice) and the filter states carry.  Traced now: low
0.79, high 0.65, both climbing.  A re-strike also rebuilds the bank from the
**seed the ringing bank was built from** (new `CymbalVoice::bankSeed`) —
`NoteOn`'s seed folds in the MIDI velocity, so two strikes of different force
used to retune every lane the voice was keeping alive, under its own vibration.

**3. The mud is synthesised, and it is a DC problem.**  A 2-pole resonator with
a constant numerator has DC gain `b0/(1-a1-a2)` — for the gong's lowest lane
(150 Hz, r = 0.99987) that is **42**, against **4313** at its own resonance, so
near-DC drive energy is only ~40 dB down on drive energy at the mode.  Both
things feeding that bank carry it: `thwack` is a **unipolar** contact burst
(20 ms on the gong, so nearly all its energy is under 50 Hz) and the noise
driver is **pink**, which by construction has more energy in 10-25 Hz than in
120-200 Hz.  Every strike parks a lump of sub-mode energy in the lanes with the
most gain, it rings for a second, and it **piles up**.  Energy under 100 Hz,
one strike vs eight 300 ms apart, on the shipping build:

| preset | 1 strike | 8 strikes |
|---|---|---|
| RidBel | 2.7 % | **25.0 %** |
| Cymbal | 1.7 % | **13.5 %** |
| Ride   | 2.5 % | **9.7 %** |
| Gong   | 5.6 % | **9.1 %** |

It is subsonic, so it is never heard as pitch — it just eats the master
limiter, and everything that IS audible gets quieter and duller with each hit.
Two guards, both two cascaded one-poles: **on the drive** at 150 Hz (the lowest
anchor frequency anywhere in the family, so nothing in the family has a mode
below it) and **on the output** at 40 Hz, replacing the old 7.6 Hz DC blocker,
for the two direct taps that bypass the bank.  Each was verified to earn its
place: with only the drive guard, a gong passage still puts 12 % of its energy
under 25 Hz.  **T43** is the regression test; it fails on the pre-fix build on
all three presets it checks.

**4. And then the gong had no metal in it at all.**  With the drive clean, the
remaining problem was plain: over a whole render, **0.2 % of the gong's energy
sat above 1 kHz and 0.0 % above 3 kHz**, against 28 % and 60 % on the crash.
Its centroid was **190 Hz**.  The enum's own reference note for the preset says
the sample "starts with 800 Hz and settles to 1680 Hz", and `REALISM_REVIEW.md`
measured the reference at 1147/815 Hz against a render of 393/505 — and both
documents wrote it off as "deliberately tonal".  It is not tonal, it is dark:
`hfTilt` was 0, and a flat `resGain` is not a flat RESPONSE — the same
`b0/((1-r)|1-r e^-2jw|)` peak gain is **32 dB louder at 162 Hz than at 8 kHz**,
and the pink driver tilts it further.  `hfTilt = 2.4` (crash is 3.0, the rides
2.0).  Single strike now: whole-render centroid **1939 Hz**, and — the part
that matters — it **RISES** across the note, 353 Hz over the first 250 ms,
996 Hz over 0.25-1 s, 2040 Hz over 1-2 s.  That is the bloom the reference
describes, over a body still a quarter to a third in 100-300 Hz.  Sweeping `highAttackSec` 0.50/0.20/0.08 moved the attack centroid
by 3 Hz, so the shipped attack times are left alone; the darkness was never
there.

**5. Level.**  Gong was the one preset on `cym_trim` 1.0, on pass 30's finding
that it was "already correctly placed" at +3.6 dB against the unit mean.  It
was placed there by its own defect: it measured **11.6 dB over the crash at a
crest factor of 3.2** (the crash's is 11.9) — not louder, flatter, because a
dense low drone is the loudest thing a limiter-bounded bus can carry.  Repeated
strikes took it to crest **1.95**, which is a drone with no strikes in it at
all.  `cym_trim` 2.0 puts it back with the family: rms 0.038 vs Cymbal 0.034
and Ride 0.041, peak 0.84, crest **22**.

**Gong, before → after** (single strike / 8 strikes 300 ms apart):

| | centroid | crest | <100 Hz | 1-3 kHz | 3-8 kHz |
|---|---|---|---|---|---|
| before, 1 strike  | 190 Hz | 4.5 | 5.6 % | 0.2 % | 0.0 % |
| after,  1 strike  | **1939 Hz** | **20.2** | **0.2 %** | **21.0 %** | **23.6 %** |
| before, 8 strikes | 171 Hz | 2.2 | 10.9 % | 0.1 % | 0.0 % |
| after,  8 strikes | **573 Hz** | **8.9** | **3.2 %** | **3.5 %** | **5.9 %** |

**Blast radius: exactly the six ENGINE_CYMBAL presets.**  34 of 40 renders are
byte-identical; the six that changed are 13/14/26/31/32/36.  Difference-RMS
against the old render: Gong +0.1 dB (a different sound), Cymbal −2.4, Ride
−2.3, RidBel −1.3, Splash −6.5, **HHat-O −7.5** — the smallest change of the
five, but HHat-O is flagged "do not break", so note what it did get: level
−0.5 dB and its sub-100 Hz energy to zero.  Nothing about its voicing was
touched.

**Two tests were rewritten because their premise was the defect.**
- **T35c** asserted a fast cymbal roll spreads over ≥ 2 voices ("the overlap IS
  the sound").  It did — by ping-pong, and the measured result was a
  stroke-to-stroke level WOBBLE (0.245 0.296 0.295 0.315 0.306 0.327 0.313
  0.332).  On one re-excited voice the same roll swells monotonically (0.203
  0.264 0.301 0.323 0.335 0.342 0.346 0.348) and measures brighter (centroid
  16.2 kHz vs 12.7) with a twentieth of the sub-100 Hz energy.  It now asserts
  the SWELL, and that ENGINE_BAR still stacks.
- **T36a** asserted "distinct voices ≥ 2".  It now asserts what the report was
  about: one plate, level rising, driver envelope rising, and the strike
  audible over the ring.  **T36b** moved to distinct notes (same-note strikes
  no longer exercise a steal) and still asserts the oldest voice goes.
Both fail against the pre-fix build.

**Found and NOT fixed (no sound change, recorded so it is not re-derived):**
`CymbalConfig::maxHz` is **dead** — `cymbal_note_on` clamps to a hard
`fHi = 20000.0f` and never reads it, so half of the `Mterl` -> "metal
brightness" mapping (`cc.maxHz *= knob_exp2(1.3 * d_cmt)`) does nothing.  The
`hfTilt` half is live and carries that knob.

Verified: 34/40 renders byte-identical and 0 NaN/silent, `test_dsp` exit 0,
`test_hw_debug` **108/108**, `stability_sweep` 4096 combos + 480 rolls with 0
problems and worst |peak| 0.9900, host syntax clean, all 18 probe tools build,
`plateau_probe` shows no knob-travel change on the cymbal family, and the ARM
cross-build measures `.text` 52,676 → 52,964, `.rodata` unchanged, `.bss`
107,884 → 107,948 (four floats per voice: `bankSeed`, `dcState2`, `driveHp1`,
`driveHp2`).


### Pass 44 — BrshSnr: the head was mixed in at ZERO, and velocity saw a third of a knob

HW: *"sound is too much explosion like, too much chaotic, rather than soft hit
with resonance.  Even with negative velocity hit is too hard (it's just
lowering the volume not softening)."*  Two claims, two separate causes, both
measured (`brush_probe.cpp`, new).  **1 of 40 renders changed.**

**"No resonance" was literal: `k_modal_mix = 0.0`.**  The preset had a fully
calibrated head — 4 modes, ratios 1.59 / 2.14 / 2.30 — that `NoteOn` built on
every strike and then multiplied by zero.  Everything you could hear was noise
through three low-Q wire bands, which is also the "chaotic": measured spectral
flatness **0.23-0.29** against AcSnare's 0.08-0.16, i.e. two to four times
closer to white noise.

Fixing the mix alone was not enough, and the reason is the second half of §4 of
`NOTE_AUDIT.md`: **BrshSnr ships Note 38, which is the General MIDI drum-map
NUMBER used as a pitch** — 73.4 Hz, a sub-thud, where a snare head sounds
180-220 Hz.  So the head is now audible AND at Note **55 (G3, 196 Hz)**, the
value that audit had already recommended.  That recommendation was labelled
"display only" because the note was inert; it stopped being cosmetic the moment
the head became audible.  Body T60 110 → 210 ms so it breathes under the swish.

| | before | after |
|---|---|---|
| head resonance, 120-500 Hz over the band median | **4.3 dB** (no head — noise-floor structure) | **11.9 dB @ 198 Hz** |
| centroid across velocity 127→30 | 3294 → 2896 (**−11 %**) | 3275 → 2448 (**−25 %**) |
| flatness across velocity 127→30 | 0.288 → 0.227 (−21 %) | 0.290 → **0.134** (−54 %) |
| attack share at low velocity | **rises** 0.186 → 0.308 | 0.204 → 0.272 |

**Why velocity was a volume control, exactly.**  BrshSnr has its own quadratic
velocity curve (`0.08 + 0.64·v²`, pass 19 round 2), so `current_velocity` only
ever spans **0.08..0.72** — and every character mapping downstream was written
against it as though it spanned 0..1.  `noise_band_mix` was
`min(0.32, 0.20 + 0.10·vqb)`, i.e. a real range of **0.208..0.272**: a knob
with 6 % of its travel connected.  Normalising back to 0..1 first
(`vbn = (vqb − 0.08) × 1.5625`) and re-spanning gives 0.08..0.27 — the hard hit
lands where it always did, the soft one is nearly pure air.

**A soft brush also ARRIVES more slowly.**  `k_onset_attack_ms` was a fixed
2 ms, so a quiet stroke still had a contact transient and read as a hit rather
than a sweep — which is precisely "the hit is too hard, it's just lowering the
volume".  The onset now scales 2 ms (accent) → 18 ms (crawl).  Verified
same-tick-safe: gate_on+gate_off in one tick renders **bit-identical** to a
held gate at both velocity 127 and 30, so the longer ramp does not walk into
the same-tick killer.

**Note the lesson, because it generalises past this preset.**  A per-preset
velocity CURVE and the mappings that consume `current_velocity` are coupled:
compress the curve and you silently compress every mapping downstream.  Pass 19
added the curve, pass 41 read the mappings and found them "subtle".  They were
not subtle, they were scaled by 0.64 and offset by 0.08.  **Grep for
`current_velocity` consumers whenever a velocity curve changes.**

*Deliberately NOT chased*: the timbre matches its (now-absent) reference —
`render_presets.cpp`'s own note records the brush refs at flatness ≈ 0.31 and
centroid ≈ 4.2 kHz, and the shipped hard hit measures 0.29 / 3.3 kHz.  So the
hard stroke was never spectrally wrong and was left alone; what was wrong was
that every stroke sounded like the hard one.  `modal_mix` 0.20 is the value to
turn if HW says the head is now too tom-like — 0.14 gives 8.5 dB of resonance
and 0.10 gives 5.3 dB.

Verified: 39/40 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**107/107**, 0 NaN/silent across 40, stability 4096 combos + 480 rolls worst
|peak| 0.9900 / 0 problems.  ARM `.text` 52,612 → **52,676** (+64); everything
else unchanged.

### Pass 43 — Timpani's note-change clicks were CPU, not signal

HW, twice: *"Timpani: changing note while playing leads to silence (audio
interface crash)"*, then *"changing note leads to sporadic clicks for the next
8-10 seconds"*.  Pass 41 hunted a waveform discontinuity, measured no
improvement from the obvious `ClearVoice` fix, and correctly refused to ship
it.  **The theory was not merely unproven, it was the wrong category.**

**The measurement that settles it** (new tool `kernel_cpu_probe.cpp`, all
figures one run, same machine, with the cymbal family as the normaliser):

| | before | after |
|---|---|---|
| Timpani, 1 kettle | 45.9 µs/block | 44.4 |
| **Timpani, 2 kettles (a note change)** | **90.6** | **53.5** |
| ratio 2 kettles / 1 kettle | **1.97x** | **1.20x** |
| Taiko, 2 kettles | 24.1 | 14.3 |
| Cymbal, 2 voices @ max bank | 44.3 | 44.5 |

The last row is the calibration and the reason these host numbers mean
anything: pass 30 measured the cymbal build that **crashed the hardware** at
95.6 µs against a last-known-good 49.7 µs, and this host reproduces that
known-good level to within 1 %.  So **a Timpani note change was landing at the
CPU level that had already been proven to stop the audio interface**, and
holding it there for ~7 s — which is the reported 8-10 second window, measured
directly (`LiveVoices() >= 2` until t = 9.01 s after a change at t = 2.00 s).

**Why a note change specifically.**  A note change is always simultaneous with
a strike, a strike resets a kettle's mode bound to the full bank, and the old
note keeps ringing on the other kettle — so the unit's dominant per-sample term
(280 biquads on Timpani) runs **twice**.  Nothing else in the unit doubles like
that.  Taiko escapes the worst of it with 72 modes, which is exactly why the
report names Timpani and not Taiko.

Three changes, in increasing order of how much they buy:

**1. The `gain[]` array is dead weight for >99 % of a ring.**  `exc_len` is at
most 40 samples, so after the strike burst `e` is exactly 0 and `gain[m] * e`
contributes nothing — yet the loop still loaded a 280-float array and issued a
multiply-add per mode per sample.  Split into two arms on `e != 0.0f`.
Bit-exact (`x + g*0 == x`), one fifth of the loop's memory traffic.

**2. Mode retirement.**  A mode whose envelope is below `kRetireEps` (1e-5,
i.e. −100 dB, *below the 16-bit LSB of 3.05e-5*) is still stepped every sample
for as long as the kettle lives.  On Timpani that is most of the bank most of
the time: the 224-mode fill above the measured skeleton has T60 620-1670 ms
against the skeleton's ~1.9 s, so by 2 s after a strike only ~70 of 280 modes
carry anything.  A schedule computed at rebuild time (`n = ln(eps/P)/ln(r)`,
suffix-maxed over NEON groups of 4) lets each kettle walk its bound down.
Shared by both kettles — r and amp are pitch-invariant by design, which is the
whole point of the base tables.

**On its own this bought only 11 %, and the reason is worth recording**: in a
played passage a strike every 500 ms resets the bound to full, so natural
retirement helps a *tail* and does nothing for the *peak* — and the peak is
what the hardware was complaining about.  Do not stop at the elegant fix.

**3. The one that actually fixed it: only the newest kettle runs a full bank.**
When a new note takes the second kettle, the older kettle keeps its measured
skeleton (the same `n/5` split `SelectedModes` already calls "always sounds")
and its dense upper fill is **damped away** — 25 ms T60, walked out of the loop
in step with the fade over 40 ms.  Steady two-note cost 2x280 → 280+56.

Damped rather than dropped for two reasons.  A scaled pole radius is
continuous, so there is no step to click on; and the fade is masked anyway,
because *the event that triggers it is a full-velocity strike on the other
kettle*.  The damping is folded into `DeriveVoiceRange` and the glide loop
rather than applied once to the coefficient arrays, so an amortized rebuild
landing mid-fade cannot restore the undamped poles.  Applying it is a pole
SCALE (`a1 *= d, a2 *= d*d`) not a re-derive: a second bank of sin/cos inside
`NoteOn`, next to the new kettle's own `RetuneVoice`, would have put a CPU
spike at the exact moment the mechanism exists to relieve.  Verified
byte-identical to the re-derive version.

**Sound cost, measured.**  38 of 40 renders byte-identical; Timpani and Taiko
differ by **exactly one 16-bit LSB at their largest sample** (`max|d|` =
3.05e-5, whole-render difference-RMS −88.6 and −90.9 dB).  The retirement
threshold sits below the DAC's own resolution, so what it discards could never
have been represented.  Fill damping does not touch these renders at all —
`render_presets` strikes once, so a second kettle never exists.

**What a note change now sounds like:** the previous drum's dense wash is
damped over ~40 ms while its pitched skeleton rings on normally.  Both kettles
still sound (**T42b** asserts it) — this is a damped wash, not a muted drum.
If the HW verdict is that the old note now thins out too much, the knob is
`kFillDampRate` (lower = slower) or `m_skeleton_padded`'s `n/5` split; if it is
still clicking, the remaining peak is the single block at the strike itself,
where both banks are briefly full.

Also new: **`click_probe.cpp`**, which finds clicks properly — pass 41 used max
sample-to-sample step, and that metric cannot answer the question: a 90 Hz
kettle at full amplitude has a legitimate per-sample step of ~0.02, one number
per render hides anything sporadic, and the largest step in any percussion
render is the strike. It high-passes at 8 kHz, flags HF spikes against a running
median, excludes the 60 ms after each known strike, and renders a CONTROL
timeline. Timpani measures **0 events at 3x, 6 at 2x, unchanged before and after**
— i.e. the clicks were never in the rendered signal, which is itself the
evidence for the CPU explanation.

Verified: 38/40 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**107/107** (T42a-d new), 0 NaN/silent across 40, stability 4096 combos + 480
rolls worst |peak| 0.9900 / 0 problems.  ARM `.text` 51,380 → 52,612 (+1,232);
`.bss` 107,564 → 107,884 (+320 = the schedule plus the per-kettle bounds);
`.rodata` and `.data.rel.ro` unchanged.

### Pass 42 — Kick2 decay −40 % and a velocity→balance trade scoped to it

User: *"The Kick2 fix (velocity to balance trade on the boom) would affect also
Kick preset?  If not let's do it.  Add to this, reduce the Kick2 preset decay
by 40%."*  Answer: it does not have to — the kick knob-design block is shared
by Kick2 / 808Sub / KickDrum, but the trade is behind one `if`.  **Only
`00_Kick2.wav` changed; the other 39 renders are byte-identical**, and 808Sub
and KickDrum measure numerically identical before and after.

**Decay −40 %, and it takes TWO values.**  Kick2 is boom-dominant (boom_mix
0.85 vs modal_mix 0.46), so cutting only one half would have halved the effect:
- `modal_preset_configs` T60s 1100/400/200/100 → **660/240/120/60 ms**
- `model_param_presets` `boom_decay` 0.99978 → **0.9996333** (T60 654 → 392 ms,
  exactly ×0.6 — `T60 ∝ 1/(1−decay)`, so the cut is `(1−d)/0.6`)

`Dkay` is deliberately NOT touched.  Unlike the Triangle change in pass 41 —
where the user specified the target as a KNOB POSITION and the anchor therefore
had to move with it — this was specified as a property of the sound, so the
data changes and the anchor stays put.  The knob keeps its full travel either
way.  Measured whole-hit t60 **525 → 325 ms** at velocity 127 (the 38 % vs 40 %
gap is the 25 ms measurement bucket).  Peak 0.9778 → 0.9776, 250 ms RMS −1.4 dB.

**The velocity trade, and why it is a trade rather than a boost.**  Kick2's
attack-to-tail ratio measured 0.93 / 0.93 / 0.95 at velocity 127 / 64 / 30 —
flat.  The master limiter pins the level, so a harder strike cannot get louder,
it only stays above the threshold longer, which is exactly what "increases the
decay but not the hit" describes.  Pass 30's rule applies: *a limited bus
cannot give you level, but balance is free.*  So velocity now moves balance —
below full velocity the boom gets weightier and its onset slower (a soft beater
contact is longer), leaving hard hits comparatively tight:

```
vd = 1 − current_velocity                  // 0 at MIDI 127
boom_mix        *= 1 + 0.35·vd             // soft = rounder, more body
boom_attack_inc *= 1 − 0.50·vd             // soft = slower onset
```

Anchored at FULL velocity, which is exact here because `current_velocity` is
linear on this preset (the quadratic curve is BrshSnr-only) — so the hardest
hit is bit-for-bit what it was, and everything below it is the deliberate
change.  A render at velocity 100 sits at vd = 0.21, which is why
`00_Kick2.wav` moves.  Measured: velocity's influence on the attack/tail
balance widens ~25 % (relative spread 1.43 → 1.79 across velocity 127→30).

**Scoping is load-bearing here** — 808Sub and KickDrum are HW-approved as they
stand and the user explicitly asked that "Kick" (= KickDrum, preset 20) not be
touched.  Do not widen that `if` without a listen on the other two.

Verified: 39/40 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**103/103**, 0 NaN/silent across 40, stability 4096 combos + 480 rolls worst
|peak| 0.9900 / 0 problems.


### Pass 41 — HW batch: display, decay, stacking, the "zip", and GtrStr removed

Eleven reports from the hardware.  Split across three commits; this section
covers all of them, including the four not fixed and why.

**`Inharm` displayed huge positive numbers instead of negatives.**  The user's
own diagnosis (signed → unsigned) was right, and the isolating fact is that
Inharm was the **only `k_unit_param_type_strings` parameter in header.c with a
negative min** — every bipolar knob that displays correctly (Velocity,
VlMllRes, VlMllStf, Mterl) is `type_none`.  The OS routes a `type_strings`
parameter through `unit_get_param_str_value()` as an unsigned selector.  Now
`type_none`; the custom ×10 format branch is deleted rather than left to rot.
The knob reads −100..100 like the other three bipolar knobs.

**Triangle decay 2175 → 300 ms, and the trap that makes it a two-value edit.**
`Dkay` is a REFERENCE ANCHOR: `LoadPreset` captures the row's own Dkay into
`m_modal_dkay_ref`, so `t60_scale` is exactly 1.0 at the shipped value and
**lowering the Dkay column alone changes nothing**.  The factor the user's
preferred knob position implies — 2^(4.5·(0.30−0.95)) = 0.1317 — is baked into
the config T60s and the column moved to 60 so the anchor follows.  Verified by
rendering the OLD build at Dkay=600: 275 ms vs the new shipped 300 ms.

**Koto stacked unnaturally** because ENGINE_KS was hard mono — `GateOff` pinned
every pluck to one slot.  The anti-beating rationale only ever applied to the
SAME note, so that is what is kept: a repluck reuses its slot, a different note
takes its own voice.  Measured 1→2→3→4 voices across four notes, steady 1
across four replucks.  **The first measurement of this was wrong** and worth
recording: a Goertzel at note 60 and note 72 "proved" stacking already worked,
but 523 Hz is note 60's own second harmonic.  Counting active voices is the
honest test; a harmonic-series probe cannot separate an octave from its own
overtone.

**DeepBs measured as stacking PERFECTLY — 1→2→3→4 voices — and that was the
bug.**  Its body is the longest membrane in the unit (t60_1 = 1800 ms), so four
overlapping bodies turn the low end to porridge; a real bass drum cannot,
because the beater damps the head it just struck.  Same-note strikes now fuse
out to 300 ms instead of 80.  `kRollFuseSec` is untouched for every other
preset, so the snare-wire continuity test still keys on it.

**Kick "thump" read as a "zip"** because the pitch sweep was tied 1:1 to the
amplitude envelope (`swp = 1 + 1.6·drop`): 299 → 115 Hz, ~1.4 octaves, spread
over the sound's whole ~60 ms life, still at 1.8× after 6 ms.  Cubed against
the envelope and narrowed to 1.0, it is 1.125× at 6 ms — the sweep collapses in
~7 ms and becomes attack CHARACTER instead of an audible descending chirp.
Byte-identical because the thump layer is zero at every shipped preset.

**"Velocity increases the decay but not the hit" — measured, and only half
fixed.**  Kick2's attack-to-tail ratio is **0.93 / 0.93 / 0.95 at velocity
127 / 64 / 30**: essentially flat.  The master limiter pins the level, so a
harder strike cannot get louder, it only stays above the threshold longer —
which is exactly what reads as "more decay, same hit".  The transient's
velocity curve is now superlinear (v·√v), which moves BALANCE rather than
level, the only thing a limited bus leaves free (pass 30's rule).  Fully
solving it needs a velocity→balance trade on the boom, and there is **no
velocity at which such a trade is neutral** (the velocity curve means
`current_velocity` never reaches 1.0), so every kick render would change.
That is a voicing decision, left for a listen.

**BrshSnr's Dkay was not "subtle", it was mathematically inaudible.**  Dkay
scales the modal body's T60 and BrshSnr ships `k_modal_mix = 0.0` — the bank it
controls is mixed in at ZERO.  Measured t40 was **150 ms at every one of six
Dkay positions across the full range**.  Dkay now drives the noise swish tail —
the brush's actual voice — as the COARSE control with Rel as the fine trim.
After: 50 / 75 / 75 / 100 / 175 / 225 ms.  Scoped to BrshSnr; AcSnare measures
100 ms at every Dkay before and after.

**GtrStr removed** (user request).  The pass-33 seven-table checklist run in
reverse, plus every index above 25 shifted down by one.  Proof the renumbering
is right: all 40 surviving presets render **byte-identical keyed by NAME**.
Koto is now the only ENGINE_KS preset and takes over as the KS reference in
`test_hw_debug`, `param_audit`, `samegate_probe`, `switch_probe` and `knobaud`.
ARM: `.rodata` −64 B, `.bss` −200 B, `.data.rel.ro` −4 B — exactly one row.

**Three tests broke on the renumbering, and each was a latent bug.**
- **T25** (`test_dsp`) swept `{1, 4, 10, 15, 19, 26}` labelled
  "…GlassBowl" — but GlassBowl is **24**; index 26 was HHat-C.  The test had
  been asserting modal ring decay on the wrong preset all along and passed by
  luck.  Fixed to 24.
- **T40a** and `stability_sweep` carried the cymbal list `{13,14,27,32,33,37}`,
  all four upper entries now off by one.
- **T16a** hard-coded `voices[1]` — the exact anti-pattern pass 25 documented
  for T18 ("read `state.next_voice_idx`, never assume an allocation order").
  It also needed the modal bank silenced: Koto, unlike the retired GtrStr, has
  `modal_mix` 0.22, so with the waveguide killed the voice is *correctly* still
  sounding and the test's premise evaporates.

**NOT fixed, with evidence.**
- **The two crashes are no longer reproducible** and never reproduced here: a
  10-simulated-minute soak of BrshSnr and Timpani (1000 hits, 76 note changes,
  random knob moves) is clean, worst |peak| 0.98, no NaN.  Note that **no host
  tool compiles the NEON paths** and there is no qemu in this environment, so
  ARM code cannot be executed at all — four `__ARM_NEON` blocks are untested by
  construction.  The kernel's NEON resonator loop was reviewed against its
  scalar arm and is math-equivalent with in-bounds indexing.
- **Timpani's "sporadic clicks for 8-10 s after a note change" — theory tested
  and REJECTED.**  The obvious suspect is `ClearVoice`, which `memset`s up to
  280 resonators to zero when a note change steals a still-ringing kettle: a
  step discontinuity on every mode at once.  A patch that retunes the bank
  under its own ring instead (physically what a timpani pedal does) measured
  **no improvement in max sample-to-sample step** in any variant — 0.04047 →
  0.03999 on Timpani, and slightly WORSE on Taiko.  Reverted rather than
  shipped.  Note a steal needs ≥3 distinct notes in play, since two kettles
  cover two notes; tests that alternate two notes never exercise it.
- **SteelPan/Triangle clicks** and the GUI-stale-after-Program-change (the user
  deferred the latter) are open.

New tool: **`live_edit_probe.cpp`** — changes parameters WHILE a voice rings,
which no existing tool did (they all configure, then strike).  Its first run
produced 36 false positives from short percussion decaying naturally, so it
now renders a CONTROL timeline with no edit and compares against that.


### Pass 40 — obsolete tools removed (no DSP change)

User: *"Remove obsolete tools."*  14 files deleted, all recoverable from git
history.  Nothing in the shipping unit changed; every remaining tool still
compiles.

**Standard applied** — remove only if all three hold: the investigation it was
built for is CLOSED and its finding is recorded here; nothing in the docs tells
anyone to run it; and it is not the permanent regression test for the bug it
found.  A tool that is a general-purpose instrument (works on any preset/knob)
was kept even if it has not been run in twenty passes.

| removed | why |
|---|---|
| `sweep_test.cpp` | min/max knob deltas — superseded by `plateau_probe` (whole range) and `param_audit` (better metrics) |
| `vlmod_probe.cpp` | "how much do VlMll\* move" — `plateau_probe` did exactly this job in pass 39 and also says *where* they stop moving |
| `dive_probe.cpp` | 808Sub dive vs Inharm; closed pass 36, table is in this file |
| `live_probe.cpp` | Inharm difference-RMS per mapping; closed pass 37, table is in this file |
| `kick_headroom_probe.cpp` | pass-29 "is the kick pinning the limiter"; closed in pass 30 when the hard clipper came out |
| `cymbal_detail.py` `cymbal_diag.py` `cymbal_note_test.py` `cymbal_sweep.py` | single-experiment cymbal scripts ("NzMx 40 vs 60", "note 76 vs 65", …) from passes 9-13, all superseded by the pass-14/16/18 rework |
| `analyse_presets.py` `analyze.py` | near-duplicates of each other, both hard-coded to 4 presets; `refcmp.py` does this properly |
| `test_audio_render.py` `test_calibration.py` `analyze_samples.py` | **actively wrong**, see below |

**The one that mattered: a render-vs-reference chain that had been lying.**
`render_presets.cpp` printed `Run: python3 test_audio_render.py` on every run,
and that script → `test_calibration.py` → `analyze_samples.py` all carry a
sample→preset map binding **index 24/25 to Flute/Clarinet** — presets deleted
back in passes 1-5.  Index 24 is GlassBowl and 25 was GuitarStr (removed in pass 41), so every
comparison that chain made for the last thirty-odd passes was against the wrong
reference.  Not merely unused: wrong, and advertised by the tool everyone runs.
`render_presets.cpp` now points at `refcmp.py` instead.  **A stale tool that
still prints an instruction is worse than a dead one** — same lesson as the
pass-36 out-of-range sweeps, one level up.

**Deliberately KEPT** (general-purpose, or the docs say to run them):
`plateau_probe`, `param_audit`, `note_audit`(+`.py`), `stability_sweep`,
`render_presets`, `velocity_probe`, `knobaud`, `verify_blind`, `samegate_probe`,
`switch_probe`, `cym_cpu_probe`, `gong_probe`, `kick_probe`, `test_dsp`,
`test_hw_debug`, `refcmp.py`, `modal_extract.py`, `pre_hw_analysis.py`.

**Left in place, needs a call** (flagged, not removed):
- **The phase-23 auto-tuning pipeline** — `auto_tune.py`,
  `batch_tune_runner.py`, `phase23_readiness.py`, `test_td.py`,
  `run_tuning.sh`, `run_phase23_tuning.sh`, `test_brachetti_render.cpp`.
  Stale the same way (Flute/Clarinet-era preset maps) and unused since ~pass
  23, when the method became measure-first via `modal_extract.py`.  But it is
  a documented workflow — ~190 lines of README in *two* files — so deleting it
  is a bigger call than deleting scaffolding.
- **`cymbal_synthesis/`** — the dense-resonator prototype that became
  `ENGINE_CYMBAL` (`dsp_core.h` and `synth_engine.h` both say "ported from
  cymbal_synthesis").  The port is done, and its `MERGE_FEASIBILITY.md` both
  recommends an option that is *not* what shipped and repeats the disproved
  "≤ 28 KB `.text`" limit.  Kept because it is the reference implementation
  for a live engine, not dead scaffolding.

Note for readers of the pass-39 entry below: it records fixing stale ranges in
`dive_probe` and `sweep_test`, and those two files no longer exist — they were
removed here.  The lesson in that entry still stands for the tools that remain.

### Pass 39 — `plateau_probe`, VlMllStf's clamp, and HitPos made bipolar

User: *"Is there any other parameter that can be made bipolar or with wider
range to check for further errors or expanded capabilities?"* → then *"Fix
VlMllStf First, HitPos then."*  **41/41 byte-identical throughout.**

**The tool first, because the answer came out of it.**  `param_audit` compares
only the two ENDS of a range, so a clamp plateau in the middle is structurally
invisible to it — which is how BOTH floored Inharm mappings survived to pass
36/37 and had to be found by hand.  `plateau_probe` walks every parameter
across its whole declared range in 11 steps, hashes each render and counts
DISTINCT results; it also splits the count into travel below vs above each
preset's own shipped value, so the "shipped on the floor, knob only goes one
way" pathology reads straight off as `below=1`.  Run over 8 engine exemplars it
reproduced every knob this file already lists as inert, and found the two below.
(Caveat baked into its header: on ranges with fewer integers than steps —
Partls, Model, NzFltr — the collapsed-span column is a sampling artefact.)

**1. `VlMllStf` saturated its clamp at TWO sites.**  Same defect as the pass-37
modal spread: a LINEAR term added into a hard bound, so the knob stops moving
partway through its own travel.

| site | mapping | measured dead span |
|---|---|---|
| legacy voice (mallet stiffness) | `clamp(base + mod·vel + rim, 0.01, 1)` | Marimba −100..−40, Clap 40..100, Handpan −100..−80 + 60..100 |
| dense kernel (`vel_sharp`) | `clamp(0.6 + 0.8·(vs−ref), 0, 1)` | Timpani −100..−40 + 80..100 |

`base_stiff` is `MlltStif × 0.02` over a 0..50 range, so it ALONE spans the
whole legal stiffness — 5 presets ship MlltStif 50 and 4 ship under 13, and
wherever it sits near an end VlMllStf had almost no room.  The kernel one was
the **only** linear-into-a-clamp mapping in `RefreshKernelMods`; everything
around it was already `knob_exp2`.

Both now scale the delta into the headroom actually **remaining on the side the
knob is moving toward**, with the delta normalised by the travel left on that
side, so full knob travel maps to full headroom whatever the preset ships.
Monotonic, cannot leave its bounds by construction, **no clamp needed**.
Anchored at the shipped value (required, not stylistic: 4 presets ship
VlMllStf 20/40/60, so pivoting at zero would re-voice them).

After: every legacy exemplar returns 11 distinct renders from 11 steps.
**Timpani returns 7, and that is NOT dead travel** — `vel_sharp` feeds
`exc_len`, an *integer* sample count over `kExcLenMin` 2 … `kExcLenMax` 10, so
at its velocity the mechanism can only represent ~7 values.  The knob is
monotonic and uses everything the mechanism has.  *Honest ceiling left in
place*: a preset shipping MlltStif 50 has no upward headroom at all, so
VlMllStf up is legitimately inert there — move MlltStif first.

**2. `HitPos` made bipolar −98..98.**  Exactly Inharm's pathology: the three
ANCHORED consumers pivot on `d = norm − shipped`, and **25 of 41 presets ship
HitPos at the exact floor 0**, so on a clear majority of the library the knob
only ever pushed toward the rim.  `below=1` before on Marimba/Cymbal/Handpan;
**11/11 distinct after**.

**Unlike Inharm, the pass-35 re-centring trick does NOT apply**, and that is the
thing to remember: HitPos also feeds two ABSOLUTE consumers, so moving the
stored value would change the sound.  They stay floored at 0 and are inert
below centre, which is correct rather than a missed clamp — 0 already IS
"struck dead centre":
- the 2D strike **radius** — and this one had to be floored explicitly,
  because `hit_x` is consumed through a MAGNITUDE (`sqrtsum2acc`), so a
  negative x folds onto its positive mirror and HitPos −50 would have rendered
  identically to +50.  A bipolar range would have made the knob
  **non-monotonic** rather than merely inert.
- `mix_ab`, the ResA/ResB blend (ENGINE_KS only) — 0 is already all-ResA.

Consequence worth knowing: on **ENGINE_NOISE** the strike radius is the only
live HitPos consumer, so Clap's whole negative half is inert by design
(measured: `−98..0` identical).

*Remaining plateau, measured and deliberately not chased*: the six modal-env
multipliers (`1 ± c·hit_off`, clamped) saturate in the far-negative corner on
presets shipping HitPos high — Kick2 (ref 0.36) below −54, AcSnare (ref 0.46)
similarly, both reading `−98..−59` identical.  Presets shipping 0 — the 25 this
pass was FOR — only saturate below −90.  The pass-37 repair (`knob_exp2`)
applies, but reproducing today's mid-range feel needs the coefficients
re-derived (mode 1 would need ~4.3, not 1.55, to give today's 0.225 at
hit_off 0.5) and that curve has been re-tuned against HW listening three times.
**That is a voicing pass with a listen attached, not a range change.**

**3. Four tools were still carrying pre-pass-36 ranges** — the same lesson that
pass documented, and it had NOT been fully applied: `dive_probe` swept Inharm
`{…,150,199}`, `kick_probe` and `sweep_test` both declared `Inharm 0..199`, and
`kick_probe`/`sweep_test`/`param_audit` still had `HitPos 2..98`.  Every one of
those out-of-range values is silently REJECTED by `setParameter`, so those
tools were reporting results for knob positions they never set.  All updated.
**When a range moves, grep every `.cpp` in the directory, not just the ones you
remember touching.**

Verified: 41/41 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**103/103**, 0 NaN/silent across 40, stability 4096 combos + 492 rolls (now
sweeping the bipolar HitPos corner) worst |peak| 0.9900 / 0 problems.  ARM
`.text` 50,928 → **51,108**; `.rodata`/`.data.rel.ro`/`.bss` unchanged.

### Pass 38 — AcSnare + Koto pitch_env enabled, and the tuning bug hiding behind it

User: *"Add pitch_env, so I can evaluate the difference."*  Pass 35 had gated
the `pitch_env` restore to 808Sub + RackTom and left AcSnare (amt 18) and Koto
(amt 1.5) dormant as HW-approved presets nobody had asked about.  Both are now
in the gate.  **39/41 renders byte-identical**; only these two changed.

**"One line in the gate" was right for Koto and wrong for AcSnare.**  Koto is
ENGINE_KS and the KS branch already reads `pitch_env` to sweep the delay line,
so the restore alone brings it to life.  **AcSnare had no reader at all**: the
non-KS path only *decays* `pitch_env`, and the boom sweep block covered
KickDrum / 808Sub / RackTom only — so restoring the fields would have been
completely silent.  It needed a fourth boom-sweep branch, written in the same
form as 808Sub's and anchored on `asn_bm` (175 Hz) so the sweep converges to
exactly the fixed `boom_inc` it replaces.  **Check for a consumer before
calling dormant data a one-line fix.**

**The KS sweep was mistuned, and it had never run so nobody knew.**  With Koto
enabled, the string settled **261.75 → 269.44 Hz and stayed there for the full
6 s render** — half a semitone sharp, permanently, with a *clean* harmonic
series (so not instability: an honest, wrong pitch).  Cause:

```
sweep_scale = fasterpowf(2.0f, -sweep_st * 0.08333f)
fasterpowf(x,p) = fasterpow2f(p * fasterlog2f(x))     // BOTH halves approximate
fasterlog2f(2.0f)     = 1.057304     (exact 1.0)
fasterpowf(2.0f,0.0f) = 0.971348     (exact 1.0)  →  −50.3 cents at the anchor
```

The sweep converges to `sweep_st = 0`, so the delay line was left permanently
2.9 % short: `261.75 / 0.971457 = 269.44` — the measured value to four
significant figures.  The error runs **−22 to −50 cents across the whole
sweep**, so the bend depth was wrong too.  This is the documented `knob_exp2`
trap (`fasterpow2f(0) ≈ 0.96`, not 1.0) at a site that had **never executed**:
the pass-35 `pitch_env` clobber kept the branch dead, so the tuning bug hid
behind the dead-code bug.  Now exact `exp2f`, per this file's own rule —
*fast math for knob curves, exact math for tuning*.  Cost is one `exp2f` per
sample for the ~240 ms the sweep runs, on KS presets with a non-zero amt,
which today is Koto alone.

**Measured, Koto** (fundamental by 2^19-point FFT; the early windows average
over a τ ≈ 21 ms sweep, so they read below the 1.5-semitone instantaneous
start — the pass-35 lesson about fast sweeps and FFTs applies):

| window | 0-20 ms | 20-50 ms | 50-120 ms | 0.3-1.5 s | 1.5-3.0 s |
|---|---|---|---|---|---|
| cents vs before | **+45.4** | +21.7 | +10.9 | **+0.0** | **+0.0** |

Sustained series exact (1.000 / 2.000 / 3.000 / 4.000 / 5.000 / 6.000).
Level 250 ms RMS 0.1691 → 0.1571 (−0.64 dB), peak unchanged at 0.9827.
**The documented "KS pitch_env shortens T60" gotcha did not reproduce** — the
tail got slightly LONGER (t40 343 → 368 ms, t60 532 → 582 ms).  The gotcha's
τ ≤ 21 ms rule is what keeps it in bounds, and Koto ships `pitch_env_decay`
0.99900 = exactly that limit, so the data was written with the rule in mind.

**Measured, AcSnare** — real but subtle, and the reason is its own attack ramp:

```
boom_attack_inc 0.00180 → 11.6 ms to full amplitude
pitch_env_decay 0.99850 → τ = 13.9 ms
```

The bend is largely over before the boom is audible.  Instantaneous start is
193 Hz, but the **amplitude-weighted mean is 179.6 Hz** against a 175 Hz rest,
and only **76 cents** of bend remain by the time the ramp opens.
Difference-RMS **−17.8 dB**; peak 0.9772 unchanged, 250 ms RMS +0.01 dB.  If
the HW verdict is "can't hear it", the knob to turn is `boom_attack_inc` (the
same value pass 34 had to fix on RackTom, for the same reason), not `amt`.

**KickDrum stays out of the gate and loses nothing by it** — its sweep formula
is written against `boom_env`, so restoring `pitch_env` there cannot change a
sample.  That is now stated in the gate comment, so the last dormant row does
not look like an oversight.

**`note_audit` now reports Koto `peak/nom +12.0`.**  Not a mistuning — the bend
pushes energy up the series (2nd partial 0.771 → 0.970) until the octave
outranks the fundamental in the peak picker.  Recorded in `NOTE_AUDIT.md` so a
later pass does not "fix" a correct tuning.

Verified: 39/41 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**103/103**, 0 NaN/silent across 40, stability 4096 combos + 492 rolls worst
|peak| 0.9900 / 0 problems.  ARM `.text` 50,644 → **50,928** (+284);
`.rodata`/`.data.rel.ro`/`.bss` all unchanged.

### Pass 37 — the rest of the Inharm mappings, audited across the new range

Pass 36 opened a downward half on every preset; this pass checks what each of
the five Inharm consumers actually *does* with it.  Measured as difference-RMS
against each preset's shipped render (below ≈ −60 dB = inaudible):

| mapping | −100 | −50 | +50 | +100 | verdict |
|---|---|---|---|---|---|
| modal overtone spread | −5.3 | −5.3 | −5.3 | −5.5 | **was floored — fixed** |
| kernel mode stretch | +3.2 | +3.0 | +3.0 | +3.1 | already live |
| cymbal jitterSemis | +1.4 | +1.0 | +1.4 | +1.3 | already live |
| KS allpass coeff | **−inf** | **−inf** | −6.9 | −28.0 | inert below centre, correctly |

**Only one needed changing.**  The modal overtone spread was
`spread = 1 + 2.4·d`, linear, clamped `[0.05, 5.0]` — so it hit the floor at
d = −0.40 and **Inharm −40 … −100 all rendered identically** (measured on
Handpan: ratios frozen at 1.000/1.011/1.054/1.099/1.103/1.168).  Worse, 0.05 is
a *degenerate* point: it collapses every partial onto the fundamental, a unison
pile-up rather than the "compressed toward harmonicity" the mapping intends.
Now `knob_exp2(1.75·d)` — spans 0.30…3.36 over the full travel, same top end,
live and musical bottom, cannot reach zero.  Handpan now compresses smoothly:

```
Inharm  -100  1.334 1.661 1.926      Inharm    0  2.069 3.033 5.135  (anchor)
Inharm   -60  1.534 1.985 2.510      Inharm  +50  4.833 6.770
Inharm   -20  1.861 3.463 4.314      Inharm +100  7.998 11.505
```

**The two that were already fine, and why** — worth recording so nobody
"fixes" them: the cymbal jitter is `knob_exp2(2.0·d)`, already exponential and
symmetric; the kernel stretch looks linear (`stretch = 2.4·d`) but is consumed
inside an exponent — `freq *= exp2f(stretch · log2f(freq/f0) · 0.25)` — so a
mode at ratio r maps to r^(1+0.6·d), symmetric in log space with no clamp.
**Linear-looking is not the test; where the value lands is.**

**The KS allpass is inert below centre BY DESIGN, not by oversight.**  It is
the one *absolute* Inharm consumer (`ap_coeff`, floored at 0 for stability),
and `ap_coeff = 0` already *is* the harmonic extreme for a Karplus-Strong
string — there is no "less inharmonic than harmonic" for that mechanism to
reach.  Both KS presets ship Inharm 0, so the negative half is byte-identical
to shipped (the −inf above is exactly that).  Leave it.

Verified: 41/41 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**103/103**, 0 NaN/silent across 40, stability 4096 combos + 492 rolls.

### Pass 36 — `Inharm` made BIPOLAR (-100..100), which un-floors it everywhere

User: *"If problem is bottom range of InHm, we could make the range bipolar
(-100, 100).  Presets will be the same and result should be the same."*  Right
on all three counts, and a better fix than pass 35's per-preset re-centring.

**The floor was systemic, not an 808Sub quirk.**  Every Inharm mapping is
reference-anchored on `d = norm − (that preset's own shipped Inharm)`, so the
knob's travel is whatever the range leaves either side of the shipped value.
**All 41 presets ship Inharm in 0..3** — against a 0..199 range they were all
sitting on the floor, so on every one of them the knob could only push upward.
Centring the range on 0 gives all 41 a downward half they never had.

- `header.c`: `{0, 199, 30, 1}` → `{-100, 100, 0, 1}` (the `{min,max,centre,
  default}` layout `Mterl` already uses for a bipolar knob).
- Normalisation `×0.005` → `×0.01` at the six Inharm sites, which **keeps the
  reach**: `100×0.01 = 1.00` against the old `199×0.005 = 0.995`, so full-up is
  the same knob it was.  (Careful: `k_paramDkay` also uses `×0.005` two cases
  away and must NOT be touched.)
- The four *anchored* consumers get `fmaxf(-1.0f, …)` so negatives survive;
  the one *absolute* consumer — the KS allpass coefficient — keeps its `≥ 0`
  floor, so the negative half is simply inert on ENGINE_KS rather than
  inverting the allpass.  Both KS presets ship Inharm 0, so nothing that used
  to work is lost.
- **Pass 35's `InHm = 40` on 808Sub is reverted to 0**, now redundant.

**All 41 renders byte-identical**, exactly as the user predicted: `d = 0` at
the shipped value under either range.

**Two real bugs this shook out.**
1. **`ap_coeff` would have hit exactly 1.000** at the new maximum — a one-pole
   allpass with its pole *on the unit circle*.  The old range never reached it
   (199×0.005 = 0.995) so the bound had only ever been implicit; it is now an
   explicit `fminf(0.995f, …)`.
2. **The dive-depth curve was linear and floored at d = −0.21.**  `max(0.05,
   1 + 4.5·d)` was written when Inharm could only go up, so its downward half
   had never been reachable — under the bipolar range everything below Inharm
   −30 measured *identically* (47.8 Hz start) and ~70 % of the new travel was
   dead.  Now `knob_exp2(2.5·d)`: exponential, never negative, and
   `knob_exp2(0) == 1.0` exactly so the anchor holds.

808Sub dive start across the finished knob (zero-crossing measured):

| Inharm | −100 | −60 | −30 | −10 | **0** | 25 | 50 | 100 |
|---|---|---|---|---|---|---|---|---|
| linear (before) | 47.8 | 47.8 | 47.8 | 89 | 134 | 242 | 348 | 571 |
| **exp (now)** | **56.5** | **70.8** | **92.7** | **117.6** | **134** | 178 | 267 | 632 |

Level is flat across the whole sweep (250 ms RMS 0.4753-0.4761).

**Three tools carried the stale range and were silently testing nothing** — the
pass-31 lesson again.  `test_hw_debug` **T27a** drove `Inharm = 199` as "max";
that is now out of bounds and `setParameter` *rejects* it, so the assertion was
passing on a value it never set.  `stability_sweep` swept the Inharm corner
`{15,0,199}` (same rejection, and it never touched the negative half) and
`param_audit` declared the range `0..199`.  All three updated to −100..100.
**When a parameter's range moves, grep every tool for the old bounds** — a
tool that sets an out-of-range value fails silently and keeps reporting PASS.

Verified: 41/41 byte-identical, syntax clean, test_dsp exit 0, test_hw_debug
**103/103** (T27a fixed), 0 NaN/silent across 40, stability re-run with the
corrected bipolar corners.

### Pass 35 — 808Sub's pitch dive turned back on (it had never fired)

User: *"Change 808sub"*, after pass 34 reported the `pitch_env` clobber.

**One line of cause, one preset of effect.**  `PartialReset()` zeroes
`pitch_env / _decay / _amt`; only `LoadPreset` ever wrote them; the NoteOn
restore block rebuilt every `boom_*` field but not those three.  So the first
hit cleared them for good and 808Sub played a **flat 45 Hz** for its whole life
instead of the 160 → 45 Hz dive its own comment documents.  The restore now
lives in that block, and 808Sub dives as designed — measured by zero-crossing
so the sweep is not smeared by a window:

```
t=  3.2 ms 133 Hz    t= 27.6 ms  71 Hz    t= 70.8 ms 48.0 Hz
t= 11.2 ms 105 Hz    t= 42.6 ms  57 Hz    t=102.8 ms 45.6 Hz
```

(The instantaneous start is 45 + 115 = 160 Hz; τ ≈ 21 ms means it is already at
133 Hz by the first complete half-cycle — which is also why a 30 ms-window FFT
read the opening as 97 Hz.  **Use zero crossings, not an FFT, to check a fast
sweep.**)  Level is unchanged: peak 1.000 → 1.000, 250 ms RMS 0.4622 → 0.4617,
so the dive costs nothing at the limiter.

**The restore is gated to 808Sub + RackTom on purpose.**  Five presets carry
non-zero pitch_env data; the other three — AcSnare (amt 18), Koto (amt 1.5, and
on ENGINE_KS it would sweep the *delay line*) and KickDrum (amt 9, inert because
its boom is written against `boom_env`) — are HW-approved as they sound and were
not part of the request.  A blanket restore would have re-voiced them silently.
**Pass 22's Inharm → dive-depth knob on 808Sub is live again as a side effect** —
it had been scaling a zero.

RackTom's own local restore from pass 34 was folded into the shared one; its
render is **byte-identical** across that refactor, which is the check that the
two paths were equivalent.

**Exactly 1 of 41 renders changed** (`02_808Sub.wav`); the other 40 are
byte-identical.  Verified: syntax clean, test_dsp exit 0, test_hw_debug
**103/103**, 0 NaN/silent across 40.

**Dive depth is on `Inharm`, and the knob was re-centred** (user: *"Is it
possible to wire the dive amount to an existing parameter?"*).  It was already
wired — pass 22 mapped Inharm → dive depth for the kick family — it had simply
been scaling a zero for as long as `pitch_env` was being cleared.  Inharm is a
free knob on this preset: its other two roles are the KS `ap_coeff` write
(ENGINE_KS only) and the modal overtone spread (needs modes; 808Sub's modal
config is empty).

The catch was reach.  808Sub shipped `InHm = 0`, the BOTTOM of the range, and
the mapping is `factor = max(0.05, 1 + 4.5·d)` with `d = norm − ref` — so with
ref pinned at 0 the knob could only ever *deepen* the dive, never trim it:

| Inharm | 0 | 25 | 50 | 100 | 150 | 199 |
|---|---|---|---|---|---|---|
| dive start, ref=0 (before) | 134 Hz | 177 | 242 | 348 | 444 | 585 |
| dive start, ref=40 (**now**) | **50.8** | 98.8 | 142 | 264 | 375 | 480 |

Moving the shipped column 0 → 40 re-centres it: Inharm 0 now gives a ~51 Hz
start — effectively flat, i.e. **the pre-pass-35 sound is still reachable from
the front panel** — through the shipped 160 Hz dive at 40, out to 480 Hz.
**All 41 renders stay byte-identical**, because `m_modal_inharm_ref` is read
from that very column, so `d = 0` at the shipped value either way.  Level is
flat across the whole sweep (250 ms RMS 0.4754-0.4763).

**This is the general trick for a one-sided knob**: when a reference-anchored
parameter ships at the end of its range, the knob loses one direction.  Moving
the shipped value re-centres the travel *for free* — the anchor moves with it,
so the sound cannot change.  Worth checking wherever a knob "only goes up".

### Pass 34 — RackTom "sdeng not thump": a glide misread as modes, and two traps

HW listen on pass 33: *"Rendered is too much string like ('sdeng' sound)
instead of a clear 'thump'."*  Correct, and the cause was a **measurement
error in pass 33**, not a voicing preference.

**1. The mode cluster was an artefact of a pitch glide.**  A windowed FFT of
`rock-rack-tom-1.wav` shows partials at ratios 1.071 / 1.272 / 1.350, and pass
33 shipped them as static modes.  Tracking the dominant partial in 30 ms
windows instead shows there is only **one** partial, and it slides:

```
t=  0 ms 160 Hz    t= 60 ms 127 Hz    t=150 ms 113 Hz
t= 20 ms 142 Hz    t= 80 ms 122 Hz    t=250 ms 112 Hz
t= 40 ms 133 Hz    t=100 ms 119 Hz    t=400 ms 104 Hz
```

A 160 → 110 Hz head bend (~650 cents, τ ≈ 55 ms).  **A stationary FFT cannot
represent a glide**, so it smears one moving partial into several fixed ones —
and resynthesising those builds a detuned chord that BEATS.  Modes at 1.000 and
1.071 beat at 12.4 Hz, whose first constructive maximum lands ~60 ms after the
strike, so the drum **swelled to its peak instead of decaying from it**
(envelope rms 0.35 → 0.65 at 60 ms; the reference peaks at t=0 and falls
monotonically).  That swell is the "sdeng".  **Rule: before trusting a mode
cluster from any FFT, track the partial over short windows.  Percussion with a
head bend will always fake a cluster.**

**2. `pitch_env` is cleared by `PartialReset()` and never restored — 808Sub's
documented sweep has been dead.**  The glide belongs on the boom oscillator
(the only thing here that can change pitch mid-note), which is what 808Sub
claims to do.  It does not: `PartialReset()` zeroes `pitch_env`,
`pitch_env_decay` and `pitch_env_amt` (`dsp_core.h`), the NoteOn restore block
rebuilds every `boom_*` field but **not these three**, and they are written
only by `LoadPreset` — so the first NoteOn clears them permanently.  Measured
on the shipping tree: **808Sub sits flat at 45 Hz** through the whole hit
against its documented "160 → 45 Hz sweep", and pass 22's Inharm → pitch-dive
knob multiplies a zero.  KickDrum escapes only because its formula uses
`boom_env`, which *is* restored.
**808Sub is deliberately NOT fixed here** — it is HW-approved as it stands and
un-breaking it would change an approved sound; that is the user's call.
RackTom restores the three fields in its own post-`PartialReset` block.
This is the same defect class as the snare-wire bug (pass 19) and the
`pitch_env` gotcha now sits with it in the gotcha list.

**3. Chasing a band ratio with gain fed the limiter.**  Pass 33 raised
`trans_gain` to 24 to close the 300 Hz-1 kHz gap.  Swept against the master
stage, that measured **worse in the band it was raised for** — 300 Hz-1 kHz
reads 2.21 % at gain 0 and 1.95 % at gain 5 — because a burst that large pins
the limiter, which ducks the whole hit and recovers into a swell: the first
20 ms sat at rms 0.25 with the burst against **0.48 without it**.  Shipped at
2.5, the last value that still decays monotonically.  Pass 30's rule holds:
*a limited bus cannot give you level.*

**4. The boom must not glide through a loud mode 1.**  First fix attempt kept
mode 1 at env 0.55 with a 520 ms T60 to carry the tail; the gliding boom passed
through it and cancelled, digging an audible "wow" notch (envelope 0.29 → 0.06
→ 0.14 across 80-120 ms).  Mode 1 is now quiet (env 0.28) since the boom *is*
the fundamental, and the boom rests at **174.61 Hz** — exactly note 53 — so the
two lock instead of beating.

**Result:** energy in the first 25 ms **6.4 % → 34.0 %** (reference 39.8 %,
Ac Tom 22.4 %), envelope monotonic with no swell and no notch, glide live at
238 → 179 Hz.  `boom_decay` 0.99972 is a measured compromise — the reference
decays in two stages and one exponential cannot, so the knob trades attack
share against tail: 0.99976 → 29.7 % / t40 383 ms, 0.99960 → 44.2 % / t40
231 ms; shipped 0.99972 → 34.4 % / 329 ms, tipped toward the attack because
thump was the complaint.

Note tracking survived the restructure (`note_audit` Δ +0.1 semitones, +12.0 on
a +12 test) because the note ratio is folded into `boom_tune` — booms in this
engine are otherwise absolute Hz, which is exactly why the kicks audit as
"Note inert".

Verified: **40/40 pre-existing renders byte-identical**, syntax clean, test_dsp
exit 0, test_hw_debug **103/103**, 0 NaN/silent across 40.

### Pass 33 — New preset 40 "RackTom", and the seven-table checklist for adding one

User request: *"Add new Tom preset."*  The shipped `Ac Tom` (12) is already the
**low** drum — it ships note 45 (110 Hz) with its boom oscillator hard-wired to
`tom_bm` = 110 Hz — so a second low tom would have duplicated it.  Chosen (with
the user) as a **rack tom at note 53 (F3, 174.6 Hz)**, which turns the two into
a kit rather than two takes on the same drum.

**What actually distinguishes it, and what does not.**  The mode *ratios* are
Ac Tom's unchanged (1.59 / 2.14 / 2.30): both are two-headed drums, and the
mode series of a circular membrane does not move with tuning — only its
absolute frequencies do, which the Note column already supplies.  Copying the
ratios is therefore the physically correct choice, not laziness.  What does
change is everything that scales with drum SIZE:

| | Ac Tom | RackTom | why |
|---|---|---|---|
| Note | 45 (110 Hz) | 53 (174.6 Hz) | rack vs floor |
| boom osc | `tom_bm` 110 Hz | `rtm_bm` 175 Hz | shell air tracks head tuning |
| `boom_mix` | 0.18 | 0.12 | smaller shell holds less air |
| `boom_decay` | 0.99945 | 0.99925 | …and holds it more briefly |
| `boom_attack_inc` | 0.0008 | 0.00115 | a small drum's boom arrives sooner |
| body T60 | 500 ms | 300 ms | tighter head |
| modes | 4 | 5 (+ratio 3.60) | the woody stick "tock" a mounted tom has |
| stick T60 | 30 ms | 22 ms, 1.3-5 kHz | tight head stops contact sooner |
| `modal_mix` | 0.18 | 0.20 | head over shell = reads as pitched |

**Measured:** fundamental **174.7 Hz** against a nominal 174.6 (`note_audit`
Δ = **+0.0 semitones**), tracks a +12 transpose at **+12.0** exactly, t40
**236 ms** vs Ac Tom's 362, and **100 %** of the 150-400 ms body energy sits in
the 150-300 Hz band (peak 174 Hz) — a clean pitched body, no hiss bed.  Level
lands **−1.5 dB** against the 41-preset mean (Ac Tom is +1.2), well inside the
family spread, so it needed no `velocityGain` trim.

**A metric trap worth recording.**  A magnitude-weighted spectral centroid read
the new body at 1059 Hz and Ac Tom's at 246, which looks like a bright-body
regression.  It is an artefact: a −60 dB noise floor spread across 20 kHz
dominates a *magnitude*-weighted mean while contributing ~0 % of the *power*.
The band-power table above is the honest measurement.  **Use power, not
magnitude, for centroid comparisons on percussion** — this metric is also what
several earlier passes quoted.

**Adding a preset touches SEVEN parallel tables**, all indexed by the same
enum and none of them checked against each other by the compiler:

1. `ProgramIndex` enum (`synth_engine.h`) — before `k_NumPrograms`
2. `modal_preset_configs[k_NumPrograms]`
3. `model_param_presets[k_NumPrograms][k_model_param_total]`
4. `kPresetEngine[k_NumPrograms]`
5. `LoadPreset`'s `presets[k_NumPrograms][k_lastParamIndex]`
6. `getPresetName`'s `preset_names[]`
7. `header.c` — **`.num_presets` AND the Program parameter's `max`**, which is
   also the only thing stopping the OS handing `LoadPreset` an out-of-range
   index

Miss any one of 2-6 and the arrays disagree in length — the enum grows but the
initialiser does not, so the last preset reads zeroed or out-of-bounds data.
Miss 7 and the preset exists but is unreachable from the UI.  Host tools with
their own copy of the count also need it: `render_presets.cpp` has a hard-coded
entry list (a preset missing there is simply never rendered or diffed, which
would silently exempt it from the byte-identity check), and `note_audit.cpp` /
`stability_sweep.cpp` had literal `40`s — **both now read
`BrachettiSynth::k_NumPrograms`** so they cannot drift again.

Size: text+rodata 84,636 → **85,028 B (+392)**; `.rodata` +60 is exactly the
new 24×`int16_t` row (48) + name pointer (4) + `"RackTom"` (8), and `.bss` +192
is the three per-instance table rows.

Verified: **40/40 pre-existing renders byte-identical** (the new one is the only
added file), syntax clean, test_dsp exit 0, test_hw_debug **103/103**,
0 NaN/silent across **41** presets, ARM cross-build clean.

**Retuned in the same pass against `samples/rock-rack-tom-1.wav`** (user-supplied).
The physical defaults above were kept only until the reference landed; every
one of them that the measurement contradicted was replaced.  Three findings:

**1. Copying Ac Tom's membrane ratios was wrong, and the sample says why.**
A two-headed tom's batter and resonant heads couple through the shell air,
which does not transpose the single-head Bessel series — it SPLITS the
fundamental into a tight cluster.  Measured (sustained 50-550 ms window):

| freq | amp | ratio | T60 |
|---|---|---|---|
| 113.0 Hz | 1.000 | 1.000 | 617 ms |
| 121.0 Hz | 0.188 | 1.071 | ~560 ms |
| 143.8 Hz | 0.124 | 1.272 | 483 ms |
| 152.5 Hz | 0.075 | 1.350 | — |
| 213.1 Hz | 0.096 | 1.886 | 586 ms |
| 309.8 Hz | 0.110 | 2.729 | 729 ms |

The drum is also far PURER than the defaults assumed — every partial above the
fundamental sits at 0.06-0.19, not 0.24-0.52, and **96 % of the reference's
power is in one 100-200 Hz band**.  The 1.071 sideband beating 8 Hz from the
fundamental IS the tom's characteristic wobble and is kept, but deliberately at
env 0.19: pass 17 showed two close modes held LOUD is exactly what made the old
Timpani "rough ripple".  The 1.815 partial is dropped rather than shipped
beside 1.886 — 8 Hz apart at equal level, same trap.

**2. The first cut rang half as long as the real drum.**  Measured t40 is
**400 ms**; the "smaller shell decays faster" reasoning had produced 220 ms.
Ac Tom is 324 ms, so a rack tom actually rings LONGER than the floor tom here.
T60s snapped to the measured values (and `boom_decay` 0.99925 → 0.99950, since
the shell has to hold under a 620 ms body rather than drop out from under it).
Result: t20 202 / t40 419 / t60 617 ms against the reference's 213 / 400 / 613.

**3. "Brighter stick" meant the wrong band.**  The burst shipped at 1.3-5 kHz
on the assumption that stick contact is treble.  The reference's 0-30 ms window
carries **12.0 % of its attack energy in 300 Hz-1 kHz and only 2.7 % in
1-6 kHz** — a stick on a tuned head is a MID thwack, not sizzle, and a 1.3 kHz
corner filtered out the one band that mattered.  Band moved to ~250 Hz-1.15 kHz,
gain 3.2 → 24.  Also note this is a **transient, not modal**: that mid energy
falls 12.0 % → 1.4 % → 0.5 % across 0-30 / 30-100 / 100-300 ms, which is why it
belongs in `trans_*` and not in the mode table.

**Structural floor, measured — do not chase it with gain.**  `trans_*` is a
*difference of two one-pole lowpasses*, so it rolls off at only 6 dB/oct above
its upper corner; integrated over 1-24 kHz that residual beats a narrow
300 Hz-1 kHz passband no matter where the corners sit.  Swept: raising gain
6.5 → 45 moves 300 Hz-1 kHz from 0.31 % → 3.58 % while 1-6 kHz races 0.77 % →
20.7 %, i.e. **the top band rises ~3× faster than the target band**, and the
result is a bright click, not a thwack.  Shipped at gain 24, where 1-6 kHz
lands on the reference (2.28 % vs 2.74 %) and 300 Hz-1 kHz reaches 1.13 %
(3.6× the first cut, still ~10× under the reference).  Closing the rest needs a
2-pole burst filter, not more level.  This is the same call pass 18 made on
Taiko's close-mic stick transient, for the same reason.

**The pitch collides with Ac Tom — DECIDED, do not re-litigate.**  The
reference's fundamental measures **113.0 Hz**; Ac Tom ships **110.0 Hz**.  The
sample is a *rock* rack tom, i.e. deliberately tuned low, so being faithful to
its pitch would have put two presets a semitone apart.  Asked, and the user
chose to **keep note 53 (174.6 Hz)** and transpose the measured *character* up,
preserving the kit logic the preset was added for.  So RackTom is deliberately
NOT at its reference's pitch, and a future pass measuring the two side by side
should not "fix" that.  (If it is ever revisited, faithful is a two-value
change: preset column Note 53 → 46 and `rtm_bm` 175 → 113 Hz.)

### Pass 32 — Code review: one real bug, dead code, and the size budget was fiction

No new features and **no sound change** (40/40 renders byte-identical).  A
general review pass; four things came out of it.

**1. `Reset()` left master-stage state queued — a 14 dB gain error (T41).**
A preset change over a ringing voice defers the incoming preset's master drive
behind the ~10 ms fade (pass 30).  `LoadPreset` can never leave a stale one,
because its parameter loop always rewrites Gain and that case clears the queue
— but **`Reset()` has no such loop and cleared nothing**, while killing every
voice, so the fade the deferral was waiting on never happened.  `Suspend()` is
`AllNoteOff() + Reset()`, so a suspend caught between a preset change and the
end of its fade handed the next session the *previous* preset's drive:
measured GtrStr → Gong → suspend → resume, `master_drive` jumped **1.0 → 5.0**
and stayed there until the user turned Gain.  `Reset()` now also clears
`master_lim_env` (a limiter follower left high rides the first strike after
Resume down for ~20 ms) and `m_idle_flush_blocks`.  **T41a-c**; T41b/c fail on
the pre-fix tree, verified.

**2. The coupling-stability branch had been dead since the K=2.5 revert.**
`processBlock` computed a delay-length ratio (a float divide, per active voice
per block) to choose between "incoherent" and "coherent" clamps whose two arms
were *character-for-character identical* — the permissive K=2.5 arm was reverted
long ago for being unstable, and the test outlived it.  Removed.

**3. The voice bus is mono; half the output writes were dead.**  Every engine
is single-channel, and Stage 4b filters `main_out[i*2]` and writes the result
to *both* lanes — so everything the voice loops accumulated into `main_out[i*2+1]`
was overwritten before it could be heard.  The voice loops now write the left
lane only, and the cymbal soft-headroom pass iterates `frames` instead of
`frames*2`: **64 fewer float divides per block** whenever a cymbal renders
(ARM disassembly: the pass drops from 128 iterations of 15 instructions to 64
of 16).  Both early returns stay correct — the kernel path writes both lanes
itself, and the idle path returns on an all-zero buffer.
*Honest measurement*: on x86-64 `-O2` this is **within noise** (±4 %), because
the host vectorises the paired stereo store into one 8-byte store and so cannot
show the win.  The saving is an in-order Cortex-A7 at `-Os` argument (no
auto-vectorisation there), supported by the instruction counts above, not a
host benchmark.

**4. Size: −1,872 bytes, and the budget in this file was wrong by ~2.8×.**
`LoadPreset`'s `presets[40][24]` was `int32_t` for a stored range of
[-10, 1999]; narrowing it to `int16_t` frees 1920 B of `.rodata` with every
value round-tripping unchanged.  Net ARM code segment 86,508 → **84,636 B**.
Which is the point: pass 32 finally *measured* it (the distro ships an ARM
cross-compiler; the Makefile only fails because it hardcodes a Windows
toolchain path), and the answer is nothing like the "≈ 30 KB" ceiling this
file has asserted for 20 passes.  See the rewritten constraint section below —
in short, `.rodata` alone is 34.3 KB, that figure is toolchain-independent, and
the unit runs.  **Stop dodging `.rodata` and start measuring.**

Also: `k_lastParamIndex` replaces the magic `24`s in `setParameter`/`LoadPreset`
plus a `static_assert` tying the enum to header.c's `num_params` (adding a
parameter to one and not the other silently shifts every preset column), and
three comments naming the retired `Rsntrs` knob were corrected.

Verified: **40/40 renders byte-identical**, syntax clean, test_dsp exit 0,
test_hw_debug **103/103** (T41a-c new), `stability_sweep` 4096 combos + 480
rolls, worst |peak| 0.9900, 0 problems, 0 NaN/silent — every number identical
to the pass-31 baseline.  ARM cross-build clean.

### Pass 31 — Rsntrs → Partls (cymbals), the freed slot becomes Velocity, note audit

User request: *"Is it possible to move Rsntrs to Partls for cymbals?  So we can
spare a parameter and use it for velocity (ghost notes for low values and big
wham for high values)"* + *"Check the note assignment for instruments."*

**1. The swap is free in both directions.**  `Rsntrs` was a cymbal-only
resonator-density control occupying one of 24 GUI slots and doing nothing on
the other 34 presets; `Partls` is inert on the six `ENGINE_CYMBAL` presets,
which bypass the shared modal bank it reshapes.  Density is now
`25 + 5 × Partls` % (the old 25-60 % range, 8 positions), and unlike `Rsntrs`
it is a **normal per-preset column** — the six cymbal rows store `Partls = 3`
= the 40 % the old knob defaulted to, so all **40 renders stay byte-identical**.
Display follows: a cymbal preset shows `Rs40%`, not `AB:32`.

  **The trap that had to be closed**: `Partls` 5-7 is the ResA/ResB *editor
  select*, and `LoadPreset` saves and restores that selector across preset
  changes.  Left alone, dialling 60 % density on a gong would leave
  `Model`/`Dkay`/`Mterl`/`Inharm` writing to **ResB only** on the next drum
  loaded — a half-dead knob nobody could trace back.  On a cymbal preset the
  selector branch is skipped entirely (**T40c**).

**2. Slot 3 = `Velocity`, bipolar -100..+100, default 0 = exact no-op.**
Applied ONCE at the top of `NoteOn`, before either strike path reads it:

```
v' = clamp( v × 2^(2.4·knob), 0.02, 1 + 0.30·knob )
```

so it is live on every family (audits `ok` on all 8 `param_audit` exemplars)
without touching a single per-engine mapping — velocity is this unit's whole
dynamics axis.  `knob_exp2(0)` is exactly 1.0, so the default is byte-identical
by construction (**T39a** asserts it over all 127 velocities).

  Measured (250 ms RMS, `velocity_probe.cpp`, PRNG pinned): ghost at -100 is
  **-4.5 dB (Kick2) to -18.3 dB (Cymbal)**; wham at +100 lifts a velocity-64
  stroke by +0.8 to +11.1 dB and reaches the full-velocity render on every
  exemplar (**T39d**).

  **Honest limitation, do not "fix" it by adding gain.**  Above neutral the
  knob may push a strike ×1.30 past a MIDI 127 (`kVelWhamMax`; the two leaf
  clamps in `cymbal_note_on` and `ModalDrumKernel::Trigger` were relaxed 1.0 →
  2.0 as sanity bounds so the over-range survives).  That is **+4.1 dB on
  Cymbal** and **inaudible on Kick2/Marimba** (-38 dB difference-RMS), because
  those presets already pin the master limiter at full velocity — pass 30's
  wall: *a limited bus cannot give you level, but balance is free*.  The up
  direction is for lifting weak strokes; for more than a full-force hit the
  controls are `VlMllRes`/`VlMllStf` (character) and `Gain` (drive).

**3. Note assignment — new `note_audit.cpp` + `note_audit.py`, findings in
`NOTE_AUDIT.md`.**  Renders every preset **at its own shipped Note** through
`GateOn()` (the sequencer path; `render_presets.cpp` passes its own hard-coded
notes and cannot see this class of drift):

- **All 8 engine anchors match** (six cymbal `ref_note`s, both kernel
  `root_note`s, Δ = 0): no preset plays a transposed calibration.
- **Note is inert on 8 presets** (exactly 0.0 semitones on a +12 test):
  808Sub/KickDrum (boom osc, empty modal config — you hear 44.7 / 56.7 Hz while
  the screen says 65.4), the three snares (wire bands are absolute Hz), and the
  three NOISE presets.  RimShot is the snare exception at a full +12.
- **Five presets are pitched oddly for the instrument** — Bongo 147 Hz
  (bongos are 250-600 Hz), Woodblock 131 Hz (~0.8-2.5 kHz), AcSnare/BrshSnr
  73 Hz (**note 38 is the General MIDI drum-map NUMBER used as a pitch**;
  Brachetti's Note is a pitch, not a kit slot), Triangle 440 Hz, Claves 4
  semitones under the one clave reference in `samples/` (measured: 955-975 Hz).
  **Nothing changed** — each alters an approved sound; `NOTE_AUDIT.md` §4 has
  the recommendation per preset and awaits a listen.
- **`render_presets.cpp` disagrees with the shipped column on exactly one
  preset**: Bongo is *scored* at note 57 (220 Hz) and *ships* at 50 (147 Hz).
  That makes Bongo the strongest of the five.

**Tooling fixed along the way** (both were measuring the wrong thing):
`test_hw_debug` T36c set slot 3 to 60 for "max Rsntrs" — that is now the
Velocity knob, so it was silently testing the CPU budget at the DEFAULT bank
size; it uses `Partls = 7` and the worst aggregate cost is back to a meaningful
368 (limit 400).  `param_audit.cpp` swept `MlltStif` over 10-500, a range the
parameter lost in pass 30 (it is ÷100 over 0-50 now), so 500 clamped to the
same stiffness as 50 and the knob's lower half went unmeasured.

Verified: **40/40 renders byte-identical**, host syntax check clean, test_dsp
exit 0, test_hw_debug **100/100** (T39a-e, T40a-c new), `stability_sweep` 4096
combos + 480 rolls with the Velocity knob riding the hit-hardness corner bit,
worst |peak| **0.9900** (the brickwall limit) and 0 problems — identical to the
pass-30 baseline.  **ARM `.text` still unverified** (no cross-compiler in this
session): this pass adds `vel_bias_apply`, one branch in the Partls case and a
`snprintf` display branch, and removes the old `k_paramCymReso` case — roughly
neutral, but confirm against the 28 KB budget on the next flash.

### Pass 30 — Cymbal CPU crash, master limiter rebuild, preset/UI defaults

HW batch on pass 29.  **HHat-C confirmed good** — the pass-27 same-tick fix is
validated on device.  Seven other items, three of which turned out to share a
root cause.

**1. Cymbal/gong audio crash — pass 26's budget measured the wrong thing.**
Pass 26 raised the cymbal cap 2→4 voices and replaced the voice-count guard with
`kCymResonatorBudget`, on the stated theory that bounding the aggregate resonator
count is "a STRONGER CPU guarantee than the voice count".  **That theory is
false**, and `cym_cpu_probe.cpp` (new) measures by how much: decomposing
`cymbal_process` into `fixed + k*resonators` shows the FIXED per-voice cost —
pink noise, two swept driver one-poles, the PM block, the DC blocker, the
magnitude envelope — is worth **~124 resonator lanes on its own, 79 % of a
default 32-lane gong voice**.  The budget therefore bounded about a fifth of the
real cost, and at the default Rsntrs it never even bound (4 gong voices ask for
128 lanes against a 240 ceiling).  Measured 4 voices = **3.7× one voice**.

Three changes:
- **Control-rate driver** (`kCymCtrlStride` = 8): the two one-pole coefficients
  and the three PM LFOs are updated every 8 samples instead of every sample —
  the fastest thing there is HHat-O's 12 ms attack, so a 6 kHz control rate
  still resolves it ~72×.  Coefficients are held piecewise constant (the filter
  output stays continuous, so a stepped cutoff cannot click); the PM sum IS a
  multiplier so it is linearly interpolated back to sample rate.
- **Cost-accurate budget**: `kCymVoiceFixedLanes` (124) + `kCymCostBudget`
  (2 × (124+60) = 368).  Every active cymbal voice charges its fixed cost PLUS
  its bank, both when deciding whether a new voice is affordable and when
  sizing its bank.  The ceiling is the **pre-pass-26 worst case**, the only
  cymbal CPU level with field evidence (25 passes, no crash).
- Result: 4 rapid gong strikes now occupy 2 voices at **29.4 µs/block**, against
  95.6 µs for the crashing build and 49.7 µs for the last known-good — **0.59×
  the last safe level**.  Fixed per-voice cost fell 37.1 → 24.6 ns/sample.

**2. Cymbal "very quiet" — real, but NOT a regression.**  Measured over a common
250 ms window (full-render RMS is not comparable: render lengths run 1-20 s),
five of the six ENGINE_CYMBAL presets sat **11-18 dB under the mean of every
other preset**: Cymbal −18.3, HHat-O −17.9, Ride −17.3, RidBel −14.7, Splash
−11.3, Gong +3.6.  Rendering the pre-26 and pass-26 trees shows the same levels
(Cymbal 0.0080 → 0.0099 RMS), so it is a long-standing voicing miscalibration
that pass 29 made *audible*: its master change lifts transient-dense presets
~3.7 dB but a decaying wash barely at all, so everything else moved up around
them.  Corrected with a per-preset trim on `velocityGain` — the only uniform
output scaler on the voice (scaling config levels is NOT uniform, because
`stickLevel` feeds both the resonator drive and the direct tap and would land on
the thwack squared).  Cymbal ×3.2, Ride ×2.9, RidBel ×2.1, Splash ×1.4; the
family now sits −2.7 dB against the rest instead of −11.  **Gong left alone**
(already correctly placed).  **HHat-O left alone** despite measuring equally
quiet, because CLAUDE.md flags it "HW-approved, do not break" — raise it only on
an explicit listen.

**3, 4. Kick "thump not increased" + "brittle/distorted on long decays" — one
cause, and it was a hard clipper nobody was looking at.**  A
`clamp(main_out, ±0.99)` sat between the voice bus and the master stage, left
over from the debug render stages.  In the shipping path Stage 4b always runs
and bounds the output by construction, so it protected nothing — but the kick
presets sit on top of it permanently (Kick2's bus is pinned at 0.99 for most of
the hit), which made it both a large distortion source and an **invisibility
cloak**: any layer added underneath a pinned bus is clipped straight back off.
That is why VlMllRes measured 0.98-1.02× on the kicks while the thump layer was
being armed perfectly (verified directly: `thump_env` 1.6 at 115 Hz on all
three).  Removed.  Kick2 crest 1.26 → 1.50, H3 **−23.9 → −37.8 dB**.

**5. Master stage: waveshaper → gain envelope.**  Pass 29's curve was applied
per SAMPLE, i.e. the gain changed *within* a cycle — on a 45-90 Hz kick boom
that manufactures high-order harmonics.  808Sub at Dkay+Rel max measured H4 at
−21.5 dB under the pre-29 `x/(1+|x|)` and **−7.4 dB after pass 29**.  Now an
instant-attack / slow-release peak follower applies ONE gain per cycle.  Since
`master_lim_env >= |x|` always, `x * (limit(env)/env)` is still bounded by
`kMasterLimCeil` by construction.

Release swept 10/20/30/40/60/90/180/350 ms.  **20 ms wins**, and beats *both*
earlier master stages on the reliable metrics (the 808Sub harmonic numbers are
unreliable — it pitch-sweeps 160→45 Hz, so a fixed-frequency Goertzel smears;
crest factor is sweep-independent and was used to decide):

| preset @Dkay+Rel max | pre-29 crest | pass-29 crest | now |
|---|---|---|---|
| Kick2 | 1.15 | 1.11 | **1.47** |
| 808Sub | 1.51 | 1.25 | **1.75** |
| KickDrum | 1.34 | 1.21 | **1.67** |

KickDrum's H4 improves **27 dB** over pass 29.  Two hits 150 ms apart show no
ducking (2nd/1st peak within 0.01 dB) at any release below 350 ms.

**Threshold raised 0.55 → 0.75** now that the stage is an envelope: a waveshaper
had to start early to keep peaks down, an envelope limiter holds the ceiling at
any threshold, so the threshold only trades loudness against gain riding.  Swept
0.55/0.65/0.75/0.85 — 0.75 recovers 0.54 dB while every kick keeps a crest well
above the pre-29 build the HW called "perfect".

**Net level: −1.37 dB vs pass 29, +1.65 dB vs pre-29.**  This is the honest
cost of the fix and it cannot be avoided: pass 29 bought its last ~1.4 dB *by*
distorting (squashing peaks raises RMS).  Worst peak 0.9890, 0 NaN/silent.

**A dead end, measured, do not re-try:** smoothing the limiter knee with a
smoothstep band (0 / 0.10 / 0.20 / 0.30 / 0.44) moved 808Sub's H4 by **0.2 dB**.
The curvature corner is irrelevant because the presets drive this stage ~4.5×
full scale — the waveform sits deep in the compressive region, nowhere near the
corner.  The problem was per-sample waveshaping itself, not the knee shape.

**6. VlMllRes/VlMllStf depth.**  With the clipper gone the kick knobs were
widened and made to **trade** rather than add — a limited bus cannot give you
level, but balance is free, and a beater-forward kick should have *less* body,
not more of everything.  VlMllRes now scales `boom_mix` down as it raises the
thump (and up when turned down); VlMllStf gained a down direction.  Knob
audibility (RMS of the knob-max minus shipped render, relative to signal):

| preset | before | after |
|---|---|---|
| Kick2 | −18.9 dB | **−16.3 dB** |
| 808Sub | −9.6 dB | **−3.8 dB** |
| KickDrum | −10.2 dB | **−3.1 dB** |

`knobaud.cpp` (new) maps both knobs across 22 presets.  It uses the
difference-RMS metric precisely because a band or total-RMS metric is fooled by
a downstream limiter holding the level constant while the character changes —
which is what hid these knobs.  Most families already read LIVE; still weak and
**left for the user to prioritise**: Marimba/Vibrph/Kalimba and Shaker/BrshSnr
read literally 0 change on VlMllRes, and Clap/Shaker/HHat-C are inert on
VlMllStf.

**7. Boot default was not Kick2.**  `Init()` calls `LoadPreset(0)`, but the OS
then restores its own parameter set on top via `unit_set_param_value` — and with
no stored state that means `header.c`'s `.default` fields, of which **15 of 21
disagreed with Kick2's preset row** (Note 60 vs 36 = two octaves up, Dkay 25 vs
200, NzRes 0 vs 420...).  Every default is now Kick2's shipped value.  Poly and
Rsntrs keep their own, because `LoadPreset` deliberately skips them.

**8. Preset not showing actual parameter values.**  **31 stored preset values
sat outside the range declared in `header.c`**, so the OS store could not
represent what `LoadPreset` wrote: HitPos min was 2 while 25 presets store 0,
Dkay max was 200 while HHat-O stores 210.  Ranges widened for those two;
out-of-range TubRad values were clamped into [0,20] in the table instead, which
is provably sound-neutral because every TubRad consumer already clamps there.
Audit is now clean: 0 out-of-range, 0 wrong defaults (`range_audit.py` pattern —
re-run it whenever the preset table or header changes).

**9. MlltStif step 10 → 100.**  Stored ÷10 over 10-500 became stored ÷100 over
0-50 (displayed ×100 = 0-5000), all 9 DSP consumers rescaled `*0.002f` →
`*0.02f`, preset column ÷10.  Only Conga (425) and RidBel (491) are not
representable at step 100 and round to 430/490; measured **−85.6 dB** below
signal.  Four other presets shifted by a last-bit float difference at −107 to
−127 dB (`350*0.002f != 35*0.02f`).

**Byte-identity: 2/40.**  Intentional and unavoidable — the master architecture
change and the pre-clip removal both touch every preset.  All *knob* mappings
remain reference-anchored (Δ=0 ⇒ no change).

Verified: syntax clean, test_dsp exit 0, test_hw_debug **92/92** (T36 rewritten
— it hard-coded pass 26's "4 distinct voices" assumption; it now asserts hits
ACCUMULATE and the aggregate COST stays inside budget), `stability_sweep` 4096
combos + 480 rolls, worst |peak| 0.9900, 0 problems, 40/40 renders non-silent
and NaN-free.  **ARM `.text` still unverified**; this pass adds the control-rate
block and the limiter state but removes the pre-clip loop.

### Pass 29 — Master output +3.0 dB, and review findings 2/3/5/6

**Louder output.**  The master stage ended in `x / (1 + |x|)`, which divides
EVERY sample rather than only the ones that need limiting — a 0.5 bus left the
unit at 0.333 (−3.5 dB) and a unity bus at 0.50 (−6 dB), taking the strike crest
with it.  It is the same curve the Timpani/Taiko kernel was given its own master
stage to escape ("compresses the strike back into the body (crest ≈ 1) — the
exact rough / synthy hit the HW comparison flagged").

Replaced with a **soft knee that asymptotes to the brickwall by construction**:

```
over = |x| − thr;      y = thr + span·over/(over + span)      (span = ceil − thr)
```

Unity slope at the knee, monotone, and strictly below `ceil` = 0.99 for every
finite input, so the brickwall clamp behind it is pure safety and nothing is
flat-topped.  Same hyperbola as the old curve, translated to start at the
threshold instead of at zero.  **Measured: +3.02 dB aggregate RMS over the 40
presets** (0.0793 → 0.1122), worst peak 0.9661, 0 presets touching the clamp.

**Threshold chosen by measurement, not taste.**  Loudness trades against
flat-topping, because getting louder in a transient instrument *is* limiting.
All 40 presets rendered at four thresholds (flat = consecutive identical samples
above 0.9, i.e. visible squashing):

| thr | RMS | worst peak | flat samples | longest run |
|---|---|---|---|---|
| 0.85 | +3.77 dB | 0.9873 | 9002 | 293 (6 ms) |
| 0.70 | +3.45 dB | 0.9792 | 5936 | 292 |
| **0.55** | **+3.02 dB** | **0.9661** | **52** | **15** |
| 0.40 | +2.46 dB | 0.9485 | 32 | 15 |

0.55 is the knee: **100× less squashing than 0.85 for 0.4 dB less level**, while
0.40 buys almost nothing for another 0.56 dB.  Baseline: 0 flat samples at RMS
0.0793.

**Two shapes measured and rejected — do not re-try them.**
`0.85 + 0.15·fastertanhf(...)` needs a clamp, because **`fastertanhf` is a
rational approximation asymptotic to ~1.168, not 1.0**: unclamped its ceiling is
1.013 so the brickwall still engaged (16-sample flat top on the RimShot crack),
and clamping the tanh merely *relocates* the brickwall to exactly 0.99, which
pinned **23034 samples** dead flat including an 11 ms plateau on the Gong.  The
kernel path uses that same unclamped form; its presets peak at 0.92-0.94 so its
clamp never engages today, but the shape is a latent trap.

**This intentionally breaks the byte-identical guarantee** — a level change must.
Every preset is louder; nothing else about them moved.

**Review findings 2, 3, 5, 6** (all behaviour-neutral — verified 40/40
byte-identical on their own, before the level change): `LoadPreset`'s bounds
check moved to the first statement so an out-of-range index can no longer be
retained and then indexed into three 40-entry tables; `setParameter(k_paramPartls)`
rejects a negative value before `partial_counts[value]`; `#pragma once` added to
`noise.h`; the stale "cymbals capped at 2" Poly comment in `header.c` corrected.
Finding **4 (the ~35 KB `.rodata` discrepancy) remains open** — it cannot be
settled without the ARM toolchain.

Verified: test_dsp exit 0, test_hw_debug **92/92**, host syntax check clean,
`stability_sweep` 4096 combos + 480 rolls, worst |peak| 0.9900, 0 problems;
40/40 renders non-silent and NaN-free; T38 preset-change fade still bounded.
**ARM `.text` still unverified** — note this pass *removes* the NEON reciprocal
block from the master stage, so it should come out slightly smaller.

### Pass 28 — Preset change during a ringing voice: fade instead of re-excite

Review finding 1, fixed.  Turning the Program knob while anything was sounding
either **burst** or **hard-cut**, measured with `switch_probe.cpp`:

| switch | peak, first 25 ms after ÷ last 25 ms before | after |
|---|---|---|
| Cymbal → Clap | **9.0×** (RMS 19×, still 0.67 at +75 ms) | 0.51 |
| Gong → Kick2 | 1.55× | 0.38 |
| Gong → GtrStr | 1.23× | 0.38 |
| GtrStr → Gong | 1.15× | 0.95 |

Three compounding causes, all the pass-26/27 shape — *state read where it is not
valid*:
- `processBlock` read `kPresetEngine[m_preset_idx]` **live, every block**, so a
  voice struck under one engine was rendered by another engine's per-sample code.
- `LoadPreset` retired ringing voices **only** on the Timpani/Taiko branch.
- The `ENGINE_CYMBAL` branch never calls `process_exciter`, so a cymbal voice sits
  at `current_frame == 0` holding an **unstarted** envelope for its whole ring
  (verified directly).  Switching to a legacy-path preset therefore fired a
  complete, never-played attack — mallet impulse and noise burst — at the ringing
  voice's velocity.  That was the 9× burst.

Fix: `VoiceState::engine` latches the engine at NoteOn and `processBlock` routes
on it; a real preset change arms a ~10 ms exponential fade
(`kPresetFadeTauSec` = 1.5 ms, 6.9 τ to −60 dB) so the orphan retires click-free
**under its own engine**; `LoadPreset` skips its per-voice state writes for a
still-sounding voice (they would retune it mid-fade and `init_modal_modes` would
re-strike its modal bank at full amplitude); and the cymbal soft-headroom stage
is keyed on a cymbal voice having actually rendered rather than on the live
preset index.  `fade_mul` is exactly 1.0f in normal play and the fade branch is
skipped, so **40/40 renders stay byte-identical**.

**Two dead ends worth not repeating.**  (a) It is *not* `LoadPreset`'s
`init_modal_modes` call — skipping that alone leaves the burst intact (tested).
(b) Snapshotting and restoring the fading voice's resonator coefficients across
the setParameter storm changes nothing measurable (tested, then removed): in the
KS branch `lowpass_coeff`/`ap_coeff` are rewritten from `transient_*_base_*`
every sample anyway.  The real second-order problem was **master Gain**: it is a
master parameter, so the incoming preset's drive hit the outgoing tail —
GtrStr (Gain 0, drive 1.0) → Gong (Gain 20, drive 5.0) drove a fading string into
the soft clip at `x/(1+|x|)` = 0.83 against the 0.52 tail it replaced.
Pre-scaling the voice by the drive ratio **cannot** fix that: the ±0.99 clamp
sits between the voice and the drive, so attenuating the voice merely moves it
out from under the clamp and it ends up louder still (measured 1.39×).  The drive
is now **deferred** (`m_pending_drive`) until the last fade retires, which keeps
the outgoing chain bit-for-bit unchanged; an explicit Gain turn overrides it.

Not covered: switches **into** Timpani/Taiko still retire legacy voices outright
(the kernel path bypasses the voice loop entirely, so a fading voice would never
be rendered and would hold its slot forever), and switching **away** from a
kernel preset still hard-stops the kernel.  Both are unchanged from before.

Verified: 40/40 byte-identical, test_dsp exit 0, test_hw_debug **92/92** (new
**T38**; both halves fail against the pre-fix build at 9.0× and 0.67 residual),
host syntax check clean, `stability_sweep` 4096 combos + 480 rolls, worst |peak|
0.9900, 0 problems.  **ARM `.text` still unverified.**

### Code-review findings (July 2026) — 1, 2, 3, 5, 6 fixed; 4 still open

A read of every file that ships to the device (`unit.cc`, `synth_engine.h`,
`dsp_core.h`, `modal_drum_kernel.h`, `envelope.h`, `filter.h`, `noise.h`,
`tables.h`, `header.c`).  Ordered by severity.  **1, 2, 3, 5 and 6 are now
fixed** (passes 28-29); **4 is still open** and needs the ARM toolchain.

**1. Changing the Program knob while a voice is ringing re-excites it.**
**FIXED — see Pass 28 below.**

**2. `LoadPreset` stores the index before bounds-checking it.**
`m_preset_idx = idx;` runs at the top; `if (idx >= k_NumPrograms) return;` is
~65 lines later.  An out-of-range index is therefore *retained*, and
`kPresetEngine[m_preset_idx]`, `modal_preset_configs[idx]` and
`model_param_presets[m_preset_idx]` (all sized 40) are then read out of bounds on
the next NoteOn.  `header.c` caps Program at 39 so a well-behaved OS cannot reach
it — but the guard exists precisely for a misbehaving one, and it does not work.
**FIXED (pass 29)**: the guard is now the first statement in `LoadPreset`, so an
out-of-range index is rejected before anything is written.

**3. `partial_counts[value]` can be indexed negatively.**  In
`setParameter(k_paramPartls)` only `value < 5` is checked, not `value >= 0`.
`header.c` min is 0, so again not OS-reachable.
**FIXED (pass 29)**: `if (value < 0) break;` ahead of the lookup.

**4. The `.rodata` budget rule and the shipped code disagree by ~35 KB.**
**STILL OPEN.**
This file states the firmware limit is ≈30 KB for text + `.rodata` and that
`static const T arr[] = {…}` is a "broken pattern to avoid".  The code uses
exactly that pattern for, at minimum:

| symbol | bytes |
|---|---|
| `kTimpaniTransient48[3360]` | 13,440 |
| `kTaikoTransient48[3360]` | 13,440 |
| `kTimpaniModes[280]` | 3,360 |
| `LoadPreset::presets[40][24]` | 3,840 |
| `kTaikoModes[71]` | 852 |

≈**34.9 KB of `.rodata` before a single instruction** (sizes are float/int32
arrays, so architecture-independent; measured with `size -A` on a host `-O2`
build of `unit.cc`).  Since the unit demonstrably loads on hardware, the
**documented budget is probably wrong or stale** — but it is the rule that drove
the non-static-class-member workaround for the preset tables, so it needs to be
corrected or explained before it misleads another pass.  Cannot be settled
without the ARM toolchain.

**5. `noise.h` has no include guard** — **FIXED (pass 29)**, `#pragma once`. (every other header does; `float_math.h`
uses `#ifndef __float_math_h`).  Latent only — it is included exactly once, from
`dsp_core.h`.

**6. `header.c` comment is stale** — **FIXED (pass 29)**.: the Poly parameter still says "Cymbals stay
internally capped at 2 for the CPU budget", which pass 26 removed.

Reviewed and found **correct**, for the record: the TPT SVF core matches
Zavalishin ch. 4 exactly; `process_waveguide` masks both interpolation indices
and clamps `delay_length`; the cymbal resonator-budget clamp can drive `rcount`
negative but the `< 32` floor restores it before `sqrtf(count)` divides, so
`resonatorNorm` is never inf/NaN; `ModalDrumKernel::RebuildBase` clamps the pole
radius to 0.99999 and guards `decay_mult`; the kernel's mode-stretch `log2f` is
guarded against both the 0×−inf and the log(0) cases; `getParameterStrValue`
bounds-checks every table index.  One accepted soft race: `FastSVF::set_coeffs`
writes `a1/a2/a3/q` from the UI thread while `process()` reads them on the audio
thread — a torn update mixes two valid coefficient sets for one sample, and TPT
is unconditionally stable for any `g > 0`, so it cannot diverge.

### Pass 27 — Review pass: Clap/Shaker/HHat-C were silent on hardware

Not an HW report — found by **reviewing the whole codebase for the defect class
behind the pass-26 gong bug**: *a decision reads a quantity that is not yet valid
at read time.*  The gong stole the voice with the smallest `magEnv` while
`magEnv` was still 0 from the attack.  The same shape, one severity higher:

**`FastEnvelope::release()` was applied to envelopes still sitting at
`value == 0` in `ENV_ATTACK`**, which sends them to `ENV_IDLE` on the very first
`process()` (`value += (0-value)*rate` stays 0 → trips the `<= 0.001f` cutoff).
Since the Drumlogue fires `gate_on`+`gate_off` in one tick, **the three
`ENGINE_NOISE` presets — Clap (21), Shaker (22), HHat-C (26) — produced a ~20 ms
blip and nothing else on device.**  Measured same-tick RMS per 25 ms:
`0.018` then all zeros, against `0.256 / 0.430 / 0.281` with the gate held.
Every host tool held the gate 20-50 ms, so all 40 renders, `param_audit`,
`stability_sweep` and 26 HW passes reported these presets healthy.

Fixed **at the mechanism**, not the site: `release()` during the attack defers
into the new `ENV_ATTACK_REL` state, the attack completes, then the release runs
from the top of it.  An already-open envelope is bit-for-bit untouched, so all
**40/40 renders stay byte-identical** — and unlike the pass-19 "skip the
release" patch, `Rel` still shapes the tail.  Restored same-tick RMS:
Clap `0.359 0.166 0.059 0.024`, Shaker `0.430 0.213 0.158`, HHat-C
`0.357 0.054 0.006`.  The pass-19 `ENGINE_SNARE` skip is left in place as the
voicing choice it now is.

**Process lesson — this bug had been fixed twice before, per-site** (`master_env`
in T20, `ENGINE_SNARE` in pass 19), each time as a local workaround, so the third
site was simply never covered.  Both earlier fixes are still described in the
code as same-tick workarounds; **when a gotcha needs a second per-site
workaround, fix the mechanism.**  And note what no existing tool could catch:
`render_presets`/`param_audit` hold the gate, and `stability_sweep` only checks
peaks and NaN, so a voice that silences *itself* passes everything.  **T37** now
asserts the same-tick path directly (both halves fail against the pre-fix build);
`samegate_probe.cpp` prints same-tick vs held RMS side by side for any preset.

Verified: 40/40 renders byte-identical, test_dsp exit 0, test_hw_debug **90/90**,
host syntax check clean, `stability_sweep` 4096 combos + 480 rolls, worst |peak|
0.9900, 0 problems — unchanged from pass 26.  `.bss` unchanged (the new state is
an extra enumerator, no new fields).  **ARM `.text` still NOT verified** — no
cross-compiler in this session; the change is a handful of instructions in an
inlined switch.

### Pass 26 — Gong/cymbal stacking + "subtle → live" knob depth pass

Two HW notes.

**"Multiple gong hits are not stacking correctly."**  Two compounding bugs in
the `ENGINE_CYMBAL` voice policy:
- The family was hard-capped at **2 voices** (`m_cym_poly = min(Poly, 2)`), a CPU
  guard predating the magnitude squelch — on the one instrument whose hits are
  meant to pile up.
- The steal rule **inverted itself**: it took the smallest `magEnv`, which is a
  ~10 ms average of `|out|` starting at 0, while the gong's driver needs
  `lowAttackSec` = 0.25 s to open.  For the first third of a second the NEWEST
  hit is the quietest voice in the bank, so the third strike killed the second
  one mid-bloom and repeats ping-ponged between two slots.

Fix: `m_cym_poly = m_poly` (full 1-4); `kCymStealProtectSec` = 0.60 s protects
young voices and falls back to stealing the **oldest** (age is the reliable
ordering inside that window — `magEnv` on a dense inharmonic wash is not);
`kCymResonatorBudget` = 240 caps the TOTAL bank across simultaneous cymbal
voices, which is a stronger CPU guarantee than a voice count and is what makes 4
voices affordable.  A restruck/stolen voice also keeps its ring (`retainRing`,
×0.75) instead of zeroing `resY1/resY2` — metal does not reset.
Measured: Gong 4 hits → distinct voices 2 → **4**, passage RMS 1.62× → 1.84×
(150 ms) and 1.90× → 2.02× (1 s); 8 hits at 150 ms reach 2.24×; peak stays
0.83 (limiter-bounded); worst aggregate bank 208 resonators at `Rsntrs` = 60.
New **T36** in `test_hw_debug.cpp` asserts all three properties — T36a fails
against the pre-fix code.

**"Increase the effect of the parameters from subtle to live, without breaking
the presets."**  Every reference-anchored mapping had its coefficient widened
and clamps opened, anchoring untouched → **40/40 byte-identical**.  Two dead
knobs wired: cymbal `MlltRes` → ring presence, cymbal `MlltStif` → beater
hardness (both audited `NO EFFECT`); kick `MlltStif` gained a live lower half
(down = slower boom onset = felt beater; it was clamped to 0 from below and
Kick2/KickDrum ship it at 0.60/0.70).  Measured: **43** knob/family swings
widened, 2 dead → live, 1 narrowed 5 % (noise); verdict changes **5 improved, 0
regressed**.  Cymbal `Inharm` deliberately left at 2.0 — widening it *shrank*
the measured HF swing (the jitter piles modes onto the `fLo`/Nyquist clamps).

**Tooling lesson (`param_audit.cpp`).**  Beyond the documented 2-s-RMS transient
blind spot, the audit had a worse one: `FastNoise::seed` is free-running and no
reset touches it, so consecutive renders start from a different noise state.
Clap/Shaker/hats drifted a few percent between runs and flipped their own
verdicts — this pass first "found" a Partls regression on Clap for a knob that
provably does nothing there (`is_modal_engine` is false for `ENGINE_NOISE`).
The audit now pins the seed per render.  Also: on the kick, use a **band**
metric, not RMS — the master chain is loudness-maximised, so a thump reshapes
the attack spectrum without raising level (`TubRad` reads 29 % → 48 % on a
100-400 Hz vs sub band metric and almost nothing on plain RMS).

Verified: 40/40 renders byte-identical, test_dsp exit 0, test_hw_debug **88/88**,
host syntax check clean, `stability_sweep` **4096** combos across 16 presets +
**480** rolls across 40 presets with 0 problems and worst |peak| 0.9900 (the
brickwall limit — bounded, and identical to the pre-change baseline despite the
much wider decay clamps).  **ARM `.text` is NOT verified** — no cross-compiler
in the session that made this pass; confirm against the 28 KB budget on the next
flash, since the pass adds code (cymbal MlltRes/MlltStif blocks, the steal
ranking and the resonator budget).

### Pass 25 — Roll fusion (pass-23 regression) + asymmetric snare TubRad

Two HW notes, both regressions from pass 23/24:

- **"Pressed rolls feel less smooth, Djambe especially is muddy."**  Pass 23's
  unconditional stacking gave every stroke of a 15-25/s pressed roll its own
  voice, so up to `Poly` complete drum bodies (each with its own crack/slap/
  beater burst) piled up — and it silently disabled the pass-19 snare buzz-roll
  wire continuity, which can only fire when the reused slot is the one still
  rattling.  `NoteOn` now **fuses** repeats before the round-robin advances:
  same note + drum family (MEMBRANE/SNARE/NOISE) + gap < `kRollFuseSec` (80 ms)
  → reuse the last voice; anything wider stacks; different note stacks;
  ENGINE_CYMBAL/BAR/PLATE always stack (HW: "important for cymbals").
  `kRollFuseSec` is **one constant** shared with the wire-restore test so the two
  can't drift.  Measured: Djambe 45 ms roll 4 voices → 1, passage RMS
  0.444 → 0.408; AcTom 0.535 → 0.421; ≥85 ms bit-identical to unfused.
- **"TubRad body-depth sounds a bit toy-ish at lower values."**  The pass-23
  mapping was symmetric, so on a preset shipping TubRad=20 the knob floor pushed
  Band A ×2.46 (+1.3 oct) — a toy snare, not a shallow one.  Deepening keeps the
  full −1.3 oct; **thinning now runs at half rate and caps at ×1.15**.  Δ=0 takes
  the deepening branch (`knob_exp2(0)` == 1.0 exactly) → still byte-identical.

**Test-design lesson (T18).**  Adding roll fusion broke T18b, and the failure was
*correct*: T18 hard-coded `voices[2]` as "the slot the second NoteOn takes".
Worse, T18a had been passing **spuriously** — it probed an untouched voice reading
0.0 and asserted `0 < nominal`.  Tests must read `state.next_voice_idx` rather
than assume an allocation order.  New **T35** asserts the fusion policy itself
(fused / stacked / sustained-excluded) so it can't be rediscovered on hardware a
third time, and `stability_sweep.cpp` gained a **phase 2 roll stress** (every
preset × 3 gaps × same/alternating notes × shipped/extreme knobs) because phase 1
leaves 3 s between hits and never exercised any roll path.

Verified: **40/40 renders byte-identical**, test_dsp exit 0, test_hw_debug
**85/85**, snare TubRad still audits `ok`.

### Pass 24 — Poly made real, VlMll* live everywhere, thump-only kick, knob_exp2

HW batch on Kick2/AcSnare. All five items landed:

- **Poly knob was a lie**: it read 1-2, was cymbal-only (`m_cym_poly`) and always
  displayed "2".  Now a **global** cap (header 1-4, default 4, `type_strings`):
  new `m_poly` bounds the NoteOn round-robin (`next_voice_idx % cap`), the
  cymbal family clamps to `min(Poly,2)` for CPU, and `getParameterStrValue`
  prints the **effective** polyphony for the current preset — `4(2)` on a
  cymbal/kernel, `4(1)` on a string.  Verified: Kick2 Poly=2 → 2 simultaneous
  voices, Poly=4 → 4.  ENGINE_KS keeps the full-range increment (GateOff pins
  it to one slot; capping there would move that slot).
- **`VlMllRes`/`VlMllStf` wired on Kick + Tom, `VlMllStf` on Cymbal.**  They had
  only ever driven the shared mallet exciter + noise attack, which the kick boom
  and tom body mask.  Kick: VlMllRes → velocity-weighted thump prominence
  (standalone — no MlltRes needed), VlMllStf → its own knock + thump pitch/decay
  snap.  Tom: VlMllRes → slap prominence (scales a shipped `trans_*` layer, or
  raises a hand-slap burst on presets without one), VlMllStf → slap sharpness.
  Cymbal: VlMllStf → stick stiffness (`lowAttackSec`/`thwackSec`/`hfTilt`).
  All anchored on the shipped values (0 everywhere) → **40/40 byte-identical**.
- **Thump-only kick** (HW: "high thump, almost no boom"): the `Mterl` → boom
  weight curve now reaches **zero** at the knob floor (quadratic in `kmn/ref`
  below the anchor, continuous ×1 at it; unchanged `2^(1.2Δ)` above).  Measured
  on 808Sub: shipped total RMS 0.215 / tail 0.084 → `Mterl=-10` 0.010 / 0.000 →
  `+MlltRes=1000` attack 0.192 / tail 0.000.
- **`knob_exp2` (fasterpow2f) for knob curves** — see the new gotcha below.
- **README matrix corrected** (3 wrong cells) + a maintenance rule.

**Doc-accuracy incident (prevention).**  The Pass-23 "which knobs are live"
matrix was written by hand from memory and marked `VlMllRes`/`VlMllStf` inert on
the Timpani/Taiko kernel and `VlMllRes` inert on cymbals — all three have ALWAYS
been live (`RefreshKernelMods` reads both; the cymbal `stickLevel` block reads
VlMllRes).  A wrong "inert by design" label is worse than none: it disguises a
regression as an intention.  **Rule: re-derive that table from `param_audit.cpp`
+ the code, never from memory**, and remember the audit's blind spot — its 2 s
RMS metric dilutes a 40 ms transient ~50×, so a knob adding an attack can audit
`weak` while being clearly audible (confirm with a windowed probe).
(Separately: the HW report read the README's kick-thump section, which documents
`MlltRes`/`MlltStif`, against the matrix rows for `VlMllRes`/`VlMllStf` — four
different knobs.  README now has an explicit disambiguation table.)

Verified: 40/40 byte-identical, test_dsp exit 0, test_hw_debug 82/82, stability
**4096** combos (7 body knobs + a combined VlMllRes/VlMllStf extreme) across 16
presets, 0 problems, worst |peak| 0.9900 = the brickwall limit (bounded).

### Pass 23 — Inharm step-10 + membrane/snare/noise polyphony (HW feedback)

HW test on Kick2 + AcSnare exemplars produced a batch of feedback.  Two items
were unambiguous and landed this pass:

- **`Inharm` encoder step 1 → 10.**  Adopted the `Dkay`/`Resnc` ÷10 pattern:
  header range 0-1999 → **0-199** (`type_strings`, display ×10), all Inharm
  consumers `×0.0005 → ×0.005`, guard `value<=1999 → <=199`, and the preset
  `InHm` column rescaled ÷10 (round-half-up) by a one-shot script.  **39/40
  presets byte-identical**; only `Koto` (sole KS preset with non-zero shipped
  Inharm, 1→0) shifts its `ap_coeff` 0.0005→0 = **−59 dB, inaudible** (render
  diff confirmed).  T27a updated (max Inharm 199 → ap_coeff 0.995).
- **MEMBRANE/SNARE/NOISE polyphony.**  `GateOff` stacking predicate widened
  from `PLATE||BAR||CYMBAL` to **`e != ENGINE_KS`** — kick/tom/conga/bongo/
  snare/clap now round-robin the 4 voices (fast repeats stack instead of
  choking one voice).  Guards kept: cymbal capped by `m_cym_poly` (Poly knob),
  Timpani/Taiko on the 2-kettle kernel path, KS mono (string-beating).  Poly
  stress test: Kick2/808Sub/AcSnare/Conga/Bongo reach 4 simultaneous voices,
  worst peak 0.84, no NaN.  **NOTE for HW:** this supersedes the mono-voice
  buzz-roll continuity (pass 19 r2) for snares — fast pressed rolls now spread
  across voices (each with its own crack) rather than merging into one voice;
  confirm the roll feel on HW.

Follow-up items from the same HW batch (user picked, second commit):

- **Snare TubRad → body depth** (only-TubRad, per user).  New reference-anchored
  mapping in the snare NoteOn block: `sn_body = clamp(exp2(-1.3·Δtubrad))`
  applied to **Band A only** (the low body band, ~2.8 kHz on AcSnare), so
  MlltStif (all bands = brightness) and TubRad (Band A = body depth) stay
  independent.  Shipped snares byte-identical; AcSnare hi-energy fraction sweeps
  0.55 (tight) → 0.44 (shipped) → 0.30 (deep) across TubRad 0→20.  Dkay left
  subtle on the snare (user chose not to overlap Rel).
- **Kick poly kept at 4** (user), **VlMllRes zap kept aggressive** (user) — no
  code change.
- **"X-for-unwired" → documented instead of on-device** (user).  Added the
  per-preset knob-activity matrix to the README, and **corrected** the Pass-22
  mislabel: `Resnc` is the **master-LP Q** (needs `Cutoff` down), not a
  "noise-driver Q"; `Rsntrs`/`Poly` are cymbal-only performance controls;
  Model/Partls are inert wherever the engine bypasses the shared modal bank.
- **Velocity/hit-hardness**: answered (MIDI note-on velocity, set per-step on
  the Drumlogue sequencer; no dedicated knob — 24/24 param slots used).
  **Superseded in pass 31**: a slot was freed by moving the cymbal resonator
  density onto `Partls`, and slot 3 is now a global `Velocity` knob.

### Pass 22 — All-family body-knob wiring (branch brachetti-review-rename)

`param_audit.cpp` (extended to a CYMBAL + MEMB-KICK exemplar) confirmed the
body-shaping knobs `Dkay/Mterl/HitPos/Rel/Inharm/TubRad/Resnc` were dead on
**two whole families**, because those engines don't use the shared modal bank
the knobs feed:
- **Kick** (Kick2/808Sub/KickDrum): 808Sub/KickDrum use the empty default
  modal config (`mode_count 0`) so the shared modal routing is skipped
  entirely; the audible voice is the boom oscillator.
- **Cymbal** (Cymbal/Gong/HHatOpen/Ride/RideBell/Splash): the dense-resonator
  port (`cymbal_note_on`) bypasses the modal bank.
- **Timpani/Taiko**: the separate dense **drum-kernel** (`m_drum_kernel`)
  bypasses the legacy voice loop; its knobs route through `RefreshKernelMods`.

Each family now maps the dead knobs to a natural property of its own engine,
all reference-anchored → **40 shipped presets byte-identical** (verified by
render diff):
- **Kick**: Dkay+Rel → boom decay length; Mterl → boom weight; TubRad → boom
  base tune (new `VoiceState::boom_tune`, applied in the per-sample sweep
  formulas since KickDrum/808Sub recompute `boom_inc`); Inharm → pitch-dive
  depth (808Sub); HitPos → beater click (borrows the `trans_*` burst).
- **Cymbal** (into `CymbalConfig` before `cymbal_note_on`): Dkay → ring decay,
  Rel → sizzle tail, Mterl → hfTilt+maxHz brightness, HitPos → edge/bell
  (stick vs wash), Inharm → jitterSemis spread, TubRad → size (folds into
  `pitch_ratio`), Resnc already lived (noise-driver Q).
- **Kernel drums**: added TubRad → `decay_mult`↑ + `hf_decay_tilt`↓ (bigger =
  longer+darker); widened Mterl 1.2→2.0.
- **Legacy membrane/bar/plate**: widened the existing modal TubRad from t1-only
  to the whole body (t1..t4), and extended Mterl's onset tilt to mode 2.

Verified with a pitch/tail probe (`verify_blind.cpp`, the 2s-RMS audit is blind
to pitch shifts and >2s decays): kick TubRad drops boom 81→53 Hz; Timpani
kernel ring 6.7→52 (×1e3 tail); Conga/AcTom rings lengthen; cymbal Dkay/Rel
lengthen the tail and TubRad shifts the HF fraction 0.49→0.23.  Stability:
extreme 7-knob corner combos across all affected presets NaN/blow-up free;
test_dsp PASS, 82/82 test_hw_debug PASS, 0 NaN/silence / 40 renders.
`Resnc`/`TubRad` remain inert-by-design where the engine has no matching
mechanism (Resnc needs a noise bed; TubRad on a plain kick = tune only).

### Pass 21 — Kick "thump" + snare-sweep corruption check (branch brachetti-review-rename)

- **Snare param sweeps verified non-corrupting**: swept each rewired snare
  knob lo/mid/hi across all 4 snares — Rel t40 spans ~100→850 ms, MlltRes /
  VlMllStf ~2× the buzz energy, VlMllRes adds onset snap; peaks bounded well
  under the 0.99 limiter, no NaN/silence, all moves smooth and musical.
- **Kick family**: the audit confirmed the mallet knobs (MlltStif/VlMllRes/
  VlMllStf/Mterl/Inharm/HitPos) were NO EFFECT/weak — the kick is boom +
  (quiet) modal body, and the mallet click is a tiny high tick, not a mid
  punch. Added a dedicated `VoiceState::thump_*` layer: a fast pitch-dropping
  mid sine (~300→115 Hz, T60 ≈ 58 ms) over the boom, wired to `MlltRes`
  (amount) + `MlltStif` (snap/pitch), reference-anchored → shipped kicks
  bit-identical, knob-up adds real punch (added-signal RMS 0.10-0.19 over the
  first 100 ms; the soft-clip means it reshapes attack timbre, not peak).
  Stable across 48 extreme kick knob combos.

### Pass 20 — Snare param-design layer + Resnc granularity (branch brachetti-review-rename)

The snare family had ~13 UI knobs that were inaudible (mallet masked by noise;
VlMllRes' attack target overridden; Rel dead because ENGINE_SNARE skips the
noise-env release). Empirical `param_audit` (snare variant) + code trace
confirmed it. Fix: a **reference-anchored snare param-design layer** in
`NoteOn` that wires the dead/weak knobs to the wire buzz (the snare's defining
voice), each a delta from the shipped value so shipped presets stay
bit-identical:
- `Rel` → buzz **tail length**, `MlltRes` → buzz **amount**, `MlltStif` →
  buzz **brightness**, `VlMllStf` → buzz **tightness/Q**, `VlMllRes` →
  **crack/snap** (new `ExciterState::snare_crack_gain`, scales the onset crack
  burst). See the README "Snare param-design layer" table.
- Verified: all 40 shipped renders byte-identical; the five knobs now swing
  strongly (audit) and 256 extreme-value combos are NaN/blow-up free (wire
  pole radius clamped < 0.995).
- **Resnc** encoder granularity coarsened from step 1 (707–4000, 3293 steps)
  to step 10 (stored ÷10, 71–400; display shows 710–4000 like MlltStif/Dkay).
  The Q map is `0.707 + (v−71)·0.01` so the shipped default (71) hits Q 0.707
  exactly → bit-identical.

### Pass 19 — Snare family revival + BrshSnr preset + CPU pass (branch snare-drum-realism-optimization)

Structural snare fixes (all measured; details in `REALISM_REVIEW.md`):
- **Wire path was dead on every live hit**: `NoteOn → PartialReset()` zeroes
  `snare_wire_mix` (only `LoadPreset` ever set it), and the velocity-tuned wire
  band coefficients were written BEFORE `PartialReset` and clobbered.  Wire
  params are now restored per-NoteOn and the band setup runs after the reset.
  Restore is gated to ENGINE_SNARE so presets HW-approved with the wire silent
  (e.g. KickDrum's dormant 0.03 mix) are unchanged.
- **Same-tick gate-off choked the buzz to ~26 ms**: ENGINE_SNARE now skips the
  noise-env release; the tail decays at the natural NzRs rate (AcSnare NzRs
  740→950, MrchSnr 800→940 — calibrated to ref t40s).  Renders and hardware now
  behave identically for snares.
- **Velocity → buzz physics**: soft hits get less wire mix + up to 2× faster
  decay, anchored at full velocity (calibrated sound unchanged at vel=127).
- Result: MrchSnr centroid 4705 Hz vs ref 4683, t40 230 vs 220 ms; AcSnare
  t40 270 vs 280 ms.
- **New preset 38 "BrshSnr"** (ENGINE_SNARE): brush sweep — ~30 ms swish
  onset, 6.5 Hz enveloped swirl AM (noise_am machinery), diffuse low-Q wire
  bands 2.2/3.6/6.3 kHz, BP noise ≈4.2 kHz, long tail.  Awaiting HW listen.
- **CPU**: idle render cost cut ~99% (idle guard skips master chain when no
  voice is active, after a 320 ms flush); Tone=0 tilt-EQ skip; several
  per-sample invariants hoisted.  All 39 renders byte-identical.
- Also fixed: HEAD did not compile (orphaned `pre_clip_trim` from the cymbal
  port merge).

**Round 2 (HW feedback on pass 19):**
- BrshSnr "too fast, hit too hard" → velocity compressed (0.30-0.72), swish
  onset ~100 ms, wires resting on head (`k_wire_onset_env=1` → no crack
  burst), 4.2 Hz swirl, tail T60≈0.64 s.  HW may supply a brush reference
  sample for calibration.
- **RimShot approved & added** (preset 39, ENGINE_SNARE): hard stick, rim-ring
  mode cluster (2.42/3.38 rings longer than the head), tight 70 ms buzz.
- **Buzz-roll continuity approved & added**: snare retriggers < 80 ms restore
  the wire resonator states across PartialReset and skip the crack burst.
- **All 7 per-family recommendations approved & applied** — measured results in
  `REALISM_REVIEW.md` "Round-2 results".  Headline: Cymbal/Ride/RidBel body
  centroid fixed via `CymbalConfig.hfTilt` HF-weighted resGain (Cymbal
  927→6932 Hz vs ref 7366; Ride 6121 vs 6087).  Kalimba/Cowbell/Conga/AcTom/
  Triangle/Clap table retunes.  Stereo idea discarded per HW.

### Pass 18 — FDN dense wash (cymbals) + strike transient layer (Timpani/Taiko)

HW after pass 17: "sound improved, but still the very same problems as before."
Diagnosis: pass 16's coupling matched the *temporal* band-envelope (corr +0.99) but
not spectral **density** — ~8 resonators produce ~8 tones, so the cymbal wash still
read as "tones + a separate noise bed". And for Timpani/Taiko the modal *tail* is
now correct; the remaining gap is the broadband **attack** the modal bank cannot make.
Two structural additions (both pure DSP, no samples, no `.text` table growth):

**Cymbals — Feedback Delay Network (FDN) dense wash**
- 4-line Hadamard FDN hosted in the **KS-dead `resB.buffer`** (zero new RAM; resB is
  unused on `ENGINE_PLATE`). 4 mutually-prime delay lengths (281/359/419/487) in
  4×512-sample partitions → hundreds of dense inharmonic modes the resonator bank
  can't make. Lossless orthonormal Hadamard × sub-unity gain ⇒ guaranteed stable.
  Per-line one-pole HF damping. Driven by the same nonlinear crash `exc`, so it blooms
  with the strike and feeds the bloom self-PM too.
- Per-preset params in NoteOn crash block (`fdn_g/damp/drive/mix`); `resB.buffer`
  cleared on each strike. Bright crashes long+light; open hat short; Gong/RidBel light
  mix so their pitched character stays foreground.
- **Result (measured, body spectral flatness)**: Cymbal 0.2→**0.70**, Ride →**0.71**,
  HHat-O →**0.58**, RidBel →**0.62** (ref ~0.55); Gong kept tonal 0.10 by design.
  The wash now has real broadband density — the documented floor is **lifted**.

**Timpani/Taiko — strike transient layer**
- Short velocity-scaled band-passed white-noise burst (difference-of-one-poles BP)
  layered over the modal body = the stick-slap / mallet-contact attack. State on
  `VoiceState` (`trans_env/decay/gain/a_lo/a_hi/lp_*`); fires in NoteOn, runs per-sample
  before the boom block.
- Taiko: ~2-6 kHz, T60≈18 ms, gain 2.9 → early centroid **539→1806 Hz** (ref 3374;
  remaining gap is the ref's close-mic stick, deliberately not chased to avoid hiss).
- Timpani: ~0.6-2.6 kHz, T60≈12 ms, felt-soft (centroid barely shifts because the ~82 Hz
  fundamental dominates the energy metric — by design; a clicky attack would be wrong).

Tests: 82/82 PASS (incl. T27 all-37 in-bounds, T28 voice-cycle no-NaN, T21a heavy-strike).
**ARM `.text` ≤ 28 KB must be confirmed on next flash** (added code is small; verify).

### Pass 17 — Data-driven modal tuning from reference samples (commit 081e82e)

Applied `modal_extract.py` to fix two standing HW complaints:

**Timpani (HW: "explosion + rough ripple, not clean bright ringing")**
- **Measured** `Orchestral-Timpani-C.wav` → series `1 : 1.495 : 1.980 : 2.601 : 3.414 : 4.010`
  (air-loaded Rossing family). Measured upper-mode amps: 0.14/0.08/0.05 (quiet).
- **Root cause found by measurement**: old top pair (3.02/3.55) sat only ~0.5 ratio apart
  AND was held loud (env 0.45/0.32) → beat against mode 4 = the "rough ripple".
- **Fix**: ratios snapped to measured (3.41/4.01 — wider, quieter cluster); upper modes
  tamed to near-measured levels (env4-6 = 0.30/0.18/0.12); bright pitched modes 2/3
  extended to 1.8/1.5 s for a clean sustained ring.
- **Result**: T60 2.19 s (ref 2.37), early centroid 668 Hz (ref 811), late centroid 295 Hz
  (was ~140 Hz dark hum).

**Taiko (HW: "still TUNNN not TAAAN")**
- **Measured** `Taiko-Hit.wav` → inharmonic series `1 : 1.377 : 1.746 : 2.100 : 2.423 : 2.754`
  (open wooden-stave shell — NOT Bessel) PLUS a strong bright partial at **ratio 16.86 ≈ 1472 Hz**
  (the open "AAN" vowel; matches enum comment "~1582 Hz").
- **Root cause found by measurement**: the bright 1472 Hz partial was **not being synthesized**
  at all (old config stopped at ratio 2.756, 4 modes) and low fundamental dominated.
- **Fix**: 6 modes on measured inharmonic ratios; dominance shifted off 87 Hz fundamental onto
  212 Hz mid (env4=1.00); mode 6 = 1472 Hz partial (env 0.60), sustained ~600 ms;
  attack click brightened (NzMx 26→36, NzFq 280→360 ≈ 3.6 kHz).
- **Result**: early centroid 421→539 Hz (ref 1856 Hz — remaining gap is close-mic stick
  transient in the reference; pushing noise click much harder risks hiss over the wood "tak").

**NOT adopted from the paper**: offline RDFT/ESPRIT pipeline and the alternate NEON
exp-decay phasor engine (full rewrite, .text-budget risk, no benefit over existing modal ringers).

---

### Pass 16 — Nonlinear modal→wash energy cascade (commit a296a0d)

**Problem (recurring, across 4 passes)**: "noise and ring not modulating each other — just
mixed together separately" (Cymbal, Gong, Ride, RidBel).

**Root cause (structural)**: crash resonator bank was driven by an **independent noise
generator**; could only ever sound juxtaposed regardless of level tuning.

**Fix — von Kármán nonlinear cascade** (Chaigne/Touzé plate theory, cited in README):
In a real cymbal the broadband crash is high-mode energy pumped FROM the low struck modes
by geometric nonlinearity. New excitation formula in processBlock:

```cpp
const float m  = voice.modal_out_prev;
const float am = fabsf(m);
const float floor_n = 0.30f;
float exc = voice.exciter.noise_out_sample * (floor_n + (1.0f - floor_n) * am)
          + (m * am) * voice.crash_couple;
```

- First term: noise blooms and dies **with the ring** (not independently).
- Second term: ring's own energy injected into the crash wash (geometric nonlinearity).
- `crash_couple` (new VoiceState field, default 0.0) set per-preset in NoteOn.

**Per-preset coupling** (split by family):
| Preset     | crash_couple | Rationale |
|------------|-------------|-----------|
| Cymbal     | 0.30        | Light — stays bright/airy, just breathes with ring |
| HHat-O     | 0.22        | Light — approved, do not increase |
| Ride       | 0.35        | Light + Ride/RidBel crackle bed also gated by ring |
| RideBell   | 0.50        | Stronger for bell-modal blend |
| Gong       | 0.60        | Strong — metallic shimmer pulling energy into partials |

**Bed-gate** (Ride/RidBel only): raw crackle noise gated by `floor_n + (1-floor_n)*am`
so the crackle bed also blooms with the bell rather than rattling independently.

**Measured band-envelope correlations** (after fix, vs references):
Cymbal +0.99, Gong +0.73, Ride +0.84, RidBel +0.64, HHat-O +0.93
(Reference crash: +0.76, high-band peak ~470 ms after strike).

**Other changes this pass**:
- **Timpani**: removed membrane-noise bed added in pass 15 (user: "rough ripple over");
  instead brightened upper modes for clean bright ringing; attack 2→7 ms softer.
- **Taiko**: earlier version of mode-lifting brightness fix (superseded by pass 17).
- **Gong**: upper modes lifted/extended modestly for metallic shimmer without clangy attack.

---

### Pass 15 — Timpani option C: de-synthesize modal body

Mode rebalancing + membrane noise bed. The noise bed was subsequently reported as
"rough ripple" on HW → **removed in pass 16**. (Documented for history only.)

---

### Pass 14 — DATA-DRIVEN from reference samples (refcmp.py)

**KEY FINDING that reverses passes 10-13**: a real cymbal/ride/hat is **BRIGHT (~11 kHz
centroid) and NOISY (spectral flatness ~0.55), sustained 0.6-3 s** — NOT a dark tonal
wash. "Crash too predominant" = crash was DARK + FLUTTERING, not that noise shouldn't
dominate. A crash IS mostly bright smooth noise.

- `modal_engine_gain` crash factor 0.95→**0.12** (ring = faint metallic undertone).
- Noise high-passed bright: NzFltr=HP, NzFq→12-13 kHz; Cymbal BP moved 4.5→11 kHz.
- Crash bank cut to light broad colouring (drive ~0.3–0.9).
- Noise releases = **measured reference T60s** (Cymbal 2.3s, Ride 2.9s, RidBel 3.3s,
  HHat-O 0.6s) with BOTH bands slow so sizzle lasts the whole tail.
- Result: Ride 10839/11032 vs ref 11167/11075 ✓; RidBel ~11k ✓ 3.9s; HHat-O 11218 ✓.
- **`refcmp.py`** (host tool): compares a render vs reference (centroid early/late,
  flatness, T60). Run after copying renders to `/tmp/rc/` (`cp rendered/*.wav /tmp/rc/`).

---

### Pass 13 — Timpani/Taiko attack-vs-sustain; metallic ring-dominant rebalance

- Timpani/Taiko "bass guitar + audible vibration": mode 1 dominant + LONG (1.3s/1.5s);
  upper modes fast-decay (300/210/150ms Timp, 260/175/120 Taiko) — colour only attack.
  Centroid early 406Hz → late 140Hz (Timp) = bright attack, dark sustain.
- Metallic crash rebalance: `modal_engine_gain` crash factor 0.60→0.95; crash_base cut
  2-3×; crash_r broadened to ~0.965–0.985 (overlapping resonators = continuous sizzle).

---

### Pass 12 — Voice-stacking polyphony fix (cymbal rolls)

- **Bug**: `GateOff()` forced `next_voice_idx = NUM_VOICES-1`; since Drumlogue fires
  gate_on+gate_off in the same tick, every repeated hit reused voice 0 → no stacking.
- **Fix**: GateOff resets only for short/percussive engines (MEMBRANE/SNARE/NOISE) and
  KS. PLATE and BAR keep round-robin → fast hits stack/overlap across 4 voices.

---

### Pass 11 — De-regress Timpani; crash decay; full param coverage

- Timpani "bass guitar" regression: ratios slightly stretched (1.5/2.03/2.49/3.02/3.55,
  not exact 0.5 steps) + T60 cut ~850 ms → percussive.
- Cymbal "continues while held": modal T60 3000→1800 ms, cymbal noise decay ~6→1.2 s.
- Gong "still an explosion": crash_base 4→1.5, bloom 0.3→0.2.
- HHat-O / Ride "shaking ~28 Hz": `parallel_noise_gain` raised, crash_r broadened →
  continuous broadband-noise-dominant instead of sparse beating resonators.
- EVERY-KNOB-DOES-SOMETHING wiring via `param_audit.cpp`: Rel→ring-length,
  MlltRes→modal presence (crash plates: crash intensity), Partls→mode count + richness.

---

### Pass 10 — Crash rebalance; Timpani harmonic modes; Shaker swell

- Crash recipe was complete but over-pushed: crash_base ~halved; ring raised
  (`modal_engine_gain` 0.45→0.60); crash_ring_tap raised (Ride 0.15→0.40 etc.).
- Timpani: ratios 1:1.5:2:2.5:3:3.5 (dropped 1.742 mode that caused critical-band beating).
- Shaker: noise-env attack slowed to ~15 ms (was instant hit); 17 Hz rattle retained.

---

### Pass 9 — Self-PM "dynamic bloom" (cymbal density)

- Added self-Phase-Modulation: metallic bus written to KS `resA.buffer` (reused — dead
  on plate engines), read back at amplitude-modulated offset → self-FM.
- Cymbal spectral density 1138→2337 bins; HHat-O 497→4722.
- Ride/RidBel crash_r lowered ~0.9965 (broad dense wash vs razor-Q sparse tones).
- `noise_am_decay = 1.0` for Shaker: 17 Hz rattle persists the full tail. Rel 18→19.

---

### Pass 7 — Crash-resonator bank

- Added 6 constant-peak-gain 2-pole bandpass resonators per voice (PLATE only),
  tuned to the same mode frequencies as the struck modal bank (reuses `modal_k_*`),
  driven by enveloped noise: `y[n] = r·k·y1 − r²·y2 + (1−r²)·noise`.
- Two prerequisites: (1) `modal_engine_gain ×0.45` on crash presets so wash competes;
  (2) noise release overridden slow (~2.4s T60) so wash isn't cut by near-instant gate-off.
- MlltRes → crash intensity on ENGINE_PLATE (REFERENCE-ANCHORED).

---

### Pass 6 — Modal tuning precision fix

- `fastercosfullf` has ~1e-3 absolute error; near w→0 Timpani's 82/124/144/165 Hz
  landed at 86/121/139/157 Hz — compressed, ~17 Hz gaps → slow beating.
- Fix: exact `cosf`/`exp2f` in `init_modal_modes` (NoteOn-time, accuracy > speed).
- Ring-mod gate reshaped: `(1−d) + d·modal` (true bipolar mix).
- Taiko velocity split: hard → boom (×0.25..×1.75 by vel²), soft → bright mid modal mode.

---

### Passes 1-5 (foundations)

- Engine routing scaffold (KS bypass for non-string presets).
- ENGINE_BAR — Marimba exemplar (Phase 2 kill-switch).
- ENGINE_MEMBRANE — Kick, Timpani, Taiko, etc.
- ENGINE_SNARE — AcSnare, MarchSnare (BrushSnare added in pass 19).
- ENGINE_NOISE — Clap, Shaker.
- ENGINE_PLATE — Cymbal, Gong, Hi-hat, Ride, etc.
- Preset list: 37 entries (Flute/Clarinet removed; Taiko2 = "DeepBs"; Tick added).
- Master filter → LOWPASS "Cutoff" (old "LowCut" HP read reversed on HW three times).
- TPT (Zavalishin) SVF: fixed Chamberlin instability that froze cutoff above ~8.2 kHz.

---

## Host Build / Test Commands

```bash
# Syntax check (host — ARM cross-compiler required for actual flash build)
g++ -std=c++14 -fsyntax-only -I. -I../common -U__ARM_NEON__ -U__ARM_NEON \
    -Wno-strict-aliasing -Wno-unused-parameter unit.cc

# ── ARM CODE-SIZE CHECK (pass 32: this is now possible in-session) ──────────
# Every pass before this one ended with ".text unverified — no cross-compiler".
# There is one in the distro; the Makefile only fails because it hardcodes a
# Windows toolchain path.  Install it and build the real ARM object directly:
#
#   apt-get install -y --no-install-recommends g++-arm-linux-gnueabihf
#
# Flags mirror the Makefile (ARCH_OPT + USE_COPT/USE_CXXOPT + -Os).  gcc 13
# rejects the Makefile's -finline-limit=9999999999 (> INT_MAX) — 2000000000
# behaves the same.  This is NOT the vendor gcc 6.5, so .text moves a little
# between toolchains; .rodata/.data/.bss are exact.
ARCH="-march=armv7-a -mtune=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4"
OPT="-Os -pipe -ffast-math -fsigned-char -fno-stack-protector -fstrict-aliasing \
     -falign-functions=16 -fno-math-errno -fomit-frame-pointer"
CXXO="-std=gnu++14 -fno-threadsafe-statics -fno-exceptions -fno-rtti \
      -finline-limit=2000000000 --param max-inline-insns-single=2000000000"
arm-linux-gnueabihf-g++ -c $ARCH $OPT $CXXO -DRENDER_STAGE=4 -fPIC \
    -ffunction-sections -fdata-sections -w -I. -I../common unit.cc -o /tmp/unit.o
arm-linux-gnueabihf-gcc -c $ARCH $OPT -std=c11 -DRENDER_STAGE=4 -fPIC \
    -ffunction-sections -fdata-sections -w -I. -I../common header.c -o /tmp/header.o
arm-linux-gnueabihf-g++ $ARCH $OPT -shared -Wl,--gc-sections \
    /tmp/unit.o /tmp/header.o -lm -o /tmp/brachetti.elf
arm-linux-gnueabihf-size -A /tmp/brachetti.elf | grep -E '^\.(text|rodata|data|bss)'

# DSP unit tests
g++ -std=c++17 -O2 -I.. -I. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    test_dsp.cpp -o /tmp/run_test && /tmp/run_test

# Render all presets to WAV
g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common \
    -DRUNTIME_COMMON_H_ render_presets.cpp -o /tmp/render_presets
/tmp/render_presets rendered/

# Sanity check renders (NaN / silent)
python3 -c "
import numpy as np, scipy.io.wavfile as wav, glob
bad=0
for f in sorted(glob.glob('rendered/*.wav')):
    sr,x=wav.read(f); x=x.astype(np.float64)
    if np.any(~np.isfinite(x)) or np.max(np.abs(x))<1e-7:
        print('BAD:',f); bad+=1
print(f'{bad} problems / {len(glob.glob(\"rendered/*.wav\"))} presets')
"

# Modal parameter extraction from a reference sample (DAFx2020 method)
python3 modal_extract.py samples/Orchestral-Timpani-C.wav
python3 modal_extract.py samples/Taiko-Hit.wav --nmodes 10

# Compare render vs reference (centroid, flatness, T60)
# Requires copying renders first: cp rendered/*.wav /tmp/rc/
python3 refcmp.py

# Velocity-knob map per preset (rms / attack-window rms / difference-RMS dB).
# Use the dRMS column, not rms: the master stage is loudness-maximised, so a
# knob that reshapes a hit without raising its level reads as "no change" on
# any total-RMS metric (this is what hid three kick knobs in pass 30).
g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    velocity_probe.cpp -o /tmp/velocity_probe
/tmp/velocity_probe            # all 40 presets, or pass indices: 0 3 13 5

# Dead-knob-travel probe — walks each parameter across its WHOLE declared range
# and counts DISTINCT renders.  param_audit only compares the two ENDS of a
# range, so a clamp plateau in the middle is invisible to it; this is what finds
# the pass-36/37 defect class (a linear mapping into a hard clamp, so a third of
# the knob does nothing).  "below=1" means the entire downward half is dead on
# that preset — the "shipped value sits on the range floor" pathology.
# Caveat: on ranges with fewer integers than steps (Partls/Model/NzFltr) the
# collapsed-span column is a sampling artefact; DEAD/one-sided stay valid.
g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    plateau_probe.cpp -o /tmp/plateau_probe
/tmp/plateau_probe

# ENGINE_CYMBAL repeated-strike probe — voice ledger, strike-over-ring ratio
# and band split for a PASSAGE, which is the only place the pass-45 defects
# show (a single strike looks fine and cym_cpu_probe/T36 only see indices and
# microseconds).  Pass a directory to also dump the WAVs.
g++ -std=c++17 -O2 -I. -I.. -I../common -I../../common -DRUNTIME_COMMON_H_ \
    gong_probe.cpp -o /tmp/gong_probe
/tmp/gong_probe                # or: /tmp/gong_probe /tmp/wavs

# Note-assignment audit — renders each preset AT ITS OWN shipped Note through
# GateOn() (render_presets.cpp uses its own hard-coded notes and cannot see
# drift between the two).  Findings live in NOTE_AUDIT.md.
g++ -std=c++17 -O2 -I. -I.. -I../../common -I../common -DRUNTIME_COMMON_H_ \
    note_audit.cpp -o /tmp/note_audit
/tmp/note_audit /tmp/na > /tmp/na.csv && python3 note_audit.py /tmp/na.csv
```

---

## .rodata / .data Constraint — MEASURED in pass 32, and the number was wrong

**The rule below is kept because it is good hygiene, but its stated limit is
not real.**  Pass 32 cross-compiled the shipping unit for ARM for the first
time (see the build command above) and measured:

```
.text = 50,312   .rodata = 34,324   (text+rodata = 84,636)   .data = 472   .bss = 107,572
```

That is **~2.8× the "≈ 30 KB" ceiling this file has asserted since a49e2f4**,
and the unit loads and runs on hardware.  The `.rodata` figure alone (34.3 KB)
is over the claimed ceiling for `text+rodata` — and `.rodata` is *data*, so it
is the same size under any compiler: `modal_drum_data.h` puts 31 KB of
`static const` mode tables and attack transients there (two 3360-float
transients = 13.4 KB each), which by the old rule should have been unloadable
since the dense kernel shipped.  Whatever the firmware actually checks, it is
not "text+rodata ≤ 30 KB".

**What this means in practice:**
- Do **not** contort code to dodge `.rodata` any more (computing a table
  instead of tabling it, splitting arrays, keeping LTO off "for size").
  Those are now cost-free choices, not requirements.
- Do keep the `.data` pattern below — it is harmless, already in place, and if
  a real limit does exist it is the safe side of it.
- **Do re-measure instead of guessing.**  The cross-build takes ~15 s and
  reports the exact number; there is no longer any reason for a pass to end
  with ".text unverified".

**The `.data` pattern (unchanged):** The large preset arrays —
`kDefaultModalPresetConfig`, `modal_preset_configs[]`, `model_param_presets[][]`,
`kPresetEngine[]` — are declared as **non-static** class members (no `static`,
no `const`, no `constexpr`), which places their initial values in `.data`.

**Patterns that land in `.rodata`** (fine now, but know where they go):
- `static constexpr T arr[] = {...}` and `static const T arr[] = {...}`
- `static T arr[] = {...}` **inside a class body** → GCC 6.5 rejects it (real)

The one large `static const` left is `LoadPreset`'s `presets[40][24]`; pass 32
narrowed it to `int16_t` (stored range is [-10, 1999]), halving it to 1920 B.

See `config.mk` `USE_LTO := no` — kept, but its stated size justification no
longer holds; if LTO is ever wanted, measure rather than assume.

---

## Known Architectural Floors (not worth chasing)

| Issue | Cause | Decision |
|-------|-------|----------|
| ~~Cymbal spectral flatness ~0.03-0.23 vs ref ~0.55~~ | ~~6-resonator bank is fundamentally tonal~~ | **RESOLVED in pass 18** — FDN dense wash lifts flatness to ~0.6-0.7 |
| Taiko early centroid 1806 Hz vs ref 3374 Hz | Ref has prominent close-mic stick transient | Improved 3.4× via pass-18 transient layer; full ref not chased (would add hiss over the wood "tak") |
| Ride 34 Hz correlated AM | Sparse 6-resonator bank beating | Addressed via broadband noise dominance + pass-18 FDN density |

---

## Key Architectural Bugs (fixed — keep as gotcha reference)

### GOTCHA: modal mix lives in TWO tables

`ModalPresetConfig.mix` is used only by `LoadPreset`.  `NoteOn` re-inits the modal bank
using `model_param_presets[preset][k_modal_mix]`, which **overrides** it.  Keep both in sync.
Always edit the `model_param_presets` `k_modal_mix` column to change the audible mix.

### GOTCHA: Plate Ratios vs. Membrane Ratios

HiHatClosed, HiHatOpen, Cowbell = ENGINE_PLATE.  If `modal_preset_configs` uses membrane
Bessel ratios (1.000/1.594/2.136/2.296) → sounds like a wood drum.  Use plate ratios
(2.92/6.37/11.75) for PLATE presets.

### GOTCHA: Modes 5/6 inherit T60_4

`init_modal_modes` computes:
```
modal_decay_5 = powf(modal_decay_4, 1/0.85)   // T60_5 = 0.85 × T60_4
modal_decay_6 = powf(modal_decay_4, 1/0.70)   // T60_6 = 0.70 × T60_4
```
There is no independent t60_5/t60_6 field — only env5/env6 are free. Modes 5/6 always
decay faster than mode 4.

### GOTCHA: KS pitch_env T60 gotcha

When pitch_env_amt>0, KS delay starts short → injects zeros into feedback path → shortens
T60. Fix: use τ≤21ms (pitch_env_decay≥0.9990) so sweep completes in attack transient.
**NEVER use τ>50ms for KS pitch_env.**

Measured in pass 38, the first time this path had ever actually run (Koto,
amt 1.5, decay 0.99900 = exactly the τ limit): **the tail did not shorten, it
lengthened slightly** — t40 343 → 368 ms, t60 532 → 582 ms.  So the rule works;
keep it.  What the sweep *does* change is the harmonic balance — the 2nd
partial rises 0.771 → 0.970 of the fundamental, which is audible as a brighter
pluck and is why `note_audit` now picks Koto's octave as its peak.

### fasterexpf catastrophic inaccuracy

`fasterexpf` catastrophically wrong for |x| < ~0.001.  `modal_decay` uses arg ~-0.00012
at T60=1.2s → `fasterexpf` returns ~0.971 (implying T60≈5ms) instead of 0.99988.
**Always use standard `expf`/`powf` in `init_modal_modes`** (NoteOn-time, once per hit).

### fastercosfullf frequency error

~1e-3 absolute error near w→0 shifts low-frequency mode ratios (Timpani ~17 Hz gaps →
slow beating). **Always use exact `cosf` in `init_modal_modes`.**

### knob_exp2 — fast 2^x for knob curves, but GUARD THE ANCHOR

Reference-anchored knob mappings don't need exact math (±0.3 % on a response
curve is inaudible), so they use `fasterpow2f` via the `knob_exp2` helper in
`synth_engine.h`.  **The naive swap is a trap**: `fasterpow2f(0.0f) ≈ 0.9614`,
not 1.0 — and every anchored mapping evaluates at exactly Δ=0 for its shipped
preset, where the factor MUST be exactly 1.0 or the byte-identical guarantee
breaks on all 40 presets at once (silently: everything just gets ~4 % shorter
/ quieter).  `knob_exp2` returns 1.0 for x==0 and `fasterpow2f` otherwise.

```cpp
inline float knob_exp2(float x) { return (x == 0.0f) ? 1.0f : fasterpow2f(x); }
```

Use it for knob-response curves only.  **Keep exact `exp2f`** for tuning and
timing: note→frequency ratios, `cymbal pitch_ratio`, the kernel trigger ratio,
**KS delay lengths** — same reasoning as the `fasterexpf` / `fastercosfullf`
gotchas above.

**`fasterpowf(2.0f, p)` is the same trap, doubled** — it expands to
`fasterpow2f(p * fasterlog2f(2.0f))` and *both* halves are approximations:
`fasterlog2f(2.0f)` = 1.057304, and the whole expression returns **0.971348 at
p = 0**.  Pass 38 found it on the KS pitch sweep, where the swept quantity is a
delay LENGTH: the sweep converges to p = 0, so the delay was left permanently
2.9 % short and Koto settled **half a semitone sharp forever** with a perfectly
clean harmonic series.  The error is −22 to −50 cents over the useful range, so
it is wrong everywhere, not just at the anchor — `knob_exp2`'s `x == 0` guard
would NOT have saved this one.  For anything whose units are pitch, use exact
`exp2f` and accept the cycles.

### REFERENCE-ANCHOR pattern

Any param→modal mapping MUST pivot at a captured reference (`m_modal_*_ref`, set in
`LoadPreset`) so default sound is unchanged and only knob *movement* alters it.
Anchoring at an absolute endpoint silently detunes every preset.

### GOTCHA: NoteOn ordering vs PartialReset

`NoteOn` calls `v.PartialReset()` roughly mid-way through.  Any per-voice
exciter/voice state written BEFORE that call that PartialReset touches is
silently clobbered (this killed the snare wire for 18 HW passes).  Rule:
per-hit DSP state setup belongs AFTER `PartialReset()`, in the restoration
section next to the boom/onset params.

### ENGINE_SNARE lifetime

NoteOff does NOT release the snare noise envelopes.  The buzz decays at the
natural NzRs-governed ENV_DECAY rate; Rel therefore does not shape the snare
noise tail.  This is a **voicing** choice (real wires ring freely once the stick
leaves the head, and the Rel-rate release choked the buzz to ~26 ms), not the
same-tick workaround it started life as — see the same-tick gotcha below.

### ENGINE_NOISE lifetime

`sustain_level=1.0f` for NOISE engines; `NoteOff` skips `master_env.release()`.
The `noise_env` (Rel knob) fully controls Clap/Shaker tail.

### GOTCHA: `pitch_env` is cleared per-hit and never restored (808Sub is dead)

Same shape as the snare-wire bug: `PartialReset()` (called from every `NoteOn`)
zeroes `pitch_env`, `pitch_env_decay` and `pitch_env_amt`, and the NoteOn
restoration block rebuilds every `boom_*` field but not those three.  They are
written **only** by `LoadPreset`, so the first hit clears them for good.

Any sweep whose formula reads `pitch_env` therefore does not happen.  Measured
on the shipping tree: **808Sub is flat at 45 Hz** for the whole hit, against
the "160 → 45 Hz pitch sweep" its comment and this file both claim, and pass
22's Inharm → pitch-dive mapping scales a zero.  KickDrum's sweep works only
because it is written in terms of `boom_env` (`55 + 35*boom_env`), which the
restore block does rebuild.

**Fixed for 808Sub + RackTom in pass 35** (user: *"Change 808sub"*), in the
shared NoteOn restore block, and **extended to AcSnare + Koto in pass 38**
(user: *"Add pitch_env, so I can evaluate the difference"*).  The restore is
**gated**, not blanket, because **adding a preset to that gate changes its
sound** — treat it as a voicing decision.  KickDrum (amt 9) is the one row
still out, and it loses nothing: its sweep reads `boom_env`, so restoring
`pitch_env` there cannot change a sample.

**Restoring the fields is only half of it — check that a CONSUMER exists.**
Pass 38 assumed both remaining presets were a one-line gate edit.  True for
Koto (the ENGINE_KS branch already sweeps the delay line from `pitch_env`),
false for AcSnare: on a non-KS engine the render loop only *decays*
`pitch_env`, and the boom-sweep block had branches for KickDrum / 808Sub /
RackTom only, so the restore alone would have been silent.  It needed a fourth
branch.  Grep for a reader before calling dormant data live.

### GOTCHA: a stationary FFT turns a pitch GLIDE into a fake mode cluster

Percussion with a head bend (toms, 808-style kicks) will show a tight cluster
of partials in any windowed FFT — `modal_extract.py` included, since it only
looks for sustained partials.  Resynthesising that cluster as static modes
produces a detuned chord that beats, and the first beat maximum typically lands
40-80 ms in, so the drum **swells to its peak after the strike** instead of
decaying from it.  That is what pass 34's "sdeng not thump" report was.

**Always track the dominant partial over short (~30 ms) windows before
believing a cluster.**  On `rock-rack-tom-1.wav` the "cluster" at ratios
1.071/1.272/1.350 is one partial sliding 160 → 110 Hz with τ ≈ 55 ms.  A glide
belongs on the boom oscillator, not in the mode table.

### GOTCHA: a release that arrives DURING the attack (the same-tick killer)

The Drumlogue fires `gate_on` **and** `gate_off` in one scheduler tick, before
any audio block.  So every `release()` in this codebase can land on an envelope
that `trigger()` has just zeroed and `process()` has never advanced.  Naively
that is fatal — `ENV_RELEASE` computes `value += (0 - value)*release_rate`,
which is still 0, trips the `value <= 0.001f` cutoff and goes `ENV_IDLE`: the
envelope dies before it opens and the voice is silent **on hardware only**,
because every host render holds the gate 20-50 ms and looks perfect.

This bug was found and fixed **three times, per-site**, before the mechanism
itself was fixed:
1. `master_env`, pass ~12 (T20) — force-set to `1.0`/`ENV_DECAY` in `NoteOn`.
2. `ENGINE_SNARE` noise envs, pass 19 — `NoteOff` skips the release ("choked
   the buzz to ~26 ms").
3. `ENGINE_NOISE` noise envs, pass 27 — **never covered by either**, so Clap,
   Shaker and HHat-C were emitting a 20 ms blip and nothing else on device.

`FastEnvelope::release()` now defers into **`ENV_ATTACK_REL`** when it is called
during `ENV_ATTACK`: the attack completes, then the release runs from the top of
it.  An envelope that is already open is untouched (40/40 renders byte-identical),
and `Rel` keeps shaping the tail — which the pass-19-style "skip the release"
patch does not.  **New sites do not need their own workaround.**  Regression
test: **T37** (both halves fail against the pre-fix build).

### ENGINE_PLATE: noise_ring_gate

`VoiceState::noise_ring_gate` reset to 1.0 on NoteOn; in processBlock:
```cpp
parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
voice.noise_ring_gate *= voice.modal_decay_1;
```
Floor of 0.15 keeps faint sustained shimmer. Without this: noise stays full while ring
dies → "juxtaposed" sound.

---

## Engine Architecture

### Engine Types

| Engine | Signal path | Presets |
|--------|-------------|---------|
| `ENGINE_KS` | Karplus-Strong delay + modal additive | Koto |
| `ENGINE_BAR` | Mallet exciter → bar modal bank | Marimba, Vibraphone, Kalimba, SteelPan, Woodblock, Claves, TubularBell, GlassBowl, GlassBottle, SlitDrum, Tick |
| `ENGINE_MEMBRANE` | Strike exciter → circular membrane modal bank + boom osc | Kick2, 808Sub, Timpani, Djambe, Taiko, AcTom, RackTom, KickDrum, Conga, Handpan, Bongo, Taiko2 |
| `ENGINE_SNARE` | Membrane body (short) + snare-wire resonators | AcSnare, MarchSnare, BrushSnare |
| `ENGINE_PLATE` | Strike → inharmonic plate modes + metallic noise + crash bank | Cymbal, Gong, HHatOpen, HHatClosed*, Ride, RideBell, BellTree, Cowbell, Triangle, Tick |
| `ENGINE_NOISE` | Noise burst (+ optional modal body / AM gating) | Clap, Shaker, HHatClosed |

### Preset → Engine Mapping

```
k_Kick2(0)        ENGINE_MEMBRANE  ← ex-Timpani body, kick voice
k_Marimba(1)      ENGINE_BAR
k_808Sub(2)       ENGINE_MEMBRANE
k_AcSnare(3)      ENGINE_SNARE
k_TubularBell(4)  ENGINE_BAR
k_Timpani(5)      ENGINE_MEMBRANE  ← data-driven from Orchestral-Timpani-C.wav
k_Djambe(6)       ENGINE_MEMBRANE
k_Taiko(7)        ENGINE_MEMBRANE  ← data-driven from Taiko-Hit.wav; 6 modes incl. 1472Hz
k_MarchSnare(8)   ENGINE_SNARE
k_Koto(9)         ENGINE_KS
k_Vibraphone(10)  ENGINE_BAR
k_Woodblock(11)   ENGINE_BAR
k_AcousticTom(12) ENGINE_MEMBRANE
k_Cymbal(13)      ENGINE_PLATE
k_Gong(14)        ENGINE_PLATE
k_Kalimba(15)     ENGINE_BAR
k_SteelPan(16)    ENGINE_BAR
k_Claves(17)      ENGINE_BAR
k_Cowbell(18)     ENGINE_PLATE
k_Triangle(19)    ENGINE_PLATE
k_KickDrum(20)    ENGINE_MEMBRANE
k_Clap(21)        ENGINE_NOISE
k_Shaker(22)      ENGINE_NOISE     ← grain-pulse AM + woodblock body
k_Taiko2(23)      ENGINE_MEMBRANE  ← ex-Taiko deep membrane ("DeepBs")
k_GlassBowl(24)   ENGINE_BAR
k_HiHatClosed(25) ENGINE_NOISE     ← ex-Shaker noise voice
k_HiHatOpen(26)   ENGINE_PLATE     ← HW-approved, do not break
k_Conga(27)       ENGINE_MEMBRANE
k_Handpan(28)     ENGINE_MEMBRANE
k_BellTree(29)    ENGINE_PLATE
k_SlitDrum(30)    ENGINE_BAR
k_Ride(31)        ENGINE_PLATE
k_RideBell(32)    ENGINE_PLATE
k_Bongo(33)       ENGINE_MEMBRANE
k_GlassBottle(34) ENGINE_BAR
k_Tick(35)        ENGINE_PLATE
k_Splash(36)      ENGINE_CYMBAL    ← small pitched splash (dense-resonator engine)
k_BrushSnare(37)  ENGINE_SNARE     ← "BrshSnr": brush sweep, swirl AM + diffuse wires
k_RimShot(38)     ENGINE_SNARE     ← "RimShot": stick crack + rim-ring ping + tight buzz
k_RackTom(39)     ENGINE_MEMBRANE  ← "RackTom": rack tom at F3, the high drum to Ac Tom's low
```

NOTE: Cymbal(13), Gong(14), HHatOpen(27), Ride(32), RideBell(33) were moved to
`ENGINE_CYMBAL` (dense-resonator port) in commit 6e28c0a — the table above shows
the historical PLATE rows for context; `kPresetEngine[]` in synth_engine.h is
the authority.

### ModalPresetConfig struct fields (synth_engine.h ~line 223)

```cpp
struct ModalPresetConfig {
    float ratio2, ratio3, ratio4;            // mode freq ratios (mode1 = 1.0)
    float t60_1_ms, t60_2_ms, t60_3_ms, t60_4_ms;  // T60 per mode (ms)
    float mix;                               // modal mix (LoadPreset only; see GOTCHA)
    float env1, env2, env3, env4;            // per-mode amplitude weights
    uint8_t mode_count;                      // 2..6
    float ratio5, ratio6;                    // mode 5/6 ratios (0 = fallback formula)
    float env5, env6;                        // per-mode amplitude weights for 5/6
};
// Modes 5/6 T60: always 0.85× and 0.70× of t60_4 (no independent T60 fields).
```

### Parameter → modal-engine mapping

| Knob | Effect | Notes |
|------|--------|-------|
| Dkay | Modal T60 scale: `2^(3*(norm-ref))` | REFERENCE-ANCHORED at shipped Dkay |
| MlltStif / VlMllStf | Upper-mode brightness tilt | REFERENCE-ANCHORED |
| Rel | Ring-length (folded into t60_scale) on modal; noise tail on NOISE | |
| MlltRes | Crash intensity on ENGINE_PLATE; modal presence on non-crash | REFERENCE-ANCHORED |
| Partls (0-4) | Mode count ± around shipped count; env3-6 also scaled | Clamped [2, 6] |
| Model | Modal ratio template swap (9 physical models) | `kModelModalRatios` |
| Inharm | Overtone spread around fundamental | |
| Mterl | Upper-mode material damping | `2^(1.5·Δ)` on modes ≥ 2 |
| HitPos | Strike-position excitation tilt (rim→upper, centre→mode1) | BIPOLAR −98..98 since pass 39. 3 anchored consumers follow it both ways; the strike RADIUS and `mix_ab` are absolute and floored at 0 (inert below centre — the radius *must* stay floored or it folds) |

---

## TODOs (documented, not started)

- **Tambourine**: bright short jingle modes + light crash + grain AM (basis exists).
- **Shaker**: improved/continuous variant.
- Await next HW listening test on pass 17 before further iteration.

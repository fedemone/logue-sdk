# Instrument-Family Realism Review — July 2026

> **Round 2 (HW feedback applied)** — all seven recommendations below were
> approved and implemented; results at the bottom ("Round-2 results").
> BrshSnr was reworked (softer/longer per HW), RimShot added (preset 39),
> buzz-roll retrigger continuity added.  Stereo idea discarded as directed.

Scope: full review of every engine family against the reference samples in
`samples/`, driven by the same offline metrics the tuning pipeline uses
(spectral centroid over the first 300 ms after the peak, t40 = time for the
10 ms-window RMS to fall 40 dB below peak, and 500 Hz–12 kHz spectral
flatness).  Renders from `render_presets` at the shipped note/velocity.

**Caveats before reading the table**: several references sit at a different
pitch than the shipped render note (Woodblock, Cowbell, Triangle), some
presets have documented architectural floors (see README "Known architectural
score floors"), and single-sample references capture one instrument, one mic,
one room.  Numbers flag *candidates*, they do not prove a preset is wrong —
the HW listening pass stays the authority.

## Measured snapshot (render vs reference)

| family / preset | centroid ren→ref (Hz) | t40 ren→ref (ms) | flatness ren→ref | verdict |
|---|---|---|---|---|
| KS Koto        | 2334 → 1354 | 1760 → 530 | 0.03 → 0.02 | bright+long, acceptable (KS sustains by design) |
| BAR Marimba    | 1422 → 878  | 1040 → 660 | ok | close; HW-approved, leave |
| BAR Kalimba    | 554 → 1667  | 460 → 2400 | 0.03 → 0.22 | **too dark, too short** |
| BAR Woodblock  | 372 → 3653  | 120 → 130  | ok | t40 matches; centroid gap is a note/ref mismatch |
| MEM Timpani    | 526 → 644   | 1450 → 1390| ok | excellent (dense kernel, HW-approved) |
| MEM Taiko      | 757 → 1496  | 500 → 1330 | — | documented floor (close-mic stick transient) |
| MEM AcTom      | 205 → 1521  | 260 → 490  | 0.13 → 0.34 | **dark + short vs close-room ref** |
| MEM Conga      | 919 → 554   | 70 → 300   | 0.35 → 0.09 | **too bright, dies too fast** |
| CYM Cymbal     | 927 → 7366  | 1060 → 1260| 0.09 → 0.72 | **body reads dark/tonal on this metric** |
| CYM Ride       | 1379 → 6087 | 1330 → 1940| 0.07 → 0.52 | same pattern as Cymbal |
| CYM RidBel     | 1349 → 7127 | 1330 → 1250| 0.08 → 0.22 | same pattern |
| CYM HHat-O     | 8904 → 8376 | 920 → 290  | — | centroid matches well (HW-approved, do not touch) |
| NSE HHat-C     | 11831 → 11188 | 90 → 170 | 0.70 → 0.72 | very good |
| NSE Clap       | 4540 → 2986 | 180 → 120  | 0.61 → 0.38 | decent; slightly bright |
| PLT Cowbell    | 1395 → 5639 | 470 → 120  | 0.07 → 0.61 | **too mellow/long — a cowbell is a short clank** |
| PLT Triangle   | 2404 → 8545 | 4940 → 640 | — | under-bright (long ring itself is realistic; ref is a short sample) |

## Snare family (the focus of this pass) — FIXED in this branch

Three structural defects were found and corrected; all three were measured,
not guessed:

1. **The 3-band snare-wire rattle never ran on a live hit.**  `NoteOn` calls
   `PartialReset()`, which zeroes `snare_wire_mix` — and the restore only ever
   happened in `LoadPreset`.  Worse, the velocity-tuned wire band coefficients
   were computed *before* `PartialReset` and clobbered by it.  The snare
   family was playing modal body + plain filtered noise only.  Fixed by
   restoring the per-preset wire parameters and computing the band
   coefficients after `PartialReset`.
2. **Gate-off choked the buzz to ~26 ms.**  The Drumlogue fires `gate_off` in
   the same tick as `gate_on`; the noise envelope's Rel-rate release therefore
   governed the whole tail.  Real snare wires ring freely once the stick
   leaves.  `ENGINE_SNARE` now skips the noise-env release and decays at the
   natural NzRs-calibrated rate.  This also makes x86 renders (50 ms hold) and
   hardware (0 ms hold) behave identically for snares — the tuning pipeline
   and the hardware now measure the same thing.
3. **No velocity → buzz physics.**  Soft hits now get less wire mix (ghost
   notes are mostly head tone) and up to 2× faster buzz decay; anchored at
   full velocity so the calibrated table sound is unchanged.

After the fix (render vs reference):

| preset | centroid (Hz) | t40 (ms) |
|---|---|---|
| AcSnare | 5211 vs 3304/4442 (two refs) | 270 vs 280 |
| MrchSnr | 4705 vs 4683 | 230 vs 220 |
| BrshSnr (new) | 3787 | 310 |

**New preset 38 "BrshSnr"** — brush-swept snare (`ENGINE_SNARE`): ~30 ms
swish onset instead of a crack, a 6.5 Hz enveloped swirl AM modelling the
circular wrist motion (reuses the Clap/Shaker `noise_am` machinery), diffuse
low-Q wire bands at 2.2/3.6/6.3 kHz, softer darker membrane body, long
bandpassed (≈4.2 kHz) noise tail.  Rooms to grow, pending HW listening:
swirl rate/depth, band A centre, body level (`k_modal_mix` 0.08).

### Remaining snare-family opportunities (not done, ranked)

- **Rimshot / sidestick variant**: the modal bank + a strong bright mode
  cluster (ratios ~3.2/6.2, short T60) would make a good `RimShot` preset —
  cheap, no new DSP.
- **Buzz roll playability**: rapid retriggers currently reset the wire
  resonator states each hit (via the exciter path they persist per voice but
  the envelopes retrigger); a dedicated "wires already in motion" state
  (skip the crack burst when retriggered < 80 ms apart) would sell press
  rolls.
- **Stereo wire spread** is not possible (mono sum in the master path) — do
  not chase.

## Per-family recommendations (ranked by audible payoff)

1. **ENGINE_CYMBAL body brightness (Cymbal/Ride/RidBel)** — the dense
   resonator port matches the *envelope* well (t40 1060 vs 1260 ms) but the
   measured body centroid sits ~1-1.4 kHz vs ~6-7 kHz in the refs, and
   flatness 0.07-0.09 vs 0.5-0.7.  Suspects, in order: the pink-noise driver
   (−3 dB/oct tilts energy down; the refs are nearly white in the 2-10 kHz
   band), `resGain` being flat across the bank while low anchors get denser
   octave stacking, and the velocity-dependent `maxCutoff` of the low driver.
   A per-resonator HF-weighted `resGain` (e.g. `∝ sqrt(f)`) or a whiter
   drive blend would be the first experiment.  HHat-O matches already —
   change must be gated per-preset to protect it.
2. **Kalimba** — too dark and half the ring of the reference: raise t60_1
   (600 → ~1500 ms), brighten via MlltStif or env2 weighting.  Small table
   change, low risk.
3. **Cowbell** — refs are a 120 ms clank with centroid ~5.6 kHz; the render
   rings 470 ms at 1.4 kHz.  Shorten modal T60s (500 → ~150 ms), push ratio
   set/env toward the upper modes.  Table-only change.
4. **Conga** — dies too fast (70 vs 300 ms) and too bright: raise t60_1
   (90 → ~250 ms), soften the slap band.  Table-only change.
5. **AcTom** — darker and shorter than the close-room ref; some gap is mic
   proximity, but t60_1 350 → ~500 ms plus a brighter strike transient layer
   (the MEM `trans_*` machinery already exists) would close most of it.
6. **Triangle** — the 5 s ring is physically right; the missing 8 kHz+
   centroid is the documented KS-attack floor.  If chased at all: raise the
   hf_branch mix / hat-style bright noise rather than the KS loop.
7. **Clap** — slightly bright/flat vs ref; NzFq trim only if HW agrees.

Families in good shape: Timpani/Taiko (kernel, HW-approved), Marimba,
Woodblock, HHat-C, HHat-O, Kick family (HW-approved), 808Sub (HW: perfect),
Djambe/Bongo (HW: ok).

## CPU review (done in this branch)

Verified bit-identical output on all 39 renders:

- **Idle-CPU guard**: a silent unit ran the pre-clip clamp + master SVF +
  soft-clip per sample forever (the OS renders continuously).  After a 320 ms
  master-chain flush window, `processBlock` now returns right after the
  buffer clear.  Host bench: idle cost 18.7 → 0.2 ms per 20 s of audio.
- Tilt-EQ one-pole skipped when Tone = 0 (every preset ships Tone = 0) in all
  three render paths (voice / cymbal / kernel).
- `parallel_noise_gain` preset-family selection hoisted out of the per-sample
  loop; non-KS coupling taps zeroed per block; `noise_band_mix` clamped at
  write sites; hi-hat centroid SVF only processed when actually used;
  AcousticTom's second loss pole selected by a per-hit flag instead of a
  per-sample preset compare.

Remaining CPU ideas (unranked, all lower value): block-loop fission of the
per-sample engine branches (risky, large diff for maybe ~5%), NEONizing the
3-band wire resonators (they are only 3 biquads — not worth it), and skipping
`process_exciter` when both noise envelopes are idle and the mallet decayed
(already effectively gated internally).

## Round-2 results (HW feedback round, all 7 recommendations applied)

Same metrics as above; render at the shipped note, velocity 100.

| preset | before → after → ref (centroid Hz) | before → after → ref (t40 ms) | notes |
|---|---|---|---|
| Cymbal   | 927 → **6932** → 7366 | 1060 → 910 → 1260 | `hfTilt=3.0` HF-weighted resGain |
| Ride     | 1379 → **6121** → 6087 | 1330 → 1310 → 1940 | `hfTilt=2.0` — essentially exact |
| RidBel   | 1349 → **6002** → 7127 | 1330 → 1180 → 1250 | `hfTilt=2.0` |
| Kalimba  | 554 → 661 → 1667 | 460 → **1140** → 2400 | T60 600→1500 ms; centroid gap is ref-pitch mismatch (ref tine ≈1.4 kHz f0, shipped note 349 Hz) |
| Cowbell  | 1395 → 1201 → 5639 | 470 → **170** → 120 | now a short clank ✓; brightness capped by low mode stack — HW listen next |
| Conga    | 919 → **481** → 554 | 70 → **180** → 300 | body 90→250 ms, slap softened |
| AcTom    | 205 → 225 → 1521 | 260 → **370** → 490 | body 350→500 ms + stick transient layer + NzMx 30; energy centroid stays body-dominated by design (attack is brighter) |
| Triangle | 2405 → **4772** → 8545 | — | modes 4-6 added (9.0/13.1/17.9); remaining gap = documented KS attack floor + ref-pitch mismatch |
| Clap     | 4540 → **3692** → 2986 | 180 → 180 → 120 | NzFq 300→200 |

**HF-tilt mechanism** (`CymbalConfig.hfTilt`, dsp_core.h): each ringer's tap
gain is weighted by `(f/2 kHz)^(0.5·hfTilt)` via `fasterpowf` (note-on time,
amplitude-only — fast-math approved).  hfTilt=1 exactly counters the pink
driver's −3 dB/oct amplitude tilt; measured, the drive carries extra red
energy (thwack burst, swept one-pole), so the shipped values are 3.0 (Cymbal)
and 2.0 (Ride/RidBel).  HHat-O ("do not break"), Gong (deliberately tonal)
and Splash keep the legacy flat bank.

**Snare round 2**: BrshSnr velocity-compressed (0.30–0.72), swish onset
~100 ms, wires resting on the head (`k_wire_onset_env=1` → no crack burst).
Buzz rolls: retriggers < 80 ms keep the wire resonator states and skip
the crack burst.

## Round 3 — calibration against the user-supplied references

Commit f924cc1 added `brush_snare_hit_hard.wav`, `snare_brush_soft.wav`,
`rimshot-snare.wav`.  Measurements and resulting presets:

**Brush references**: a smooth, steady, VERY bright hiss (~0.65 s, flatness
0.84, energy rising with frequency — 62% above 6 kHz), body <300 Hz ≈ 1%,
near-zero amplitude modulation (depth ~0.002, faint ~12 Hz flutter), no wire
resonance.  Hard and soft samples are nearly identical spectrally — velocity
is mostly level, slightly brightness.  The round-2 BrshSnr was harsh
precisely where it deviated: ringing wire bands, nasal 4.2 kHz BP colour,
deep 4.2 Hz swirl.  Fixed: wire mix 0.60→0.10, noise HP 2.5 kHz + bright
band tilt (velocity-tilted band mix 0.55+0.30·v, VlMllStf 60 in the row),
modal body 0.02, swirl → subtle persistent 12 Hz shimmer at depth 0.15.

| metric | BrshSnr render | brush ref (hard) |
|---|---|---|
| centroid | 10692 Hz | 11025 Hz |
| flatness | 0.83 | 0.84 |
| 1-3k / 3-6k / >6k energy | 12/17/69 % | 9/14/72 % |

**Rimshot reference**: an extremely tight pop — t40 45 ms, centroid 3.1 kHz,
woody honk cluster at 877/945/1017/1107 Hz + 1754 + 2785 Hz, <300 Hz ≈ 1%,
>6 kHz ≈ 1%.  RimShot rebuilt on note 69 (440 Hz) with measured ratios
(2.0/2.29/3.99/6.33), quiet fundamental, env weights countering the fixed
modal_sum taper, BP noise at 3 kHz, NzRs 540.

| metric | RimShot render | rimshot ref |
|---|---|---|
| centroid | 3127 Hz | 3106 Hz |
| t40 | 55 ms | 45 ms |
| 300-1k / 1-3k / 3-6k | 43/52/4 % | 33/56/10 % |

## Round 4 — corrected brush references (BrshSnr re-calibration)

Commit f93f4b5 amended the brush reference set: `brush_snare_hit_hard.wav`
was **mislabelled** and is gone; the correct dynamic layers are
`snare_brush_hard.wav`, `snare_brush_medium.wav`, `snare_brush_soft.wav`.
(RimShot was confirmed good on hardware and is unchanged from Round 3.)

The corrected samples are a completely different sound from what Round 3
chased — much **darker and more coloured**, not a bright white hiss:

| metric | hard | medium | soft |
|---|---|---|---|
| peak / RMS | 1.00 / −25.2 dB | 0.21 / −34.6 dB | 0.07 / −38.1 dB |
| centroid | 4202 Hz | 3931 Hz | 4481 Hz |
| flatness (0.5–12k) | 0.35 | 0.29 | 0.26 |
| attack → peak | 25 ms | 23 ms | 13 ms |
| t40 (decay) | 315 ms | 282 ms | 185 ms |
| <300 / .3–2k / 2–6k / 6–22k | 1/22/54/23 % | 1/24/57/17 % | 1/13/66/20 % |

Key corrections vs the Round-3 build (which measured centroid 11 101 Hz,
78 % of energy above 6 kHz — far too bright):

- **Noise voicing**: SVF switched from **HP 2.5 kHz → BP ~4.9 kHz**
  (`NzFltr` 2→1, `NzFltFrq` 250→500). The old HP-over-white produced the
  11 kHz centroid; a band-pass concentrates energy in 2–6 kHz where the refs
  live and drops flatness to a colored ~0.31.
- **band_mix** dropped from the 0.79 cap to a centred ~0.62 (0.56+0.12·v) —
  the body/sizzle split no longer over-emphasises the top.
- **Attack** sped up from ~100 ms (attack_rate 0.0005) to ~22 ms
  (0.0030 + 0.0012·(1−v), faster for soft hits) — the corrected refs peak at
  13–25 ms, not 100 ms.
- **Dynamics** widened: velocity map 0.30..0.72 → **0.18..0.72** (soft floor
  lowered, hard-hit "can't crack" ceiling kept) so ghost strokes are genuinely
  quiet, matching the refs' ~13 dB pp→ff span. Velocity still drives decay
  length (soft t40 186 ms → hard 260 ms in-engine, matching "soft and long").
- **Tail** lengthened (Rel 14→17).

Result — render (note 38, vel 100 → mid-range velocity) vs the **medium** ref:

| metric | BrshSnr render | brush ref (medium) |
|---|---|---|
| centroid | 3735 Hz | 3931 Hz |
| flatness | 0.31 | 0.29 |
| attack / t40 | 17 / 253 ms | 23 / 282 ms |
| .3–2k / 2–6k / 6–22k | 26/59/15 % | 24/57/17 % |

In-engine velocity sweep (confirms "soft and long"):

| MIDI vel | RMS | t40 |
|---|---|---|
| 40 | −26.2 dB | 186 ms |
| 80 | −23.7 dB | 226 ms |
| 120 | −22.1 dB | 260 ms |

### Round 4b — HW listen: "too much hit for medium, too bright"

Darkened and softened past the measured reference (the ear overrides the
metric here):

- **band_mix** 0.62 → ~0.44 — with a band-pass source the split mostly
  controls the bright onset burst, so this tames the "hit" more than the tone.
- **BP center** 4.9 kHz → 4.0 kHz (`NzFltFrq` 500→400) — this is what actually
  lowers the tone: centroid 3744 → 3126 Hz, energy >6 kHz halved (15%→8%).
- **Onset** softened: attack_rate base 0.0030→0.0020 and onset ramp 12→24 ms —
  the start swells in (attack-to-peak 17 → 37 ms) instead of striking.

Render (mid velocity): centroid 3126 Hz, flatness 0.225, band split
33/58/8% (.3-2k / 2-6k / 6-22k), attack 37 ms, t40 290 ms — sits below the
medium reference's 3931 Hz on purpose.

### Round 4c — HW listen: "still too much hit, hitting more than the hard sample"

The Round-4b onset was diagnosed by comparing peak-normalised onset envelopes
against the references. The references PEAK EARLY (~16 ms) then decay; the 4b
build instead SWELLED to a late broad hump (peak ~50 ms, sustained 0.4-0.5 of
peak through 20-50 ms) — a crescendo gesture that reads as a "hit". The long
onset ramp (45 ms) plus slow attack were building that hump. Fixed by matching
the reference shape: quick soft entry + immediate decay, no swell.

- Onset ramp 45 ms -> 10 ms (declick only, no swell-up).
- attack_rate 0.0009 -> 0.0045 base: peak-to-onset now ~15 ms (refs: 16 ms),
  then a clean decay (20 ms:0.37, 50 ms:0.17, 120 ms:0.06) instead of a plateau.
- Crest 15.6 — peakier than 4b's humped 9.9 but still below the hard
  reference's 18.1, and now shaped like the refs (transient + decay, not a
  sustained body).

Tone kept dark per 4b: centroid 3065 Hz, energy >6 kHz 8%. Tail still "long":
t40 230 ms (soft) -> 311 ms (hard, ref 315). Level 5 dB pp->ff spread.

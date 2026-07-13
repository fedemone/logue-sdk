# Instrument-Family Realism Review — July 2026

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

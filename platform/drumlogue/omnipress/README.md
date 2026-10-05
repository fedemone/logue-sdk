# README.md - OmniPress Master Compressor for KORG drumlogue

> **Disclaimer:** OmniPress is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

## Overview

**OmniPress** is a character master bus compressor for the KORG drumlogue, loosely inspired by the **Eventide Omnipressor**. The panel offers **two compression modes** — Standard and Multiband — and takes advantage of the drumlogue's 4-channel master effect input for external sidechain processing.

A third engine, modelled on the **Empirical Labs EL8 Distressor**, is still in the source and on the bench but no longer on the panel: see [Shelved: the Distressor engine](#shelved-the-distressor-engine).

### Key Features

| Feature | Description |
|---------|-------------|
| **2 Compression Modes** | Standard (Omnipressor transfer curve) and Multiband (3 bands) |
| **External Sidechain** | 4-channel input for ducking/pumping — add 4 to the DETECT parameter (ID 11) to key from SC L/R instead of the main bus. In Multiband the key is split by its own crossover, so it ducks the bands the key actually occupies |
| **Drive** | Overlord tube stage in Standard, a per-band triode in Multiband |
| **Overlord EQ** | 3-band semi-parametric EQ (Bass/Treble/Presence) in the dynamics chain |
| **Multiband on the panel** | One threshold and one ratio knob per band, two independent crossover points, and a Solo/Mute selector — no band selector in front of shared knobs |
| **Exact ballistics** | ATTACK and RELEASE are applied once, by the gain smoother, in both modes and to every band |
| **Bad-sample guard** | A NaN, infinity or absurd sample on the bus cannot latch the master; a watchdog clears the state if anything goes wrong inside |
| **NEON Optimization** | Fully vectorized for ARM Cortex-A7 |
| **24 User Parameters** | Every slot the SDK allows, across 6 control pages |
| **24dB/oct Crossover** | Linkwitz-Riley filters for multiband mode |

---

## Compression Modes

### Mode 0: Standard Compressor
A versatile, clean compressor with all the essentials:
- **Threshold**: -60 to 0 dB
- **Ratio**: 1:1 to 20:1
- **Attack**: 0.1 to 100 ms
- **Release**: 10 to 2000 ms
- **Soft/Hard Knee** selectable
- **Peak/RMS detection** with blend

### Mode 1: Multiband Compressor
Three bands split by Linkwitz-Riley 24 dB/oct crossovers, each with its own
compressor and triode saturation, all on the panel at once:

| Band | Default range | On the panel |
|------|---------------|--------------|
| **Low** | up to 250 Hz | Lo Thresh, Lo Ratio |
| **Mid** | 250 Hz – 2.5 kHz | Mid Thresh, Mid Ratio |
| **High** | above 2.5 kHz | Hi Thresh, Hi Ratio |

- **Xover Lo** moves the low/mid split over 62.5 Hz – 1 kHz, **Xover Hi** the
  mid/high split over 1 kHz – 16 kHz. The ranges meet only at 1 kHz, so the
  splits can never cross.
- **SoloMute** solos or mutes one band at a time: Off, Lo-Solo, Mi-Solo,
  Hi-Solo, Lo-Mute, Mi-Mute, Hi-Mute.
- **ATTACK** and **RELEASE** on page 1 set all three bands; **MAKEUP** and
  **DRIVE** are global (DRIVE feeds each band's own triode voicing).
- Each band's detector is a peak follower; the gain smoother behind it carries
  the ballistics.

Until this layout the per-band controls sat behind a band selector (MBand) that
the Thr/Ratio/Atk/Rel/Makeup/State knobs then edited, and the readout could show
only one band; the two crossover points moved together, a decade apart, from one
XOVER knob; and page 1's ATTACK and RELEASE did nothing in Multiband. Per-band
attack, release and makeup gave way to the global controls to make room.

---

## Shelved: the Distressor engine

The Distressor (EL8-style ratios, Opto and NUKE, nine DstrDist distortion types,
the slam region) was taken off the panel because its distortion was not pleasant
in any setting. Its code stays — `distressor_mode.h`, `drive_slam.h`,
`wavefolder.h` and its branches in `masterfx.h` — fixed, documented and measured
by the bench, so it can be reused or brought back:

* **Reaching it.** The drumlogue only ever calls `setParameter()`, and no panel
  value selects it (bench section M4 checks that). `MasterFX::setEngineMode(
  COMP_MODE_DISTRESSOR)` and `MasterFX::setDistressorDistortion(0..8)` do, and
  that is how `test_levels` keeps measuring it.
* **Putting it back on the panel** needs a slot for DstrDist (ID 15 is SoloMute
  now) and COMP MODE's maximum raised to 2 with the panel-to-engine mapping in
  `setParameter(k_compressor_mode)` extended. Programs saved before the change
  stored 1 = Distressor; they now load as Multiband.
* **Fixed before shelving.** Its gain smoother now carries ATTACK, RELEASE, the
  Opto release and the program-dependent release exactly (they never worked
  before), and every DstrDist shaper has a DC blocker and level matching behind
  it — see [Drive stage output](#drive-stage-output-dc-blocker-and-level-matching).

| Feature | Implementation |
|---------|---------------|
| **8 Ratios** | 1:1 (warm), 2:1, 3:1, 4:1, 6:1, 10:1 (opto), 20:1, NUKE |
| **Distortion Modes** | Dist2 (2nd harmonic), Dist3 (3rd harmonic), Both, plus 5 wavefolder shapes |
| **Opto Mode** | Release 5.7× the RELEASE setting |
| **Program-dependent release** | Blends toward 3× longer as gain reduction deepens toward −20 dB |
| **NUKE Mode** | Brick-wall limiting |
| **Soft knee** | Ratio-dependent: wide at 2:1, tightening to a brick wall at NUKE |

The next three sections describe its drive stage.

---

## Distressor Drive/Wavefolder (5 Modes, shelved)

The drive stage offers 5 distinct character modes, controllable via the DRIVE parameter (0-100%):

| Mode | Name | Description | Character |
|------|------|-------------|-----------|
| **0** | Soft Clip | Tanh approximation | Tube-like saturation |
| **1** | Hard Clip | Brick-wall limiter | Digital distortion |
| **2** | Triangle Folder | Wavefolding | Synth-like aggression |
| **3** | Sine Folder | Sinusoidal folding | Smooth, complex harmonics |
| **4** | Sub-Octave | Zero-crossing square wave | Gritty, synth bass |

The drive amount controls both the input gain to the waveshaper and the dry/wet blend for parallel processing.

---

## The Slam Region (Distressor, DRIVE above 60, shelved)

Every shaper in the unit is bounded — soft clip, both harmonic saturators and
the tube all converge on ±1, and the folders on their fold pattern — while the
drive law feeding them was linear in the knob (`g = 1 + 19d` for the wavefolder,
`1 + 39d` for the harmonic saturators). The whole top half of DRIVE was
therefore worth about 4 dB of extra push, which on a −20 dBFS bus left the
shapers barely into their knee: DRIVE 60 → 100 moved Soft from 12.8% to 16.9%
THD. What little did change arrived mostly as level — +17 dB of fundamental
across the knob — and the ear discounts level, so the knob read as a volume
control with a bit of thickening.

From **DRIVE 60 up, in Distressor mode only**, three things change:

| | What it does | Why a bounded shaper needs it |
|---|---|---|
| **Geometric pre-gain** | Up to ×24 on top of the old law (×960 total for the harmonic saturators) | Linear gain gives a vanishing number of dB per click near the top. Geometric gain makes every click worth the same push, which is what carries each saturating type from its knee into the square-wave regime |
| **Program-dependent bias** | A DC offset ahead of the shaper, tracking the drive stage's envelope (15 ms attack, 250 ms release) | Once a stage is fully saturated more gain genuinely cannot change the waveform — a square is a square. Moving the level it clips *around* still can: it shifts the duty cycle, which brings in the even harmonics and the hollow, nasal quality of a hard-biased fuzz. A fixed offset would be nothing next to a few hundred times gain, so it has to follow the envelope — which also makes it breathe with the program, the way grid blocking does in the tube stage next door |
| **Output trim** | Up to −1.9 dB, tube path only | The point is to hear character rather than level. The DstrDist shapers no longer need it: their level is held continuously by the drive stage's level matching (see below), which a fixed trim in front of it could not change |

Below 60 all three are inert, and Standard and Multiband — which never arm the
slam — are untouched.

### Drive stage output: DC blocker and level matching

Every DstrDist shaper (everything but Off) now runs through
`distressor_drive_output()` at **every** DRIVE setting, not only in the slam
region:

* **DC blocker** (≈11 Hz, the slam's corner). Dist2 and Both are asymmetric by
  design and leave an offset at any drive. Below the knee nothing used to remove
  it: on the reported chain Dist2 parked up to +0.22 of DC on the master bus
  (+20–25% of peak between DRIVE 10 and 60).
* **Level matching.** A bounded shaper's output level, once it saturates, is its
  own ceiling whatever went in, so the old law turned a −20 dBFS bus into a
  −4 dBFS one by DRIVE 60, and at SLAM 67 the whole bus into a full-scale
  square pinned on the output limiter. No fixed makeup can fix that, because how
  far a shaper saturates depends on the programme — the history here is 1/g
  (quieter as you drive), then 1/√g (still quieter), then none (louder). So the
  stage measures its own input and output with two identical mean-square
  followers (5 ms attack, 200 ms release) and applies their ratio, clamped to
  −40…+12 dB. Fast attack, so turning DRIVE up cannot blast the bus for more than
  a few ms; identical ballistics on both sides, so the ratio is the shaper's gain
  at the current level and drums do not pump.

DRIVE now changes the character and not the level: THD climbs exactly as it did
(Dist2 24% → 52% from 60 to 100) while the output stays within about 2.5 dB of
DRIVE 0 for every type.

The nine DstrDist settings reach three structurally different stages, so each
takes its own share (`drive_slam_voicing` in `constants.h`):

| Family | DstrDist | Pre-gain | Bias | Trim |
|--------|----------|----------|------|------|
| **SAT** | Dist2, Dist3, Both, Soft, Hard, SubOct | ×24 | full | none (level matched) |
| **FOLD** | Trg, Sine | ×1 | 15% | none (level matched) |
| **TUBE** | Off (Overlord fall-through) | ×6 | 50% | −1.9 dB |

The folders need no extra gain: their transfer curve is periodic, so gain adds
folds linearly and they are already past 100% THD at the top of the knob — only
a little bias, to break the fold pattern's symmetry. The tube is two cascaded
stages that compound and carries its own bias tracker, so it needs less of
everything than a bare saturator.

Measured on a 1 kHz sine at −20 dBFS, Distressor at 1:1 so the compressor is out
of the picture (`./test_levels slam`):

| DstrDist | THD at 60 | THD at 100 | out at 60 | out at 100 | before level matching (60 / 100) |
|----------|-----------|------------|-----------|------------|------------|
| Off | 32.7% | 49.6% | −5.2 dBFS | −6.1 dBFS | (tube, unchanged) |
| Dist2 | 24.0% | 52.1% | −22.9 dBFS | −22.9 dBFS | −3.9 / −2.6 dBFS |
| Dist3 | 18.7% | 58.1% | −22.0 dBFS | −23.0 dBFS | −4.6 / −4.4 dBFS |
| Both | 17.9% | 56.7% | −22.1 dBFS | −23.0 dBFS | −4.0 / −3.4 dBFS |
| Soft | 12.8% | 54.8% | −22.3 dBFS | −23.0 dBFS | −7.2 / −4.7 dBFS |
| Hard | 8.7% | 63.6% | −22.4 dBFS | −23.0 dBFS | −2.0 / −4.1 dBFS |
| SubOct | 12.8% | 54.8% | −23.5 dBFS | −24.1 dBFS | −7.1 / −4.5 dBFS |

(A −20 dBFS-peak sine is −23 dBFS RMS, so the shapers now come out where the
signal went in.) A symmetric square wave is 48% THD, so the saturating families
cross into and past it. The Off row is the Overlord tube that Standard shares,
whose level law is unchanged.

---

## Parameter Reference

OmniPress uses all **24 parameters** the SDK allows (`UNIT_MAX_PARAM_COUNT`),
across 6 pages of 4. IDs below match `header.c`.

### Page 1: Core Dynamics

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 0 | THRESH | -60.0 to 0.0 dB | Threshold (x0.1 dB) — Standard |
| 1 | SLOPE | 1 to 100 | Omnipressor function knob: expansion → 1:1 → limiting → reverse — Standard |
| 2 | ATTACK | 0.1 to 100.0 ms | Attack of the gain smoother — Standard and every Multiband band |
| 3 | RELEASE | 10 to 2000 ms | Release of the gain smoother — Standard and every Multiband band |

### Page 2: Character & Output

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 4 | MAKEUP | 0.0 to 24.0 dB | Output makeup gain (x0.1 dB) |
| 5 | DRIVE | 0 to 100% | Standard: Overlord tube stage · Multiband: per-band triode saturation |
| 6 | MIX | -100 to +100 | Dry/wet balance (-100=dry, 0=balanced, +100=wet) |
| 7 | SC HPF | 20 to 500 Hz | Sidechain high-pass filter cutoff |

### Page 3: Mode, Limits & Detector

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 8 | COMP MODE | 0–1 | 0=Standard, 1=Multiband (a program stored as 2 also loads Multiband) |
| 9 | ATT LMT | -30.0 to 0.0 dB | Omnipressor attenuation limit: how far the VCA may duck — Standard |
| 10 | GAIN LMT | 0.0 to 30.0 dB | Omnipressor gain limit: how far it may boost below threshold — Standard |
| 11 | DETECT | 0–7 | Standard: 0=Peak, 1=RMS, 2=Blend · **+4 keys from the external sidechain input** (Multiband reads only the +4) |

### Page 4: Overlord EQ & Solo/Mute

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 12 | BASS | 0–100% | Overlord EQ low shelf (50 = flat) |
| 13 | TREBLE | 0–100% | Overlord EQ high shelf (50 = flat) |
| 14 | PRESENCE | 0–100% | Overlord EQ presence shelf (50 = flat) |
| 15 | SoloMute | 0–6 | Multiband: Off, Lo-Solo, Mi-Solo, Hi-Solo, Lo-Mute, Mi-Mute, Hi-Mute (was DstrDist) |

### Page 5: Multiband Thresholds

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 16 | Lo Thresh | -60.0 to 0.0 dB | Low band threshold |
| 17 | Mid Thresh | -60.0 to 0.0 dB | Mid band threshold |
| 18 | Hi Thresh | -60.0 to 0.0 dB | High band threshold |
| 19 | Xover Lo | 0–100 | Low/mid split, 62.5 Hz – 1 kHz, logarithmic. 50 = 250 Hz |

### Page 6: Multiband Ratios

| ID | Name | Range | Description |
|----|------|-------|-------------|
| 20 | Lo Ratio | 1.0 to 20.0 | Low band ratio |
| 21 | Mid Ratio | 1.0 to 20.0 | Mid band ratio |
| 22 | Hi Ratio | 1.0 to 20.0 | High band ratio |
| 23 | Xover Hi | 0–100 | Mid/high split, 1 kHz – 16 kHz, logarithmic. 33 = 2.5 kHz |

All three band thresholds default to -20 dB. Before this layout the factory
reset put -20 dB on the Low band only and left Mid and High at an invisible
-10 dB, so a default Multiband now compresses a little more.

---

## Signal Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         INPUT (4-channel)                         │
│              [Main L, Main R, Sidechain L, Sidechain R]           │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                         INPUT GUARD                               │
│        NaN / Inf -> silence, everything clamped to +-16           │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      SIDECHAIN SELECT                             │
│              External (SC L/R) or Internal (Main L/R)             │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                      SIDECHAIN HPF (20-500 Hz)                    │
│                    12dB/oct Bessel filter                         │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                        MODE SELECTOR                              │
│  ┌──────────────────────────┐   ┌──────────────────────────┐     │
│  │  STANDARD                │   │  MULTIBAND               │     │
│  │  • Peak/RMS/Blend level  │   │  • LR4 split, Xover Lo/Hi│     │
│  │    follower              │   │  • per-band peak follower│     │
│  │  • Omnipressor curve     │   │  • per-band Thresh/Ratio │     │
│  │  • gain smoother:        │   │  • gain smoother:        │     │
│  │    ATTACK / RELEASE      │   │    ATTACK / RELEASE      │     │
│  │                          │   │  • per-band triode DRIVE │     │
│  │                          │   │  • SoloMute              │     │
│  └────────────┬─────────────┘   └────────────┬─────────────┘     │
└───────────────┼──────────────────────────────┼───────────────────┘
                ▼                              ▼
┌──────────────────────────────┐               │
│  OVERLORD TUBE (DRIVE > 0)   │               │
└───────────────┬──────────────┘               │
                └──────────────┬───────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                   OVERLORD EQ (BASS/TREBLE/PRESENCE)              │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│               DRY/WET MIX, MAKEUP GAIN, OUTPUT LIMITER            │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│     WATCHDOG: non-finite wet path or detector state ->            │
│     clear the audio-rate state, keep the settings                 │
└─────────────────────────┬───────────────────────────────────────┘
                          ▼
                   OUTPUT (Stereo)
```

---

## NEON Optimization Strategy

All processing is vectorized to process **4 samples simultaneously**:

```c
// Load 4 stereo frames with sidechain
float32x4x4_t interleaved = vld4q_f32(in_p);
float32x4_t main_l = interleaved.val[0];  // L0, L1, L2, L3
float32x4_t main_r = interleaved.val[1];  // R0, R1, R2, R3
float32x4_t sc_l   = interleaved.val[2];  // SC L0, SC L1, SC L2, SC L3
float32x4_t sc_r   = interleaved.val[3];  // SC R0, SC R1, SC R2, SC R3

// Process all 4 samples in parallel
float32x4_t envelope = envelope_detect(&envelope_, main_l, sidechain);
float32x4_t gain_db = gain_computer_process(&gain_comp_, envelope_db, thresh_db_, ratio_);
```

Performance target: **< 200 cycles per sample** (< 2% CPU on 1GHz ARM Cortex-A7)

---

## Parameter String Display

| Parameter | Values Displayed |
|-----------|------------------|
| COMP MODE | "Stndrd", "Mltibnd" |
| DETECT | "Peak"/"RMS"/"Blend" (+"SC" variants) |
| SoloMute | "Off", "Lo-Solo", "Mi-Solo", "Hi-Solo", "Lo-Mute", "Mi-Mute", "Hi-Mute" |
| Xover Lo | "63Hz" … "250Hz" … "1000Hz" |
| Xover Hi | "1.0kHz" … "2.5kHz" … "16.0kHz" |
| Lo/Mid/Hi Ratio | "4.0:1" |
| SLOPE | "Exp 1.5" / "2.0:1" / "Limit" / "Rev 1.5" |
| MIX | "DRY" (-100), "BAL" (0), "WET" (+100) |
| DRIVE | "45%" |

---

## Bug Fixes Applied

| Bug | Symptom | Fix |
|-----|---------|-----|
| Multiband gain-reduction polarity inverted | Multiband mode acted as a downward expander (attenuated quiet signals, passed loud ones) | `excess = env_dB − threshold_dB` with clamp to ≥ 0; was `thresh − env` |
| ratio=0 hard-limit returned +100 dB | NUKE mode at ratio=0 blew up output | `gain_red = 100.0f` before negation; was `-100.0f` |
| Multiband 7 dB quieter than the other modes | Switching COMP MODE to Multiband dropped the level | `MASTER_SUM_SCALING` 0.45 → 1.0; the Linkwitz-Riley tree already reconstructs to unity |
| Detector fed `L+R` instead of `0.5*(L+R)` | Threshold 6 dB optimistic on mono material, and different from Multiband's per-band detector | Average the sidechain in `process_block` |
| MAKEUP built from `fasterpowf` | 0.25 dB insertion loss at MAKEUP=0, 24.0 dB delivered as 23.69 dB | `e_expff(dB * INV_DB_COEFF)` — 0.033 dB worst case (master, per-band, and the shelving-filter gain) |
| `linear_to_db` interpolated the raw mantissa | 0.52 dB of error landing straight on the Standard/Distressor threshold; Multiband duplicated the same bit-trick at 0.17 dB | Shared `neon_log2q_f32` in float_math.h, minimax cubic, 0.005 dB |
| Wavefolder applied the drive gain twice | Total gain `(1+19d)²` = +52 dB at DRIVE=100, pinning everything to the output limiter | Makeup is `Q_rsqrt(g)`, so net small-signal gain is `sqrt(g)` (+13 dB across the knob) |
| Triangle folder inverted polarity | `y = -x` throughout the linear region, so the wet path cancelled the dry one at partial MIX (-37 dB at BAL) | Return `1 - |…|` instead of `|…| - 1` |
| Triangle folder never folded negative peaks | `vcvtq_s32_f32` truncates toward zero, so the modulo went negative; mode was indistinguishable from Hard clip | Floor the quotient before the modulo |
| Sine folder used a 2-term Taylor series over ±π | Returned -2.03 instead of 0 at the fold; fundamental collapsed above DRIVE 15 | `sin_ps` from float_math.h, whose Cephes range reduction lets the fold keep folding — no clamp needed |
| Distortion types not level-matched | Sine ran +3.9 dB hot, SubOct 2 dB quiet, at DRIVE=0 | Sine scaled by 2/π, SubOct mix renormalised — every shaper now has unity small-signal gain |
| DRIVE=1 did nothing; DRIVE=2 stepped ~2 dB | Gate `drive_ > 0.01f`, plus the Overlord EQ never ran below DRIVE=2 | DRIVE=0 takes the EQ-only path; the tube blend fades in over the bottom tenth of the knob |
| DRIVE was a dead knob on the factory default | Distressor bypasses its shaper at DstrDist=None (the init value) and the broadband tube was gated off for every non-Standard mode, so DRIVE 0→100 changed nothing at all in the mode most users reach for | Distressor falls through to the Overlord tube when DstrDist is None, matching Standard row for row (bench section G8) |
| Denormals stalled the audio thread on silence | Every decaying IIR parks its delay line in the subnormal range and stays there. Multiband holds 64 crossover states alone, and measured **44× more CPU to process silence than signal** — clicks between drum hits, and an overrun on a loaded machine | `flush_denormal` / `flush_denormal_q` at each state store; the ratio is now 1.0× (bench section G9) |
| Shelving filter left a stale delay line when flat | `fabsf(gain_db) < 0.01f` returned early without updating state, so moving BASS/TREBLE/PRESENCE back off flat resumed the filter from however long ago it was last active — a click | Clear the state on bypass. At 0 dB the shelf's b coefficients equal its a coefficients, so the identity filter's steady state really is zero — resuming is continuous |
| Presence shelf ran at two frequencies | 5 kHz in the EQ-only path, 5.5 kHz in the drive path, sharing one biquad state, so crossing between them recomputed coefficients under a live delay line | One `OVERLORD_PRESENCE_HZ` for both |
| `multiband_init` never cleared its filter state | Crossover, envelope, gain, tube-bias and DC-blocker state survived a Reset — they were only ever zeroed by landing in `.bss` at load, so nothing could clear them afterwards | Zero all of it in `multiband_init` |
| `Reset()` wiped the parameters it had just set | `multiband_init` ran *after* the per-band `setParameter` calls, so every band came back on its hardcoded defaults rather than the header's | Clear DSP state first, then apply defaults |
| Wavefolder makeup scaled the saturated ceiling | Every shaper saturates at ±1, so a `1/sqrt(g)` makeup scaled the ceiling with it: peak fell to 0.224 at DRIVE=100 while the harmonic saturators reached 1.000. Driving harder made those five modes **quieter** — on a drum bus Sine sat 15.3 dB below DstrDist=Off | Apply the drive once and don't compensate. Worst-case spread across the nine types drops from 16.1 dB to 4.7 dB (bench G4b) |
| DstrDist switched a filter into the detector | Selecting a wavefolder mode also enabled the detector's 100 Hz HPF, so on a kick-heavy bus gain reduction backed off and the output jumped ~4 dB — the distortion selector was changing the compression | Detector shaping belongs to DETECT (which has Emph for this); DstrDist no longer touches `detector_mode` |
| SLOPE gave no readout of what it selected | Typed `k_unit_param_type_none`, so the host showed a bare 0.01–1.00 while the knob was really selecting 8 Distressor ratios or an Omnipressor curve | Typed `k_unit_param_type_strings` so the existing per-mode display is used: "2.0:1", "Limit", "Rev 1.5", or the Distressor ratio by name |
| SLOPE evaluated against the previous COMP MODE | A host replaying parameters in ID order sets SLOPE (ID 1) before COMP MODE (ID 8), so the Distressor ratio stayed at its 4:1 init | `k_compressor_mode` re-applies the stored SLOPE |
| DstrDist max was 9 | Value 9 is rejected by `setParameter` and has no display string | Max is 8 in `header.c` |
| Peak detector latched | Hold counter incremented once per 4-sample block, so the "10 ms hold" was 417 ms and expiry applied a single 0.999 step — ~0.009 dB of decay per 417 ms. Gain reduction never recovered after a transient and RELEASE did nothing in Peak mode, the default | Rectify, then let the attack/release one-pole provide ballistics, with the intended 10 ms hold implemented in samples |
| Blend detection was 0.3 × RMS | Its branch read `peak_hold`, which only the Peak branch wrote — and a `switch` runs one branch, so peak stayed 0 and the envelope sat 10.5 dB low, pivoting into upward gain | Blend derives peak locally |
| Detector and Distressor gain smoother ran 4× slow | State was `float32x4_t`, giving each lane its own history advanced once per block, so per-sample coefficients were applied at 12 kHz | Sequential scalar state, as the crossovers and `standard_process` already use |
| DRIVE went dead above about 60% | Every shaper is bounded, and the drive law was linear in the knob, so the top 40% was worth ~4 dB of push. At a −20 dBFS bus level DRIVE 60 → 100 moved Soft from 12.8% to 16.9% THD, and most of what did change arrived as +17 dB of level rather than harmonics — the knob read as a volume control | A slam region above DRIVE 60 in Distressor mode: geometric pre-gain, a program-dependent bias that shifts the duty cycle once the shaper is saturated, and an output trim so the region buys character instead of level. THD at DRIVE 100 goes 16.9% → 54.8% on Soft, 23.6% → 63.6% on Hard, at the same loudness and off the output limiter (bench section S) |
| External sidechain unreachable | `use_external_sc_` was only ever assigned 0; the 4-channel input the unit asks for could not be selected | DETECT + 4 selects it (all 24 SDK parameter slots were already taken). Multiband splits the key through its own mono crossover so it ducks per band |
| One bad sample on the bus could silence the drumlogue | Nothing guarded the input or the state. Every stage is an IIR, the detector takes any finite value at face value, and the master FX sees every other unit's output: measured on the shipped ARM build, one +Inf sample left Multiband at -300 dBFS until the unit was reloaded and one 1e20 sample silenced Distressor (Dist2, DRIVE 67, WET) for 9 s. Whether a NaN washes out was left to what `-ffast-math` made of each comparison: Standard and Distressor happened to recover on ARM, and a host build of the same source latched in every mode | An input guard drops non-finite samples and clamps the rest to ±16 (+24 dBFS), with the test on the exponent bits so `-ffast-math` cannot fold it. A watchdog checks the wet path and the detector state once per render; if anything has gone non-finite it clears the audio-rate state, keeping every parameter including the per-band Multiband values, and outputs one silent buffer instead of a dead unit (bench section R) |
| ATTACK and RELEASE did nothing in the gain smoother | Standard and Distressor built the smoother's coefficients with `fasterexpf`, a piecewise-linear fit that cannot exceed 0.9713 — a 0.7 ms time constant whatever the knob said — and Standard's smoother was written `y += c·(x − y)`, which with c near 1 jumps almost all the way every sample. The detectors carried the user's times instead, so ATTACK/RELEASE acted on the level rather than the gain, and the Distressor's Opto release and program-dependent release never did anything | One structure in every mode, as Multiband always had: the detector is a plain level follower (0.05 ms attack, 10 ms hold, 10 ms release) and the gain smoother carries ATTACK and RELEASE, once, exactly. Opto now really releases 5.7× slower (bench H1). Steady-state levels moved by at most 0.02 dB |
| Long releases froze gain reduction for good | `e_expff` forms `1 + x/1024` first, which rounds to exactly 1.0 once \|x\| < 3·10⁻⁵, so every time constant past ~0.7 s came out as a coefficient of 1.0: in Multiband, RELEASE ≥ 700 ms let a band's gain reduction deepen on every hit and never recover. Shorter ones were quantised (224 ms ran as 171 ms) | Every time-constant coefficient now comes from `ballistics_coeff()` (libm `expf`, parameter changes only) |
| Dist2 put DC on the bus and turned it into a square wave | Dist2 and Both are asymmetric and nothing removed their offset below the slam knee (up to +0.22 of DC, +25% of peak); and with no makeup a saturated shaper outputs its ceiling, so DRIVE was a +16–20 dB volume knob that ended in a full-scale square on the output limiter | `distressor_drive_output()`: a DC blocker and programme-dependent level matching behind every DstrDist shaper at every DRIVE (see *Drive stage output*). Bench S3 now sweeps the whole knob for DC instead of only DRIVE 100 |

Verified by `test_levels.cpp`, which drives the real `MasterFX::Process()` loop:

```
g++ -std=c++14 -O2 -I test_portable -I . -I ../common -o test_levels test_levels.cpp -lm
./test_levels
```

All three engines — Standard, Multiband and the shelved Distressor, which the
bench still selects through `setEngineMode()` — measure 0.00 dB insertion
gain, and all nine distortion types sit within 0.9 dB of each other at
DRIVE=0.

`./test_levels slam` covers the slam region on its own: the sweep across the
knee, the proof that Standard and Multiband are unaffected, that a host
replaying parameters in ID order arms it identically, and that nothing leaves
DC on the bus.

`./test_levels panel` is pass/fail and sets the exit status: every SoloMute
value plays exactly the bands it names, each threshold and ratio knob
compresses its own band and no other, each crossover moves its own split,
programs stored under the old COMP MODE numbering load a panel mode, no panel
value reaches the Distressor, and page-1 RELEASE reaches every band.

`./test_levels recover` is pass/fail and sets the exit status too: one NaN, ±Inf,
1e30 or 1e20 sample on the bus in each mode, with the output three seconds
later required to match an untouched run, and NaN planted directly in the
detector, tube, slam and crossover state to prove the watchdog clears it. The
NEON shim propagates NaN through `vmaxq_f32`/`vminq_f32` the way the hardware
does, tested on the bits, since the DSP is compiled `Ofast` and an `x != x`
test would be folded away.

## Future Expansion

The architecture supports easy addition of:

1. **More band parameters** (Attack, Release per band)
2. **Knee control** (0-100% softness)
3. **Detection mode** (Peak/RMS/Blend)
4. **Stereo link** adjustment
5. **Lookahead** (up to 10ms)
6. **Sidechain listen** mode
7. **8 factory presets** for common use cases

---

## Technical Specifications

| Specification | Value |
|---------------|-------|
| Sample Rate | 48 kHz fixed |
| Input Channels | 4 (Main L/R + Sidechain L/R) |
| Output Channels | 2 (Stereo) |
| Parameters | 24 |
| CPU Target | < 2% @ 1GHz |
| Memory | ~4 KB |
| Crossover | Linkwitz-Riley 24dB/oct |
| Drive | Overlord tube (Standard), per-band triode (Multiband) |

---

## Credits & Inspiration

- **Eventide Omnipressor** (1970s) - Reverse compression concept
- **Empirical Labs EL8 Distressor** - Ratio modes and harmonic distortion (shelved engine)
- **SSL Console** - Multiband architecture
- **SHARC Audio Elements** - DSP building block patterns

---

**OmniPress** brings studio-grade dynamics processing to the KORG drumlogue: an Omnipressor-style compressor and a three-band compressor in one unit.
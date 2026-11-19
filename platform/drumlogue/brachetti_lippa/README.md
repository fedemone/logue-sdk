# Brachetti Lippa (`BrcLippa`)

> **Disclaimer:** Brachetti Lippa is an unofficial, independently developed unit, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from its use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The tenth unit of the [Brachetti family](../tools/brachetti_equiv/README.md),
and its first drum made from a **delay loop** rather than from modes: a
Karplus-Strong loop with a Schroeder all-pass filter inside it, after Miguel
Moreno's article [*Realistic drums with karplus-strong synthesis*](https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/)
(see [Credits](#credits-and-sources)).  The modal units build a drum from at
most six modes per voice, the kernel and the cymbal from dense resonator
banks, BrcNoise from shaped noise; a loop has a resonance at every trip round
it, up to the top of the spectrum, for the cost of two delay-line reads, so
it rings with many partials at little CPU.  The all-pass moves those partials off the harmonic
series, which is what turns a plucked string into a struck head.  (Where it
moves them only a little the loop stays a string: five presets voiced here
came out that way, and are [BrcPizzico](../brachetti_pizzico/README.md)'s
strings now.)

It runs on the family's shared modal engine (`brachetti_common/modal_synth.h`)
like BrcWorld: the same 24-knob layout, preset loading, voice life cycle,
Punch and master stage, with the loop drum as its own layer.  Like BrcWorld it
was not split from the original `brachetti`: its presets were voiced after
recordings.

| | |
|---|---|
| Unit name on the drumlogue | `BrcLippa` |
| dev_id / unit_id | `0x46654465` ('FeDe') / `0x42724C70` ('BrLp') |
| Engine | Karplus-Strong loop + Schroeder all-pass, tension glide, snares on the head's motion |
| Presets | 8: 3 drums voiced after recordings, Miguel Moreno's 5 |
| Knobs | 24 (the modal units' layout, six slots the family's own) |
| ARM size (cross-built, `arm_size.sh`) | .text 23492 · .rodata 2400 · .data.rel.ro 80 · .data 4 · .bss 52784 |
| CPU (`arm_cpu.sh`) | 945.2 ARM instructions per rendered frame (an instruction count, for comparing this unit with itself); see "Restrikes and CPU on mymembrane~" |

## Presets

| # | Name | Root | Sound | Voiced after |
|---|---|---|---|---|
| 0 | Snare | 60 | acoustic snare: a short head, the wires on its motion | `acoustic-snare.wav` |
| 1 | SnrHvy | 47 | heavy snare: a deep, slack snare struck hard | `snare heavy.wav` |
| 2 | Bendir | 41 | bendir: a frame drum with a snare string across the head | `020_BendirDoum.wav` (Pelle e Legno) |
| 3 | MmSnare | 54 | mymembrane~'s help patch: a snare-like drum, its snares on | Miguel Moreno's settings |
| 4 | MmKick | 32 | mymembrane~ demo 1: a dark, deep drum, the all-pass at full gain | Miguel Moreno's settings |
| 5 | MmSnr2 | 45 | mymembrane~ demo 2: a lower snare-like drum | Miguel Moreno's settings |
| 6 | MmTom | 48 | mymembrane~ demo 3: a tom, the all-pass's gain negative | Miguel Moreno's settings |
| 7 | MmFloor | 41 | mymembrane~ demo 4: a long, bright floor drum | Miguel Moreno's settings |

Presets 0-2 are voiced after recordings: Snare and SnrHvy after the original
brachetti's reference recordings — its `samples/` folder, on the
`original_brachetti` branch (the same files its AcSnre was tuned to) — and
Bendir after a [Pelle e Legno](../pelle_legno/) sample.  Five more presets
were voiced the same way, after a rack tom, a floor tom, a kick, a taiko and
a surdo, and rang like plucked strings instead, which is what a
Karplus-Strong loop is at heart (periodicity 0.87-0.94 against their
recordings' 0.20-0.82, below, "From drums to strings").  They sounded good
as strings, so they became strings, at the exact pitch of their roots, and
moved to a unit of their own, [BrcPizzico](../brachetti_pizzico/README.md),
byte for byte.

Presets 3-7 are Miguel Moreno's own drums: the settings of mymembrane~'s
help patch and its demo's four instruments, played by a port of his patch
("mymembrane~ in BrcLippa", below); each root is the note nearest its
strongest partial.

## The loop drum

One voice is the family's exciter (a mallet and a noise burst, mostly silent
here: `exciter_gain`), an optional modal bank (no preset uses it), and the
loop drum (`loop::Drum` in `voice.h`):

```
            excitation e[n] ───────────────────────────────┐
                                                           ▼
  ┌──► delay N(t) ──► one-pole LP ──► DC block ──► × G (± flip) ──► all-pass M(t), g ──► (+) ──► y[n] ──► out
  │     (linear interp.)  loss filter                  K-S blend                     │
  └──────────────────────────────────────────────────────────────────────────────────┘
                                                      y[n] ──► follower ──► × noise ──► band-pass ──► + out
                                                                                (the snares)
```

- **The loop** (Karplus & Strong 1983; Jaffe & Smith 1983).  A delay line
  read with linear interpolation, a one-pole low-pass `lp += a·(x − lp)` as
  the loss filter, and a gain per trip `G` set from the preset's T60 at the
  note.  The loss filter's and the DC blocker's own attenuation at the note
  is given back, and `G` is capped at 0.9995: every filter in the loop has a
  gain of at most 1, so no knob can make the loop grow.
- **The loss filter from two decay times** (after Välimäki, Huopaniemi,
  Karjalainen & Jánosy 1996).  A preset gives the T60 at the note and, as a
  fraction of it (`lp_hf`), the T60 at 3 kHz; the strike solves for the
  one-pole that loses that much more at 3 kHz per trip.  With the ratio `r`
  of the two per-trip attenuations, `|H(ω₁)|² / |H(ω₀)|² = r²` is a quadratic
  in the pole: `b = B − √(B² − 1)`, `B = (cos ω₀ − r²·cos ω₁) / (1 − r²)`.
  A fixed coefficient (the article's "damping", and BrcWorld's wire) loses
  the same per TRIP, and a 58 Hz loop makes 58 trips a second against a
  240 Hz loop's 240: the first voicing search, with one, left the surdo's
  and the taiko's highs ringing for seconds like a steel string's.  Designed
  from times, the highs die in the time the preset says at every pitch.
- **The all-pass** (Schroeder; the "all-pass from two combs" of J. O. Smith's
  *Physical Audio Signal Processing*).  `v[n] = u[n] + g·v[n−M]`,
  `y[n] = −g·v[n] + v[n−M]`: flat in magnitude, but its delay depends on
  frequency, periodically in ωM.  Inside the loop it pulls each resonance a
  different distance off the harmonic series.  How they spread depends on
  the ratio of `M` to the loop's period (`ap_ratio`, the `ApDelay` knob; the
  article's "all-pass frequency") and on `g` (`ap_gain`, the `Inharm` knob;
  the article's "all-pass gain", ±0.95 here).
- **The DC blocker** (`x − lp₈ Hz(x)`, inside the loop).  A positive loop
  holds DC as its lowest "resonance", and the excitation's noise has some;
  without it a struck preset would drift on a slowly decaying offset.
- **The excitation** (the article's).  One sine cycle `exc_ratio` periods
  long (the article: one period) plus white noise, low-passed at
  `exc_noise_hz` and shaped by the cycle's Hann window, fed into the loop
  (and heard directly: the attack).  Both run on rotating phasors, so a
  strike costs no `sinf` per sample.  A stiffer stick (`MlltStif`, and
  `VlMllStf` with velocity) shortens the cycle: a brighter strike.  The
  strike position (`exc_pos`, `HitPos`) subtracts the same cycle `pos ×
  period` later — the comb filter of a plucked string's pick position
  (Jaffe & Smith) — which thins the fundamental the way a strike off the
  centre does.
- **The glide** (the article's "pitch bend").  Both delays, the loop's and
  the all-pass's, are scaled by `1 / (1 + bend·env)`, `env` falling from 1
  with time constant `bend_ms`: the strike starts `bend` above the note and
  settles, as a head's tension rises with a hard stroke and relaxes.  `bend`
  scales with velocity.  A fast, deep glide is also a chirp that sharpens the
  attack, which the article points out.
- **The snares** (the article's).  An envelope follower (attack `sn_att_ms`,
  release `sn_rel_ms`) on the loop's output sets the level of a noise
  through a two-pole band-pass (`sn_hz`, `sn_q`; 3 kHz in the article): the
  wires rattle as long as, and as hard as, the head moves.  The noise is
  white blended towards `brc::crackle` (`sn_crackle`): mymembrane~'s, see
  below.  (BrcSnare's wires ring on the exciter's noise burst instead;
  BrcWorld's balafon `Buzz` is the same idea as this, gated per sample by
  the bank's motion; BrcCordiera puts BrcSnare's wires on the head.)
- **The Karplus-Strong drum blend** (Karplus & Strong 1983, not in the
  article).  With probability `b` a trip round the loop flips sign: at
  `b = 0.5` the loop's tone turns into a pitched noise burst, Karplus and
  Strong's own drum.  `blend` in the table, and the `Blend` knob adds to it.

### What the Pd code adds: mymembrane~

The article describes `[mymembrane~]`; its patch, in
[pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr) (`instruments/mymembrane~.pd`,
BSD-3-Clause), has the details the article leaves out.  Read object by
object:

| | mymembrane~ | BrcLippa |
|---|---|---|
| loop | `vd~` → `hip~ 20` → × feedback → `tanh` → + excitation → all-pass → `lop~`; the delay set from the frequency alone, so the all-pass and filters pull the pitch | delay → loss filter → DC blocker → gain → all-pass; the delay solved so the note is exact (Tuning) |
| loss | `lop~` at f · 2^(6·damping) / 2, a fixed coefficient per pitch | the one-pole designed from two T60s (above) |
| all-pass | delay 1 / apf (the demo's four instruments: 0.6-1.6 periods), gain −1 .. 1 | `ap_ratio` (presets 0.16-0.44), gain ±0.95 |
| excitation | one sine cycle (`vline~` → `cos~` of the ramp − 0.25), plus `noise~` (re-seeded identically at every strike) through a low-pass at f · 2^(3·velocity) / 3, under a rectangular window one period long | one sine cycle and Hann-windowed noise, the noise's corner a preset value |
| pitch envelope | the delay's frequency × (1 + (2 + velocity) · env¹⁰), the all-pass's × (1 + (1 + velocity) · env²⁰), env a ramp 1 → 0 over 80 ms: the strike starts 3-4× the note and falls in ~10 ms — an attack chirp | `bend` (≤ 35 % on the presets) falling with `bend_ms` |
| loop boost | × (1 + 0.7·√velocity · env²⁰) during the chirp | — |
| snares | `noise~` → `pow~ 50` → `hip~ 100` → `bp~ 3000 1`, × the RMS envelope (`env~`) of the drum | the band-pass on white noise blended with the same crackle (`sn_crackle`), × a peak follower |
| retrigger | the output ramps to 0 in 1 ms, the delays are cleared, the strike follows 2 ms later | a ringing loop keeps its lines (below) |

Three things came into BrcLippa from it: the whole patch, ported (the next
section), and two pieces for the loop drum:

- **The crackle.**  `pow~ 50` of uniform noise is near 0 but for rare spikes
  where |x| is near 1: a sparse rattle, where white noise is a hiss.  It is
  `brc::crackle` in `brachetti_common/noise.h` (its mean taken off and its
  RMS put on white noise's, so a blend keeps the level), and `sn_crackle`
  blends it in.  The band energies the voicing compares cannot tell crackle
  from hiss — the same band-pass, the same spectrum — so the voicing gained a
  measure of it, the kurtosis of the 2-8 kHz band (How the presets were
  voiced): the recordings' snares read 3.45 (Snare), 3.46 (SnrHvy) and 9.71
  (Bendir's gut snares) against white noise's 3; with `sn_crackle` 0.29, 0.30
  and 0.85 the presets read 3.45, 3.47 and 9.71.
- **Restrikes without a click.**  A voice slot struck again while its loop
  rings (Poly reached, or a long ring) used to clear the delay lines: the old
  ring stopped dead.  mymembrane~ fades it out over 1 ms first.  BrcLippa
  keeps it instead (`loop::Drum::Motion`): the lines, their positions and the
  filters' states stay, and the new strike adds to the head's motion, as a
  second stroke on a real drum does.  The glide would jump both delays at the
  new strike — a jump in the read position inside a ringing waveform — so on
  a restrike its factor slews over ~1 ms from where the old ring had it.
  Measured (Poly 1, a strike at velocity 127, a second at 20 after 300 ms; the
  largest sample step in the first three samples of the second strike, against
  the same strike on a silent voice): the step is now the strike's own attack
  (Bendir 0.264 against 0.264; the floor tom and the kick, now BrcPizzico's
  Oud and Fretls, 0.092 against 0.095 and 0.116 against 0.120); before, a cut
  ring added to it (the surdo, the rack tom and the taiko, now BassPiz, Harp
  and Guitar, 0.140, 0.033 and 0.020 on the old code; now 0.011, 0.006,
  0.010).  A fresh strike is unchanged.  (mymembrane~'s restrikes needed one
  more fix: "Restrikes and CPU on mymembrane~", below.)

The rest stays as it was, on purpose: the exact tuning (mymembrane~'s pitch
moves with every filter setting), the two-T60 loss, and a glide of tension
rather than a 3-4× chirp (BrcLippa's presets are voiced after real drums).
`Bend` at its top starts a full-velocity strike 1.3× (Snare) to 1.46×
(SnrHvy, Bendir) the note, and `BendTm` at its bottom makes the fall 4×
faster: towards mymembrane~'s chirp.

### mymembrane~ in BrcLippa

`mm::Membrane` (`voice.h`) is the patch, object by object, with Pd's own
formulas for the objects it uses: `noise~`'s generator (re-seeded with 16 at
every strike, as the patch does), `hip~`, `lop~`, `bp~` (with its own cosine
approximation), `biquad~` with the coefficients of the patch's `lowpass~`,
`vd~`'s 4-point interpolation and its minimum of one block in a feedback loop,
`env~`'s Hann-windowed RMS over 1024 samples every 512 (computed without a
buffer: each sample adds to the two half-windows it belongs to), the `line~`
after it, and the patch's rational `hv.tanh`.  A preset plays it in place of
the loop drum (`mm_level` > 0) and reads the patch's own controls
(`mm_hz`, `mm_feedback`, `mm_damping`, `mm_apgain`, `mm_apf_hz`, `mm_snares`),
with the patch's velocity curves.  It shares the loop drum's delay lines.

Checked against Pd itself
([`mymembrane_check.py`](../tools/brachetti_equiv/mymembrane_check.py)): Pd 0.54
rendered the patch headless (`pd -nogui`, 48 kHz) with its help and demo
settings at velocities 127 and 90, and the five presets, struck through the
unit's own path, compare as:

| Preset (setting) | waveform correlation, first 200 ms (v127 / v90) | t40, ms (unit / Pd) | periodicity (unit / Pd) |
|---|---:|---:|---:|
| MmSnare (help) | 0.937 / 0.960 | 180 / 180 | 0.56 / 0.55 |
| MmKick (demo 1) | 0.999 / 0.999 | 190 / 190 | 0.65 / 0.65 |
| MmSnr2 (demo 2) | 0.981 / 0.982 | 230 / 210 | 0.72 / 0.72 |
| MmTom (demo 3) | 0.966 / 0.969 | 470 / 475 | 0.94 / 0.94 |
| MmFloor (demo 4) | 0.971 / 0.975 | 1020 / 1000 | 0.88 / 0.88 |

The residue is the snares' noise (a different `noise~` instance, unseeded in
the patch) and float rounding through the tanh.  Three things differ on
purpose: a strike into a ringing voice keeps the ring and slews the delays
over 1 ms (mymembrane~ ramps its output to 0 over 1 ms, clears its lines and
strikes 2 ms later), its attack boost then shrinks with the ring's level,
and a snare level under 0.005 is not run (both below, "Restrikes and CPU on
mymembrane~"); and the knobs reach it, each anchored: `Dkay` the
feedback (as a T60), `Mterl` the damping's corner, `Inharm` and `ApDelay`
the all-pass, `MlltRes` the level, `MlltStif`/`VlMllStf` the excitation's
length, `NzMix`/`NzFltFrq` its noise, `Bend`/`BendTm` the chirp's depth and
length, `Snares`/`SnrTone` the snares, `Blend` the Karplus-Strong sign flip.
`HitPos` has nothing to move (the patch has no strike position).

**From drums to strings.**  The loop drum's rack tom, floor tom, kick, taiko
and surdo rang like strings (periodicity 0.87-0.94 against their recordings'
0.20-0.82).  They were voiced again on mymembrane~ against the same
recordings, starting from the nearest of Moreno's settings, with the
periodicity in the score (the `lippa_mm` specs, in `match_specs.py` at commit
8faf63a).  Periodicity came down where it mattered most — the rack tom 0.94 →
0.71 (the recording's 0.71), the taiko 0.94 → 0.66 (0.20) — but not on the
kick (0.87 → 0.84, against 0.23) or the floor tom (0.94 → 0.86, against
0.61), and every one matched its recording worse on the rest (energy over
time above all: the rack tom 83 % in its first 30 ms against the recording's
46 %; scores 1.47 → 1.54, 1.50 → 1.71, 2.12 → 2.57, 2.37 → 2.66, 0.45 →
0.46).  So they stayed on the loop and became strings: BrcPizzico's five
([its README](../brachetti_pizzico/README.md), "Where the strings come
from").  Making drums of those recordings is left for later: Moreno's sound
is here as his own five presets, and `match_specs.py` keeps `mm_spec`, the
start of a voicing on his loop.

### Restrikes and CPU on mymembrane~

**The click.**  MmFloor clicked when it was struck again quickly: a sharp
broadband tick on top of each new strike.  It was the restrike, a strike on
a voice slot whose drum still rings (with Poly 4, any pattern faster than
four strikes per ring reuses one, and MmFloor rings for a second).  The
voice keeps its ring (above), and mymembrane~'s attack multiplies its loop
by up to 1 + 0.7·√v (×1.7 at full velocity) for its first ~10 ms — meant
for the new excitation, in a loop the patch has just cleared.  Here the loop
also holds the old ring, and boosting a loud ring drives it into the patch's
tanh: the tick.  Found with
[`restrike_check.py`](../tools/brachetti_equiv/restrike_check.py) (a second
strike on the voice still ringing, against the same strike on a silent voice
plus the old ring alone; the peak above 6 kHz in the strike's first 10 ms,
through the master stage), and taken apart: without the boost the restrike
was clean, without the chirp it was not.  Now the boost shrinks with the old
ring's level (`Membrane::resume`): whole on a silent or quiet voice, gone
from a ring at −20 dB (`kRingNoBoost`, 0.1, on the drum's follower) up.  A
fresh strike is unchanged, so the Pd comparison above is too.

| MmFloor restruck after | 30 ms | 60 ms | 150 ms | 300 ms | 600 ms |
|---|---:|---:|---:|---:|---:|
| before | 2.94 | 2.91 | 2.08 | 0.93 | 0.97 |
| now | 0.96 | 0.98 | 1.07 | 1.03 | 0.97 |

(restrike / (strike + ring); a clean restrike is at or below 1.)  Every
other preset of this unit and of BrcPizzico reads at most 1.21 (SnrHvy's
wires) at every gap, as before.

**The CPU.**  Clicks can also be the drumlogue running out of time, and
mymembrane~ was the heaviest voice in the family's loop units: about 480
instructions per voice per sample, four of them divisions.  On the A7 a
division costs about fourteen cycles, which `arm_cpu.sh`'s instruction count
does not show; [`arm_cycles.sh`](../tools/brachetti_equiv/arm_cycles.sh)
weights them.  Three cuts, the first two bit-exact:

- the delays' `kSR / f` and `kSR / apf` and their clamps, computed every
  sample, are computed once at the strike (`dl_rest`, `da_rest`; the chirp
  still computes its own);
- the family's mallet and noise burst ran on every mymembrane~ voice, at a
  gain of 0, sharing nothing with it (the loop drum takes its excitation
  noise from the burst's generator; mymembrane~ has its own): it is skipped
  there, its clock kept for the squelch;
- a snare level under 0.005 (`kMmSnaresMin`) is not run: MmFloor's 0.001,
  Moreno's demo 4 setting, put its snares 69 dB under the drum (peak; 84 dB
  RMS) for a third of the voice's cost.  MmFloor is the one preset whose
  render changed, by exactly its snares; the other four keep theirs (26-53
  dB under their drums).

| Estimated A7 cycles per frame (`arm_cycles.sh`, 400 ms) | before | now |
|---|---:|---:|
| MmFloor, four voices | ~2240 | ~1510 |
| MmFloor, one voice | | ~430 |
| MmTom, four voices (snares on) | | ~1890 |
| BrcCymbal's Gong, two voices, for scale | | ~1810 |
| BrcKernel's Timpni, two kettles, for scale | | ~1900 |

Four MmFloor voices now cost less than two of the cymbal's gongs or the
kernel's kettles; before, they cost a fifth more.  (BrcKernel's two full
kettles, before its cost bound, were measured at about twice what is known to
run on the drumlogue, and were heard as clicks: the kernel's
`modal_drum_kernel.h` has the story.)  Not yet measured on hardware.

### Tuning

A Karplus-Strong loop sounds at `fs / (total delay)`, where the total is the
delay line plus every filter's phase delay at that frequency.  The all-pass
adds a lot, and differently at every frequency, so `strike_loop` sets the
delay line so that the loop's phase at the note's ω₀ is exactly one period:

```
N = 2π/ω₀ − P_ap(ω₀) − P_lp(ω₀) + Lead_dc(ω₀)

P_ap(ω) = (ωM + 2·atan2(g·sin ωM, 1 − g·cos ωM)) / ω        all-pass phase delay
P_lp(ω) = atan2(b·sin ω, 1 − b·cos ω) / ω,  b = 1 − a        loss filter
Lead_dc(ω) = ((π − ω)/2 − atan2(b·sin ω, 1 − b·cos ω)) / ω    DC blocker (a lead)
```

The loop's total phase rises monotonically with frequency (an all-pass's
phase always falls), so this is its first resonance and the only one at ω₀.
Where the all-pass alone would take more than a period (`M` long and `g`
near 1: its low-frequency delay is `M·(1+g)/(1−g)`), `M` is shortened until
the line keeps at least two samples.  Measured (velocity 127, after the
glide; `Inharm`, `ApDelay` and `Mterl` at both ends, three octaves of
notes): the first resonance within 2 cents of the note, except at `Inharm`
+100 (g = 0.95), where a crowd of neighbouring resonances makes the strongest
one near the note read up to 40 cents off on low notes.

## Knobs

Pages 1-2 hold the **common knobs**: the same slots, names and meaning on all twelve Brachetti units.  Pages 3 onward hold this family's **dedicated knobs**.

| Page | | Knobs |
|---|---|---|
| 1 | common | `Program` · `Pitch` · `Poly` · `Velocity` |
| 2 | common | `Tone` · `Cutoff` · `Resnc` · `Gain` |
| 3 | dedicated | `MlltRes` · `MlltStif` · `Bend` · `VlMllStf` |
| 4 | dedicated | `BendTm` · `Snares` · `Dkay` · `Punch` |
| 5 | dedicated | `Mterl` · `HitPos` · `Inharm` · `ApDelay` |
| 6 | dedicated | `NzMix` · `Blend` · `SnrTone` · `NzFltFrq` |

### Common knobs

| Knob | |
|---|---|
| `Program` | the preset |
| `Pitch` | semitones from the preset's root: the loop follows the played note, exactly in tune (see Tuning) |
| `Poly` | voice cap 1-4; every strike stacks |
| `Velocity` | velocity bias: ghost (−) … as played (0) … wham (+) |
| `Tone` | tilt EQ |
| `Cutoff` / `Resnc` | master low-pass and its Q |
| `Gain` | drive |

`Pitch`, `Poly`, `Velocity`, `Tone`, `Cutoff` and `Resnc` are performance knobs the host owns: a Program change leaves them where they are.  `Gain` loads with the preset.  The common knobs are [`brachetti_common/common_knobs.h`](../brachetti_common/common_knobs.h).

### Dedicated knobs

Every mapping is REFERENCE-ANCHORED, as on every Brachetti unit: a factor of
exactly 1 (or an offset of exactly 0) at the preset's shipped value, so a
preset plays as voiced until a knob moves.  The standard knobs keep the
meaning they have on the other modal units and act on the loop with it; six
slots are BrcLippa's own (they ship 0):

| Knob | On the loop | Up turns it | Range |
|---|---|---|---|
| `MlltRes` | **presence**: the loop's level (as it is the bank's) | louder | 3.4 octaves per unit |
| `MlltStif` | **stick**: the excitation cycle's length (and the mallet's stiffness) | shorter, brighter | ×¼ .. ×4 |
| `Bend` *(own, VlMllRes' slot)* | the glide's depth; at its top it adds a 30 % glide where a preset has none | deeper | ×¼ .. ×4 |
| `VlMllStf` | velocity → stick: a harder hit shortens the cycle further (and stiffens the mallet) | more | 2 octaves per unit × velocity |
| `BendTm` *(own, Partls' slot)* | the glide's time | slower | ×¼ .. ×4 |
| `Snares` *(own, Model's slot)* | the snares' level; at its top it adds snares (3 kHz, Q 2) where a preset has none | louder | ×¼ .. ×4 |
| `Dkay` | **length**: the loop's T60 (as the bank's ring) | longer | 4.5 octaves per unit |
| `Punch` *(Rel's slot)* | the family's transient shaper (below) | | |
| `Mterl` | **brightness**: the T60 at 3 kHz against the note's (`lp_hf`) | brighter, the highs ringing longer | 3 octaves per unit |
| `HitPos` | **strike position**: the excitation's comb, from the centre towards the rim | thinner fundamental | half a period per unit |
| `Inharm` | **inharmonicity**: the all-pass gain, an offset | further from harmonic (− the other way) | ±0.9 per unit, clamped ±0.95 |
| `ApDelay` *(TubRad's role, relabelled)* | the all-pass delay against the period | longer delay: a different spread | ×0.35 .. ×2.8 |
| `NzMix` | the noise in the excitation (as it is the burst's) | noisier strike | 3 octaves per unit |
| `Blend` *(own, NzRes' slot)* | Karplus & Strong's drum: the chance a trip flips sign, added to the preset's | noisier tail | 0 .. +0.5 |
| `SnrTone` *(own, NzFltr's slot)* | the snares' band | higher | ±1 octave |
| `NzFltFrq` | the excitation noise's corner (as it is the burst's filter) | brighter noise | proportional |

The displaced standard knobs play at fixed values: VlMllRes 0, Partls 2
(the bank's own mode count), Model `Drumhd`, Rel each preset's shipped value,
NzRes 30 %, NzFltr low-pass.

### Punch: the transient shaper

`Punch` is a gain curve over each voice, started by its strike.  **Up**, the first 5 ms pass untouched and then the body drops (τ 15 ms), down to −15 dB at the top of the knob.  **Down**, the voice fades in from almost nothing over 2-20 ms: a rounder, softer strike.  At **0**, where every preset ships, it is off and the preset plays exactly as voiced.  It never boosts the attack (the unit's peak limiter would take a boost straight back off).  It is `brc::Punch`, shared by all twelve units.

### Knob review

Measured with [`knob_liveness.py`](../tools/brachetti_equiv/knob_liveness.py)
(each knob to both ends on every preset, mean dB change over bands and time).
With the standard layout, four knobs did nothing on any preset: `VlMllStf`,
`NzRes`, `NzFltr` and `NzFltFrq` act on the noise burst, which these presets
barely use.  `NzRes` and `NzFltr` gave their slots to `Blend` and `SnrTone`;
`VlMllStf` and `NzFltFrq` kept their jobs and now reach the loop's
excitation too.  `Mterl` and `ApDelay` got steeper curves (they moved
under 1.5 dB on half the presets).  As shipped:

```
=== brachetti_lippa   (mean |dB| change at the knob's ends; '.' < 0.3, ':' < 1.5)
           Pitch    Poly Velocit MlltRes MlltSti    Bend VlMllSt  BendTm  Snares    Dkay   Punch   Mterl  HitPos  Inharm ApDelay   NzMix   Blend SnrTone NzFltFr
Snare        6.7       .    10.0    11.4     3.2    :0.7     6.3       .     6.1    17.2     7.5    :0.5     3.1     6.8       .       .    18.1     2.8       .
SnrHvy       4.9       .     9.9    11.3     2.1     2.0     4.3       .     5.7    10.2     7.5    :1.2     2.8     3.0     4.3     3.4     9.9     3.2    :0.6
Bendir       6.3    :0.6     9.0    10.1     3.6    :0.7     5.1       .     6.5    10.3     6.5     2.4     2.5     4.5     4.7     5.2    10.9     1.8     2.3
MmSnare     11.0       .     9.0     8.5     1.9     2.2     2.7     3.2     4.2     7.4     4.7     2.9       .     6.7    12.2     4.7     7.8    :1.4     1.9
MmKick       6.6       .     5.2     4.6     1.8    :1.3     2.7    :1.3     4.1     1.7     2.0     6.4       .    :1.2       .     3.2     4.3    :0.5     2.4
MmSnr2      10.8       .     8.6     8.5     2.3     2.1     3.5     2.7     5.3     9.1     5.0     3.4       .     5.1     7.8     4.3    10.5     1.6     1.6
MmTom       14.0       .     8.2     7.6     2.4     2.7     3.8     3.8     7.2     7.8     3.9     7.4       .     8.6     6.7     4.2    11.9    :0.6     3.1
MmFloor     19.5    :0.3     7.8     8.6     3.7     2.8     4.6     2.8     7.5     9.1     5.7     6.1       .     4.0     2.9     6.6    19.7       .     4.5
```

`BendTm` is small where a preset's glide is (it times a glide that is barely
there: raise `Bend` first), and `SnrTone` is silent on the presets without
snares (raise `Snares` first).  On Snare, `NzMix` and `NzFltFrq` scale an
excitation noise the preset barely uses, and `ApDelay` an all-pass whose
gain it ships near 0 (raise `Inharm` first).  On Moreno's five, `HitPos` does
nothing (mymembrane~ has no strike position).

## How the presets were voiced

As BrcWorld's and BrcTom's added presets:
[`match_samples.py`](../tools/brachetti_equiv/match_samples.py) with the
`lippa` specs of [`match_specs.py`](../tools/brachetti_equiv/match_specs.py)
walks each preset's free values downhill against its recording (energy over
time, band balance over time, 40 dB decay, strongest partial), comparing the
voice before the master limiter; trims by the family's body-target method
(`preset_probe calib` reprints them).  Each preset starts with its loop at
the recording's strongest partial (its root a note near it) and its T60
from the recording's decay.  (The five that rang as strings are BrcPizzico's
now, with their specs.)  Three changes to the tool came with it, all inert
for the other units:

- **References at 44.1 kHz** are resampled to 48 kHz (several of the
  original's recordings are 44.1 kHz).  `load` also scaled integer WAVs by
  1/32768 after converting them to float, which never applied; it does now
  (the features are level-independent, so no score moves).
- **A spec may ask for each band's decay** (`late`, `band_decay`): the
  energy of the six bands from 300 ms to 1 s against their own in the first
  100 ms, in decades, weighted 0.5.  The default features cannot see a body
  whose highs ring too long — their spectral windows end at 300 ms, and in a
  later window the band SHARES are the fundamental's in any drum — and the
  first search used that to open the loss filter: the highs rang on for a
  second like a steel string's, which the recordings' do not.  (The second
  half of the fix was the loss filter itself: see "The loop drum".)
- **A spec may ask for the snares' kurtosis** (`sparsity`, `band_kurtosis`):
  the 2-8 kHz band in 20 ms frames over 20-200 ms, each frame normalised, its
  kurtosis compared as a log ratio (weight 1).  Without it the search cannot
  set `sn_crackle` (see "What the Pd code adds"); the lippa specs turn it on
  where a preset has snares.

```
BRACHETTI_ORIG=/tmp/brachetti_orig/platform/drumlogue/brachetti \
    python3 match_samples.py match_specs.py lippa --state loop.json
```

Measured, preset vs recording (velocity 110, sequencer gate):

| Preset | score | t40, ms | strongest partial, Hz | periodicity |
|---|---:|---:|---:|---:|
| Snare | 0.993 | 225 / 225 | 264 / 264 | 0.81 / 0.50 |
| SnrHvy | 0.737 | 190 / 190 | 127 / 127 | 0.72 / 0.70 |
| Bendir | 0.703 | 240 / 240 | 101 / 202 | 0.81 / 0.49 |

The score includes the band-decay term (above), and on the three presets
with snares the snares' kurtosis (`sparsity`); without them the drums score
0.52-0.74, where BrcWorld's measure 0.41-2.50 against their samples.  Bendir's loop sits at 101 Hz, between the recording's doum
(87 Hz) and half its strongest partial (202 Hz); the score counts an octave
as a near-miss.  Snare and SnrHvy match their decay and pitch but not their
energy over time (e30: 71 % in the first 30 ms against the recording's
92 %, 73 % against 33 % — SnrHvy's recording is cut at 0.25 s).

**What a loop cannot do.**  On the spectrograms the recordings' highs are a
dense, noise-like cloud; the loop's are discrete partials — one all-pass
spreads them, but a membrane's mode density grows with frequency and a
loop's does not.  The search uses the excitation's noise, the snares and the
two-T60 loss filter to make up for it in the first 100 ms; after that a
BrcLippa drum is a cleaner, more tonal drum than the recording — and where
the all-pass spreads the partials least, a string (BrcPizzico).  That is the
character of the technique, and where it is useful beside the modal units:
tuned drums that glide, ring clean and cost little.

## Build

```
build drumlogue/brachetti_lippa
```

(see [the drumlogue README](../README.md) for the Docker build environment).
The product is `brachetti_lippa.drmlgunit`; copy it to the drumlogue's
`Units/Synths` folder.  On hardware so far: MmFloor clicked when struck again
quickly, fixed since ("Restrikes and CPU on mymembrane~"); the fix is not yet
heard on the drumlogue.  The five strings it once held are in
`brachetti_pizzico.drmlgunit` now: install both.

## Verification

There is no original to compare with.  What was checked on the host:

- **Tuning**: the first resonance against the note, `Inharm` and `ApDelay`
  at both ends, three octaves (see Tuning).
- **mymembrane~**: the port against Pd's own renders of the patch (above,
  "mymembrane~ in BrcLippa").
- **The strings' move to BrcPizzico**: every preset that stayed renders
  byte-identically to before in its new place (velocities 110 and 60; MmFloor
  against the render with its snares off, below), every string in BrcPizzico
  likewise, and `match_samples.py --rewrite` rebuilds both tables from the
  specs byte for byte.
- **The restrike fix and the CPU cuts** ("Restrikes and CPU on mymembrane~"):
  fresh strikes byte-identical on every preset but MmFloor, which is byte for
  byte the old code with its snares off; `mymembrane_check.py` still matches
  Pd; `restrike_check.py` passes every preset.
- **Stability**: 3000 random sessions — every preset, every knob anywhere in
  its range, random notes and velocities, up to four stacked strikes,
  AllNoteOff — with every output sample finite and within the limiter's
  ±0.99, and every loop's state bounded.
- **Identity at the anchors**: re-voicing the knob slots (the knob review)
  left every preset's score and trim unchanged to the last digit; adding the
  crackle and the restrike left every preset's single-strike render
  byte-identical until the snares were revoiced.
- **Restrikes**: see "What the Pd code adds" and "Restrikes and CPU on
  mymembrane~".
- **Units loaded together**: `multi_load.sh` (all twelve units, 512,206
  display strings, identical alone and together); `arm_size.sh` finds no
  unique symbol.
- **Its unit id.**  The first commit of this unit (as BrcLoop) carried
  BrcWorld's `unit_id` ('BrWd'), left over from the header it started from:
  on a drumlogue the two would have collided.  It is 'BrLp' now, and
  `multi_load.sh` fails any set of units with a duplicated id.
- `lib_uses.py --check`.
- **The other units**: the library changes that came with this unit are
  comments; `run_equiv.sh` finds the eight split units IDENTICAL to the
  original, and BrcWorld's renders are byte-identical to before.

## Code

| File | What it holds |
|---|---|
| `presets.h` | the presets, the family's knob slots, each preset's loop settings and output trim |
| `voice.h` | one voice: the exciter, the modal bank and the loop drum |
| `synth_engine.h` | the strike (the loop tuned and set up from the table, the knobs on it) and the per-sample voice; the delay lines |

Shared with the other units: [`brachetti_common`](../brachetti_common/)
(`modal_synth.h` is the engine).

## Credits and sources

- **Miguel Moreno**, *Realistic drums with karplus-strong synthesis*, Mike
  Moreno DSP, 13 March 2022 —
  <https://mikemorenodsp.github.io/posts/Realistic-drums-with-karplus-strong-synthesis/>
  — and its Pure Data instrument `[mymembrane~]` in
  [pd-mkmr](https://github.com/MikeMorenoDSP/pd-mkmr).  The structure of this
  unit's drum is his: the all-pass inside the Karplus-Strong loop as the
  source of a drum's inharmonicity, the pitch envelope on both delays, the
  sine cycle plus windowed noise as the excitation, and the snares as noise
  following the head's envelope.  The article publishes no code or
  coefficients; the loop drum's C++, its tuning and its voicing are this
  repository's.  `mm::Membrane` is a port of the patch itself
  (`instruments/mymembrane~.pd`), and presets 3-7 are its help and demo
  settings; pd-mkmr's 3-Clause BSD License covers that part (below), and
  `brc::crackle` comes from the same patch.  The article carries no licence
  beyond its copyright notice; what is used from it is its description of a
  technique, credited here.
- **Kevin Karplus and Alex Strong**, "Digital Synthesis of Plucked-String and
  Drum Timbres", *Computer Music Journal* 7(2), 1983 — the loop, and the drum
  blend (`Blend`).
- **Vesa Välimäki, Jyri Huopaniemi, Matti Karjalainen and Zoltán Jánosy**,
  "Physical Modeling of Plucked String Instruments with Application to
  Real-Time Sound Synthesis", *Journal of the Audio Engineering Society*
  44(5), 1996 — the loss filter designed from decay times.
- **David A. Jaffe and Julius O. Smith**, "Extensions of the Karplus-Strong
  Plucked-String Algorithm", *Computer Music Journal* 7(2), 1983 — the
  tuning by phase delay, the loss filter, the pick-position comb (`HitPos`).
- **Julius O. Smith III**, *Physical Audio Signal Processing*, CCRMA,
  Stanford — "Extended Karplus-Strong Algorithm" and "Allpass from Two
  Combs", the two pages the article cites.
- **Manfred R. Schroeder and Benjamin F. Logan**, "'Colorless' Artificial
  Reverberation", *IRE Transactions on Audio* AU-9(6), 1961, and
  **M. R. Schroeder**, "Natural Sounding Artificial Reverberation", *Journal
  of the Audio Engineering Society* 10(3), 1962 — the all-pass comb.
- Universal Audio, "Allpass Filters" (blog), cited by the article for the
  all-pass.

pd-mkmr is distributed under the 3-Clause BSD License; `mm::Membrane` is a
port of `mymembrane~`, presets 3-7 are its settings, and the crackle
(`brc::crackle`) is taken from its snare layer, so its conditions apply to
those parts:

> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
> 1. Redistributions of source code must retain the above copyright notice,
> this list of conditions and the following disclaimer.  2. Redistributions in
> binary form must reproduce the above copyright notice, this list of
> conditions and the following disclaimer in the documentation and/or other
> materials provided with the distribution.  3. Neither the name of the
> copyright holder nor the names of its contributors may be used to endorse or
> promote products derived from this software without specific prior written
> permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
> ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
> LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
> CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
> INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
> CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
> ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
> POSSIBILITY OF SUCH DAMAGE.

(The license file in pd-mkmr names no copyright holder; the patch's help file
reads "mymembrane~ v1, Miguel Moreno 2019".)

## What the article gave the other units

Two of the ideas above would improve units that are bit-identical to the
original (BrcSnare) or checked against their own renders (BrcWorld), so they
went into a family of their own, [BrcCordiera](../brachetti_cordiera/README.md),
on those units' engines, with instruments of its own (a tabor, a daf, an mbira
with buzzers, a tom with the snares on, a marranzano, a spring drum, a
gopichand), and left both units as they are:

- **BrcSnare's wires on the head**, with the crackle.
- **BrcWorld's berimbau wire made stiff** (all-passes in its loop, as the
  Pelle e Legno berimbau samples' sharp upper partials ask), tuned through
  its loss filter, and able to bend.

Not done: **BrcTom / BrcKick glides** bend a modal bank; the loop's glide
bends every partial at once for the price of one, but the kick's `Dive`
already does what a deep loop glide would.

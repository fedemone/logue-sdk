# RipplerX – Session Brief

## Working State (branch tip — see git log for exact commit)

- Unit **loads on hardware** and all non-KS presets now produce inharmonic modal
  sounds instead of strings.
- Marimba ring bug is **fixed** — ring now lasts ~1.2s as configured (Phase 2 complete).
- **13th HW pass — Timpani/Taiko attack-vs-sustain, metallic ring-dominant rebalance:**
  - **Timpani/Taiko "bass guitar + audible vibration":** root cause was NOT octave
    (fundamental energy >> sub-octave) — it was SUSTAINED bright upper harmonics +
    a close-partial cluster.  A real timpani has a bright struck ATTACK that settles
    to a smooth dark fundamental.  Fix: mode 1 dominant + LONG (1.3s Timp / 1.5s Taiko);
    upper modes present but FAST-decay (300/210/150ms Timp, 260/175/120 Taiko) so they
    colour only the attack.  Verified: centroid early 406Hz → late 140Hz (Timp) =
    bright attack, dark continuous sustain.
  - **Metallic "crash still too predominant / not blended" (4th report):** decisive
    ring-dominant rebalance.  `modal_engine_gain` crash factor 0.60→0.95 (ring is the
    foreground); crash_base cut 2-3× more (Cymbal 6→2.5, Gong 1.5→0.6, HHat-O 4→1.8,
    Ride→2.0, RidBel 4→1.6); **crash_r broadened to ~0.965-0.985** so the 6 resonators
    OVERLAP into a continuous COLOURED sizzle (blended) instead of sparse beating or
    raw broadband hiss; raw `parallel_noise_gain` cut (HHat-O/Ride 6→2.2, etc.); bloom
    halved.  Flutter prominence: Cymbal 2.4×, Gong 2.0×, HHat-O 1.7×, RidBel 2.9×.
  - **Ride** still the weakest (10.3×, down from 23.8): its longer noise sustain exposes
    the 6-resonator bank's own ~34Hz correlated AM that Cymbal's faster decay hides.
    Note raised 57→69 (rides are bright), rm_depth→0, noise decay shortened.  STILL a
    discard candidate per the standing user option if HW doesn't convince.
- **12th HW pass — voice-stacking for sustained engines (cymbal rolls):**
  - **Polyphony bug:** `GateOff()` forced `next_voice_idx = NUM_VOICES-1`, and since
    the Drumlogue fires gate_on+gate_off in the same tick, EVERY repeated hit of a
    preset reused voice 0 → each trigger reset the previous one = NO stacking (fatal
    for cymbals: a second hit cut the first's ring).  The reset existed only to stop a
    melodic STRING preset from beating across presses.
  - **Fix (engine-aware):** GateOff now resets only for short/percussive engines
    (MEMBRANE/SNARE/NOISE) and the KS string; **PLATE (cymbal/gong/ride/hi-hat/bell/
    cowbell/triangle) and BAR (marimba/vibe/…) keep the round-robin so fast hits land
    on different voices and STACK/overlap** across the 4 voices.  Verified: 3 fast hits
    → 3 active voices on Cymbal/HHat-O/Marimba; mono on Kick/Snare.  (Per-voice crash
    state incl. the reused resA self-PM buffer means stacked voices don't interfere.)
  - **auto_tune.py:** confirmed NOT running/stuck (no process); it's idle.
- **11th HW pass — de-regress Timpani, crash decay/continuity, full param coverage:**
  - **Timpani "bass guitar" regression:** the 10th-pass clean 1:1.5:2:2.5:3 series at
    1.4 s T60 = a sustained harmonic tone = bass.  Fixed: ratios slightly STRETCHED
    (1.5/2.03/2.49/3.02/3.55, not exact 0.5 steps) + T60 cut to ~850 ms → percussive,
    struck-drum identity, still no 1.742 beating.  Decay 1.4 s→0.76 s.
  - **Cymbal "crash too strong + no decay envelope, continues while held":** crash_base
    11→6, bloom 1.2→0.9, modal T60 3000→1800 ms, cymbal noise decay ~6 s→~1.2 s → a
    clear ~1.4 s decay regardless of gate hold.
  - **Gong "still an explosion":** crash_base 4→1.5, bloom 0.3→0.2 (ring-dominant).
  - **HHat-O / Ride "ringing too slow, like shaking ~28 Hz":** the sparse 6-resonator
    crash wash BEATS at ~28 Hz.  Fixed by making these CONTINUOUS broadband-noise-
    dominant (a real open hat is hiss, not a resonant wash): per-preset
    `parallel_noise_gain` raised (HHat-O 6.0, Ride 6.5, RidBel 4.5; Cymbal/Gong stay 1.2),
    crash_base cut (HHat-O 8→4, Ride 10→2.5), crash_r broadened (→0.985/0.982 = wider,
    less ringing).  28 Hz beat prominence: HHat-O 5.3×→1.8×, Ride 8.7×→4.7× (Ride's low
    note 57 limits it — flagged for possible discard).
  - **EVERY-KNOB-DOES-SOMETHING (HW demand), via `param_audit.cpp`:** newly wired on
    modal engines, all REFERENCE-ANCHORED: **Rel→ring-length** (folded into t60_scale,
    ±~1 oct), **MlltRes→modal presence** (non-crash plates; crash plates keep MlltRes=
    crash intensity), **Partls→mode count + overtone richness** (env3-6 scaled, since the
    count change alone was inaudible).  Remaining contextual-by-design (documented):
    velocity knobs (VlMllRes/Stf need velocity variation); NzRes/NzFltr/NzFltFrq/Resnc
    are dead only when NzMix=0 (NzMix is the noise master-enable); KS Rel/Inharm and the
    master Tone EQ are subtle/contextual on no-noise or low-freq presets.
  - **Autotune:** can refine the SAMPLE-MATCHED tonal presets (membrane/bar/snare) and
    catch regressions, but CANNOT help the metallic family — their reference scoring is
    unreliable (no reliable f0; documented arch floors) and their character lives in
    hardcoded crash_* / modal-config constants the tunable preset row doesn't reach.
- **10th HW pass — crash REBALANCE + Timpani harmonic modes + Shaker swell:**
  - **The crash recipe is COMPLETE, not missing** (answering "is comb filtering /
    phase modulation missing?"): the crash resonator bank (pass 7) IS the comb/
    resonator matrix and self-PM bloom (pass 9) IS the phase modulation.  The
    problem was BALANCE — passes 7–9 kept pushing crash up + ring down, so the
    crash overpowered and didn't blend.  Fix: **crash_base ~halved** (Cymbal 20→11,
    Gong 14→4, HHat-O 16→8, Ride 22→10, RidBel 15→7); ring raised
    (`modal_engine_gain` crash factor 0.45→0.60); `crash_ring_tap` raised
    (Ride 0.15→0.40, RidBel 0.20→0.45, HHat-O 0.25→0.35) so the wash is COLOURED
    BY the ring = blended.  Gong bloom 0.6→0.3 (was "a big explosion" — recipe
    saved in PROGRESS.md for a future Explosion preset).
  - **Timpani harmonic preferred modes** (SoS "Practical Percussion Synthesis:
    Timpani" + Rossing): ratios now **1 : 1.5 : 2 : 2.5 : 3 : 3.5** (2:3:4:5:6 of a
    missing fundamental → clear "singing" pitch).  Dropped the 1.742 mode that sat
    ~20 Hz from 1.504 = the critical-band BEATING heard as "rough" every pass.
    Verified: modes 82/124/165 Hz, gaps a consonant 41 Hz, no beating pair.  The
    3D FDTD membrane+air model is GPGPU-scale → infeasible here (documented, not attempted).
  - **Shaker hit removed:** zeroed the woodblock modal body (struck "tok") AND
    slowed the noise-env attack to ~15 ms (`attack_rate 0.004`) — the noise burst
    hit full in <1 ms = the "hit".  Now a soft swell (onset/body peak ratio 5.35→1.10)
    with the 17 Hz rattle intact.
  - TODO captured in PROGRESS.md: **Tambourine** preset (we have the basis —
    bright short jingle modes + light crash + grain AM).
- **9th HW pass — self-PM "dynamic bloom" (THE cymbal recipe, completed):**
  - **Recipe status (answering "did you do the recipe?"):** pass 7 built the
    resonator matrix (recipe item #5).  This pass adds the **self-Phase-Modulation
    "dynamic bloom"** (item #4, Kilohearts Phase-Distortion) that was missing —
    that is why Cymbal/Ride/HHat-O sounded "identical": 6 discrete resonators ring
    as sparse tones with no density and no intermodulation.  Self-PM is the Bessel
    sideband generator that fills the spectrum into a real crash AND is literally
    the "modulation between the two" (ring × wash) the HW kept asking for.
    Phase-smear all-pass cascade (#2/#3) NOT added — self-PM already supplies the
    diffusion; revisit only if more wash is wanted.
  - **Implementation:** the metallic bus (resonated noise + raw/HF noise + a tap
    of the struck ring, `crash_ring_tap`) is written to a short delay line —
    the KS `resA.buffer`, **reused** (dead on plate engines → zero new memory) —
    and read back at an offset modulated by the signal's own amplitude
    (`crash_bloom`).  Self-FM.  Verified: Cymbal spectral density 1138→2337 bins,
    HHat-O 497→4722; both now dense crashes.
  - **Ride/RidBel `crash_r` was too high** (0.998+, pass 7) → ultra-narrow
    resonators = sparse tones the bloom couldn't densify.  Lowered to ~0.9965
    (broad, dense); ring length now comes from the noise-release sustain, not
    razor-Q resonators.  Their low ring_tap (0.15/0.20) keeps them sizzle-bright.
  - **Shaker rattle:** `noise_am_decay` was fading the grain LFO out (~150 ms) so a
    long Rel gave smooth hiss.  Set to **1.0** — the 17 Hz rattle now persists for
    the whole tail, so a longer Rel = a longer RATTLE (HW: verified mod 0.65 at
    Rel 20).  Default Rel 18→19 so the rattle is audible out of the box.
  - **Master gain 1.0→1.5** (HW "+0.5").  **Koto**: higher harmonics raised + 6th
    partial (6.81).  **Vibraphone**: 3→6 modes (adds 20/24/30:1 overtones).
    **Timpani**: inner beating modes (1.742) trimmed for a smoother low end (still
    imperfect — reference spectrum/T60 data would help).  **Taiko**: short bright
    1.6 kHz noise "tk" click (NzMx 9→26, NzRs 550→180) for the missing wood attack.
- **8th HW pass — param-reroute template extended (accepted by user):**
  - **ENGINE_NOISE (Clap/Shaker/HHat-C):** MlltStif → grain/burst LFO rate
    (±2 oct), MlltRes → burst depth.  Both REFERENCE-ANCHORED (default sound
    bit-identical; verified self-dist 0.000).
  - **ENGINE_PLATE crash family:** TubRad also sets crash ring length (bigger
    shell → longer wash), on top of its modal-body role.
  - Taiko sine-stack snippet & snare feedback-synthesis notes reviewed: already
    covered and exceeded — Taiko = membrane modal bank + boom + velocity split
    (vs 3 harmonic sines); snare = modal body + 3-band wire resonators (= their
    transient/feedback-head/spring decomposition).  No change made (both approved).
- **7th HW pass — crash-resonator bank + param repurposing:**
  - **CRASH BANK (dsp_core.h `crash_*`, processBlock):** the recurring "noise
    just put over the ring, not crashing" on Cymbal/Gong/Ride/RidBel/HHat-O was
    structural — noise was generated separately and summed in parallel, so it
    could never be a crash.  Per the user's feedback-synthesis research, a crash
    is broadband noise RESONATED through a bank of inharmonic resonators.  Added
    a bank of 6 constant-peak-gain 2-pole bandpass resonators per voice, tuned to
    the SAME mode frequencies as the struck modal bank (reuses `modal_k_*`), driven
    continuously by the enveloped noise burst:
    `y[n] = r·k·y1 − r²·y2 + (1−r²)·noise`.  The wash IS the partials → it crashes
    and swirls with the ring.  Gated on `crash_drive>0` so non-plate presets cost 0.
  - **Two prerequisites that made it actually work** (every prior pass failed on
    these): (1) the struck modal ring is pulled back (`modal_engine_gain ×0.45`)
    on crash presets so the noise wash can compete — the ring was 30× louder than
    the noise; (2) the noise release is overridden slow (`release_rate` T60 ≈ 2.4 s)
    so the wash isn't cut ~50 ms after the Drumlogue's near-instant gate-off — the
    short Rel release was killing the crash fuel.  Now Cymbal decays 0.52→0.11→0.03
    over ~1.5 s, Gong sustains ~2 s, Ride ~1.5 s — real crash envelopes.
  - **MlltRes → crash intensity (PARAM REPURPOSING):** on ENGINE_PLATE the mallet
    LP2 that MlltRes drives is inaudible under a wash, so MlltRes is repurposed as
    live crash-bank intensity, REFERENCE-ANCHORED at the shipped value
    (`m_modal_mltres_ref`): shipped 800 → calibrated drive, range ~6.6–26.4.
    This is the pattern for the "wire dead params to hidden controls" request —
    extend the same way for other engines as needed.
  - **Crash presets:** Cymbal drive 20 r0.997, Gong 14 r0.9985, HHat-O 16 r0.9968,
    Ride 22 r0.9984, RidBel 15 r0.9980.  Raw parallel noise dropped to 1.2 and
    ring-mod depths halved (the resonated wash now carries the pitched energy).
  - **Timpani**: FM growl removed entirely (mode 6 carries the shimmer); mode-1
    env lifted to 1.0, uppers trimmed → rounder, less-beaty low end.
  - **Koto**: richer/slightly-inharmonic overtone stack (2.005/3.012/4.215/5.42,
    mix 0.30, +mode 5) for the characteristic koto timbre vs a plain string.
  - **Kick**: faster boom attack (0.0035) + shorter onset ramp (2 ms) + boom_mix
    0.70 → "thump" not "whomp" (sub-90 Hz peak now at 38 ms).
  - **Taiko**: boom_mix 0.45→0.58 (more sub under the wood crack).
- **6th HW pass:**
  - **ROOT CAUSE — modal tuning was wrong at low frequencies**: `fastercosfullf`
    (and `fasterpowf` for base_f) in `init_modal_modes` has ~1e-3 error; near
    w→0 the recovered frequency shifts by δcos/(w·sin w).  Timpani's intended
    82/124/144/165 Hz landed at 86/121/139/157 Hz — compressed, ~17 Hz gaps →
    slow beating = the "rough, not smooth" low-end reverberation.  Fixed with
    exact `cosf`/`exp2f` (NoteOn-time, same rationale as the expf T60 fix).
    ALL modal presets are now tuned exactly (small global pitch correction).
  - **Ring-mod gate reshaped**: `(1−d) + d·modal` (true bipolar mix, only
    (1−d) static carrier) instead of `1 + d·modal`; also applied to the
    hf_branch shimmer.  Cymbal d=0.80, Ride 0.75, Gong 0.65, RidBel 0.60.
  - **Ride/RidBel**: noise-dominant crash washes (NzMx 55/45, NzRs 950,
    band_mix 0.95/0.90) + Gong-style long noise-env overrides (their NzRs gave
    noise_env_hi T60 of 3-8 ms — sizzle was dead before the hf/rm paths ran).
  - **Taiko velocity split**: hard hits → boom (×0.25..×1.75 by vel²), soft
    hits → wood mode 4 (×1.6 soft .. ×0.6 hard).
  - **Timpani**: +mode 6 (2.896) copper shimmer; FM chirp depth 0.16→0.06
    (200 Hz onset growl read as "rough").
  - **HHat-O**: de-glassed — use_hat=false (broadband HP@8k coloured noise like
    HHat-C), modal mix 0.30→0.12 (light crash ring), Rel 18.
  - **Shaker**: wood "toc" cut (modal mix 0.04), noise-dominant (NzMx 95),
    grains 17 Hz / τ150 ms.  **Kick2**: boom kck_bm mix 0.85, modal 0.46.
    **Kick** Gain 12, boom 0.58.  **808Sub** Gain 14 (was relatively quiet).
    **AcSnare** fast noise attack 0.012 + wire mix 0.85 + NzMx 52.
    **Koto** overtone mix 0.22, brighter envs, MlSt 420.  **Gong** upper-mode
    T60s 1400/900/600 (ringing reverb) + NzMx 34.  **Handpan** 5 modes.
    **Bongo** note 50 + boom 0.18 (deeper than Conga).  **Tick** clack 1.25.
    **Taiko2 renamed "DeepBs"** ("a good guitar bass").
- **5th HW pass (program list now 37 entries — Flute/Clarinet REMOVED outright):**
  - Slot 0 = **Kick2** (the pre-redesign Timpani body — HW liked it as a kick).
    Tests that probe the KS waveguide now LoadPreset(k_GuitarStr) explicitly.
  - **Timpani redesigned**: kettledrum principal tones 1:1.504:1.742:2.0:2.444 with
    SOLID upper-mode energy/T60s (old config's uppers were near-silent → "bouncy
    single sine", and made Model/MlltStif/HitPos/Tone inaudible on this preset).
  - **Taiko redesigned**: woodblock-hard crack (strong short mode 4 at 2.756) under a
    long quasi-harmonic TAANNG ring; old Taiko lives on as **Taiko2** (slot 23,
    replaces PluckBass per HW request).
  - **Tick** = the pre-redesign HHat-C metallic chick + low "clack" mode (1.40, 50ms).
  - **HHat-C** = the pre-redesign Shaker noise voice ("a perfect closed hi-hat").
  - **Shaker redesigned**: woodblock modal body + BP noise gated by an
    enveloped-LFO (13 Hz grain pulses, noise_am_* fields).
  - **Clap**: multi-burst AM (~55 Hz, depth fades in ~15 ms) + NzRs 950 → "tcha" tail.
  - **Ride/RidBel**: near-harmonic ratio sets (read as "a string") replaced with
    thick-plate (2.92/6.37/11.75) and bell-partial (2:3.01:4.7) sets.
  - **Noise ⇄ ring cross-modulation** (modal_rm_depth): parallel noise is
    ring-modulated by the previous sample's modal output for Cymbal/Gong/HHat-O/
    Ride/RidBel — wash and ring now interact instead of overlaying (Risset).
  - **MarchSnare**: noise attack staging removed (0.012 rate) — click+buzz land together.
  - **Koto**: harmonic-overtone modal bank (2/3/4.2, mix 0.10) on top of the KS string.
  - **Bongo**: + wood "tock" mode 5 at ratio 3.80.
  - **Master filter is now a LOWPASS "Cutoff"** (header.c renamed; default 1999 =
    open; all preset rows col16 = 1999).  The old "LowCut" HP read as reversed on
    HW three times.  NOTE: the old per-preset HP rumble-cut (e.g. Triangle 1.2 kHz)
    no longer exists.
  - **TubRad → modal body** (anchored): scales mode-1 T60 and boom_mix, 2^(1.2·Δ).
  - **HitPos modal tilt coefficients doubled** (HW: "no effect" on membranes).
- **Dkay controls modal T60** for BAR/MEMBRANE/SNARE/PLATE engines, anchored at the
  preset's shipped Dkay (`m_modal_dkay_ref`): `t60_scale = 2^(3*(norm - ref))`.
  Calibrated config T60 always plays at the default Dkay (no regression); knob trims
  around it.  Uses `exp2f` (NOT `fasterpow2f` — that returns 0.971 at scale 1.0).
- **Mallet stiffness → modal brightness** (`m_modal_stiff_ref` pivot).  MlltStif and
  VlMllStf (velocity) now tilt the higher modal modes' initial energy: stiffer = brighter.
  Neutral at the shipped MlltStif so no default-sound regression.
- **Noise-ring coupling (ENGINE_PLATE):** `noise_ring_gate` in VoiceState tracks the
  modal fundamental decay (`modal_decay_1`) each sample. `parallel_noise_gain` is
  multiplied by `fmax(0.15, noise_ring_gate)` so noise amplitude follows the ring — no
  more "juxtaposed" independent noise on metallic instruments.
- **ENGINE_NOISE duration fix:** `sustain_level=1.0f` for NOISE engines (not 0); NoteOff
  skips `master_env.release()` for NOISE. The `noise_env` (Rel knob) now fully controls
  Clap/Shaker tail. Old behaviour: master_env auto-decayed in ~11 ms at default Dkay,
  killing the voice before `noise_env` could produce any tail.
- **Rel → noise_env.release_rate** for ENGINE_NOISE:
  `rel_rate = 0.00005 + (1-norm)*0.01`. At Rel=18 (norm=0.90): ~200 ms tail.
- **HiHat/Cowbell modal ratios:** switched from membrane Bessel (1.479/1.932/2.332) to
  plate ratios (2.92/6.37, 2.00/2.68/4.30) — Bessel ratios produced "wood-like pop"
  instead of metallic character.
- **Gong k_modal_mix was 0 (silent body):** Only FM chirp + noise played ("sci-fi zap").
  Fixed to 0.20 in model_param_presets. FM depth halved (0.16→0.08).
- **3rd HW pass — T60 calibration (Dkay at max = user can only shorten, not lengthen):**
  Taiko 500ms→1800ms, Woodblock 50ms→160ms, Bongo 50ms→320ms, Tick 25ms→200ms,
  KickDrum boom T60 240ms→760ms. Modal T60 must give headroom above user's Dkay=200 so
  the Dkay knob has an audible range. Verify with `analyze_samples.py` on reference WAVs.
- **808Sub redesigned as ENGINE_MEMBRANE sub kick:** pitch_env (τ≈21ms) sweeps boom
  oscillator 160→45 Hz independently from boom amplitude (T60=760ms). processBlock
  special-cases k_808Sub: `sweep_hz = 45 + pitch_env_amt * pitch_env`. Changed kPresetEngine
  from ENGINE_KS to ENGINE_MEMBRANE.
- **Koto "thwaaang" + T60 fix:** pitch_env_decay=0.99900 (τ=21ms, not 83ms) so the 1.5st
  pitch glide completes in ~63ms. Growing KS delay injects zeros → fast τ minimises energy
  loss (<2%). Dkay 185→200, Mterl 12→20, TubRad 7→18 → T60 0.49s→3.3s@C4.
- **KS pitch_env T60 gotcha:** When pitch_env_amt>0, the KS delay starts SHORT and grows to
  base (starts sharp → settles to fundamental). Growing delay injects interpolated zeros into
  the feedback path, shortening T60. Fix: use τ≤21ms (pitch_env_decay≥0.9990) so the sweep
  completes in the attack transient. NEVER use τ>50ms for KS pitch_env.
- **Snare character fix (3rd pass):** AcSnare body T60 350ms→80ms, modal_mix 0.24→0.10.
  MarchSnare body T60 60ms→30ms, modal_mix 0.16→0.06. Wire resonators (4.5/7.2 kHz) now
  fully dominate over the brief membrane "toc".
- **Gong (3rd pass):** noise_band_mix 0.82→0.50, modal_mix 0.20→0.26, fm_depth 0.08→0.12.
  Cymbal/Gong parallel_noise_gain 7→3.5 (was too harsh).

### Parameter → modal-engine mapping status (4th pass — full wiring)
- **Working on modal engines:** Dkay (T60), MlltStif/VlMllStf (brightness), Tone, LowCut,
  Gain, NzMix, NzRes/NzFltr/NzFltFrq (noise colour), and:
  - **Model** → modal ratio template swap (`kModelModalRatios`, 9 physical models)
  - **Partls** (0-4) → mode count offset around the shipped count, clamped [2, 6]
  - **Inharm** → overtone spread `1 + (ratio−1)·spread` around the fundamental
  - **Mterl** → upper-mode material damping (T60 of modes ≥ 2 scaled `2^(1.5·Δ)`)
  - **HitPos** → strike-position excitation tilt (rim → upper modes, centre → mode 1)
  All five are REFERENCE-ANCHORED at the preset's shipped values (see below): defaults
  are bit-identical to the calibrated configs; only knob movement reshapes the bank.
- **Rel works for ENGINE_NOISE** (Clap/Shaker) — controls noise_env release tail.
- **Still KS-only (documented):** TubRad (loop LP coefficient), MlltRes (exciter
  transient shaping only on modal engines), Rel on non-NOISE engines (noise tail only).

### Cutoff "in reverse" — fixed (TPT SVF + noise band rework)
- `filter.h` is now a TPT (Zavalishin) SVF.  The old Chamberlin stability clamp froze
  every cutoff above ~8.2 kHz onto a resonant ~8 kHz boundary, so LowCut/NzFltFrq got
  LOUDER as raised — the reversal reported from hardware.  TPT is stable and accurate
  to Nyquist; no clamp.  Chamberlin BP near Nyquist also had centroid ~18 kHz (not fc):
  hat presets were retuned for the accurate BP (HHat-C hat HP@6k, HHat-O hat BP@12k).
- Noise hi band now derives from the SVF-coloured noise (was: unfiltered source with
  split corner tied to 2.2×NzFq, which REMOVED sizzle as the cutoff rose).
  `noise_hi_lp_coeff` is a per-preset constant again (NzFq no longer overwrites it).
- NoteOn no longer clobbers per-preset `k_noise_band_mix` with model-profile defaults
  (table value > 0 wins; 0 = use profile default).  This un-broke HHat-C's hat path
  (mix 0.86 > 0.80 gate) and honoured Gong's calibrated 0.50.
- Clap retuned NzFltr HP@6k→BP@3k (ref centroid ≈1.5 kHz); GlsBotl got AtkMs=0.5.
- `auto_tune.py` table regexes updated for the non-static member declarations
  (`model_param_presets`, `modal_preset_configs`) — model/modal tuning was crashing.

### REFERENCE-ANCHOR pattern (important)
Calibrated modal configs are tuned assuming the preset's *shipped* knob value.  Any new
param→modal mapping MUST pivot at a captured reference (`m_modal_*_ref`, set in LoadPreset)
so the default sound is unchanged and only knob *movement* alters it.  Anchoring at an
absolute endpoint (e.g. Dkay=200) silently detunes every preset — that was the
"string-like" Marimba regression.

### GOTCHA: modal mix lives in TWO tables
`ModalPresetConfig.mix` is used only by `LoadPreset`.  `NoteOn` re-inits the modal bank
using `model_param_presets[preset][k_modal_mix]`, which **overrides** it.  Keep both in
sync.  MarchSnare shipped with config.mix=0.14 but `k_modal_mix`=0 → silent body, pure
noise.  Always edit the `model_param_presets` `k_modal_mix` column to change the audible mix.

## Critical .rodata / .data Constraint — Do NOT break this

The drumlogue firmware checks `.text segment` size (= `.text + .rodata + .init + .fini`) per unit. Limit ≈ 30 KB. The preset tables (~7 KB) must stay in `.data`.

**Working fix (a49e2f4):** The large preset arrays —
`kDefaultModalPresetConfig`, `modal_preset_configs[]`, `model_param_presets[][]`,
`kPresetEngine[]` — are declared as **non-static** class members (no `static`,
no `const`, no `constexpr`). This makes them part of the global `s_synth`
object layout and places their initial values in `.data`.

**Broken patterns to avoid:**
- `static constexpr T arr[] = {...}` → goes to `.rodata` → text-size check fails
- `static const T arr[] = {...}` → same problem
- `static T arr[] = {...}` **inside a class body** → GCC 6.5 rejects it (out-of-line
  definition required for non-const static members, and constexpr causes .rodata)

See `config.mk` `USE_LTO := no` and the comment block for background.

## Root Cause of Marimba "Click, No Ring" Bug (fixed — see current commit)

`fasterexpf` from the fast-math library is catastrophically inaccurate for
arguments with |x| < ~0.001. `modal_decay` is computed as
`expf(ln(0.001) / (T60_s × srate))`. For T60=1.2s the argument is -0.000120,
for T60=5.0s it is -0.0000288 — both fall in the broken range.
`fasterexpf(-0.000120)` returns ~0.971 (implying T60≈5ms) instead of 0.99988.
**Fix:** use standard `expf` (and `powf`) in `init_modal_modes` for
`modal_decay_1..6`. These are computed once at NoteOn time, so accuracy
dominates over the ~10 ns saved by the fast approximation.

## Root Cause of Marimba "1/3-Second Ring" Bug (fixed commit 859a2a4)

`master_env` in `NoteOn` was set to `sustain_level=0.0f`, causing it to
auto-decay from 1.0 → 0 at `decay_rate = master_rate × 0.3`. At default
Dkay (stored=25, t_s≈1.94 s), `decay_rate≈0.000446` → `ENV_IDLE` in ~323 ms.
The processBlock squelch `!is_releasing && master_env.state==ENV_IDLE` then
killed the voice at exactly the "1/3 second" the user heard, regardless of
modal T60 (Marimba mode-1 T60=1200 ms). Dkay=2000 didn't help because the
*release_rate* (not decay_rate) also cuts the voice in ~97 ms after gate-off.

**Fix:** For non-KS engines (including NOISE), `NoteOn` sets `sustain_level=1.0f`
so `master_env` holds at 1.0 and never auto-decays. `NoteOff` skips
`master_env.release()` for non-KS engines. Modal voices deactivate when
`mag_env < kSquelchThreshold` (ring naturally dies); NOISE voices deactivate when
`noise_env` reaches ENV_IDLE. See commits 859a2a4 and 94952f8.

## Root Cause of "Always String-Like" Sound

The KS waveguide produces a harmonic series (f, 2f, 3f…) regardless of modal preset.
The modal bank runs **in addition** to KS at a low mix (15–30%), so KS always
dominates. Fix: bypass KS for non-string presets; use the modal bank as the sole
tonal resonator.

## Engine Architecture (6-type redesign)

Each preset is assigned one `EngineType`. `processBlock` routes via a switch/if on
`kPresetEngine[m_preset_idx]`.

### Engine Types

| Engine | Signal path | Presets |
|--------|-------------|---------|
| `ENGINE_KS` | Karplus-Strong delay + modal additive | Init, 808Sub, Koto, GuitarStr, PluckBass |
| `ENGINE_BAR` | Mallet exciter → bar modal bank (ratios 1:2.756:5.404) | Marimba, Vibraphone, Kalimba, SteelPan, Woodblock, Claves, TubularBell, GlassBowl, GlassBottle, SlitDrum, Tick |
| `ENGINE_MEMBRANE` | Strike exciter → circular membrane modal bank (Bessel ratios 1:1.594:2.136:2.296) + boom osc | Timpani, Djembe, Taiko, AcTom, Conga, Bongo, Handpan, KickDrum |
| `ENGINE_SNARE` | Membrane body (short, no KS) + snare-wire resonators | AcSnare, MarchSnare |
| `ENGINE_PLATE` | Strike → dense inharmonic plate modes + metallic noise | Cymbal, Gong, HiHatClosed, HiHatOpen, Ride, RideBell, BellTree, Cowbell, Triangle |
| `ENGINE_NOISE` | Noise burst (+ optional modal body / AM gating) | Clap, Shaker, HiHatClosed |
| `ENGINE_REMOVED` | Silent placeholder (currently unused — Flute/Clarinet were removed from the program list entirely) | — |

### Preset → Engine Mapping

```
k_Kick2(0)        ENGINE_MEMBRANE  ← ex-Timpani body, kick voice
k_Marimba(1)      ENGINE_BAR       ← Phase 2 exemplar
k_808Sub(2)       ENGINE_MEMBRANE
k_AcSnare(3)      ENGINE_SNARE
k_TubularBell(4)  ENGINE_BAR
k_Timpani(5)      ENGINE_MEMBRANE  ← redesigned principal tones
k_Djambe(6)       ENGINE_MEMBRANE
k_Taiko(7)        ENGINE_MEMBRANE  ← woodblock crack + TAANNG ring
k_MarchSnare(8)   ENGINE_SNARE
k_Koto(9)         ENGINE_KS        ← + overtone modal bank
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
k_Clap(21)        ENGINE_NOISE     ← multi-burst AM
k_Shaker(22)      ENGINE_NOISE     ← grain-pulse AM + woodblock body
k_Taiko2(23)      ENGINE_MEMBRANE  ← ex-Taiko deep membrane
k_GlassBowl(24)   ENGINE_BAR
k_GuitarStr(25)   ENGINE_KS
k_HiHatClosed(26) ENGINE_NOISE     ← ex-Shaker noise voice
k_HiHatOpen(27)   ENGINE_PLATE
k_Conga(28)       ENGINE_MEMBRANE
k_Handpan(29)     ENGINE_MEMBRANE
k_BellTree(30)    ENGINE_PLATE
k_SlitDrum(31)    ENGINE_BAR
k_Ride(32)        ENGINE_PLATE     ← thick-plate ratios
k_RideBell(33)    ENGINE_PLATE     ← bell partials
k_Bongo(34)       ENGINE_MEMBRANE  ← + wood tock mode
k_GlassBottle(35) ENGINE_BAR
k_Tick(36)        ENGINE_PLATE     ← ex-HHat-C chick + clack
```

## Implementation Phases

| Phase | Task | Effort | Risk | Validation |
|-------|------|--------|------|------------|
| 1 | Engine routing scaffold — KS bypass for non-string presets, ENGINE_REMOVED silence | Low | Low | All presets audible; strings unchanged |
| 2 | ENGINE_BAR — Marimba (kill-switch) | Medium | Low | Marimba sounds like a marimba, not a string |
| 3 | ENGINE_MEMBRANE — Kick, Timpani, Taiko, etc. | Medium | Medium | Kick thumps, Timpani rings inharmonically |
| 4 | ENGINE_SNARE — AcSnare, MarchSnare | Medium | Medium | Snare has sharp crack + sizzle, not ring |
| 5 | ENGINE_NOISE — Clap, Shaker | Low | Low | Clean noise bursts |
| 6 (optional) | ENGINE_PLATE — Cymbal, Gong, Hi-hat, etc. | High | High | Replace with samples if effort/result too high |
| 7 | Final tuning + binary size check | Low | Low | Size within budget, all engines pass HW test |

**Phase 2 is the kill-switch.** If Marimba does not sound convincingly like a
marimba after Phase 2, stop and re-evaluate the approach before continuing.

**ENGINE_PLATE is optional.** If effort/result ratio is too high, replace plate
presets with sampled content rather than synthetic DSP.

## ENGINE_PLATE: Noise-Ring Coupling (noise_ring_gate)

Metallic instruments need noise to decay *with* the ring, not independently.

`VoiceState::noise_ring_gate` (float, default 1.0) is reset to 1.0 on every NoteOn.
In `processBlock`, for `ENGINE_PLATE` with `modal_pilot_enabled`:
```cpp
parallel_noise_gain *= fmaxf(0.15f, voice.noise_ring_gate);
voice.noise_ring_gate *= voice.modal_decay_1;
```
`modal_decay_1` (the per-sample decay factor for mode 1) also decays the noise envelope.
The floor of 0.15 keeps a faint sustained noise bed (metallic shimmer) even after the
ring has mostly decayed. Without this, noise stays at full gain while the ring dies →
"juxtaposed" rather than integrated metallic sound.

## ENGINE_NOISE: Voice Lifetime

Clap and Shaker use `ENGINE_NOISE`. The voice lifecycle:
1. NoteOn: `master_env.trigger()`, `sustain_level=1.0f` → master_env holds at 1.0 forever.
2. NoteOn: `noise_env.trigger()` with short attack; `noise_env.release_rate` set by Rel.
3. NoteOff: **nothing** — no `master_env.release()` for NOISE engines.
4. `noise_env` decays from 1.0 → 0 under its own release; when it reaches ENV_IDLE the
   processBlock squelch fires and deactivates the voice.

The Rel formula: `release_rate = 0.00005 + (1.0 - norm) * 0.01`.  
At Rel=0: rate≈0.01 (~200 samples = 4 ms). At Rel=18 (norm=0.90): rate≈0.00105 (~200 ms).
At Rel=127 (norm=1.0): rate≈0.00005 (~4 s). Shipped Clap/Shaker at Rel=18.

## GOTCHA: Plate Ratios vs. Membrane Ratios

HiHatClosed, HiHatOpen, and Cowbell are ENGINE_PLATE. If their modal_preset_configs
use membrane Bessel ratios (1.000 / 1.594 / 2.136 / 2.296), they sound like a wood drum,
not metal. Plate ratios (2.92 / 6.37 / 11.75) produce the inharmonic metallic character.
Always use plate-appropriate ratios for PLATE presets.

## Modal Bank — Key Parameters

`modal_preset_configs[k_NumPrograms]` in `synth_engine.h` holds per-preset mode
ratios and T60 values. These are already calibrated and use inharmonic Bessel/bar
ratios. The table is `static` (not constexpr) per the .rodata rule above.

For **non-KS engines**, the modal bank is the **primary** tonal source (KS is
bypassed). The modal mix values (~0.18–0.32) were calibrated relative to KS, so
they must be scaled up by `kModalEngineGain ≈ 5.0` when running without KS.

## Dev Branch

`claude/continue-previous-session-vydFO` on `fedemone/logue-sdk`.

Always rebuild and check `arm-unknown-linux-gnueabihf-size ripplerx.elf`:
- `.text` (= text + .rodata) must stay below **28 KB** (safe margin below 30 KB limit).
- `.bss` must stay near **552 bytes**.

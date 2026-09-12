# EffeESP32 — Porting Progress & TODO

Port of copych/ESP32-S3_FM_Drum_Synth → KORG drumlogue, using **EffeMD** as the
project template. This file tracks what is done, what is next, and gives
instructions for the next agent.

---

## Done

- **Project scaffold** (mirrors EffeMD): `Makefile`, `config.mk`
  (`PROJECT=effeesp32`, synth), `header.c`, `unit.cc`, `.gitignore`,
  `.clang-format`.
- **FM operator** (`fm_operator.h`): reused EffeMD's C port of copych's
  `FmOperator`; **added AM support** (`fmo_am`, `am_level`/`am_offset`) required
  by algorithms 14–17.
- **Voice engine** (`fm_voice6.h`): port of `FmVoice6` — 6 operators, **all 18
  algorithms reproduced verbatim**, AHDSR + SVF per voice, block rendering.
- **Envelope** (`adsr.h`): **faithful port** of the upstream `FMDrums/adsr.h`
  (one-pole exponential-target AHDSR; Soundpipe/Diedrichsen lineage, Copych
  fast/semi-fast releases + HOLD phase). END_NOW / END_REGULAR / END_SEMI_FAST /
  END_FAST modes and the D0 time-constant math preserved verbatim; only the
  uninitialised-segment bug in `getPenalty()` was fixed.
- **Filter** (`svf_filter.h`): direct port of `svf_morph.h` (Chamberlin SVF with
  low→band→high morph), 48 kHz, LUT sine swapped for `fastersinfullf`.
- **Instrument table** (`drum_patches.h`, **auto-generated**): 59 patches =
  47 GM (notes 35–81, GM names) + 12 unique non-GM extras from notes 0–34 / 82–127.
  Generator: `tools/gen_patches.py` (dedupes by parameter signature, skips empty
  names). Source: `Drumkit_default.json` @ commit `6e47275`.
- **Synth controller** (`synth.h`): instrument cache + override-on-touch params,
  8-voice allocator with steal-by-score, chunked block render, **NEON pan/mix**
  (`vld1q`/`vmlaq_n`/`vst2q`) with scalar fallback, master gain for headroom.
- **Polyphonic output stage**: the voice mix now goes through
  `dl::PeakLimiter` (new, in `common/output_stage.h`) before the soft knee. The
  knee alone is a memoryless waveshaper, and a polyphonic bus drives it in
  proportion to the number of sounding voices, so stacked hits were being
  waveshaped rather than limited — inaudible-as-distortion on a kick, and
  broadband intermodulation on the dense inharmonic spectra of cymbals and
  gongs. Distortion against the linear voice mix dropped 19–29 dB on the
  metallic instruments (`tools/stack_meter`), at a cost of 2.2 LU of measured
  loudness that was itself distortion. See the README's *Output stage*.
- **Click-free retrigger**: a hit on a sounding note chokes that voice over
  ~20 ms and takes a fresh one, instead of re-using it and resetting every
  operator phase and the SVF under a live tail. Steal score no longer prefers
  the loudest voice in an envelope stage. See the README's *Voice allocation*.
- **`unit_reset()` now silences, and no longer reloads the patch**: it called
  `noteOff()` (a release — six seconds of cymbal after the host has reset the
  unit) *and* `load_instrument()`, which re-copies the patch over the working
  cache and rewrites the knob array, so every reset silently threw away the
  user's edits. The SDK asks for notes deactivated and phases reset but says
  parameter values "should not be reset to their default values".
- **Decay/Release range raised to 8000 ms.** Ten patches stored 2.8–8.0 s, so
  `load_instrument()` was clamping the *displayed* value to the old 2000 ms max
  while the engine ran the real 6 s — the panel disagreed with what you heard,
  and the knob could not shorten those tails without first jumping them to 2 s.
  The voicing edits below have since shortened those patches (longest is now
  Twirl at 1.95 s), so nothing is clamped either way; the range stays at 8000
  because it is now the only way to dial a long tail back in.
- **`MASTER_GAIN` 2.51 → 0.71.** At 2.51 a single hit arrived 6–17 dB past the
  output ceiling, so the stage held it flat until the envelope had fallen that
  far: Crash1 delivered 0.51 dB of its 6.50 dB natural fall over the first
  second — a cymbal with no envelope. It now delivers 6.47 dB. Costs 7.7 LU of
  mean loudness (−11.75 → −19.48 LUFS); see the README table for the full curve.
- **Instrument labels come from the GM map** — reverted, after a pass that
  relabelled them from each patch's `name` field in the kit JSON shipped and was
  wrong. `name` is the *seed patch* a slot was started from in the upstream
  editor, not what the slot became: slots 35–50 carry GM's own names verbatim
  for sixteen consecutive entries, slots 88–127 are one 13-entry template
  sequence repeated three times, and where a name and its slot disagree the
  parameters side with the slot (two slots named `Cymbal`, 1.5 s and 8.0 s,
  sitting exactly on GM's Splash and Crash 2). Order, count, trigger notes and
  the numeric data were unchanged through both passes. See the README's
  *Instrument names*.
- **Voicing edits (`VOICE_EDITS` in `tools/gen_patches.py`).** Fifteen slots
  ship with an envelope the kit did not give them, five of those with a
  different algorithm. The kit was authored without a 16-step grid in front of
  it and sixteen of its slots ring for 2.8–8.0 s, so every step was still
  sounding when the next one landed. Release is the length control — the
  sequencer gates a step off within a few ms, so the envelope is in RELEASE for
  almost all of its audible life and the tail tracks `rel`, not `dec`
  (measured; see the README table). Decay still sets the level release starts
  from, so it is changed only where the body of the hit needs changing (OpHat
  and MtlStk keep the kit's decay and get a shorter release only). Applied
  *after* selection, so the instrument list, order, trigger notes and the
  duplicate filter stay keyed to the untouched source.
- **Instruments also exposed as 59 presets.** `unit_load_preset()` is the only
  call the drumlogue API gives a unit for changing its *other* exposed parameter
  values with the host's knowledge — the SDK README says loading a preset "can
  cause exposed parameters to change value as a side effect", and there is no
  unit → host parameter push anywhere in `unit_runtime_desc_t`. Driven from the
  `Instr` knob, an instrument change rewrote the envelope inside the unit only,
  so the panel went on showing the previous instrument's values and the host's
  next push of a touched knob put them back. Picking the instrument from the
  preset UI now lands its own values on the panel; `Instr` is kept for
  automation and both funnel through `load_instrument()`.
- **24-parameter GUI** (`header.c`) with proper SDK param types (strings, semi,
  percent, pan, msec, hertz, on/off, **midi_note**).
- **Trigger Note** (param 22): each instrument carries its canonical MIDI note
  (`g_drum_inst_notes[]` in the generated table — GM note for 35–81, original
  slot index for the extras). Selecting an instrument reloads `Note` to that
  value; `GateOn` triggers the assigned note.
- **Feedbk macro** (param 23): global operator-feedback control, 0–200 %
  (100 % = patch). Additive ±3.5 offset in the FM feedback domain (clamped 0–10),
  so it adds grit even to zero-feedback patches. Applied at note-on
  (`FmVoice6::addFeedback`).
- **Combined Filter selector** (param 11, range −4…5): folds the SVF on/off flag
  together with a carrier-waveform override for operator 0 (Sin/Tri/Sqr/Saw).
  `0`/`1` keep the patch waveform; string labels (`Off`, `On Saw`, …) shown via
  `getParameterStrValue`. Chosen over an ADSR-model switch (the alternative
  envelope was a placeholder with no musical intent — see design discussion).
- **Import fidelity pass** (in response to "if we have to correct them, we
  probably made an error during import" — correct, we had). Diffed the port
  against upstream at the same commit the kit JSON came from (6e47275). The
  JSON→struct field mapping, `adsr.h`, `svf_filter.h` and all 18 algorithm
  graphs are exact. Four engine-side narrowings were not, and each one is the
  kind of simplification that looks harmless when written:
  - **Waveforms 5–9 folded onto 0–4**, dropping the sign inversion. Harmless on
    a lone carrier, not on a modulator (half-cycle phase shift into the
    carrier). Reached **19 of the 59** instruments, Crash1 among them.
  - **Operator feedback clamped to 0–7**, but `feedback_ = 1/2^(7−fb)` makes
    `fb` an exponent and the kit goes to 10 — up to 8× too little feedback on 6
    instruments. Upstream applies no clamp; its own editor offers 0–10.
  - **Linear pan** where upstream is equal-power (`sin_lut` is a full-cycle
    sine indexed in turns), putting the kit's 21 off-centre slots up to 3 dB
    loud against the rest.
  - **Effective operator frequency clamped to ≥ 0**, freezing the phase of the
    ratio-0/negative-detune "noise" operator into DC on 5 toms and LoAgogo.
  All four fixed; `MASTER_GAIN` 0.71 → 0.50 (= 0.71/√2) so the drive into the
  limiter is unchanged. Mean loudness −20.51 → −20.49 LUFS, worst peak −0.39
  dBFS, tails within 19 ms of before, 59/59 instruments still report every
  reflected parameter inside its declared header range.
- **Voicing pass on the unvoiced template slots.** Reported: RideBel silent;
  OHConga / LoConga / HiTimbl / LoTimbl / Cabasa / LoWdBlk / GlasFX "zapping,
  really too much, feel not correct"; HiBongo should be algorithm 1 and LoBongo
  algorithm 0. Measured rather than guessed, and it turned out to be two
  opposite failure modes:
  - **Bare sine (too little).** Slots 60/61/63..66 run algorithm 2, whose only
    modulator is op5 — and in the editor's default operator set op5 has ratio 0
    *and* detune 0, so its phase increment is zero and its feedback loop is
    seeded at last_out = 0: sin(fb·0) = 0 forever, the operator emits exactly
    nothing, and the carrier is a bare sine. MHConga is the control (same
    algorithm, op5 ratio 0.98, never reported). Fixed with algorithm 1, which
    rings ops 0/4/5 as carriers so op4 supplies a partial.
  - **Index ~92 (too much).** Cabasa / LoWdBlk / GlasFX have a live modulator
    at the kit's operator volume 0.8, i.e. fm_level 5.77 × MOD_RANGE 16. Fixed
    by lowering modulator volume (0.25–0.30), not by changing the algorithm.
  - **RideBel** was this unit's own fault: an earlier voicing edit put it on
    algorithm 17, index ~92, 14.2 kHz centroid, 0.67 flatness, −33.5 LUFS —
    which is what "outputs no sound" was. Back to the kit's algorithm 10 plus
    the level its 6 s ring used to pay for: −21.1 LUFS.
  - LoBongo's requested algorithm 0 measured as a no-op (rings only op0 and the
    silent op5 — bit-identical timbre to algorithm 2, 3 dB down); user chose
    algorithm 1 with op5 at 0.30 instead.
  - **Family de-duplication.** The kit shipped slots 60/63/65/66 byte-identical
    (four names, same operators, same 211 Hz). Spread into the ladder GM
    implies using the three levers this operator set offers — base frequency,
    op5's volume (the 2.5x partial) and release — with patch volume
    compensating the level op5 carries: HiBongo 250 Hz / 150 ms, LoBongo
    180 Hz / 220 ms, MHConga 211 Hz / 330 ms (kit pitch; muting raises pitch,
    so it correctly sits above the open conga), OHConga 190 Hz / 350 ms,
    LoConga 127 Hz / 400 ms, LoTimbl 225 Hz / 620 ms, HiTimbl 290 Hz / 700 ms.
    Loudness across the family −21.1 to −23.6 LUFS. MHConga needed a level edit
    to reach that band: its 50 ms decay had it 5 LU down, and raising it without
    lengthening it costs the whole available budget — patch volume to 2.0 (the
    `Level` knob's max) plus carrier op1 from 0.8 to 1.0 (`fmo_set_volume`'s
    clamp), +3.9 dB, −26.9 -> −23.1 LUFS, tail unchanged at 0.271 s. LoBongo needed one extra
    lever: seeded from the Rail bell template, every operator but its carrier
    is a square, so ringing one as the partial put its centroid at 4.8 kHz
    against 168..494 Hz for its siblings; its op5 is switched to a sine.
    `VOICE_EDITS` grew an `opwave` key for that. NOTE: `opvol`/`opwave` keys
    are 0-based indices into `ops[]`; everything a human reads is 1-based.
  - Presets kept and `Instr` left clamped, both by decision: `unit_load_preset`
    is the only unit → host parameter push the SDK has, and the host clamps
    parameter values, so a wrapping `Instr` knob is not implementable without
    declaring the parameter over several laps of 59.
  - `VOICE_EDITS` now also carries patch volume, filter fields and per-operator
    volumes (`opvol`), so all of this stays in the generator.
- **Verification:**
  - Compiles for the real target (`armv7-a`, `-mfpu=neon-vfpv4`) with
    `arm-linux-gnueabihf-g++`; links to a `.drmlgunit` shared object exporting
    all `unit_*` symbols (~27 KB text).
  - Functional test (`/tmp/test_effeesp32.cc`) run under **qemu-arm**: all 59
    instruments and all 18 algorithms produce finite audio; single hits peak
    ~0.24–0.87; idle output is silent; 8-voice polyphony works.

> Note: the canonical build is the logue-SDK **Docker** toolchain
> (`build drumlogue/EffeESP32`). The cross-compiler + qemu path above is only an
> out-of-tree verification convenience; do not change `config.mk`'s inline-limit
> flags for it (newer host GCCs reject the large `max-inline-insns-single` value,
> but the SDK's Docker GCC accepts it — that is the intended build).

---

## Next

1. **Build in the official Docker image** and load on hardware to confirm the
   `.drmlgunit` is accepted (`dev_id` `0x46654465`, `unit_id` `0x34`, version
   `1.0.0`) and audibly correct.
2. **Tune levels/voicing** against the original ESP32 firmware. `MASTER_GAIN`
   (constants.h) is 0.50, chosen so a single hit clears the output ceiling on
   its own and the limiter only acts on stacks; the measured loudness/envelope
   curve is in the README. Nothing clips (worst peak −0.39 dBFS, no non-finite
   samples at 8 voices on any of the 59). The unit now measures −20.5 LUFS mean,
   which is quiet against the rest of the repo — that is deliberate, but it is
   the number to revisit first if it does not sit right on hardware. (It was
   −19.5 before the voicing edits; shorter envelopes carry less energy through
   the gated 400 ms LUFS window, so about 1 LU of that drop is the shortening
   itself rather than a level change.)
   Per-patch `volume` came straight from the JSON and spans 26 dB; the loudest
   patches produce voices above unity on their own (slot 55 peaks at 2.68 with
   no master gain at all), which is what forces the trade.
3. **Choke groups**: the original `FmDrumPatch` has `chokeGroup` (e.g. open vs
   closed hi-hat). It is currently dropped. Re-add `chokeGroup` to the patch
   struct + generator and implement choking in the allocator.
4. **Pan from patch**: original stores per-patch `pan`; we load it and now
   apply upstream's equal-power law (see the import-fidelity bullet). Only 21
   of the 128 source slots are off centre and none past |0.23|, so the image is
   narrow by design — verify on hardware.

## TODO / Known limitations

- [ ] **Import the remaining instruments from the original JSON.** Only 59 of the
      128 `Drumkit_default.json` slots are currently included (47 GM + 12 unique
      extras; empty-named and parameter-duplicate slots are skipped by
      `tools/gen_patches.py`). Decide a policy for the remaining slots — e.g.
      include all non-empty slots even if their parameters duplicate an existing
      patch (they map to different MIDI notes), or hand-name the duplicate
      families (Noise Bell / Chime / Tight Clap / Tick Click / Glass FX /
      Rail bell cycles at 82–127) — then relax the dedupe in the generator and
      regenerate. Watch the `Instr` param `max` in `header.c` and `P_INSTR` range
      when the count changes.
- [ ] **Choke groups not implemented** (hi-hats won't cut each other).
- [ ] **Reverb send dropped** — drumlogue has its own master FX; `reverbSend`
      from the JSON is intentionally ignored.
- [x] ~~**Waveforms 5–9** fold onto their positive base shapes~~ — done.
      `fmo_waveform_t` now carries all ten shapes, `fmo_wf_render` dispatches
      them and the generator's `WF` map is the identity. This was not cosmetic;
      see the import-fidelity bullet above.
- [ ] **Operator ratios/detune not individually exposed** in the UI (the 6 op
      *levels*, a global Detune and a global Feedbk macro are). The carrier
      *waveform* is now exposed via the combined Filter selector (op 0 only); a
      modulator-waveform or per-op ratio macro could follow if slots are freed.
- [ ] **All 24 parameter slots are now used** — adding a feature means
      repurposing/encoding an existing one (as the Filter param already encodes
      both filter state and carrier waveform).
- [ ] **Load-order dependence, at boot** (was "startup timbre nuance"): at boot
      the runtime pushes every parameter, so whichever it pushes *after* index 0
      overrides the instrument's stored values — a freshly loaded Crash1 gets
      the header's 200 ms decay, while the same instrument selected by hand gets
      its own. Selecting the instrument **from the preset UI** now restores the
      patch reliably (that is what `unit_load_preset` is for; see the README's
      *Instrument selection and the panel*), and so does re-selecting it with
      the `Instr` knob as far as the engine is concerned. What is left is only
      the first-boot state, before anything is selected. The SDK has no
      unit → host parameter push at all — `unit_runtime_desc_t` carries the
      sample-bank accessors and nothing else — so a unit that rewrites its own
      parameters has exactly one sanctioned way to tell the host about it, and
      it is the preset call.
- [ ] **True 4-voice SIMD FM** is not attempted (data-dependent routing per
      algorithm). Only the mix stage is vectorized.
- [ ] No automated *unit* test is committed; `/tmp/test_effeesp32.cc` is
      throwaway. Two host measurement harnesses now are, though, and both
      cross-compile the real NEON build and run it under qemu:
      `platform/drumlogue/tools/level_meter` (loudness, peak, DC, harmonic cost)
      and `platform/drumlogue/tools/stack_meter` (polyphonic distortion,
      retrigger continuity).

---

## Instructions for the next AI agent

- **Patch data is generated — never hand-edit `drum_patches.h`.** Change
  `tools/gen_patches.py` and re-run it (download the JSON first; see the header
  of that script for the exact `curl` command and commit hash).
- **Keep the 18 algorithms byte-faithful** to upstream `FmVoice6.h`; the timbres
  depend on the exact routing and the `1/√N` carrier normalisation.
- **Parameter order in `header.c` must match the `P_*` enum in `synth.h`.** If
  you add/remove a parameter, update both, plus `load_instrument()` /
  `apply_param()` and the README table.
- **Real-time rules:** no `malloc`/`new` in the audio path, no locks, no
  unbounded loops. Scratch buffers are fixed (`EFFEESP32_MAX_BLOCK = 64`); render
  is chunked, so arbitrary `frames` are safe.
- **To verify without hardware/Docker:**
  ```sh
  apt-get install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf qemu-user
  cd platform/drumlogue/EffeESP32
  arm-linux-gnueabihf-g++ -std=gnu++14 -fconcepts -march=armv7-a -mfpu=neon-vfpv4 \
      -mfloat-abi=hard -O2 -ffast-math -D__NEON__ -static -I. -I../common \
      <your_test>.cc -o /tmp/t && qemu-arm -L /usr/arm-linux-gnueabihf /tmp/t
  ```
- **Provenance / license:** preserve the MIT attribution headers when copying
  more upstream code; the drumkit data and algorithms are © Copych (MIT).

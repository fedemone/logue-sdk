# Brachetti common library (`brachetti_common`)

> **Disclaimer:** the Brachetti units are unofficial, independently developed units, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from their use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The code the nine Brachetti family units share: [`brachetti_kick`](../brachetti_kick/),
[`brachetti_tom`](../brachetti_tom/), [`brachetti_kernel`](../brachetti_kernel/),
[`brachetti_snare`](../brachetti_snare/), [`brachetti_bar`](../brachetti_bar/),
[`brachetti_plate`](../brachetti_plate/), [`brachetti_cymbal`](../brachetti_cymbal/),
[`brachetti_noise`](../brachetti_noise/) and [`brachetti_world`](../brachetti_world/) (see
[the Brachetti family](../tools/brachetti_equiv/README.md)).  It is not a unit:
there is no Makefile here, so `build -l` does not list it.  Units include its
headers by relative path (`#include "../brachetti_common/knobs.h"`), which
works the same in the SDK's Docker build, in `arm_size.sh` and in the
equivalence driver.

The original 40-preset [`brachetti`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/) does not use this library;
it keeps its own code.

## What is here

Header-only C++14, everything in namespace `brc` (except the original support
headers' `FastSVF`, `FastNoise`, `FastEnvelope` and the `float_math.h`
functions).  Each header opens with a **Provides** list; the index below has
every helper, what it does and which units use it.

| File | What it holds |
|---|---|
| `dsp_math.h` | constants and small math: clamps, anchored 2^x, envelope steps, phase increments |
| `knobs.h` | what a knob means: raw value → DSP value, anchors, the knobs the host owns, Velocity, the played note, display strings |
| `master_stage.h` | the output side: Tone tilt, limiters, bus helpers, `MasterControls`, `MasterStage` |
| `small_filters.h` | one-pole low/high-pass steps and coefficient, the two-pole resonator |
| `noise.h` | random sources: xorshift32, `FastNoise`, an LCG, pink noise |
| `exciter.h` | `Exciter`: the mallet and the two-band noise burst |
| `voice_base.h` | `VoiceBase` and the voice-set helpers |
| `voice_layers.h` | voice layers more than one family uses: chirp, onset, pitch envelope, boom, transient, noise gate |
| `models.h` | the Model knob |
| `modal_bank.h` | the six-mode bank and the knobs that shape it |
| `modal_synth.h` | `ModalSynth`: the engine of the six modal units |
| `float_math.h`, `filter.h`, `envelope.h` | the original's support headers: fast math approximations, the TPT state-variable filter, the AR/ADSR envelope |

## Helper index

Before writing a helper, look for it here.  "Used by" names the units whose
own files use it; "library" means it is used inside the library (the five
modal units reach those through `ModalSynth`).  Every unit file opens with a
**From brachetti_common** block listing the helpers it uses — see
[Using and adding helpers](#using-and-adding-helpers).

### `dsp_math.h` — constants and small math

| Helper | What it does | Used by |
|---|---|---|
| `kSampleRate`, `kInvSampleRate` | 48 kHz (the drumlogue's only rate) and its inverse | kernel, snare, plate, cymbal, noise, world; library |
| `kLn0001` | ln 0.001: a T60 is a −60 dB decay | cymbal, world; library |
| `kSilence` | 1e-5: below this a layer counts as finished | kick, plate, world; library |
| `clampf(x, lo, hi)` | clamp, as `fmaxf(lo, fminf(hi, x))` | kick, tom, kernel, snare, cymbal, noise, world; library |
| `or_default(value, unset, fallback)` | a voicing-table value, or the fallback when the table leaves it unset (below `unset`) | snare, plate |
| `knob_exp2(x)` | 2^x for anchored knob curves: `fasterpow2f`, but exactly 1.0 at 0 | kick, tom, kernel, snare, cymbal, noise; library |
| `env_mul(tau, sr)` | per-sample multiplier of a geometric envelope with time constant tau (exact `expf`) | cymbal; library |
| `phase_inc(hz)` | radians per sample of an oscillator at hz | kick, tom, snare, cymbal, noise, world; library |
| `wrap_phase(phase)` | keep a phase inside one turn | kick; library |
| `preset_column_in(table, col, lo, hi)` | compile-time check that a preset-table column stays in range (pins the knobs a preset must not ship) | all nine |

### `knobs.h` — what a knob means

| Helper | What it does | Used by |
|---|---|---|
| `knob::mallet_res`, `mallet_stiff`, `decay`, `release`, `noise_mix`, `noise_res`, `material`, `inharm`, `hit_pos`, `tube_radius`, `percent`, `poly`, `noise_freq`, `gain_drive`, `clamp_int`, `unit_range` | a knob's raw `header.c` value → the number the DSP uses (ranges in the file) | per knob: kick, tom, kernel, snare, cymbal, noise; library |
| `Anchors` | the knob values the current preset shipped with (REFERENCE ANCHORS) | kernel, cymbal, noise; library |
| `off_anchor(delta)` | has a knob moved off its anchor? (\|knob − anchor\| > 0.001) | kick, tom, cymbal; library |
| `off_unity(factor)` | the same for an anchored factor (exactly 1 at the anchor) | kick; library |
| `HostKnob`, `is_host_knob`, `seed_host_knobs` | the knobs the host owns (Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc): one table with their defaults drives both "LoadPreset leaves them alone" and "Init seeds them" | kernel, cymbal, noise; library |
| `midi_velocity(v)` | MIDI velocity 0-127 → 0..1 | kernel, cymbal, noise; library |
| `VelocityBias` | the Velocity knob: ghost … as played … wham, exact at 0 | kernel, cymbal, noise; library |
| `SequencerNote` | the note the sequencer plays: preset root + Pitch, and what GateOff must release | kernel, cymbal, noise; library |
| `name_at(names, idx, fallback)` | a name from a fixed table, or the fallback; the table must be a namespace-scope `static` array (see the rules) | all nine; library |
| `knob_text(slot)`, `kKnobTextSize` | the buffer a knob's display string is built in, one per knob slot, private to the unit | kernel, cymbal, noise; library |
| `format_int`, `format_percent`, `format_hz` | knob display strings ("40", "Rs40%", "1.2kHz") into the caller's buffer | kernel, cymbal, noise; library |

### `master_stage.h` — the output side

| Helper | What it does | Used by |
|---|---|---|
| `tone_tilt(x, lp, tone)` | the Tone knob's tilt EQ on one sample | all nine |
| `brickwall(x)`, `kBrickwall` | the final ±0.99 safety clamp | kernel; library |
| `tanh_knee(x, thr, span, inv_span)` | a peak limiter on one sample: unity below thr, a tanh knee above | kernel |
| `soft_saturate_bus(out, frames, k)` | x / (1 + k·\|x\|) over a bus: soft headroom for stacked voices | cymbal |
| `write_stereo(out, frame, x)` | a mono sample to both channels | kernel; library |
| `MasterControls` | the master knobs alone (Tone, Cutoff / Resnc, Gain) for a unit with its own stage | kernel |
| `MasterStage` | the controls + bus gain, preset trim, preset-change hold, idle guard and the transparent limiter | kick, tom, snare, bar, plate, cymbal, noise, world |

### `small_filters.h` — one- and two-pole filters

| Helper | What it does | Used by |
|---|---|---|
| `one_pole_lp(x, a, state)` | one step of a one-pole low-pass, `state += a·(x − state)` | kernel, cymbal, world; library |
| `one_pole_hp(x, a, state)` | x minus its one-pole low-pass (two in cascade: a DC blocker) | plate, cymbal; library |
| `one_pole_coeff_fast(hz)` | the one-pole coefficient for a cutoff swept at control rate (fast exp) | cymbal |
| `Resonator2` | a two-pole resonator: `tune(hz, r)`, `ring(x)` | snare, world |

### `noise.h` — random sources

| Helper | What it does | Used by |
|---|---|---|
| `xorshift32(state)` | one step of the xorshift32 generator | kernel; library |
| `FastNoise` | white noise in [−1, 1) from xorshift32 | world; library (the exciter, `Transient`) |
| `lcg_unit(state)` | one step of an LCG, as a float in [0, 1) | cymbal |
| `PinkFilter` | white → pink noise (Paul Kellet's refined filter) | cymbal |

### The voice

| Helper | Header | What it does | Used by |
|---|---|---|---|
| `Exciter` | `exciter.h` | the mallet and the two-band noise burst, with a setter per knob | noise; library |
| `NoiseBurst` | `exciter.h` | one sample of the noise burst in parts, so a unit can reshape a part | snare, plate |
| `noise_filter_name` | `exciter.h` | the NzFltr knob's display names | noise; library |
| `VoiceBase` | `voice_base.h` | what every exciter-driven voice has, and its life cycle | kick, tom, snare, bar, plate, noise, world |
| `next_voice`, `fuses_with` | `voice_base.h` | round-robin allocation within Poly; roll fusion | noise; library |
| `any_fading`, `fade_out_all` | `voice_base.h` | the preset-change fade of the voice set | cymbal, noise; library |
| `set_all_exciters(voices, setter, value)` | `voice_base.h` | one knob into every voice's exciter | noise; library |
| `kNumVoices`, `kRollFuseSec`, `kPresetFadeTauSec`, `kSquelch*` | `voice_base.h` | the voice count, roll window, fade and squelch constants | kick, tom, snare, cymbal, noise, world; library |
| `MetalChirp`, `Onset`, `PitchEnv`, `Boom`, `Transient`, `NoiseGate`, `GlideCurve` | `voice_layers.h` | shared voice layers (the table in the file says which family uses which); `GlideCurve` is a held, then S-shaped bend | kick, tom, snare, bar, plate, noise, world |
| `model_name`, `model_noise_band_mix`, the Model enum | `models.h` | the Model knob | noise; library |
| `ModalPresetConfig`, `ModalBank` | `modal_bank.h` | a preset's calibrated bank; the bank itself (NEON path) | kick, tom, snare, bar, plate, world |
| `shape_modal`, `strike_modal`, `ModalShape`, `ModalKnobs`, `format_mode_count` | `modal_bank.h` | the knobs on the bank, before and at the strike; Partls' display | library (`ModalSynth`) |
| `ModalLayout`, `kModalHostKnobs`, `ModalSynth` | `modal_synth.h` | the 24-knob layout and the engine of the six modal units | kick, tom, snare, bar, plate, world |

`ModalSynth` also hands its units these building blocks for their `NoteOn`:
`strike_velocity`, `allocate_voice`, `shape_exciter`, `modal_knobs`,
`strike_modal_bank` and `load_modal_bank`.

### The original's support headers

| Helper | Header | Used by |
|---|---|---|
| `FastSVF` (TPT state-variable filter) | `filter.h` | plate (Tick's hat filter); library (noise filter, master Cutoff) |
| `FastEnvelope`, `ENV_*` | `envelope.h` | library (the exciter's envelopes) |
| `fastexpf`, `fastsinf`, `fasterpowf`, `fastersinfullf`, `fastertanhf`, `fasterpow2f`, `fastercosfullf`, … | `float_math.h` | cymbal, kick; library.  For a knob's response or an envelope sweep only: tuning and decay coefficients use exact `expf` / `cosf` / `exp2f` |

## Using and adding helpers

- **Look here first.**  If a unit needs a clamp, a one-pole, a resonator, a
  random source, a display string or a knob conversion, it is probably in
  the index above.
- **A unit file lists what it uses.**  Each `presets.h`, `voice.h`,
  `synth_engine.h` (and the kernel's `modal_drum_kernel.h`) opens with a
  `From brachetti_common` block naming every library header it includes and
  the helpers it uses from each.  [`lib_uses.py`](../tools/brachetti_equiv/lib_uses.py)
  keeps those blocks true:

      cd ../tools/brachetti_equiv
      python3 lib_uses.py --write     # after using a new helper, or dropping one
      python3 lib_uses.py --check     # before a commit: exits 1 if a block is stale
      python3 lib_uses.py --index     # helper → the units that use it

- **A new helper goes here** when it is generic (it carries no family's
  voicing) and more than one place could use it — even if only one does
  today.  Add it to its header's **Provides** list and to this index, with
  the exact arithmetic of the code it replaces, then run `run_equiv.sh` on
  every unit that uses it.  A family's own layers (the snare wires, the
  plate's shimmer, the kick's thump, the cymbal and kernel voices) stay in
  their unit.

## How a unit is laid out

```
presets.h        the knob layout, the presets, their voicing tables
voice.h          one voice: VoiceBase + the layers the family uses
synth_engine.h   the family: presets and knobs → voices, voices → the master stage
unit.cc          the SDK callbacks (unchanged from the SDK template)
header.c         the knobs as the drumlogue sees them
```

The six modal units derive their engine from `ModalSynth` (CRTP: static
dispatch, no virtual calls) and supply only their family: the strike recipe
(`NoteOn`), the per-voice render and what a preset loads into a voice.  The
noise and cymbal units have their own knob layouts and engines built from the
same pieces.  The kernel keeps its drum in `modal_drum_kernel.h` (tables in
`modal_drum_data.h`) instead of `voice.h`.

The knobs a host owns (Pitch, Poly, Velocity, VlMllRes, Tone, Cutoff, Resnc)
are one table per layout — `kModalHostKnobs` in `modal_synth.h` for the six
modal units, `kHostKnobs` in `presets.h` for the others — with their
`header.c` defaults.  Each `.h` file opens with its `From brachetti_common`
block (see [Using and adding helpers](#using-and-adding-helpers)).

## Rules

- **Every preset split from the original renders bit-identically to it** at the knob values
  it ships with (BrcWorld and the presets added after the split have no
  counterpart there) (the [equivalence check](../tools/brachetti_equiv/README.md)).
  A change in this folder reaches every unit, so check all nine:

      cd ../tools/brachetti_equiv
      for u in kick tom kernel snare bar plate cymbal noise world; do ./run_equiv.sh ../../brachetti_$u; done

- **Keep the float operations in their order.**  The host check is built
  without `-ffast-math`, so `(a * b) * c` and `a * (b * c)` are different
  numbers.  Moving a computation into a helper is safe; reordering it is not.
- **Keep a variable divisor a variable.**  Under the units' `-ffast-math` a
  division by a compile-time constant becomes a multiplication by its
  reciprocal, which moves a coefficient by a rounding step — on a resonator
  bank ringing for seconds that is a measurable phase drift on ARM (the
  kernel keeps its sample rate in `m_sr` for this reason).
- **REFERENCE ANCHORS.**  A preset captures its shipped knob values through
  the same `knob::` functions the live mappings read, and every mapping is a
  function of the delta from them, exact at delta 0 (`knob_exp2(0)` is exactly
  1.0; `fasterpow2f(0)` is not).  (The original clamped the HitPos anchor to
  0..1 while the knob is bipolar; every shipped row is >= 0, so the two agree.)
- **Exact math for coefficients.**  Tuning and decay coefficients use exact
  `expf` / `cosf` / `exp2f`: the `faster*` approximations in `float_math.h`
  are badly wrong for small arguments and shift low mode frequencies.
- **Per-sample code must stay inline.**  GCC's size heuristics can leave a
  method the render loop calls every sample out of line, which costs a call
  per sample and shows up in `arm_cpu.sh` (the plate's `excite()` did, at
  +1.7 %).  Mark such a method `force_inline` (the SDK's
  `common/attributes.h`), and compare `arm_cpu.sh` before and after any change
  to code a render loop runs.
- **Nothing shared between units.**  A static local of an inline function or
  a template — a table, a buffer — gets GCC's `STB_GNU_UNIQUE` binding, and
  the dynamic loader keeps ONE copy of a unique symbol for every library that
  defines it, drumlogue units included.  Every family unit's preset-name table
  was once `preset_name()::names`, so all of them showed the first-loaded
  unit's Program names (BrcKernel read "Marmba", "TblrBel").  Tables and
  buffers live at namespace scope with `static` (each unit's `kPresetNames`,
  `kModelNames`, ...) or in a `static` function (`knob_text`).
  `arm_size.sh` fails a unit that has a unique symbol, and
  [`multi_load.sh`](../tools/brachetti_equiv/multi_load.sh) loads all nine
  units into one process and checks every display string against the unit
  loaded alone.
- **No constructors.**  Plain structs with default member initialisers, no
  virtual functions, no heap: each unit's global synth object stays
  constant-initialised and loads from the ELF instead of being built at run
  time.  Check `arm_size.sh` after adding members or tables.
- **Only what is shared, and only as it is.**  A change of voicing belongs to
  the unit it is for.  When one family needs a variant of a shared piece, the
  variant stays in that unit (the snare's wires, the plate's shimmer, the
  kick's thump, the cymbal and kernel voices).

## Quirks kept on purpose

The refactor changed no output, so the original's behaviour stays where it is
arguably a bug.  Each is noted at its place in the code:

- Plate: Cowbell's and BellTree's shimmer never decays (its decay is 1.0); the
  voice ends when the rest of it does.
- Plate: a voice still fading out when the preset changes to Tick keeps its
  previous `use_hat` flag until it is struck again.
- Modal units: re-selecting the current preset while a voice rings reloads
  that voice's per-voice tables (`load_voicing_into`) under it, as the
  original does.  The two places where the original then writes state the
  family units no longer have are the documented
  [known differences](../tools/brachetti_equiv/README.md#known-differences).

## Helpers that moved here from the units

Each was a generic piece of a unit's code (or the same step written in
several places); it now has one definition here, with the same arithmetic,
and the units call it:

| Helper | Was in |
|---|---|
| `one_pole_lp`, `one_pole_hp` | cymbal (drivers, DC blockers, level follower), kernel (noise wedge), plate (shimmer), and inside the library (exciter bands, `Transient`) |
| `one_pole_coeff_fast` | cymbal (`pole_coeff`) |
| `Resonator2` | snare (`SnareWires::Band`) |
| `lcg_unit`, `PinkFilter` | cymbal (`frand`, `pink`) |
| `xorshift32` | kernel (noise wedge) and `FastNoise` |
| `tanh_knee`, `brickwall`, `write_stereo` | kernel (`TanhKnee`, `clip`, `write_stereo`) and `MasterStage` |
| `soft_saturate_bus` | cymbal (`soft_saturate`) |
| `off_anchor`, `off_unity` | cymbal (`moved`), kick, tom, `modal_bank.h` (`moved`) |
| `or_default` | snare (`or_default`), plate (Tick's hat Q) |
| `HostKnob`, `is_host_knob`, `seed_host_knobs` | every unit's `loads_with_preset` and `seed_performance_params` |
| `set_all_exciters` | noise and `ModalSynth` (`set_exciters`) |
| `name_at` | every unit's `preset_name`, `model_name`, `noise_filter_name` |
| `format_percent` | cymbal and kernel (Partls' display) |
| `knob_text` | the display buffers every `getParameterStrValue` kept as static locals (shared between units — see the rules) |
| `knob::noise_freq`, `knob::gain_drive` | kernel (`noise_freq`, its reference drive) and `MasterControls` |

## What was removed, and where it still lives

- **Each unit's `dsp_core.h`** became its `voice.h` plus this library.
- **Each unit's own copies** of `float_math.h`, `filter.h`, `envelope.h` and
  `noise.h`: one copy here.
- **`project.mk`** in every unit: the build script reads it only for the
  platforms with a `manifest.json`; drumlogue projects are found by their
  `Makefile` and `config.mk` (as the SDK's `dummy-synth`).
- **`ChannelPressure` / `Aftertouch` stubs**: no unit callback calls them.
- **Cymbal:** `cym_one_pole_low`, which nothing called after the driver
  filters moved to control rate (its two halves are `pole_coeff` and
  `one_pole`), and the strike's phase-mod amount, which was always 1.0.
- **Kernel:** `ModalDrumKernel::Init`, which nothing called, and
  `Deactivate` / `LiveVoices` / `ModeCount` / `LiveModes`, the introspection
  the original's `kernel_cpu_probe.cpp` reads.  They are still in
  [`../brachetti/modal_drum_kernel.h`](https://github.com/fedemone/logue-sdk/blob/original_brachetti/platform/drumlogue/brachetti/modal_drum_kernel.h);
  take them from there if a CPU probe is built for the kernel unit.
- **The Koto's Karplus-Strong waveguide** was never part of a family unit; it
  lives on in the original (see the family README).

# Brachetti common library (`brachetti_common`)

> **Disclaimer:** the Brachetti units are unofficial, independently developed units, not affiliated with or supported by KORG. Provided "as is" with no guarantee of correct operation; the developer(s) and distributor(s) accept no liability for any damage, defect, or problem resulting from their use. See the [repository disclaimer](../../../README.md#disclaimer) for full terms.

The code the eight Brachetti family units share: [`brachetti_kick`](../brachetti_kick/),
[`brachetti_tom`](../brachetti_tom/), [`brachetti_kernel`](../brachetti_kernel/),
[`brachetti_snare`](../brachetti_snare/), [`brachetti_bar`](../brachetti_bar/),
[`brachetti_plate`](../brachetti_plate/), [`brachetti_cymbal`](../brachetti_cymbal/)
and [`brachetti_noise`](../brachetti_noise/) (see
[the Brachetti family](../tools/brachetti_equiv/README.md)).  It is not a unit:
there is no Makefile here, so `build -l` does not list it.  Units include its
headers by relative path (`#include "../brachetti_common/knobs.h"`), which
works the same in the SDK's Docker build, in `arm_size.sh` and in the
equivalence driver.

The original 40-preset [`brachetti`](../brachetti/) does not use this library;
it keeps its own code.

## What is here

Header-only C++14, everything in namespace `brc`.

| File | What it holds | Used by |
|---|---|---|
| `dsp_math.h` | the sample rate, `clampf`, `knob_exp2` (2^x, exactly 1 at 0), `env_mul` (geometric-envelope step), `phase_inc`, `wrap_phase`, `preset_column_in` (compile-time check on a preset-table column) | all |
| `knobs.h` | `knob::` raw value → DSP value for every shared knob; `Anchors` (the REFERENCE ANCHORS); `VelocityBias` (the Velocity knob); `SequencerNote` (root + Pitch → the note the sequencer plays); `format_int` / `format_hz` display strings | all |
| `master_stage.h` | `tone_tilt` (Tone); `MasterControls` (Tone, Cutoff / Resnc, Gain); `MasterStage` (the controls + bus gain, preset trim, preset-change hold, idle guard, transparent limiter) | all; the kernel uses `MasterControls` and runs its own limiter |
| `exciter.h` | `Exciter`: the mallet and the two-band noise burst every exciter-driven voice starts from | kick, tom, snare, bar, plate, noise |
| `voice_base.h` | `VoiceBase`: voice life cycle (squelch, preset-change fade, retirement); `next_voice`, `fuses_with` (roll fusion), `any_fading`, `fade_out_all` | kick, tom, snare, bar, plate, noise; the cymbal uses the last two |
| `voice_layers.h` | `MetalChirp`, `Onset`, `PitchEnv`, `Boom`, `Transient`, `NoiseGate` — layers more than one family uses | see the table in the file |
| `models.h` | the Model knob: its names and noise-band defaults | units with a Model knob |
| `modal_bank.h` | `ModalPresetConfig`, `ModalBank` (up to six modes, NEON path), `shape_modal` / `strike_modal` (the modal knobs) | kick, tom, snare, bar, plate |
| `modal_synth.h` | `ModalLayout` (the 24-knob layout) and `ModalSynth<Unit, Voice>`: the engine the five modal units share — lifecycle, presets, knobs, note routing, the block loop | kick, tom, snare, bar, plate |
| `float_math.h`, `filter.h`, `envelope.h`, `noise.h` | the support headers (fast math approximations, the TPT state-variable filter, the AR/ADSR envelope, xorshift noise) each unit used to carry its own copy of — the original's, unchanged | all |

## How a unit is laid out

```
presets.h        the knob layout, the presets, their voicing tables
voice.h          one voice: VoiceBase + the layers the family uses
synth_engine.h   the family: presets and knobs → voices, voices → the master stage
unit.cc          the SDK callbacks (unchanged from the SDK template)
header.c         the knobs as the drumlogue sees them
```

The five modal units derive their engine from `ModalSynth` (CRTP: static
dispatch, no virtual calls) and supply only their family: the strike recipe
(`NoteOn`), the per-voice render and what a preset loads into a voice.  The
noise and cymbal units have their own knob layouts and engines built from the
same pieces.  The kernel keeps its drum in `modal_drum_kernel.h` (tables in
`modal_drum_data.h`) instead of `voice.h`.

## Rules

- **Every unit renders bit-identically to the original** at the knob values
  its presets ship with (the [equivalence check](../tools/brachetti_equiv/README.md)).
  A change in this folder reaches every unit, so check all eight:

      cd ../tools/brachetti_equiv
      for u in kick tom kernel snare bar plate cymbal noise; do ./run_equiv.sh ../../brachetti_$u; done

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
  [`../brachetti/modal_drum_kernel.h`](../brachetti/modal_drum_kernel.h);
  take them from there if a CPU probe is built for the kernel unit.
- **The Koto's Karplus-Strong waveguide** was never part of a family unit; it
  lives on in the original (see the family README).

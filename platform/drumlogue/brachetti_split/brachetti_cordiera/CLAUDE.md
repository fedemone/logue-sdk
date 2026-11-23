# Brachetti Cordiera – Session Brief

`brachetti_cordiera` (`BrcCordiera` on the drumlogue) is the eleventh Brachetti
family unit: instruments whose voice is a wire (*cordiera*: a drum's snare
wire).  Tabor, Daf, MbiraBz, TomBuzz on BrcSnare's engine with the wires driven
by the head; Marranz, Spring, Gopichd on BrcWorld's berimbau wire, made stiff,
tuned through its filters and able to bend.  Its instruments are its own.
README.md has the engines, the knobs, the voicing and the credits.

## Before changing anything

- **No original, no recordings.**  `run_equiv.sh` does not apply.  Before a
  refactor, render every preset (`preset_probe render 0 6 110 3 dir`, built
  against this directory) and compare after: a refactor must leave the files
  byte-identical.
- **The voicing is by design, to measured targets** (README.md, "How the
  instruments were voiced"): the share of the energy above 2 kHz and the
  kurtosis of the 2-8 kHz band for the wires on a head, spectrograms and the
  pitch track for the wire instruments.  When a reference recording exists,
  voice against it with `match_samples.py` (`match_specs.py`'s `unit_rows`
  reads a preset's rows from `presets.h` for a spec) and patch the columns
  it frees; then `preset_probe calib` and update `kOutTrim`.
- **The field order of `Voicing`** must match `VOICING_FIELDS["cordiera"]` in
  `match_samples.py`.  A preset is a wire instrument when `bw_level` > 0.
- **The two engines are BrcSnare's and BrcWorld's**, checked bit-identical to
  them with the additions off, on their presets (commit e7eeca5).  Keep their paths
  separate from the additions: a snare with `head_follow` 0 runs
  `SnareWires::process()`, a wire with `bw_stages` 0 and no bend runs
  BrcWorld's loop.  The wire strike re-applies the NzRes setter and shapes the
  exciter with VlMllRes at 0, as BrcWorld's does.
- **Snare branches come from the table** (`snare_kind`, 0 on every preset
  here).  The wires' low band is `band_a_hz` (0 = the kind's default).
- **Stability.**  The all-passes are stable for any coefficient under 1 (held
  under `kMaxStiff`, 0.97); the half-period guard in `strike_wire` keeps the
  pitch.
- **Shared code.**  The engine is `../../brachetti_common/modal_synth.h`; a
  change in the library reaches all twelve units.  Each file's `From
  brachetti_common` block lists the helpers it uses
  (`../../tools/brachetti_equiv/lib_uses.py --write` / `--check`).
- **Knobs: common, then dedicated.**  BrcSnare's layout and names (`Buzz`,
  `BuzzTon`, `Crack`, `Tight`, `Punch` in Partls' slot, `BuzzLen`), `Crack`
  host-owned; static_asserts pin it and Punch to 0 on every row, and
  `kShippedPartls` holds Partls.  header.c's defaults equal preset 0's row.
- **Nothing shared between units** (`kPresetNames`, `s_wire_lines` at
  namespace scope, `static`), **no user-defined constructors**, **per-sample
  code `force_inline`**.
- **Ringing on.**  `unit.cc` runs the engine as `brc::RingOver` (two copies,
  `../../brachetti_common/ring_over.h`): a Program change while a sound rings
  lets it ring out.  The engine itself is unchanged, so `run_equiv.sh` (which
  drives it directly) still compares it with the original; the ring-over is
  checked by `brachetti_check.sh`.  A buffer outside the class (a delay line)
  needs one bank per copy (`SetBank`, as in BrcLippa).
  The two copies share one instrument's voice budget (the CPU: two
  instruments' voices stopped the drumlogue's audio): the engine provides
  `LiveVoices()`, `VoiceBudget()` and `FadeOldestVoice()`; `ring_budget_check.sh`
  must pass.
- **Also played by [`brachetti`](../../brachetti/README.md)**, which compiles this
  family's `synth_engine.h` inside a namespace (`fam_<family>`) as one of its
  twelve presets.  So: no bare `::` before a family name, and a new
  `#include` from outside this folder must also go in `../../brachetti/families.h`.
  After a change here run `../../tools/brachetti_equiv/brachetti_check.sh` too
  (it must stay IDENTICAL: Brachetti plays this family bit for bit as this unit).

## Measure, do not assume

- ARM sizes: `../../tools/brachetti_equiv/arm_size.sh .`
  (shipped: .text 23172 · .rodata 2652 · .data.rel.ro 76 · .data 4 · .bss 36048).
- CPU: `../../tools/brachetti_equiv/arm_cpu.sh .`
  (shipped: 1072.1 ARM instructions per rendered frame).
- Knobs: `../../tools/brachetti_equiv/knob_liveness.py cordiera`.

Not yet run on hardware.

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
- **24-parameter GUI** (`header.c`) with proper SDK param types (strings, semi,
  percent, pan, msec, hertz, on/off, **midi_note**).
- **Trigger Note** (param 22): each instrument carries its canonical MIDI note
  (`g_drum_inst_notes[]` in the generated table — GM note for 35–81, original
  slot index for the extras). Selecting an instrument reloads `Note` to that
  value; `GateOn` triggers the assigned note.
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
   (constants.h, 0.5) is a first guess; per-patch `volume` came straight from the
   JSON (some are 2.0). Check the loudest patches (Ride, OpHat) for clipping with
   the drumlogue master path.
3. **Choke groups**: the original `FmDrumPatch` has `chokeGroup` (e.g. open vs
   closed hi-hat). It is currently dropped. Re-add `chokeGroup` to the patch
   struct + generator and implement choking in the allocator.
4. **Pan from patch**: original stores per-patch `pan`; we load it, but the
   non-GM repeats are mostly centered. Verify stereo image.

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
- [ ] **Waveforms 5–9** (negative sine/cos/tri/sqr/saw) fold onto their positive
      base shapes in the generator (`fm_operator.h` only defines 5 waveforms).
      Add the 5 negative variants to `fmo_waveform_t` + `fmo_wf_render` for full
      fidelity, then update `WF` map in `tools/gen_patches.py`.
- [ ] **Operator detune/ratio not exposed** in the UI (only the 6 op *levels*).
      Could add a global ratio/feedback macro if more knobs are freed.
- [ ] **Startup timbre nuance**: at boot the runtime sets every param to its
      header `init`, overriding the loaded instrument's stored values for that
      one boot sound (same behavior as EffeMD). Re-selecting the instrument
      restores its exact values. Could be smoothed with a "first-load" guard.
- [ ] **True 4-voice SIMD FM** is not attempted (data-dependent routing per
      algorithm). Only the mix stage is vectorized.
- [ ] No automated test is committed; `/tmp/test_effeesp32.cc` is throwaway.
      Consider adding a `tools/` host test that stubs `arm_neon.h`.

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

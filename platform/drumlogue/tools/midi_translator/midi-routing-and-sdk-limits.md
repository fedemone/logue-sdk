# drumlogue MIDI routing, firmware encryption, and SDK unit limits

Reference notes for a recurring question: *can the drumlogue's MIDI behaviour be
fixed or extended from the logue SDK — for example by writing a user unit (even a
master effect) that handles the MIDI the firmware handles badly?*

**Short answer: no.** Every MIDI routing/choke behaviour on the drumlogue lives in
the system firmware, which is encrypted and signed, and the SDK gives a user unit
no MIDI actuator to work around it. This document records *why*, with the evidence,
so the question does not have to be re-investigated from scratch each time.

---

## 1. The four MIDI pain points this is about

Stated as user-visible symptoms:

1. **Choke groups do not fire when notes are triggered over MIDI.** They work from
   the internal sequencer; they do not work from external MIDI.
2. **The note→track map in single-channel mode is illogical.** Which incoming note
   number lands on which drum track follows no sensible layout.
3. **Multi-channel mode only allows a contiguous channel block** (e.g. base channel
   1 → tracks on 1..12, base 2 → 2..13, …). You cannot scatter tracks onto
   arbitrary channels.
4. **There is no "two-channel" split mode:** all drum/sample tracks on one channel
   (played as a kit) *and* the MULTI ENGINE (Noise/VPM/user synth) track on a
   separate channel (played chromatically as a melodic voice) at the same time.

All four are properties of how the **instrument** interprets and routes incoming
MIDI. None of them is a property of any single sound-generating unit.

---

## 2. The system firmware is encrypted (so it cannot be reverse-engineered or patched)

The official system updater (`drumlogue_system_update_vX_Y_Z.bin`, from
<https://www.korg.com/us/support/download/software/0/894/4975/>) is a fully
encrypted image. Measured on the v1.2.0 and v1.3.0 updaters:

| Property | v1.2.0 | v1.3.0 | Meaning |
|---|---|---|---|
| Size | 27,476,633 B | 27,475,433 B | — |
| Sliding-window entropy (64 KiB windows) | 7.997 bits/byte across **100%** of the file | same | Indistinguishable from random — no plaintext, no headers, no filesystem |
| Duplicate 16-byte blocks (first 4 MiB) | 0 | 0 | Not ECB — a chained/stream cipher (CBC/CTR/…) or per-image keystream |
| Common prefix between the two versions | 0 bytes | 0 bytes | No shared plaintext header; per-image IV/nonce or header |

There is **no** unencrypted region — not at the start, not at the end, not anywhere.
Loading the `.bin` into Ghidra (or any disassembler) yields only noise. The
decryption key is fused into the drumlogue's SoC/bootloader and is not present in
the download, and the image is signed, so even a hypothetically decrypted-and-edited
image would not boot.

**Consequence:** reverse-engineering or patching the drumlogue's MIDI engine is not
achievable. This is also why every MIDI fix in KORG's own changelog
(Program-Change selection, external clock / effect sync, pitch-bend & pressure
routing, saved tempo, …) shipped as a *firmware update* — those all live on the
locked side of the wall.

Reproduce the entropy analysis with the script in
[Appendix A](#appendix-a--reproducing-the-encryption-finding).

---

## 3. What a user unit can actually see and do

A drumlogue user unit is a **per-track DSP object**. The *entire* interface between
the firmware and a unit is defined in `platform/drumlogue/common/` — there is
nothing else. Summarised:

### 3a. Everything the firmware hands a unit at init (`unit_runtime_desc_t`)

```c
typedef struct unit_runtime_desc {
  uint16_t target;               // platform | module
  uint32_t api;                  // API version
  uint32_t samplerate;           // e.g. 48000
  uint16_t frames_per_buffer;    // block size
  uint8_t  input_channels;       // audio in count
  uint8_t  output_channels;      // audio out count
  unit_runtime_get_num_sample_banks_ptr    get_num_sample_banks;      // sample ROM access
  unit_runtime_get_num_samples_for_bank_ptr get_num_samples_for_bank; // sample ROM access
  unit_runtime_get_sample_ptr              get_sample;                // sample ROM access
} unit_runtime_desc_t;
```

The only "capabilities" passed in are three **sample-bank readers**. There is no
MIDI channel, no track index (a unit does not even know *which* track it is on), no
handle to any other track, no MIDI-output function, no voice/choke control.

### 3b. Every callback a unit can implement (`unit.h` / `_unit_base.c`)

| Callback | Purpose | Relevant to routing/choke? |
|---|---|---|
| `unit_init / teardown / reset / resume / suspend` | lifecycle | no |
| `unit_render(in, out, frames)` | **audio in → audio out**, this unit's track only | no |
| `unit_set_param_value / get_*` | the unit's own 1–24 parameters | no |
| `unit_load_preset / get_preset_*` | the unit's own presets | no |
| `unit_set_tempo(tempo)` | 16.16 fixed-point BPM (informational) | no |
| `unit_note_on / note_off` | notes **already routed to this unit's track** | receive-only |
| `unit_gate_on / gate_off` | internal-sequencer gate for this track | receive-only |
| `unit_all_note_off` | panic | receive-only |
| `unit_pitch_bend` | 14-bit, center 0x2000 — unit defines its own range | receive-only |
| `unit_channel_pressure / aftertouch` | 7-bit pressure for this track | receive-only |

Two hard limits follow directly from this list:

- **A unit is downstream of all routing.** By the time `unit_note_on` fires, the
  firmware has *already decided* this note belongs to this track. A unit cannot
  change that decision, cannot see notes meant for other tracks, and cannot forward
  a note to another track.
- **A unit has no MIDI/voice actuator.** There is no function to emit MIDI, to
  trigger or silence another track's voice, or to configure channels/modes. So even
  perfect knowledge of the incoming MIDI stream would be inert.

What a unit *can* legitimately fix is confined to **how its own voice responds** to
the events it is handed: pitch-bend range, velocity/aftertouch curves, note
priority / voice-stealing, microtuning, tempo-synced behaviour. None of the four
pain points is in that set.

---

## 4. Can a *master effect* do the missing MIDI handling? No.

A master effect (`k_unit_target_drumlogue_masterfx`) is a valid module type, so the
question is reasonable — but it fails for two independent reasons, either of which
is sufficient:

1. **No actuator (same as every unit).** The masterfx gets the same
   `unit_runtime_desc_t` — sample readers only. It cannot emit MIDI, trigger a drum
   voice, choke a voice, re-route a note, or change a channel mode. The SDK exposes
   none of these to any unit type.
2. **It is post-mix audio only.** A master effect runs *at the end of the chain*:
   `unit_render(in, out, frames)` receives the **summed stereo output of all tracks
   at once** and returns processed stereo. By that point every voice has already
   fired and all tracks are mixed together, so the effect cannot isolate — let alone
   choke, re-trigger, or re-route — any individual drum. The best it could do in the
   audio domain is duck/gate the *entire* master, which is useless for a per-voice
   choke and irrelevant to routing.

Empirically, KORG's own master-effect template
(`platform/drumlogue/dummy-masterfx/unit.cc`) wires up **no note/gate/pitch/pressure
callbacks at all** — only `init/render/param/tempo/preset`. Whether the firmware
would even call `unit_note_on` on a masterfx is untested and, per the two reasons
above, moot: there is no path from "master effect saw a note" to "a drum voice was
choked / a note was re-routed / a channel was split."

The same reasoning rules out delay and reverb effects: they are send/insert **audio**
processors with the same descriptor and the same lack of any MIDI actuator.

---

## 5. Per-issue verdict

| # | Issue | Where it lives | Fixable in a user unit (any type)? | Practical mitigation |
|---|---|---|---|---|
| 1 | Choke groups ignore MIDI triggers | Firmware drum-engine exclusive-group logic | ❌ | External choke emulation via note-off — **conditional**, see §6 |
| 2 | Illogical note→track map (single-channel) | Firmware note-routing table | ❌ | External note remap — **fully solvable**, see §6 |
| 3 | Multi-channel only contiguous | Firmware MIDI config | ❌ | External channel remap makes internal layout irrelevant — see §6 |
| 4 | No "drums-on-one-channel + synth-on-another" mode | Firmware — mode does not exist | ❌ | External split reproduces it exactly — see §6 |

---

## 6. The realistic path: an external MIDI translation layer

Since the firmware is locked and units have no MIDI actuator, the only lever is a
small **MIDI translator placed between your controller/DAW and the drumlogue**. Put
the drumlogue in **multi-channel mode** (each drum track + the MULTI ENGINE track on
its own channel — contiguous is fine, because it is now hidden) and let the
translator present *your* preferred interface on the input side:

- **#2 + #3 — note & channel remap.** You play/sequence with any note map and channel
  layout you like; the translator rewrites events onto the drumlogue's actual
  per-track channels. The internal map and the contiguous-block constraint become
  invisible.
- **#4 — two-channel split.** The translator takes **one "drums" channel** (with a
  sane GM-style note map) and **one "synth" channel** (chromatic), fans the drum
  channel out to the individual per-track channels, and passes the synth channel
  straight through to the MULTI ENGINE track. The MULTI ENGINE track already plays a
  user synth chromatically when it receives notes on its own channel, so the melodic
  side needs no trick. This *is* the missing mode, implemented outside the box.
- **#1 — choke.** Emulatable **only if** the drumlogue's drum voices honour note-off
  (i.e. the "choked" voice sustains until released). The translator sends a note-off
  to the open-hat voice when the closed-hat note fires. If those voices are
  fire-and-forget one-shots that ignore note-off, no external trick can choke them
  and this one genuinely needs KORG. **Hardware test required** (see below).

Suitable hosts for the translator: a computer or Raspberry Pi (e.g. Python +
`python-rtmidi`, or a Pd/Max patch); a standalone MIDI processor (Blokas Midihub,
Bome BomeBox, MIDI Solutions, mioXL, …); or a DAW MIDI-effect / script (Ableton MIDI
tools, Logic Scripter, Bitwig).

**A working implementation of the Pi/Python host lives alongside this document**
(see [`README.md`](README.md) and [`midi_translator.py`](midi_translator.py)). It
provides the drum fan-out (#2/#3), the drums+synth split (#4), and choke emulation
(#1, subject to the note-off test below), plus `--list`/`--monitor`/`--probe` modes
to discover your controller's output and the drumlogue's per-track channels.

Related notes in this folder: [hardware & wiring](README.md#hardware--wiring),
the [Raspberry Pi Pico port evaluation](pi-pico-port.md), and the
[MIDI latency analysis](midi-latency.md).

### Choke hardware test (5 minutes, settles issue #1)

1. On a drum track that has an audible tail (e.g. open hi-hat, crash), send a MIDI
   **note-on**, let it ring, then send a **note-off** for the same note.
2. If the tail **stops/mutes on note-off** → choke can be emulated externally (send
   the note-off when the choking voice fires). Fully solvable outside the box.
3. If the tail **plays through and ignores note-off** → the voice is a one-shot;
   external choke emulation is impossible and only a firmware change would fix it.

---

## 7. What a native fix would require (KORG only)

For completeness, a proper fix for #1–#4 would need changes inside the encrypted
firmware's MIDI engine: configurable note maps, arbitrary (non-contiguous) channel
assignment, a drums+synth split mode, and MIDI-triggered notes participating in the
choke/exclusive-group logic. These are **feature/bug requests to send KORG**, not
things achievable through the SDK. Nothing in the logue SDK — no unit type, master
effect included — can substitute for them.

---

## Appendix A — reproducing the encryption finding

```python
# entropy of a firmware .bin in 64 KiB windows; ~7.99 across 100% of windows == encrypted
import sys, math, collections
d = open(sys.argv[1], 'rb').read()
def H(b):
    c = collections.Counter(b); n = len(b)
    return -sum((v/n) * math.log2(v/n) for v in c.values())
win = 65536
hi = tot = 0
for off in range(0, len(d), win):
    chunk = d[off:off+win]
    if len(chunk) < 4096: break
    tot += 1
    if H(chunk) > 7.9: hi += 1
print(f"size={len(d)}  high-entropy windows = {hi}/{tot} ({100*hi/tot:.1f}%)")

# ECB check: duplicate 16-byte blocks in the first 4 MiB (0 == not ECB)
seen = collections.Counter(d[i:i+16] for i in range(0, min(len(d), 4_000_000), 16))
print("duplicate 16B blocks:", sum(v-1 for v in seen.values() if v > 1))
```

## Appendix B — source of truth in this repo

- `platform/drumlogue/common/runtime.h` — `unit_runtime_desc_t`, module targets,
  param/header structs.
- `platform/drumlogue/common/unit.h` — the full callback list a unit may implement.
- `platform/drumlogue/common/_unit_base.c` — weak/fallback callback definitions
  (what a unit gets if it implements nothing).
- `platform/drumlogue/dummy-masterfx/unit.cc` — KORG's master-effect template
  (note: no MIDI callbacks wired).
- `platform/drumlogue/dummy-synth/unit.cc` — KORG's synth template (all MIDI
  callbacks wired, for comparison).

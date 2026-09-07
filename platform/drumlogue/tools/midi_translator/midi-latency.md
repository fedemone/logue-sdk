# MIDI latency: what the translator adds, and how to keep it low

*"Any possible problems with MIDI latency?"* — Short answer: the translator adds a
small, usually inaudible amount (**~1–4 ms typical**), and the design already keeps
it low. The things worth knowing are **where** the latency comes from, that
**jitter matters more than the average**, and a few knobs that trim both.

---

## 1. The latency budget, piece by piece

A note travels: `controller → [transport] → translator → [transport] → drumlogue`.
Each stage adds a little.

### 1a. The transport (same whether or not the translator is present)

| Transport | Per-message cost | Notes |
|---|---|---|
| **DIN MIDI** (31250 baud serial) | 1 byte = 10 bits = **320 µs**; a 3-byte note = **~1 ms** | Fixed, unavoidable, one message at a time on the wire |
| **USB-MIDI** (Full-Speed 12 Mbit/s) | bytes are fast, but bundled into **1 ms USB frames** | ~1 ms granularity from frame polling + host scheduling |

So each hop (in and out) costs on the order of **~1 ms** regardless of the box in
the middle. Two hops ≈ **~2 ms** of pure transport before the translator does
anything. This is inherent to MIDI, not something the translator introduces.

### 1b. What the translator itself adds

On the **Linux Pi + Python** implementation in this folder:

- **Poll interval.** The run loop calls `iter_pending()` then `time.sleep(0.001)`
  (see `do_run()` in [`midi_translator.py`](midi_translator.py)). Worst case a
  message waits ~1 ms for the next poll; **average ~0.5 ms**.
- **Interpreter + OS scheduling.** Per-message Python work is tens of µs
  (negligible), but a **non-realtime Linux scheduler** can occasionally stall the
  process for a few ms if the system is busy. On an idle, dedicated Pi this is
  small; it is the main source of *jitter* (variability), not average latency.
- **Extra messages from choke / fan-out.** A plain hit is one message out. A
  **choked** hit emits a **note-off *then* a note-on**. On DIN those serialize:
  ~2 ms on the wire instead of ~1 ms. With `choke_note_off_delay_ms > 0` you add
  exactly that delay on purpose. The drums+synth split does not multiply messages.

### 1c. The drumlogue's own MIDI-in → sound latency

A fixed property of the instrument, identical with or without the translator in
the path. Not added by us, but part of the total the player feels.

### Typical totals

| Setup | Typical added by translator | Total (both transports + translator) | Character |
|---|---|---|---|
| Linux Pi + Python, USB both sides | ~0.5–1.5 ms | **~2–4 ms** | low average, occasional jitter on a busy system |
| Linux Pi + Python, DIN both sides | ~0.5–1.5 ms | **~2–4 ms** | + serial smearing under dense traffic |
| **Pico (C) + DIN** | a few µs | **~1–2 ms** | lowest, **near-zero jitter** |
| Pico (CircuitPython) + DIN | tens–hundreds of µs | ~1–2 ms | low, deterministic |

For reference, added latency below **~5–10 ms** is generally imperceptible for
triggering percussion. So ~2–4 ms is comfortably in the "fine" range.

---

## 2. Jitter matters more than the average

A steady 4 ms delay is easy to play through — the brain adapts to a constant
offset. **Variability** (jitter) is what feels "loose" or "sloppy," especially on
fast hi-hats and rolls. This is the key qualitative point:

- The **Linux/Python** path has a low *average* but a longer *worst case* (OS
  scheduler, GC, other processes) → more jitter.
- The **microcontroller** path has no OS between the UART and the code →
  latency is nearly constant → **less jitter**, even if the raw number is similar.

If tight feel matters and your I/O is DIN, that alone is a reason to prefer the
[Pico port](pi-pico-port.md).

---

## 3. The real bottleneck to watch: DIN serial bandwidth

DIN MIDI carries **one message at a time** at ~1 ms each. Two things compete for
that single wire:

- **MIDI clock** (24 pulses per quarter-note) is a steady background stream when
  `pass_clock` is on.
- **Dense hits** — a fan-out where one input event becomes note-off + note-on, or
  several drums struck together — queue behind each other and behind clock bytes.

Under load, messages **smear** (arrive a few ms late in sequence). USB-MIDI has far
more bandwidth and barely smears. So:

- If you sync clock over the **same DIN** you send drums on, expect a little smear
  on busy bars. Consider sending **clock over USB** (or a separate DIN) if you hit
  this.
- USB-MIDI end-to-end sidesteps the serial bottleneck almost entirely.

---

## 4. Knobs that reduce latency and jitter (Pi/Python)

Ordered by impact:

1. **Use a callback instead of the poll loop.** `python-rtmidi` can deliver
   messages via a callback the instant they arrive, removing the ~0.5 ms average
   poll wait and cutting jitter. The current loop polls at 1 ms for simplicity and
   portability; switching to a callback is the single biggest software win. *(Not
   yet wired up — see the offer at the bottom.)*
2. **Give the process realtime priority.** Run under `SCHED_FIFO` so the scheduler
   can't preempt it. In the systemd unit:
   ```ini
   [Service]
   CPUSchedulingPolicy=fifo
   CPUSchedulingPriority=80
   ```
   or manually: `sudo chrt -f 80 python3 midi_translator.py ...`
3. **Pin the CPU governor to `performance`** so the Pi doesn't downclock while
   idle-waiting: `sudo cpufreq-set -g performance` (or set it in config).
4. **Dedicate the Pi.** No desktop, no browser, no background jobs — jitter is
   almost entirely "something else ran." A stock Raspberry Pi OS Lite, idle, is
   already good.
5. **Keep `choke_note_off_delay_ms` at 0** unless a specific voice needs release
   time; every ms there is added latency on the choked hit by design.
6. **Prefer direct USB connections over a hub** where practical (marginally less
   scheduling overhead), and don't route clock through a congested DIN (§3).
7. **A `PREEMPT_RT` kernel** removes almost all remaining scheduler jitter, if you
   want to go the last mile.

None of these are required for the tool to feel fine on an idle Pi — they are the
path to "as tight as the hardware allows."

---

## 5. What the translator already does right

- **~1 ms poll** is well below perception and keeps CPU near idle.
- **No buffering/queuing** of its own — it forwards each message immediately;
  there is no internal delay line adding latency.
- **Choke delay defaults to 0**, so choke costs only the unavoidable extra
  message on the wire.
- **Clock/Start/Stop/SPP are passed straight through**, so external sync timing is
  preserved rather than re-generated.

---

## 6. Bottom line

- Expect **~2–4 ms** added on the Linux/Python path, **~1–2 ms** on a Pico — both
  below the ~5–10 ms perceptibility floor for percussion.
- **Jitter, not average latency, is the thing to manage**; a dedicated/idle host
  (or a microcontroller) is how you manage it.
- **DIN serial bandwidth** is the one real bottleneck under dense traffic; USB-MIDI
  or a separate clock path avoids it.

> Want the callback-based input (item 4.1) wired into `midi_translator.py`? It's a
> small, self-contained change that lowers average latency and jitter. Say the word
> and I'll add it with a poll-loop fallback for environments where the callback
> backend isn't available.

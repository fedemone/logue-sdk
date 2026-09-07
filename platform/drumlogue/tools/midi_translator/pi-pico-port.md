# Porting the translator to a Raspberry Pi Pico (C / CircuitPython)

*Is a Linux Raspberry Pi overkill? Could a $4 Pico (RP2040 / RP2350) do the same
job in C or CircuitPython?*

**Short answer:** it depends entirely on **which MIDI ports you use**, because the
hard part is not the translation logic — that is trivial on any microcontroller —
it is **being a USB host for two USB-MIDI devices at once**.

- If your controller and the drumlogue can both talk **5-pin DIN MIDI** (UART):
  the Pico is not just feasible, it is the *better* tool — cheaper, instant-on,
  deterministic latency, nothing to maintain. **Recommended.**
- If you need **USB-to-USB** (two class-compliant USB-MIDI devices): the Pico
  *can* do it, but only through the advanced TinyUSB host path (C) with a hub or a
  second PIO-USB port. This is the one place where a Linux Pi's mature multi-device
  USB stack genuinely earns the "overkill." **Feasible but fiddly.**

The translation code (note map, channel rewrite, choke state, drums/synth split)
is a lookup table plus a few bytes of state. An RP2040 has 264 KB of SRAM and two
133 MHz cores; the RP2350 ("Pico 2") has 520 KB. Memory and CPU are **not** the
constraint. USB topology is.

---

## 1. The core problem: USB roles

Recall the two USB roles (this is the same issue that made the Hecsall
gadget-mode project the wrong template — see [`README.md`](README.md#hardware--wiring)):

- A class-compliant **controller** is a USB **device**.
- The drumlogue's **USB-B "TO HOST"** port is a USB **device**.
- Therefore the translator, sitting between them, must be a USB **host** — and a
  host for **two** devices simultaneously.

A Linux Pi is a general-purpose USB host with a hub on-board, so "two USB-MIDI
devices" is a non-event: they both just appear as ALSA ports. The Pico has **one**
USB controller and no OS, so hosting two devices is the whole engineering problem.

| Path | Controller side | drumlogue side | Pico difficulty |
|---|---|---|---|
| **A — all DIN** | DIN OUT → Pico UART RX | Pico UART TX → DIN IN | **Easy** ✅ |
| **B — USB in, DIN out** | USB host (1 device) | Pico UART TX → DIN IN | **Moderate** |
| **C — all USB** | USB host device #1 | USB host device #2 | **Hard** ⚠️ |

The drumlogue's DIN **MIDI IN** receives all 16 channels, so the multi-channel
fan-out works over DIN exactly as it does over USB. That is what makes Path A and
Path B viable.

---

## 2. Path A — DIN in, DIN out (the microcontroller sweet spot)

MIDI on DIN is just 31250-baud serial. The Pico has multiple hardware UARTs, so:

```
  controller  ──DIN MIDI OUT──▶  [ optocoupler ]  ──▶  Pico UART0 RX (GP1)
  Pico UART1 TX (GP4)  ──▶  [ MIDI out buffer ]  ──DIN──▶  drumlogue MIDI IN
```

- **Input circuit:** a standard MIDI-IN optocoupler (e.g. 6N138 / H11L1) protects
  the GPIO and gives the correct current-loop front end. Do **not** wire a raw DIN
  socket straight to a GPIO.
- **Output circuit:** two resistors (the classic 220 Ω pair) from a UART TX pin
  and 3.3 V to the DIN socket. That's it.
- A ready-made Pico "MIDI FeatherWing"-style board or any generic RP2040 MIDI
  breakout gives you both circuits without soldering the optocoupler yourself.

This path is what the overwhelming majority of RP2040 MIDI projects use. It is
rock-solid, deterministic, and both **C and CircuitPython handle it trivially**.

**Caveat:** it requires your controller to have a physical DIN **MIDI OUT**. Many
modern USB-only controllers do not. If yours is USB-only, use Path B.

---

## 3. Path B — USB host for the controller, DIN out to the drumlogue

The pragmatic middle ground when the controller is USB-only but you're happy to
feed the drumlogue over DIN:

```
  USB controller  ──USB──▶  Pico (USB host, ONE device)
  Pico UART TX  ──▶ MIDI out circuit ──DIN──▶  drumlogue MIDI IN
```

Hosting **one** USB device is well within TinyUSB's comfort zone (C), and the
drumlogue side is a plain UART. No hub, no second USB port. This is a clean Pico
design and covers the common "USB controller + hardware synth" case. In
CircuitPython, single-device USB host MIDI is possible on RP2040 but newer/rougher
than the UART path; in C via TinyUSB it is well trodden.

---

## 4. Path C — USB host for two USB devices (the hard one)

If *both* ends must be USB (e.g. a USB-only controller **and** you want to drive
the drumlogue's USB-B TO HOST rather than its DIN IN), the Pico needs to host two
USB-MIDI devices. On a single-USB-controller chip that means one of:

1. **TinyUSB host + a USB hub** (C). TinyUSB supports hubs, but two USB-MIDI
   devices behind a hub on the RP2040 is at the edge of what is routinely proven —
   expect to debug enumeration and endpoint handling.
2. **Native USB host + a second PIO-USB host port** (C, `Pico-PIO-USB` +
   TinyUSB). The PIO block bit-bangs a *second* full-speed USB port on two GPIO
   pins, so each MIDI device gets its own port and you avoid a hub. This works and
   is the most elegant Pico answer, but it is firmly "advanced project" territory
   and effectively **C-only**.

CircuitPython's USB **host** support (let alone a hub or two devices) is
experimental and not a dependable base for this. So Path C is realistically a
**C / Pico-SDK / TinyUSB** effort. This is exactly the capability you get "for
free" on a Linux Pi, which is why Linux stops being overkill the moment you commit
to two-USB-device hosting.

---

## 5. C vs CircuitPython (for the paths where both are options)

For Paths A and B the *logic* ports almost verbatim; the choice is about the USB
stack and how much polish you want.

| | CircuitPython | C (Pico SDK + TinyUSB) |
|---|---|---|
| Port the translation logic | trivial (`adafruit_midi`, dicts) | easy (arrays/structs) |
| UART / DIN MIDI | excellent | excellent |
| USB **device** MIDI (Pico *as* a device) | excellent | excellent |
| USB **host**, one device | possible, newer/rougher | solid (TinyUSB host) |
| USB **host**, two devices / hub / PIO-USB | not a dependable path | the only real option |
| Iteration speed | edit `code.py`, saves instantly | compile/flash cycle |
| Determinism / worst-case latency | good | best |

**Guideline:** prototype Path A/B in **CircuitPython** because you can port the
existing Python almost line-for-line; move to **C** only if you need Path C's
dual-USB hosting or want the tightest latency/jitter.

### The logic ports almost unchanged

The whole translator is a note→(channel,note,group) table, a `group_state` map,
and an `active` set — see [`midi_translator.py`](midi_translator.py). A
CircuitPython sketch of the UART core (Path A) looks like:

```python
# CircuitPython sketch (illustrative) -- Path A, DIN in / DIN out
import board, busio
import adafruit_midi
from adafruit_midi.note_on import NoteOn
from adafruit_midi.note_off import NoteOff

uart_in  = busio.UART(board.GP4, board.GP5, baudrate=31250)  # from controller
uart_out = busio.UART(board.GP0, board.GP1, baudrate=31250)  # to drumlogue
midi_in  = adafruit_midi.MIDI(midi_in=uart_in,  in_channel=tuple(range(16)))
midi_out = adafruit_midi.MIDI(midi_out=uart_out)

DRUM_MAP = {          # in_note -> (out_channel_0based, out_note, choke_group)
    36: (1, 36, None),   # Bass Drum
    42: (3, 42, 1),      # Closed Hat  (group 1)
    46: (4, 46, 1),      # Open Hat    (group 1 -> choked by CH)
    # ...
}
group_state = {}

while True:
    msg = midi_in.receive()
    if isinstance(msg, NoteOn) and msg.velocity > 0:
        entry = DRUM_MAP.get(msg.note)
        if entry:
            out_ch, out_note, grp = entry
            if grp is not None:                       # choke
                prev = group_state.get(grp)
                if prev:
                    midi_out.send(NoteOff(prev[1], 0), channel=prev[0])
                group_state[grp] = (out_ch, out_note)
            midi_out.send(NoteOn(out_note, msg.velocity), channel=out_ch)
    # ... NoteOff, synth-channel passthrough, clock passthrough, panic ...
```

That is the same design as the Python tool, minus the port-discovery/monitor
conveniences (which need a host OS and are development aids, not runtime needs).

---

## 6. When Linux stops being overkill (features that pull you back)

Choose a Linux Pi (Zero 2 W and up) over a Pico if you want any of:

- **Two USB-MIDI devices** with no hub/PIO fuss (Path C made trivial).
- **Config / monitoring over the network**, a web UI, or WiFi remote editing.
- **The `--list` / `--monitor` / `--probe` discovery tools** at runtime (they rely
  on enumerating named ports and a terminal).
- **Logging, hot-reloading config files, RTP/network MIDI, MIDI 2.0**, or running
  other software beside the translator.
- **Hosting many devices** (several controllers, a pedalboard, etc.).

Choose a **Pico** if you want a fixed, sealed, instant-on appliance with the
lowest and most predictable latency, and your I/O is DIN or single-USB-in.

---

## 7. Recommendation

| Your gear | Best target | Language |
|---|---|---|
| Controller **and** drumlogue on DIN MIDI | **Pico** | CircuitPython (or C) |
| USB controller, drumlogue on DIN IN | **Pico** (Path B) | C (or CircuitPython) |
| Must be USB↔USB (drumlogue TO HOST) | **Pico only if committed to C+TinyUSB**; otherwise **Linux Pi Zero 2 W** | C / (Python on Linux) |
| Want network config, discovery tools, many devices | **Linux Pi** | Python (this tool as-is) |

The Python tool in this folder already runs today on any Linux Pi. A Pico port is
a worthwhile, cheaper productionization **once your setup is settled** and you know
which ports you're committing to — do the discovery on the Linux Pi with
`--monitor`/`--probe`, then bake the resulting map into a Pico if you want the
appliance.

See also: [MIDI latency analysis](midi-latency.md) — the Pico path wins on jitter,
which matters more than raw latency for feel.

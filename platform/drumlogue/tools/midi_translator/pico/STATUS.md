# Pico port — status & decisions (paused)

Short version: the **translation logic is done and tested**; the **Pico I/O
transport is not decided yet**, because a full USB controller→Pico→drumlogue link
needs two USB ports, which pushes against the "cheap and fast" goal. Paused here
on purpose until we pick the simplest path.

## What's done

- **Translation core** — `translator.c`, `midi_parse.c`, `config.c/.h`. A faithful
  C port of the Python tool (drum fan-out, drums+synth split, choke groups, panic;
  a MIDI parser with running status, real-time interleave, SysEx passthrough).
- **Host unit tests** — `test/run.sh` compiles the core on a PC and checks all of
  the above. Passing. This logic is **transport-agnostic**: it doesn't care whether
  bytes arrive over USB or DIN.

## What's NOT decided (the reason we paused)

The core needs a MIDI **in** and a MIDI **out**. On a Pico that is the awkward part:

- The Pico has **one native USB port** and **no 5-pin DIN jack**.
- A controller and the drumlogue's USB-B "TO HOST" are **both USB devices**, and
  one Pico USB port can only play one role. So connecting controller → Pico →
  drumlogue over USB needs a **second USB port**. That extra hardware is exactly
  the complication that makes this stop feeling "cheap and fast".

### USB topology options (none chosen yet)

| Option | Wiring | Cost / effort | Robustness |
|---|---|---|---|
| **(a) device + PIO-USB host** | Pico native USB = device → drumlogue **USB-A** host port (output); a 2nd port via **PIO-USB** hosts the controller (input). Mirrors the proven [rppicomidi *pico-usb-midi-processor*](https://github.com/rppicomidi/pico-usb-midi-processor). | Pico-PIO-USB lib + a USB-A breakout on 2 GPIOs | Highest |
| **(b) host + USB hub** | Pico = USB host; a (powered) hub connects the controller **and** the drumlogue USB-B. | OTG adapter + powered hub | Two USB-MIDI devices through a hub on RP2040 can be flaky |
| **(c) device only (bench)** | Pico = USB-MIDI device into a **PC** to validate the logic live. No controller→drumlogue link yet. | Nothing extra | N/A — validation only |

## Decision so far

- **Start with USB MIDI** (native to the Pico; no DIN circuit needed to begin).
- **5-pin DIN is desirable but is roadmap**, not the starting point — the Pico has
  no DIN jack, so it needs an add-on (see below).

## Roadmap — adding 5-pin DIN later

DIN is genuinely the nicest transport (deterministic, lowest jitter — see
[`../midi-latency.md`](../midi-latency.md)); it just isn't built into the Pico.
When we add it:

- **Add-on hardware:** a UART **MIDI-IN optocoupler** (6N138 / H11L1) and a
  **220 Ω MIDI-OUT** pair on DIN sockets — or an off-the-shelf "Pico MIDI" board /
  MIDI FeatherWing that already has both.
- **Software:** it drops into the **UART code path that already exists** in
  `main.c` (`MIDI_UART`, `MIDI_UART_TX_PIN` / `RX_PIN`, GP0/GP1, 31250 baud). The
  parser and translator need no changes — DIN bytes feed the same `midi_parser`.
- **Coexistence:** DIN and USB can run together — merge both inputs into the parser,
  and fan output to both sinks — once a transport is settled.

## When we resume

1. Pick a USB topology: **(a)** for robustness, **(b)** for cheapest parts, or
   **(c)** just to see the logic run.
2. Wire the chosen USB glue in `main.c` around the (already-tested) core and bring
   it up on hardware.
3. Later: add the DIN add-on per the roadmap above.

*Nothing about the tested core changes with any of these — only `main.c`.*

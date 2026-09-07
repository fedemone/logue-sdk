# drumlogue MIDI translator — Raspberry Pi Pico firmware

A microcontroller build of the [MIDI translator](../README.md), for a Raspberry
Pi **Pico / Pico 2** (RP2040 / RP2350). Same behaviour as the Python tool — drum
fan-out, drums+synth split, choke emulation — but as a tiny, sealed, instant-on
box with deterministic latency and no OS to maintain. Why a microcontroller and
when a Linux Pi is worth it: [`../pi-pico-port.md`](../pi-pico-port.md). Latency:
[`../midi-latency.md`](../midi-latency.md).

> **No Linux prototype needed.** The translation logic here is the same logic as
> the Python tool, and it's covered by host unit tests (below). Set your note map
> and channels in `config.c` / `config.h` and flash — you can skip the Raspberry
> Pi OS step entirely. (If you don't yet know which drumlogue channel drives which
> track, discover it once by ear with the [choke/probe test](../README.md#does-choke-work-on-your-unit),
> or on any computer with the Python tool's `--probe`, then bake the numbers in.)

## What's here

| File | Role |
|---|---|
| `translator.c` / `.h` | The translation logic — a faithful C port of the Python `Translator`. **Portable, unit-tested.** |
| `midi_parse.c` / `.h` | Byte-stream MIDI parser (running status, real-time interleave, SysEx passthrough). **Portable, unit-tested.** |
| `config.h` | Options (channels, choke, passthrough) + hardware pins. Edit here. |
| `config.c` | The drum fan-out map. Edit here. |
| `main.c` | Pico hardware glue (UART DIN I/O, optional TinyUSB host). |
| `tusb_config.h` | TinyUSB host config (only used when USB host is enabled). |
| `CMakeLists.txt`, `pico_sdk_import.cmake` | Pico SDK build. |
| `test/` | Host unit tests for the portable core (`translator` + parser). |

The logic (`translator.c`, `midi_parse.c`, `config.*`) has **no Pico or TinyUSB
dependency**, so it compiles and runs on a normal PC — that's what the tests use.
Only `main.c` touches hardware.

## I/O paths (which one is yours?)

The deciding factor for a microcontroller build is **USB roles** — the same issue
covered in the [wiring guide](../README.md#hardware--wiring): a controller and the
drumlogue's USB-B "TO HOST" are both USB *devices*, so anything between them must
be a USB *host*. On a single-USB chip that's the hard part; DIN sidesteps it.

| Path | Controller → Pico | Pico → drumlogue | Build |
|---|---|---|---|
| **A — all DIN** *(default)* | DIN OUT → UART RX (optocoupler) | UART TX → DIN IN (220Ω pair) | Pico SDK only ✅ |
| **B — USB in, DIN out** | USB host (TinyUSB, 1 device) | UART TX → DIN IN | + TinyUSB host |
| **C — all USB** | USB host device #1 | USB host device #2 (PIO-USB) | + TinyUSB + Pico-PIO-USB ⚠️ |

The drumlogue's **DIN MIDI IN receives all 16 channels**, so the multi-channel
fan-out works over DIN exactly as over USB — that's what makes Paths A and B the
easy, robust choices.

## Wiring — Path A (DIN in / DIN out), the default

```
  controller DIN OUT ──▶ [ 6N138 / H11L1 optocoupler ] ──▶ Pico GP1 (UART0 RX)
  Pico GP0 (UART0 TX) ──▶ [ 220Ω pair to a DIN socket ] ──DIN──▶ drumlogue MIDI IN
```

- **MIDI IN** (from the controller) needs an **optocoupler** front end — do *not*
  wire a DIN socket straight to a GPIO. Any "Pico MIDI" breakout/FeatherWing gives
  you this without hand-soldering the optocoupler.
- **MIDI OUT** (to the drumlogue) is just the classic **220Ω** pair from TX and
  3.3 V to the DIN socket.
- Pins are set in `config.h` (`MIDI_UART_TX_PIN` / `MIDI_UART_RX_PIN`, default
  GP0/GP1 on `uart0`).

Set the drumlogue to **multi-channel mode** so each track (and the MULTI ENGINE)
listens on its own channel — see the [main README](../README.md#set-up-the-drumlogue).

## Configure

1. `config.h` — channels, choke behaviour, passthrough, and the UART pins.
   These mirror the Python tool's `CONFIG` one-for-one.
2. `config.c` — the `DRUM_MAP`: `{ in_note, out_channel, out_note, choke_group }`.
   `choke_group = -1` means no choke; equal values are mutually exclusive. The
   shipped values are a **placeholder** — replace with your layout.

## Build & flash

```bash
export PICO_SDK_PATH=/path/to/pico-sdk        # https://github.com/raspberrypi/pico-sdk
cmake -B build -S . -DPICO_BOARD=pico         # or: pico2, pico_w
cmake --build build
```

Hold **BOOTSEL**, plug the Pico into your computer, and copy
`build/drumlogue_midi_translator.uf2` onto the `RPI-RP2` drive. It reboots and
starts translating immediately (no OS, ~instant).

## Enabling USB host (Path B)

To host a USB-MIDI controller instead of a DIN one:

1. In `config.h`: set `USE_USB_HOST_IN` to `1` (leave `USE_UART_OUT` = 1 so the
   drumlogue is still driven over DIN).
2. In `CMakeLists.txt`: uncomment the
   `target_link_libraries(... tinyusb_host tinyusb_board)` line.
3. Rebuild. Plug the controller into the Pico's USB port (you'll need a USB-OTG /
   host cable or a host-capable carrier).

The TinyUSB **host MIDI API has changed across versions**; if the callback
prototypes in `main.c` don't match your tree, align them with
`tinyusb/src/class/midi/midi_host.h`. Because USB host is compiled out by default,
this never affects the default DIN build.

### Path C (all-USB, advanced)

Driving the drumlogue's USB-B TO HOST *and* a USB controller means hosting two USB
devices. The RP2040/RP2350 has one USB controller, so add a second port with
[Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) (a PIO-driven USB
port on two GPIOs) and route one device to each. This is C-only and firmly an
advanced project; for two-USB-device hosting a Linux Pi Zero 2 W is often the
pragmatic choice (see [`../pi-pico-port.md`](../pi-pico-port.md)). The output sink
in `main.c` has a marked spot where USB-host output would be added.

## Run the unit tests (no hardware)

The important part — the translation logic — is verified on a normal PC:

```bash
bash test/run.sh
```

This compiles `translator.c` + `midi_parse.c` + `config.c` with a test harness and
checks drum fan-out, the synth split, choke (including that panic sends no
duplicate note-offs), program-change/clock passthrough, and MIDI parsing with
running status and real-time interleave.

## Behaviour notes / differences from the Python tool

- **Feature parity** for the runtime path: fan-out, split, choke (with
  `retrigger_same` and `choke_note_off_delay_ms`), clock/start/stop/continue/SPP
  passthrough, program-change passthrough, SysEx passthrough, and panic
  (all-notes-off + CC123 on exit/unplug).
- The Python-only **development conveniences** (`--list`, `--monitor`, `--probe`,
  virtual ports) are not in the firmware — they need a host OS and are discovery
  aids, not runtime features. Do discovery once on a computer, then flash.
- Latency is lower and far more consistent than the Linux/Python path because
  there's no OS scheduler between the UART and the code — see
  [`../midi-latency.md`](../midi-latency.md).

# drumlogue MIDI translator

A small standalone MIDI processor that works around the drumlogue MIDI
limitations which **cannot** be fixed from the logue SDK — see
[`midi-routing-and-sdk-limits.md`](midi-routing-and-sdk-limits.md)
for why (encrypted firmware; units are downstream, per-track, receive-only with
no MIDI actuator).

### Documents in this folder

| File | What it covers |
|---|---|
| [`README.md`](README.md) (this file) | Setup, workflow, config, **[hardware & wiring](#hardware--wiring)**, choke test |
| [`midi_translator.py`](midi_translator.py) | The tool itself (all behaviour driven by the `CONFIG` block) |
| [`midi-routing-and-sdk-limits.md`](midi-routing-and-sdk-limits.md) | *Why* the four MIDI issues can't be fixed in an SDK unit (firmware-encryption evidence, unit surface) |
| [`pi-pico-port.md`](pi-pico-port.md) | Evaluation of a Raspberry Pi **Pico** rewrite in C / CircuitPython (is a Linux Pi overkill?) |
| [`midi-latency.md`](midi-latency.md) | MIDI latency analysis and how to keep it low |
| [`requirements.txt`](requirements.txt) | Python dependencies (`mido`, `python-rtmidi`) |

It sits between your controller/DAW and the drumlogue:

```
  controller / DAW  ──MIDI──▶  [ Pi running midi_translator.py ]  ──MIDI──▶  drumlogue
```

and gives you:

- **A sane drum note layout** — you play your own note map on one "drums"
  channel; the translator fans each note out to the correct drumlogue track
  channel. (Fixes the illogical single-channel note map.)
- **A drums + synth split** — a separate "synth" channel is passed straight
  through to the MULTI ENGINE track, so you can play it chromatically at the
  same time as the drums. (The missing "two-channel" mode.)
- **Choke / exclusive groups over MIDI** — a new hit in a choke group sends a
  note-off to the member currently sounding. *Works only if the drumlogue's
  drum voices honour note-off* — see [the choke test](#does-choke-work-on-your-unit).

Everything is driven by the `CONFIG` block at the top of
[`midi_translator.py`](midi_translator.py).

## Requirements

```bash
pip install -r requirements.txt      # mido + python-rtmidi
```

On a Raspberry Pi / Debian you may first need the ALSA/JACK dev headers that
`python-rtmidi` builds against:

```bash
sudo apt install libasound2-dev libjack-jackd2-dev
```

The drumlogue connects to the Pi over USB (it enumerates as a USB-MIDI device)
or over 5-pin DIN via a USB-MIDI interface. Either way it shows up as a port by
name.

## Hardware / wiring

The translator sits **between** your controller and the drumlogue, so the host
(Pi) must be the **USB host** for *both* — the opposite role from a
"Pi-as-a-MIDI-device" (USB gadget) project. The drumlogue's rear panel matters
here:

- **USB-B "TO HOST"** — a USB **device** port (it expects to plug into a host).
- **USB-A** — a USB **host** port (for connecting class-compliant controllers
  *directly to the drumlogue*).
- **5-pin DIN MIDI IN / OUT** and analog SYNC.

Because both the controller and the drumlogue's TO HOST port are USB *devices*,
the Pi hosts them both:

```
  [ controller ] ──USB──▶ [ Pi USB-A #1 ]
                          [ Pi (USB host) ]  runs midi_translator.py
  [ drumlogue  ] ◀─USB─── [ Pi USB-A #2 ]   (into drumlogue USB-B "TO HOST")
```

### Cables

| Link | Cable | Into |
|---|---|---|
| Pi → drumlogue | **USB-A → USB-B** ("printer" cable) | drumlogue **TO HOST** port |
| Pi → controller | **USB-A → (controller's connector: B / mini / micro / C)** | the controller |
| DIN alternative | a **USB→5-pin-DIN MIDI interface** on a Pi USB port | use if a device is DIN-only |

"TO HOST" is the correct drumlogue port here — it just means "expects a host,"
and the Pi *is* the host. The drumlogue then appears as a class-compliant
USB-MIDI port (no Linux drivers needed).

> Don't plug the controller into the drumlogue's own USB-A host port for this —
> that bypasses the Pi and you lose the translation. The Pi must be in the middle.

If a device is DIN-only, add a cheap USB-to-DIN interface rather than wiring a
GPIO optocoupler — simpler and more reliable. (A GPIO/UART MIDI circuit only
makes sense on a microcontroller build — see [`pi-pico-port.md`](pi-pico-port.md).)

### Cheapest board that runs this

The script is featherweight; the only real requirement is **enough USB host
ports for two devices**. It must run Linux — a ~$4 Pi **Pico** cannot run this
Python (it's a microcontroller; that would be a full rewrite — see
[`pi-pico-port.md`](pi-pico-port.md)).

| Board | ~Price | USB for 2 devices | Verdict |
|---|---|---|---|
| **Pi Zero 2 W** | ~$15 | 1× micro-USB OTG → needs OTG adapter **+ USB hub** | Cheapest board; a bit fiddly, ~20–30 s boot |
| **Pi 3 Model A+** | ~$25 | 1× USB-A → still needs a hub | Little gain over the Zero |
| **Pi 3 Model B/B+** | ~$35 | **4× USB-A** → plug both directly, no hub | **Cheapest plug-and-go** |
| Pi 4 / 5 | $45+ | plenty | Overkill for this |

- **Cheapest/smallest:** Pi **Zero 2 W** + a small USB hub (use a *powered* hub if
  your controller is bus-powered and hungry).
- **No-hassle:** Pi **3B/3B+** — four USB-A ports, plug both cables in directly.

Also needed: a **microSD card** and a **5 V power supply** (micro-USB for Zero/3,
USB-C for Pi 4). The **drumlogue powers itself** from its own adapter and draws
nothing from the Pi — only a bus-powered controller draws Pi USB power.

Sources: [Korg drumlogue specifications](https://www.korg.com/us/products/drums/drumlogue/specifications.php),
[Sound On Sound](https://www.soundonsound.com/news/korg-drumlogue-now-available).

## Set up the drumlogue

Put the drumlogue in **multi-channel mode** (Global MIDI settings). Each track —
including the MULTI ENGINE / synth track — then listens on its own channel in a
contiguous block. That contiguous layout is now *hidden*: you play whatever you
like on the translator's input side, and it maps onto these channels.

## Workflow

```bash
# 1. See the port names on your system.
python3 midi_translator.py --list

# 2. Learn what your controller/DAW actually sends (note numbers + channels).
python3 midi_translator.py --monitor --in "Your Controller"

# 3. Discover which drumlogue channel drives which track: this plays one note
#    on each channel in turn — listen for which track responds, and note the
#    channel numbers.
python3 midi_translator.py --probe --out drumlogue
#    (narrow it down: --probe-channels 1-12  --probe-note 60  --probe-hold 800)

# 4. Edit the CONFIG block in midi_translator.py with your channels + note map,
#    then run:
python3 midi_translator.py --in "Your Controller" --out drumlogue
```

If you leave `input_port` as `None`, the translator instead creates a **virtual
input port** named `drumlogue-xlate` that your DAW/controller connects *to* —
convenient on a Pi where the DAW is elsewhere on the USB/network MIDI chain.

## Configuring

All of it lives in `CONFIG` at the top of the script. The key parts:

| Key | Meaning |
|---|---|
| `drums_in_channel` | the channel you trigger drums on (GM drums = 10) |
| `synth_in_channel` | the channel you play the MULTI ENGINE synth on |
| `synth_out_channel` | the drumlogue channel of the MULTI ENGINE track |
| `drum_map` | list of `(label, in_note, out_channel, out_note, choke_group)` |
| `choke_enabled` | master switch for choke emulation |

In `drum_map`, tracks that share the same integer `choke_group` are mutually
exclusive (a new hit note-offs the one sounding); `None` means no choke. The
example map is a **placeholder** — replace the notes and channels with your own.

Clock, Start/Stop/Continue, Program Change and SysEx are passed through
untouched by default (so external sync still works); toggle with `pass_clock`,
`pass_program_change`, `pass_sysex`.

## Does choke work on your unit?

Choke emulation depends on the drum voices releasing on note-off. Test it in
five minutes:

1. On a track with an audible tail (open hat, crash), send a note-on, let it
   ring, then send a note-off for the same note.
2. Tail **stops** on note-off → choke works; set `choke_group`s and you're done.
3. Tail **plays through** → the voice is a one-shot that ignores note-off;
   external choke isn't possible and only a KORG firmware change would fix it.
   Leave `choke_enabled` off (the fan-out and split still work).

You can drive this test straight from the tool:

```bash
python3 midi_translator.py --probe --out drumlogue --probe-channels <ch> --probe-hold 1500
```

## Run headless on a Pi (systemd)

Adjust the paths/port names, then:

```ini
# /etc/systemd/system/drumlogue-xlate.service
[Unit]
Description=drumlogue MIDI translator
After=sound.target

[Service]
ExecStart=/usr/bin/python3 /home/pi/midi_translator/midi_translator.py --in "Your Controller" --out drumlogue
Restart=always
User=pi

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now drumlogue-xlate.service
```

## Notes

- Latency: the run loop polls at ~1 ms, which is inaudible for MIDI; CPU stays
  near idle. Full analysis (budget, jitter, tuning knobs) in
  [`midi-latency.md`](midi-latency.md).
- The script sends an all-notes-off (and CC123 on every channel) on exit, so
  Ctrl-C never leaves a stuck note.
- Pure translation logic is hardware-independent and unit-testable — feed
  `Translator.handle()` `mido.Message`s and inspect what it sends.

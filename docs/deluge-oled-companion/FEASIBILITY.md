# Feasibility study — an ESP32‑S3 external OLED companion screen for the Synthstrom Deluge

*(an open re-implementation of the "OVERFIT for Deluge" concept)*

**Date:** 2026‑08‑23
**Verdict:** **Yes — clearly feasible, and the hardest part is already solved for you by the Deluge firmware itself.**

---

## 0. Executive summary

| Question | Answer |
|---|---|
| Can the OVERFIT concept be recreated? | **Yes.** Everything it does rides on documented, open, already-shipping Deluge SysEx APIs. No Deluge hardware mod, no firmware patch, no reverse engineering required. |
| Does it work on a **7‑segment** Deluge? | **Yes, and this is the killer feature.** The community firmware has an `Emulated Display` setting that makes a 7SEG unit render the *full 128×48 graphical OLED UI* into RAM. That buffer is what gets streamed over SysEx. A 7SEG Deluge can therefore drive a real graphical external screen. |
| Screen 1 — Sunton **ESP32‑2432S032** (3.2" CYD) | **No — wrong chip.** A *classic* ESP32‑WROOM‑32: no USB‑OTG at all, so it cannot speak USB MIDI in either direction. Good panel, unreachable Deluge. §6.0 |
| Screen 2 — Guition **JC4827W543** (4.3", 480×272) | **Yes — the best option so far**, if the listing's "LX6 / 520K RAM" is the stale copy-paste it appears to be. The real board is an ESP32‑S3‑WROOM‑1‑N4R8 with native USB on its Type‑C and an exact 3.75× fit. Verify the module marking first. §6.3 |
| Effort for a working prototype | ~2–3 weekends for the core mirror (USB‑MIDI host + SysEx + OLED). Weeks more for Wi‑Fi UI / BLE MIDI / file manager parity. |
| Parts cost | ~€30–40 per unit in single quantities. |
| Legal risk | Low. Deluge firmware is GPLv3, DEx and Deluge‑Synth‑Editor are MIT. Don't copy OVERFIT branding/assets; the protocol is not theirs. |

---

## 1. What OVERFIT actually is

From <https://overfit.expremiental.com/> (their own claims, beta, waitlist-only, no price and no source published):

- ESP32‑S3 + **2.4" OLED**, 18650 battery, USB‑C charging.
- Connects to the Deluge **over USB**.
- **BLE MIDI** ("no dongles") and **Wi‑Fi** for a web UI.
- Web UI adds an **XY controller with presets**, wireless screen mirroring, fullscreen performance mode.
- Works with **any Deluge (7SEG or OLED)** on **Community Firmware v1.1+**.
- Roadmap: wireless file manager, custom enclosure.

Nothing on that list requires anything private. Every capability maps onto a public Deluge SysEx endpoint (§3, §4) that the two reference projects you named already exercise from a browser.

---

## 2. Why this is easy: the Deluge already broadcasts its screen

The Deluge firmware contains a **display mirroring service over SysEx**. Verified in `SynthstromAudible/DelugeFirmware` @ `4f4a176` (2026‑08‑23):

- `src/deluge/hid/hid_sysex.cpp` — the display mirror (full frame, RLE frame, delta frame, 7‑seg frame, display-type swap)
- `src/deluge/io/midi/midi_engine.cpp:764` — SysEx dispatcher
- `src/deluge/util/pack.c` — the two packing codecs
- `src/RZA1/cpu_specific.h:137,145` — `OLED_MAIN_WIDTH_PIXELS 128`, `OLED_MAIN_HEIGHT_PIXELS 48`

Crucially it is **push, not poll**. `OLED::sendMainImage()` calls `HIDSysex::sendDisplayIfChanged()` every time the UI marks the framebuffer dirty (the render loop re-arms every 5–15 ms). A client subscribes once, then frames arrive at the Deluge's own UI refresh rate. The 1 Hz polling you see in DEx is only there to keep the subscription alive.

**The framebuffer format is the single biggest gift here:** 128×48 monochrome, stored as 6 pages × 128 bytes, one byte = 8 vertical pixels, LSB at top. **That is byte-for-byte the SSD1306/SSD1309 GDDRAM layout.** Rendering it on a mono OLED is a `memcpy` of 768 bytes into the display buffer, then flush. No bit-twiddling, no rotation, no scaling.

---

## 3. Protocol reference (what your ESP32 has to speak)

### 3.1 Framing

Two accepted request prefixes (`midi_engine.cpp:790‑797`):

| Prefix | Bytes | Notes |
|---|---|---|
| Official Synthstrom ID | `F0 00 21 7B 01 …` | Current firmware; **all replies use this** |
| Legacy "developer" ID | `F0 7D …` | Still accepted on *input*; sets `developerSysexCodeReceived` |

> ⚠️ **Gotcha:** DEx's `DisplayViewer.tsx:114` only matches `data[1] === 0x7d` on incoming frames, while current firmware replies with `00 21 7B 01`. **Your parser must accept both prefixes on receive.** Assume nothing about which one your target firmware release emits — sniff for both.

Top-level command byte (`SysEx::SysexCommands`): `0`=Ping, `1`=Popup, `2`=HID, `3`=Debug, `4`=Json, `5`=JsonReply, `0x7F`=Pong.

### 3.2 Display commands (category `02` = HID)

Requests (shown with legacy prefix for brevity; prepend `F0 00 21 7B 01` instead of `F0 7D` for the official form):

| Request | Meaning |
|---|---|
| `F0 7D 02 00 00 F7` | Send one full OLED frame, 7‑bit packed (no RLE) |
| `F0 7D 02 00 01 F7` | Send one full OLED frame, RLE packed |
| `F0 7D 02 00 02 F7` | **Subscribe** + send delta now |
| `F0 7D 02 00 03 F7` | **Subscribe** + force full frame (and 7‑seg frame if 7SEG active) |
| `F0 7D 02 00 04 F7` | **Swap display type** (OLED ⇄ 7SEG rendering) |
| `F0 7D 02 01 00 F7` | Send one 7‑segment frame |
| `F0 7D 00 F7` | Ping → replies `F0 00 21 7B 01 7F 00 F7` |

Replies (official prefix, 8‑byte header):

| Reply | Layout |
|---|---|
| Full frame | `F0 00 21 7B 01 02 40 <00\|01> 00 <packed 768 B> F7` |
| **Delta frame** | `F0 00 21 7B 01 02 40 02 <start> <len> <RLE payload> F7` — apply at byte offset `start*8`, covering `len*8` bytes |
| 7‑segment | `F0 00 21 7B 01 02 41 00 00 <hi> <d0> <d1> <d2> <d3> F7` |

For the 7‑seg reply, `d0..d3` are **raw segment bitmaps** (not ASCII, not hex values) and `hi` is the packed high-bit byte — which, because bit 7 of each segment byte is the decimal point, doubles as a **dot bitmap**. DEx exploits exactly this (`DisplayViewer.tsx:135‑137`).

### 3.3 Two things that will bite you

1. **The subscription expires after 2 seconds.** `hid_sysex.cpp:59` sets `midiDisplayUntil = audioSampleTimer + 2 * kSampleRate`. Re-send `02 00 02` at ≥1 Hz or the stream dies. Also note `midiDisplayCable` is a *single* pointer — the last requester wins; only one mirror client at a time.
2. **Back-pressure.** If `cable.sendBufferSpace() < 512` the firmware defers 100 ms. On USB this rarely trips; on DIN MIDI it will trip constantly.

### 3.4 The packing codecs (`src/deluge/util/pack.c`)

**`pack_8bit_to_7bit`** — the standard Sequential/DSI scheme: every 7 source bytes → 8 output bytes, first byte carries the 7 MSBs (bit *j* ↔ source byte *j*), then the 7 low-7-bit values.

**`pack_8to7_rle`** — custom, used for all OLED frames. Decoder (cross-checked against DEx `display.ts:13‑88`, which is a correct port):

```
marker = next byte
if marker < 60:                       # dense literal packet
    size, off = (2,0) if marker<4 else (3,4) if marker<12 else (4,12) else (5,28)
    highbits = marker - off
    emit size bytes: (src[j] & 0x7F) | (0x80 if highbits & (1<<j) else 0)
else:                                 # run-length packet
    m = marker - 64
    high = m & 1
    runlen = m >> 1
    if runlen == 31: runlen = 31 + next_byte     # extended, max 31+127
    value = (next_byte & 0x7F) | (0x80 if high else 0)
    emit runlen copies of value
```

Worst case a full frame packs to 922 bytes; a typical Deluge screen is mostly empty and compresses hard, and deltas are usually tens of bytes.

---

## 4. The 7‑segment question — the feature that makes this worth building

This is the finding that matters most for your stated use case (7SEG Deluge + external screen).

`display.cpp:89 swapDisplayType()` destroys the current display driver and constructs the other one. On a **7SEG unit** that means constructing `deluge::hid::display::OLED`, so the firmware starts rendering the **complete 128×48 graphical UI** — menus, waveforms, envelope curves, automation, everything — into `oledCurrentImage`. That buffer is precisely what the SysEx mirror streams.

It is not a hack; it is an exposed community feature:

- `RuntimeFeatureSettingType::EmulatedDisplay`, states `Hardware = 0`, `Toggle = 1`, `OnBoot = 2` (`runtime_feature_settings.h:42`)
- `Toggle` enables the **Shift + Learn + Affect Entire** shortcut (`buttons.cpp:113‑119`)
- `OnBoot` swaps at startup (`deluge.cpp:811‑813`)
- `02 00 04` swaps it remotely over SysEx — **your companion box can flip the Deluge into graphical mode by itself on connect**

So: set `Emulated Display` to `OnBoot` (or let the companion send `02 00 04`), and a 7‑segment Deluge feeds your external screen the same rich UI an OLED Deluge has. That, not the screen size, is the actual product.

*(The mirror is symmetric: on an OLED unit, swapping the other way renders an emulated 7‑seg via `OLED::renderEmulated7Seg`.)*

**Verify on your target firmware release.** `EmulatedDisplay` is confirmed present on `main` as of 2026‑08‑23; I did not verify which released version introduced it.

---

## 5. Hardware architecture — the USB link is the real design decision

The Deluge's USB‑B port is **dual-role, decided once at boot** (`deluge.cpp:815‑826`):

```c
openUSBHost();
// If nothing was plugged in to us as host, we'll go peripheral
if (!anythingInitiallyAttachedAsUSBHost) { closeUSBHost(); openUSBPeripheral(); }
```

That gives you three options.

### Option A — ESP32‑S3 as USB **device**, Deluge as host  ★ simplest
The Deluge enumerates your box like any USB MIDI controller and powers it.

- ✅ Easiest firmware by far: TinyUSB device MIDI, first-class on ESP32‑S3 (`esp_tinyusb`, or Arduino).
- ✅ No battery strictly needed — the Deluge supplies 5 V (when the Deluge itself is on DC power).
- ⚠️ Must be attached **before/at Deluge boot**, or the Deluge has already fallen back to peripheral mode.
- ⚠️ Occupies the Deluge's only USB port; no simultaneous computer connection.
- ⚠️ Depends on the Deluge's host stack accepting your descriptor — there is a community "tested controllers" list for a reason. **Prototype this on day one.**

### Option B — ESP32‑S3 as USB **host**, Deluge as peripheral  ★ recommended, and almost certainly what OVERFIT does
Your box supplies VBUS; the Deluge finds nothing attached at boot, falls back to peripheral, and enumerates as its usual 3‑cable MIDI device.

- ✅ Uses the Deluge's **best-tested** USB path (the "connect to a computer" path).
- ✅ Plug order doesn't matter much; behaves like a laptop.
- ✅ Explains OVERFIT's 18650 — a host must source VBUS.
- ⚠️ Brief VBUS contention while the Deluge attempts host mode at boot (both sides drive 5 V). This is what happens with a PC too, but budget for a proper VBUS switch / e‑fuse rather than tying the rails together.
- ⚠️ Needs a USB‑MIDI **host** class driver on the ESP32: [`enudenki/esp32-usb-host-midi-library`](https://github.com/enudenki/esp32-usb-host-midi-library), [`sauloverissimo/ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI), or ESP‑IDF's USB Host stack directly.
- 🔴 **Verify SysEx support before committing.** Many USB‑MIDI helper libraries only handle 3‑byte channel messages and silently drop the SysEx code-index-numbers (`0x4/0x5/0x6/0x7`). You need full multi-packet SysEx reassembly and transmission. This is the #1 integration risk in the whole project.

### Option C — DIN MIDI  (fallback only)
`MIDICableDINPorts::sendSysex` exists, so the mirror works over 5‑pin DIN. At 31 250 baud you get ~3 125 B/s: a worst-case full frame is ~0.3 s. Deltas (tens of bytes) land in 10–50 ms, so menu text is usable but meters and waveforms will smear. Worse, the stream shares the Deluge's single DIN OUT with your notes and clock — **see §6.2 before choosing this.** A bench escape hatch, not a primary link.

### Bandwidth budget (Option A or B)
Worst-case full frame ≈ 922 packed bytes → ~308 USB‑MIDI 4‑byte packets → ~1.2 kB on the wire → ~20 × 64‑byte bulk transactions ≈ 20 ms at the conservative 1 transaction/ms. Typical deltas are 1–2 orders of magnitude smaller. **The USB link is not the bottleneck; the Deluge's own 5–15 ms render loop is.** Expect 30–60 fps and ~10–30 ms of glass-to-glass latency.

---

## 6. The display — the ESP32‑2432S032 ("Cheap Yellow Display", 3.2")

The listing is the **Sunton ESP32‑2432S032**, the 3.2" member of the "Cheap Yellow Display" (CYD) family.

#### What the board actually is

| | |
|---|---|
| MCU | **ESP32‑WROOM‑32** — the *classic* dual-core Xtensa LX6 @ 240 MHz |
| Memory | 4 MB flash, 520 KB SRAM, **no PSRAM** |
| Display | 3.2" IPS TFT, 240×320, RGB565, **ST7789 over SPI (HSPI)** — SCK 14, MOSI 13, MISO 12, CS 15, DC 2 |
| Touch | `…S032N` none · `…S032R` resistive XPT2046 (VSPI) · `…S032C` capacitive GT911 (I²C, SCL 32 / SDA 33 / INT 21) |
| Extras | microSD slot, PAM8002A speaker amp, RGB LED (GPIO 4/16/17), LDR, IP5306 Li‑ion charger on JST 1.25 |
| USB | micro‑USB **and** USB‑C — **both go to a CH340 UART bridge** |
| Free GPIO | 22, 27 (backlight PWM), 35 (input‑only), plus 0/1/3/21 on the connectors |

#### Verdict: 3.2" is a fine size. This board is the wrong chip.

🔴 **The ESP32‑WROOM‑32 has no USB‑OTG peripheral at all.** It cannot be a USB host, and it cannot even be a USB *device* — the two USB sockets are wired to a CH340 serial bridge, not to the ESP32. The soft‑USB‑host hack that exists for the classic ESP32 is limited to **low‑speed HID**; USB‑MIDI is a full‑speed bulk class and is out of reach.

That deletes Options A and B from §5 outright. Only three paths remain for this specific board, and none of them is "buy it and start":

| Path | Assessment |
|---|---|
| **DIN MIDI** (§5 Option C) | Genuinely workable — wire an H11L1/6N138 opto on GPIO 35 (input-only is fine for MIDI IN) and MIDI OUT on GPIO 22, keeping 27 for backlight. But see §6.2: it will damage your MIDI timing. |
| **Pair it with a €5 ESP32‑S3** doing USB host, linked by UART at 921 600 baud (~92 kB/s — a worst-case frame crosses in ~11 ms) or ESP‑NOW | Works, and you keep the touch/SD/speaker/charger. But if you're buying an S3 anyway, hanging a plain SPI TFT off it is simpler and cheaper. |
| **Wi‑Fi second screen** for milestone 6 | The board's *best* role: an S3 box does USB host and serves frames over Wi‑Fi; the CYD is a dedicated wireless display. Also a fine bench target for writing the scaler while the S3 ships. |

### 6.1 How big is the mirrored screen, really?

This is the counter-intuitive part, and it is worth doing the arithmetic before spending money. The physical width of the mirrored image is **scale × 128 × pixel pitch** — and high-resolution small panels have a *fine* pitch that cancels out the scale factor.

| Panel | Pitch | Scale | Image | Diagonal |
|---|---|---|---|---|
| **2.42" 128×64 mono OLED** | 0.430 mm | 1× | 55.0 × 20.6 mm | **2.31"** |
| **3.2" 240×320 (this board)** | 0.203 mm | 2× nearest | 52.0 × 19.5 mm | **2.19"** |
| 3.2" 240×320 | 0.203 mm | 2.5× anti-aliased | 65.0 × 24.4 mm | **2.73"** |
| 3.5" 480×320 | 0.154 mm | 3× nearest | 59.2 × 22.2 mm | 2.49" |
| **4.3" 480×272** | 0.198 mm | 3× nearest | 76.0 × 28.5 mm | **3.20"** |
| 4.3" 480×272 | 0.198 mm | 3.75× anti-aliased | 95.0 × 35.6 mm | **4.00"** |
| 4.3" 800×480 | 0.117 mm | 6× nearest | 89.9 × 33.7 mm | 3.78" |
| 7" 800×480 | 0.191 mm | 6× nearest | 146.4 × 54.9 mm | 6.15" |

Read off the two lines that matter: **a 3.2" 240×320 panel at honest 2× integer scale produces an image slightly *smaller* than a 2.42" mono OLED at 1:1.** A "3.2 inch screen" does not mean a 3.2 inch Deluge screen.

*(This corrects an estimate in an earlier draft of this document, which suggested a 3.5" 480×320 at 3× would be meaningfully bigger than the OLED. It isn't — 2.49" against 2.31", an 8% linear gain.)*

Two escapes from the integer-scale trap:

- **Anti-aliased fractional upscaling.** On a colour TFT you are not stuck with 1-bit nearest-neighbour. Upscale the 128×48 bitmap at 2.5× or 3.75× with grayscale interpolation and it reads as clean type rather than blocky pixels — which unlocks *full panel width* on any panel. This is the single highest-value rendering trick available on a TFT and it does not exist on a mono OLED.
- **Buy a large, low-DPI panel.** 4.3" 480×272 is the sweet spot: 3× nearest gives 3.20", 3.75× anti-aliased gives a genuine **4.00"** — nearly double the OLED's linear size and about 3× the area.

### 6.2 The DIN-MIDI caveat nobody mentions

If you go the DIN route on this board, understand what you are spending: **the Deluge has one DIN MIDI OUT, and the screen stream shares those 31 250 baud with your notes and your clock.** A typical delta is 50–90 bytes ≈ 16–29 ms of wire time; a full-frame resync is 64–128 ms. Injecting that into the same port that clocks your drum machine will produce audible timing jitter. It's fine on a bench, or if you don't use DIN MIDI for anything else. It is not fine in a live rig. On USB the mirror rides its own pipe and this problem does not exist.

### 6.3 Candidate 2: Guition JC4827W543, 4.3" — the right board, if the listing is telling the truth

**Verdict: this is the best option considered so far — but the spec block in that listing contradicts itself, and the contradiction lands exactly on the one spec that decides the project.**

#### The tell: *"A bordo 240MHz LX6 MCU … 520K Byte RAM"*

| | Classic ESP32 | ESP32‑S3 |
|---|---|---|
| Core | Xtensa **LX6** | Xtensa **LX7** |
| SRAM | **520 KB** | 512 KB (+ 384 KB ROM) |
| USB‑OTG | **none** | yes |

"LX6" and "520K RAM" are both the *classic* ESP32's numbers. Taken literally, that listing describes a board that cannot talk to the Deluge — the same dead end as the CYD in §6.0.

But everything *else* in the block — 4.3", 480×272, 8 MB PSRAM, 4 MB flash, capacitive-or-resistive touch, the JST1.25 4‑pin TTL header, and Guition's own `pan.jczn1688.com` download link — is an exact match for the **Guition JC4827W543**, which is definitively an **ESP32‑S3‑WROOM‑1‑N4R8**. Guition's factory documentation confirms it.

So it is almost certainly a stale copy-pasted spec blurb and the board is the S3 you want. "Almost certainly" is not how to spend money on the one spec the whole project rests on — Guition does also sell classic‑ESP32 panels.

**Any one of these settles it before you pay:**

| Check | ESP32‑S3 (good) | Classic ESP32 (useless here) |
|---|---|---|
| Model number | **JC4827W543** | JC2432W328 and similar |
| Module marking in the photos | **ESP32‑S3‑WROOM‑1** | ESP32‑WROOM‑32 / WROVER |
| Core | LX7 | LX6 |
| SRAM | 512 KB + 384 KB ROM | 520 KB |
| Bluetooth | BLE 5 only | Bluetooth Classic **and** BLE |

#### What Guition's factory docs actually specify

From `lsdlsd88/JC4827W543` (the factory documentation drop) and community ESPHome configs:

| | |
|---|---|
| MCU | ESP32‑S3‑WROOM‑1 **N4R8** — 4 MB flash, 8 MB octal PSRAM |
| Display | 4.3" IPS 480×272, **NV3041A over QSPI** — CS 45, CLK 47, data 21/48/40/39, backlight 1 |
| Touch | `N` none · `R` resistive · **`C` GT911 capacitive** (I²C: SDA 8, SCL 4, INT 3, RST 38) |
| USB | factory pin table lists **IO19 = USB+, IO20 = USB−** |
| Debug UART | JST1.25 4‑pin (+5 V, TX, RX, GND) on **IO17/IO18 = U1TXD/U1RXD** |
| Storage | TF card slot |

#### The USB finding, and what it costs you

Three independent signals say the Type‑C is wired to the **ESP32‑S3's native USB**, not to a CH340 bridge: the factory IO table names IO19/IO20 as USB+/USB−; a community PlatformIO config uploads to `/dev/ttyACM0`, which is a native USB CDC device (a CH340 enumerates as `ttyUSB`); and no CH340 appears anywhere in the factory documentation. **Ten-second confirmation on arrival:** plug it in — `ttyACM*` or "USB JTAG/serial debug unit" means native; "USB‑SERIAL CH340" means not.

That makes §5 Option B possible on this board, with two consequences to plan for:

1. **You lose the USB console.** USB‑Serial‑JTAG and USB‑OTG share the GPIO19/20 pads — once the host stack claims them, your serial console is gone. Route it to the JST1.25 TTL header instead. That header is the reason this board is comfortable to develop on rather than merely possible.
2. **The Type‑C is a sink and sources no VBUS**, so it will not power the Deluge's USB‑B port, which needs VBUS present to enumerate as a peripheral. Either use a **powered USB‑C OTG adapter** (USB‑C to the board, USB‑A to the Deluge, separate power in — the standard phone-OTG part), or tap D+/D− and feed VBUS from a 5 V boost off the Li‑ion cell.

#### The fit is unusually clean

**480 ÷ 128 = exactly 3.75.** Render the mirror full-width at 3.75× and it occupies **480×180**, leaving **480×92** — a quarter of the panel — for an XY pad, a status strip, or transport info. Per §6.1 that's a **3.20"** image at plain 3× nearest-neighbour and **4.00"** at 3.75× anti-aliased, against 2.31" for the 2.42" OLED. Nothing else discussed lands this well.

Budget roughly 4 ms to flush 480×180 at RGB565 over QSPI, plus 1–3 ms for a box-filter upscale — and the SysEx delta tells you which row band actually changed, so most frames cost far less. 30–60 fps is comfortable.

#### Gotchas to plan around

- **4 MB flash, and there is no 16 MB SKU.** Fine for this firmware; tight once you want LVGL *and* OTA partitions.
- **NV3041A** needs colour-invert and byte-swap, and only 0°/180° rotation works. Harmless here — 480×272 is already landscape.
- **Shared-bus quirk:** SD-card and touch access can break immediately after a display flush. Sequence your operations rather than interleaving them.

**Buy the `C` (capacitive) variant** if you want the XY pad — resistive is single-touch and needs pressure.

### 6.4 If you'd rather not risk the listing

An **ESP32‑S3 devkit with two USB ports** (~€8) plus a plain 4.3" or 3.5" SPI TFT (~€8–12). More wiring, fewer surprises, and the native USB port is unambiguously yours. The 3.5" 320×480 Guition **JC3248W535** is the smaller sibling if 4.3" is too large physically.

**And still buy one 2.42" SSD1309 SPI OLED (~€10)** for comparison. It maps 1:1 with a `memcpy`, it's the authentic look, and per §6.1 it isn't actually smaller than the mid-size TFTs. Choosing between "authentic mono" and "big colour with touch" is a thirty-minute experiment once both are on the bench, and it isn't a decision worth making from a spec sheet.

### 6.5 Checklist for any display you consider

1. **Does the MCU have USB‑OTG?** ESP32‑S3, S2 or P4 — **not** classic ESP32/WROOM‑32. This is the pass/fail gate.
2. **Is the native USB reachable?** Two USB sockets, or GPIO19/20 free.
3. **Panel pitch × scale** — run the §6.1 arithmetic before believing the inch count.
4. **SPI, not I²C** for mono OLEDs (many 2.42" modules ship strapped for I²C and need a 0 Ω resistor moved).
5. **Touch**, if you want the XY-pad feature — resistive is fine for one finger, capacitive for gestures.

Two practical notes carried over: a static menu on a **mono OLED** for hours is the burn-in worst case, so honour the firmware screensaver (`oled.cpp:373‑390`) and add a dimming timeout; and on any **SPI TFT**, only push the rows the delta actually touched — a `start`/`len` pair of 8-row pages at 2× is ~16 kB, about 3 ms at 40 MHz, so 30 fps+ is comfortable even without PSRAM.

## 7. Bill of materials (single unit)

| Item | ~Cost |
|---|---|
| ESP32‑S3 devkit, **N16R8 (PSRAM)**, dual USB (native + UART) — *classic ESP32 boards such as the CYD will not work, see §6.0* | €8–12 |
| 2.42" SSD1309 128×64 SPI OLED | €9–14 |
| *or* Guition **JC4827W543C** — 4.3" 480×272, ESP32‑S3‑N4R8, GT911 capacitive touch, TF slot (§6.3) | €15–25 |
| Powered USB‑C OTG adapter, to source VBUS to the Deluge (§6.3) | €3–6 |
| 18650 cell + holder + charge/boost (TP4056 + 5 V boost, or IP5306) | €6–10 |
| USB‑A receptacle breakout / OTG pigtail, VBUS switch | €2–4 |
| Rotary encoder + 2 buttons (optional local UI) | €3 |
| 3D-printed enclosure | — |
| **Total** | **€28–43** |

**Get the dual-USB devkit.** On the ESP32‑S3 the USB‑OTG peripheral and the USB‑Serial‑JTAG share GPIO19/20 — if the native port is doing USB host duty you cannot also flash/debug over it. A board with a separate UART bridge saves you real pain.

---

## 8. Suggested build order

| # | Milestone | Effort |
|---|---|---|
| 1 | **Spike the risk first.** ESP32‑S3 USB‑MIDI host enumerates the Deluge, sends `F0 7D 02 00 03 F7`, logs a reply of the expected length. If SysEx reassembly is broken in your chosen library, you learn it now, not in week 3. | 1 evening |
| 2 | Port `unpack_7to8_rle` (from `pack.c`, or transcribe DEx's `display.ts`), reconstruct the 768‑byte frame, `memcpy` → OLED, flush. Re-arm the subscription at 1 Hz. **This is the whole product.** | 1–2 evenings |
| 3 | Delta handling (`02 40 02`, offset `start*8`), full-frame resync on the first connect and on any decode error. | 1 evening |
| 4 | 7‑seg path (`02 41 00`) — render segment bitmaps + dots, for users who *don't* want emulated mode. | 1 evening |
| 5 | Auto-detect + auto-swap: send `02 00 04` on connect when the unit reports 7‑seg frames. Handle hot-plug and Deluge reboot. | 1 evening |
| 6 | Wi‑Fi AP + HTTP + WebSocket streaming the same frames to a browser (mirror + fullscreen). | 1 weekend |
| 7 | XY pad in the web UI → MIDI CC out over the USB link, with presets. | 1 weekend |
| 8 | BLE MIDI bridge. ESP32‑S3 is **BLE‑only** (no Bluetooth Classic) — BLE‑MIDI is the right and only target. | 1–2 weekends |
| 9 | File manager: implement the **smSysex JSON API** (command `4`/`5`). Port from DEx `src/lib/smsysex.ts` + `sysexPacking.ts`; Deluge‑Synth‑Editor's `js/sysex-core.js` + `js/file-browser.js` is a second, simpler reference. Requires **CFW ≥ 1.3**. | 2–4 weekends |

Milestones 1–4 give you something better than OVERFIT's core promise on a 7SEG Deluge. Everything after is polish.

---

## 9. Using the two reference projects

Both are **MIT licensed** — you can lift code with attribution.

**`silicakes/deluge-extensions` (DEx)** — *the* reference for this project. Read in order:

| File | Why |
|---|---|
| `src/lib/webMidi.ts:38‑43` | The two display request commands |
| `src/components/DisplayViewer.tsx:113‑139` | The complete reply dispatcher — full / delta / 7‑seg |
| `src/lib/display.ts:13‑88` | `unpack7to8Rle` — a correct, readable port of the firmware codec |
| `src/lib/display.ts:208‑281` | Frame and delta application into the 768‑byte buffer |
| `src/lib/smsysex.ts`, `sysexPacking.ts` | The JSON/file-transfer API for milestone 9 |
| `src/lib/checkFirmwareSupport.ts` | Firmware capability probing |

**`solaris76/Deluge-Synth-Editor`** — a preset editor (XML over SysEx file transfer), not a display mirror. It explicitly credits DEx for the smSysex implementation. Useful as a **second reading** of the file-transfer protocol in plain JS (`js/sysex-core.js`, `js/file-browser.js`), not for the screen.

Neither is ESP32 firmware — both are browser apps. **You are porting protocol logic, not code**, and the protocol logic is ~200 lines. The firmware C in `pack.c` is arguably the better source to port from since you're writing C/C++ anyway.

---

## 10. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Buying a board whose MCU has no USB‑OTG (classic ESP32 / WROOM‑32, e.g. any 2432Sxxx CYD) | 🔴 High | Hard blocker — no workaround; the soft-USB-host hack is low-speed HID only. Gate every board purchase on §6.5 item 1. |
| AliExpress spec blocks are stale copy-paste (the Guition listing claims "LX6 / 520K RAM" for what is an ESP32‑S3 board) | 🟠 Med | Never buy on the spec text alone. Confirm the module marking in the product photos and the model number; see the table in §6.3. |
| ESP32 USB‑MIDI host library drops SysEx (CIN 0x4–0x7) | 🔴 High | Milestone 1 exists to find this immediately. Worst case, drive ESP‑IDF's USB Host stack directly — it's ~300 lines for a bulk‑endpoint MIDI class driver. |
| Deluge host stack rejects your device descriptor (Option A only) | 🟠 Med | Prototype early; or switch to Option B, which sidesteps it entirely. |
| VBUS contention at Deluge boot (Option B) | 🟠 Med | Proper VBUS load switch / e‑fuse. Don't hard-tie the rails. |
| SysEx ID migration (`7D` → `00 21 7B 01`) | 🟡 Low | Accept both on receive. Send with `7D` (universally accepted) or probe with Ping and match the reply prefix. |
| 2 s subscription timeout / single-client `midiDisplayCable` | 🟡 Low | Re-arm at 1 Hz. Accept that DEx and your box can't mirror simultaneously. |
| `EmulatedDisplay` absent on the user's firmware release | 🟡 Low | Feature-detect: if only `02 41` frames arrive after sending `02 00 04`, fall back to 7‑seg rendering. |
| OLED burn-in | 🟡 Low | Honour the firmware screensaver; add local dimming/blanking. |
| Firmware API churn (this is an actively developed repo) | 🟡 Low | Pin behaviour to a firmware version, feature-detect via Ping / identity request (`F0 7E 7F 06 01 F7` returns firmware version). |

---

## 11. Legal / ethical

- **Deluge firmware:** GPLv3. You are not linking against it or shipping it — you're speaking its wire protocol. **Protocols aren't copyrightable.** No obligation triggered.
- **DEx / Deluge‑Synth‑Editor:** MIT. Copy freely, keep the copyright notices (Michael Katz; Chris Griggs).
- **OVERFIT:** closed and unreleased. Don't copy their name, logo, page copy, UI artwork, or enclosure design. Recreating the *function* from public APIs is entirely legitimate — this is the same protocol DEx has exposed in the open for years.
- Publish yours open source and you're not competing with them so much as doing the thing the community firmware was designed to enable.

---

## 12. Bottom line

The feasibility question is settled: **the Deluge streams its own screen over a documented SysEx endpoint, in a byte layout that is already the native format of any SSD1306‑family mono OLED.** An ESP32‑S3 that speaks USB‑MIDI host, decodes one RLE scheme, and blits 768 bytes is the entire minimum product — a weekend or two of work, not a research project.

Three things to settle before you order parts:

1. **The chip, before anything else.** ESP32‑**S3** (or S2/P4). A classic ESP32 has no USB‑OTG and cannot reach the Deluge at all — which is why the 3.2" CYD in §6 is out despite being a perfectly good screen.
2. **Which side hosts USB** — Option B (ESP32 as host) is the better product; Option A is the faster prototype. You can start on A and migrate.
3. **Mono OLED or colour TFT** — the Guition JC4827W543C is the strongest single candidate (§6.3). Still buy one of each for €20 and settle it empirically. Run the §6.1 arithmetic first: a bigger inch count does not mean a bigger Deluge screen, and anti-aliased fractional upscaling on a TFT is worth more than any integer scale factor.

And the thing that actually makes this worth building for a 7‑segment Deluge is not the screen. It's `Emulated Display` + `02 00 04`.

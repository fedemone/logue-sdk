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
| Is the linked AliExpress screen usable? | **Probably yes, but I could not open the listing** (AliExpress blocks automated fetching — 302 → 503). See §6 for a 5‑point checklist and what each likely candidate implies. If it is a **2.42" SSD1309 128×64 mono OLED, SPI**, it is the *ideal* part — pixel-exact 1:1 mapping, zero conversion code. |
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
`MIDICableDINPorts::sendSysex` exists, so the mirror works over 5‑pin DIN. At 31 250 baud you get ~3 125 B/s: a worst-case full frame is ~0.3 s. Deltas (tens of bytes) land in 10–50 ms, so menu text is usable but meters and waveforms will smear. **Not viable as the primary link, but a nice no-USB-port escape hatch.**

### Bandwidth budget (Option A or B)
Worst-case full frame ≈ 922 packed bytes → ~308 USB‑MIDI 4‑byte packets → ~1.2 kB on the wire → ~20 × 64‑byte bulk transactions ≈ 20 ms at the conservative 1 transaction/ms. Typical deltas are 1–2 orders of magnitude smaller. **The USB link is not the bottleneck; the Deluge's own 5–15 ms render loop is.** Expect 30–60 fps and ~10–30 ms of glass-to-glass latency.

---

## 6. The display — can you use that AliExpress module?

**I could not open your link.** `it.aliexpress.com` 302‑redirects to `aliexpress.us`, which returned HTTP 503 to every automated fetch, and searching the item ID `1005011714889431` returned nothing specific. So I can't confirm what it is. Here is how to decide in 30 seconds.

### Checklist — read these off the listing

1. **OLED or TFT LCD?** OLED modules are thin glass, monochrome (white/yellow/blue), 4–7 pins, no backlight mentioned. If the listing says ILI9341 / ST7789 / "TFT" / "IPS" / "backlight", it's an LCD.
2. **Resolution ≥ 128×48**, ideally exactly **128×64**.
3. **SPI available?** Many 2.42" modules ship strapped for **I²C** and need a 0 Ω resistor moved. Insist on 4‑wire SPI.
4. **Driver IC**: SSD1309 (2.42"), SSD1306 (0.96"), SH1106 (1.3"), SSD1322 (2.8" grayscale 256×64).
5. **3.3 V logic** compatible.

### Verdict by candidate

| If it is… | Verdict |
|---|---|
| **2.42" SSD1309 128×64 mono OLED, SPI** — most likely at €9.49 and "2.4 inch", and the near-certain match for OVERFIT's "2.4" OLED" | ✅ **Ideal.** 1:1 pixel mapping: `memcpy` the 768‑byte Deluge frame into pages 1–6 of the 1024‑byte display buffer (8 blank rows top and bottom, vertically centred) and flush. U8g2 supports SSD1309 128×64 out of the box and hands you `getBufferPtr()` in exactly this layout. |
| 1.3" SH1106 / 0.96" SSD1306 128×64 | ✅ Works identically, but *smaller than the stock Deluge OLED* — pointless except as a bench prototype. |
| 2.8" SSD1322 256×64 grayscale | ⚠️ Works, but 2× scaling needs 256×96 and you only have 64 rows. Either 1:1 (tiny) or non-square pixels (ugly). ~€25–35. Skip. |
| Colour TFT (ST7789 240×240, ILI9341 320×240, ST7796 480×320) | ✅ **Also perfectly workable, and a genuinely different product.** Nearest-neighbour 2× (256×96) or 3× (384×144), plus room for extra widgets, colour theming, an on-screen XY pad. Cheaper per square inch. ❌ Loses the OLED contrast/viewing angle and the "authentic Deluge look". |

### The size reality check nobody mentions

A 2.42" **128×64** panel showing only **128×48** yields a visible image of roughly **2.1" diagonal**. Against the stock Deluge OLED that's a meaningful but *modest* bump — maybe 1.6–1.8× linear. If your goal is "I can finally read it from across the room", a **3.5" 480×320 IPS at 3× scale** is bigger, cheaper, and more flexible; you're just trading OLED contrast for area. If your goal is "an authentic second Deluge screen", take the 2.42" OLED.

**My recommendation:** buy **both** (~€20 total). The rendering layer is ~50 lines either way, the SysEx layer is identical, and you'll know within an evening which one you actually want to live with.

### Two practical notes

- **Burn-in.** A static menu on a mono OLED for hours is exactly the burn-in worst case. The Deluge firmware already has a screensaver whose canvas the mirror correctly follows (`oled.cpp:373‑390`) — respect it, and add your own dimming timeout.
- **SPI speed.** At 8–20 MHz a full 768‑byte flush is well under 1 ms. I²C at 400 kHz is ~19 ms/frame before overhead — workable but wasteful. Use SPI.

---

## 7. Bill of materials (single unit)

| Item | ~Cost |
|---|---|
| ESP32‑S3 devkit, **N16R8 (PSRAM)**, dual USB (native + UART) | €8–12 |
| 2.42" SSD1309 128×64 SPI OLED | €9–14 |
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

The feasibility question is settled: **the Deluge streams its own screen over a documented SysEx endpoint, in a byte layout that is already the native format of the exact display panel you want to use.** An ESP32‑S3 that speaks USB‑MIDI host, decodes one RLE scheme, and blits 768 bytes is the entire minimum product — a weekend or two of work, not a research project.

The two things worth deciding before you order parts:

1. **Which side hosts USB** — Option B (ESP32 as host) is the better product; Option A is the faster prototype. You can start on A and migrate.
2. **Mono OLED or colour TFT** — buy one of each for €20 and settle it empirically.

And the thing that actually makes this worth building for a 7‑segment Deluge is not the screen. It's `Emulated Display` + `02 00 04`.

# Project plan — Deluge companion screen (ESP32‑S3 + Guition JC4827W543C)

**Baseline decision (accepted):** Guition **JC4827W543C** — ESP32‑S3‑WROOM‑1‑N4R8, 4.3" 480×272 IPS, NV3041A over QSPI, GT911 capacitive touch, native USB on the Type‑C, JST1.25 console UART. See [FEASIBILITY.md](FEASIBILITY.md) §6.3.

**Plan date:** 2026‑08‑23 · **Target v0.2 (the actual product):** ~3 weeks · **Target v1.0:** ~3 months

> Note on naming: the board is an **IPS LCD**, not an OLED. The thing being mirrored is the Deluge's 128×48 monochrome OLED framebuffer; the panel showing it is colour LCD. Worth keeping straight in code and docs.

---

## 1. Repository strategy

**Question asked: is a fork needed as the main repository to merge into?**

**No — not for the firmware.** There is no existing ESP32 project that does this; there is nothing upstream to merge into. The firmware is a new repository.
**Yes — for the web UI.** DEx is a near-complete implementation of everything we want in a browser, and forking it saves weeks.

| Repo | Role | Action |
|---|---|---|
| **`deluge-companion`** (new) | ESP32‑S3 firmware, hardware notes, docs. The main repo. | **Create fresh.** Nothing to fork. |
| **`deluge-extensions`** (fork of [silicakes/deluge-extensions](https://github.com/silicakes/deluge-extensions), MIT) | The web UI: display mirror, XY pad, SD‑card file browser. | **Fork.** Replace the WebMIDI transport with a WebSocket transport to the ESP32; keep the fork rebaseable on upstream. |
| [SynthstromAudible/DelugeFirmware](https://github.com/SynthstromAudible/DelugeFirmware) (GPL‑3.0) | Protocol reference only. | **Do not fork.** Pin a commit in docs (currently `4f4a176`). We never modify the Deluge. |
| `fedemone/logue-sdk` (this repo) | Where the feasibility study currently lives. | **Not the right home.** Move `docs/deluge-oled-companion/` into `deluge-companion` once it exists; leave a pointer or drop the branch. |

### Why fork DEx rather than vendor it

DEx's architecture already isolates the transport: `src/lib/webMidi.ts` owns the MIDI ports, everything downstream consumes decoded messages. Swapping that one module for a WebSocket client that talks to the ESP32 gives us — unchanged — the display decoder, the delta handling, the 7‑seg renderer, the SysEx console, the fuzzy file search, the drag‑and‑drop file browser, and the transfer queue. That is the single largest lever in the whole project. A fork (not a copy) keeps upstream fixes mergeable.

### Licence decision — needed before the first commit

The RLE codec exists in two forms:

- **DelugeFirmware `src/deluge/util/pack.c`** — GPL‑3.0. Lifting it verbatim makes the firmware GPL‑3.0.
- **DEx `src/lib/display.ts:13‑88`** — MIT. Porting that to C keeps us MIT-clean.

**Recommendation: GPL‑3.0 for `deluge-companion`.** It is the community norm in this ecosystem, it lets us lift firmware code verbatim without thinking about it, and it matches the project's premise — an open answer to a closed product. It does not prevent selling units; it requires shipping source with them. Choose MIT only if you want a proprietary path later.

---

## 2. What already exists (reuse)

| Need | Source | Licence | State |
|---|---|---|---|
| **USB‑MIDI host with SysEx** | [`sauloverissimo/ESP32_Host_MIDI`](https://github.com/sauloverissimo/ESP32_Host_MIDI) (ESP component: `sauloverissimo/midi2`) | MIT | ✅ RX **and** TX SysEx, queue + callback, Arduino/PlatformIO/ESP‑IDF, ESP32‑S3 host. ~198 commits, active. |
| Fallback USB host MIDI | [`enudenki/esp32-usb-host-midi-library`](https://github.com/enudenki/esp32-usb-host-midi-library) | MIT | SysEx support unconfirmed — hold as plan B. |
| **NV3041A QSPI display** | [`moononournation/Arduino_GFX`](https://github.com/moononournation/Arduino_GFX) | check before shipping | ✅ Proven on this exact board. |
| Board pin maps & examples | [`profi-max/JC4827W543_4.3inch_ESP32S3_board`](https://github.com/profi-max/JC4827W543_4.3inch_ESP32S3_board), [`morfirk/JC4827W543_ESP32`](https://github.com/morfirk/JC4827W543_ESP32) | **unstated** | Reference only until a licence appears. Don't copy code. |
| Factory documentation | [`lsdlsd88/JC4827W543`](https://github.com/lsdlsd88/JC4827W543) | factory drop | IO pin table, spec PDF, Arduino demos. |
| GT911 touch | `espressif/esp_lcd_touch_gt911` | Apache‑2.0 | ✅ Official component. |
| RLE codec | `pack.c` (GPL‑3.0) **or** DEx `display.ts` (MIT) | see §1 | ✅ Both correct; pick per licence choice. |
| Protocol reference | DelugeFirmware `hid/hid_sysex.cpp`, `io/midi/sysex.h`, `io/midi/midi_engine.cpp` | GPL‑3.0 | ✅ Fully documented in FEASIBILITY.md §3. |
| Web UI + file browser | [`silicakes/deluge-extensions`](https://github.com/silicakes/deluge-extensions) | MIT | ✅ Fork target. |
| BLE‑MIDI | `h2zero/NimBLE-Arduino` + a BLE‑MIDI service | Apache‑2.0 | Phase 7. |
| HTTP + WebSocket | `ESPAsyncWebServer`, or ESP‑IDF `esp_http_server` | — | Phase 5. |

### Two gaps in the reuse story

1. **`esp_lcd` has no NV3041A **QSPI** driver.** The registry component `eric-c-e/esp_lcd_nv3041` is **4‑wire SPI only** (confirmed). ESP‑IDF's `esp_lcd` *does* support QSPI panels, but a NV3041A vendor driver has to be written (~200–300 lines: init register sequence plus a `draw_bitmap` wrapping 0x2A/0x2B/0x2C in the QSPI command framing). **Or** use Arduino_GFX, which already does it. See §4.
2. **`ESP32_Host_MIDI` defaults to `maxSysExSize = 512`.** The Deluge's worst-case full frame packs to **922 bytes**, and the SysEx wrapper adds 11. **Set `maxSysExSize` ≥ 1024 or frames will be silently truncated.** This is the single most likely "why is nothing working" moment in the project — put it in the first commit.

---

## 3. What has to be written

| # | Module | What it does | Size |
|---|---|---|---|
| M1 | `sysex_rle.c/h` | `unpack_7to8_rle()` and `unpack_7bit_to_8bit()`. Pure, host-testable. | ~120 lines |
| M2 | `deluge_proto.c/h` | Request builders, reply parser accepting **both** manufacturer prefixes (`F0 7D` and `F0 00 21 7B 01`), 768‑byte framebuffer, delta application at `start*8`, 7‑seg segment decode, ping/identity probe. | ~350 lines |
| M3 | `deluge_link.c/h` | Connection state machine: enumerate → probe → subscribe → **re-arm every 1 s** (2 s server timeout) → resync on gap/error → handle unplug and Deluge reboot. Sends `02 00 04` to swap a 7SEG unit into emulated display. | ~250 lines |
| M4 | `mirror_render.c/h` | 128×48 1‑bit → RGB565. **3.75× box-filter upscale with anti-aliasing** to 480×180, colour LUT (amber/white/custom), dirty-band tracking so only the changed row band is flushed. | ~250 lines |
| M5 | `board_jc4827w543.h` + `panel_nv3041a.c` | Pin map, backlight LEDC PWM, QSPI panel bring-up. | ~300 lines |
| M6 | `ui.c` | The spare 480×92 strip: connection state, firmware version, framerate, mode. | ~200 lines |
| M7 | `net.c` | Wi‑Fi AP+STA, provisioning, `esp_http_server`, WebSocket streaming the same frames. | ~350 lines |
| M8 | `xy.c` | GT911 touch → MIDI CC out, preset slots. | ~200 lines |
| M9 | `smsysex.c` | The JSON file-transfer API (SysEx commands `4`/`5`), ported from DEx `smsysex.ts` + `sysexPacking.ts`. **The big one.** | ~800 lines |
| M10 | `blemidi.c` | BLE‑MIDI bridge (NimBLE). | ~300 lines |
| H1 | Power path | VBUS to the Deluge: powered USB‑C OTG adapter, or USB‑A socket + 5 V boost off a Li‑ion cell. | hardware |
| H2 | Enclosure | 3D-printed case, Deluge-top mount or desk stand. | hardware |

**Roughly 3 000 lines of new firmware**, of which M1–M5 (~1 300 lines) is the entire minimum product.

---

## 4. What fits the Guition board specifically

### Pin map (from the factory IO table and community configs — verify on the bench)

| Function | Pins |
|---|---|
| Display NV3041A QSPI | CS **45**, CLK **47**, D0–D3 **21 / 48 / 40 / 39** |
| Backlight | **GPIO 1** (LEDC PWM) |
| Touch GT911 (I²C) | SDA **8**, SCL **4**, INT **3**, RST **38** |
| **USB host** | **GPIO19 (D−) / GPIO20 (D+)** — native, on the Type‑C |
| **Console UART** | JST1.25 4‑pin: **IO17 U1TXD**, **IO18 U1RXD**, +5 V, GND |
| microSD | TF slot — pins to confirm from factory docs |

### Framework: PlatformIO + Arduino (arduino‑esp32 3.x)

**Recommended for v0.x.** arduino‑esp32 3.x sits on ESP‑IDF 5.1+, so IDF APIs stay callable, and it is the only path where *every* library we want already works on this exact board: Arduino_GFX drives the NV3041A QSPI today, ESP32_Host_MIDI supports Arduino, NimBLE‑Arduino and ESPAsyncWebServer are one line each.

The alternative — pure ESP‑IDF with `esp_lcd` — is cleaner long-term and gives better DMA control, but costs a hand-written QSPI panel driver up front (§2 gap 1). **Migrate the display layer to `esp_lcd` at v0.3 if the renderer needs the headroom; not before.** Do not let framework purity delay first pixels.

### Flash budget (4 MB)

App with Wi‑Fi + USB host + display lands ~1.2–2 MB.
- **v0.x:** single `factory` partition, no OTA → ~3 MB available. Comfortable.
- **v1.0:** either stay single-app and flash over USB, or squeeze dual‑OTA at ~1.5–1.9 MB/slot. Web assets go in a LittleFS partition, gzipped.

### Render budget

3.75× box filter over 480×180 ≈ 1–3 ms; QSPI flush of the changed band only. A one-page delta (16 source rows → 60 dest rows) is ~57 kB ≈ 1.5 ms at 40 MB/s. **30–60 fps with room to spare.**

---

## 5. Phases, tasks and schedule

Assumes one developer working evenings and weekends. Hardware from AliExpress: budget **2–3 weeks in transit** — Phase 0 is designed to fill exactly that window with useful work.

### Phase 0 — Procure & scaffold · **Aug 24 – Sep 6** · ~12 h · *no hardware needed*

- [ ] **Order parts.** JC4827W543**C** (capacitive), powered USB‑C OTG adapter, USB‑A↔USB‑B cable, 2.42" SSD1309 SPI OLED for comparison, JST1.25 pigtail for the console.
- [ ] **Confirm the board on arrival photos/listing:** module silkscreen reads `ESP32‑S3‑WROOM‑1`, not WROVER. (FEASIBILITY §6.3)
- [ ] Create `deluge-companion`; decide licence (§1); README, `.gitignore`, `platformio.ini`, CI that builds on push.
- [ ] Fork `silicakes/deluge-extensions`; add an upstream remote; confirm it builds untouched.
- [ ] Move `docs/deluge-oled-companion/` out of `logue-sdk` into the new repo.
- [ ] **Capture golden test vectors.** Open [dex.silicak.es](https://dex.silicak.es) with the Deluge on USB, use the SysEx console + "Copy Base64" to capture: a full frame, several deltas, a 7‑seg frame, a ping/pong, an identity reply. Commit as `test/vectors/`. *This is the highest-value hour in Phase 0 — it lets M1–M2 be written and tested before any hardware exists.*
- [ ] **M1 + M2 on the host.** Build `sysex_rle.c` and `deluge_proto.c` as a plain desktop binary with a unit test that decodes the golden vectors to the expected 768‑byte frames. Render to PNG to eyeball it.

**Done when:** `make test` on your laptop turns captured SysEx into correct frames, and both repos exist and build.

### Phase 1 — The spike · **first evening with hardware** · ~4 h · 🔴 GO/NO‑GO

- [ ] Flash blink; confirm the Type‑C enumerates as `ttyACM*` / "USB JTAG serial debug unit" (**not** CH340). Console on the JST1.25 header.
- [ ] Bring up USB host with `ESP32_Host_MIDI`. **Set `maxSysExSize = 1024`, `maxSysExEvents = 8`.**
- [ ] Power the Deluge's USB‑B from the OTG adapter; enumerate it; log the descriptor.
- [ ] Send `F0 7D 02 00 03 F7`. Log the reply length and first bytes.

**Gate:** a reply of roughly 200–930 bytes arrives intact. If SysEx is truncated at 512, raise the limit. If SysEx never arrives at all, switch to `enudenki`'s library; if that also fails, write a minimal MIDI class driver on ESP‑IDF's `usb_host` (~300 lines). **Do not proceed to Phase 2 until a frame arrives.**

### Phase 2 — v0.1 "It mirrors" · **week 1** · ~8 h

- [ ] M5: panel bring-up via Arduino_GFX, backlight PWM, landscape 480×272 at 0°.
- [ ] M4: 3.75× upscale to 480×180, centred, amber-on-black LUT.
- [ ] Wire M2 (already tested) to the live USB stream; render full frames.
- [ ] M3 minimal: subscribe, re-arm at 1 Hz.

**Done when:** the Deluge's screen is on the panel, live. **Tag `v0.1`.** *This is the moment the project is real.*

### Phase 3 — v0.2 "It's reliable" · **week 2** · ~8 h

- [ ] Delta application (`02 40 02`, offset `start*8`), dirty-band flush.
- [ ] Full-frame resync on connect, on decode error, and every N seconds as a safety net.
- [ ] Hot-plug: Deluge unplug/replug, Deluge reboot, our own reboot.
- [ ] 7‑seg path (`02 41 00`) — segment bitmaps + dots.
- [ ] Auto-detect 7SEG and send `02 00 04` to swap into emulated display; fall back gracefully if the firmware lacks it.
- [ ] Identity probe (`F0 7E 7F 06 01 F7`) → show firmware version; accept both reply prefixes.
- [ ] M6: status strip in the spare 480×92.

**Done when:** you can gig with it — plug in, unplug, reboot, and it recovers every time. **Tag `v0.2`. This is the product.**

### Phase 4 — v0.3 "It's a device" · **week 3** · ~8 h

- [ ] Power path (H1): decide OTG adapter vs integrated boost; measure Deluge VBUS draw.
- [ ] Li‑ion cell + charging; battery indicator in the status strip.
- [ ] Screensaver / dimming timeout; honour the Deluge's own screensaver.
- [ ] Enclosure v1 (H2).
- [ ] Settings persisted in NVS: colour theme, brightness, scale, auto-swap on/off.

**Done when:** it's a box you can hand to someone. **Tag `v0.3`.**

### Phase 5 — v0.4 "Wireless mirror" · **weeks 4–5** · ~16 h

- [ ] M7: Wi‑Fi AP + optional STA, captive provisioning page, `esp_http_server` + WebSocket.
- [ ] Stream the same decoded frames over WebSocket.
- [ ] In the DEx fork: add `src/lib/wsTransport.ts` alongside `webMidi.ts`, selectable at runtime. Everything downstream unchanged.
- [ ] Serve the built DEx bundle from LittleFS (gzipped).

**Done when:** a phone on the box's Wi‑Fi shows the Deluge screen fullscreen. **Tag `v0.4`.**

### Phase 6 — v0.5 "XY pad" · **week 6** · ~8 h

- [ ] M8: GT911 touch → CC pairs, configurable channel/CC, preset slots in NVS.
- [ ] On-panel XY mode using the spare strip, and a full-screen XY mode.
- [ ] Same XY control in the web UI.

**Tag `v0.5`.**

### Phase 7 — v0.6 "BLE MIDI" · **week 7** · ~10 h

- [ ] M10: NimBLE BLE‑MIDI service; bridge BLE ↔ the Deluge's USB link.
- [ ] Routing matrix: what goes where (BLE in → Deluge, XY → Deluge, etc.).

**Tag `v0.6`.**

### Phase 8 — v1.0 "File manager" · **weeks 8–11** · ~30 h

- [ ] M9: port the smSysex JSON API from DEx (`smsysex.ts`, `sysexPacking.ts`, `delugeErrors.ts`).
- [ ] Directory listing, download, upload, rename, delete, mkdir.
- [ ] Expose over the web UI (the DEx file browser then works as-is).
- [ ] Requires Deluge CFW ≥ 1.3 — feature-detect and hide when unavailable.

**Done when:** you can drag a WAV onto a browser tab and it lands on the Deluge's SD card. **Tag `v1.0`.**

### Milestone calendar

| Week | Dates (2026) | Deliverable |
|---|---|---|
| 0 | Aug 24 – Sep 6 | Repos, host-side decoder, golden vectors, parts ordered |
| 1 | Sep 7 – 13 | 🔴 Spike gate → **v0.1** mirror live |
| 2 | Sep 14 – 20 | **v0.2** reliable — *the product* |
| 3 | Sep 21 – 27 | **v0.3** battery, enclosure, settings |
| 4–5 | Sep 28 – Oct 11 | **v0.4** Wi‑Fi mirror + DEx fork |
| 6 | Oct 12 – 18 | **v0.5** XY pad |
| 7 | Oct 19 – 25 | **v0.6** BLE MIDI |
| 8–11 | Oct 26 – Nov 22 | **v1.0** file manager |

Dates assume parts land in week 1. Everything from Phase 4 on is independent — reorder freely by what you actually want.

---

## 6. Open decisions

| # | Decision | Recommendation | Blocks |
|---|---|---|---|
| D1 | Repo name and owner for the firmware | `fedemone/deluge-companion` | Phase 0 |
| D2 | Licence — GPL‑3.0 or MIT | **GPL‑3.0** (§1) | first commit |
| D3 | Framework — PlatformIO+Arduino or pure ESP‑IDF | **PlatformIO + Arduino 3.x**, migrate the display layer later if needed (§4) | Phase 0 |
| D4 | v1.0 scope — is the file manager in, or is v1.0 the mirror + Wi‑Fi? | Ship **v0.4 as "1.0"** publicly; keep the file manager as 1.1 | Phase 8 |
| D5 | Power — powered OTG adapter or integrated boost + cell | Adapter for v0.1–v0.2, integrated at v0.3 | Phase 4 |

---

## 7. Standing risks

| Risk | Gate |
|---|---|
| SysEx truncated at 512 bytes | Phase 1 — set `maxSysExSize = 1024` up front |
| USB host library drops SysEx entirely | Phase 1 gate; plan B and C named above |
| Board is a classic ESP32, not S3 | Phase 0 — check the silkscreen before paying |
| Type‑C turns out to be CH340 | Phase 1 — `ttyACM` check; fall back to soldering GPIO19/20 |
| VBUS contention at Deluge boot | Phase 1/4 — load switch, don't hard-tie rails |
| 4 MB flash too tight at v1.0 | Phase 8 — drop OTA, or move to a 16 MB board |
| NV3041A byte-swap / colour-invert quirks | Phase 2 — known, documented in FEASIBILITY §6.3 |
| Firmware API churn upstream | Pin the reference commit; feature-detect via ping and identity |

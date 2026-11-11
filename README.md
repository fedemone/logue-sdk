# logue-sdk 

[![Discord](https://img.shields.io/discord/1037767234803740694?logo=discord&label=Discord)](https://discord.com/invite/pqW5VpdQkZ)

[日本語](./README_ja.md)

This repository contains all the files and tools needed to build custom oscillators and effects for the [prologue](https://www.korg.com/products/synthesizers/prologue), [minilogue xd](https://www.korg.com/products/synthesizers/minilogue_xd), [Nu:Tekt NTS-1 digital kit](https://www.korg.com/products/dj/nts_1), [Nu:Tekt NTS-1 digital kit mkII](https://www.korg.com/products/synthesizers/nts_1_mk2), [microKORG2](https://www.korg.com/products/synthesizers/microkorg2) synthesizers, the [Nu:Tekt NTS-3 kaoss pad kit](https://www.korg.com/products/dj/nts_3), and [drumlogue](https://www.korg.com/products/drums/drumlogue) drum machine.

This is a personal fork of KORG's official [logue-sdk](https://github.com/korginc/logue-sdk) that additionally hosts a number of custom, independently developed synth and effect units (for example, but not limited to, `OmniPress`, `NeonLabirinto`, `LuceAlNeon`, `ScrutaAstri`, `EffeESP32`, `EffeMD`, `PortaCassette`, `Brachetti` (and its eight per-family `Brachetti` units) and `delay_tribal` under [platform/drumlogue/](platform/drumlogue/)), plus the `Pelle e Legno` percussion sample kit. **Please read the [Disclaimer](#disclaimer) below before downloading, building, installing, or using any of them.**  To find the document you need — and only that one — see the [Documentation map](#documentation-map).

## Disclaimer

The custom synth and effect units distributed from this repository are **unofficial, independently developed, third-party projects**. They are not created, reviewed, tested, certified, endorsed, or supported by KORG Inc. or any other hardware manufacturer named or implied in this repository. Product and platform names (e.g. "KORG", "drumlogue", "prologue", "minilogue", "Nu:Tekt", "NTS-1", "NTS-3", "microKORG") are trademarks of their respective owners and are used here solely to identify hardware compatibility; no affiliation, sponsorship, or endorsement is claimed or implied.

These units are experimental, hobbyist software, distributed for free with **no guarantee of correctness, stability, sound quality, safety, or compatibility** with any past, current, or future firmware or hardware revision. Nothing here is a finished, quality-assured commercial product.

By downloading, building, installing, or using any unit, source code, or tool from this repository, you acknowledge and agree that:

* Everything is provided **"AS IS" and "AS AVAILABLE", WITHOUT WARRANTY OF ANY KIND**, express or implied, including but not limited to the implied warranties of merchantability, fitness for a particular purpose, title, and non-infringement.
* **Use is entirely at your own risk.** You are solely responsible for evaluating the risks of installing and running third-party unit files on your hardware before doing so.
* Neither the developer(s) of these units, nor this repository's maintainer(s), nor any third party who distributes, mirrors, or hosts them (collectively, the "developers and distributors") shall be held liable for any damage, malfunction, defect, data loss, bricked or degraded hardware, hearing damage or other injury from unexpected audio behavior, lost time, lost revenue, or any other direct, indirect, incidental, special, exemplary, or consequential damages of any kind arising out of, or in any way connected with, the download, build, installation, or use (or inability to use) of this software — even if advised in advance of the possibility of such damages, and regardless of the legal theory (contract, tort, negligence, or otherwise) under which liability is asserted.
* No official technical support, maintenance, or bug-fix commitment is implied by publishing this code. Any support offered by the developer(s) is provided on a purely best-effort, voluntary basis and is entirely separate from, and not a substitute for, official support channels from KORG or any other hardware manufacturer.
* Units, source files, and documentation in this repository may be changed, renamed, moved, or removed at any time without prior notice.
* Where local law does not allow the exclusion or limitation of certain warranties or liabilities, the above exclusions and limitations apply only to the maximum extent permitted by that law.

If you do not agree with these terms, do not download, build, install, or use any of the units or code made available through this repository.

## How to use these custom effects

* Plug the USB cable to the Drumlogue (switched off) and to the PC
* Power on the Drumlogue keeping pressed the "record" button until a new window appear on the PC
* Browse to Unit folder
* Drop the *.drmlogunit files into the appropriate folder:
* - reverb: Luce al Neon, Neon Labirinto
  - delay: Tribale
  - master FX: Omnipress, Porta Cassette
  - synth: EffeMD, EffeSP32, Brachetti, Scruta Astri, and the Brachetti family units (BrcKick, BrcTom,
    BrcKernel, BrcSnare, BrcBar, BrcPlate, BrcCymbal, BrcNoise — one per Brachetti instrument
    family, see [platform/drumlogue/tools/brachetti_equiv](platform/drumlogue/tools/brachetti_equiv/README.md))
* Press "play" button on Drumlogue to start

### Pelle e Legno sample kit

[Pelle e Legno](platform/drumlogue/pelle_legno/) is a kit of 61 synthesised organic, skin and ethnic percussion samples (djembe, conga, tabla, darbuka, cajón, talking drum, shakers, guiro, wood blocks, udu, kalimba, balafon and more). Copy the files inside its `Samples/` folder into the drumlogue's `Samples/` folder (same USB mode as above). They then appear in BANK `USER` of the CH, OH, RS, CP, SP1 and SP2 parts. Its README has ready-made kit recipes.

## Documentation map

Read it top-down: each entry says what a document holds, so you open one only
when you need what is in it.  The KORG SDK's own documents (one README per
platform, `docker/`, `tools/`) are listed under [Repository Structure](#repository-structure).

- **This README** — the fork, the disclaimer, installing units on the drumlogue.
- [`platform/drumlogue/README.md`](platform/drumlogue/README.md) ([日本語](platform/drumlogue/README_ja.md)) — the drumlogue SDK: the Docker build environment, listing and building units, the project layout.
  - **Synth units**
    - **Brachetti** — physical-modelling percussion: the original 40-preset unit and its eight per-family units ([below](#brachetti-documents)).
    - [EffeESP32](platform/drumlogue/EffeESP32/README.md) — 6-operator FM drum synth: features, architecture, every GUI parameter; 59 presets, one per instrument (see "Instrument selection and the panel").  [`PROGRESS.md`](platform/drumlogue/EffeESP32/PROGRESS.md): the porting log and open TODOs — for whoever works on it next.
    - [EffeMD](platform/drumlogue/EffeMD/README.md) — Machinedrum-style FM percussion synth: history, architecture, building, tests; 2 presets (the instrument is a parameter).
    - [ScrutaAstri](platform/drumlogue/scruta_astri/README.md) — drone synth: signal path, LFOs, modulation matrix; 242 programs, laid out in "The Program Layout".
  - **Effects** (no preset slots)
    - [OmniPress](platform/drumlogue/omnipress/README.md) — master compressor: the modes, DRIVE, the parameter reference, the signal flow.
    - [PortaCassette](platform/drumlogue/PortaCassette/README.md) (`PortaK7`) — master effect, Portastudio and vinyl emulation: signal chain, machines, parameters.
    - [NeonLabirinto](platform/drumlogue/reverb_labirinto/README.md) — reverb; its five factory settings (foresta, tempio, labirinto, esotico, stellare) are chosen with its Preset parameter.
    - [LuceAlNeon](platform/drumlogue/reverb_light/README.md) — reverb, five parallel paths: design and user guide.
    - [delay_tribal](platform/drumlogue/delay_tribal/README.md) (Tribale) — delay, a percussion micro-ensemble.  [`TODO.md`](platform/drumlogue/delay_tribal/TODO.md): hardware findings, fixes and open items.
    - Each synth and effect above also carries a `README_<NAME>.md`: an identical copy of its `README.md`.
  - **Host tools**
    - [`tools/level_meter`](platform/drumlogue/tools/level_meter/README.md) — what a synth unit puts on the bus, measured on the host.
    - [`tools/stack_meter`](platform/drumlogue/tools/stack_meter/README.md) — what a synth unit does with stacked hits.
    - [`tools/brachetti_equiv`](platform/drumlogue/tools/brachetti_equiv/README.md) — the Brachetti family's checks (below).

### Brachetti documents

The original 40-preset unit split into eight per-family units, which share
one library.  Go down a level only for what you need:

1. [**The Brachetti family**](platform/drumlogue/tools/brachetti_equiv/README.md) — start here: the eight units, how they were split from the original and restructured, how they are verified bit-identical to it (and the tools that do it), their sizes and CPU, how to maintain them.
2. **The family units** — each has a `README.md` (presets, knob pages, what every knob does, its files, build, verification) and a `CLAUDE.md` (the rules for changing it).  Presets in the order the unit shows them, and the same presets' slots in the original:

   | Unit · rules | On the drumlogue | Presets | Slots in the original | Engine |
   |---|---|---|---|---|
   | [`brachetti_kick`](platform/drumlogue/brachetti_kick/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_kick/CLAUDE.md) | `BrcKick` | Kick2, 808Sub, Kick | 0, 2, 20 | membrane modes + boom + thump |
   | [`brachetti_tom`](platform/drumlogue/brachetti_tom/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_tom/CLAUDE.md) | `BrcTom` | Djambe, Ac Tom, DeepBs, Conga, Handpn, Bongo, RackTom; added: DjmTone, DjmSlap, DjmBass, CngOpen, CngMute, CngSlap, CngHeel, Tumba, BngMach, BngHmbr, TblaNa, TblaTin, TblaTun, TblaGe, TblGeBd, TblaKe, DrbDoum, DrbTek, DrbKa, TalkUp, TalkDn, SurdOpn, SurdMut, Tambrim, UduBass | 6, 12, 23, 27, 28, 33, 39 | membrane modes + boom + strike transient |
   | [`brachetti_kernel`](platform/drumlogue/brachetti_kernel/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_kernel/CLAUDE.md) | `BrcKernel` | Timpni, Taiko | 5, 7 | dense coupled-resonator drum kernel |
   | [`brachetti_snare`](platform/drumlogue/brachetti_snare/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_snare/CLAUDE.md) | `BrcSnare` | AcSnre, MrchSnr, BrshSnr, RimShot; added: BndDoum, BndTek, CajBass, CajSlap | 3, 8, 37, 38 | modal head + snare-wire resonators |
   | [`brachetti_bar`](platform/drumlogue/brachetti_bar/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_bar/CLAUDE.md) | `BrcBar` | Marmba, TblrBel, Vibrph, Wodblk, Kalimba, StelPan, Claves, GlsBwl, SltDrm, GlsBotl; added: WdBlkHi, TmplBlk, SltDrHi, SltDrLo, Bamboo, Coconut, UduTone, KlmbE4, KlmbA4 | 1, 4, 10, 11, 15, 16, 17, 24, 30, 34 | free-bar modes + mallet |
   | [`brachetti_plate`](platform/drumlogue/brachetti_plate/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_plate/CLAUDE.md) | `BrcPlate` | Cowbel, Trngle, BelTre, Tick; added: AgogoHi, AgogoLo, Zills | 18, 19, 29, 35 | plate modes + ring-coupled noise |
   | [`brachetti_cymbal`](platform/drumlogue/brachetti_cymbal/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_cymbal/CLAUDE.md) | `BrcCymbal` | Cymbal, Gong, HHat-O, Ride, RidBel, Splash | 13, 14, 26, 31, 32, 36 | dense-resonator cymbal |
   | [`brachetti_noise`](platform/drumlogue/brachetti_noise/README.md) · [CLAUDE.md](platform/drumlogue/brachetti_noise/CLAUDE.md) | `BrcNoise` | Clap, Shaker, HHat-C; added: ShkFwd, ShkBack, Maracas, Caxixi, Cabasa | 21, 22, 25 | shaped noise + AM gating |

   The original's 40th preset, Koto (slot 9, a Karplus-Strong string), is in no family unit; it stays playable in the original.
3. [**The common library**](platform/drumlogue/brachetti_common/README.md) — what the family units share: what is where, the **helper index** (open it before writing a helper), the rules for changing shared code, the quirks kept on purpose, what was removed or moved.
4. **The original** — [`brachetti/README.md`](platform/drumlogue/brachetti/README.md): the 40-preset unit — overview, signal flow, architectural decisions and quirks, the tuning toolchain, the parameter reference (`README_BRACHETTI.md` is a copy).  Its other documents record how the sounds were voiced; open them only for that history:
   - [`CLAUDE.md`](platform/drumlogue/brachetti/CLAUDE.md) — the session brief: the rules and the record of every change and measurement.
   - [`PROGRESS.md`](platform/drumlogue/brachetti/PROGRESS.md), [`PROGRESS2.md`](platform/drumlogue/brachetti/PROGRESS2.md) — status trackers and TODO history.
   - [`NOTE_AUDIT.md`](platform/drumlogue/brachetti/NOTE_AUDIT.md) — what each preset's root note does, measured.
   - [`REALISM_REVIEW.md`](platform/drumlogue/brachetti/REALISM_REVIEW.md) — the instrument-family realism review and its hardware rounds.
   - [`cymbal_synthesis/README.md`](platform/drumlogue/brachetti/cymbal_synthesis/README.md), [`MERGE_FEASIBILITY.md`](platform/drumlogue/brachetti/cymbal_synthesis/MERGE_FEASIBILITY.md) — the cymbal prototype and how it was merged into the unit.
   - [`samples/README.md`](platform/drumlogue/brachetti/samples/README.md) — the reference recordings and how renders are scored against them.

## Existing logue SDK Units

To download ready to use oscillators and effects, refer to the [Unit Index](https://korginc.github.io/logue-sdk/unit-index/) and follow instructions on the developer's website.

There is an easier to navigate unit index page called [logue-SDK-filter](https://logue-sdk.vercel.app/).

#### Binary Compatibility

User units built for prologue, minilogue xd and Nu:Tekt NTS-1 digital kit (mkI only) are binary compatible with each other, as long as the SDK version matches.
However, developers are encouraged to optimize their units for each target platform, and thus specialized builds should be preferred if available.

## Repository Structure

* [platform/prologue/](platform/prologue/) : *prologue* specific files, templates and demo projects.
* [platform/minilogue-xd/](platform/minilogue-xd/) : *minilogue xd* specific files, templates and demo projects.
* [platform/nutekt-digital/](platform/nutekt-digital/) : *Nu:Tekt NTS-1 digital kit* specific files, templates and demo projects.
* [platform/drumlogue/](platform/drumlogue/) : *drumlogue* specific files and templates.
* [platform/nts-1_mkii/](platform/nts-1_mkii/) : *Nu:Tekt NTS-1 digital kit mkII* specific files, templates and demo projects.
* [platform/nts-3_kaoss/](platform/nts-3_kaoss/) : *Nu:Tekt NTS-3 kaoss pad kit* specific files, templates and demo projects.
* [platform/microkorg2/](platform/microkorg2/) : *microKORG2* specific files, templates and demo projects.
* [platform/ext/](platform/ext/) : External dependencies and submodules.
* [docker/](docker/) : Sources for a docker container that allows building projects for any platform in a more host OS agnostic way.
* [tools/](tools/) : Installation location and documentation for tools required to build projects and manipulate built products. Can be ignored if using the docker container.
* [devboards/](devboards/) : Information and files related to limited edition development boards.



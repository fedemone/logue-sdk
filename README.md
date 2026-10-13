# logue-sdk 

[![Discord](https://img.shields.io/discord/1037767234803740694?logo=discord&label=Discord)](https://discord.com/invite/pqW5VpdQkZ)

[日本語](./README_ja.md)

This repository contains all the files and tools needed to build custom oscillators and effects for the [prologue](https://www.korg.com/products/synthesizers/prologue), [minilogue xd](https://www.korg.com/products/synthesizers/minilogue_xd), [Nu:Tekt NTS-1 digital kit](https://www.korg.com/products/dj/nts_1), [Nu:Tekt NTS-1 digital kit mkII](https://www.korg.com/products/synthesizers/nts_1_mk2), [microKORG2](https://www.korg.com/products/synthesizers/microkorg2) synthesizers, the [Nu:Tekt NTS-3 kaoss pad kit](https://www.korg.com/products/dj/nts_3), and [drumlogue](https://www.korg.com/products/drums/drumlogue) drum machine.

This is a personal fork of KORG's official [logue-sdk](https://github.com/korginc/logue-sdk) that additionally hosts a number of custom, independently developed synth and effect units (for example, but not limited to, `OmniPress`, `NeonLabirinto`, `LuceAlNeon`, `ScrutaAstri`, `EffeESP32`, `EffeMD`, `PortaCassette`, `Brachetti` and `delay_tribal` under [platform/drumlogue/](platform/drumlogue/)). **Please read the [Disclaimer](#disclaimer) below before downloading, building, installing, or using any of them.**

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
  - synth: EffeMD, EffeSP32, Brachetti, Scruta Astri
* Press "play" button on Drumlogue to start

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



## About this project

This repository is a successful port of the [RipplerX](https://github.com/tiagolr/ripplerx/) physical modelling synth to Korg Drumlogue.
The version is based on 1.5.0-2 with Juce dependencies removed and ARM NEON intrinsics added for optimized processing.

**Status:** ✅ Successfully compiled and built as `ripplerx.drmlgunit`

### Build Process (Windows with Docker Desktop)

#### Prerequisites
1. **Docker Desktop** with WSL2 backend
2. **File Sharing Configuration:**
   - Open Docker Desktop → Settings → Resources → File Sharing
   - Add `D:\Fede` (parent directory of your workspace)
   - Apply and restart Docker Desktop

#### Building

**Option 1: Using direct Docker command (recommended for Windows):**
```bash
cd /d/Fede/drumlogue/logue-sdk-ripplerx/docker
MSYS_NO_PATHCONV=1 docker run --rm -v "d:/Fede/drumlogue/logue-sdk-ripplerx/platform:/workspace" -h logue-sdk -it logue-sdk-dev-env:latest //app/interactive_entry
```

Inside the container:
```bash
env drumlogue
build drumlogue/ripplerx
```

**Option 2: Using run_interactive.sh:**
```bash
cd /d/Fede/drumlogue/logue-sdk-ripplerx/docker
bash run_interactive.sh
env drumlogue
build drumlogue/ripplerx
```

#### Build Output
- Unit file: `platform/drumlogue/ripplerx/ripplerx.drmlgunit`
- Size: ~80KB compiled binary

### Installation on Drumlogue
1. Power up drumlogue in USB mass storage mode
2. Copy `ripplerx.drmlgunit` to `Units/Synths/` directory
3. Restart drumlogue
4. RipplerX will appear in synth selection

### Technical Notes

**Key Fixes Applied:**
- Changed `models->` to `models.` (object member access, not pointer)
- Changed `voice->` to `voice.` in range-based for loop (array elements, not pointers)

**Architecture:**
- 8-voice polyphonic physical modeling synthesizer
- NEON-optimized DSP (ARM SIMD intrinsics)
- 48 kHz sample rate, stereo output
- 24 parameters across 6 pages (4 params per page)
- 28 internal presets

Many thanks to [Ice Moon Prison](https://github.com/futzle/logue-sdk/) for the inspiration on this.


## The original RipplerX readMe
RipplerX is a physically modeled synth, capable of sounds similar to AAS Chromaphone and Ableton Collision.
# Features
- Dual resonators with serial and parallel coupling.
- 9 Models of acoustic resonators: String, Beam, Squared, Membrane, Drumhead, Plate, Marimba, - Open tube, and Closed tube.
- Inharmonicity, Tone, Ratio, and Material sliders to shape the timbre.
- Noise and mallet generators.
- Up to 64 partials per resonator.


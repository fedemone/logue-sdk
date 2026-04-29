# RipplerX-Waveguide (Drumlogue Bare-Metal DSP)

## Overview
This project is a polyphonic Physical Modeling synthesizer designed natively for the Korg Drumlogue. It abandons desktop-VST object-oriented paradigms in favor of a strictly **Data-Oriented Design**. It uses fixed memory allocations, branchless mathematics, and ARM NEON SIMD optimization to perfectly respect the Drumlogue's ~20-microsecond RTOS audio deadline.

## Core Acoustic Logic (Karplus-Strong / Digital Waveguide)
Instead of additive sine-wave synthesis (which is mathematically expensive and prone to instability), this engine uses Digital Waveguides.
The physics of a string, tube, or membrane are simulated by injecting a short burst of energy (The Exciter) into a circular buffer (The Delay Line). The sound loops continuously, passing through a Loss Filter on every cycle. The Delay Length determines the pitch, and the Loss Filter determines the physical material.

## Signal Flow Architecture
```text
MIDI Note / Gate
       │
       ▼
┌────────────────────────────────────────────────────────┐
│ 1. EXCITER STAGE (The Strike)                          │
│   ├─► Noise Generator (Filtered burst)                 │
│   ├─► Mallet (Mathematical impulse)                    │
│   └─► PCM Sample (Safely bound-checked via OS)         │
└──────┬─────────────────────────────────────────────────┘
       │
       ├─────────────────────────┐ (A/B Split)
       │                         │
┌──────▼──────────────┐   ┌──────▼──────────────┐
│ 2A. RESONATOR A     │   │ 2B. RESONATOR B     │
│   ├─► Delay Line    │   │   ├─► Delay Line    │
│   ├─► Loss Filter   │   │   ├─► Loss Filter   │
│   └─► Fractional Hz │   │   └─► Fractional Hz │
└──────┬──────────────┘   └──────┬──────────────┘
       │                         │
       └───────────┬─────────────┘
                   │
┌──────────────────▼─────────────────────────────────────┐
│ 3. MASTER SHAPING                                      │
│   ├─► A/B Mixer                                        │
│   ├─► Brickwall Limiter (RTOS NaNs safety net)         │
└──────────────────┬─────────────────────────────────────┘
                   ▼
            TO DRUMLOGUE OS
```
                --------

## 2. Architectural Breakdown of the Missing Modules
Here is how we will build those missing pieces without losing our ultra-fast, bare-metal performance.

### A. Constants & Tables (constants.h / tables.h)
- Instead of calculating complex math on the fly, we use pre-calculated arrays (tables) stored in the Drumlogue's flash memory.

- Pitch-to-Frequency Table: Instead of calling powf() to convert MIDI notes to Hertz, we will have a simple const float mtof_table[128] array. Looking up a value in an array takes 1 CPU cycle.

- Constants: We will define c_sampleRate = 48000.0f, c_maxVoices = 4, and our UI boundary constants here so the whole project uses a single source of truth.

### B. The Envelope Generator (envelope.h)
- We need a lightweight Envelope to shape the Noise burst and act as a VCA (Voltage Controlled Amplifier) if the user wants the sound to choke immediately on GateOff.

- The Structure: A simple struct holding attack_rate, decay_rate, and current_level.

- The Logic: We use an exponential decay multiplier (e.g., current_level *= decay_rate). It requires zero if/else branches during the release phase, making it incredibly fast.

### C. The Filter (filter.h)
- We will use a Chamberlin State Variable Filter (SVF). It is highly efficient and provides Lowpass, Highpass, and Bandpass outputs simultaneously from the exact same calculation.

- We will use this to filter the Noise Exciter (so you can have low "thumps" or high "clicks" to strike the resonator).

- We will also put one at the very end of the signal chain as a Master Filter (mapped to header.c LowCut knob).

### D. The Waveguide Models (models.h)
- In old RipplerX VST, "Models" meant loading an array of 64 sine-wave ratios. In our new Waveguide engine, "Models" dictate the physical topology of the delay lines:

- String / Marimba: 1 Delay Line + Lowpass Filter + Dispersion (Allpass).

- Open Tube (Flute): 1 Delay Line + Inverting Feedback (multiplies by -1.0 every loop).

- Closed Tube (Clarinet): 1 Delay Line + Non-Inverting Feedback.

- Membrane (Drumhead): 2 Detuned Delay Lines running in parallel to simulate the chaotic 2D vibration of a drum skin.

- We will use c_parameterModel UI knob to simply swap out a few coefficients and routing paths inside process_waveguide().

### The Exciter Stage (Mallet & Noise)
To excite the passive waveguide resonators, the engine uses lightweight mathematical impulse generators.
- **The Mallet:** A heavily damped, single-cycle pulse simulating a physical strike.
- **The Noise:** A fast PRNG noise burst shaped by an AR (Attack-Release) envelope to simulate breath, bow friction, or snare wires.
Both are implemented as flat, inline C++ structs to minimize function-call overhead.

### Preset & UI Translation
The synthesizer reuses the legacy preset parameter arrays (Bells, Marimba, etc.). The 0-1000 UI ranges are caught by the `setParameter` function and mathematically translated into physical Waveguide coefficients (Feedback $g$, Filter Cutoff $H(z)$, Dispersion) in the control thread. This allows legacy UI configurations to seamlessly drive the new acoustic engine without modification.

---

## Reference / Literature / Inspiration Links

### Timbre analysis, similarity, and representation
- Timbre Models of Musical Sounds: From the Model of One Sound to the Model of One Instrument
  https://www.academia.edu/1051621/Timbre_models_of_musical_sounds_from_the_model_of_one_sound_to_the_model_of_one_instrument
- Aucouturier et al. (timbre representation context PDF mirror)
  https://www.francoispachet.fr/wp-content/uploads/2021/01/aucouturier-06a-1.pdf
- ISMIR 2019 paper reference used for descriptor/classification inspiration
  https://archives.ismir.net/ismir2019/paper/000091.pdf
- Brightness perception / spectral centroid relation (reference link)
  https://scispace.com/pdf/brightness-perception-for-musical-instrument-sounds-relation-13u09obfoq.pdf

### Damping / decay modeling
- Three decaying modes with equal and unequal energies and reverberation times
  https://www.researchgate.net/publication/371112063_Three_decaying_modes_with_equal_and_unequal_energies_and_reverberation_times
- Tonazzi et al. postprint (material linked in discussion)
  https://iris.uniroma1.it/retrieve/08f9d8c1-3060-409c-8997-817e882b8e13/Tonazzi_Postprint_Material_2024.pdf
- T20/T30/T60 measurement references shared during tuning discussion
  https://download.spsc.tugraz.at/thesis/PhD_Balint_20201203.pdf
  https://amslaurea.unibo.it/id/eprint/348/1/tesi_formattata.pdf

### Digital instrument modeling / discrete parametrization
- Discrete-time modelling of musical instruments
  https://www.researchgate.net/publication/228667658_Discrete-time_modelling_of_musical_instruments
- Dissertation reference shared for discrete model context
  http://lib.tkk.fi/Dipl/2007/urn009585.pdf
- Sensors article link shared for additional modeling context
  https://www.mdpi.com/1424-8220/25/11/3469
- Musical instrument recognition reference for discrete parametrization guidance
  https://www.nature.com/articles/s41598-025-09493-y

### Oscillator / recursion / filter coefficient references
- Harmonic quadrature oscillator recursion (Vicanek)
  https://vicanek.de/articles/QuadOsc.pdf
- Digital sine oscillator design notes
  https://www.njohnson.co.uk/pdf/drdes/Chap7.pdf
- Biquad and coefficient calculation references
  https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_10.pdf
  https://www.ti.com/lit/an/slaa447/slaa447.pdf
- Minimal sinusoidal oscillator implementation (VCII paper)
  https://www.mdpi.com/2079-9268/11/3/30

### Advanced mathematical modeling (exploratory)
- HAL preprint shared as thought-provoking modeling reference
  https://hal.science/hal-03178044v1

### Cymbal / gong modal modelling references used for Stage-2+ design
- Chaigne, C. & Doutaut, V. — Numerical simulations of xylophones and cymbals (plate modal context)
  https://ensta.hal.science/hal-01135295/file/ACCTOT.pdf
- Chaigne et al. / Touzé related plate-vibration nonlinear modal interaction reference
  https://perso.ensta.fr/~touze/PDF/ISMA04.pdf
- Chalmers publication (plate/cymbal vibro-acoustic modelling reference)
  https://publications.lib.chalmers.se/records/fulltext/5768.pdf
- Vibrating plates mode visualisation/intuition reference
  https://mdphys.org/plates.html
- Cymbal harmonics-informed design method
  https://ord.npust.edu.tw/wp-content/uploads/2023/07/Cymbals-with-Harmonics-Sound-a-Method-for-Design-the-Cymbals-and-Percussion-Instruments-with-Cymbals.pdf
- Acoustical Science and Technology article (cymbal/percussion acoustic analysis context)
  https://www.jstage.jst.go.jp/article/ast/42/6/42_E2087/_pdf/-char/en

### Snare-wire / resonant-noise & filter-complexity references
- Avnell Das thesis (snare/percussion synthesis and implementation context)
  https://www.diva-portal.org/smash/get/diva2:833643/FULLTEXT01.pdf
- University of Sydney review (drum/percussion modelling notes)
  https://ses.library.usyd.edu.au/bitstream/handle/2123/9178/Jarad%20Avnell%20Das%20Final%20Review.pdf?sequence=2&isAllowed=y
- IIR approximately-linear-phase complexity reference (for low-cost resonant shaping discussions)
  https://www.researchgate.net/publication/333784589_A_Complexity_Analysis_of_IIR_Filters_with_an_Approximately_Linear_Phase

### Non-physical / hybrid and broader modeling context
- Frontiers (2025) signal-processing reference shared for non-physical modelling context
  https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2025.1715792/full
- The NESS Project: Physical Modeling Algorithms and Sound Synthesis
  https://www.researchgate.net/publication/337399991_The_NESS_Project_Physical_Modeling_Algorithms_and_Sound_Synthesis
- CCRMA overview of sound modeling
  https://ccrma.stanford.edu/overview/modeling.html
- CCRMA CLM/physical-modeling tutorial notes
  https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/pm.html

### Additional collection / paywalled candidate
- AIP collection: Modeling of Musical Instruments
  https://pubs.aip.org/collection/1314/Modeling-of-Musical-Instruments
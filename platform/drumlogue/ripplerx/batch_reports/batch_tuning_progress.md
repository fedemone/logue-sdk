# Batch Tuning Progress

- Samples discovered: 63
- Samples mapped to presets: 58
- Pairs compared: 58
- Mean score: 93.638
- Target score: 12.0
- Assumed improvement/run: 0.72

- Auto note align: False

## Family pitch thresholds

- mallets: 10.00%
- membranes: 10.00%
- wood: 10.00%

## Family pitch summary

| Family | Count | f0% mean | f0% max | Target % | Met |
|---|---:|---:|---:|---:|:---:|
| mallets | 5 | 30.66 | 98.13 | 10.00 | no |
| membranes | 5 | 51.11 | 94.96 | 10.00 | no |
| other | 45 | 63.20 | 98.36 | n/a | n/a |
| wood | 3 | 60.84 | 88.10 | 10.00 | no |

## Unmapped samples

- ChaChaNut2.wav
- Crunch1.wav
- Guiro1.wav
- KnockFoley1.wav
- Metal-pipe-vibrating-B-minor.wav

## Preset results

### Kick (avg score 145.00, est runs 8)

- Sample `KickA-Hard-012-CloseRoom.wav` score `145.00`
  - Adjust Dkay around 55 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 350 by ~50 to align attack sharpness.
  - Shift Mterl around -5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-71.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### MrchSnr (avg score 143.65, est runs 8)

- Sample `Marching-Snare-Drum-A#-minor.wav` score `144.15`
  - Adjust Dkay around 130 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-52.66 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Metal-Pipe-Snare-E-major.wav` score `143.16`
  - Adjust Dkay around 130 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-36.80 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### AcSnre (avg score 136.97, est runs 8)

- Sample `acoustic-snare.wav` score `149.83`
  - Adjust Dkay around 78 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Adjust NzMix around 58 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-52.66 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `snare heavy.wav` score `124.12`
  - Adjust Dkay around 78 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-52.66 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Ac Tom (avg score 121.57, est runs 8)

- Sample `Tom2-004-CloseRoom.wav` score `121.59`
  - Adjust Dkay around 90 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-62.04 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tom1-001-CloseRoom.wav` score `121.55`
  - Adjust Dkay around 90 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-62.04 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Gong (avg score 118.22, est runs 8)

- Sample `Gong-long-G#.wav` score `123.77`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 80 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-55.04 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Chinese-Gong.wav` score `112.67`
  - Increase/decrease MlSt around 80 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-70.91 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Cymbal (avg score 116.21, est runs 7)

- Sample `CrashA-001-CloseRoom.wav` score `117.72`
  - Adjust Dkay around 182 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `cymbal-Crash16Inch.wav` score `114.70`
  - Adjust Dkay around 182 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Shaker (avg score 110.06, est runs 7)

- Sample `MaracasPair.wav` score `110.06`
  - Adjust Dkay around 12 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 370 by ~50 to align attack sharpness.
  - Shift Mterl around 10 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 90 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-17.16 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Clap (avg score 110.06, est runs 7)

- Sample `07_Clap_05_SP.wav` score `110.06`
  - Adjust Dkay around 15 by ±10..20 (current mismatch on decay is high).
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 95 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+64.59 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-C (avg score 106.28, est runs 7)

- Sample `HatClosedLive3.wav` score `114.73`
  - Adjust Dkay around 110 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 48 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `TightClosedHat.wav` score `97.84`
  - Adjust Dkay around 110 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 48 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Bongo (avg score 105.42, est runs 7)

- Sample `Bongo_Conga2.wav` score `105.42`
  - Adjust Dkay around 152 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 410 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-51.73 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-O (avg score 104.88, est runs 7)

- Sample `OpenHatBig.wav` score `104.88`
  - Adjust Dkay around 198 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 520 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 60 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+44.24 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Clrint (avg score 104.38, est runs 7)

- Sample `Clarinet-A-minor.wav` score `114.47`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+15.29 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Clarinet-C-minor.wav` score `94.29`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.

### Trngle (avg score 103.88, est runs 7)

- Sample `Triangle-Bell-C#.wav` score `110.03`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 440 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-36.57 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Triangle-Bell_F5.wav` score `97.74`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 440 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-7.06 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### RidBel (avg score 100.01, est runs 7)

- Sample `cymbal-RideBell20InchSabian.wav` score `100.01`
  - Adjust Dkay around 194 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 461 by ~50 to align attack sharpness.
  - Shift Mterl around 18 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-17.13 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Tick (avg score 95.94, est runs 7)

- Sample `high-church-clock-fx_100bpm.wav` score `100.16`
  - Adjust Dkay around 140 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 11 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-12.46 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav` score `93.94`
  - Adjust Dkay around 140 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-39.13 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `one-tic-clock.wav` score `93.73`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-40.63 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Claves (avg score 95.52, est runs 7)

- Sample `wetclave.wav` score `95.52`
  - Adjust Dkay around 13 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around -1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Timpni (avg score 94.86, est runs 7)

- Sample `Orchestral-Timpani-C.wav` score `94.86`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Adjust NzMix around 16 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-68.89 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### GlsBotl (avg score 92.96, est runs 7)

- Sample `GlassBottle.wav` score `95.19`
  - Adjust Dkay around 195 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (+0.98 st); check Note parameter and model/delay coupling.
- Sample `BottlePop1.wav` score `90.74`
  - Adjust Dkay around 195 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-20.71 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Djambe (avg score 91.93, est runs 7)

- Sample `Djambe-B3.wav` score `96.57`
  - Adjust Dkay around 102 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 350 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 7 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-34.82 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Djambe-A3.wav` score `87.30`
  - Adjust Dkay around 102 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 350 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 7 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-13.98 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Marmba (avg score 91.66, est runs 7)

- Sample `marimba-hit-c4_C_minor.wav` score `91.66`
  - Adjust Dkay around 194 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 130 by ~50 to align attack sharpness.
  - Shift Mterl around -7 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.

### Flute (avg score 84.79, est runs 7)

- Sample `Flute-A2.wav` score `87.80`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 132 by ~50 to align attack sharpness.
  - Shift Mterl around 22 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+15.72 st); check Note parameter and model/delay coupling.
- Sample `Flute-D3.wav` score `81.78`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 132 by ~50 to align attack sharpness.
  - Shift Mterl around 22 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### StelPan (avg score 83.88, est runs 7)

- Sample `steel-pan-Nova Drum Real C 432.wav` score `86.16`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `steel-pan-yudin C3.wav` score `86.06`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+12.00 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `steel-pan-PERCY-C4-SHort.wav` score `79.42`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Ride (avg score 82.73, est runs 6)

- Sample `cymbal-Ride18Inch.wav` score `82.73`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-70.91 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Cowbel (avg score 79.26, est runs 6)

- Sample `Cowbell_2.wav` score `79.26`
  - Increase/decrease MlSt around 420 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-13.98 st); check Note parameter and model/delay coupling.

### Vibrph (avg score 79.18, est runs 7)

- Sample `vibraphone_C_major1.wav` score `89.28`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `vibraphone_C_major.wav` score `69.07`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Koto (avg score 78.75, est runs 7)

- Sample `Koto-B5.wav` score `86.24`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-11.81 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-Stab-F#.wav` score `82.78`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+12.06 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-Pluck-C-Major.wav` score `67.22`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Taiko (avg score 76.83, est runs 6)

- Sample `Taiko-Hit.wav` score `76.83`
  - Adjust Dkay around 180 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 390 by ~50 to align attack sharpness.
  - Adjust NzMix around 14 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Kalimba (avg score 75.89, est runs 6)

- Sample `kalimba-e_E.wav` score `75.89`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 461 by ~50 to align attack sharpness.
  - Shift Mterl around 20 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).

### GlsBwl (avg score 72.89, est runs 6)

- Sample `glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav` score `72.89`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `glass-singing-bowl_23042017-01-raw-71015.wav` score `72.89`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Wodblk (avg score 70.96, est runs 6)

- Sample `WoodBlock1.wav` score `80.43`
  - Adjust Dkay around 156 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 24 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-33.00 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Woodblock.wav` score `61.49`
  - Adjust Dkay around 156 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-36.86 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### TblrBel (avg score 67.39, est runs 6)

- Sample `tubular-bells-phased.wav` score `73.08`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells.wav` score `71.48`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-23.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells-71571.wav` score `70.36`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Pitch mismatch detected (-24.29 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `phased-tubular-bell-87283.wav` score `61.01`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.
- Sample `tubular-bell-47849.wav` score `61.01`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.

### Conga (avg score 53.34, est runs 5)

- Sample `Bongo_Conga_Mute4.wav` score `53.34`
  - Increase/decrease MlSt around 365 by ~50 to align attack sharpness.
  - Shift Mterl around 3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Handpn (avg score 52.53, est runs 5)

- Sample `percussion-one-shot-tabla-3_C_major.wav` score `54.62`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 22 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+1.25 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tabla-Drum-Hit-D4_.wav` score `50.43`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+3.77 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.


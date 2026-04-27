# Batch Tuning Progress

- Samples discovered: 61
- Samples mapped to presets: 55
- Pairs compared: 55
- Mean score: 88.512
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
| mallets | 5 | 45.41 | 98.13 | 10.00 | no |
| membranes | 7 | 67.70 | 96.87 | 10.00 | no |
| other | 40 | 65.66 | 98.34 | n/a | n/a |
| wood | 3 | 60.84 | 88.10 | 10.00 | no |

## Unmapped samples

- ChaChaNut2.wav
- Crunch1.wav
- Guiro1.wav
- KnockFoley1.wav
- MaracasPair.wav
- Metal-pipe-vibrating-B-minor.wav

## Preset results

### Gong (avg score 122.17, est runs 8)

- Sample `Gong-long-G#.wav` score `133.41`
  - Adjust Dkay around 188 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 10 by ~50 to align attack sharpness.
  - Shift Mterl around -8 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 4 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-42.88 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Chinese-Gong.wav` score `110.94`
  - Increase/decrease MlSt around 10 by ~50 to align attack sharpness.
  - Shift Mterl around -8 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 4 by ±5..15 for noise/transient texture.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-58.75 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Bongo (avg score 119.10, est runs 7)

- Sample `Bongo_Conga2.wav` score `119.10`
  - Adjust Dkay around 94 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 457 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-51.73 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Clrint (avg score 118.28, est runs 8)

- Sample `Clarinet-A-minor.wav` score `123.69`
  - Adjust Dkay around 145 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 10 by ~50 to align attack sharpness.
  - Shift Mterl around -8 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+50.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Clarinet-C-minor.wav` score `112.86`
  - Adjust Dkay around 145 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 10 by ~50 to align attack sharpness.
  - Shift Mterl around -8 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+35.36 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Kick (avg score 116.17, est runs 7)

- Sample `KickA-Hard-012-CloseRoom.wav` score `116.17`
  - Adjust Dkay around 70 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 150 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 3 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-63.20 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Cymbal (avg score 112.40, est runs 7)

- Sample `CrashA-001-CloseRoom.wav` score `118.21`
  - Increase/decrease MlSt around 425 by ~50 to align attack sharpness.
  - Shift Mterl around 20 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `cymbal-Crash16Inch.wav` score `106.60`
  - Increase/decrease MlSt around 425 by ~50 to align attack sharpness.
  - Shift Mterl around 20 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.17 st); check Note parameter and model/delay coupling.

### Ac Tom (avg score 108.53, est runs 7)

- Sample `Tom2-004-CloseRoom.wav` score `109.68`
  - Adjust Dkay around 80 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 200 by ~50 to align attack sharpness.
  - Adjust NzMix around 2 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-54.28 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tom1-001-CloseRoom.wav` score `107.38`
  - Adjust Dkay around 80 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 200 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 2 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-54.28 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### MrchSnr (avg score 107.50, est runs 7)

- Sample `Marching-Snare-Drum-A#-minor.wav` score `110.93`
  - Adjust Dkay around 86 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around -1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 25 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Metal-Pipe-Snare-E-major.wav` score `104.07`
  - Adjust Dkay around 86 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around -1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 25 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-28.11 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Claves (avg score 99.61, est runs 7)

- Sample `wetclave.wav` score `99.61`
  - Adjust Dkay around 3 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### GlsBotl (avg score 98.72, est runs 7)

- Sample `GlassBottle.wav` score `99.98`
  - Adjust Dkay around 175 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+0.98 st); check Note parameter and model/delay coupling.
- Sample `BottlePop1.wav` score `97.47`
  - Adjust Dkay around 175 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-20.71 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Wodblk (avg score 94.76, est runs 7)

- Sample `Woodblock.wav` score `101.60`
  - Adjust Dkay around 82 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 430 by ~50 to align attack sharpness.
  - Shift Mterl around 9 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 18 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-36.86 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `WoodBlock1.wav` score `87.92`
  - Increase/decrease MlSt around 430 by ~50 to align attack sharpness.
  - Adjust NzMix around 18 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-33.00 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### AcSnre (avg score 94.18, est runs 7)

- Sample `acoustic-snare.wav` score `102.83`
  - Adjust Dkay around 15 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 200 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 40 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-13.69 st); check Note parameter and model/delay coupling.
- Sample `snare heavy.wav` score `85.54`
  - Adjust Dkay around 15 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 200 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 40 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-13.69 st); check Note parameter and model/delay coupling.

### Flute (avg score 93.42, est runs 7)

- Sample `Flute-A2.wav` score `97.01`
  - Adjust Dkay around 191 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 162 by ~50 to align attack sharpness.
  - Shift Mterl around -5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+15.72 st); check Note parameter and model/delay coupling.
- Sample `Flute-D3.wav` score `89.82`
  - Adjust Dkay around 191 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 162 by ~50 to align attack sharpness.
  - Shift Mterl around -5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Ride (avg score 93.10, est runs 7)

- Sample `cymbal-Ride18Inch.wav` score `93.10`
  - Increase/decrease MlSt around 491 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-70.91 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-C (avg score 90.55, est runs 7)

- Sample `HatClosedLive3.wav` score `90.58`
  - Adjust Dkay around 119 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 25 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 50 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `TightClosedHat.wav` score `90.53`
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 25 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 50 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-O (avg score 90.07, est runs 7)

- Sample `OpenHatBig.wav` score `90.07`
  - Adjust Dkay around 169 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 480 by ~50 to align attack sharpness.
  - Shift Mterl around 25 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 40 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+32.24 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### RidBel (avg score 88.95, est runs 7)

- Sample `cymbal-RideBell20InchSabian.wav` score `88.95`
  - Increase/decrease MlSt around 491 by ~50 to align attack sharpness.
  - Shift Mterl around 20 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-17.13 st); check Note parameter and model/delay coupling.

### Trngle (avg score 87.33, est runs 7)

- Sample `Triangle-Bell-C#.wav` score `99.53`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Triangle-Bell_F5.wav` score `75.13`
  - Adjust Dkay around 199 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-14.46 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Tick (avg score 86.39, est runs 7)

- Sample `one-tic-clock.wav` score `87.23`
  - Increase/decrease MlSt around 445 by ~50 to align attack sharpness.
  - Adjust NzMix around 29 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-28.78 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav` score `85.55`
  - Adjust Dkay around 100 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 445 by ~50 to align attack sharpness.
  - Adjust NzMix around 29 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-27.28 st); check Note parameter and model/delay coupling.

### StelPan (avg score 84.79, est runs 7)

- Sample `steel-pan-PERCY-C4-SHort.wav` score `87.95`
  - Adjust Dkay around 194 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 150 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `steel-pan-yudin C3.wav` score `83.28`
  - Increase/decrease MlSt around 150 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
- Sample `steel-pan-Nova Drum Real C 432.wav` score `83.14`
  - Increase/decrease MlSt around 150 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Djambe (avg score 83.61, est runs 7)

- Sample `Djambe-B3.wav` score `91.89`
  - Increase/decrease MlSt around 360 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-38.85 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `percussion-one-shot-tabla-3_C_major.wav` score `82.95`
  - Adjust Dkay around 107 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 360 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-16.89 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tabla-Drum-Hit-D4_.wav` score `80.31`
  - Adjust Dkay around 107 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 360 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-14.38 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Djambe-A3.wav` score `79.30`
  - Increase/decrease MlSt around 360 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-18.01 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Marmba (avg score 78.34, est runs 6)

- Sample `marimba-hit-c4_C_minor.wav` score `78.34`
  - Adjust Dkay around 184 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 130 by ~50 to align attack sharpness.
  - Shift Mterl around -9 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Timpni (avg score 76.77, est runs 6)

- Sample `Orchestral-Timpani-C.wav` score `76.77`
  - Increase/decrease MlSt around 440 by ~50 to align attack sharpness.
  - Shift Mterl around 1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 2 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-68.89 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### GlsBwl (avg score 73.04, est runs 6)

- Sample `glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav` score `73.04`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `glass-singing-bowl_23042017-01-raw-71015.wav` score `73.04`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Taiko (avg score 72.62, est runs 6)

- Sample `Taiko-Hit.wav` score `72.62`
  - Increase/decrease MlSt around 387 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.99 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Vibrph (avg score 71.30, est runs 6)

- Sample `vibraphone_C_major1.wav` score `71.47`
  - Adjust Dkay around 199 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+12.57 st); check Note parameter and model/delay coupling.
- Sample `vibraphone_C_major.wav` score `71.13`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+23.71 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### TblrBel (avg score 67.70, est runs 6)

- Sample `tubular-bells-71571.wav` score `74.60`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Pitch mismatch detected (-24.29 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells-phased.wav` score `71.53`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `high-church-clock-fx_100bpm.wav` score `69.85`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.48 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells.wav` score `69.78`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-23.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `phased-tubular-bell-87283.wav` score `60.23`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.
- Sample `tubular-bell-47849.wav` score `60.23`
  - Increase/decrease MlSt around 100 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.

### Conga (avg score 67.48, est runs 6)

- Sample `Bongo_Conga_Mute4.wav` score `67.48`
  - Increase/decrease MlSt around 350 by ~50 to align attack sharpness.
  - Adjust NzMix around 12 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Koto (avg score 64.71, est runs 6)

- Sample `Koto-B5.wav` score `66.62`
  - Increase/decrease MlSt around 395 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-11.81 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-Stab-F#.wav` score `63.76`
  - Increase/decrease MlSt around 395 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+12.06 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-Pluck-C-Major.wav` score `63.74`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 395 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Kalimba (avg score 59.61, est runs 5)

- Sample `kalimba-e_E.wav` score `59.61`
  - Increase/decrease MlSt around 491 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.


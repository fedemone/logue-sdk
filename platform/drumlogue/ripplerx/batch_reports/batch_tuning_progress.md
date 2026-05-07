# Batch Tuning Progress

- Samples discovered: 64
- Samples mapped to presets: 58
- Pairs compared: 58
- Mean score: 73.222
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
| mallets | 5 | 27.64 | 74.58 | 10.00 | no |
| membranes | 5 | 51.11 | 94.96 | 10.00 | no |
| other | 45 | 57.43 | 98.34 | n/a | n/a |
| wood | 3 | 60.84 | 88.10 | 10.00 | no |

## Unmapped samples

- ChaChaNut2.wav
- Crunch1.wav
- Guiro1.wav
- KnockFoley1.wav
- MaracasPair - Copia.wav
- Metal-pipe-vibrating-B-minor.wav

## Preset results

### MrchSnr (avg score 163.93, est runs 9)

- Sample `Marching-Snare-Drum-A#-minor.wav` score `174.95`
  - Adjust Dkay around 130 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Metal-Pipe-Snare-E-major.wav` score `152.90`
  - Adjust Dkay around 130 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 12 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-28.11 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### AcSnre (avg score 146.24, est runs 8)

- Sample `acoustic-snare.wav` score `161.10`
  - Adjust Dkay around 78 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 58 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-17.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `snare heavy.wav` score `131.37`
  - Adjust Dkay around 78 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 58 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-17.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Kick (avg score 122.89, est runs 8)

- Sample `KickA-Hard-012-CloseRoom.wav` score `122.89`
  - Adjust Dkay around 55 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 120 by ~50 to align attack sharpness.
  - Shift Mterl around -5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.

### Ac Tom (avg score 119.21, est runs 8)

- Sample `Tom2-004-CloseRoom.wav` score `120.85`
  - Adjust Dkay around 90 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-62.28 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tom1-001-CloseRoom.wav` score `117.58`
  - Adjust Dkay around 90 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-62.28 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Clap (avg score 118.14, est runs 7)

- Sample `07_Clap_05_SP.wav` score `118.14`
  - Adjust Dkay around 15 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 270 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 95 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+52.59 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Cymbal (avg score 116.76, est runs 8)

- Sample `CrashA-001-CloseRoom.wav` score `122.57`
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `cymbal-Crash16Inch.wav` score `110.94`
  - Adjust Dkay around 182 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.17 st); check Note parameter and model/delay coupling.

### Shaker (avg score 112.35, est runs 7)

- Sample `MaracasPair.wav` score `112.35`
  - Adjust Dkay around 12 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 370 by ~50 to align attack sharpness.
  - Shift Mterl around 10 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 90 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-10.53 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Bongo (avg score 102.10, est runs 7)

- Sample `Bongo_Conga2.wav` score `102.10`
  - Adjust Dkay around 152 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 410 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-51.73 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Clrint (avg score 98.19, est runs 7)

- Sample `Clarinet-A-minor.wav` score `103.84`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+15.29 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Clarinet-C-minor.wav` score `92.54`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 8 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.

### Claves (avg score 92.54, est runs 7)

- Sample `wetclave.wav` score `92.54`
  - Adjust Dkay around 13 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around -1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Flute (avg score 90.56, est runs 7)

- Sample `Flute-A2.wav` score `94.43`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 132 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+15.72 st); check Note parameter and model/delay coupling.
- Sample `Flute-D3.wav` score `86.69`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 132 by ~50 to align attack sharpness.
  - Shift Mterl around -3 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Djambe (avg score 87.43, est runs 7)

- Sample `Djambe-B3.wav` score `91.25`
  - Adjust Dkay around 102 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 350 by ~50 to align attack sharpness.
  - Shift Mterl around 0 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 7 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-34.82 st); check Note parameter and model/delay coupling.
- Sample `Djambe-A3.wav` score `83.61`
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

### Cowbel (avg score 87.17, est runs 7)

- Sample `Cowbell_2.wav` score `87.17`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 420 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-7.10 st); check Note parameter and model/delay coupling.

### GlsBotl (avg score 85.83, est runs 7)

- Sample `GlassBottle.wav` score `88.05`
  - Adjust Dkay around 195 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+0.98 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `BottlePop1.wav` score `83.62`
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 5 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 45 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-20.71 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Taiko (avg score 76.80, est runs 6)

- Sample `Taiko-Hit.wav` score `76.80`
  - Adjust Dkay around 180 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 390 by ~50 to align attack sharpness.
  - Shift Mterl around 4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 14 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### GlsBwl (avg score 72.67, est runs 6)

- Sample `glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav` score `72.67`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `glass-singing-bowl_23042017-01-raw-71015.wav` score `72.67`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+35.26 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### StelPan (avg score 70.67, est runs 6)

- Sample `steel-pan-Nova Drum Real C 432.wav` score `75.92`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `steel-pan-yudin C3.wav` score `72.65`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Pitch mismatch detected (+12.00 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `steel-pan-PERCY-C4-SHort.wav` score `63.43`
  - Increase/decrease MlSt around 0 by ~50 to align attack sharpness.
  - Shift Mterl around 14 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Vibrph (avg score 69.65, est runs 6)

- Sample `vibraphone_C_major1.wav` score `71.47`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+12.57 st); check Note parameter and model/delay coupling.
- Sample `vibraphone_C_major.wav` score `67.82`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+23.71 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Gong (avg score 68.95, est runs 6)

- Sample `Gong-long-G#.wav` score `69.11`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 80 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-42.88 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Chinese-Gong.wav` score `68.79`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 80 by ~50 to align attack sharpness.
  - Shift Mterl around -4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-58.75 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Conga (avg score 63.81, est runs 6)

- Sample `Bongo_Conga_Mute4.wav` score `63.81`
  - Increase/decrease MlSt around 365 by ~50 to align attack sharpness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Koto (avg score 62.29, est runs 6)

- Sample `Koto-Pluck-C-Major.wav` score `66.56`
  - Adjust Dkay around 185 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-B5.wav` score `63.21`
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-11.81 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Koto-Stab-F#.wav` score `57.11`
  - Increase/decrease MlSt around 335 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+12.06 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Wodblk (avg score 61.69, est runs 6)

- Sample `Woodblock.wav` score `64.24`
  - Adjust Dkay around 156 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-36.86 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `WoodBlock1.wav` score `59.14`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-33.00 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Marmba (avg score 56.85, est runs 5)

- Sample `marimba-hit-c4_C_minor.wav` score `56.85`
  - Adjust Dkay around 194 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 130 by ~50 to align attack sharpness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-O (avg score 53.88, est runs 5)

- Sample `OpenHatBig.wav` score `53.88`
  - Adjust Dkay around 198 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 520 by ~50 to align attack sharpness.
  - Shift Mterl around 26 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 60 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+44.24 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Timpni (avg score 52.52, est runs 5)

- Sample `Orchestral-Timpani-C.wav` score `52.52`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Shift Mterl around -2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 16 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.

### Handpn (avg score 52.47, est runs 5)

- Sample `percussion-one-shot-tabla-3_C_major.wav` score `53.72`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (+1.25 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Tabla-Drum-Hit-D4_.wav` score `51.22`
  - Increase/decrease MlSt around 300 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Pitch mismatch detected (+3.77 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### HHat-C (avg score 51.50, est runs 5)

- Sample `HatClosedLive3.wav` score `52.37`
  - Adjust Dkay around 110 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 48 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `TightClosedHat.wav` score `50.63`
  - Adjust Dkay around 110 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 26 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 48 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-29.55 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Ride (avg score 50.44, est runs 5)

- Sample `cymbal-Ride18Inch.wav` score `50.44`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 15 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-70.91 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Kalimba (avg score 46.32, est runs 5)

- Sample `kalimba-e_E.wav` score `46.32`
  - Increase/decrease MlSt around 461 by ~50 to align attack sharpness.
  - Shift Mterl around 10 by ±3..8 to rebalance brightness.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Tick (avg score 41.08, est runs 5)

- Sample `one-tic-clock.wav` score `53.43`
  - Adjust Dkay around 140 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-28.78 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav` score `37.55`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.28 st); check Note parameter and model/delay coupling.
- Sample `high-church-clock-fx_100bpm.wav` score `32.25`
  - Adjust Dkay around 140 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 11 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 24 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Trngle (avg score 25.94, est runs 4)

- Sample `Triangle-Bell-C#.wav` score `42.82`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 440 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Triangle-Bell_F5.wav` score `9.05`
  - Adjust Dkay around 190 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 440 by ~50 to align attack sharpness.
  - Shift Mterl around 2 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 0 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-14.46 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### RidBel (avg score 15.44, est runs 1)

- Sample `cymbal-RideBell20InchSabian.wav` score `15.44`
  - Increase/decrease MlSt around 461 by ~50 to align attack sharpness.
  - Shift Mterl around 18 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 20 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-17.13 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### TblrBel (avg score 10.30, est runs 2)

- Sample `tubular-bells.wav` score `17.90`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-23.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells-phased.wav` score `16.18`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Pitch mismatch detected (-11.72 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `tubular-bells-71571.wav` score `12.06`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Pitch mismatch detected (-24.29 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `phased-tubular-bell-87283.wav` score `2.68`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.
- Sample `tubular-bell-47849.wav` score `2.68`
  - Increase/decrease MlSt around 340 by ~50 to align attack sharpness.
  - Adjust NzMix around 5 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (+7.11 st); check Note parameter and model/delay coupling.


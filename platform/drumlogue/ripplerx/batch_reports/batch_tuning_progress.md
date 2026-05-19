# Batch Tuning Progress

- Samples discovered: 63
- Samples mapped to presets: 10
- Pairs compared: 10
- Mean score: 94.803
- Target score: 12.0
- Assumed improvement/run: 0.72

- Auto note align: False

## Acceptance-state summary

- tunable_in_scope: 10
- architecture_backlog: 0
- out_of_scope_trace: 0

## Family pitch thresholds

- mallets: 10.00%
- membranes: 10.00%
- wood: 10.00%

## Family pitch summary

| Family | Count | f0% mean | f0% max | Target % | Met |
|---|---:|---:|---:|---:|:---:|
| other | 10 | 70.98 | 98.34 | n/a | n/a |

## Unmapped samples

- ChaChaNut2.wav
- Crunch1.wav
- Guiro1.wav
- KnockFoley1.wav
- Metal-pipe-vibrating-B-minor.wav
- Tom1-001-CloseRoom.wav
- Tom2-004-CloseRoom.wav

## Preset results

### Cymbal (avg score 104.92, est runs 7)

- Sample `CrashA-001-CloseRoom.wav` score `107.69`
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 13 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `cymbal-Crash16Inch.wav` score `102.15`
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 13 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### MrchSnr (avg score 104.27, est runs 7)

- Sample `Marching-Snare-Drum-A#-minor.wav` score `104.73`
  - Shift Mterl around 4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 30 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Metal-Pipe-Snare-E-major.wav` score `103.82`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 4 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 30 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-28.11 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Gong (avg score 101.52, est runs 7)

- Sample `Chinese-Gong.wav` score `103.59`
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Shift Mterl around 1 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 19 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-70.91 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Gong-long-G#.wav` score `99.45`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 50 by ~50 to align attack sharpness.
  - Adjust NzMix around 19 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-55.04 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### Trngle (avg score 81.72, est runs 7)

- Sample `Triangle-Bell-C#.wav` score `94.52`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-43.97 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `Triangle-Bell_F5.wav` score `68.91`
  - Increase/decrease MlSt around 500 by ~50 to align attack sharpness.
  - Shift Mterl around 16 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 10 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-14.46 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.

### AcSnre (avg score 81.58, est runs 6)

- Sample `acoustic-snare.wav` score `84.86`
  - Adjust Dkay around 168 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Shift Mterl around -7 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 61 by ±5..15 for noise/transient texture.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.
- Sample `snare heavy.wav` score `78.30`
  - Adjust Dkay around 168 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 280 by ~50 to align attack sharpness.
  - Shift Mterl around -7 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 61 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-1.69 st); check Note parameter and model/delay coupling.


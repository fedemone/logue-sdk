# Batch Tuning Progress

- Samples discovered: 63
- Samples mapped to presets: 2
- Pairs compared: 2
- Mean score: 116.212
- Target score: 12.0
- Assumed improvement/run: 0.72

- Auto note align: False

## Acceptance-state summary

- tunable_in_scope: 2
- architecture_backlog: 0
- out_of_scope_trace: 0

## Family pitch thresholds

- mallets: 10.00%
- membranes: 10.00%
- wood: 10.00%

## Family pitch summary

| Family | Count | f0% mean | f0% max | Target % | Met |
|---|---:|---:|---:|---:|:---:|
| other | 2 | 87.95 | 96.72 | n/a | n/a |

## Unmapped samples

- ChaChaNut2.wav
- Crunch1.wav
- Guiro1.wav
- KnockFoley1.wav
- Metal-pipe-vibrating-B-minor.wav
- Tom1-001-CloseRoom.wav
- Tom2-004-CloseRoom.wav

## Preset results

### Cymbal (avg score 116.21, est runs 7)

- Sample `CrashA-001-CloseRoom.wav` score `117.72`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 40 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Pitch mismatch detected (-59.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.
- Sample `cymbal-Crash16Inch.wav` score `114.70`
  - Adjust Dkay around 200 by ±10..20 (current mismatch on decay is high).
  - Increase/decrease MlSt around 450 by ~50 to align attack sharpness.
  - Shift Mterl around 28 by ±3..8 to rebalance brightness.
  - Adjust NzMix around 40 by ±5..15 for noise/transient texture.
  - Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.
  - Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).
  - Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.
  - Pitch mismatch detected (-27.17 st); check Note parameter and model/delay coupling.
  - Inharmonicity mismatch; try modest InHm and/or model change.


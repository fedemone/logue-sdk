#!/usr/bin/env python3
"""
batch_tune_runner.py

Batch pre-HW runner for RipplerX sample matching.

Pipeline:
1) Discover reference samples.
2) Map sample -> preset using keyword matching (+ manual overrides).
3) (Optional) Render mapped presets with existing render_presets binary.
4) Compare rendered vs reference via pre_hw_analysis.
5) Produce tuning hints + estimated number of iteration runs to converge.

Quick start:
  python3 batch_tune_runner.py --helper
  python3 batch_tune_runner.py --write-helper batch_tuning_helper.md
  python3 batch_tune_runner.py --run-render --render-cmd "qemu-arm ./run_test_render --preset {preset_idx} --note {note} --name {preset_name} --out {output_wav}"
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import shlex
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

from pre_hw_analysis import compare_pair, read_wav_mono, autocorr_f0


REPO_DIR = Path(__file__).resolve().parent
SAMPLES_DIR = REPO_DIR / "samples"
SYNTH_ENGINE = REPO_DIR / "synth_engine.h"
DEFAULT_RENDER_DIR = REPO_DIR / "rendered_batch"
DEFAULT_OUT_DIR = REPO_DIR / "batch_reports"


# Manual coupling for ambiguous names / aliases.
MANUAL_SAMPLE_TO_PRESET = {
    "Orchestral-Timpani-C.wav": "Timpani",
    "Marching-Snare-Drum-A#-minor.wav": "MrchSnr",
    "Woodblock.wav": "Wodblk",
    "WoodBlock1.wav": "Wodblk",
    "cymbal-Crash16Inch.wav": "Cymbal",
    "cymbal-Ride18Inch.wav": "Ride",
    "cymbal-RideBell20InchSabian.wav": "RidBel",
    # User correction: clock sample is Tick, not TblrBel
    "high-church-clock-fx_100bpm.wav": "Tick",
    "one-tic-clock.wav": "Tick",
    "ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav": "Tick",
    # NOTE (cross-check): user says Conga→Bongo_Conga2, Bongo→Bongo_Conga_Mute4
    # Current mapping is reversed (AI judgment: muted tone better matches conga character)
    "Bongo_Conga2.wav": "Bongo",
    "Bongo_Conga_Mute4.wav": "Conga",
    "GlassBottle.wav": "GlsBotl",
    "steel-pan-Nova Drum Real C 432.wav": "StelPan",
    "steel-pan-PERCY-C4-SHort.wav": "StelPan",
    "steel-pan-yudin C3.wav": "StelPan",
    "Koto-B5.wav": "Koto",
    "Koto-Pluck-C-Major.wav": "Koto",
    # User correction: tabla samples → Handpn (not Djambe)
    "Tabla-Drum-Hit-D4_.wav": "Handpn",
    "percussion-one-shot-tabla-3_C_major.wav": "Handpn",
    # New samples added after initial batch run
    "CrashA-001-CloseRoom.wav": "Cymbal",
    "KickA-Hard-012-CloseRoom.wav": "Kick",
    "Tom1-001-CloseRoom.wav": "AcTom",
    "Tom2-004-CloseRoom.wav": "AcTom",
    "acoustic-snare.wav": "AcSnre",
    "snare heavy.wav": "AcSnre",
    "vibraphone_C_major.wav": "Vibrph",
    "vibraphone_C_major1.wav": "Vibrph",
    "wetclave.wav": "Claves",
    "percussion-clave-like-hit-107112.mp3": "Claves",
    # Previously unmapped samples with known targets
    "Triangle-Bell-C#.wav": "Trngle",
    "Triangle-Bell_F5.wav": "Trngle",
    "glass-bowl-e-flat-tibetan-singing-bowl-struck-38746.wav": "GlsBwl",
    "glass-singing-bowl_23042017-01-raw-71015.wav": "GlsBwl",
    "marimba-hit-c4_C_minor.wav": "Marmba",
    # NOTE (cross-check): user says HHat-C→HatClosedLive3+OpenHatBig, HHat-O→TightClosedHat
    # Current mapping: HHat-C→closed hats (HatClosedLive3+TightClosedHat), HHat-O→OpenHatBig
    "TightClosedHat.wav": "HHat-C",
    "BottlePop1.wav": "GlsBotl",
    # Clarinet sample (user correction: was unmapped after TamTam removal)
    "Clarinet-A-minor.wav": "Clrint",
    # Maracas → Shaker
    "MaracasPair.wav": "Shaker",
}

KEYWORD_TO_PRESET = {
    "timpani": "Timpani",
    "marimba": "Marimba",
    "kalimba": "Kalimba",
    "koto": "Koto",
    "gong": "Gong",
    "djambe": "Djambe",
    "tabla": "Djambe",
    "taiko": "Taiko",
    "snare": "MrchSnr",
    "wood": "Wodblk",
    "flute": "Flute",
    "clarinet": "Clrint",
    "cymbal": "Cymbal",
    "ridebell": "RidBel",
    "ride": "Ride",
    "tubular": "TblrBel",
    "hihat": "HHat-C",
    "hatclosed": "HHat-C",
    "openhat": "HHat-O",
    "bongo": "Bongo",
    "conga": "Conga",
    "glassbottle": "GlsBotl",
    "tick": "Tick",
    "bottle": "GlsBotl",
}

# Canonical -> current synth table aliases/typos.
PRESET_NAME_ALIASES = {
    "Marimba": "Marmba",
    "Timpani": "Timpni",
    "AcSnare": "AcSnre",
    "AcTom": "Ac Tom",
}

# Render file aliases used by render_presets.cpp naming convention.
RENDER_PRESET_NAMES = {
    "InitDbg": "InitDbg",
    "Marimba": "Marimba",
    "808Sub": "808Sub",
    "AcSnare": "AcSnre",
    "TblrBel": "TblrBel",
    "Timpani": "Timpani",
    "Djambe": "Djambe",
    "Taiko": "Taiko",
    "MrchSnr": "MrchSnr",
    "Koto": "Koto",
    "Vibrph": "Vibrph",
    "Wodblk": "Wodblk",
    "AcTom": "AcTom",
    "Cymbal": "Cymbal",
    "Gong": "Gong",
    "Kalimba": "Kalimba",
    "StelPan": "StelPan",
    "Claves": "Claves",
    "Cowbel": "Cowbel",
    "Triangle": "Triangle",
    "Kick": "Kick",
    "Clap": "Clap",
    "Shaker": "Shaker",
    "Flute": "Flute",
    "Clarinet": "Clrint",
    "PlkBss": "PlkBss",
    "GlsBwl": "GlsBwl",
    "GtrStr": "GtrStr",
    "HHat-C": "HHat-C",
    "HHat-O": "HHat-O",
    "Conga": "Conga",
    "Handpn": "Handpn",
    "BelTre": "BelTre",
    "SltDrm": "SltDrm",
    "Ride": "Ride",
    "RidBel": "RidBel",
    "Bongo": "Bongo",
    "GlsBotl": "GlsBotl",
    "Tick": "Tick",
}

PERCUSSIVE_PRESETS = {
    "AcSnre", "Timpani", "Djambe", "Taiko", "MrchSnr", "Wodblk",
    "Ac Tom", "AcTom", "Cymbal", "Gong", "Claves", "Cowbel", "Triangle", "Kick",
    "Clap", "Shaker", "HHat-C", "HHat-O", "Conga", "SltDrm", "Ride", "RidBel", "Bongo", "Tick",
}

KICK_PRESETS = {"Kick", "808Sub"}
SNARE_PRESETS = {"AcSnre", "MrchSnr"}
TOM_PRESETS = {"Ac Tom", "AcTom", "Conga", "Bongo", "Taiko", "Djambe"}
HIHAT_PRESETS = {"HHat-C", "HHat-O"}
CYMBAL_PRESETS = {"Cymbl", "Ride", "RidBel"}
GONG_PRESETS = {"Gong"}
PERC_TEXTURE_PRESETS = {"Shaker", "Clap", "Handpn"}

STYLE_PROFILES: Dict[str, Dict[str, float]] = {
    # Open-hat windows in ms, snare crack center in Hz.
    "electronic": {"hhat_open_min_ms": 100.0, "hhat_open_max_ms": 500.0, "hhat_closed_max_ms": 500.0, "snare_crack_hz": 3200.0},
    "trap": {"hhat_open_min_ms": 100.0, "hhat_open_max_ms": 200.0, "hhat_closed_max_ms": 250.0, "snare_crack_hz": 3800.0},
    "house": {"hhat_open_min_ms": 180.0, "hhat_open_max_ms": 320.0, "hhat_closed_max_ms": 420.0, "snare_crack_hz": 3200.0},
    "rock": {"hhat_open_min_ms": 300.0, "hhat_open_max_ms": 550.0, "hhat_closed_max_ms": 520.0, "snare_crack_hz": 3000.0},
    "metal": {"hhat_open_min_ms": 160.0, "hhat_open_max_ms": 360.0, "hhat_closed_max_ms": 350.0, "snare_crack_hz": 4200.0},
    "funk": {"hhat_open_min_ms": 140.0, "hhat_open_max_ms": 280.0, "hhat_closed_max_ms": 320.0, "snare_crack_hz": 5000.0},
    "jazz": {"hhat_open_min_ms": 220.0, "hhat_open_max_ms": 520.0, "hhat_closed_max_ms": 520.0, "snare_crack_hz": 2600.0},
}
UNPITCHED_FOCUS_PRESETS = {
    # First-batch focus where autocorrelation pitch is frequently unstable and
    # should not dominate acceptance.
    "Taiko", "Bongo", "Wodblk", "Conga", "Djambe",
}

PRESET_TO_FAMILY = {
    # membranes
    "Bongo": "membranes",
    "Conga": "membranes",
    "Djambe": "membranes",
    "Taiko": "membranes",
    # mallets
    "Marimba": "mallets",
    "Marmba": "mallets",  # table alias
    "Kalimba": "mallets",
    "Vibrph": "mallets",
    "Timpani": "mallets",
    "Timpni": "mallets",  # table alias
    # wood / wood blocks
    "Wodblk": "wood",
    "Claves": "wood",
}

DEFAULT_FAMILY_PITCH_THRESHOLDS = {
    "membranes": 10.0,
    "mallets": 10.0,
    "wood": 10.0,
}

PRESET_GROUPS = {
    "first_batch": ["Djambe", "Taiko", "Bongo", "Conga", "Marimba", "Kalimba", "Wodblk", "Timpani"],
    "remaining_batch": [
        "Clrint", "Cymbal", "Flute", "GlsBotl", "Gong", "HHat-C", "HHat-O", "Koto",
        "MrchSnr", "Ride", "RidBel", "StelPan", "TblrBel", "Tick", "Timpani",
    ],
}


@dataclass
class PresetRow:
    idx: int
    name: str
    values: List[int]


def build_helper_text() -> str:
    return f"""# RipplerX Batch Tuning Helper

This helper describes how to run the full pre-HW loop with `batch_tune_runner.py`.

## 1) Goal

Converge rendered preset audio toward reference sample audio by iterating:

1. render preset audio (strike excitation, no sample injection),
2. compare rendered vs references,
3. apply parameter tuning,
4. repeat until score reaches target.

## 2) Main command

From `{REPO_DIR}`:

```bash
python3 batch_tune_runner.py \\
  --render-dir rendered_batch \\
  --out-dir batch_reports \\
  --iterations 5 \\
  --target-score 12 \\
  --assumed-improvement 0.72
```

If an ARM renderer is available:

```bash
python3 batch_tune_runner.py \\
  --run-render \\
  --render-cmd "qemu-arm ./run_test_render --preset {{preset_idx}} --note {{note}} --name {{preset_name}} --out {{output_wav}}" \\
  --render-dir rendered_batch \\
  --out-dir batch_reports
```

## 3) WSL build/run notes (as provided by user)

Use this on WSL to build ARM render binary:

```bash
mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/bin/arm-unknown-linux-gnueabihf-g++ -static -std=c++17 -O3 -I.. -I /mnt/d/Fede/drumlogue/arm-unknown-linux-gnueabihf/arm-unknown-linux-gnueabihf test_ripplerx_render.cpp -o run_test_render
```

Run it with QEMU:

```bash
qemu-arm ./run_test_render --preset 12 --name Wodblk --out rendered_batch/12_Wodblk.wav
```

## 4) Outputs generated

- `batch_tuning_report.json`: complete metrics + suggested actions.
- `batch_tuning_report.csv`: sortable table for quick triage.
- `batch_tuning_progress.md`: per-preset tuning notes and run estimates.

## 5) How samples are mapped

Sample names are matched to presets by:

1. manual overrides for ambiguous filenames,
2. keyword mapping (e.g. \"timpani\", \"tabla\", \"ridebell\"),
3. fallback fuzzy match to preset names.

Unmapped samples are listed in the report for manual review.

Note: analysis reports both raw and pitch-normalized spectral deltas so
different sample note vs rendered note is partially compensated.

## 6) Convergence estimate

Estimated runs-to-target use:

`score_next = score_now * assumed_improvement`

So expected runs:

`n = ceil(log(target/current) / log(assumed_improvement))`

Tune `--target-score` and `--assumed-improvement` as empirical history grows.

## 7) Suggested loop

1. Run batch report.
2. Inspect worst-scoring preset/sample pairs.
3. Apply small parameter edits (`Dkay`, `MlSt`, `Mterl`, `NzMix`, optionally `InHm`).
4. Re-render and re-run batch.
5. Track score trend in `batch_tuning_progress.md`.
6. Flash only when pre-HW score trend is clearly improving.
7. For repeated passes in one command, use `--iterations N`; a
   `batch_tuning_history.json/.md` trend summary is generated.

Tip: pass `--auto-note-align` when sample note and rendered note may differ.
Use `--preset-group first_batch` to keep focusing on the initial gate set.
Use `--preset-group remaining_batch` to run the next staged scope.
"""


def parse_presets(path: Path) -> Dict[str, PresetRow]:
    txt = path.read_text()
    # Extract idx->name mapping from getPresetName array.
    names_block = re.search(r"static inline const char \* getPresetName\(uint8_t idx\).*?\{(.*?)\};", txt, re.S)
    names = []
    if names_block:
        for token in re.findall(r'"([^"]+)"', names_block.group(1)):
            names.append(token)

    table = re.search(r"static const int32_t presets\[k_NumPrograms\]\[k_lastParamIndex\]\s*=\s*\{(.*?)\n\s*\};", txt, re.S)
    if not table:
        raise RuntimeError("Could not parse presets table")

    rows: Dict[str, PresetRow] = {}
    for line in table.group(1).splitlines():
        m = re.search(r"\{([^}]+)\}", line)
        if not m:
            continue
        values = [int(x.strip()) for x in m.group(1).split(",") if x.strip()]
        if len(values) < 24:
            continue
        idx = values[0]
        n = names[idx] if idx < len(names) else f"Preset{idx}"
        rows[n] = PresetRow(idx=idx, name=n, values=values)
    return rows


def normalize_key(s: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", s.lower())


NOTE_TO_SEMITONE = {
    "c": 0, "c#": 1, "db": 1,
    "d": 2, "d#": 3, "eb": 3,
    "e": 4,
    "f": 5, "f#": 6, "gb": 6,
    "g": 7, "g#": 8, "ab": 8,
    "a": 9, "a#": 10, "bb": 10,
    "b": 11,
}

# Explicit note overrides for ambiguous/number-heavy filenames.
# Prevents accidental parsing like "tabla-3" -> A-3.
SAMPLE_NOTE_OVERRIDES = {
    "Djambe-A3.wav": 57,
    "Djambe-B3.wav": 59,
    "Tabla-Drum-Hit-D4_.wav": 62,
    "percussion-one-shot-tabla-3_C_major.wav": 60,
    "marimba-hit-c4_C_minor.wav": 60,
    "kalimba-e_E.wav": 64,
    "Taiko-Hit.wav": 60,
    "Bongo_Conga2.wav": 45,
    "Bongo_Conga_Mute4.wav": 45,
    "Woodblock.wav": 60,
    "WoodBlock1.wav": 60,
}

# Deterministic defaults for noisy/percussive classes where audio f0 detection
# is often unstable and harms timbre-only optimization.
PRESET_RENDER_NOTE_DEFAULTS = {
    "AcSnre": 38,
    "Kick": 36,
    "HHat-C": 79,
    "HHat-O": 79,
    "Clap": 60,
    "Shaker": 72,
    "Timpni": 40,
    "Ac Tom": 45,
}

# Provisional per-preset pitch calibration in semitones, applied after note
# inference/overrides. Keep conservative and only for pitched presets.
PRESET_NOTE_CALIBRATION = {
    "Marmba": 12,
    "Marimba": 12,
    "Kalimba": 12,
    # First-batch pilot: only retain offsets that improved aggregate score.
    "Djambe": -12,
    "Conga": 19,
    # Remaining-batch pilot calibration (Phase 30): reduce large persistent
    # note offsets before applying deeper preset retuning.
    "Gong": 12,
    "MrchSnr": 24,
}


def midi_from_note_name(note: str, octave: int) -> int:
    key = note.lower()
    if key not in NOTE_TO_SEMITONE:
        return 60
    midi = 12 * (octave + 1) + NOTE_TO_SEMITONE[key]
    return max(0, min(127, midi))


def infer_midi_note_from_sample_name(sample: Path, fallback: int = 60) -> int:
    """Best-effort pitch extraction from filename tokens (e.g. B3, C#4, Eb2)."""
    if sample.name in SAMPLE_NOTE_OVERRIDES:
        return SAMPLE_NOTE_OVERRIDES[sample.name]
    stem = sample.stem
    m = re.search(r"(?<![A-Za-z0-9])([A-Ga-g])([#b]?)[ _-]?(-?\d)(?![A-Za-z0-9])", stem)
    if not m:
        m = re.search(r"([A-Ga-g])([#b]?)(-?\d)", stem)
    if not m:
        return fallback
    note = f"{m.group(1)}{m.group(2)}"
    octave = int(m.group(3))
    return midi_from_note_name(note, octave)


def midi_from_hz(hz: float, fallback: int = 60) -> int:
    if hz <= 0.0:
        return fallback
    midi = int(round(69.0 + 12.0 * math.log2(hz / 440.0)))
    return max(0, min(127, midi))


def infer_midi_note_from_audio(sample: Path, fallback: int = 60, fmin: float = 35.0, fmax: float = 4000.0) -> int:
    """Infer pitch from waveform autocorrelation (useful when filename has no note token)."""
    try:
        sig, sr, _ = read_wav_mono(sample)
        if not sig:
            return fallback
        early = sig[: min(len(sig), int(sr * 0.35))]
        f0 = autocorr_f0(early, sr, fmin=fmin, fmax=fmax)
        return midi_from_hz(f0, fallback=fallback)
    except Exception:
        return fallback




def pitch_search_range_for_preset(preset_name: str) -> Tuple[float, float]:
    if preset_name in {"HHat-C", "HHat-O", "Ride", "RideBll", "Cymbl"}:
        return (400.0, 16000.0)
    if preset_name in {"Kick", "Timpni", "AcSnre", "Ac", "Taiko", "Djmb", "MrchSnr"}:
        return (20.0, 2500.0)
    return (35.0, 5000.0)
def apply_preset_note_calibration(preset_name: str, note: int) -> int:
    offset = PRESET_NOTE_CALIBRATION.get(preset_name, 0)
    # Safety clamp for accidental over-calibration.
    offset = max(-24, min(24, int(offset)))
    return max(0, min(127, note + offset))


def resolve_preset_name(name: str, presets: Dict[str, PresetRow]) -> str | None:
    if name in presets:
        return name
    alias = PRESET_NAME_ALIASES.get(name)
    if alias and alias in presets:
        return alias
    # Also allow reverse lookup when user passes table name directly.
    for canonical, table_name in PRESET_NAME_ALIASES.items():
        if name == table_name and table_name in presets:
            return table_name
        if name == canonical and table_name in presets:
            return table_name
    return None


def map_sample_to_preset(sample: Path, presets: Dict[str, PresetRow]) -> str | None:
    if sample.name in MANUAL_SAMPLE_TO_PRESET:
        p = MANUAL_SAMPLE_TO_PRESET[sample.name]
        resolved = resolve_preset_name(p, presets)
        if resolved is not None:
            return resolved

    key = normalize_key(sample.stem)
    for kw, preset in KEYWORD_TO_PRESET.items():
        if kw in key:
            resolved = resolve_preset_name(preset, presets)
            if resolved is not None:
                return resolved

    # Fallback by fuzzy inclusion on normalized preset names.
    preset_keys = {normalize_key(p): p for p in presets}
    for pk_norm, pname in preset_keys.items():
        if pk_norm and (pk_norm in key or key in pk_norm):
            return pname

    return None


def render_filename_for_preset(preset_name: str, idx: int, note: int | None = None) -> str:
    alias = RENDER_PRESET_NAMES.get(preset_name, preset_name)
    if note is None:
        return f"{idx:02d}_{alias}.wav"
    return f"{idx:02d}_{alias}_n{int(note):03d}.wav"


def find_render_for_preset(render_dir: Path, preset: PresetRow, note: int | None = None) -> Path | None:
    candidate = render_dir / render_filename_for_preset(preset.name, preset.idx, note=note)
    if candidate.exists():
        return candidate
    # loose fallback by idx prefix.
    pref = f"{preset.idx:02d}_"
    for wav in render_dir.glob(f"{pref}*.wav"):
        return wav
    return None


def suggest_tuning(metrics: Dict[str, float], preset_values: List[int]) -> List[str]:
    # Indices in preset row.
    DKAY = 10
    MTERL = 11
    MLST = 5
    NZMX = 19

    hints: List[str] = []
    dky = preset_values[DKAY]
    mterl = preset_values[MTERL]
    mlst = preset_values[MLST]
    nzmx = preset_values[NZMX]

    if metrics["t60_pct"] > 25:
        if metrics["t60_pct"] > 0:
            hints.append(f"Adjust Dkay around {dky} by ±10..20 (current mismatch on decay is high).")

    if metrics["attack_pct"] > 25:
        hints.append(f"Increase/decrease MlSt around {mlst} by ~50 to align attack sharpness.")

    if metrics["centroid_pct"] > 25 or metrics["rolloff_pct"] > 25:
        hints.append(f"Shift Mterl around {mterl} by ±3..8 to rebalance brightness.")

    if metrics["flatness_pct"] > 35 or metrics["flux_pct"] > 35:
        hints.append(f"Adjust NzMix around {nzmx} by ±5..15 for noise/transient texture.")

    if metrics.get("mel_entropy_pct", 0.0) > 25:
        hints.append("Mel-band entropy mismatch; rebalance exciter/noise mix and model brightness together.")

    if metrics.get("centroid_decay_slope_pct", 0.0) > 35:
        hints.append("Brightness decay trajectory mismatch; adjust transient LP/AP depth or early damping behavior.")

    if metrics.get("mode_tau2_pct", 0.0) > 35 or metrics.get("mode_tau3_pct", 0.0) > 35:
        hints.append("Late decay modes mismatch; refine Dkay and modal damping constants (if Stage-2 pilot is enabled).")

    if metrics.get("mode_r2_pct", 0.0) > 20 or metrics.get("mode_r3_pct", 0.0) > 20:
        hints.append("Discrete-time damping pole mismatch; reduce/raise modal feedback decay carefully to keep stable tail character.")

    if metrics.get("centroid_corr_dist", 0.0) > 0.35:
        hints.append("Brightness trajectory correlation is low; tune attack/transient filters rather than static brightness only.")

    if metrics["f0_pct"] > 5:
        hints.append(
            f"Pitch mismatch detected ({metrics.get('note_offset_semitones', 0.0):+.2f} st); "
            "check Note parameter and model/delay coupling."
        )

    if metrics["inharm_pct"] > 30:
        hints.append("Inharmonicity mismatch; try modest InHm and/or model change.")

    if not hints:
        hints.append("No large mismatch: fine-tune in small steps (±2..5) and re-run.")

    return hints


def class_weighted_score(base_score: float, preset_name: str, metrics: Dict[str, float], goal_mode: str = "exact") -> float:
    score = base_score
    family = PRESET_TO_FAMILY.get(preset_name, "other")

    if preset_name in PERCUSSIVE_PRESETS:
        # Stage-1 metric steering: prioritize transient complexity for percussion.
        score += 0.12 * metrics.get("flatness_pct", 0.0)
        score += 0.12 * metrics.get("flux_pct", 0.0)

    # Per-family weighting: for membranes/wood-like instruments, reduce
    # pitch-dominance when f0 tracking is unstable and reward timbre trajectory.
    if family in {"membranes", "wood"} or preset_name in UNPITCHED_FOCUS_PRESETS:
        score -= 0.10 * metrics.get("f0_pct", 0.0)
        score -= 0.20 * abs(metrics.get("note_offset_semitones", 0.0))
        score -= 6.0 * metrics.get("timbre_vec_cosdist", 0.0)
        score -= 2.0 * metrics.get("centroid_corr_dist", 0.0)

    if goal_mode == "recognizable":
        # For inharmonic/metallic instruments we care more about recognizability
        # and character than exact note matching.
        if preset_name in {"Cymbl", "Ride", "RidBel", "Trngle", "Gong", "HHat-C", "HHat-O", "Tick", "TblrBel"}:
            score -= 0.20 * metrics.get("centroid_pct", 0.0)
            score -= 0.20 * metrics.get("rolloff_pct", 0.0)
            score -= 0.30 * metrics.get("flatness_pct", 0.0)
            score -= 0.30 * metrics.get("flux_pct", 0.0)
            score -= 10.0 * metrics.get("timbre_vec_cosdist", 0.0)
            score += 0.05 * metrics.get("f0_pct", 0.0)
            score += 0.10 * abs(metrics.get("note_offset_semitones", 0.0))

    # Descriptor-driven penalties (recognizable-family targets from HW design goals):
    # lower is better; penalties activate only when outside desired descriptor windows.
    flux = metrics.get("flux_pct", 0.0)
    centroid = metrics.get("centroid_pct", 0.0)
    t60 = metrics.get("t60_pct", 0.0)
    flat = metrics.get("flatness_pct", 0.0)

    def over(v: float, lim: float, gain: float) -> float:
        return max(0.0, v - lim) * gain

    def under(v: float, lim: float, gain: float) -> float:
        return max(0.0, lim - v) * gain

    # Kick: needs punch/sub balance and controlled sustain.
    if preset_name in KICK_PRESETS:
        score += over(t60, 40.0, 0.35) + over(flat, 55.0, 0.20) + under(flux, 18.0, 0.20)
    # Snare: crack/snap with quick dry body.
    if preset_name in SNARE_PRESETS:
        score += over(t60, 48.0, 0.25) + under(centroid, 15.0, 0.20) + under(flux, 20.0, 0.25)
    # Toms/congas/bongos: musical resonance, clear pitch body.
    if preset_name in TOM_PRESETS:
        score += over(flat, 60.0, 0.15) + under(t60, 8.0, 0.10)
    # Hi-hats: bright click (closed) vs wash (open).
    if preset_name == "HHat-C":
        score += under(centroid, 18.0, 0.25) + over(t60, 26.0, 0.20)
    if preset_name == "HHat-O":
        score += under(centroid, 16.0, 0.18) + under(t60, 16.0, 0.12)
    # Cymbals / ride: long natural decay + shimmer.
    if preset_name in CYMBAL_PRESETS:
        score += under(t60, 10.0, 0.12) + under(flux, 14.0, 0.12)
    # Gong: booming dark body + overtone growth.
    if preset_name in GONG_PRESETS:
        score += under(t60, 12.0, 0.18) + over(centroid, 70.0, 0.10)
    # Shakers/percussion textures: organic noise & subtle dynamics.
    if preset_name in PERC_TEXTURE_PRESETS:
        score += under(flat, 8.0, 0.15) + under(flux, 8.0, 0.12)

    return score


def descriptor_window_penalty(comp: Dict[str, object], style: str = "electronic", bpm: float = 120.0) -> float:
    """Absolute descriptor windows (family intent priors, recognizable goal).
    Uses extracted feature values directly where possible.
    """
    p = str(comp.get("preset", ""))
    ren = comp.get("ren_features", {}) or {}
    # Defensive numeric extraction.
    t60_ms = float(ren.get("t60_ms", 0.0) or 0.0)
    centroid_hz = float(ren.get("centroid_hz", 0.0) or 0.0)
    rolloff_hz = float(ren.get("rolloff_hz", 0.0) or 0.0)
    flux = float(ren.get("flux", 0.0) or 0.0)

    pen = 0.0
    prof = STYLE_PROFILES.get(style, STYLE_PROFILES["electronic"])
    hhat_closed_max = prof["hhat_closed_max_ms"]
    hhat_open_min = prof["hhat_open_min_ms"]
    hhat_open_max = prof["hhat_open_max_ms"]
    # Optional tempo-aware ceiling (roughly 1/8 note).
    if bpm > 1.0:
        eighth_ms = 30000.0 / bpm
        hhat_closed_max = min(hhat_closed_max, max(120.0, eighth_ms))
        hhat_open_max = max(hhat_open_max, 0.8 * eighth_ms)

    # Closed hat tail max.
    if p == "HHat-C" and t60_ms > hhat_closed_max:
        pen += (t60_ms - hhat_closed_max) * 0.02
    # Open hat wash minimum + excessive wash guard.
    if p == "HHat-O":
        if t60_ms < hhat_open_min:
            pen += (hhat_open_min - t60_ms) * 0.03
        if t60_ms > hhat_open_max:
            pen += (t60_ms - hhat_open_max) * 0.01

    # Snare crack prominence proxy (2.5k..5k): use brightness proxies.
    snare_crack_hz = prof["snare_crack_hz"]
    if p in SNARE_PRESETS:
        if centroid_hz < snare_crack_hz:
            pen += (snare_crack_hz - centroid_hz) * 0.004
        if rolloff_hz < 5000.0:
            pen += (5000.0 - rolloff_hz) * 0.002
        if flux < 2.0:
            pen += (2.0 - flux) * 12.0

    # Gong sweep aggression + swell proxy: require moderate transient movement and not overly bright body.
    if p in GONG_PRESETS:
        if flux < 1.4:
            pen += (1.4 - flux) * 10.0
        if centroid_hz > 3200.0:
            pen += (centroid_hz - 3200.0) * 0.001
    return pen


def estimate_runs_needed(current_score: float, target_score: float = 12.0, assumed_improvement: float = 0.72) -> int:
    """Estimate runs using exponential convergence model:
    score_next = score_now * assumed_improvement.
    """
    if current_score <= target_score:
        return 0
    # Guard against invalid convergence factors.
    # Expected range is strictly (0, 1):
    #   - <= 0 or == 1 are invalid for log-based estimate.
    #   - > 1 implies divergence, so treat as "no finite estimate".
    if assumed_improvement <= 0.0 or assumed_improvement == 1.0:
        return 0
    if assumed_improvement > 1.0:
        return 0
    n = math.log(target_score / current_score) / math.log(assumed_improvement)
    return max(1, math.ceil(n))


def run_renderer_for_preset(render_cmd: str, render_dir: Path, preset: PresetRow, note: int = 60) -> Path:
    render_dir.mkdir(parents=True, exist_ok=True)
    out = render_dir / render_filename_for_preset(preset.name, preset.idx, note=note)
    fmt = {
        "preset_idx": str(preset.idx),
        "preset_name": preset.name,
        "note": str(int(note)),
        "output_wav": str(out),
    }
    cmd = render_cmd.format(**fmt)
    subprocess.run(shlex.split(cmd), check=True)
    return out


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Batch runner for rendered-vs-reference analysis and tuning hints")
    p.add_argument("--samples-dir", type=Path, default=SAMPLES_DIR)
    p.add_argument("--render-dir", type=Path, default=DEFAULT_RENDER_DIR)
    p.add_argument("--run-render", action="store_true", help="Render presets before analysis using --render-cmd")
    p.add_argument(
        "--render-cmd",
        type=str,
        default="",
        help=(
            "Renderer command template with placeholders: {preset_idx}, {preset_name}, {note}, {output_wav}. "
            "Example: \"qemu-arm ./run_test_render --preset {preset_idx} --note {note} --name {preset_name} --out {output_wav}\""
        ),
    )
    p.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    p.add_argument("--target-score", type=float, default=12.0)
    p.add_argument("--assumed-improvement", type=float, default=0.72)
    p.add_argument("--auto-note-align", action="store_true", help="Enable pitch-aligned MR-STFT term in comparisons")
    p.add_argument(
        "--prefer-audio-pitch",
        action="store_true",
        help="Infer sample notes from audio autocorrelation before filename-note parsing.",
    )
    p.add_argument(
        "--preset-group",
        choices=["all", "first_batch", "remaining_batch"],
        default="all",
        help="Optional preset group selector. Use with/without --preset-filter.",
    )
    p.add_argument(
        "--preset-filter",
        type=str,
        default="",
        help="Comma-separated preset names to include (e.g. Wodblk,Timpani). Empty = all.",
    )
    p.add_argument("--helper", action="store_true", help="Print detailed workflow helper and exit")
    p.add_argument("--write-helper", type=Path, default=None, help="Write detailed workflow helper markdown and exit")
    p.add_argument(
        "--iterations",
        type=int,
        default=1,
        help="Number of consecutive tuning iterations to run (default: 1).",
    )
    p.add_argument(
        "--early-stop-stable-runs",
        type=int,
        default=0,
        help="Stop early once this many consecutive iterations are stable (0 disables).",
    )
    p.add_argument(
        "--stable-eps",
        type=float,
        default=0.20,
        help="Absolute mean-score delta threshold used to count an iteration as stable.",
    )
    p.add_argument(
        "--goal-mode",
        choices=["exact", "recognizable"],
        default="exact",
        help="Scoring goal: exact sample match or recognizable instrument character.",
    )
    p.add_argument("--style", choices=sorted(STYLE_PROFILES.keys()), default="electronic", help="Style profile for descriptor windows.")
    p.add_argument("--bpm", type=float, default=120.0, help="Tempo hint used for hat tail windows.")
    p.add_argument(
        "--family-pitch-thresholds",
        type=str,
        default="",
        help=(
            "Optional family pitch mismatch thresholds in percent, format "
            "'membranes:10,mallets:10,wood:10'. Defaults are applied when omitted."
        ),
    )
    p.add_argument(
        "--sample-map-file",
        type=Path,
        default=None,
        help="Optional JSON map (sample filename -> preset name) to override sample-to-preset matching.",
    )
    p.add_argument(
        "--note-map-file",
        type=Path,
        default=None,
        help="Optional JSON map (sample-name or preset-name -> MIDI note) to lock render notes.",
    )
    return p.parse_args()


def parse_family_pitch_thresholds(raw: str) -> Dict[str, float]:
    out = dict(DEFAULT_FAMILY_PITCH_THRESHOLDS)
    if not raw.strip():
        return out
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue
        if ":" not in token:
            raise SystemExit(f"Invalid family threshold token: {token}")
        fam, val = token.split(":", 1)
        fam = fam.strip().lower()
        try:
            out[fam] = float(val.strip())
        except ValueError as e:
            raise SystemExit(f"Invalid threshold value in token: {token}") from e
    return out


def load_note_map(path: Path | None) -> Dict[str, int]:
    if path is None:
        return {}
    if not path.exists():
        raise SystemExit(f"Note-map file not found: {path}")
    try:
        data = json.loads(path.read_text())
    except Exception as e:
        raise SystemExit(f"Invalid note-map JSON: {path}") from e
    if not isinstance(data, dict):
        raise SystemExit(f"Note-map must be a JSON object: {path}")
    out: Dict[str, int] = {}
    for k, v in data.items():
        try:
            out[str(k)] = max(0, min(127, int(v)))
        except Exception:
            continue
    return out



def load_sample_map(path: Path | None, presets: Dict[str, PresetRow]) -> Dict[str, str]:
    if path is None:
        return {}
    if not path.exists():
        raise SystemExit(f"Sample-map file not found: {path}")
    try:
        data = json.loads(path.read_text())
    except Exception as e:
        raise SystemExit(f"Invalid sample-map JSON: {path}") from e
    if not isinstance(data, dict):
        raise SystemExit(f"Sample-map must be a JSON object: {path}")
    out: Dict[str, str] = {}
    for sample_name, preset_name in data.items():
        resolved = resolve_preset_name(str(preset_name), presets)
        if resolved is None:
            continue
        out[str(sample_name)] = resolved
    return out

def run_one_iteration(
    args: argparse.Namespace,
    iteration_idx: int,
    out_dir: Path,
) -> Dict[str, object]:
    presets = parse_presets(SYNTH_ENGINE)
    selected_presets = None
    if args.preset_group != "all":
        selected_presets = set()
        for raw in PRESET_GROUPS.get(args.preset_group, []):
            resolved = resolve_preset_name(raw, presets)
            if resolved is not None:
                selected_presets.add(resolved)
    if args.preset_filter.strip():
        if selected_presets is None:
            selected_presets = set()
        for raw in (x.strip() for x in args.preset_filter.split(",") if x.strip()):
            resolved = resolve_preset_name(raw, presets)
            selected_presets.add(resolved if resolved is not None else raw)

    out_dir.mkdir(parents=True, exist_ok=True)
    sample_files = sorted(args.samples_dir.glob("*.wav"))

    mapped: List[Tuple[Path, PresetRow, int]] = []
    unmapped: List[str] = []
    note_map = load_note_map(args.note_map_file)
    sample_map = load_sample_map(args.sample_map_file, presets)

    for s in sample_files:
        pname = sample_map.get(s.name)
        if pname is None:
            pname = map_sample_to_preset(s, presets)
        if pname is None:
            unmapped.append(s.name)
            continue
        if selected_presets is not None and pname not in selected_presets:
            continue
        if s.name in note_map:
            render_note = note_map[s.name]
        elif pname in note_map:
            render_note = note_map[pname]
        elif pname in PRESET_RENDER_NOTE_DEFAULTS:
            render_note = PRESET_RENDER_NOTE_DEFAULTS[pname]
        else:
            if args.prefer_audio_pitch:
                fmin, fmax = pitch_search_range_for_preset(pname)
                render_note = infer_midi_note_from_audio(s, fallback=60, fmin=fmin, fmax=fmax)
            else:
                render_note = infer_midi_note_from_sample_name(s, fallback=60)
        render_note = apply_preset_note_calibration(pname, render_note)
        mapped.append((s, presets[pname], render_note))

    if args.run_render:
        if not args.render_cmd.strip():
            raise SystemExit("--run-render requires --render-cmd")
        # Render each unique (preset, inferred-note) pair once.
        unique_jobs = {}
        for _sample, preset, render_note in mapped:
            unique_jobs[(preset.name, render_note)] = (preset, render_note)
        for preset, render_note in unique_jobs.values():
            run_renderer_for_preset(args.render_cmd, args.render_dir, preset, note=render_note)

    family_thresholds = parse_family_pitch_thresholds(args.family_pitch_thresholds)
    results = []
    by_preset = defaultdict(list)
    by_family = defaultdict(list)

    for sample, preset, render_note in mapped:
        render_wav = find_render_for_preset(args.render_dir, preset, note=render_note)
        if not render_wav:
            continue

        comp = compare_pair(sample, render_wav, auto_note_align=args.auto_note_align)
        comp["preset"] = preset.name
        comp["family"] = PRESET_TO_FAMILY.get(preset.name, "other")
        comp["preset_idx"] = preset.idx
        comp["render_note"] = render_note
        comp["sample"] = sample.name
        comp["raw_score"] = comp["score"]
        comp["score"] = class_weighted_score(comp["score"], preset.name, comp["metrics"], goal_mode=args.goal_mode)
        comp["score"] += descriptor_window_penalty(comp, style=args.style, bpm=args.bpm)
        comp["suggestions"] = suggest_tuning(comp["metrics"], preset.values)
        comp["estimated_runs_to_target"] = estimate_runs_needed(
            comp["score"],
            target_score=args.target_score,
            assumed_improvement=args.assumed_improvement,
        )
        results.append(comp)
        by_preset[preset.name].append(comp)
        by_family[comp["family"]].append(comp)

    family_summary = {}
    for family, items in by_family.items():
        f0_vals = [float(x["metrics"]["f0_pct"]) for x in items]
        thr = family_thresholds.get(family)
        meets = (max(f0_vals) <= thr) if (thr is not None and f0_vals) else None
        family_summary[family] = {
            "count": len(items),
            "f0_pct_mean": (sum(f0_vals) / len(f0_vals)) if f0_vals else None,
            "f0_pct_max": max(f0_vals) if f0_vals else None,
            "pitch_threshold_pct": thr,
            "pitch_threshold_met": meets,
        }

    summary = {
        "iteration": iteration_idx,
        "samples_total": len(sample_files),
        "samples_mapped": len(mapped),
        "samples_unmapped": unmapped,
        "pairs_compared": len(results),
        "mean_score": (sum(r["score"] for r in results) / len(results)) if results else None,
        "target_score": args.target_score,
        "assumed_improvement": args.assumed_improvement,
        "family_pitch_thresholds": family_thresholds,
        "family_summary": family_summary,
        "results": sorted(results, key=lambda r: r["score"]),
    }

    json_path = out_dir / "batch_tuning_report.json"
    json_path.write_text(json.dumps(summary, indent=2))

    csv_path = out_dir / "batch_tuning_report.csv"
    with csv_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "preset",
            "preset_idx",
            "sample",
            "render_note",
            "score",
            "estimated_runs_to_target",
            "family",
            "f0_threshold_met",
            "f0_pct",
            "f0_ratio",
            "note_offset_semitones",
            "attack_pct",
            "t60_pct",
            "centroid_pct",
            "centroid_pitchnorm_pct",
            "rolloff_pct",
            "rolloff_pitchnorm_pct",
            "flatness_pct",
            "flux_pct",
            "inharm_pct",
            "mrstft_log_l1",
            "note_aligned_mrstft_log_l1",
        ])
        for r in summary["results"]:
            m = r["metrics"]
            fam = r.get("family", "other")
            fam_thr = family_thresholds.get(fam)
            fam_ok = (m["f0_pct"] <= fam_thr) if fam_thr is not None else ""
            w.writerow([
                r["preset"],
                r["preset_idx"],
                r["sample"],
                r.get("render_note", 60),
                f"{r['score']:.4f}",
                r["estimated_runs_to_target"],
                fam,
                fam_ok,
                f"{m['f0_pct']:.4f}",
                f"{m['f0_ratio']:.6f}",
                f"{m['note_offset_semitones']:.4f}",
                f"{m['attack_pct']:.4f}",
                f"{m['t60_pct']:.4f}",
                f"{m['centroid_pct']:.4f}",
                f"{m['centroid_pitchnorm_pct']:.4f}",
                f"{m['rolloff_pct']:.4f}",
                f"{m['rolloff_pitchnorm_pct']:.4f}",
                f"{m['flatness_pct']:.4f}",
                f"{m['flux_pct']:.4f}",
                f"{m['inharm_pct']:.4f}",
                f"{m['mrstft_log_l1']:.6f}",
                f"{m['note_aligned_mrstft_log_l1']:.6f}",
            ])

    md_path = out_dir / "batch_tuning_progress.md"
    with md_path.open("w") as f:
        f.write("# Batch Tuning Progress\n\n")
        f.write(f"- Samples discovered: {len(sample_files)}\n")
        f.write(f"- Samples mapped to presets: {len(mapped)}\n")
        f.write(f"- Pairs compared: {len(results)}\n")
        if summary["mean_score"] is not None:
            f.write(f"- Mean score: {summary['mean_score']:.3f}\n")
        f.write(f"- Target score: {args.target_score}\n")
        f.write(f"- Assumed improvement/run: {args.assumed_improvement:.2f}\n\n")
        f.write(f"- Auto note align: {args.auto_note_align}\n\n")
        if family_thresholds:
            f.write("## Family pitch thresholds\n\n")
            for fam, thr in sorted(family_thresholds.items()):
                f.write(f"- {fam}: {thr:.2f}%\n")
            f.write("\n")
        if family_summary:
            f.write("## Family pitch summary\n\n")
            f.write("| Family | Count | f0% mean | f0% max | Target % | Met |\n")
            f.write("|---|---:|---:|---:|---:|:---:|\n")
            for fam, fs in sorted(family_summary.items()):
                mean = fs["f0_pct_mean"]
                mx = fs["f0_pct_max"]
                thr = fs["pitch_threshold_pct"]
                met = fs["pitch_threshold_met"]
                mean_txt = "n/a" if mean is None else f"{mean:.2f}"
                max_txt = "n/a" if mx is None else f"{mx:.2f}"
                thr_txt = "n/a" if thr is None else f"{thr:.2f}"
                met_txt = "n/a" if met is None else ("yes" if met else "no")
                f.write(f"| {fam} | {fs['count']} | {mean_txt} | {max_txt} | {thr_txt} | {met_txt} |\n")
            f.write("\n")

        if unmapped:
            f.write("## Unmapped samples\n\n")
            for name in unmapped:
                f.write(f"- {name}\n")
            f.write("\n")

        f.write("## Preset results\n\n")
        for preset_name, items in sorted(by_preset.items(), key=lambda kv: sum(x["score"] for x in kv[1]) / len(kv[1]), reverse=True):
            avg = sum(x["score"] for x in items) / len(items)
            runs = max(x["estimated_runs_to_target"] for x in items)
            f.write(f"### {preset_name} (avg score {avg:.2f}, est runs {runs})\n\n")
            for item in sorted(items, key=lambda x: x["score"], reverse=True):
                f.write(f"- Sample `{item['sample']}` score `{item['score']:.2f}`\n")
                for s in item["suggestions"]:
                    f.write(f"  - {s}\n")
            f.write("\n")

    # Final snapshot artifact: presets and values involved in this run.
    touched = sorted({r["preset"] for r in results if "preset" in r})
    snapshot_rows = []
    for pname in touched:
        p = presets.get(pname)
        if p is None:
            continue
        rows = by_preset.get(pname, [])
        avg = sum(x["score"] for x in rows) / max(len(rows), 1) if rows else None
        snapshot_rows.append({
            "preset": pname,
            "preset_idx": p.idx,
            "values": p.values,
            "avg_score": avg,
            "samples": [x.get("sample") for x in rows],
        })
    snap_json = out_dir / "batch_tuning_final_presets.json"
    snap_json.write_text(json.dumps({"presets": snapshot_rows}, indent=2))
    snap_md = out_dir / "batch_tuning_final_presets.md"
    with snap_md.open("w") as f:
        f.write("# Batch Tuning Final Preset Values\n\n")
        f.write("| Preset | Idx | Avg score | Values |\n")
        f.write("|---|---:|---:|---|\n")
        for r in snapshot_rows:
            atxt = f"{r['avg_score']:.2f}" if isinstance(r.get("avg_score"), (int, float)) else "n/a"
            f.write(f"| {r['preset']} | {r['preset_idx']} | {atxt} | `{r['values']}` |\n")

    print(f"Wrote: {json_path}")
    print(f"Wrote: {csv_path}")
    print(f"Wrote: {md_path}")
    print(f"Wrote: {snap_json}")
    print(f"Wrote: {snap_md}")
    if unmapped:
        print(f"Unmapped samples: {len(unmapped)}")

    return summary


def main() -> int:
    args = parse_args()

    if args.helper or args.write_helper is not None:
        helper = build_helper_text()
        if args.helper:
            print(helper)
        if args.write_helper is not None:
            args.write_helper.parent.mkdir(parents=True, exist_ok=True)
            args.write_helper.write_text(helper)
            print(f"Wrote helper: {args.write_helper}")
        return 0

    if args.iterations < 1:
        raise SystemExit("--iterations must be >= 1")

    run_summaries: List[Dict[str, object]] = []
    stable_streak = 0
    stopped_early = False
    stop_reason = ""
    for it in range(1, args.iterations + 1):
        iter_dir = args.out_dir if args.iterations == 1 else (args.out_dir / f"iter_{it:02d}")
        print(f"[iter {it}/{args.iterations}] running...")
        summary = run_one_iteration(args, it, iter_dir)
        run_summaries.append(summary)
        if args.early_stop_stable_runs > 0 and len(run_summaries) >= 2:
            prev = run_summaries[-2].get("mean_score")
            cur = run_summaries[-1].get("mean_score")
            if prev is not None and cur is not None:
                delta = abs(float(cur) - float(prev))
                if delta <= args.stable_eps:
                    stable_streak += 1
                else:
                    stable_streak = 0
                print(
                    f"[iter {it}] mean-score delta={delta:.4f} "
                    f"(stable<= {args.stable_eps:.4f}), streak={stable_streak}/{args.early_stop_stable_runs}"
                )
                if stable_streak >= args.early_stop_stable_runs:
                    stopped_early = True
                    stop_reason = (
                        f"Reached max-reachable plateau: {stable_streak} stable runs "
                        f"(delta <= {args.stable_eps:.4f})."
                    )
                    print(f"[iter {it}] early stop: {stop_reason}")
                    break

    if args.iterations > 1:
        history_path = args.out_dir / "batch_tuning_history.json"
        history_payload = {
            "iterations": args.iterations,
            "iterations_executed": len(run_summaries),
            "target_score": args.target_score,
            "assumed_improvement": args.assumed_improvement,
            "early_stop_stable_runs": args.early_stop_stable_runs,
            "stable_eps": args.stable_eps,
            "stopped_early": stopped_early,
            "stop_reason": stop_reason,
            "runs": [
                {
                    "iteration": s["iteration"],
                    "pairs_compared": s["pairs_compared"],
                    "mean_score": s["mean_score"],
                }
                for s in run_summaries
            ],
        }
        history_path.write_text(json.dumps(history_payload, indent=2))
        print(f"Wrote: {history_path}")

        md_history_path = args.out_dir / "batch_tuning_history.md"
        with md_history_path.open("w") as f:
            f.write("# Batch Tuning Iteration History\n\n")
            f.write(f"- Iterations: {args.iterations}\n")
            f.write(f"- Iterations executed: {len(run_summaries)}\n")
            f.write(f"- Target score: {args.target_score}\n")
            f.write(f"- Assumed improvement/run: {args.assumed_improvement:.2f}\n\n")
            if args.early_stop_stable_runs > 0:
                f.write(f"- Stable-run early-stop: {args.early_stop_stable_runs}\n")
                f.write(f"- Stability epsilon: {args.stable_eps:.4f}\n")
            if stopped_early and stop_reason:
                f.write(f"- Stop reason: {stop_reason}\n")
            f.write("\n")
            f.write("| Iteration | Pairs compared | Mean score |\n")
            f.write("|---:|---:|---:|\n")
            for s in run_summaries:
                mean = s["mean_score"]
                mean_txt = "n/a" if mean is None else f"{float(mean):.3f}"
                f.write(f"| {s['iteration']} | {s['pairs_compared']} | {mean_txt} |\n")
        print(f"Wrote: {md_history_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

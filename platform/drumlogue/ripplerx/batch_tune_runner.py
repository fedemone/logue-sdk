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
  python3 batch_tune_runner.py --run-render --render-cmd "qemu-arm ./run_test_render --preset {preset_idx} --name {preset_name} --out {output_wav}"
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

from pre_hw_analysis import compare_pair


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
    "high-church-clock-fx_100bpm.wav": "TblrBel",
    "one-tic-clock.wav": "Tick",
    "ordinary-old-clock-ticking-sound-recording_120bpm-mechanical-strike.wav": "Tick",
    "Bongo_Conga2.wav": "Bongo",
    "Bongo_Conga_Mute4.wav": "Conga",
    "GlassBottle.wav": "GlsBotl",
    "steel-pan-Nova Drum Real C 432.wav": "StelPan",
    "steel-pan-PERCY-C4-SHort.wav": "StelPan",
    "steel-pan-yudin C3.wav": "StelPan",
    "Koto-B5.wav": "Koto",
    "Koto-Pluck-C-Major.wav": "Koto",
    "Tabla-Drum-Hit-D4_.wav": "Djambe",
    "percussion-one-shot-tabla-3_C_major.wav": "Djambe",
}

KEYWORD_TO_PRESET = {
    "timpani": "Timpani",
    "marimba": "Marimba",
    "kalimba": "Kalimba",
    "koto": "Koto",
    "gong": "Gong",
    "tam": "TamTam",
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
    "TamTam": "TamTam",
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
    "AcSnre", "Timpani", "Djambe", "Taiko", "MrchSnr", "TamTam", "Wodblk",
    "Ac Tom", "AcTom", "Cymbal", "Gong", "Claves", "Cowbel", "Triangle", "Kick",
    "Clap", "Shaker", "HHat-C", "HHat-O", "Conga", "SltDrm", "Ride", "RidBel", "Bongo", "Tick",
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
  --render-cmd "qemu-arm ./run_test_render --preset {{preset_idx}} --name {{preset_name}} --out {{output_wav}}" \\
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


def map_sample_to_preset(sample: Path, presets: Dict[str, PresetRow]) -> str | None:
    if sample.name in MANUAL_SAMPLE_TO_PRESET:
        p = MANUAL_SAMPLE_TO_PRESET[sample.name]
        if p in presets:
            return p

    key = normalize_key(sample.stem)
    for kw, preset in KEYWORD_TO_PRESET.items():
        if kw in key and preset in presets:
            return preset

    # Fallback by fuzzy inclusion on normalized preset names.
    preset_keys = {normalize_key(p): p for p in presets}
    for pk_norm, pname in preset_keys.items():
        if pk_norm and (pk_norm in key or key in pk_norm):
            return pname

    return None


def render_filename_for_preset(preset_name: str, idx: int) -> str:
    alias = RENDER_PRESET_NAMES.get(preset_name, preset_name)
    return f"{idx:02d}_{alias}.wav"


def find_render_for_preset(render_dir: Path, preset: PresetRow) -> Path | None:
    candidate = render_dir / render_filename_for_preset(preset.name, preset.idx)
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


def class_weighted_score(base_score: float, preset_name: str, metrics: Dict[str, float]) -> float:
    score = base_score
    if preset_name in PERCUSSIVE_PRESETS:
        # Stage-1 metric steering: prioritize transient complexity for percussion.
        score += 0.12 * metrics.get("flatness_pct", 0.0)
        score += 0.12 * metrics.get("flux_pct", 0.0)
    return score


def estimate_runs_needed(current_score: float, target_score: float = 12.0, assumed_improvement: float = 0.72) -> int:
    """Estimate runs using exponential convergence model:
    score_next = score_now * assumed_improvement.
    """
    if current_score <= target_score:
        return 0
    n = math.log(target_score / current_score) / math.log(assumed_improvement)
    return max(1, math.ceil(n))


def run_renderer_for_preset(render_cmd: str, render_dir: Path, preset: PresetRow) -> Path:
    render_dir.mkdir(parents=True, exist_ok=True)
    out = render_dir / render_filename_for_preset(preset.name, preset.idx)
    fmt = {
        "preset_idx": str(preset.idx),
        "preset_name": preset.name,
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
            "Renderer command template with placeholders: {preset_idx}, {preset_name}, {output_wav}. "
            "Example: \"qemu-arm ./run_test_render --preset {preset_idx} --name {preset_name} --out {output_wav}\""
        ),
    )
    p.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    p.add_argument("--target-score", type=float, default=12.0)
    p.add_argument("--assumed-improvement", type=float, default=0.72)
    p.add_argument("--auto-note-align", action="store_true", help="Enable pitch-aligned MR-STFT term in comparisons")
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
    return p.parse_args()


def run_one_iteration(
    args: argparse.Namespace,
    iteration_idx: int,
    out_dir: Path,
) -> Dict[str, object]:
    presets = parse_presets(SYNTH_ENGINE)
    selected_presets = None
    if args.preset_filter.strip():
        selected_presets = {x.strip() for x in args.preset_filter.split(",") if x.strip()}

    out_dir.mkdir(parents=True, exist_ok=True)
    sample_files = sorted(args.samples_dir.glob("*.wav"))

    mapped: List[Tuple[Path, PresetRow]] = []
    unmapped: List[str] = []

    for s in sample_files:
        pname = map_sample_to_preset(s, presets)
        if pname is None:
            unmapped.append(s.name)
            continue
        if selected_presets is not None and pname not in selected_presets:
            continue
        mapped.append((s, presets[pname]))

    if args.run_render:
        if not args.render_cmd.strip():
            raise SystemExit("--run-render requires --render-cmd")
        # Render each preset once even if multiple samples map to it.
        unique_presets = {}
        for _sample, preset in mapped:
            unique_presets[preset.name] = preset
        for preset in unique_presets.values():
            run_renderer_for_preset(args.render_cmd, args.render_dir, preset)

    results = []
    by_preset = defaultdict(list)

    for sample, preset in mapped:
        render_wav = find_render_for_preset(args.render_dir, preset)
        if not render_wav:
            continue

        comp = compare_pair(sample, render_wav, auto_note_align=args.auto_note_align)
        comp["preset"] = preset.name
        comp["preset_idx"] = preset.idx
        comp["sample"] = sample.name
        comp["raw_score"] = comp["score"]
        comp["score"] = class_weighted_score(comp["score"], preset.name, comp["metrics"])
        comp["suggestions"] = suggest_tuning(comp["metrics"], preset.values)
        comp["estimated_runs_to_target"] = estimate_runs_needed(
            comp["score"],
            target_score=args.target_score,
            assumed_improvement=args.assumed_improvement,
        )
        results.append(comp)
        by_preset[preset.name].append(comp)

    summary = {
        "iteration": iteration_idx,
        "samples_total": len(sample_files),
        "samples_mapped": len(mapped),
        "samples_unmapped": unmapped,
        "pairs_compared": len(results),
        "mean_score": (sum(r["score"] for r in results) / len(results)) if results else None,
        "target_score": args.target_score,
        "assumed_improvement": args.assumed_improvement,
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
            "score",
            "estimated_runs_to_target",
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
            w.writerow([
                r["preset"],
                r["preset_idx"],
                r["sample"],
                f"{r['score']:.4f}",
                r["estimated_runs_to_target"],
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

    print(f"Wrote: {json_path}")
    print(f"Wrote: {csv_path}")
    print(f"Wrote: {md_path}")
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
    for it in range(1, args.iterations + 1):
        iter_dir = args.out_dir if args.iterations == 1 else (args.out_dir / f"iter_{it:02d}")
        print(f"[iter {it}/{args.iterations}] running...")
        summary = run_one_iteration(args, it, iter_dir)
        run_summaries.append(summary)

    if args.iterations > 1:
        history_path = args.out_dir / "batch_tuning_history.json"
        history_payload = {
            "iterations": args.iterations,
            "target_score": args.target_score,
            "assumed_improvement": args.assumed_improvement,
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
            f.write(f"- Target score: {args.target_score}\n")
            f.write(f"- Assumed improvement/run: {args.assumed_improvement:.2f}\n\n")
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

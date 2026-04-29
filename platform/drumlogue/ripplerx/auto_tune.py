#!/usr/bin/env python3
"""
auto_tune.py — Automated coordinate-descent tuner for RipplerX presets.

Strategy (one round = 13 compiles, ~72 s):
  For each of 6 tunable parameters × 2 directions (12 trials):
    - Apply that delta to ALL presets simultaneously, compile once, render,
      score every preset against its reference samples.
  After all trials, each preset independently accepts the (param, direction)
  that gave the biggest improvement (if any).  A single "apply" compile
  writes the accepted changes for that round.

Runs up to MAX_ROUNDS rounds; stops early when no preset improves for
STABLE_ROUNDS consecutive rounds.

Usage:
    python3 auto_tune.py                        # 15 rounds, all presets
    python3 auto_tune.py --rounds 5             # fewer rounds
    python3 auto_tune.py --preset Djambe,Bongo  # subset
    python3 auto_tune.py --dry-run              # print plan, no compilation
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ── Paths ─────────────────────────────────────────────────────────────────────
REPO_DIR       = Path(__file__).resolve().parent
SYNTH_ENGINE   = REPO_DIR / "synth_engine.h"
SAMPLES_DIR    = REPO_DIR / "samples"
RENDER_DIR     = REPO_DIR / "rendered_tune"
REPORTS_DIR    = REPO_DIR / "batch_reports"

COMPILE_CMD = (
    "g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common "
    "-DRUNTIME_COMMON_H_ render_presets.cpp -o render_presets"
)
RENDER_CMD  = "./render_presets {render_dir}/"

# ── Tunable parameters: (name, col_idx, min_val, max_val, delta) ──────────────
# col_idx matches the ParamIndex enum in synth_engine.h (0-based preset row)
PARAMS: List[Tuple[str, int, int, int, int]] = [
    ("Dkay",  10,   0, 200, 10),
    ("Mterl", 11, -10,  30,  2),
    ("NzMx",  19,   0, 100,  5),
    ("NzRs",  20,   0, 1000, 40),
    ("NzFq",  22,  30, 1500, 60),
    ("MlSt",   5,   0, 500, 30),
    ("InHm",  15,   0,  10,  1),
    ("TbRd",  17,  -5,  35,  2),
]

MAX_ROUNDS    = 15
STABLE_ROUNDS = 3   # stop if no improvement for this many consecutive rounds
FINE_TUNE_START_STABLE = 1
FINE_STEP_OVERRIDES = {
    "Dkay":  5,
    "Mterl": 1,
    "NzMx":  2,
    "NzRs":  20,   # half of coarse 40 step
    "NzFq":  30,   # half of coarse 60 step (= 300 Hz effective)
}

# ── Import scoring from existing infrastructure ────────────────────────────────
sys.path.insert(0, str(REPO_DIR))
from batch_tune_runner import (
    parse_presets, map_sample_to_preset, find_render_for_preset,
    infer_midi_note_from_sample_name, apply_preset_note_calibration,
    class_weighted_score, render_filename_for_preset, PresetRow,
)
from pre_hw_analysis import compare_pair


# ── Preset-table I/O ──────────────────────────────────────────────────────────

def _preset_block_bounds(text: str) -> Tuple[int, int]:
    """Return (start, end) char offsets of the preset data block in text."""
    m = re.search(
        r"static const int32_t presets\[k_NumPrograms\]\[k_lastParamIndex\]\s*=\s*\{",
        text,
    )
    if not m:
        raise RuntimeError("Could not locate preset table in synth_engine.h")
    brace_start = text.index("{", m.start())
    depth, i = 0, brace_start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return brace_start, i + 1
        i += 1
    raise RuntimeError("Unmatched braces in preset table")


def read_preset_rows(text: str) -> List[List[int]]:
    """Return list of value lists, one per data row, preserving order."""
    start, end = _preset_block_bounds(text)
    block = text[start:end]
    rows: List[List[int]] = []
    for line in block.splitlines():
        m = re.search(r"\{([^}]+)\}", line)
        if not m:
            continue
        content = m.group(1).strip()
        if not content or content.startswith("/"):
            continue
        try:
            vals = [int(x.strip()) for x in content.split(",") if x.strip()]
        except ValueError:
            continue
        if len(vals) >= 24:
            rows.append(vals)
    return rows


def write_preset_rows(text: str, rows: List[List[int]]) -> str:
    """Replace each data row inside the preset block, preserving per-line structure."""
    start, end = _preset_block_bounds(text)
    lines = text[start:end].splitlines(keepends=True)
    row_iter = iter(rows)
    out_lines = []
    for line in lines:
        m = re.search(r"\{([^}]+)\}", line)
        if m:
            content = m.group(1).strip()
            if content and not content.startswith("/"):
                try:
                    vals = [int(x.strip()) for x in content.split(",") if x.strip()]
                except ValueError:
                    vals = []
                if len(vals) >= 24:
                    new_vals = next(row_iter, vals)
                    inner = ",".join(f"{v:4d}" for v in new_vals)
                    line = line[: m.start()] + "{" + inner + "}" + line[m.end():]
        out_lines.append(line)
    return text[:start] + "".join(out_lines) + text[end:]


# ── Rendering and scoring ──────────────────────────────────────────────────────

def compile_and_render(render_dir: Path, verbose: bool = False) -> bool:
    render_dir.mkdir(parents=True, exist_ok=True)
    # Remove old WAVs so stale renders don't pollute scores.
    for w in render_dir.glob("*.wav"):
        w.unlink()

    cwd = REPO_DIR
    r = subprocess.run(COMPILE_CMD, shell=True, cwd=cwd,
                       capture_output=not verbose)
    if r.returncode != 0:
        print("  [compile FAILED]", file=sys.stderr)
        if not verbose:
            print(r.stderr.decode(), file=sys.stderr)
        return False

    cmd = RENDER_CMD.format(render_dir=render_dir)
    r = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=not verbose)
    if r.returncode != 0:
        print("  [render FAILED]", file=sys.stderr)
        if not verbose:
            print(r.stderr.decode(), file=sys.stderr)
        return False
    return True


def score_all(
    presets: Dict[str, PresetRow],
    render_dir: Path,
    sample_files: List[Path],
    preset_filter: Optional[set],
) -> Dict[str, float]:
    """Return {preset_name: mean_score} for every preset that has a rendered WAV."""
    by_preset: Dict[str, List[float]] = {}

    for s in sample_files:
        pname = map_sample_to_preset(s, presets)
        if pname is None:
            continue
        if preset_filter is not None and pname not in preset_filter:
            continue
        preset = presets.get(pname)
        if preset is None:
            continue
        render_note = apply_preset_note_calibration(
            pname, infer_midi_note_from_sample_name(s, fallback=60)
        )
        wav = find_render_for_preset(render_dir, preset, note=render_note)
        if wav is None or not wav.exists():
            continue
        try:
            comp = compare_pair(s, wav, auto_note_align=False)
        except Exception:
            continue
        score = class_weighted_score(comp["score"], pname, comp["metrics"])
        by_preset.setdefault(pname, []).append(score)

    return {name: sum(vs) / len(vs) for name, vs in by_preset.items() if vs}


# ── Main optimisation loop ────────────────────────────────────────────────────

def run_auto_tune(
    rounds: int = MAX_ROUNDS,
    stable_stop: int = STABLE_ROUNDS,
    preset_names: Optional[List[str]] = None,
    dry_run: bool = False,
    verbose: bool = False,
) -> None:
    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    REPORTS_DIR.mkdir(parents=True, exist_ok=True)

    original_text = SYNTH_ENGINE.read_text()
    current_text  = original_text

    presets = parse_presets(SYNTH_ENGINE)

    # Build preset filter set (resolved names only).
    preset_filter: Optional[set] = None
    if preset_names:
        preset_filter = set()
        for n in preset_names:
            if n in presets:
                preset_filter.add(n)
            else:
                print(f"  Warning: preset '{n}' not found — skipped.")

    sample_files = sorted(SAMPLES_DIR.glob("*.wav")) + sorted(SAMPLES_DIR.glob("*.mp3"))

    print(f"auto_tune: {len(presets)} presets in table, "
          f"{len(sample_files)} sample files")
    if preset_filter:
        print(f"  Filtering to: {sorted(preset_filter)}")
    print(f"  Up to {rounds} rounds, early-stop after {stable_stop} stable rounds\n")

    if dry_run:
        print("[dry-run] Would trial params:", [p[0] for p in PARAMS])
        print("[dry-run] No compilation performed.")
        return

    # ── Baseline scores ────────────────────────────────────────────────────────
    print("Building baseline scores…")
    t0 = time.time()
    ok = compile_and_render(RENDER_DIR, verbose=verbose)
    if not ok:
        sys.exit(1)
    baseline = score_all(presets, RENDER_DIR, sample_files, preset_filter)
    print(f"  Baseline in {time.time()-t0:.1f}s — {len(baseline)} presets scored")
    for name, sc in sorted(baseline.items(), key=lambda x: x[1]):
        print(f"    {name:<12} {sc:.2f}")
    print()

    # Current accepted values (row lists indexed by preset_idx).
    current_rows = read_preset_rows(current_text)
    # Map preset_name → row index in current_rows (using values[0] = Prg).
    name_to_row: Dict[str, int] = {}
    for i, row in enumerate(current_rows):
        prg = row[0]
        # Find name by matching idx across parse_presets result.
        for name, pr in presets.items():
            if pr.idx == prg:
                name_to_row[name] = i
                break

    # Pre-build column → (min, max) lookup for clamping.
    col_bounds: Dict[int, Tuple[int, int]] = {
        p[1]: (p[2], p[3]) for p in PARAMS
    }

    best_scores: Dict[str, float] = dict(baseline)
    stable_count = 0
    history: List[Dict] = []

    for rnd in range(1, rounds + 1):
        round_start = time.time()
        print(f"━━━ Round {rnd}/{rounds} ━━━")

        # Per-preset: best (delta_col, delta_val) found this round.
        best_change: Dict[str, Tuple[int, int]] = {}
        best_trial:  Dict[str, float]            = dict(best_scores)

        use_fine_steps = (stable_count >= FINE_TUNE_START_STABLE)
        for param_name, col_idx, vmin, vmax, delta in PARAMS:
            eff_delta = FINE_STEP_OVERRIDES.get(param_name, delta) if use_fine_steps else delta
            for direction in (+1, -1):
                step = eff_delta * direction

                # Build trial rows: apply step to all presets (clamped).
                trial_rows = copy.deepcopy(current_rows)
                for name, row_i in name_to_row.items():
                    if preset_filter and name not in preset_filter:
                        continue
                    old_val = trial_rows[row_i][col_idx]
                    new_val = max(vmin, min(vmax, old_val + step))
                    trial_rows[row_i][col_idx] = new_val

                trial_text = write_preset_rows(current_text, trial_rows)
                SYNTH_ENGINE.write_text(trial_text)

                ok = compile_and_render(RENDER_DIR, verbose=verbose)
                if not ok:
                    continue

                # Re-parse presets from updated file for scoring.
                trial_presets = parse_presets(SYNTH_ENGINE)
                trial_scores = score_all(
                    trial_presets, RENDER_DIR, sample_files, preset_filter
                )

                for name, sc in trial_scores.items():
                    if sc < best_trial.get(name, float("inf")):
                        best_trial[name]  = sc
                        best_change[name] = (col_idx, step)

                label = f"+{step}" if step > 0 else str(step)
                print(f"  trial {param_name}{label:>4}: "
                      + "  ".join(
                          f"{n}={trial_scores.get(n, float('nan')):.1f}"
                          for n in sorted(preset_filter or trial_scores)
                          if n in trial_scores
                      ))

                # Restore synth_engine.h before next trial.
                SYNTH_ENGINE.write_text(write_preset_rows(current_text, current_rows))

        # Apply accepted per-preset changes.
        # accepted[name] = (col_idx, old_val, new_val, trial_improvement)
        accepted: Dict[str, Tuple[int, int, int, float]] = {}
        new_rows = copy.deepcopy(current_rows)
        for name, (col_idx, step) in best_change.items():
            improvement = best_scores.get(name, float("inf")) - best_trial[name]
            if improvement <= 0.0:
                continue
            row_i = name_to_row[name]
            old_val = new_rows[row_i][col_idx]
            lo, hi  = col_bounds.get(col_idx, (-10000, 10000))
            new_val = max(lo, min(hi, old_val + step))
            new_rows[row_i][col_idx] = new_val
            accepted[name] = (col_idx, old_val, new_val, improvement)

        if accepted:
            new_text = write_preset_rows(current_text, new_rows)
            SYNTH_ENGINE.write_text(new_text)
            # Final confirmation render with all accepted changes together.
            ok = compile_and_render(RENDER_DIR, verbose=verbose)
            if ok:
                apply_presets = parse_presets(SYNTH_ENGINE)
                apply_scores  = score_all(apply_presets, RENDER_DIR, sample_files, preset_filter)
                # Keep only presets whose final score actually improved.
                for name in list(accepted.keys()):
                    col_idx, old_val, new_val, _ = accepted[name]
                    if apply_scores.get(name, float("inf")) >= best_scores.get(name, float("inf")):
                        row_i = name_to_row[name]
                        new_rows[row_i][col_idx] = old_val   # revert
                        del accepted[name]
                best_scores.update({n: apply_scores[n] for n in accepted if n in apply_scores})
            current_rows = new_rows
            current_text = write_preset_rows(current_text, current_rows)
            SYNTH_ENGINE.write_text(current_text)
        else:
            # No changes — restore to current (already correct).
            SYNTH_ENGINE.write_text(write_preset_rows(current_text, current_rows))

        elapsed = time.time() - round_start
        improved_names = [n for n in accepted]
        mean_score = sum(best_scores.values()) / len(best_scores) if best_scores else 0

        print(f"\n  Round {rnd} done in {elapsed:.1f}s | mean score: {mean_score:.2f}")
        if improved_names:
            print(f"  Improved: " + ", ".join(
                f"{n} {baseline.get(n, 0):.1f}→{best_scores.get(n, 0):.1f}"
                for n in improved_names
            ))
            stable_count = 0
        else:
            stable_count += 1
            print(f"  No improvement this round ({stable_count}/{stable_stop} stable)")

        history.append({
            "round": rnd,
            "mean_score": mean_score,
            "improved": improved_names,
            "scores": dict(best_scores),
        })

        if stable_count >= stable_stop:
            print(f"\n  Early stop: {stable_stop} consecutive stable rounds.")
            break
        print()

    # ── Summary ────────────────────────────────────────────────────────────────
    print("\n" + "═" * 60)
    print("AUTO-TUNE COMPLETE\n")
    print(f"{'Preset':<12} {'Before':>8} {'After':>8} {'Delta':>7}")
    print("─" * 40)
    all_names = sorted(set(baseline) | set(best_scores))
    for name in all_names:
        before = baseline.get(name, float("nan"))
        after  = best_scores.get(name, float("nan"))
        delta  = after - before
        marker = " ✓" if delta < -0.5 else (" ✗" if delta > 0.5 else "")
        print(f"{name:<12} {before:>8.2f} {after:>8.2f} {delta:>+7.2f}{marker}")
    print("─" * 40)
    if baseline and best_scores:
        common = set(baseline) & set(best_scores)
        b_mean = sum(baseline[n] for n in common) / len(common)
        a_mean = sum(best_scores[n] for n in common) / len(common)
        print(f"{'MEAN':<12} {b_mean:>8.2f} {a_mean:>8.2f} {a_mean-b_mean:>+7.2f}")

    # Save history JSON.
    hist_path = REPORTS_DIR / "auto_tune_history.json"
    hist_path.write_text(json.dumps({"baseline": baseline, "history": history}, indent=2))
    print(f"\nHistory saved to {hist_path}")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> None:
    p = argparse.ArgumentParser(description="Automated coordinate-descent tuner for RipplerX presets")
    p.add_argument("--rounds",  type=int, default=MAX_ROUNDS)
    p.add_argument("--stable",  type=int, default=STABLE_ROUNDS,
                   help="Consecutive stable rounds before early stop")
    p.add_argument("--preset",  type=str, default="",
                   help="Comma-separated preset names (default: all with samples)")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    preset_names = [x.strip() for x in args.preset.split(",") if x.strip()] or None

    run_auto_tune(
        rounds=args.rounds,
        stable_stop=args.stable,
        preset_names=preset_names,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


if __name__ == "__main__":
    main()

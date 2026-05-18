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
import math
import re
import os
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

RENDER_BIN = f"render_presets_tune_{os.getpid()}"
COMPILE_CMD = (
    "g++ -std=c++17 -O2 -I. -Itest_stubs -I.. -I../../common -I../common "
    f"-DRUNTIME_COMMON_H_ render_presets.cpp -o {RENDER_BIN}"
)
RENDER_CMD = f"./{RENDER_BIN} {{render_dir}}/"

# ── Tunable parameters: (name, col_idx, min_val, max_val, delta) ──────────────
# col_idx matches the ParamIndex enum in synth_engine.h (0-based preset row)
PARAMS: List[Tuple[str, int, int, int, int]] = [
    ("Dkay",  10,   0, 200, 10),
    ("Mterl", 11, -10,  30,  2),
    ("NzMx",  19,   0, 100,  5),
    ("NzRs",  20,   0, 1000, 40),
    ("NzFq",  22,  30, 1500, 60),
    ("MlSt",   5,   0, 500, 30),
    ("InHm",  15,   0,  20,  1),
    ("TbRd",  17,  -5,  35,  2),
]

# model_param_presets tuning columns (float table in synth_engine.h)
MODEL_PARAMS: List[Tuple[str, int, float, float, float]] = [
    ("SnrMix", 3, 0.0, 1.0, 0.03),
    ("SnrA1", 4, 1.20, 1.95, 0.02),
    ("SnrA2", 5, 0.70, 0.99, 0.01),
    ("WireAtk", 7, 0.0002, 0.02, 0.0004),
    ("NzMixB", 9, 0.0, 1.0, 0.04),
    ("NzHi", 11, 0.05, 0.95, 0.03),
    ("PitchAmt", 16, 0.0, 24.0, 1.0),
    ("BoomMix", 20, 0.0, 0.8, 0.03),
    ("BoomAtk", 22, 0.0002, 0.02, 0.0004),
]

# Named C++ constexpr constants that appear literally in model_param_presets.
# The parser substitutes these before float-parsing; the writer restores them.
_SR = 48000.0
_NAMED_CONSTS: Dict[str, float] = {
    "kck_bm": 2.0 * math.pi * 58.0  / _SR,
    "tak_bm": 2.0 * math.pi * 70.0  / _SR,
    "tom_bm": 2.0 * math.pi * 110.0 / _SR,
    "asn_bm": 2.0 * math.pi * 175.0 / _SR,
}
# Reverse map: rounded float value → original token name.
_REV_CONSTS: Dict[str, str] = {str(round(v, 10)): k for k, v in _NAMED_CONSTS.items()}
# Boolean column indices in model_param_presets (k_use_hat_filter=12, k_reed_nl_enabled=23).
_BOOL_COLS = frozenset({12, 23})

MAX_ROUNDS    = 15
STABLE_ROUNDS = 3   # stop if no improvement for this many consecutive rounds
OUT_OF_SCOPE_PRESETS = {"Clrint", "Flute"}  # keep trace in docs; skip in percussive autotune
ARCH_BLOCKED_PRESETS = {"AcSnre", "MrchSnr", "Trngle", "Cymbal", "Gong", "HHat-O", "Marmba"}
MIN_ACCEPT_IMPROVEMENT = 0.25  # avoid churn on tiny score wiggles
PRESET_ALIASES = {
    "Marimba": "Marmba",
    "Triangle": "Trngle",
    "Clarinet": "Clrint",
    "SteelPan": "StelPan",
    "AcSnare": "AcSnre",
    "MarchSnare": "MrchSnr",
    "AcTom": "Ac Tom",
    "RideBell": "RidBel",
    "HiHatOpen": "HHat-O",
    "HiHatClosed": "HHat-C",
}

def acceptance_state_for_preset(name: str) -> str:
    if name in OUT_OF_SCOPE_PRESETS:
        return "out_of_scope_trace"
    if name in ARCH_BLOCKED_PRESETS:
        return "architecture_backlog"
    return "tunable_in_scope"
FINE_TUNE_START_STABLE = 1
FINE_STEP_OVERRIDES = {
    "Dkay": 5,
    "Mterl": 1,
    "NzMx": 2,
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

def _block_bounds(text: str, pattern: str, missing_msg: str) -> Tuple[int, int]:
    """Return (start, end) char offsets of the preset data block in text."""
    m = re.search(pattern, text)
    if not m:
        raise RuntimeError(missing_msg)
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
    raise RuntimeError("Unmatched braces in table")


def _preset_block_bounds(text: str) -> Tuple[int, int]:
    return _block_bounds(
        text,
        r"static const int32_t presets\[k_NumPrograms\]\[k_lastParamIndex\]\s*=\s*\{",
        "Could not locate preset table in synth_engine.h",
    )


def _model_param_block_bounds(text: str) -> Tuple[int, int]:
    return _block_bounds(
        text,
        r"inline static const float model_param_presets\[k_NumPrograms\]\[k_model_param_total\]\s*",
        "Could not locate model_param_presets table in synth_engine.h",
    )


def read_preset_rows(text: str) -> List[List[float]]:
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
            vals = [float(x.strip().rstrip("f")) for x in content.split(",") if x.strip()]
        except ValueError:
            continue
        if len(vals) >= 24:
            rows.append(vals)
    return rows


def write_preset_rows(text: str, rows: List[List[float]]) -> str:
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
                    vals = [float(x.strip().rstrip("f")) for x in content.split(",") if x.strip()]
                except ValueError:
                    vals = []
                if len(vals) >= 24:
                    new_vals = next(row_iter, vals)
                    inner = ",".join(f"{int(round(v)):4d}" for v in new_vals)
                    line = line[: m.start()] + "{" + inner + "}" + line[m.end():]
        out_lines.append(line)
    return text[:start] + "".join(out_lines) + text[end:]


def read_model_param_rows(text: str) -> List[List[float]]:
    start, end = _model_param_block_bounds(text)
    block = text[start:end]
    rows: List[List[float]] = []
    for line in block.splitlines():
        # Substitute named C++ constants so float() parsing succeeds.
        for cname, cval in _NAMED_CONSTS.items():
            line = line.replace(cname, repr(cval))
        m = re.search(r"\{([^}]+)\}", line)
        if not m:
            continue
        content = m.group(1).strip()
        if not content:
            continue
        norm = content.replace("false", "0").replace("true", "1")
        try:
            vals = [float(x.strip().rstrip("f")) for x in norm.split(",") if x.strip()]
        except ValueError:
            continue
        if len(vals) >= 20:
            rows.append(vals)
    return rows


def write_model_param_rows(text: str, rows: List[List[float]]) -> str:
    start, end = _model_param_block_bounds(text)
    lines = text[start:end].splitlines(keepends=True)
    row_iter = iter(rows)
    out_lines = []
    for line in lines:
        # Substitute named constants before detecting row content.
        subst_line = line
        for cname, cval in _NAMED_CONSTS.items():
            subst_line = subst_line.replace(cname, repr(cval))
        m = re.search(r"\{([^}]+)\}", subst_line)
        if m:
            content = m.group(1).strip()
            norm = content.replace("false", "0").replace("true", "1")
            try:
                vals = [float(x.strip().rstrip("f")) for x in norm.split(",") if x.strip()]
            except ValueError:
                vals = []
            if len(vals) >= 20:
                new_vals = next(row_iter, vals)
                formatted = []
                for col_i, v in enumerate(new_vals):
                    # Restore symbolic constant names where applicable.
                    rev_key = str(round(v, 10))
                    if rev_key in _REV_CONSTS:
                        formatted.append(_REV_CONSTS[rev_key])
                    elif col_i in _BOOL_COLS:
                        formatted.append("true" if v else "false")
                    else:
                        formatted.append(f"{v:10.5f}f")
                # Replace in the ORIGINAL line (which has the symbolic names).
                orig_m = re.search(r"\{([^}]+)\}", line) if line != subst_line else m
                if orig_m:
                    line = line[: orig_m.start()] + "{" + ", ".join(formatted) + "}" + line[orig_m.end():]
                else:
                    line = line[: m.start()] + "{" + ", ".join(formatted) + "}" + line[m.end():]
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
    stall_delta: float = 0.0,
    preset_names: Optional[List[str]] = None,
    dry_run: bool = False,
    verbose: bool = False,
    include_out_of_scope: bool = False,
    include_arch_blocked: bool = False,
    skip_model_params: bool = False,
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
            resolved = n if n in presets else PRESET_ALIASES.get(n)
            if resolved in presets:
                preset_filter.add(resolved)
            else:
                print(f"  Warning: preset '{n}' not found — skipped.")

    sample_files = sorted(SAMPLES_DIR.glob("*.wav")) + sorted(SAMPLES_DIR.glob("*.mp3"))

    print(f"auto_tune: {len(presets)} presets in table, "
          f"{len(sample_files)} sample files")
    if preset_filter:
        print(f"  Filtering to: {sorted(preset_filter)}")
    if not include_out_of_scope:
        if preset_filter is None:
            preset_filter = {name for name in presets.keys() if name not in OUT_OF_SCOPE_PRESETS}
        else:
            preset_filter = {name for name in preset_filter if name not in OUT_OF_SCOPE_PRESETS}
    if not include_arch_blocked:
        if preset_filter is None:
            preset_filter = {name for name in presets.keys() if name not in ARCH_BLOCKED_PRESETS}
        else:
            preset_filter = {name for name in preset_filter if name not in ARCH_BLOCKED_PRESETS}
    if preset_filter is not None:
        print(f"  Active tune-set after scope/arch gates: {sorted(preset_filter)}")
        if not preset_filter:
            print("  Nothing left to tune after scope/architecture gates. Exiting.")
            return
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
    current_model_rows = read_model_param_rows(current_text)
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
    previous_mean_score = (
        sum(best_scores.values()) / len(best_scores) if best_scores else 0.0
    )
    stable_count = 0
    history: List[Dict] = []
    routing_log: List[Dict] = []

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

        # Tune model_param_presets in the same round.
        if not skip_model_params:
            for param_name, col_idx, vmin, vmax, delta in MODEL_PARAMS:
                for direction in (+1, -1):
                    step = delta * direction
                    trial_model_rows = copy.deepcopy(current_model_rows)
                    for name, row_i in name_to_row.items():
                        if preset_filter and name not in preset_filter:
                            continue
                        old_val = trial_model_rows[row_i][col_idx]
                        trial_model_rows[row_i][col_idx] = max(vmin, min(vmax, old_val + step))

                    trial_text = write_model_param_rows(current_text, trial_model_rows)
                    SYNTH_ENGINE.write_text(trial_text)
                    ok = compile_and_render(RENDER_DIR, verbose=verbose)
                    if not ok:
                        continue
                    trial_presets = parse_presets(SYNTH_ENGINE)
                    trial_scores = score_all(trial_presets, RENDER_DIR, sample_files, preset_filter)
                    for name, sc in trial_scores.items():
                        if sc < best_trial.get(name, float("inf")):
                            best_trial[name] = sc
                            best_change[name] = (1000 + col_idx, step)  # model table marker
                    label = f"+{step:.4f}" if step > 0 else f"{step:.4f}"
                    print(f"  trial M.{param_name}{label:>8}")
                    SYNTH_ENGINE.write_text(write_model_param_rows(current_text, current_model_rows))

        # Apply accepted per-preset changes.
        # accepted[name] = (col_idx, old_val, new_val, trial_improvement)
        accepted: Dict[str, Tuple[int, int, int, float]] = {}
        new_rows = copy.deepcopy(current_rows)
        for name, (col_idx, step) in best_change.items():
            improvement = best_scores.get(name, float("inf")) - best_trial[name]
            if improvement <= MIN_ACCEPT_IMPROVEMENT:
                routing_log.append({
                    "round": rnd,
                    "preset": name,
                    "decision": "reject_small_delta",
                    "improvement": improvement,
                })
                continue
            if (name in ARCH_BLOCKED_PRESETS) and (not include_arch_blocked):
                routing_log.append({
                    "round": rnd,
                    "preset": name,
                    "decision": "route_arch_backlog",
                    "reason": "arch_blocked_default",
                    "improvement": improvement,
                })
                continue
            row_i = name_to_row[name]
            if col_idx >= 1000:
                mcol = col_idx - 1000
                old_val = current_model_rows[row_i][mcol]
                mp = next((p for p in MODEL_PARAMS if p[1] == mcol), None)
                lo, hi = (mp[2], mp[3]) if mp else (-1e9, 1e9)
                new_val = max(lo, min(hi, old_val + step))
                current_model_rows[row_i][mcol] = new_val
                accepted[name] = (col_idx, int(round(old_val * 10000)), int(round(new_val * 10000)), improvement)
            else:
                old_val = new_rows[row_i][col_idx]
                lo, hi  = col_bounds.get(col_idx, (-10000, 10000))
                new_val = max(lo, min(hi, old_val + step))
                new_rows[row_i][col_idx] = new_val
                accepted[name] = (col_idx, int(round(old_val)), int(round(new_val)), improvement)

        if accepted:
            new_text = write_model_param_rows(write_preset_rows(current_text, new_rows), current_model_rows)
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
                        if col_idx >= 1000:
                            current_model_rows[row_i][col_idx - 1000] = old_val / 10000.0
                        else:
                            new_rows[row_i][col_idx] = old_val   # revert
                        del accepted[name]
                best_scores.update({n: apply_scores[n] for n in accepted if n in apply_scores})
                for n in accepted:
                    routing_log.append({
                        "round": rnd,
                        "preset": n,
                        "decision": "accepted",
                        "after_score": apply_scores.get(n),
                    })
            current_rows = new_rows
            current_text = write_model_param_rows(write_preset_rows(current_text, current_rows), current_model_rows)
            SYNTH_ENGINE.write_text(current_text)
        else:
            # No changes — restore to current (already correct).
            SYNTH_ENGINE.write_text(write_model_param_rows(write_preset_rows(current_text, current_rows), current_model_rows))

        elapsed = time.time() - round_start
        improved_names = [n for n in accepted]
        mean_score = sum(best_scores.values()) / len(best_scores) if best_scores else 0
        mean_improvement = previous_mean_score - mean_score

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
            "mean_improvement": mean_improvement,
            "improved": improved_names,
            "scores": dict(best_scores),
        })

        if mean_improvement <= stall_delta:
            print(
                f"\n  Early stop: mean score improvement {mean_improvement:.2f} <= stall threshold {stall_delta:.2f}."
            )
            break

        if stable_count >= stable_stop:
            print(f"\n  Early stop: {stable_stop} consecutive stable rounds.")
            break
        previous_mean_score = mean_score
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
    routing_counts = {
        "accepted": sum(1 for x in routing_log if x.get("decision") == "accepted"),
        "reject_small_delta": sum(1 for x in routing_log if x.get("decision") == "reject_small_delta"),
        "route_arch_backlog": sum(1 for x in routing_log if x.get("decision") == "route_arch_backlog"),
    }
    acceptance_state_counts = {
        "tunable_in_scope": 0,
        "architecture_backlog": 0,
        "out_of_scope_trace": 0,
    }
    for n in sorted(preset_filter) if preset_filter else sorted(best_scores.keys()):
        st = acceptance_state_for_preset(n)
        acceptance_state_counts[st] += 1
    hist_path = REPORTS_DIR / "auto_tune_history.json"
    hist_path.write_text(json.dumps({
        "baseline": baseline,
        "history": history,
        "routing_log": routing_log,
        "routing_counts": routing_counts,
        "acceptance_state_counts": acceptance_state_counts,
        "min_accept_improvement": MIN_ACCEPT_IMPROVEMENT,
    }, indent=2))
    print(f"\nHistory saved to {hist_path}")

    # Save final preset values snapshot for easy/manual application and regression tracing.
    final_presets = parse_presets(SYNTH_ENGINE)
    tracked_names = sorted(preset_filter) if preset_filter else sorted(final_presets.keys())
    tracked_rows = []
    for n in tracked_names:
        pr = final_presets.get(n)
        if not pr:
            continue
        tracked_rows.append({
            "preset": n,
            "preset_idx": pr.idx,
            "values": pr.values,
            "before_score": baseline.get(n),
            "after_score": best_scores.get(n),
        })
    final_json = REPORTS_DIR / "auto_tune_final_presets.json"
    final_json.write_text(json.dumps({"presets": tracked_rows}, indent=2))
    final_md = REPORTS_DIR / "auto_tune_final_presets.md"
    with final_md.open("w") as f:
        f.write("# Auto Tune Final Preset Values\n\n")
        f.write("## Routing summary\n\n")
        f.write(f"- accepted: {routing_counts['accepted']}\n")
        f.write(f"- reject_small_delta: {routing_counts['reject_small_delta']}\n")
        f.write(f"- route_arch_backlog: {routing_counts['route_arch_backlog']}\n")
        f.write(f"- min_accept_improvement: {MIN_ACCEPT_IMPROVEMENT}\n\n")
        f.write("## Acceptance-state summary\n\n")
        f.write(f"- tunable_in_scope: {acceptance_state_counts['tunable_in_scope']}\n")
        f.write(f"- architecture_backlog: {acceptance_state_counts['architecture_backlog']}\n")
        f.write(f"- out_of_scope_trace: {acceptance_state_counts['out_of_scope_trace']}\n\n")
        f.write("| Preset | Idx | Before | After | Values |\n")
        f.write("|---|---:|---:|---:|---|\n")
        for r in tracked_rows:
            b = r["before_score"]
            a = r["after_score"]
            btxt = f"{b:.2f}" if isinstance(b, (int, float)) else "n/a"
            atxt = f"{a:.2f}" if isinstance(a, (int, float)) else "n/a"
            f.write(f"| {r['preset']} | {r['preset_idx']} | {btxt} | {atxt} | `{r['values']}` |\n")
    print(f"Final preset snapshot saved to {final_json} and {final_md}")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main() -> None:
    p = argparse.ArgumentParser(description="Automated coordinate-descent tuner for RipplerX presets")
    p.add_argument("--rounds",  type=int, default=MAX_ROUNDS)
    p.add_argument("--stable",  type=int, default=STABLE_ROUNDS,
                   help="Consecutive stable rounds before early stop")
    p.add_argument(
        "--stall-delta",
        type=float,
        default=0.0,
        help="Stop early when per-round mean-score improvement is <= this threshold",
    )
    p.add_argument("--preset",  type=str, default="",
                   help="Comma-separated preset names (default: all with samples)")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--include-out-of-scope", action="store_true",
                   help="Include non-percussive presets (Clrint/Flute).")
    p.add_argument("--include-arch-blocked", action="store_true",
                   help="Include architecture-limited presets (not recommended for pure parameter tuning).")
    p.add_argument("--skip-model-params", action="store_true",
                   help="Skip model_param_presets sweep for faster coarse runs.")
    args = p.parse_args()

    preset_names = [x.strip() for x in args.preset.split(",") if x.strip()] or None

    run_auto_tune(
        rounds=args.rounds,
        stable_stop=args.stable,
        stall_delta=args.stall_delta,
        preset_names=preset_names,
        dry_run=args.dry_run,
        verbose=args.verbose,
        include_out_of_scope=args.include_out_of_scope,
        include_arch_blocked=args.include_arch_blocked,
        skip_model_params=args.skip_model_params,
    )


if __name__ == "__main__":
    main()

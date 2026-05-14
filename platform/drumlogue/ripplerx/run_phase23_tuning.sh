#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

RENDER_CMD="./run_test_render --preset {preset_idx} --note {note} --name {preset_name} --out {output_wav}"
NOTE_MAP="./note_map_priority.json"
SAMPLES_DIR="./samples"

CLASSICS_FILTER="AcSnare,Kick,HHat-C,HHat-O,Timpani,Ac Tom"
GUARD_FILTER="Flute,Clrint,Tick,Clap,Kalimba"

mkdir -p batch_reports/phase2_pitch_validation batch_reports/phase3_classics batch_reports/phase3_guard

echo "[1/4] Building host renderer"
g++ -std=c++17 -O3 -I.. -I../common -I. test_ripplerx_render.cpp -o run_test_render

echo "[2/4] Step 2 pitch-only validation"
python3 batch_tune_runner.py \
  --samples-dir "$SAMPLES_DIR" \
  --run-render \
  --render-cmd "$RENDER_CMD" \
  --preset-filter "$CLASSICS_FILTER,$GUARD_FILTER" \
  --note-map-file "$NOTE_MAP" \
  --auto-note-align \
  --iterations 1 \
  --out-dir ./batch_reports/phase2_pitch_validation \
  --render-dir ./rendered_phase2_pitch_validation

echo "[3/4] Step 3 phase A (classics)"
python3 batch_tune_runner.py \
  --samples-dir "$SAMPLES_DIR" \
  --run-render \
  --render-cmd "$RENDER_CMD" \
  --preset-filter "$CLASSICS_FILTER" \
  --note-map-file "$NOTE_MAP" \
  --auto-note-align \
  --iterations 2 \
  --out-dir ./batch_reports/phase3_classics \
  --render-dir ./rendered_phase3_classics

echo "[4/4] Step 3 phase B (guard)"
python3 batch_tune_runner.py \
  --samples-dir "$SAMPLES_DIR" \
  --run-render \
  --render-cmd "$RENDER_CMD" \
  --preset-filter "$GUARD_FILTER" \
  --note-map-file "$NOTE_MAP" \
  --auto-note-align \
  --iterations 2 \
  --out-dir ./batch_reports/phase3_guard \
  --render-dir ./rendered_phase3_guard

echo "Done. Check batch_reports/phase2_pitch_validation, phase3_classics, and phase3_guard."

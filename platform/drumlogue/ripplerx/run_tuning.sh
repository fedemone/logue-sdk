#!/usr/bin/env bash
set -euo pipefail

# run_tuning.sh
# Convenience wrapper for RipplerX tuning smoke checks and helper generation.

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${ROOT_DIR}/../../.." && pwd)"
REPORT_JSON="${REPORT_JSON:-${REPO_ROOT}/batch_reports/batch_tuning_report.json}"
DELTA_THRESHOLD="${DELTA_THRESHOLD:-20}"
HELPER="${HELPER:-${REPO_ROOT}/batch_tuning_helper.md}"

step() {
  echo
  echo "[run_tuning] ▶ $1"
}

echo "[run_tuning] ==============================================="
echo "[run_tuning] Starting RipplerX tuning helper checks"
echo "[run_tuning] ROOT_DIR=${ROOT_DIR}"
echo "[run_tuning] REPO_ROOT=${REPO_ROOT}"
echo "[run_tuning] REPORT_JSON=${REPORT_JSON}"
echo "[run_tuning] DELTA_THRESHOLD=${DELTA_THRESHOLD}"
echo "[run_tuning] HELPER=${HELPER}"
echo "[run_tuning] ==============================================="

cd "${REPO_ROOT}"

batch_runner="$(find . -name 'batch_tune_runner.py' | head -n 1)"
if [[ -z "${batch_runner}" ]]; then
  echo "[run_tuning] ❌ Could not locate batch_tune_runner.py from ${REPO_ROOT}" >&2
  exit 1
fi
echo "[run_tuning] batch_runner=${batch_runner}"

step "Compile-check batch_tune_runner.py"
python3 -m py_compile "${batch_runner}"

echo "[run_tuning] ✅ py_compile OK"

step "Print helper preview"
python3 "${batch_runner}" --helper | sed -n '1,80p'

echo "[run_tuning] ✅ helper preview OK"

step "Write helper markdown"
python3 "${batch_runner}" --write-helper "${HELPER}"

echo "[run_tuning] ✅ helper markdown generated"

step "Validate helper file is non-empty"
test -s "${HELPER}"

echo "[run_tuning] ✅ helper file exists and is non-empty"

step "Validate git diff formatting"
git diff --check

echo "[run_tuning] ✅ git diff --check OK"

step "Optional convergence hint from batch report"
if [[ -f "${REPORT_JSON}" ]]; then
  read -r mean_score pairs <<<"$(python3 - <<'PY' "${REPORT_JSON}"
import json, sys
p = sys.argv[1]
obj = json.load(open(p))
mean_score = obj.get('mean_score')
pairs = obj.get('pairs_compared', 0)
if mean_score is None:
    print('nan', pairs)
else:
    print(mean_score, pairs)
PY
)"

  if [[ "${mean_score}" == "nan" ]]; then
    echo "[run_tuning] ⚠ report found but mean_score is null (no compared pairs)."
  else
    echo "[run_tuning] mean_score=${mean_score} over pairs=${pairs}"
    python3 - <<'PY' "${mean_score}" "${DELTA_THRESHOLD}"
import sys
mean_score = float(sys.argv[1])
threshold = float(sys.argv[2])
if mean_score > threshold:
    print("[run_tuning] ⚠ Delta is still high: please tune parameters and run a NEW render+compare cycle after reviewing results.")
else:
    print("[run_tuning] ✅ Delta is within threshold: candidate is ready for HW A/B validation.")
PY
  fi
else
  echo "[run_tuning] ℹ No batch report found at ${REPORT_JSON}; skip delta advice."
fi

echo
echo "[run_tuning] ==============================================="
echo "[run_tuning] Completed successfully"
echo "[run_tuning] ==============================================="

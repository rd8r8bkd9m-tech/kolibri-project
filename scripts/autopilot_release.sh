#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPORT_DIR="$ROOT_DIR/docs/reports"
mkdir -p "$REPORT_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_FILE="$REPORT_DIR/autopilot_release_${STAMP}.json"
GATE_REPORT_FILE="$REPORT_DIR/autopilot_quality_gate_${STAMP}.json"
SITE_URL="${KOLIBRI_SITE_URL:-https://kolibriai.ru}"
GATE_RUNS="${KOLIBRI_GATE_RUNS:-1}"
GATE_MIN_SCORE="${KOLIBRI_GATE_MIN_SCORE:-0.85}"
GATE_TIMEOUT="${KOLIBRI_GATE_TIMEOUT:-300}"

echo "[autopilot] backend deploy"
"$ROOT_DIR/scripts/deploy_backend_ubuntu.sh"

echo "[autopilot] quality gate"
python3 "$ROOT_DIR/scripts/benchmark_quality_suite.py" \
  --base-url "$SITE_URL" \
  --runs "$GATE_RUNS" \
  --min-score "$GATE_MIN_SCORE" \
  --request-timeout "$GATE_TIMEOUT" \
  --require-gates-pass | tee "$GATE_REPORT_FILE"

echo "[autopilot] frontend deploy"
"$ROOT_DIR/scripts/deploy_kolibriai.sh"

echo "[autopilot] smoke"
python3 "$ROOT_DIR/scripts/smoke_kolibriai.py" | tee "$REPORT_FILE"

echo "[autopilot] report: $REPORT_FILE"
echo "[autopilot] quality gate report: $GATE_REPORT_FILE"

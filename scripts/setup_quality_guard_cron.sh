#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${KOLIBRI_REMOTE_HOST:-ubuntu-home-wan}"
REMOTE_REPO="${KOLIBRI_REMOTE_SRV_REPO:-/srv/kolibri/repo}"
BASE_URL="${KOLIBRI_QUALITY_BASE_URL:-http://127.0.0.1:8001}"
RUNS="${KOLIBRI_QUALITY_RUNS:-2}"
MIN_SCORE="${KOLIBRI_QUALITY_MIN_SCORE:-0.85}"
REQUEST_TIMEOUT="${KOLIBRI_QUALITY_REQUEST_TIMEOUT:-300}"
MAX_P95_RATIO="${KOLIBRI_QUALITY_MAX_P95_RATIO:-1.35}"
P95_REGRESSION_MIN_DELTA_MS="${KOLIBRI_QUALITY_P95_REGRESSION_MIN_DELTA_MS:-350}"
MAX_GATE_FAIL_RUNS="${KOLIBRI_QUALITY_MAX_GATE_FAIL_RUNS:-1}"
CRON_HOUR="${KOLIBRI_QUALITY_CRON_HOUR:-3}"
CRON_MINUTE="${KOLIBRI_QUALITY_CRON_MINUTE:-17}"
ALERT_WEBHOOK="${KOLIBRI_QUALITY_ALERT_WEBHOOK:-}"
ALERT_TELEGRAM_BOT_TOKEN="${KOLIBRI_QUALITY_ALERT_TELEGRAM_BOT_TOKEN:-}"
ALERT_TELEGRAM_CHAT_ID="${KOLIBRI_QUALITY_ALERT_TELEGRAM_CHAT_ID:-}"

CRON_MARKER="kolibri-nightly-quality-guard"
GUARD_ARGS=(
  --base-url "$BASE_URL"
  --runs "$RUNS"
  --min-score "$MIN_SCORE"
  --request-timeout "$REQUEST_TIMEOUT"
  --max-gate-fail-runs "$MAX_GATE_FAIL_RUNS"
  --max-p95-regression-ratio "$MAX_P95_RATIO"
  --p95-regression-min-delta-ms "$P95_REGRESSION_MIN_DELTA_MS"
)
if [[ -n "$ALERT_WEBHOOK" ]]; then
  GUARD_ARGS+=(--alert-webhook "$ALERT_WEBHOOK")
fi
if [[ -n "$ALERT_TELEGRAM_BOT_TOKEN" ]]; then
  GUARD_ARGS+=(--alert-telegram-bot-token "$ALERT_TELEGRAM_BOT_TOKEN")
fi
if [[ -n "$ALERT_TELEGRAM_CHAT_ID" ]]; then
  GUARD_ARGS+=(--alert-telegram-chat-id "$ALERT_TELEGRAM_CHAT_ID")
fi
GUARD_ARGS_ESCAPED=""
for arg in "${GUARD_ARGS[@]}"; do
  printf -v q '%q' "$arg"
  GUARD_ARGS_ESCAPED+=" $q"
done
CRON_CMD="cd $REMOTE_REPO && /usr/bin/python3 scripts/nightly_quality_guard.py$GUARD_ARGS_ESCAPED >> /tmp/kolibri_quality_guard.log 2>&1"
CRON_CMD_B64="$(printf '%s' "$CRON_CMD" | base64 | tr -d '\n')"

echo "[quality-cron] host: $REMOTE_HOST"
echo "[quality-cron] schedule: $CRON_MINUTE $CRON_HOUR * * *"
echo "[quality-cron] repo: $REMOTE_REPO"

ssh "$REMOTE_HOST" "CRON_MINUTE='$CRON_MINUTE' CRON_HOUR='$CRON_HOUR' CRON_MARKER='$CRON_MARKER' CRON_CMD_B64='$CRON_CMD_B64' bash -s" <<'SH'
set -euo pipefail
CRON_CMD="$(printf '%s' "$CRON_CMD_B64" | base64 -d)"

tmp="$(mktemp)"
(crontab -l 2>/dev/null || true) \
  | grep -v "$CRON_MARKER" \
  | grep -v "nightly_quality_guard.py" \
  | grep -v "cd # /srv/kolibri/repo" > "$tmp"
printf '%s %s * * * %s # %s\n' "$CRON_MINUTE" "$CRON_HOUR" "$CRON_CMD" "$CRON_MARKER" >> "$tmp"
crontab "$tmp"
rm -f "$tmp"

echo "[quality-cron] installed line:"
crontab -l | grep "$CRON_MARKER"
SH

echo "[quality-cron] done"

#!/usr/bin/env bash
set -euo pipefail

API_BASE="${KOLIBRI_API_BASE:-http://127.0.0.1:8001}"
TEXT="${KOLIBRI_DEMO_TEXT:-Право определяет допустимое поведение и защищает права участников общества.}"
QUESTION="${KOLIBRI_DEMO_QUESTION:-что такое право}"
TITLE="${KOLIBRI_DEMO_TITLE:-Право — короткое определение}"
CATEGORY="${KOLIBRI_DEMO_CATEGORY:-law}"
SOURCE="${KOLIBRI_DEMO_SOURCE:-manual}"
CURL_MAX_TIME="${KOLIBRI_DEMO_CURL_MAX_TIME:-60}"
export TEXT QUESTION TITLE CATEGORY SOURCE

tmp_demo="$(mktemp)"
tmp_status="$(mktemp)"
tmp_chat="$(mktemp)"
trap 'rm -f "$tmp_demo" "$tmp_status" "$tmp_chat"' EXIT

echo "[demo] ingest + refresh"
curl -fsS \
  --max-time "$CURL_MAX_TIME" \
  -H 'content-type: application/json' \
  -d "$(python3 - <<'PY'
import json, os
payload = {
    "text": os.environ["TEXT"],
    "question": os.environ["QUESTION"],
    "title": os.environ["TITLE"],
    "source": os.environ["SOURCE"],
    "category": os.environ["CATEGORY"],
}
print(json.dumps(payload, ensure_ascii=False))
PY
)" \
  "$API_BASE/api/v1/ai/demo/learn/text" > "$tmp_demo"

echo "[demo] swarm status"
curl -fsS --max-time "$CURL_MAX_TIME" "$API_BASE/api/v1/swarm/runtime/status" > "$tmp_status"

echo "[demo] chat answer"
curl -fsS \
  --max-time "$CURL_MAX_TIME" \
  -H 'content-type: application/json' \
  -d "$(python3 - <<'PY'
import json, os
payload = {
    "message": os.environ["QUESTION"],
    "profile": "balanced",
    "persona": "assistant",
    "memory_enabled": True,
}
print(json.dumps(payload, ensure_ascii=False))
PY
)" \
  "$API_BASE/api/v1/ai/chat" > "$tmp_chat"

python3 - "$tmp_demo" "$tmp_status" "$tmp_chat" <<'PY'
import json
import sys
from pathlib import Path

demo = json.loads(Path(sys.argv[1]).read_text())
status = json.loads(Path(sys.argv[2]).read_text())
chat = json.loads(Path(sys.argv[3]).read_text())

latest_demo = status.get("latest_demo") or {}
latest_knowledge = status.get("latest_knowledge") or {}
delta = status.get("last_knowledge_refresh_delta") or {}

print("\n=== Kolibri Public Demo ===")
print(f"report: {demo.get('report','').strip()}")
print(f"chat_method: {chat.get('method')}")
print(f"chat_response: {chat.get('response','').strip()}")
print(f"documents: {latest_knowledge.get('total_documents')}")
print(f"single_hit: {((latest_knowledge.get('single') or {}).get('hit_ratio'))}")
print(f"swarm_hit: {((latest_knowledge.get('swarm_final') or {}).get('hit_ratio'))}")
print(f"swarm_vs_single_delta: {((latest_knowledge.get('comparison') or {}).get('swarm_vs_single_delta'))}")
print(f"last_documents_delta: {delta.get('documents_delta')}")
print(f"last_swarm_hit_delta: {delta.get('swarm_hit_delta')}")
if latest_demo:
    print(f"latest_demo_focus: {latest_demo.get('focus_domain')}")
PY

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL_BACKEND_DIR="$ROOT_DIR/backend/service"

REMOTE_HOST="${KOLIBRI_REMOTE_HOST:-ubuntu-home-wan}"
REMOTE_HOME_REPO="${KOLIBRI_REMOTE_HOME_REPO:-/home/ladik/kolibri-project}"
REMOTE_SRV_REPO="${KOLIBRI_REMOTE_SRV_REPO:-/srv/kolibri/repo}"

STAMP="$(date +%Y%m%d_%H%M%S)"
REMOTE_BACKUP_PATH="/tmp/kolibri_backend_service_${STAMP}.tgz"
STRICT_QUALITY_GATE="${KOLIBRI_STRICT_QUALITY_GATE:-1}"
STRICT_GATE_TIMEOUT_SEC="${KOLIBRI_STRICT_GATE_TIMEOUT_SEC:-420}"

log() {
  printf '[strict-deploy] %s\n' "$1"
}

remote_restart() {
  ssh "$REMOTE_HOST" "set -euo pipefail
have_sudo_systemctl=0
if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
  have_sudo_systemctl=1
fi

systemctl_manage() {
  if [[ \"\$have_sudo_systemctl\" -eq 1 ]]; then
    sudo -n systemctl \"\$@\"
  else
    systemctl \"\$@\"
  fi
}

unit_exists() {
  [[ -f /etc/systemd/system/kolibri-backend.service || -f /lib/systemd/system/kolibri-backend.service || -f /usr/lib/systemd/system/kolibri-backend.service ]]
}

free_port_8001() {
  for phase in term kill9; do
    for _ in \$(seq 1 20); do
      pids=\$(ss -ltnp | awk -F'pid=' '/:8001 /{split(\$2,a,\",\"); print a[1]}' | sort -u || true)
      [[ -n \"\$pids\" ]] || return 0
      for pid in \$pids; do
        [[ -n \"\$pid\" ]] || continue
        if [[ \"\$phase\" == term ]]; then
          kill \"\$pid\" 2>/dev/null || true
        else
          kill -9 \"\$pid\" 2>/dev/null || true
        fi
      done
      sleep 1
    done
  done
  pids=\$(ss -ltnp | awk -F'pid=' '/:8001 /{split(\$2,a,\",\"); print a[1]}' | sort -u || true)
  [[ -z \"\$pids\" ]]
}

kill_port_8001_snapshot() {
  pids=\$(ss -ltnp | awk -F'pid=' '/:8001 /{split(\$2,a,\",\"); print a[1]}' | sort -u || true)
  [[ -n \"\$pids\" ]] || return 0
  for pid in \$pids; do
    [[ -n \"\$pid\" ]] || continue
    kill \"\$pid\" 2>/dev/null || true
  done
  sleep 2
  for pid in \$pids; do
    [[ -n \"\$pid\" ]] || continue
    kill -0 \"\$pid\" 2>/dev/null && kill -9 \"\$pid\" 2>/dev/null || true
  done
}

manual_restart=0
if unit_exists; then
  if [[ \"\$have_sudo_systemctl\" -ge 1 ]]; then
    systemctl_manage stop kolibri-backend.service >/dev/null 2>&1 || true
    for _ in \$(seq 1 20); do
      state=\$(systemctl is-active kolibri-backend.service 2>/dev/null || true)
      [[ \"\$state\" == 'inactive' || \"\$state\" == 'failed' || -z \"\$state\" ]] && break
      sleep 1
    done
    free_port_8001
    systemctl_manage reset-failed kolibri-backend.service >/dev/null 2>&1 || true
    systemctl_manage start kolibri-backend.service >/dev/null 2>&1
  else
    kill_port_8001_snapshot
  fi
else
  manual_restart=1
fi

if [[ \"\$manual_restart\" -eq 1 ]]; then
  free_port_8001 || true
  for env_file in \
    $REMOTE_SRV_REPO/.env.backend \
    $REMOTE_SRV_REPO/.env \
    $REMOTE_HOME_REPO/.env.backend \
    $REMOTE_HOME_REPO/.env; do
    if [[ -f \"\$env_file\" ]]; then
      set -a
      . \"\$env_file\"
      set +a
    fi
  done
  cd $REMOTE_SRV_REPO
  nohup $REMOTE_SRV_REPO/.venv/bin/uvicorn backend.service.main:app --host 127.0.0.1 --port 8001 > /tmp/uvicorn_8001.log 2>&1 &
fi

ready=0
for i in \$(seq 1 45); do
  if curl -fsS http://127.0.0.1:8001/api/v1/ai/models >/dev/null 2>&1; then
    if unit_exists && [[ \"\$have_sudo_systemctl\" -ge 1 ]]; then
      systemctl is-active --quiet kolibri-backend.service || { sleep 1; continue; }
    fi
    ready=1
    break
  fi
  sleep 1
done

if [[ \"\$ready\" -ne 1 ]]; then
  echo 'backend_not_ready'
  exit 1
fi
"
}

remote_contract_smoke() {
  ssh "$REMOTE_HOST" "python3 - <<'PY'
import json
import socket
import sys
import time
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:8001'

def req(path, payload=None, timeout=25.0):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode('utf-8')
        headers['Content-Type'] = 'application/json'
    req_obj = urllib.request.Request(BASE + path, data=data, headers=headers, method='POST' if payload is not None else 'GET')
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(req_obj, timeout=timeout) as resp:
            raw = resp.read().decode('utf-8', errors='replace')
            elapsed_ms = round((time.perf_counter() - started) * 1000, 2)
            try:
                body = json.loads(raw)
            except json.JSONDecodeError:
                body = {'_raw': raw[:1000]}
            return resp.status, body, elapsed_ms
    except urllib.error.HTTPError as e:
        raw = e.read().decode('utf-8', errors='replace')
        elapsed_ms = round((time.perf_counter() - started) * 1000, 2)
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            body = {'_raw': raw[:1000]}
        return e.code, body, elapsed_ms
    except (urllib.error.URLError, TimeoutError, socket.timeout) as e:
        elapsed_ms = round((time.perf_counter() - started) * 1000, 2)
        return 0, {'error': str(e)}, elapsed_ms

def is_ok(code, body):
    return code == 200 and isinstance(body, dict)

checks = []

code, body, ms = req('/api/v1/ai/models', timeout=20.0)
checks.append({
    'name': 'models',
    'ok': is_ok(code, body),
    'status': code,
    'latency_ms': ms,
})

conv = f'strict_{int(time.time())}'
code1, body1, ms1 = req('/api/v1/ai/chat', payload={
    'message': 'Я люблю шахматы и программирование',
    'conversation_id': conv,
    'profile': 'fast',
    'time_budget_ms': 9000,
}, timeout=35.0)
checks.append({
    'name': 'chat_base',
    'ok': is_ok(code1, body1) and bool(body1.get('response')),
    'status': code1,
    'latency_ms': ms1,
})

code2, body2, ms2 = req('/api/v1/ai/chat', payload={
    'message': 'Что я люблю?',
    'conversation_id': conv,
    'profile': 'fast',
    'time_budget_ms': 9000,
}, timeout=35.0)
answer2 = str((body2 or {}).get('response', '') or '').lower()
ctx_ok = ('шахмат' in answer2) or ('программир' in answer2)
checks.append({
    'name': 'chat_context',
    'ok': is_ok(code2, body2) and ctx_ok,
    'status': code2,
    'latency_ms': ms2,
    'method': (body2 or {}).get('method', ''),
})

code3, body3, ms3 = req('/api/v1/ai/chat', payload={
    'message': 'какие новости в мире',
    'conversation_id': f'{conv}_news',
    'profile': 'fast',
    'time_budget_ms': 9000,
}, timeout=50.0)
answer3 = str((body3 or {}).get('response', '') or '').lower()
news_ok = is_ok(code3, body3) and len(answer3) >= 60 and ('мало проверенных данных' not in answer3)
checks.append({
    'name': 'chat_news',
    'ok': news_ok,
    'status': code3,
    'latency_ms': ms3,
    'method': (body3 or {}).get('method', ''),
})

report = {'checks': checks, 'all_ok': all(c['ok'] for c in checks)}
print(json.dumps(report, ensure_ascii=False, indent=2))
sys.exit(0 if report['all_ok'] else 2)
PY"
}

remote_quality_gate() {
  ssh "$REMOTE_HOST" "KOLIBRI_GATE_TIMEOUT_SEC='$STRICT_GATE_TIMEOUT_SEC' python3 - <<'PY'
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:8001'
TIMEOUT_SEC = max(60.0, float(os.getenv('KOLIBRI_GATE_TIMEOUT_SEC', '420') or 420.0))

payload = {}
body = {}
status = 0
elapsed_ms = 0.0
t0 = time.perf_counter()
try:
    req = urllib.request.Request(
        BASE + '/api/v1/ai/quality/benchmark/run',
        data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
        headers={'Content-Type': 'application/json', 'Accept': 'application/json'},
        method='POST',
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT_SEC) as resp:
        status = resp.status
        body = json.loads(resp.read().decode('utf-8'))
except urllib.error.HTTPError as exc:
    status = exc.code
    raw = exc.read().decode('utf-8', errors='replace')
    try:
        body = json.loads(raw)
    except json.JSONDecodeError:
        body = {'error': raw[:1000]}
except (urllib.error.URLError, TimeoutError, socket.timeout) as exc:
    body = {'error': str(exc)}
finally:
    elapsed_ms = round((time.perf_counter() - t0) * 1000.0, 2)

gates = body.get('gates') if isinstance(body, dict) else {}
overall_pass = bool(gates.get('overall_pass', False)) if isinstance(gates, dict) else False
report = {
    'status': status,
    'latency_ms': elapsed_ms,
    'run_id': body.get('run_id') if isinstance(body, dict) else '',
    'score': body.get('score') if isinstance(body, dict) else None,
    'pass_rate': body.get('pass_rate') if isinstance(body, dict) else None,
    'latency_p95_ms': body.get('latency_p95_ms') if isinstance(body, dict) else None,
    'gates': gates if isinstance(gates, dict) else {},
    'quality_gate_ok': bool(status == 200 and overall_pass),
}
print(json.dumps(report, ensure_ascii=False, indent=2))
sys.exit(0 if report['quality_gate_ok'] else 2)
PY"
}

remote_rollback() {
  log "rollback: restoring service backup from $REMOTE_BACKUP_PATH"
  ssh "$REMOTE_HOST" "set -euo pipefail
if [[ ! -f '$REMOTE_BACKUP_PATH' ]]; then
  echo 'rollback_backup_missing'
  exit 1
fi
tar -C '$REMOTE_SRV_REPO/backend' -xzf '$REMOTE_BACKUP_PATH'
rsync -az --delete '$REMOTE_SRV_REPO/backend/service/' '$REMOTE_HOME_REPO/backend/service/'
"
  remote_restart
}

log "preflight: local syntax check"
python3 -m py_compile \
  "$LOCAL_BACKEND_DIR/ai_engine.py" \
  "$LOCAL_BACKEND_DIR/ai_chat.py" \
  "$LOCAL_BACKEND_DIR/context_window.py"

log "remote backup: $REMOTE_BACKUP_PATH"
ssh "$REMOTE_HOST" "set -euo pipefail; tar -C '$REMOTE_SRV_REPO/backend' -czf '$REMOTE_BACKUP_PATH' service"

log "sync backend/service -> remote repos"
for target in "$REMOTE_HOME_REPO" "$REMOTE_SRV_REPO"; do
  log "  rsync -> $target/backend/service"
  rsync -az --delete "$LOCAL_BACKEND_DIR/" "$REMOTE_HOST:$target/backend/service/"
done

log "restart backend"
remote_restart

log "contract smoke"
if ! remote_contract_smoke; then
  log "contract smoke failed; rolling back"
  remote_rollback
  log "deploy failed and rolled back"
  exit 1
fi

if [[ "$STRICT_QUALITY_GATE" == "1" ]]; then
  log "quality gate benchmark"
  if ! remote_quality_gate; then
    log "quality gate failed; rolling back"
    remote_rollback
    log "deploy failed and rolled back"
    exit 1
  fi
else
  log "quality gate skipped (KOLIBRI_STRICT_QUALITY_GATE=$STRICT_QUALITY_GATE)"
fi

log "deploy success: backend healthy, contract checks and quality gate passed"

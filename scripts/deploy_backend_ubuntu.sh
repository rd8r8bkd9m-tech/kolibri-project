#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${KOLIBRI_REMOTE_HOST:-ubuntu-home-wan}"
REMOTE_HOME_REPO="${KOLIBRI_REMOTE_HOME_REPO:-/home/ladik/kolibri-project}"
REMOTE_SRV_REPO="${KOLIBRI_REMOTE_SRV_REPO:-/srv/kolibri/repo}"
REMOTE_LIVE_FORMULA_MEMORY="${KOLIBRI_REMOTE_LIVE_FORMULA_MEMORY:-/srv/kolibri/repo/data/swarm/live_formula_memory}"

LOCAL_BACKEND_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../backend/service" && pwd)"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[backend] sync changed backend service files"
for target in "$REMOTE_HOME_REPO" "$REMOTE_SRV_REPO"; do
  echo "  -> $target/backend/service"
  rsync -az --delete \
    "$LOCAL_BACKEND_DIR/" \
    "$REMOTE_HOST:$target/backend/service/"
done

echo "[backend] sync requirements"
for target in "$REMOTE_HOME_REPO" "$REMOTE_SRV_REPO"; do
  rsync -az \
    "$ROOT_DIR/requirements.txt" \
    "$REMOTE_HOST:$target/requirements.txt"
done

echo "[backend] sync C-core and formula memory files"
for target in "$REMOTE_HOME_REPO" "$REMOTE_SRV_REPO"; do
  echo "  -> $target/backend/src"
  rsync -az --delete \
    "$ROOT_DIR/backend/src/" \
    "$REMOTE_HOST:$target/backend/src/"
  echo "  -> $target/backend/include/kolibri"
  rsync -az --delete \
    "$ROOT_DIR/backend/include/kolibri/" \
    "$REMOTE_HOST:$target/backend/include/kolibri/"
  echo "  -> $target/apps"
  rsync -az --delete \
    "$ROOT_DIR/apps/" \
    "$REMOTE_HOST:$target/apps/"
  echo "  -> $target/benchmarks"
  rsync -az --delete \
    "$ROOT_DIR/benchmarks/" \
    "$REMOTE_HOST:$target/benchmarks/"
  echo "  -> $target/data/formula_domains"
  rsync -az --delete \
    "$ROOT_DIR/data/formula_domains/" \
    "$REMOTE_HOST:$target/data/formula_domains/"
  echo "  -> $target/CMakeLists.txt"
  rsync -az \
    "$ROOT_DIR/CMakeLists.txt" \
    "$REMOTE_HOST:$target/CMakeLists.txt"
done

echo "[backend] sync manual domain docs into live formula memory"
ssh "$REMOTE_HOST" "mkdir -p $REMOTE_LIVE_FORMULA_MEMORY"
rsync -az \
  --include='*/' \
  --include='*manual*.txt' \
  --include='*manual*.md' \
  --exclude='*' \
  "$ROOT_DIR/data/formula_domains/" \
  "$REMOTE_HOST:$REMOTE_LIVE_FORMULA_MEMORY/"

echo "[backend] build C binaries on remote"
ssh "$REMOTE_HOST" "set -euo pipefail
cd $REMOTE_SRV_REPO
$REMOTE_SRV_REPO/.venv/bin/pip install -r requirements.txt >/tmp/kolibri_pip_install.log 2>&1
mkdir -p build/manual
cc -std=gnu2x -O2 -march=native -Ibackend/include \
  benchmarks/kolibri_swarm_benchmark.c \
  backend/src/formula.c \
  backend/src/roy.c \
  backend/src/random.c \
  backend/src/decimal.c \
  backend/src/symbol_table.c \
  backend/src/genome.c \
  -lcrypto -lpthread -lm -o build/kolibri_swarm_benchmark >/tmp/kolibri_swarm_build.log 2>&1
cc -std=gnu2x -O2 -march=native -Ibackend/include \
  benchmarks/kolibri_swarm_knowledge_benchmark.c \
  backend/src/formula.c \
  backend/src/roy.c \
  backend/src/random.c \
  backend/src/decimal.c \
  backend/src/symbol_table.c \
  backend/src/genome.c \
  -lcrypto -lpthread -lm -o build/kolibri_swarm_knowledge_benchmark >/tmp/kolibri_swarm_knowledge_build.log 2>&1
cc -std=gnu2x -O2 -march=native -Ibackend/include \
  apps/kolibri_formula_trainer.c \
  backend/src/web_crawler.c \
  -lm -o build/kolibri_formula_trainer >/tmp/kolibri_formula_trainer_build.log 2>&1
cc -std=gnu2x -O2 -march=native -Ibackend/include -c backend/src/knowledge.c -o build/manual/knowledge.c.o >/tmp/kolibri_c_core_rebuild.log 2>&1
cc -std=gnu2x -O2 -march=native -Ibackend/include -c backend/src/formula.c -o build/manual/formula.c.o >>/tmp/kolibri_c_core_rebuild.log 2>&1
cc -std=gnu2x -O2 -march=native -Ibackend/include -c backend/src/inference.c -o build/manual/inference.c.o >>/tmp/kolibri_c_core_rebuild.log 2>&1
ar r build/libkolibri_core.a build/manual/knowledge.c.o build/manual/formula.c.o build/manual/inference.c.o >>/tmp/kolibri_c_core_rebuild.log 2>&1
ranlib build/libkolibri_core.a >>/tmp/kolibri_c_core_rebuild.log 2>&1
cc -std=gnu2x -Ibackend/include apps/kolibri_infer_cli.c build/libkolibri_core.a -lcrypto -lsqlite3 -lpthread -lm -o build/kolibri_infer_cli >/tmp/kolibri_c_infer_build.log 2>&1
test -x build/kolibri_infer_cli
test -x build/kolibri_formula_trainer
test -d data/formula_domains
mkdir -p $REMOTE_LIVE_FORMULA_MEMORY
if ! find $REMOTE_LIVE_FORMULA_MEMORY -type f \\( -name '*.txt' -o -name '*.md' \\) | grep -q .; then
  rsync -a data/formula_domains/ $REMOTE_LIVE_FORMULA_MEMORY/
fi
for env_file in $REMOTE_SRV_REPO/.env.backend $REMOTE_HOME_REPO/.env.backend; do
  if [[ -f \"\$env_file\" ]]; then
    python3 - <<'PY' \"\$env_file\" \"$REMOTE_LIVE_FORMULA_MEMORY\"
from pathlib import Path
import sys

env_path = Path(sys.argv[1])
formula_path = sys.argv[2]
lines = env_path.read_text(encoding='utf-8').splitlines()
wanted = {
    'KOLIBRI_ENABLE_C_INFERENCE': '1',
    'KOLIBRI_LIVE_FORMULA_MEMORY_PATH': formula_path,
    'KOLIBRI_FORMULA_MEMORY_PATH': formula_path,
    'KOLIBRI_KNOWLEDGE_PATH': formula_path,
    'KOLIBRI_ENABLE_SWARM_RUNTIME': '1',
    'KOLIBRI_SWARM_RUNTIME_INTERVAL_SEC': '300',
}
seen = set()
new_lines = []
for line in lines:
    if '=' in line:
        key = line.split('=', 1)[0].strip()
        if key in wanted:
            new_lines.append(f'{key}={wanted[key]}')
            seen.add(key)
            continue
    new_lines.append(line)
for key, value in wanted.items():
    if key not in seen:
        new_lines.append(f'{key}={value}')
env_path.write_text('\\n'.join(new_lines) + '\\n', encoding='utf-8')
PY
  fi
done
"

echo "[backend] restart backend service"
ssh "$REMOTE_HOST" "set -euo pipefail
pids_8001=\$(lsof -tiTCP:8001 -sTCP:LISTEN 2>/dev/null || true)
pids_8102=\$(lsof -tiTCP:8102 -sTCP:LISTEN 2>/dev/null || true)
for pid in \$pids_8001 \$pids_8102; do
  [[ -n \"\$pid\" ]] || continue
  kill \"\$pid\" 2>/dev/null || true
done
sleep 1

pids_8001=\$(lsof -tiTCP:8001 -sTCP:LISTEN 2>/dev/null || true)
pids_8102=\$(lsof -tiTCP:8102 -sTCP:LISTEN 2>/dev/null || true)
for pid in \$pids_8001 \$pids_8102; do
  [[ -n \"\$pid\" ]] || continue
  kill -9 \"\$pid\" 2>/dev/null || true
done

if systemctl list-unit-files 2>/dev/null | grep -q '^kolibri-backend.service'; then
  manual_restart=0
else
  manual_restart=1
fi

if [[ \"\${manual_restart:-0}\" -eq 1 ]]; then
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

  export KOLIBRI_ENABLE_C_INFERENCE=1
  export KOLIBRI_LIVE_FORMULA_MEMORY_PATH=$REMOTE_LIVE_FORMULA_MEMORY
  export KOLIBRI_FORMULA_MEMORY_PATH=$REMOTE_LIVE_FORMULA_MEMORY
  export KOLIBRI_KNOWLEDGE_PATH=$REMOTE_LIVE_FORMULA_MEMORY
  export KOLIBRI_ENABLE_SWARM_RUNTIME=1
  export KOLIBRI_SWARM_RUNTIME_INTERVAL_SEC=300

  pid_8001=\$(lsof -tiTCP:8001 -sTCP:LISTEN 2>/dev/null | head -n1 || true)
  if [[ -n \"\${pid_8001:-}\" ]]; then kill \"\$pid_8001\" || true; fi

  cd $REMOTE_SRV_REPO
  nohup $REMOTE_SRV_REPO/.venv/bin/uvicorn backend.service.main:app --host 127.0.0.1 --port 8001 > /tmp/uvicorn_8001.log 2>&1 &
fi

ready=0
for i in \$(seq 1 40); do
  if curl -fsS http://127.0.0.1:8001/api/v1/ai/models >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
done

if [[ \"\$ready\" -ne 1 ]]; then
  echo '[backend] ERROR: service did not become ready on :8001'
  systemctl status kolibri-backend.service --no-pager -l || true
  journalctl -u kolibri-backend.service -n 120 --no-pager || true
  exit 1
fi

ss -ltnp | egrep ':8001|:8102' || true
"

echo "[backend] smoke checks"
ssh "$REMOTE_HOST" "set -euo pipefail
truncate_print() {
  local max_len=\"\$1\"
  local value=\"\$2\"
  if [[ \${#value} -le \$max_len ]]; then
    printf '%s\n' \"\$value\"
  else
    printf '%s\n' \"\${value:0:max_len}\"
  fi
}

echo '- /api/v1/ai/models'
resp=\$(curl -m 60 -sS http://127.0.0.1:8001/api/v1/ai/models)
truncate_print 260 \"\$resp\"

echo '- /api/v1/ai/chat'
resp=\$(curl -m 180 -sS -X POST http://127.0.0.1:8001/api/v1/ai/chat -H 'Content-Type: application/json' -d '{\"message\":\"Проверка backend\",\"conversation_id\":\"backend-smoke\"}')
truncate_print 260 \"\$resp\"

echo '- /api/v1/ai/chat (c-core formula)'
resp=\$(curl -m 180 -sS -X POST http://127.0.0.1:8001/api/v1/ai/chat -H 'Content-Type: application/json' -d '{\"message\":\"что такое математика\",\"conversation_id\":\"backend-c-core-smoke\"}')
truncate_print 260 \"\$resp\"

echo '- /api/v1/ai/chat/stream'
resp=\$(curl -m 180 -sS -N -X POST http://127.0.0.1:8001/api/v1/ai/chat/stream -H 'Content-Type: application/json' -d '{\"message\":\"Проверка stream\",\"conversation_id\":\"backend-smoke-stream\"}')
truncate_print 220 \"\$resp\"

echo '- /api/v1/swarm/runtime/status'
resp=\$(curl -m 180 -sS http://127.0.0.1:8001/api/v1/swarm/runtime/status)
truncate_print 260 \"\$resp\"

echo '- /api/v1/swarm/runtime/ingest/text'
resp=\$(curl -m 180 -sS -X POST http://127.0.0.1:8001/api/v1/swarm/runtime/ingest/text -H 'Content-Type: application/json' -d '{\"title\":\"Автодеплой Колибри\",\"source\":\"deploy-smoke\",\"category\":\"deploy\",\"text\":\"Kolibri обучается на входящих данных и хранит знания в живой формульной памяти.\"}')
truncate_print 260 \"\$resp\"

echo '- /api/v1/ai/imagine'
resp=\$(curl -m 240 -sS -X POST http://127.0.0.1:8001/api/v1/ai/imagine -H 'Content-Type: application/json' -d '{\"prompt\":\"Проверка imagine\",\"style\":\"Фотореализм\",\"aspect\":\"1:1\",\"quality\":\"high\"}')
truncate_print 220 \"\$resp\"
"

echo "[backend] done"

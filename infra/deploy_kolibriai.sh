#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRONT_DIR="$ROOT_DIR/frontend"
ARCHIVE="/tmp/kolibriai_front_dist.tar"
REMOTE_HOST="${KOLIBRI_REMOTE_HOST:-ubuntu-home-wan}"
REMOTE_WEBROOT="${KOLIBRI_REMOTE_WEBROOT:-/var/www/kolibri}"
SITE_URL="${KOLIBRI_SITE_URL:-https://kolibriai.ru}"

echo "[deploy] build frontend"
echo "[deploy] build wasm"
cd "$ROOT_DIR"
"$ROOT_DIR/scripts/build_wasm.sh"

echo "[deploy] build frontend"
cd "$FRONT_DIR"
npm run build

echo "[deploy] pack dist"
COPYFILE_DISABLE=1 tar -C "$FRONT_DIR/dist" -cf "$ARCHIVE" .

echo "[deploy] upload archive -> $REMOTE_HOST"
scp "$ARCHIVE" "$REMOTE_HOST:/tmp/kolibriai_front_dist.tar"

echo "[deploy] extract to $REMOTE_WEBROOT"
ssh "$REMOTE_HOST" "set -euo pipefail; \
  docker run --rm \
    -v $REMOTE_WEBROOT:/target \
    -v /tmp/kolibriai_front_dist.tar:/tmp/dist.tar \
    alpine sh -lc 'rm -rf /target/* /target/.[!.]* /target/..?* 2>/dev/null || true; \
                    tar -xf /tmp/dist.tar -C /target; \
                    find /target -name \"._*\" -type f -delete; \
                    chown -R 33:33 /target'"

echo "[deploy] smoke checks"
echo "  - frontend bundle"
ssh "$REMOTE_HOST" "curl -s $SITE_URL | sed -n 's/.*\\(assets\\/index-[A-Za-z0-9_-]*\\.js\\).*/\\1/p' | head -n1"
echo "  - ai models endpoint (primary/fallback)"
ssh "$REMOTE_HOST" "if curl -fsS $SITE_URL/api/v1/ai/models >/dev/null 2>&1; then \
  resp=\$(curl -m 60 -sS $SITE_URL/api/v1/ai/models); printf '%s\n' \"\${resp:0:220}\"; \
else \
  resp=\$(curl -m 60 -sS $SITE_URL/api/v1/model/stats); printf '%s\n' \"\${resp:0:220}\"; \
fi"
echo "  - ai chat endpoint"
ssh "$REMOTE_HOST" "SMOKE_ID=\$(date +%s); resp=\$(curl -m 180 -sS -X POST $SITE_URL/api/v1/ai/chat \
  -H 'Content-Type: application/json' \
  -d '{\"message\":\"Проверка после деплоя\",\"conversation_id\":\"deploy-smoke-'\"\$SMOKE_ID\"'\",\"client_id\":\"deploy-smoke-'\"\$SMOKE_ID\"'\"}'); \
  printf '%s\n' \"\${resp:0:260}\""
echo "  - ai imagine endpoint"
ssh "$REMOTE_HOST" "resp=\$(curl -m 240 -sS -X POST $SITE_URL/api/v1/ai/imagine \
  -H 'Content-Type: application/json' \
  -d '{\"prompt\":\"Проверка imagine\",\"style\":\"Фотореализм\",\"aspect\":\"1:1\",\"quality\":\"high\"}'); \
  printf '%s\n' \"\${resp:0:260}\""

echo "[deploy] done"

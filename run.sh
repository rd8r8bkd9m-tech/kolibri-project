#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB="$ROOT/web"
BACKEND_PORT="${KOLIBRI_BACKEND_PORT:-8001}"
FRONTEND_PORT="${KOLIBRI_FRONTEND_PORT:-3000}"
BACKEND_LOG="${KOLIBRI_BACKEND_LOG:-/tmp/kolibri_backend.log}"
FRONTEND_LOG="${KOLIBRI_FRONTEND_LOG:-/tmp/kolibri_frontend.log}"

export PATH="/opt/homebrew/bin:/opt/homebrew/opt/sqlite/bin:${PATH}"

backend_pid=""
frontend_pid=""

cleanup() {
  if [[ -n "${frontend_pid}" ]] && kill -0 "${frontend_pid}" 2>/dev/null; then
    kill "${frontend_pid}" 2>/dev/null || true
  fi
  if [[ -n "${backend_pid}" ]] && kill -0 "${backend_pid}" 2>/dev/null; then
    kill "${backend_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

wait_for_http() {
  local url="$1"
  local label="$2"
  local attempts="${3:-40}"

  for _ in $(seq 1 "$attempts"); do
    if curl -fsS --max-time 2 "$url" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.25
  done

  echo "❌ ${label} не отвечает: ${url}"
  return 1
}

free_port() {
  local port="$1"
  local pattern="$2"

  if lsof -iTCP:"$port" -sTCP:LISTEN -Pn >/dev/null 2>&1; then
    pkill -f "$pattern" 2>/dev/null || true
    sleep 0.5
  fi
}

build_backend_if_needed() {
  local target="$ROOT/build/kolibri_http_server"

  if [[ ! -x "$target" || "$ROOT/core/kolibri_http_server.c" -nt "$target" ]]; then
    echo "🔧 Сборка C-core HTTP server..."
    cmake -S "$ROOT" -B "$ROOT/build" -DKOLIBRI_ENABLE_TESTS=OFF >/tmp/kolibri_cmake_configure.log
    cmake --build "$ROOT/build" --target kolibri_http_server -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
      >/tmp/kolibri_cmake_build.log
  fi

  echo "$target"
}

echo "🐦 Kolibri AI: быстрый старт полного C-core + web shell"

free_port "$BACKEND_PORT" "kolibri_http|kolibri_http_server"
free_port "$FRONTEND_PORT" "node .*web/server.cjs|vite --host .*${FRONTEND_PORT}"

backend_bin="$(build_backend_if_needed)"

echo "📦 Сборка web frontend..."
npm --prefix "$WEB" run build >/tmp/kolibri_web_build.log

echo "🧠 Запуск C-core на :${BACKEND_PORT}..."
cd "$ROOT"
"$backend_bin" "$BACKEND_PORT" "$WEB/dist" >"$BACKEND_LOG" 2>&1 &
backend_pid=$!
wait_for_http "http://127.0.0.1:${BACKEND_PORT}/api/v1/health" "Backend"

echo "🎨 Запуск web shell на :${FRONTEND_PORT}..."
cd "$WEB"
KOLIBRI_FRONTEND_PORT="$FRONTEND_PORT" KOLIBRI_BACKEND_PORT="$BACKEND_PORT" node server.cjs >"$FRONTEND_LOG" 2>&1 &
frontend_pid=$!
wait_for_http "http://127.0.0.1:${FRONTEND_PORT}/" "Frontend"
wait_for_http "http://127.0.0.1:${FRONTEND_PORT}/api/v1/health" "Frontend API proxy"

echo ""
echo "============================================================"
echo "  🐦 Kolibri AI готов"
echo "============================================================"
echo "  Chat:     http://127.0.0.1:${FRONTEND_PORT}"
echo "  C-core:   http://127.0.0.1:${BACKEND_PORT}"
echo "  Health:   http://127.0.0.1:${FRONTEND_PORT}/api/v1/health"
echo "  Logs:     $BACKEND_LOG"
echo "            $FRONTEND_LOG"
echo "============================================================"
echo "Нажмите Ctrl+C для остановки."

while true; do
  if ! kill -0 "$backend_pid" 2>/dev/null; then
    echo "❌ Backend завершился. Последние строки лога:"
    tail -80 "$BACKEND_LOG" || true
    exit 1
  fi
  if ! kill -0 "$frontend_pid" 2>/dev/null; then
    echo "❌ Frontend завершился. Последние строки лога:"
    tail -80 "$FRONTEND_LOG" || true
    exit 1
  fi
  sleep 2
done

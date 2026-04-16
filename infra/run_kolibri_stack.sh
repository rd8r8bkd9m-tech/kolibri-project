#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PID_FILE="$ROOT_DIR/.kolibri/knowledge_server.pid"
LOG_DIR="$ROOT_DIR/logs"
KNOWLEDGE_LOG="$LOG_DIR/knowledge_server.log"
PORT=${KOLIBRI_KNOWLEDGE_PORT:-8000}

mkdir -p "$LOG_DIR"
mkdir -p "$ROOT_DIR/.kolibri"

function ensure_port_free() {
  if lsof -ti :"$PORT" >/dev/null 2>&1; then
    local pids
    pids="$(lsof -ti :"$PORT")"
    echo "[kolibri-stack] Останавливаю процессы, занявшие порт $PORT: $pids"
    kill -9 $pids 2>/dev/null || true
    sleep 1
  fi
}

function stop_knowledge_server() {
  if [[ -f "$PID_FILE" ]]; then
    local existing_pid
    existing_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "$existing_pid" ]]; then
      if kill -0 "$existing_pid" 2>/dev/null; then
        echo "[kolibri-stack] Останавливаю прошлый knowledge_server (PID $existing_pid)"
        kill "$existing_pid" 2>/dev/null || true
        wait "$existing_pid" 2>/dev/null || true
      fi
    fi
    rm -f "$PID_FILE"
  fi
}

function start_knowledge_server() {
  echo "[kolibri-stack] Запускаю kolibri_knowledge_server на порту $PORT"
  "$ROOT_DIR/build/kolibri_knowledge_server" --port "$PORT" \
    >"$KNOWLEDGE_LOG" 2>&1 &
  local pid=$!
  echo "$pid" > "$PID_FILE"
  sleep 1
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "[kolibri-stack] Не удалось запустить kolibri_knowledge_server. См. $KNOWLEDGE_LOG" >&2
    exit 1
  fi
  echo "[kolibri-stack] kolibri_knowledge_server (PID $pid) слушает http://127.0.0.1:$PORT"
}

function cleanup() {
  local status=$?
  if [[ -f "$PID_FILE" ]]; then
    local pid
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
  fi
  exit "$status"
}

trap cleanup EXIT INT TERM

stop_knowledge_server
ensure_port_free

echo "[kolibri-stack] Конфигурирую CMake"
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"

echo "[kolibri-stack] Собираю C-компоненты"
cmake --build "$ROOT_DIR/build"

echo "[kolibri-stack] Собираю kolibri.wasm"
"$ROOT_DIR/scripts/build_wasm.sh"

start_knowledge_server

echo "[kolibri-stack] Запускаю Vite preview (Ctrl+C для остановки)"
cd "$ROOT_DIR/frontend"
# Для preview Vite не проксирует запросы, поэтому передаём абсолютный эндпоинт через VITE_ переменную
VITE_KNOWLEDGE_API="http://127.0.0.1:$PORT/api/knowledge/search" npm run preview

#!/bin/bash
# ═══════════════════════════════════════════════════════
#  Kolibri OS — Единый скрипт запуска
#  Backend (FastAPI) + Frontend (Vite)
#  Использование: bash start.sh       (запуск)
#                 bash start.sh stop   (остановка)
# ═══════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${KOLIBRI_ROOT:-$SCRIPT_DIR}"
DEFAULT_BACKEND_PORT="${KOLIBRI_BACKEND_PORT:-8001}"
DEFAULT_FRONTEND_PORT="${KOLIBRI_FRONTEND_PORT:-3000}"

LOGDIR="$ROOT/logs"
RUNDIR="$ROOT/.run"
BACKEND_PIDFILE="$RUNDIR/backend.pid"
FRONTEND_PIDFILE="$RUNDIR/frontend.pid"

mkdir -p "$LOGDIR" "$RUNDIR"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

listener_pid() {
    lsof -nP -tiTCP:"$1" -sTCP:LISTEN 2>/dev/null | head -n 1 || true
}

port_busy() {
    [[ -n "$(listener_pid "$1")" ]]
}

find_free_port() {
    local start_port="$1"
    local port="$start_port"
    while port_busy "$port"; do
        port=$((port + 1))
        if [[ "$port" -gt $((start_port + 50)) ]]; then
            echo ""
            return 1
        fi
    done
    echo "$port"
}

is_alive() {
    local pid="$1"
    kill -0 "$pid" 2>/dev/null
}

stop_pidfile() {
    local pidfile="$1"
    if [[ -f "$pidfile" ]]; then
        local pid
        pid="$(cat "$pidfile" 2>/dev/null || true)"
        if [[ -n "$pid" ]] && is_alive "$pid"; then
            kill "$pid" 2>/dev/null || true
            sleep 1
            is_alive "$pid" && kill -9 "$pid" 2>/dev/null || true
        fi
        rm -f "$pidfile"
    fi
}

stop_all() {
    echo -e "${CYAN}[Kolibri]${NC} Останавливаю процессы, запущенные start.sh..."
    stop_pidfile "$BACKEND_PIDFILE"
    stop_pidfile "$FRONTEND_PIDFILE"
}

detect_python() {
    local candidates=(
        "$ROOT/.venv312/bin/python"
        "$ROOT/.venv/bin/python"
        "python3"
        "python"
    )
    local py
    for py in "${candidates[@]}"; do
        if ! command -v "$py" >/dev/null 2>&1; then
            continue
        fi
        if "$py" -c "import uvicorn" >/dev/null 2>&1; then
            echo "$py"
            return 0
        fi
    done
    return 1
}

ensure_wasm_ready() {
    local wasm_file="$ROOT/build/wasm/kolibri.wasm"
    local wasm_info="$ROOT/build/wasm/kolibri.wasm.txt"

    echo -e "${CYAN}[Kolibri]${NC} Проверяю WebAssembly (kolibri.wasm)..."
    if [[ ! -x "$ROOT/scripts/build_wasm.sh" ]]; then
        echo -e "${RED}[ERROR] Не найден исполняемый скрипт: $ROOT/scripts/build_wasm.sh${NC}"
        return 1
    fi

    (cd "$ROOT" && ./scripts/build_wasm.sh)

    if [[ ! -f "$wasm_file" ]]; then
        echo -e "${RED}[ERROR] Не найден $wasm_file после сборки WASM.${NC}"
        return 1
    fi

    if [[ -f "$wasm_info" ]] && grep -qi "заглушка" "$wasm_info"; then
        echo -e "${RED}[ERROR] Обнаружена заглушка WASM. Деградационный режим отключён.${NC}"
        echo "Исправьте окружение сборки (Emscripten/Docker) и пересоберите полноценный kolibri.wasm."
        return 1
    fi
}

backend_ready() {
    curl -fsS "http://127.0.0.1:${BACKEND_PORT}/api/health" >/dev/null 2>&1
}

frontend_ready() {
    curl -fsS "http://127.0.0.1:${FRONTEND_PORT}/" >/dev/null 2>&1
}

# ─── Если передан аргумент stop ───
if [[ "${1:-}" == "stop" ]]; then
    stop_all
    echo -e "${GREEN}[  OK  ] Kolibri остановлен.${NC}"
    exit 0
fi

if [[ ! -d "$ROOT/backend" ]] || [[ ! -d "$ROOT/frontend" ]]; then
    echo -e "${RED}[ERROR] Некорректный ROOT: ${ROOT}${NC}"
    echo "Укажите путь через KOLIBRI_ROOT=/path/to/kolibri-project"
    exit 1
fi

PYTHON_BIN="$(detect_python || true)"
if [[ -z "$PYTHON_BIN" ]]; then
    echo -e "${RED}[ERROR] Не найден Python с установленным модулем uvicorn.${NC}"
    echo "Установите зависимости или активируйте venv в корне проекта."
    exit 1
fi

echo ""
echo -e "${CYAN}╔═══════════════════════════════════════╗${NC}"
echo -e "${CYAN}║     🐦 Kolibri OS — Запуск            ║${NC}"
echo -e "${CYAN}╚═══════════════════════════════════════╝${NC}"
echo ""

stop_all

BACKEND_PORT="$(find_free_port "$DEFAULT_BACKEND_PORT")"
FRONTEND_PORT="$(find_free_port "$DEFAULT_FRONTEND_PORT")"

if [[ -z "$BACKEND_PORT" ]] || [[ -z "$FRONTEND_PORT" ]]; then
    echo -e "${RED}[ERROR] Не удалось подобрать свободные порты.${NC}"
    exit 1
fi

if [[ "$BACKEND_PORT" != "$DEFAULT_BACKEND_PORT" ]]; then
    echo -e "${YELLOW}[Kolibri]${NC} Порт $DEFAULT_BACKEND_PORT занят, backend будет на :$BACKEND_PORT"
fi
if [[ "$FRONTEND_PORT" != "$DEFAULT_FRONTEND_PORT" ]]; then
    echo -e "${YELLOW}[Kolibri]${NC} Порт $DEFAULT_FRONTEND_PORT занят, frontend будет на :$FRONTEND_PORT"
fi

ensure_wasm_ready

# ─── Backend ───
echo -e "${CYAN}[Kolibri]${NC} Запускаю Backend на :$BACKEND_PORT ..."
cd "$ROOT"
PYTHONPATH="$ROOT:${PYTHONPATH:-}" \
KOLIBRI_PROJECT_ROOT="$ROOT" \
KOLIBRI_RESPONSE_MODE=script \
KOLIBRI_AUTH_ENABLED=0 \
nohup "$PYTHON_BIN" -m uvicorn backend.service.main:app \
    --host 127.0.0.1 --port "$BACKEND_PORT" \
    > "$LOGDIR/backend.log" 2>&1 &
BPID=$!
echo "$BPID" > "$BACKEND_PIDFILE"

# ─── Frontend ───
echo -e "${CYAN}[Kolibri]${NC} Запускаю Frontend на :$FRONTEND_PORT ..."
cd "$ROOT/frontend"
KNOWLEDGE_API="http://127.0.0.1:${BACKEND_PORT}" \
nohup node node_modules/.bin/vite \
    --host 127.0.0.1 --port "$FRONTEND_PORT" --strictPort \
    > "$LOGDIR/frontend.log" 2>&1 &
FPID=$!
echo "$FPID" > "$FRONTEND_PIDFILE"
cd "$ROOT"

# ─── Ждём запуска ───
echo -e "${CYAN}[Kolibri]${NC} Жду запуска (до 60с)..."
for i in $(seq 1 60); do
    B=0
    F=0
    backend_ready && B=1
    frontend_ready && F=1
    if [[ $B -eq 1 && $F -eq 1 ]]; then
        break
    fi
    if (( i % 5 == 0 )); then
        echo -ne "  ${i}с..."
        [[ $B -eq 1 ]] && echo -ne " backend✅" || echo -ne " backend⏳"
        [[ $F -eq 1 ]] && echo -ne " frontend✅" || echo -ne " frontend⏳"
        echo ""
    fi
    sleep 1
done

# ─── Результат ───
echo ""
echo "═══════════════════════════════════════"
if backend_ready; then
    echo -e "${GREEN}  ✅ Backend:  http://localhost:${BACKEND_PORT}${NC}  (PID ${BPID})"
else
    echo -e "${RED}  ❌ Backend:  НЕ ЗАПУСТИЛСЯ${NC}"
    echo "     Лог: tail -30 $LOGDIR/backend.log"
fi

if frontend_ready; then
    echo -e "${GREEN}  ✅ Frontend: http://localhost:${FRONTEND_PORT}${NC}  (PID ${FPID})"
else
    echo -e "${RED}  ❌ Frontend: НЕ ЗАПУСТИЛСЯ${NC}"
    echo "     Лог: tail -30 $LOGDIR/frontend.log"
fi

echo ""
echo -e "  API docs: http://localhost:${BACKEND_PORT}/docs"
echo -e "  Логи: backend=$LOGDIR/backend.log, frontend=$LOGDIR/frontend.log"
echo -e "  Остановить: ${CYAN}bash start.sh stop${NC}"
echo "═══════════════════════════════════════"

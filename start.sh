#!/bin/bash
# ═══════════════════════════════════════════════════════
#  Kolibri OS — Единый скрипт запуска
#  Backend (FastAPI :8001) + Frontend (Vite :3000)
#  Использование: bash start.sh       (запуск)
#                 bash start.sh stop   (остановка)
# ═══════════════════════════════════════════════════════
ROOT="/workspaces/kolibri-project"
BACKEND_PORT=8001
FRONTEND_PORT=3000
LOGDIR="$ROOT/logs"
mkdir -p "$LOGDIR"

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

# ─── Остановка ───
stop_all() {
    echo -e "${CYAN}[Kolibri]${NC} Останавливаю старые процессы..."
    kill -9 $(lsof -t -i:$BACKEND_PORT) 2>/dev/null || true
    kill -9 $(lsof -t -i:$FRONTEND_PORT) 2>/dev/null || true
    pkill -9 -f "uvicorn backend" 2>/dev/null || true
    pkill -9 -f "node.*vite.*3000" 2>/dev/null || true
    sleep 2
}

# ─── Если передан аргумент stop ───
if [[ "${1:-}" == "stop" ]]; then
    stop_all
    echo -e "${GREEN}[  OK  ] Kolibri остановлен.${NC}"
    exit 0
fi

# ─── Проверка порта (через lsof) ───
check_port() {
    lsof -i:"$1" -sTCP:LISTEN >/dev/null 2>&1
}

echo ""
echo -e "${CYAN}╔═══════════════════════════════════════╗${NC}"
echo -e "${CYAN}║     🐦 Kolibri OS — Запуск            ║${NC}"
echo -e "${CYAN}╚═══════════════════════════════════════╝${NC}"
echo ""

stop_all

# ─── Backend ───
echo -e "${CYAN}[Kolibri]${NC} Запускаю Backend на :$BACKEND_PORT ..."
cd "$ROOT"
PYTHONPATH="$ROOT:${PYTHONPATH:-}" \
KOLIBRI_RESPONSE_MODE=script \
nohup python3 -m uvicorn backend.service.main:app \
    --host 0.0.0.0 --port $BACKEND_PORT \
    > "$LOGDIR/backend.log" 2>&1 &
BPID=$!

# ─── Frontend ───
echo -e "${CYAN}[Kolibri]${NC} Запускаю Frontend на :$FRONTEND_PORT ..."
cd "$ROOT/frontend"
KOLIBRI_SKIP_WASM_AUTOBUILD=1 \
KOLIBRI_ALLOW_WASM_STUB=1 \
nohup node node_modules/.bin/vite \
    --host 0.0.0.0 --port $FRONTEND_PORT \
    > "$LOGDIR/frontend.log" 2>&1 &
FPID=$!
cd "$ROOT"

# ─── Ждём запуска ───
echo -e "${CYAN}[Kolibri]${NC} Жду запуска (до 60с)..."
for i in $(seq 1 60); do
    B=0; F=0
    check_port $BACKEND_PORT  && B=1
    check_port $FRONTEND_PORT && F=1
    if [[ $B -eq 1 && $F -eq 1 ]]; then break; fi
    # Прогресс каждые 5 секунд
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
if check_port $BACKEND_PORT; then
    echo -e "${GREEN}  ✅ Backend:  http://localhost:$BACKEND_PORT${NC}  (PID $BPID)"
else
    echo -e "${RED}  ❌ Backend:  НЕ ЗАПУСТИЛСЯ${NC}"
    echo "     Лог: tail -30 $LOGDIR/backend.log"
fi
if check_port $FRONTEND_PORT; then
    echo -e "${GREEN}  ✅ Frontend: http://localhost:$FRONTEND_PORT${NC}  (PID $FPID)"
else
    echo -e "${RED}  ❌ Frontend: НЕ ЗАПУСТИЛСЯ${NC}"
    echo "     Лог: tail -30 $LOGDIR/frontend.log"
fi
echo ""
echo -e "  API docs: http://localhost:$BACKEND_PORT/docs"
echo -e "  Остановить: ${CYAN}bash start.sh stop${NC}"
echo "═══════════════════════════════════════"

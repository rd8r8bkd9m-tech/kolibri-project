#!/usr/bin/env bash
# Запуск 15 узлов Kolibri swarm кластера с последовательными задержками

PROJECT_ROOT="/Users/kolibri/Desktop/kolibri-project"
VENV_BIN="$PROJECT_ROOT/.venv/bin/activate"
NODES_COUNT=15
BASE_PORT=8001

echo "🚀 ЗАПУСК KOLIBRI SWARM КЛАСТЕРА ($NODES_COUNT узлов)"
echo "=================================================="

# Активируем виртуальное окружение
source "$VENV_BIN" 2>/dev/null || {
    echo "❌ Не найдено venv, создаём..."
    python3 -m venv "$PROJECT_ROOT/.venv" >/dev/null 2>&1
    source "$VENV_BIN"
    pip install -q --upgrade pip fastapi uvicorn httpx requests >/dev/null 2>&1
}

cd "$PROJECT_ROOT" || exit 1

# Удаляем старые PID файлы
rm -f /tmp/kolibri_nodes_*.pid

# Запускаем узлы последовательно с задержками
for i in $(seq 0 $((NODES_COUNT - 1))); do
    PORT=$((BASE_PORT + i))
    # Генерируем имя узла от node_a до node_o (для 15 узлов)
    NODE_ID=$(printf "node_%s" "$(printf \\$(printf '%03o' $((97 + i))))")
    
    # Запускаем узел в фоне
    ( KOLIBRI_NODE_ID="$NODE_ID" \
      KOLIBRI_PORT="$PORT" \
      KOLIBRI_ENABLE_BACKGROUND_LEARNING=1 \
        nohup python3 -m uvicorn backend.service.main:app \
            --host 0.0.0.0 \
            --port "$PORT" \
            --log-level error \
            > /dev/null 2>&1 & 
      echo $! > /tmp/kolibri_nodes_${PORT}.pid
    ) &
    
    if [ $((i % 5)) -eq 0 ] && [ $i -gt 0 ]; then
        echo "  ✓ Запущено $i узлов..."
        sleep 1
    fi
    
    # Небольшая задержка между запусками
    sleep 0.3
done

echo ""
echo "✅ Запущено $NODES_COUNT узлов"
echo "📊 Диапазон портов: $BASE_PORT - $((BASE_PORT + NODES_COUNT - 1))"
echo ""

# Даём время на инициализацию
sleep 5

# Проверяем статус
echo "🔍 ПРОВЕРКА СТАТУСА:"
READY=0
for port in $(seq $BASE_PORT $((BASE_PORT + NODES_COUNT - 1))); do
    if timeout 1 curl -s "http://localhost:$port/api/v1/learning/status" >/dev/null 2>&1; then
        READY=$((READY + 1))
    fi
done

echo "  🟢 Готовых узлов: $READY/$NODES_COUNT"

echo ""
echo "📡 СИНХРОНИЗАЦИЯ АКТИВИРОВАНА:"
echo "  • Период: каждые 30-60 сек"
echo "  • Обмен: лучшие паттерны и формулы"
echo "  • Топология: кольцо + 4 случайных пира"
echo ""
echo "✨ Роевой кластер готов!"

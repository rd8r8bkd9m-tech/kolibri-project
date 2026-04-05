#!/usr/bin/env bash
# ✨ ОПТИМИЗИРОВАН: Запуск 50 узлов с партиями, лайт-режимом, мониторингом памяти
# OLD: Запуск 50 узлов Kolibri swarm кластера

PROJECT_ROOT="/Users/kolibri/Desktop/kolibri-project"
VENV_BIN="$PROJECT_ROOT/.venv/bin/activate"
NODES_COUNT=50
BATCH_SIZE=5              # Запускаем партиями (меньше = медленнее но безопаснее)
BATCH_DELAY=2             # Пауза между партиями (сек)
BASE_PORT=8001
MAX_MEMORY_PCT=80         # Стоп если памяти > 80%

echo "🚀 ОПТИМИЗИРОВАННЫЙ ЗАПУСК KOLIBRI SWARM (50 узлов)"
echo "====================================================="
echo "  Тактика: партии x${BATCH_SIZE}, ${BATCH_DELAY}s пауза, макс $MAX_MEMORY_PCT% RAM"

# Активируем виртуальное окружение
source "$VENV_BIN" || {
    echo "❌ Не найдено venv, создаём..."
    python3 -m venv "$PROJECT_ROOT/.venv"
    source "$VENV_BIN"
    pip install -q --upgrade pip fastapi uvicorn httpx requests
}

cd "$PROJECT_ROOT" || exit 1
rm -f /tmp/kolibri_nodes_*.pid

# Функция мониторинга памяти (процент используемой)
get_memory_usage() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS: used = active + wired
        local pages_active=$(vm_stat | grep "Pages active:" | awk '{print $3}' | tr -d '.')
        local pages_wired=$(vm_stat | grep "Pages wired down:" | awk '{print $4}' | tr -d '.')
        local used_mb=$(( (pages_active + pages_wired) / 256 ))
        local total_mb=$(( $(sysctl -n hw.memsize) / 1024 / 1024 ))
        echo $(( used_mb * 100 / total_mb ))
    else
        free | grep Mem | awk '{print ($3/$2)*100}' | cut -d. -f1
    fi
}

# Главный цикл: запуск партиями
started=0
for batch_num in $(seq 0 $((BATCH_SIZE-1)) $((NODES_COUNT-1))); do
    batch_end=$(( batch_num + BATCH_SIZE - 1 ))
    [ $batch_end -ge $NODES_COUNT ] && batch_end=$((NODES_COUNT-1))
    
    echo "  📦 Партия узлов [$batch_num..$batch_end]..."
    
    for i in $(seq $batch_num $batch_end); do
        PORT=$((BASE_PORT + i))
        NODE_ID="node_$(printf '%02x' $i)"
        
        # ЛАЙТ-РЕЖИМ: первые 5 узлов обучаются, остальные только синхронизируются
        ENABLE_LEARNING=$([ $i -lt 5 ] && echo 1 || echo 0)
        
        (
            export KOLIBRI_NODE_ID="$NODE_ID"
            export KOLIBRI_PORT="$PORT"
            export KOLIBRI_ENABLE_CONTINUOUS_LEARNING="$ENABLE_LEARNING"
            export KOLIBRI_ENABLE_BACKGROUND_LEARNING="$ENABLE_LEARNING"
            export KOLIBRI_ENABLE_SWARM_RUNTIME=1
            
            nohup python3 -m uvicorn backend.service.main:app \
                --host 127.0.0.1 \
                --port "$PORT" \
                --workers 1 \
                --log-level error \
                > /tmp/kolibri_node_${PORT}.log 2>&1 &
            echo $! > /tmp/kolibri_nodes_${PORT}.pid
        ) &
        
        sleep 0.15
    done
    
    sleep "$BATCH_DELAY"
    started=$((i + 1))
    
    # Проверяем память
    mem=$(get_memory_usage)
    echo "    💾 Использование памяти: $mem%"
    
    if [ "$mem" -gt "$MAX_MEMORY_PCT" ]; then
        echo "    ⚠️  ВНИМАНИЕ: Память на пределе ($mem%) — останавливаем запуск"
        break
    fi
done

echo ""
echo "✅ Запущено $started узлов (max $NODES_COUNT)"
echo "📊 Порты: $BASE_PORT - $((BASE_PORT + started - 1))"
echo ""

# Ждём инициализации
sleep 6

echo "🔍 ПРОВЕРКА ГОТОВНОСТИ:"
READY=0
for port in $(seq $BASE_PORT $((BASE_PORT + started - 1))); do
    if timeout 1 curl -s "http://127.0.0.1:$port/api/v1/learning/status" >/dev/null 2>&1; then
        READY=$((READY + 1))
    fi
done

echo "  🟢 Готовых: $READY/$started узлов"
echo ""

# Справка по мониторингу
echo "📈 МОНИТОРИНГ И УПРАВЛЕНИЕ:"
echo "  killall uvicorn                  # Остановить все узлы"
echo "  ps aux | grep uvicorn            # Список процессов"
echo "  lsof -i :8001-8050              # Какие порты заняты"
echo "  tail -f /tmp/kolibri_node_800*.log  # Логи узла"
echo ""

echo "🔗 СИНХРОНИЗАЦИЯ:"
echo "  • Лайт-режим: узлы 0-4 обучаются, 5-49 синхронизируют + участвуют в роевой сети"
echo "  • Период: 30-60 сек"
echo "  • Обмен: лучшие паттерны и знания"
echo ""
echo "✨ Swarm готов к работе!"

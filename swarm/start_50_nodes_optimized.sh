#!/usr/bin/env bash
# Оптимизированный запуск 50 узлов Kolibri swarm кластера
# Стратегия: партийный запуск, лайт-режим, мониторинг памяти

PROJECT_ROOT="/Users/kolibri/Desktop/kolibri-project"
VENV_BIN="$PROJECT_ROOT/.venv/bin/activate"
NODES_COUNT=50
BATCH_SIZE=5              # Запускаем партиями по 5 узлов
BATCH_DELAY=3             # Задержка между партиями (сек)
BASE_PORT=8001
MAX_MEMORY_PCT=85         # Стоп если памяти > 85%
PID_FILE="/tmp/kolibri_50nodes_pids.txt"

echo "🚀 ОПТИМИЗИРОВАННЫЙ ЗАПУСК KOLIBRI SWARM ($NODES_COUNT узлов)"
echo "=============================================================="
echo "  • Партиями по $BATCH_SIZE узлов"
echo "  • Задержка между партиями: ${BATCH_DELAY}s"
echo "  • Max память: ${MAX_MEMORY_PCT}%"
echo ""

# Активируем venv один раз
source "$VENV_BIN" 2>/dev/null || {
    echo "❌ Не найдено venv, создаём..."
    python3 -m venv "$PROJECT_ROOT/.venv" >/dev/null 2>&1
    source "$VENV_BIN"
    pip install -q --upgrade pip fastapi uvicorn httpx requests >/dev/null 2>&1
}

cd "$PROJECT_ROOT" || exit 1

# Удаляем старые PID файлы
rm -f /tmp/kolibri_nodes_*.pid "$PID_FILE"
touch "$PID_FILE"

# Функция проверки памяти
check_memory() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS: используем vm_stat
        local pages_used=$(vm_stat | grep "Pages active:" | awk '{print $3}' | tr -d '.')
        local pages_wired=$(vm_stat | grep "Pages wired down:" | awk '{print $4}' | tr -d '.')
        local memory_mb=$(( (pages_used + pages_wired) / 256 ))
        local total_mb=$(( $(sysctl -n hw.memsize) / 1024 / 1024 ))
    else
        # Linux: используем /proc/meminfo
        local memavail=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
        local memtotal=$(grep MemTotal /proc/meminfo | awk '{print $2}')
        local memory_mb=$(( (memtotal - memavail) / 1024 ))
        local total_mb=$(( memtotal / 1024 ))
    fi
    
    local pct=$(( memory_mb * 100 / total_mb ))
    echo "$pct ${memory_mb}M/${total_mb}M"
}

# Функция получения статуса процесса
get_process_info() {
    local pid=$1
    if [[ "$OSTYPE" == "darwin"* ]]; then
        ps -p "$pid" -o %mem=,rss= 2>/dev/null | awk '{print $1 " " $2/1024 "M"}'
    else
        ps -p "$pid" -o %mem=,rss= 2>/dev/null | awk '{print $1 " " $2/1024 "M"}'
    fi
}

# ============================================================================
# ЗАПУСК ПАРТИЯМИ
# ============================================================================
echo "📊 ЗАПУСК ПАРТИЯМИ:"
echo ""

started=0
failed=0

for batch_start in $(seq 0 $BATCH_SIZE $((NODES_COUNT - 1))); do
    batch_end=$(( batch_start + BATCH_SIZE - 1 ))
    if [ $batch_end -ge $NODES_COUNT ]; then
        batch_end=$((NODES_COUNT - 1))
    fi
    
    echo "  📦 Партия [$batch_start-$batch_end]..."
    
    for i in $(seq $batch_start $batch_end); do
        PORT=$((BASE_PORT + i))
        NODE_ID="node_$(printf '%02x' $i)"
        
        # Для первых нескольких узлов включаем learning, для остальных — лайт-режим
        if [ $i -lt 5 ]; then
            LEARN_FLAG=1
        else
            LEARN_FLAG=0
        fi
        
        # Запускаем с ограничениями памяти
        (
            export KOLIBRI_NODE_ID="$NODE_ID"
            export KOLIBRI_PORT="$PORT"
            export KOLIBRI_ENABLE_CONTINUOUS_LEARNING="$LEARN_FLAG"
            export KOLIBRI_ENABLE_BACKGROUND_LEARNING="$LEARN_FLAG"
            export KOLIBRI_ENABLE_SWARM_RUNTIME=1
            
            # Лайт-режим: меньше тредов, меньше памяти
            if [ $LEARN_FLAG -eq 0 ]; then
                export PYTHONUNBUFFERED=1
            fi
            
            nohup python3 -m uvicorn backend.service.main:app \
                --host 127.0.0.1 \
                --port "$PORT" \
                --workers 1 \
                --log-level error \
                --access-log \
                > /tmp/kolibri_node_${PORT}.log 2>&1 &
            
            local pid=$!
            echo "$pid" >> "$PID_FILE"
            echo "    ✓ $NODE_ID (PID: $pid, port: $PORT)"
        ) &
        
        sleep 0.1
    done
    
    # Даём время на инициализацию партии
    sleep "$BATCH_DELAY"
    
    # Проверяем память
    local mem_status=$(check_memory)
    local mem_pct=$(echo "$mem_status" | awk '{print $1}')
    echo "    💾 Память: $mem_status"
    
    if [ "$mem_pct" -gt "$MAX_MEMORY_PCT" ]; then
        echo "    ⚠️  ПЕРЕГРУЗКА ПАМЯТИ ($mem_pct%) - останавливаем запуск"
        break
    fi
    
    started=$((batch_end - batch_start + 1 + started))
done

echo ""
echo "✅ Запущено $started узлов (~$(( started * 3 ))% от CPU)"
echo "📊 Диапазон портов: $BASE_PORT - $((BASE_PORT + started - 1))"
echo ""

# ============================================================================
# МОНИТОРИНГ ИНИЦИАЛИЗАЦИИ
# ============================================================================
sleep 5

echo "🔍 ПРОВЕРКА СТАТУСА:"
READY=0
FAILED=0

for port in $(seq $BASE_PORT $((BASE_PORT + started - 1))); do
    if timeout 2 curl -s "http://127.0.0.1:$port/api/v1/learning/status" >/dev/null 2>&1; then
        READY=$((READY + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo "  🟢 Готовых узлов: $READY/$started"
echo "  🔴 Ошибок: $FAILED"
echo ""

# ============================================================================
# СПРАВКА УПРАВЛЕНИЯ
# ============================================================================
echo "📡 СИНХРОНИЗАЦИЯ:"
echo "  • Режим: лайт (узлы 0-4 с learning, 5+ только роевая синхронизация)"
echo "  • Период синхронизации: 30-60 сек"
echo "  • Обмен: лучшие паттерны через /api/v1/swarm/sync"
echo ""
echo "🛠️  УПРАВЛЕНИЕ:"
echo "  Остановить все:      pkill -f 'uvicorn.*kolibri'"
echo "  Статус память:       ps aux | grep uvicorn | awk '{s+=\$6} END {print s/1024 \" MB\"}'"
echo "  Логи узла N:         tail -f /tmp/kolibri_node_800N.log"
echo "  Проверить узел:      curl http://127.0.0.1:800N/api/v1/learning/status | python3 -m json.tool"
echo ""
echo "✨ Роевой кластер готов! Запущено: $started узлов"

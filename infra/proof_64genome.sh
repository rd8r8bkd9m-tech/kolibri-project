#!/bin/bash
# ============================================================================
# Доказательство: 100 узлов Kolibri с 64-значным геномом и реальным фитнесом
# ============================================================================
set -e

NODE_BIN="/workspaces/kolibri-project/build/kolibri_node"
NUM_NODES=100
RESULTS_DIR="/tmp/kolibri_proof_64"
rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"

echo "=============================================="
echo "  Kolibri 64-Genome Proof: $NUM_NODES узлов"
echo "=============================================="

# --- Обучающие данные (разнообразный текст) ---
TEACH_DATA=(
    "колибри летит над горами в небе"
    "солнце светит ярко над полями"
    "искусственный интеллект учится числами"
    "эволюция формул создаёт решения"
    "генетический алгоритм мутирует лучших"
    "цифровой геном кодирует формулу"
)

echo "[Фаза 1] Запуск $NUM_NODES узлов с текстовым обучением..."
START_TIME=$(date +%s%N)

for i in $(seq 1 $NUM_NODES); do
    (
        # Каждому узлу — свой рабочий каталог (избежание конфликта genome.dat)
        NODE_DIR="$RESULTS_DIR/work_${i}"
        mkdir -p "$NODE_DIR"
        
        # Каждый узел получает случайный набор обучающих фраз
        CMDS=""
        for phrase in "${TEACH_DATA[@]}"; do
            CMDS+=":teach $phrase\n"
        done
        # Дополнительные уникальные данные для каждого узла
        CMDS+=":teach узел $i обрабатывает часть задачи\n"
        # Эволюция (teach уже запускает 8 поколений каждый раз)
        CMDS+=":evolve 5\n"
        CMDS+=":why\n"
        CMDS+=":stats\n"
        CMDS+=":exit\n"
        
        cd "$NODE_DIR"
        echo -e "$CMDS" | timeout 30 "$NODE_BIN" --node-id $i --seed $((42 + i * 7)) \
            > "$RESULTS_DIR/node_${i}.log" 2>&1
    ) &
done

echo "[Фаза 1] Ожидание завершения всех $NUM_NODES узлов..."
wait

END_TIME=$(date +%s%N)
ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))

echo ""
echo "=============================================="
echo "  РЕЗУЛЬТАТЫ: 100 узлов с 64-значным геномом"
echo "=============================================="
echo "Время выполнения: ${ELAPSED_MS} мс"
echo ""

# --- Анализ результатов ---
TOTAL_OK=0
TOTAL_FAIL=0
TOTAL_FITNESS=0
MAX_FITNESS=0
TOTAL_PAIRS=0
GENOMES_64=0

for i in $(seq 1 $NUM_NODES); do
    LOG="$RESULTS_DIR/node_${i}.log"
    if [ ! -f "$LOG" ]; then
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        continue
    fi
    
    # Извлекаем фитнес
    FITNESS=$(grep -oP 'Лучший фитнес: \K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    if [ -z "$FITNESS" ]; then
        FITNESS=$(grep -oP 'фитнес=\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    fi
    
    # Извлекаем связи
    PAIRS=$(grep -oP 'Активных связей: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
    
    # Извлекаем геном
    GENE=$(grep -oP 'ген: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
    GENE_LEN=${#GENE}
    
    if [ -n "$FITNESS" ] && [ "$FITNESS" != "0.000000" ]; then
        TOTAL_OK=$((TOTAL_OK + 1))
        # Проверка длины генома >= 64
        if [ "$GENE_LEN" -ge 64 ] 2>/dev/null; then
            GENOMES_64=$((GENOMES_64 + 1))
        fi
        if [ -n "$PAIRS" ]; then
            TOTAL_PAIRS=$((TOTAL_PAIRS + ${PAIRS:-0}))
        fi
    else
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
    fi
done

# Уникальные геномы
UNIQUE_GENOMES=$(grep -ohP 'ген: \K[0-9]+' "$RESULTS_DIR"/node_*.log 2>/dev/null | sort -u | wc -l)

# Выборочный вывод нескольких узлов
echo "--- Выборочные результаты (5 узлов) ---"
for i in 1 25 50 75 100; do
    LOG="$RESULTS_DIR/node_${i}.log"
    if [ -f "$LOG" ]; then
        FITNESS=$(grep -oP 'фитнес=\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
        GENE=$(grep -oP 'ген: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
        PAIRS=$(grep -oP 'Активных связей: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
        echo "  Узел $i: фитнес=$FITNESS связей=$PAIRS геном=${GENE:0:20}... (${#GENE} цифр)"
    fi
done

echo ""
echo "=============================================="
echo "  ИТОГОВАЯ СТАТИСТИКА"
echo "=============================================="
echo "Узлов с ненулевым фитнесом: $TOTAL_OK / $NUM_NODES"
echo "Узлов с 64+ цифрами генома: $GENOMES_64 / $NUM_NODES"
echo "Уникальных геномов: $UNIQUE_GENOMES"
echo "Суммарно обучающих связей: $TOTAL_PAIRS"
echo "Время: ${ELAPSED_MS} мс (${NUM_NODES} узлов параллельно)"
echo ""

# Финальный вердикт
if [ "$TOTAL_OK" -ge 95 ] && [ "$GENOMES_64" -ge 95 ]; then
    echo "✅ ДОКАЗАНО: 64-значный геном + реальный фитнес на $TOTAL_OK из $NUM_NODES узлов"
else
    echo "⚠️  Частичный успех: $TOTAL_OK узлов с фитнесом, $GENOMES_64 с 64-значным геномом"
fi
echo "=============================================="

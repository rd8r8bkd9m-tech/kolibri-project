#!/bin/bash
# ============================================================================
# Доказательство: 1000 узлов Kolibri с 64-значным геномом (пакетный запуск)
# ============================================================================
set -e

NODE_BIN="/workspaces/kolibri-project/build/kolibri_node"
NUM_NODES=1000
BATCH_SIZE=200
RESULTS_DIR="/tmp/kolibri_proof_1000"
rm -rf "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR"

echo "=============================================="
echo "  Kolibri 64-Genome Proof: $NUM_NODES узлов"
echo "  (пакетами по $BATCH_SIZE)"
echo "=============================================="

# --- Обучающие данные ---
TEACH_DATA=(
    "колибри летит над горами в небе"
    "солнце светит ярко над полями"
    "искусственный интеллект учится числами"
    "эволюция формул создаёт решения"
    "генетический алгоритм мутирует лучших"
    "цифровой геном кодирует формулу"
)

START_TIME=$(date +%s%N)
TOTAL_LAUNCHED=0

for BATCH_START in $(seq 1 $BATCH_SIZE $NUM_NODES); do
    BATCH_END=$((BATCH_START + BATCH_SIZE - 1))
    if [ "$BATCH_END" -gt "$NUM_NODES" ]; then
        BATCH_END=$NUM_NODES
    fi
    BATCH_COUNT=$((BATCH_END - BATCH_START + 1))
    echo "[Пакет] Узлы $BATCH_START..$BATCH_END ($BATCH_COUNT шт.)..."
    
    for i in $(seq $BATCH_START $BATCH_END); do
        (
            NODE_DIR="$RESULTS_DIR/work_${i}"
            mkdir -p "$NODE_DIR"
            
            CMDS=""
            for phrase in "${TEACH_DATA[@]}"; do
                CMDS+=":teach $phrase\n"
            done
            CMDS+=":teach узел $i часть роя\n"
            CMDS+=":evolve 5\n"
            CMDS+=":why\n"
            CMDS+=":stats\n"
            CMDS+=":exit\n"
            
            cd "$NODE_DIR"
            echo -e "$CMDS" | timeout 60 "$NODE_BIN" --node-id $i --seed $((42 + i * 7)) \
                > "$RESULTS_DIR/node_${i}.log" 2>&1
        ) &
    done
    wait
    TOTAL_LAUNCHED=$((TOTAL_LAUNCHED + BATCH_COUNT))
done

END_TIME=$(date +%s%N)
ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))

echo ""
echo "=============================================="
echo "  РЕЗУЛЬТАТЫ: $NUM_NODES узлов"
echo "=============================================="
echo "Время выполнения: ${ELAPSED_MS} мс ($(( ELAPSED_MS / 1000 )) сек)"
echo ""

# --- Анализ результатов ---
TOTAL_OK=0
TOTAL_FAIL=0
GENOMES_64=0
TOTAL_PAIRS=0
FITNESS_SUM=""

for i in $(seq 1 $NUM_NODES); do
    LOG="$RESULTS_DIR/node_${i}.log"
    if [ ! -f "$LOG" ]; then
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        continue
    fi
    
    FITNESS=$(grep -oP 'Лучший фитнес: \K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    if [ -z "$FITNESS" ]; then
        FITNESS=$(grep -oP 'фитнес=\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    fi
    
    PAIRS=$(grep -oP 'Активных связей: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
    GENE=$(grep -oP 'ген: \K[0-9]+' "$LOG" 2>/dev/null | tail -1)
    GENE_LEN=${#GENE}
    
    if [ -n "$FITNESS" ] && [ "$FITNESS" != "0.000000" ]; then
        TOTAL_OK=$((TOTAL_OK + 1))
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

UNIQUE_GENOMES=$(grep -ohP 'ген: \K[0-9]+' "$RESULTS_DIR"/node_*.log 2>/dev/null | sort -u | wc -l)

# Диапазон фитнеса
MIN_FIT=$(grep -ohP 'Лучший фитнес: \K[0-9.]+' "$RESULTS_DIR"/node_*.log 2>/dev/null | sort -n | head -1)
MAX_FIT=$(grep -ohP 'Лучший фитнес: \K[0-9.]+' "$RESULTS_DIR"/node_*.log 2>/dev/null | sort -rn | head -1)

echo "--- Выборочные результаты (6 узлов) ---"
for i in 1 100 250 500 750 1000; do
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
echo "Диапазон фитнеса: $MIN_FIT .. $MAX_FIT"
echo "Время: $(( ELAPSED_MS / 1000 )) сек ($NUM_NODES узлов, пакетами по $BATCH_SIZE)"
echo ""

if [ "$TOTAL_OK" -ge 950 ] && [ "$GENOMES_64" -ge 950 ]; then
    echo "✅ ДОКАЗАНО: 64-значный геном + реальный фитнес на $TOTAL_OK из $NUM_NODES узлов"
elif [ "$TOTAL_OK" -ge 800 ]; then
    echo "✅ УСПЕХ: $TOTAL_OK из $NUM_NODES узлов отработали с фитнесом"
else
    echo "⚠️  Частичный успех: $TOTAL_OK узлов с фитнесом, $GENOMES_64 с 64-значным геномом"
fi
echo "=============================================="

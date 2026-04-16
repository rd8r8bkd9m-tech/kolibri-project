#!/bin/bash
# ═══════════════════════════════════════════════════════════
#  Kolibri OS — Массовое обучение (полный пайплайн)
#
#  Этап 1: Краулинг Wikipedia → C-модель (.klm)
#  Этап 2: Обучение Python-движка из корпуса
#  Этап 3: Краулинг из seeds → C-модель
#  Этап 4: Обучение Python-движка из новых wiki текстов
#  Этап 5: Переобучение эмбеддингов
#
#  Использование:
#    bash scripts/mass_train.sh              # Полный пайплайн
#    bash scripts/mass_train.sh --wiki 200   # Только 200 статей
#    bash scripts/mass_train.sh --seeds-only # Только из seeds
#    bash scripts/mass_train.sh --status     # Проверить прогресс
# ═══════════════════════════════════════════════════════════

set -euo pipefail

ROOT="/workspaces/kolibri-project"
MODEL="$ROOT/data/models/kolibri_web.klm"
LOGDIR="$ROOT/logs"
WIKI_DIR="$ROOT/data/corpus/wiki_mass"
SEEDS_DIR="$ROOT/data/corpus/seeds_mass"

mkdir -p "$LOGDIR" "$WIKI_DIR" "$SEEDS_DIR"

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

WIKI_COUNT=500
SEED_FILE="$ROOT/seeds/quick_100.txt"
SEEDS_ONLY=false

# ─── Аргументы ───
while [[ $# -gt 0 ]]; do
    case "$1" in
        --wiki)
            WIKI_COUNT="${2:-500}"
            shift 2
            ;;
        --seeds-only)
            SEEDS_ONLY=true
            shift
            ;;
        --seed-file)
            SEED_FILE="$2"
            shift 2
            ;;
        --status)
            echo -e "${CYAN}═══ Статус массового обучения ═══${NC}"
            echo ""

            # C-модель wiki
            if [[ -f "$LOGDIR/mass_train_wiki.log" ]]; then
                LAST=$(tail -1 "$LOGDIR/mass_train_wiki.log" 2>/dev/null || echo "нет данных")
                echo -e "${CYAN}[C-модель Wiki]${NC} $LAST"
            else
                echo -e "${YELLOW}[C-модель Wiki]${NC} Не запущено"
            fi

            # C-модель seeds
            if [[ -f "$LOGDIR/mass_train_seeds.log" ]]; then
                LAST=$(tail -1 "$LOGDIR/mass_train_seeds.log" 2>/dev/null || echo "нет данных")
                echo -e "${CYAN}[C-модель Seeds]${NC} $LAST"
            else
                echo -e "${YELLOW}[C-модель Seeds]${NC} Не запущено"
            fi

            # Python-движок
            if [[ -f "$LOGDIR/mass_train_python.log" ]]; then
                LAST=$(tail -3 "$LOGDIR/mass_train_python.log" 2>/dev/null || echo "нет данных")
                echo -e "${CYAN}[Python-движок]${NC}"
                echo "$LAST"
            else
                echo -e "${YELLOW}[Python-движок]${NC} Не запущено"
            fi

            # Статистика API
            echo ""
            STATS=$(curl -s --max-time 10 http://localhost:8001/api/v1/ai/stats 2>/dev/null || echo '{}')
            if echo "$STATS" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'📊 Паттерны: {d.get(\"graph_patterns\",0):,}  Рёбра: {d.get(\"graph_edges\",0):,}  Эмбеддинги: {d.get(\"embedding_vocab_size\",0):,}  Формулы: пок.{d.get(\"formula_generation\",0)}')" 2>/dev/null; then
                :
            else
                echo -e "${RED}Бэкенд недоступен${NC}"
            fi

            # Модель
            if [[ -f "$MODEL" ]]; then
                SIZE=$(du -h "$MODEL" | cut -f1)
                echo -e "💾 C-модель: $SIZE ($MODEL)"
            fi

            # Wiki файлы
            WIKI_FILES=$(ls "$WIKI_DIR" 2>/dev/null | wc -l)
            echo -e "📁 Wiki текстов: $WIKI_FILES"

            # Процессы
            echo ""
            PROCS=$(ps aux | grep -E "train_corpus|mass_train_python|kolibri_mass_trainer" | grep -v grep | wc -l)
            echo -e "⚙️  Активных процессов обучения: $PROCS"

            exit 0
            ;;
        *)
            echo "Неизвестный аргумент: $1"
            exit 1
            ;;
    esac
done

# ─── Проверка бэкенда ───
check_backend() {
    curl -s --max-time 5 http://localhost:8001/api/v1/ai/stats >/dev/null 2>&1
}

echo ""
echo -e "${CYAN}╔═══════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║  🐦 Kolibri OS — Массовое обучение            ║${NC}"
echo -e "${CYAN}╚═══════════════════════════════════════════════╝${NC}"
echo ""

if ! check_backend; then
    echo -e "${RED}❌ Бэкенд не запущен! Запускаю...${NC}"
    bash "$ROOT/start.sh"
    sleep 10
    if ! check_backend; then
        echo -e "${RED}❌ Бэкенд не смог запуститься${NC}"
        exit 1
    fi
fi

# Начальная статистика
STATS=$(curl -s --max-time 10 http://localhost:8001/api/v1/ai/stats 2>/dev/null)
P0=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('graph_patterns',0))" 2>/dev/null || echo 0)
E0=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('graph_edges',0))" 2>/dev/null || echo 0)
echo -e "${CYAN}📊 Начальное состояние: $P0 паттернов, $E0 рёбер${NC}"
echo ""

T0=$(date +%s)

# ═══════════════════════════════════════
# Этап 1: Wikipedia → C-модель
# ═══════════════════════════════════════
if [[ "$SEEDS_ONLY" == "false" ]]; then
    echo -e "${GREEN}━━━ Этап 1/4: Wikipedia ($WIKI_COUNT статей) → C-модель ━━━${NC}"
    python3 "$ROOT/scripts/train_corpus.py" \
        --wiki --count "$WIKI_COUNT" \
        --model "$MODEL" \
        --save-dir "$WIKI_DIR" \
        --verbose \
        > "$LOGDIR/mass_train_wiki.log" 2>&1 &
    WIKI_PID=$!
    echo -e "  Запущено в фоне (PID $WIKI_PID)"
    echo -e "  Лог: tail -f $LOGDIR/mass_train_wiki.log"
    echo ""
fi

# ═══════════════════════════════════════
# Этап 2: Существующий корпус → Python-движок
# ═══════════════════════════════════════
echo -e "${GREEN}━━━ Этап 2/4: Существующий корпус → Python-движок ━━━${NC}"
python3 "$ROOT/scripts/mass_train_python.py" \
    --dir "$ROOT/data/corpus" "$ROOT/docs/wikipedia" "$ROOT/docs/ingested" \
    --delay 0.3 --verbose \
    > "$LOGDIR/mass_train_python_phase1.log" 2>&1 &
PY1_PID=$!
echo -e "  Запущено в фоне (PID $PY1_PID)"
echo -e "  Лог: tail -f $LOGDIR/mass_train_python_phase1.log"
echo ""

# ═══════════════════════════════════════
# Этап 3: Seeds → C-модель (параллельно)
# ═══════════════════════════════════════
if [[ -f "$SEED_FILE" ]]; then
    SEED_COUNT=$(wc -l < "$SEED_FILE")
    echo -e "${GREEN}━━━ Этап 3/4: Seeds ($SEED_COUNT URL) → C-модель ━━━${NC}"

    # Ждём завершения Этапа 1 (используют одну модель)
    if [[ "$SEEDS_ONLY" == "false" ]] && [[ -n "${WIKI_PID:-}" ]]; then
        echo -e "  Ожидаю завершения Этапа 1..."
        wait "$WIKI_PID" 2>/dev/null || true
        echo -e "  ✅ Этап 1 завершён"
    fi

    python3 "$ROOT/scripts/train_corpus.py" \
        --urls "$SEED_FILE" \
        --model "$MODEL" \
        --save-dir "$SEEDS_DIR" \
        --verbose \
        > "$LOGDIR/mass_train_seeds.log" 2>&1 &
    SEEDS_PID=$!
    echo -e "  Запущено в фоне (PID $SEEDS_PID)"
    echo -e "  Лог: tail -f $LOGDIR/mass_train_seeds.log"
    echo ""
fi

# ═══════════════════════════════════════
# Ждём Python Phase 1
# ═══════════════════════════════════════
echo -e "${CYAN}⏳ Ожидаю обучение Python-движка (фаза 1)...${NC}"
wait "$PY1_PID" 2>/dev/null || true
echo -e "${GREEN}✅ Python-движок (фаза 1) завершён${NC}"
echo ""

# ═══════════════════════════════════════
# Этап 4: Новые Wiki-тексты → Python-движок
# ═══════════════════════════════════════
if [[ "$SEEDS_ONLY" == "false" ]]; then
    # Ждём завершения Wiki-краулинга
    if [[ -n "${WIKI_PID:-}" ]]; then
        wait "$WIKI_PID" 2>/dev/null || true
    fi

    WIKI_FILES=$(ls "$WIKI_DIR" 2>/dev/null | wc -l)
    if [[ "$WIKI_FILES" -gt 0 ]]; then
        echo -e "${GREEN}━━━ Этап 4/4: Wiki-тексты ($WIKI_FILES файлов) → Python-движок ━━━${NC}"
        python3 "$ROOT/scripts/mass_train_python.py" \
            --wiki-mass \
            --delay 0.3 --verbose \
            > "$LOGDIR/mass_train_python_phase2.log" 2>&1 &
        PY2_PID=$!
        echo -e "  Запущено в фоне (PID $PY2_PID)"
        echo -e "  Лог: tail -f $LOGDIR/mass_train_python_phase2.log"
    fi
fi

# ═══════════════════════════════════════
# Ждём всех фоновых процессов
# ═══════════════════════════════════════
echo ""
echo -e "${CYAN}⏳ Ожидаю завершения всех процессов обучения...${NC}"
wait 2>/dev/null || true

T1=$(date +%s)
ELAPSED=$((T1 - T0))
MINUTES=$((ELAPSED / 60))
SECONDS_R=$((ELAPSED % 60))

# ═══════════════════════════════════════
# Этап 5: Переобучение эмбеддингов
# ═══════════════════════════════════════
echo -e "${GREEN}━━━ Бонус: Переобучение эмбеддингов ━━━${NC}"
curl -s --max-time 300 -X POST http://localhost:8001/api/v1/ai/embeddings/train \
    -H "Content-Type: application/json" -d '{}' > /dev/null 2>&1 &

echo ""
echo -e "${CYAN}╔═══════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║  🏁 Массовое обучение завершено!               ║${NC}"
echo -e "${CYAN}╚═══════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ⏱  Время: ${MINUTES}м ${SECONDS_R}с"
echo ""

# Финальная статистика
STATS=$(curl -s --max-time 10 http://localhost:8001/api/v1/ai/stats 2>/dev/null || echo '{}')
P1=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('graph_patterns',0))" 2>/dev/null || echo "?")
E1=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('graph_edges',0))" 2>/dev/null || echo "?")
S1=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('sentence_count',0))" 2>/dev/null || echo "?")
V1=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('embedding_vocab_size',0))" 2>/dev/null || echo "?")
G1=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('formula_generation',0))" 2>/dev/null || echo "?")
CM=$(echo "$STATS" | python3 -c "import sys,json; print(json.load(sys.stdin).get('c_model_size_mb',0))" 2>/dev/null || echo "?")

echo -e "  📊 Итоговая статистика:"
echo -e "     Паттерны:   $P0 → $P1"
echo -e "     Рёбра:      $E0 → $E1"
echo -e "     Предложения: $S1"
echo -e "     Эмбеддинги: $V1 слов"
echo -e "     Формулы:    поколение $G1"
echo -e "     C-модель:   ${CM} МБ"
echo ""
echo -e "  📁 Сохранённые тексты:"
echo -e "     Wiki: $(ls "$WIKI_DIR" 2>/dev/null | wc -l) файлов"
echo -e "     Seeds: $(ls "$SEEDS_DIR" 2>/dev/null | wc -l) файлов"
echo ""
echo -e "  ${CYAN}Проверка: bash scripts/mass_train.sh --status${NC}"
echo -e "  ${CYAN}Тест чата: curl -X POST http://localhost:8001/api/v1/ai/chat -H 'Content-Type: application/json' -d '{\"message\":\"Что такое искусственный интеллект?\"}' | python3 -m json.tool${NC}"

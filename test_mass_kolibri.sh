#!/bin/bash
# Массовое тестирование Kolibri AGI
# Задаёт 50+ вопросов из разных областей

BASE_URL="http://localhost:8001"
LOG_FILE="/tmp/kolibri_mass_test_$(date +%Y%m%d_%H%M%S).log"

# Счётчики
TOTAL=0
SUCCESS=0
FAILED=0

# Цвета
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

ask() {
    local category="$1"
    local question="$2"
    TOTAL=$((TOTAL + 1))
    
    echo -e "\n${BLUE}[$TOTAL] [$category]${NC} $question"
    
    RESPONSE=$(curl -s -X POST "$BASE_URL/api/v1/ai/chat" \
        -d "{\"message\":\"$question\"}" \
        --connect-timeout 5 \
        --max-time 30 \
        2>/dev/null)
    
    if [ -z "$RESPONSE" ]; then
        echo -e "  ${RED}❌ СЕРВЕР УПАЛ!${NC}"
        FAILED=$((FAILED + 1))
        echo "[$TOTAL] [$category] $question | FAILED: SERVER CRASHED" >> "$LOG_FILE"
        return 1
    fi
    
    # Парсим ответ
    PARSED=$(echo "$RESPONSE" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    ans = d.get('response', 'НЕТ ОТВЕТА')
    method = d.get('method', 'unknown')
    conf = d.get('confidence', 0)
    duration = d.get('duration_ms', 0)
    # Обрезаем длинный ответ
    if len(ans) > 150:
        ans = ans[:150] + '...'
    print(f'{ans}|{method}|{conf}|{duration}')
except Exception as e:
    print(f'ERROR: {e}')
" 2>&1)
    
    if [[ "$PARSED" == ERROR* ]]; then
        echo -e "  ${RED}❌ Ошибка парсинга: $PARSED${NC}"
        FAILED=$((FAILED + 1))
        echo "[$TOTAL] [$category] $question | FAILED: $PARSED" >> "$LOG_FILE"
    else
        ANSWER=$(echo "$PARSED" | cut -d'|' -f1)
        METHOD=$(echo "$PARSED" | cut -d'|' -f2)
        CONF=$(echo "$PARSED" | cut -d'|' -f3)
        DURATION=$(echo "$PARSED" | cut -d'|' -f4)
        
        SUCCESS=$((SUCCESS + 1))
        echo -e "  ${GREEN}✅ $ANSWER${NC}"
        echo -e "  ${YELLOW}📊 Метод: $METHOD | Confidence: $CONF | ${duration}ms${NC}"
        echo "[$TOTAL] [$category] $question | OK: method=$method, conf=$conf" >> "$LOG_FILE"
    fi
    
    # Небольшая пауза
    sleep 0.5
}

echo "╔══════════════════════════════════════════════════════════╗"
echo "║     МАССОВОЕ ТЕСТИРОВАНИЕ KOLIBRI AGI PIPELINE          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "📝 Лог: $LOG_FILE"
echo ""

# ==================== ПРИВЕТСТВИЯ ====================
echo -e "${YELLOW}══════════ ПРИВЕТСТВИЯ ══════════${NC}"
ask "greeting" "Привет!"
ask "greeting" "Здравствуй"
ask "greeting" "как дела?"
ask "greeting" "hello"

# ==================== МАТЕМАТИКА ====================
echo -e "\n${YELLOW}══════════ МАТЕМАТИКА ══════════${NC}"
ask "math" "2+2"
ask "math" "7 умножить на 8"
ask "math" "100 делить на 5"
ask "math" "50 минус 23"
ask "math" "12 в степени 2"
ask "math" "квадрат 15"
ask "math" "площадь квадрата стороной 5"
ask "math" "теорема виета"
ask "math" "дискриминант"
ask "math" "формула корней квадратного уравнения"
ask "math" "логарифм"
ask "math" "sin 30 градусов"
ask "math" "косинус 60"

# ==================== ХИМИЯ ====================
echo -e "\n${YELLOW}══════════ ХИМИЯ ══════════${NC}"
ask "chemistry" "формула воды"
ask "chemistry" "формула углекислого газа"
ask "chemistry" "формула поваренной соли"
ask "chemistry" "формула кислорода"
ask "chemistry" "формула серной кислоты"
ask "chemistry" "что такое метан"
ask "chemistry" "число авогадро"
ask "chemistry" "формула глюкозы"
ask "chemistry" "что такое озон"

# ==================== ФИЗИКА ====================
echo -e "\n${YELLOW}══════════ ФИЗИКА ══════════${NC}"
ask "physics" "второй закон Ньютона"
ask "physics" "формула энергии"
ask "physics" "закон Ома"
ask "physics" "скорость света"

# ==================== БИОЛОГИЯ ====================
echo -e "\n${YELLOW}══════════ БИОЛОГИЯ ══════════${NC}"
ask "biology" "что такое клетка"
ask "biology" "что такое ДНК"
ask "biology" "фотосинтез"
ask "biology" "что такое эволюция"

# ==================== АСТРОНОМИЯ ====================
echo -e "\n${YELLOW}══════════ АСТРОНОМИЯ ══════════${NC}"
ask "astronomy" "сколько лет Вселенной"
ask "astronomy" "что такое чёрная дыра"
ask "astronomy" "сколько планет в Солнечной системе"

# ==================== ГЕОГРАФИЯ ====================
echo -e "\n${YELLOW}══════════ ГЕОГРАФИЯ ══════════${NC}"
ask "geography" "Столица Франции"
ask "geography" "Столица Японии"
ask "geography" "Столица Австралии"
ask "geography" "самая высокая гора"
ask "geography" "самое глубокое озеро"
ask "geography" "самая длинная река"
ask "geography" "какой океан самый большой"

# ==================== ИСТОРИЯ ====================
echo -e "\n${YELLOW}══════════ ИСТОРИЯ ══════════${NC}"
ask "history" "когда была вторая мировая война"
ask "history" "когда был полёт Гагарина"
ask "history" "когда человек полетел на Луну"
ask "history" "когда появился интернет"

# ==================== ПРОГРАММИРОВАНИЕ ====================
echo -e "\n${YELLOW}══════════ ПРОГРАММИРОВАНИЕ ══════════${NC}"
ask "programming" "что такое алгоритм"
ask "python" "что такое Python"
ask "programming" "что такое Docker"
ask "programming" "что такое API"
ask "programming" "что такое SQL"

# ==================== ЛИТЕРАТУРА ====================
echo -e "\n${YELLOW}══════════ ЛИТЕРАТУРА ══════════${NC}"
ask "literature" "кто написал Войну и мир"
ask "literature" "кто написал Преступление и наказание"
ask "literature" "кто написал Мона Лизу"

# ==================== МЕДИЦИНА ====================
echo -e "\n${YELLOW}══════════ МЕДИЦИНА ══════════${NC}"
ask "medicine" "нормальное давление"
ask "medicine" "нормальная температура тела"
ask "medicine" "что такое иммунитет"

# ==================== ЭКОНОМИКА ====================
echo -e "\n${YELLOW}══════════ ЭКОНОМИКА ══════════${NC}"
ask "economics" "что такое ВВП"
ask "economics" "что такое инфляция"

# ==================== ФИЛОСОФИЯ ====================
echo -e "\n${YELLOW}══════════ ФИЛОСОФИЯ ══════════${NC}"
ask "philosophy" "что такое сознание"
ask "philosophy" "что такое этика"
ask "philosophy" "что такое логика"

# ==================== ПРАВО ====================
echo -e "\n${YELLOW}══════════ ПРАВО ══════════${NC}"
ask "law" "презумпция невиновности"
ask "law" "срок исковой давности"

# ==================== ИИ ====================
echo -e "\n${YELLOW}══════════ ИИ ══════════${NC}"
ask "ai" "что такое нейросеть"
ask "ai" "что такое машинное обучение"
ask "ai" "что такое трансформер"

# ==================== МУЗЫКА ====================
echo -e "\n${YELLOW}══════════ МУЗЫКА ══════════${NC}"
ask "music" "сколько нот в октаве"
ask "music" "что такое мажор"

# ==================== СПОРТ ====================
echo -e "\n${YELLOW}══════════ СПОРТ ══════════${NC}"
ask "sports" "сколько игроков в футболе"
ask "sports" "длина марафона"

# ==================== ИТОГИ ====================
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
if [ $FAILED -eq 0 ]; then
    echo -e "║           ${GREEN}✅ ВСЕ ТЕСТЫ ПРОЙДЕНЫ!${NC}                          ║"
else
    echo -e "║           ${RED}❌ ЕСТЬ ОШИБКИ: $FAILED${NC}                           ║"
fi
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "📊 СТАТИСТИКА:"
echo "   Всего вопросов: $TOTAL"
echo -e "   ✅ Успешно: ${GREEN}$SUCCESS${NC}"
echo -e "   ❌ Ошибки: ${RED}$FAILED${NC}"
echo "   Процент успеха: $(( SUCCESS * 100 / TOTAL ))%"
echo ""
echo "📝 Полный лог: $LOG_FILE"
echo ""

# Выводим ошибки если есть
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}⚠️  Ошибки:${NC}"
    grep "FAILED" "$LOG_FILE" | tail -10
fi

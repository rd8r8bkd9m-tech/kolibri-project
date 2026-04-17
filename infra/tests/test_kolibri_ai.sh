#!/bin/bash
# Тестирование Kolibri AGI Pipeline

BASE_URL="http://localhost:8001"

test_question() {
    local question="$1"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "❓ ВОПРОС: $question"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    RESPONSE=$(curl -s -X POST "$BASE_URL/api/v1/ai/chat" -d "{\"message\":\"$question\"}")
    
    if [ -z "$RESPONSE" ]; then
        echo "❌ ПУСТОЙ ОТВЕТ (сервер упал?)"
        return 1
    fi
    
    # Extract fields using python
    echo "$RESPONSE" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(f'✅ ОТВЕТ: {d.get(\"response\", \"НЕТ ОТВЕТА\")}')
    print(f'📊 Метод: {d.get(\"method\", \"unknown\")}')
    print(f'🎯 Confidence: {d.get(\"confidence\", 0):.2f}')
    print(f'⏱️  Время: {d.get(\"duration_ms\", 0):.1f} ms')
    print(f'🧠 Query Kind: {d.get(\"runtime_query_kind\", \"unknown\")}')
    print(f'💾 Digit Winner: {d.get(\"runtime_digit_winner\", \"unknown\")}')
except:
    print(f'❌ ОШИБКА ПАРСИНГА: {sys.stdin.read()[:200]}')
"
    echo ""
}

echo "🧪 ТЕСТИРОВАНИЕ KOLIBRI AGI PIPELINE"
echo "====================================="
echo ""

# Тест 1: Приветствие
test_question "Привет, как дела?"

# Тест 2: География
test_question "Столица Франции?"

# Тест 3: Математика
test_question "7 умножить на 8"

# Тест 4: Химия
test_question "формула воды"

# Тест 5: Физика
test_question "второй закон Ньютона"

# Тест 6: Программирование
test_question "что такое алгоритм QuickSort?"

# Тест 7: Биология
test_question "что такое ДНК?"

# Тест 8: История
test_question "когда был полет Гагарина?"

# Тест 9: Астрономия
test_question "что такое черная дыра?"

# Тест 10: Литература
test_question "кто написал Войну и мир?"

echo "====================================="
echo "✅ ТЕСТИРОВАНИЕ ЗАВЕРШЕНО"

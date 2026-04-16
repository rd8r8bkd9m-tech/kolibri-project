#!/bin/bash

# Test script for Kolibri AI - 45 questions
PORT=9001
OUTPUT_FILE="test_results_45_questions.txt"

echo "=== Kolibri AI Test: 45 Questions ===" > "$OUTPUT_FILE"
echo "Date: $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

questions=(
    # Math (5 questions)
    "2+2"
    "Решить уравнение: 2x + 3 = 7"
    "Что такое теорема Пифагора?"
    "Вычислить 15 * 23"
    "Корни квадратного уравнения: x² + 5x + 6 = 0"

    # Physics (5 questions)
    "Что такое второй закон Ньютона?"
    "Объясни квантовые компьютеры"
    "Скорость света в вакууме?"
    "Что такое энергия?"
    "Закон Ома"

    # Chemistry (4 questions)
    "Что такое pH?"
    "Реакция горения водорода"
    "Число Авогадро"
    "Нейтрализация кислоты щелочью"

    # Biology (4 questions)
    "Что такое ДНК?"
    "Фотосинтез"
    "Эволюция по Дарвину"
    "Митоз и мейоз"

    # History (4 questions)
    "Когда была Вторая мировая война?"
    "Полёт Гагарина"
    "Падение Берлинской стены"
    "Распад СССР"

    # Geography (4 questions)
    "Столица Франции"
    "Самая высокая гора"
    "Самый длинный река"
    "Самый большой океан"

    # Programming/IT (4 questions)
    "Что такое алгоритм?"
    "Бинарный поиск"
    "Структуры данных"
    "Что такое API?"

    # Philosophy (3 questions)
    "Что такое этика?"
    "Стоицизм"
    "Экзистенциализм"

    # Medicine (3 questions)
    "Нормальное давление"
    "Вакцина"
    "Диабет"

    # Economics (3 questions)
    "Что такое ВВП?"
    "Инфляция"
    "Акции и облигации"

    # AI/ML (3 questions)
    "Что такое машинное обучение?"
    "Нейронные сети"
    "Градиентный спуск"

    # Law (2 questions)
    "Презумпция невиновности"
    "Договор"

    # Music/Art/Sports (3 questions)
    "7 нот в музыке"
    "Война и мир"
    "Олимпийские игры"
)

conversation_id="test_45_questions"

for i in "${!questions[@]}"; do
    question="${questions[$i]}"
    echo "Question $((i+1)): $question" >> "$OUTPUT_FILE"

    response=$(curl -s -X POST "http://localhost:$PORT/api/v1/ai/chat" \
        -d "{\"message\":\"$question\",\"conversation_id\":\"$conversation_id\"}" \
        -H 'Content-Type: application/json')

    method=$(echo "$response" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('method', 'unknown'))")
    answer=$(echo "$response" | python3 -c "import sys, json; data=json.load(sys.stdin); print(data.get('response', 'ERROR'))")

    echo "Method: $method" >> "$OUTPUT_FILE"
    echo "Answer: $answer" >> "$OUTPUT_FILE"
    echo "" >> "$OUTPUT_FILE"

    # Small delay to avoid overwhelming the server
    sleep 0.5
done

echo "Test completed. Results saved to $OUTPUT_FILE"
#!/bin/bash
# Тест роевого интеллекта на реальных данных (Wikipedia)

# 1. Сборка
cmake -S . -B build -G Ninja
cmake --build build

mkdir -p logs/test_swarm
rm -rf .kolibri/test_swarm
mkdir -p .kolibri/test_swarm

echo "[Тест] Запуск 5 узлов..."

BASE_PORT=11000
COUNT=5

for i in $(seq 1 $COUNT); do
    PORT=$((BASE_PORT + i))
    PEER_PORT=$((BASE_PORT + (i % COUNT) + 1))
    
    # Запуск узла с массовым обучением
    stdbuf -oL -eL ./build/kolibri_node --listen $PORT \
        --node-id $i \
        --peer 127.0.0.1:$PEER_PORT \
        --genome .kolibri/test_swarm/node$i.dat \
        --mass-learn > logs/test_swarm/node$i.log 2>&1 &
    
    echo "[Тест] Узел $i запущен на порту $PORT"
done

echo "[Тест] Ожидание завершения обучения (10 сек)..."
sleep 10

echo "[Тест] Отправка запросов рою..."

# Функция для запроса
query_node() {
    local WORD=$1
    echo "[Запрос] Что рой думает о: $WORD?"
    echo ":mass-learn" > query_cmd.txt
    echo ":evolve 2000" >> query_cmd.txt
    echo ":ask $WORD" >> query_cmd.txt
    echo ":why" >> query_cmd.txt
    echo ":quit" >> query_cmd.txt
    ./build/kolibri_node --listen 11099 --node-id 0 --peer 127.0.0.1:11001 < query_cmd.txt | grep -E "\[Вопрос\]|\[Ответ\]|тип=|фитнес="
}

query_node "Колибри"
query_node "будущее"
query_node "философия"

echo "[Тест] Завершение. Остановка узлов..."
pkill -f kolibri_node
rm query_cmd.txt

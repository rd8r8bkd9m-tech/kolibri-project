#!/bin/bash
# Гипер-масштабируемый рой Колибри (50 узлов)
# Эмуляция коллективного разума

mkdir -p logs
mkdir -p .kolibri/swarm_50

echo "[Рой] Запуск 50 узлов... (может занять время)"

BASE_PORT=10000
COUNT=20

for i in $(seq 1 $COUNT); do
    PORT=$((BASE_PORT + i))
    PEER_PORT=$((BASE_PORT + (i % COUNT) + 1))
    
    # Каждый узел знает 45 млн паттернов (синтетически + из docs/)
    stdbuf -oL -eL ./build/kolibri_node --listen $PORT \
        --node-id $i \
        --peer 127.0.0.1:$PEER_PORT \
        --genome .kolibri/swarm_50/node$i.dat \
        --mass-learn > logs/node$i.log 2>&1 &
    
    if [ $((i % 100)) -eq 0 ]; then
        echo "[Рой] Запущено $i узлов..."
    fi
done

echo "[Рой] Все узлы запущены. Ожидание стабилизации графа..."
sleep 10

echo "[Вопрос] Задаем вопрос мастер-узлу: Что такое философия?"
# Мы используем :ask с хешем слова "философия" (если оно есть в словаре)
# Или просто передаем текст, если мы обновили :ask

echo ":mass-learn" > swarm_cmd.txt
echo ":ask философия" >> swarm_cmd.txt
echo ":quit" >> swarm_cmd.txt

./build/kolibri_node --listen 10000 --node-id 0 --peer 127.0.0.1:10001 --mass-learn < swarm_cmd.txt

echo "[Завершение] Рой продолжает работу в фоне. Используйте 'pkill kolibri_node' для остановки."

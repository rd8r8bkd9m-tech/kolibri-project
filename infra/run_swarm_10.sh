#!/bin/bash
# Kolibri OS - Swarm Deployment Script (10 nodes)
# Hyper-Scale 45M patterns testing

echo "[Система] Запуск Роя Kolibri OS (10 узлов)..."
mkdir -p .kolibri/swarm

# Узел 1 (Мастер-узел роя)
echo "[Рой] Запуск Узла 1 (Порт 4050)..."
stdbuf -oL -eL ./build/kolibri_node --listen 4050 --node-id 1 --genome .kolibri/swarm/node1.dat --mass-learn > logs/node1.log 2>&1 &
sleep 2

# Узлы 2-10 (Периферия)
for i in {2..10}
do
    PORT=$((4049 + i))
    echo "[Рой] Запуск Узла $i (Порт $PORT, пир 4050)..."
    stdbuf -oL -eL ./build/kolibri_node --listen $PORT --node-id $i --peer 127.0.0.1:4050 --genome .kolibri/swarm/node$i.dat --mass-learn > logs/node$i.log 2>&1 &
    sleep 1
done

echo "[Система] Рой запущен. Идет Гипер-масштабное обучение на 45 млн паттернах..."
echo "[Система] Логи доступны в директории logs/"
ps aux | grep kolibri_node | grep -v grep

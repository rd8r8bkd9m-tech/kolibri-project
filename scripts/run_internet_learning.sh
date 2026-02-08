#!/bin/bash
# Kolibri OS - Continuum Learning Script
# Запускает процесс полного сканирования интернет-кэша и упаковки знаний

echo "=================================================="
echo " ЗАПУСК ОБУЧЕНИЯ (INTERNET CONTINUUM)"
echo " Цель: 100,000 сайтов -> Единый Геном Знаний"
echo "=================================================="

# 1. Генерация/Проверка списка сайтов
if [ ! -f "seeds/internet_map_100k.txt" ]; then
    echo "[1/3] Генерация карты интернета..."
    python3 scripts/generate_sites.py
else
    echo "[1/3] Карта интернета обнаружена."
fi

# 2. Подготовка пайплайна
echo "[2/3] Запуск конвейера: Crawler -> Ingester -> Genome"

# Используем именованные каналы для эффективности (stream processing)
# Python Crawler читает список сайтов и выдает поток данных
# Kolibri Ingest читает поток и обновляет genome.dat

# Создаем pipe если нет
# Но для простоты используем стандартный pipe |

cat seeds/internet_map_100k.txt | \
python3 backend/python/universal_parser.py | \
./build/kolibri_ingest

echo "=================================================="
echo "[3/3] Обучение завершено. Знания упакованы."
echo "Проверьте genome.dat и логи."

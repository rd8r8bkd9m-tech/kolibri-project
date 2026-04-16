#!/bin/bash
# Скрипт для создания и активации SWAP файла
# Используется для улучшения стабильности при запуске большого количества узлов
# Требует привилегий sudo

SWAP_SIZE="4G"
SWAP_FILE="/swapfile"

echo "[System] Проверка наличия swap..."

if [ $(swapon --show | wc -l) -gt 0 ]; then
    echo "[System] Swap уже активен:"
    swapon --show
    free -h
    exit 0
fi

echo "[System] Swap не найден. Создаем swap файл размером $SWAP_SIZE..."

# Проверка свободного места
FREE_SPACE=$(df -h / | awk 'NR==2 {print $4}')
echo "[System] Свободное место на диске: $FREE_SPACE"

if [ -f "$SWAP_FILE" ]; then
    echo "[System] Файл $SWAP_FILE уже существует, но не активен. Активируем..."
else
    echo "[System] Выделение места под $SWAP_FILE..."
    sudo fallocate -l $SWAP_SIZE $SWAP_FILE
    if [ $? -ne 0 ]; then
        echo "[System] fallocate не сработал, пробуем dd..."
        sudo dd if=/dev/zero of=$SWAP_FILE bs=1M count=4096
    fi
    
    echo "[System] Установка прав доступа..."
    sudo chmod 600 $SWAP_FILE
    
    echo "[System] Создание файловой системы swap..."
    sudo mkswap $SWAP_FILE
fi

echo "[System] Активация swap..."
sudo swapon $SWAP_FILE

if [ $? -eq 0 ]; then
    echo "[System] Swap успешно активирован!"
    echo "[System] Текущий статус памяти:"
    free -h
else
    echo "[Ошибка] Не удалось активировать swap. Возможно, не хватает привилегий (в контейнерах часто ограничено)."
fi

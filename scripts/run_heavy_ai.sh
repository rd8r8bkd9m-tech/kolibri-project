#!/bin/bash
# Kolibri OS - Heavy AI Swarm (64-Core Optimization)
# Запускает 64 тяжелых процесса для полной загрузки всех ядер.

# Настройки для "настоящего ИИ"
CORES=64
BASE_PORT=4000
SESSION_ID="heavy_ai_$(date +%s)"
LOG_DIR="logs/swarm_$SESSION_ID"
DATA_DIR=".kolibri/swarm_$SESSION_ID"

mkdir -p "$LOG_DIR"
mkdir -p "$DATA_DIR"

# Генерируем общий ключ для роевого интеллекта
if [ ! -f "root.key" ]; then
    echo "Генерация ключа роя..."
    dd if=/dev/urandom of=root.key bs=32 count=1 2>/dev/null
fi

echo "=================================================="
echo " ЗАПУСК ТЯЖЕЛОГО ИИ (High-Performance Computing)"
echo " Ядер: $CORES | Режим: Deep Architecture"
echo "=================================================="

# Функция-обертка для ноды
run_node() {
    local id=$1
    local port=$((BASE_PORT + id))
    local log_file="$LOG_DIR/node_$id.log"
    
    # Запуск с привязкой к ядру (taskset если доступен, иначе просто фоном)
    # Используем unbuffer для сохранения вывода
    ./build/kolibri_node --port $port --genome "$DATA_DIR/genome_$id.dat" --root-key root.key > "$log_file" 2>&1 &
    local pid=$!
    echo $pid > "$LOG_DIR/node_$id.pid"
    
    # Даем инициализироваться
    sleep 3
    
    # Команды для ноды:
    # 1. Загрузить общие знания (если есть)
    # 2. Включить режим массивного обучения
    # 3. Периодически синхронизироваться
    
    # Мы используем именованные каналы или просто отправляем сигналы? 
    # В текущей реализации kolibri_node читает stdin.
    # Это проблема для фона. 
    # Вариант: отправить начальные команды и оставить работать?
    # Но kolibri_node ждет команд в интерактивном режиме и выходит при EOF stdin?
    # Нужно проверить код.
}

# Очистка
trap 'echo "Остановка ИИ..."; kill $(cat "$LOG_DIR"/*.pid 2>/dev/null); exit' INT TERM

# Запуск роя
echo "Инициализация нейронов..."
for ((i=0; i<CORES; i++)); do
    # Создаем input pipe для каждой ноды
    pipe="$LOG_DIR/input_$i"
    mkfifo "$pipe"
    
    # Запускаем ноду, читающую из pipe
    ./build/kolibri_node --port $((BASE_PORT + i)) --genome "$DATA_DIR/genome_$i.dat" --root-key root.key < "$pipe" > "$LOG_DIR/node_$i.log" 2>&1 &
    echo $! > "$LOG_DIR/node_$i.pid"
    
    # Держим pipe открытым
    sleep 0.1
    # Инициализация:
    # Интенсивное обучение
    echo ":teach 1+1=2" > "$pipe"   # Примитивная затравка
    # Запускаем бесконечные циклы эволюции через скрипт команд
    (
        while true; do
            # Эволюционный скачок
            echo ":tick 500" 
            # Прокачиваем интеллект сложным вопросом
            echo ":ask Настоящий_ИИ"
            # Проверка прогресса
            echo ":stats"
            # Синхронизация (симуляция)
            # echo ":sync" 
            sleep 5
        done
    ) > "$pipe" &
    
    echo -ne "\rЗапущено ядер: $((i+1))/$CORES"
done

echo -e "\n\nСистема работает. Мониторинг логов..."
echo "Для остановки нажмите Ctrl+C"

# Мониторинг производительности
while true; do
    clear
    echo "=== СОСТОЯНИЕ ИИ [$SESSION_ID] ==="
    echo "Активных процессов: $(ls "$LOG_DIR"/*.pid 2>/dev/null | wc -l)"
    echo "Использование памяти: $(free -h | grep Mem | awk '{print $3 "/" $2}')"
    # Показать последние строки случайного лога
    rand_id=$(( RANDOM % CORES ))
    echo -e "\n--- Активность нейрона #$rand_id ---"
    tail -n 5 "$LOG_DIR/node_$rand_id.log" 2>/dev/null
    
    sleep 2
done

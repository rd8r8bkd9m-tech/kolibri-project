#!/usr/bin/env bash
# Управление Kolibri swarm кластером: мониторинг, остановка, диагностика

PROJECT_ROOT="/Users/kolibri/Desktop/kolibri-project"
COMMAND="${1:-help}"

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Функции
print_header() {
    echo -e "${BLUE}=== $1 ===${NC}"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

# === СТАТУС ===
status() {
    print_header "СТАТУС SWARM КЛАСТЕРА"
    
    local count=$(pgrep -f 'uvicorn.*kolibri' | wc -l)
    echo "Активных процессов: $count"
    
    if [ $count -eq 0 ]; then
        print_error "Узлы не запущены"
        return 1
    fi
    
    print_success "Процессы запущены"
    echo ""
    
    # Память
    if [[ "$OSTYPE" == "darwin"* ]]; then
        local memory=$(ps aux | grep 'uvicorn' | grep -v grep | awk '{s+=$6} END {print s/1024 " MB"}')
    else
        local memory=$(ps aux | grep 'uvicorn' | grep -v grep | awk '{s+=$6} END {print s/1024 " MB"}')
    fi
    
    echo "Использование памяти: $memory"
    
    # Порты
    echo ""
    print_header "АКТИВНЫЕ ПОРТЫ"
    lsof -i -P -n 2>/dev/null | grep uvicorn | awk '{print $9}' | sort | uniq | head -20
    
    echo "..."
    local port_count=$(lsof -i -P -n 2>/dev/null | grep uvicorn | wc -l)
    echo "Всего портов: $((port_count / 2)) (lsof reported $port_count lines)"
}

# === ПАМЯТЬ ===
memory() {
    print_header "АНАЛИЗ ПАМЯТИ"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        print_header "Дюпль macOS"
        echo "Общая память:"
        wc -c < /dev/zero 2>/dev/null | tr -d '\0' | wc -c
        
        echo "vm_stat:"
        vm_stat | grep -E "Pages (active|inactive|wired)" | head -5
        
        echo ""
        echo "Top процессы в Python:"
        ps aux | grep -E 'python|uvicorn' | grep -v grep | sort -k 6 -rn | head -5 | \
            awk '{printf "  %-20s %6s MB  %6s %% CPU\n", substr($11,1,20), $6/1024, $3}'
    else
        echo "Свободная память:"
        free -h | head -2
        
        echo ""
        echo "Top процессы:"
        ps aux --sort=-%mem | head -10 | grep -E 'python|uvicorn'
    fi
}

# === ОСТАНОВКА ===
stop() {
    print_header "ОСТАНОВКА УЗЛОВ"
    
    local count=$(pgrep -f 'uvicorn.*kolibri' | wc -l)
    
    if [ $count -eq 0 ]; then
        print_warning "Нет активных узлов"
        return 0
    fi
    
    echo "Останавливаем $count процессов..."
    
    pkill -f 'uvicorn.*kolibri'
    sleep 1
    
    local remaining=$(pgrep -f 'uvicorn.*kolibri' | wc -l)
    
    if [ $remaining -eq 0 ]; then
        print_success "Все узлы остановлены"
    else
        print_warning "$remaining процессов ещё работают, убиваем..."
        pkill -9 -f 'uvicorn.*kolibri'
        sleep 1
        print_success "Принудительно остановлены"
    fi
    
    rm -f /tmp/kolibri_nodes_*.pid
}

# === ЛОГИ ===
logs() {
    local node_id="${2:-0}"
    local port=$((8001 + node_id))
    local logfile="/tmp/kolibri_node_${port}.log"
    
    if [ ! -f "$logfile" ]; then
        print_error "Лог-файл не найден: $logfile"
        echo "Доступные логи:"
        ls -la /tmp/kolibri_node_*.log 2>/dev/null || echo "  (неизвестно)"
        return 1
    fi
    
    print_header "ЛОГИ УЗЛА $node_id (порт $port)"
    tail -100 "$logfile"
}

# === ЗДОРОВЬЕ ===
health() {
    print_header "ПРОВЕРКА ЗДОРОВЬЯ УЗЛОВ"
    
    local base_port=8001
    local healthy=0
    local unhealthy=0
    
    for i in $(seq 0 49); do
        local port=$((base_port + i))
        
        if timeout 0.5 curl -s "http://127.0.0.1:$port/api/v1/learning/status" >/dev/null 2>&1; then
            healthy=$((healthy + 1))
            if [ $((i % 10)) -eq 0 ]; then
                echo -n "."
            fi
        else
            unhealthy=$((unhealthy + 1))
        fi
    done
    
    echo ""
    echo ""
    print_success "$healthy узлов ответили"
    if [ $unhealthy -gt 0 ]; then
        print_error "$unhealthy узлов не ответили"
    fi
}

# === СИНХРОНИЗАЦИЯ ===
sync_check() {
    print_header "ПРОВЕРКА СИНХРОНИЗАЦИИ"
    
    echo "Посещение endpoint /api/v1/swarm/sync первых 3 узлов:"
    
    for i in 0 1 2; do
        local port=$((8001 + i))
        echo ""
        echo "Узел $i (порт $port):"
        
        local resp=$(curl -s -m 1 "http://127.0.0.1:$port/api/v1/swarm/sync" 2>/dev/null)
        if [ -z "$resp" ]; then
            print_warning "Нет ответа"
        else
            echo "$resp" | python3 -m json.tool 2>/dev/null | head -20 || echo "$resp" | head -5
        fi
    done
}

# === ОЧИСТКА ===
clean() {
    print_header "ОЧИСТКА"
    
    echo "Удаляю:"
    rm -fv /tmp/kolibri_nodes_*.pid
    rm -fv /tmp/kolibri_node_*.log
    
    print_success "Очищено"
}

# === ПОМОЩЬ ===
help() {
    cat << 'EOF'
┌─ Kolibri Swarm Manager ─────────────────────────────────────────┐
│                                                                  │
│  Использование:  ./swarm_manager.sh <команда>                  │
│                                                                  │
│  КОМАНДЫ:                                                       │
│                                                                  │
│  status          Статус кластера (процессы, порты, память)     │
│  memory          Анализ использования памяти                    │
│  health          Проверить здоровье узлов (HTTP пинги)         │
│  logs [N]        Показать логи узла N (default: 0)             │
│  sync_check      Проверить синхронизацию узлов                 │
│  stop            Остановить все узлы                           │
│  clean           Удалить PID и лог файлы                      │
│  help            Эта справка                                   │
│                                                                  │
│  ПРИМЕРЫ:                                                       │
│                                                                  │
│    ./swarm_manager.sh status                                   │
│    ./swarm_manager.sh memory                                   │
│    ./swarm_manager.sh logs 10          # Лог узла 10 (порт 8011)│
│    ./swarm_manager.sh health                                   │
│    ./swarm_manager.sh stop                                     │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
EOF
}

# === ГЛАВНЫЙ ДИСПЕТЧЕР ===
case "$COMMAND" in
    status)  status ;;
    memory)  memory ;;
    health)  health ;;
    logs)    logs "$@" ;;
    sync_check) sync_check ;;
    stop)    stop ;;
    clean)   clean ;;
    help)    help ;;
    *)
        print_error "Неизвестная команда: $COMMAND"
        echo ""
        help
        exit 1
        ;;
esac

#!/bin/bash
# 📋 QUICK REFERENCE — Команды для управления Kolibri Swarm 50 узлов
# Скопируй нужную команду и выполни в терминале

# 🚀 ЗАПУСК
echo "=== ЗАПУСК ==="
echo "cd /Users/kolibri/Desktop/kolibri-project/swarm && ./start_50_nodes.sh"

# 📊 МОНИТОРИНГ
echo ""
echo "=== МОНИТОРИНГ (в новом терминале) ==="
echo "./swarm_manager.sh status        # Общий статус"
echo "./swarm_manager.sh memory        # Анализ памяти"
echo "./swarm_manager.sh health        # Проверить узлы (пинги)"
echo "./swarm_manager.sh sync_check    # Синхронизация"

# 📖 ЛОГИ
echo ""
echo "=== ЛОГИ ==="
echo "./swarm_manager.sh logs 0        # Лог узла 0 (порт 8001)"
echo "tail -f /tmp/kolibri_node_8001.log  # Live лог узла 0"
echo "tail -f /tmp/kolibri_node_8010.log  # Live лог узла 10"

# 🛑 ОСТАНОВКА
echo ""
echo "=== ОСТАНОВКА ==="
echo "./swarm_manager.sh stop          # Нормально остановить"
echo "pkill -9 uvicorn                 # Силой (если завис)"

# 🔍 ДИАГНОСТИКА
echo ""
echo "=== ДИАГНОСТИКА ==="
echo "python3 optimize_swarm_config.py # Анализ системы и рекомендации"
echo "lsof -i :8001-8050              # Какие порты заняты"
echo "ps aux | grep uvicorn            # Активные процессы"

# 🧹 ОЧИСТКА
echo ""
echo "=== ОЧИСТКА ==="
echo "./swarm_manager.sh clean         # Удалить PID и логи"

# 📡 ПРОВЕРКА КОНКРЕТНОГО УЗЛА
echo ""
echo "=== ПРОВЕРКА УЗЛА (HTTP) ==="
echo "curl http://127.0.0.1:8001/api/v1/learning/status | python3 -m json.tool"
echo "curl http://127.0.0.1:8010/api/v1/swarm/sync"

# 🎯 ОПТИМИЗИРОВАННЫЙ ЗАПУСК (если первый не сработал)
echo ""
echo "=== АЛЬТЕРНАТИВА: МАКСИМАЛЬНАЯ ОПТИМИЗАЦИЯ ==="
echo "./start_50_nodes_optimized.sh"

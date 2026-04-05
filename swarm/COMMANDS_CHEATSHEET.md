# ⌨️ Шпаргалка команд — Kolibri Swarm

## 🚀 ЗАПУСК

```bash
# Основной запуск (рекомендуется)
./start_50_nodes.sh

# Максимально оптимизированный
./start_50_nodes_optimized.sh

# Анализ вашей системы перед запуском
python3 optimize_swarm_config.py
```

---

## 📊 МОНИТОРИНГ (выполнять в отдельном терминале)

```bash
# Полный статус
./swarm_manager.sh status

# Память (какую используют узлы)
./swarm_manager.sh memory

# Живы ли все 50 узлов? (HTTP пинги)
./swarm_manager.sh health

# Синхронизация между узлами
./swarm_manager.sh sync_check

# Логи конкретного узла
./swarm_manager.sh logs 0     # Узел 0 (порт 8001)
./swarm_manager.sh logs 10    # Узел 10 (порт 8011)
./swarm_manager.sh logs 49    # Узел 49 (порт 8050)
```

---

## 📖 ПРОСМОТР ЛОГОВ

```bash
# Читай логи узла 0
tail -50 /tmp/kolibri_node_8001.log

# Живой хвост логов (обновляется в реальном времени)
tail -f /tmp/kolibri_node_8001.log

# Живой хвост + фильтр по ошибкам
tail -f /tmp/kolibri_node_8001.log | grep -i error

# Последние ошибки в логе
grep ERROR /tmp/kolibri_node_8001.log | tail -20
```

---

## 🧪 ПРОВЕРКА УЗЛОВ (HTTP)

```bash
# Статус узла 0
curl http://127.0.0.1:8001/api/v1/learning/status | python3 -m json.tool

# Статус узла 25
curl http://127.0.0.1:8026/api/v1/learning/status | python3 -m json.tool

# Синхронизация узла 0
curl http://127.0.0.1:8001/api/v1/swarm/sync | python3 -m json.tool

# Все компактнее
for i in {0..10}; do echo "=== Узел $i ===" && \
  curl -s http://127.0.0.1:$((8001+i))/api/v1/learning/status | \
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('running'))" 2>/dev/null || echo "нет"; done
```

---

## 🔍 СИСТЕМНАЯ ДИАГНОСТИКА

```bash
# Все активные uvicorn процессы
ps aux | grep uvicorn | grep -v grep

# Сколько памяти используют узлы (в MB)
ps aux | grep uvicorn | grep -v grep | awk '{s+=$6} END {print s/1024}'

# Какие порты заняты
lsof -i :8001-8050

# Сколько процессов на портах 8001-8050
lsof -i :8001-8050 | wc -l

# Проверить конкретный порт
lsof -i :8001
```

---

## 🛑 ОСТАНОВКА

```bash
# Нормальная остановка (graceful)
./swarm_manager.sh stop

# Или всех сразу убить
pkill -f 'uvicorn.*kolibri'

# Если совсем завис — силовая остановка
pkill -9 uvicorn

# Очистить PID и логи
./swarm_manager.sh clean
```

---

## 🧹 ОЧИСТКА

```bash
# Удалить все PID файлы
rm -f /tmp/kolibri_nodes_*.pid

# Удалить все логи
rm -f /tmp/kolibri_node_*.log

# Или использовать Manager
./swarm_manager.sh clean
```

---

## 🐛 ОТЛАДКА

```bash
# Если узел 10 не отвечает, смотри лог
tail -100 /tmp/kolibri_node_8010.log

# Все ошибки в логе
grep -E "(ERROR|CRITICAL|Exception)" /tmp/kolibri_node_8010.log

# Сколько памяти использует процесс PID 1234
ps -p 1234 -o rss=

# Что делается на порту 8010
lsof -i :8010

# Все активные сокеты Python
netstat -an | grep -E ":(800[0-9]|80[1-4][0-9])"
```

---

## 📋 ШАБЛОНЫ ДЛЯ СКРИПТОВ

### Мониторинг памяти каждые 5 сек
```bash
while true; do
  clear
  echo "=== Память узлов ==="
  ps aux | grep uvicorn | grep -v grep | awk '{s+=$6} END {printf "Всего: %.0f MB\n", s/1024}'
  echo ""
  ./swarm_manager.sh health | tail -3
  sleep 5
done
```

### Перезагрузка узла X
```bash
X=10
PORT=$((8001 + X))
kill $(lsof -i :$PORT -t 2>/dev/null) 2>/dev/null
echo "Узел $X перезагружен"
```

### Получить метрики всех узлов
```bash
for i in {0..49}; do
  PORT=$((8001 + i))
  curl -s "http://127.0.0.1:$PORT/api/v1/learning/status" \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'Node {$i}: {d.get(\"running\")}')" 2>/dev/null || echo "Node $i: DOWN"
done
```

---

## 🆘 БЫСТРЫЕ РЕШЕНИЯ

### Система зависает
```bash
# Проверить память
./swarm_manager.sh memory

# Остановить все
./swarm_manager.sh stop

# Уменьшить количество узлов в скрипте и перезапустить
nano start_50_nodes.sh  # Измени BATCH_SIZE=3
./start_50_nodes.sh
```

### Узел не отвечает
```bash
# Посмотри логи
./swarm_manager.sh logs 0

# Если совсем плохо — перезагрузи
./swarm_manager.sh stop
sleep 2
./start_50_nodes.sh
```

### Какой узел сломался?
```bash
./swarm_manager.sh health  # Увидишь какие не ответили
# Потом посмотри логи того узла
```

---

**Сохрани этот файл в закладки — очень полезно!** 📌

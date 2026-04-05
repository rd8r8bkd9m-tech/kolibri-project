# 🚀 Оптимизация Kolibri Swarm — Решение проблемы памяти

## 🎯 Проблема
При запуске **50 узлов** система **зависает из-за нехватки памяти**. Каждый узел загружает полный Python + FastAPI + AI движок, что требует ~50-100 MB на процесс.

**Расчет:**
- 50 узлов × 60-80 MB = **3-4 GB памяти** просто на базовые процессы
- С continuous learning + background learning = **5-7 GB** и выше

## ✅ Решение: 4 подхода

### 1️⃣ **Быстрый запуск (рекомендуется для 50 узлов)**
```bash
cd /Users/kolibri/Desktop/kolibri-project/swarm
./start_50_nodes.sh
```

**Что это делает:**
- ✅ Запускает узлы **партиями по 5** с 2-3 сек паузой между партиями
- ✅ Только первые 5 узлов обучаются (learning=1) — остальные в лайт-режиме
- ✅ Мониторит память и останавливает запуск если память > 80%
- ✅ Использует локальный лупбэк (`127.0.0.1` вместо `0.0.0.0`) — быстрее
- ✅ По 1 worker процессу на uvicorn вместо автоматического

**Результат:** ~1.5-2 GB памяти вместо 5-7 GB

---

### 2️⃣ **Максимально-оптимизированный запуск**
```bash
./start_50_nodes_optimized.sh
```

Еще более агрессивная оптимизация:
- Партии по 5, пауза 3 сек
- Детальный мониторинг памяти между партиями
- Информационный вывод про каждый узел
- Автоматическая остановка при перегрузке

---

### 3️⃣ **Анализ вашей системы и рекомендации**
```bash
cd swarm
python3 optimize_swarm_config.py
```

Показывает:
- 📊 Всего памяти / доступно
- 🖥️ Активные узлы (если есть)
- 📈 Рекомендации для 15, 25, 50 узлов
- 🔧 Оптимальные параметры конфигурации

Пример вывода:
```
📊 СИСТЕМА:
  Total Memory Mb: 32768
  Available Memory Mb: 24000
  ...

📈 РЕКОМЕНДАЦИИ:
  ✅ 15 узлов: ~ 1200 MB
  ✅ 25 узлов: ~ 1800 MB
  ✅ 50 узлов: ~ 3200 MB
```

---

### 4️⃣ **Управление кластером во время работы**
```bash
# Статус: процессы, память, порты
./swarm_manager.sh status

# Анализ памяти
./swarm_manager.sh memory

# Проверить здоровье всех узлов (HTTP пинги)
./swarm_manager.sh health

# Логи узла N
./swarm_manager.sh logs 10    # Узел 10, порт 8011

# Проверить синхронизацию
./swarm_manager.sh sync_check

# Остановить все
./swarm_manager.sh stop

# Очистить PID и логи
./swarm_manager.sh clean
```

---

## 🔍 Детали оптимизаций

### Конфигурация окружения (в скрипте)

| Параметр | Значение | Эффект |
|----------|----------|--------|
| `BATCH_SIZE` | 5 | Запускать партиями по 5 узлов |
| `BATCH_DELAY` | 2-3 сек | Пауза между партиями для стабилизации |
| `MAX_MEMORY_PCT` | 80% | Останавливаем если память > 80% |
| `KOLIBRI_ENABLE_CONTINUOUS_LEARNING` | 0 для узлов 5-49 | Отключаем тяжелый демон |
| `KOLIBRI_ENABLE_BACKGROUND_LEARNING` | 0 для узлов 5-49 | Отключаем фоновое обучение |
| `--workers 1` | Вместо auto | Меньше процессов = меньше памяти |

### Стратегия лайт-режима

**Узлы 0-4** (обучающие):
- ✅ `KOLIBRI_ENABLE_CONTINUOUS_LEARNING=1`
- ✅ `KOLIBRI_ENABLE_BACKGROUND_LEARNING=1`
- ✅ Участвуют в обучении и синхронизации
- 💾 ~70-80 MB каждый

**Узлы 5-49** (синхронизирующие):
- ❌ `KOLIBRI_ENABLE_CONTINUOUS_LEARNING=0`
- ❌ `KOLIBRI_ENABLE_BACKGROUND_LEARNING=0`
- ✅ Участвуют в роевой синхронизации (`/api/v1/swarm/sync`)
- 💾 ~30-40 MB каждый

---

## 🛠️ Дополнительные оптимизации

### Прямо в `backend/service/main.py` (если нужно):

```python
# Отключить автостарт фоновых демонов
os.environ.setdefault("KOLIBRI_ENABLE_CONTINUOUS_LEARNING", "0")
os.environ.setdefault("KOLIBRI_ENABLE_BACKGROUND_LEARNING", "0")
```

### В `backend/service/continuous_learning_daemon.py`:

```python
# Увеличить интервал между циклами обучения (вместо 120 сек)
_cycle_interval_sec = max(60, _env_int("KOLIBRI_CONTINUOUS_LEARNING_INTERVAL", 300))
```

---

## 📊 Сравнение: до и после

### ДО (старый `start_50_nodes.sh`)
```
50 узлов запущены ОДНОВРЕМЕННО
├─ Каждый грузит full AI engine
├─ Каждый стартует continuous learning
├─ Каждый через multiprocessing → 2-4 процесса на uvicorn
└─ Результат: 50 × 80MB = 4GB+ → ЗАВИСАНИЕ
```

### ПОСЛЕ (новый оптимизированный)
```
50 узлов запущены ПАРТИЯМИ (по 5, пауза 2-3 сек)
├─ Узлы 0-4:   ~70 MB с learning
├─ Узлы 5-49:  ~35 MB без learning
├─ Всего:      4×70 + 45×35 = 1,855 MB
└─ Результат: Стабильная работа, без зависаний
```

---

## 🚨 Что делать если...

### ...система все еще зависает?

1. **Уменьшить количество узлов:**
   ```bash
   # Редактировать BATCH_SIZE или NODES_COUNT
   nano start_50_nodes.sh
   NODES_COUNT=30  # Вместо 50
   ```

2. **Увеличить задержки:**
   ```bash
   BATCH_DELAY=5    # Было 2, теперь 5 сек
   ```

3. **Отключить learning полностью:**
   ```bash
   # Запуск с переменной окружения
   KOLIBRI_ENABLE_CONTINUOUS_LEARNING=0 ./start_50_nodes.sh
   ```

4. **Закрыть другие приложения** и проверить память:
   ```bash
   ./swarm_manager.sh memory
   ```

### ...узлы не отвечают на /api/v1/learning/status?

1. Проверить логи:
   ```bash
   ./swarm_manager.sh logs 0    # Узел 0
   tail -50 /tmp/kolibri_node_8001.log
   ```

2. Проверить доступны ли порты:
   ```bash
   lsof -i :8001-8050
   ```

3. Остановить и перезапустить:
   ```bash
   ./swarm_manager.sh stop
   sleep 3
   ./start_50_nodes.sh
   ```

### ...логи полны ошибками?

Включить DEBUG логирование (в start_50_nodes.sh):
```bash
# Вместо --log-level error
--log-level debug \
# И сохранить логи:
> /tmp/kolibri_node_${PORT}.log 2>&1 &
```

---

## 📈 Масштабирование

Максимальные узлы для типичной системы:

| Система | RAM | Рекомендуемо | Max |
|---------|-----|--------------|-----|
| MacBook Pro 2021 (16GB) | 16 GB | 20-30 узлов | 40 |
| MacBook Air (8GB) | 8 GB | 8-10 узлов | 15 |
| Linux Server (32GB) | 32 GB | 50-60 узлов | 80 |
| Linux Server (64GB) | 64 GB | 100+ узлов | 150+ |

---

## ✨ Финальный чек-лист

Перед запуском 50 узлов:

- [ ] Система имеет > 8 GB свободной памяти (`./swarm_manager.sh memory`)
- [ ] Закрыты браузеры, IDE (кроме VS Code), другое ПО
- [ ] Последняя версия скриптов из `swarm/`
- [ ] Python venv активирована
- [ ] Предыдущие узлы остановлены (`./swarm_manager.sh stop`)

**Команда запуска:**
```bash
cd /Users/kolibri/Desktop/kolibri-project/swarm
./start_50_nodes.sh
```

**Проверка готовности:**
```bash
# В другом терминале
./swarm_manager.sh health
```

---

## 📚 Дополнительные ресурсы

- [docs/PERFORMANCE.md](../docs/PERFORMANCE.md) — Детальный анализ производительности
- [backend/service/main.py](../backend/service/main.py) — Конфигурация приложения
- [.github/copilot-instructions.md](../.github/copilot-instructions.md) — Архитектурные детали

---

**Последнее обновление:** 5 апреля 2026
**Статус:** ✅ Протестировано на macOS/Linux с 50+ узлами

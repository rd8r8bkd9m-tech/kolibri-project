# Backend — Kolibri AI Platform

## Обзор

Backend Kolibri состоит из двух частей:
1. **C-ядро** (`src/` + `include/kolibri/`) — 35 модулей на C23
2. **FastAPI-сервис** (`service/`) — 14 Python-модулей

---

## Структура

```
backend/
├── include/kolibri/          # 27 публичных C-заголовков
├── src/                      # 35 C-модулей
├── service/                  # FastAPI backend (14 модулей)
│   ├── main.py               # Точка входа FastAPI
│   ├── common.py             # Settings, InferenceRequest, LLM-прокси
│   ├── number_mind.py        # Ядро числового мышления (1335 строк)
│   ├── ai_chat.py            # AI Chat роутер (/api/v1/ai/*)
│   ├── ai_engine.py          # Singleton AI-движок
│   ├── agent.py              # Автономный агент обучения (/api/v1/agent/*)
│   ├── crawler.py            # Веб-краулер (/api/v1/crawl)
│   ├── search_engine.py      # DDG/Bing/Wiki поиск
│   ├── swarm_sync.py         # P2P-синхронизация (/api/v1/swarm/*)
│   ├── gpu_store.py          # GPU-хранилище (/api/gpu/*)
│   ├── os_bridge.py          # ОС-мост (/api/dev/*)
│   ├── knowledge_base.py     # Построение базы знаний
│   └── __init__.py
├── feedback_service/         # RLHF Feedback Pipeline
│   ├── main.py               # FastAPI сервис обратной связи
│   ├── database.py           # SQLite/PostgreSQL доступ
│   ├── repository.py         # Репозиторий обратной связи
│   ├── schemas.py            # Pydantic-модели
│   └── rlhf_dataset.py       # Экспорт в RLHF-формат
├── python/                   # Python-обёртки
│   ├── kolibri_compress.py   # Обёртка над C-компрессией
│   └── universal_parser.py   # Парсер любых форматов
├── Dockerfile
└── __init__.py
```

---

## FastAPI-модули

### `main.py` — Точка входа

```python
app = FastAPI(title="Kolibri AI backend", version="0.2.0")
```

Подключает роутеры:
- `gpu_router` → `/api/gpu`
- `factory_router` → `/api/factory`
- `os_router` → `/api/dev`
- `crawler_router` → `/api/v1`
- `agent_router` → `/api/v1/agent`
- `ai_router` → `/api/v1/ai`
- `swarm_router` → `/api/v1/swarm`

Собственные эндпоинты:
- `GET /api/health` — healthcheck
- `GET /api/knowledge/healthz` — health базы знаний
- `POST /api/v1/infer` — LLM-прокси (если `KOLIBRI_RESPONSE_MODE=llm`)

### `number_mind.py` — Ядро числового мышления (1335 строк)

Самый большой модуль. Содержит:

| Класс/Функция | Описание |
|----------------|----------|
| `word_to_pattern(word)` | Слово → 64-цифровой паттерн (DJB2 + LCG) |
| `text_to_digits(text)` | Текст → массив цифр (3 цифры на UTF-8 байт) |
| `digits_to_text(digits)` | Обратное восстановление цифр → текст |
| `KolibriGene` | Геном из 4000 цифр с мутациями и кроссовером |
| `FormulaPool` | Популяция из 16 формул + эволюция + predict |
| `KnowledgeGraph` | Граф знаний (паттерны слов + взвешенные рёбра) |
| `SentenceStore` | TF-IDF хранилище предложений |
| `_run_layers()` | ResNet-архитектура: 50 блоков × 10 слоёв |

**Константы:**
```python
KLM_PATTERN_SIZE = 64       # Цифр в паттерне слова
GENE_SIZE = 4000            # Цифр в геноме (500 слоёв × 8)
FORMULA_LAYERS = 500        # Слоёв формульной сети
MAX_ASSOCIATIONS = 10000    # Макс ассоциаций Q→A
POPULATION_SIZE = 16        # Формул в популяции
```

### `ai_chat.py` — AI Chat роутер

| Эндпоинт | Метод | Описание |
|----------|-------|----------|
| `/api/v1/ai/chat` | POST | Главный AI-чат |
| `/api/v1/ai/train` | POST | Обучение с верификацией |
| `/api/v1/ai/pattern` | POST | Числовой паттерн слова |
| `/api/v1/ai/embedding` | POST | Числовой embedding |
| `/api/v1/ai/stats` | GET | Статистика движка |
| `/api/v1/ai/reload` | POST | Перезагрузка корпуса |

### `agent.py` — Автономный агент

Архитектура агента:
```
[Topic] → [Search Engines (DDG/Bing/Wiki)]
  → [URLs] → [Python Crawler] → [Text Files]
    → [C Mass Trainer --dir] → [KLM Model]
    → [Python train_text()] → [KnowledgeGraph + SentenceStore + Формулы]
```

### `swarm_sync.py` — P2P-синхронизация

Протокол обмена знаниями:
1. `POST /register` — регистрация узла (адрес, порт, метаданные)
2. `POST /sync` — обмен паттернами и рёбрами графа
3. `GET /peers` — список активных пиров
4. `GET /status` — общий статус роя

Контроль целостности: SHA-256 хеши при синхронизации.

### `gpu_store.py` — GPU-хранилище

SQLite-база с векторными данными:
- Хранение: IEEE-754 BLOB
- Поиск: Cosine Similarity
- Fallback при отсутствии GPU

### `content_factory.py` — Контент-фабрика

SQLAlchemy + SQLite:
- `ContentItemDB` — контент-записи
- `TrendInsightDB` — тренды
- `VideoReferenceDB` — видео-референсы

### `os_bridge.py` — ОС-мост

Безопасный доступ к файловой системе:
- `GET /api/dev/ls` — листинг (только `/workspaces/kolibri-project`)
- `POST /api/dev/read` — чтение файлов
- `POST /api/dev/save` — запись файлов
- `GET /api/dev/system` — системная информация (psutil)

---

## C-ядро (35 модулей)

### Публичные заголовки (`include/kolibri/`)

| Заголовок | Описание |
|-----------|----------|
| `genome.h` | ReasonBlock (блокчейн-подобная цепочка событий), WAL-логирование |
| `formula.h` | KolibriGene (4000 цифр), FormulaPool, мутации, кроссовер |
| `knowledge.h` | Граф знаний (паттерны + рёбра) |
| `knowledge_index.h` | Индексация знаний |
| `knowledge_queue.h` | Очередь обучения |
| `net.h` | P2P-протокол (TCP, HMAC, 4200 байт payload) |
| `script.h` | Интерпретатор KolibriScript |
| `symbol_table.h` | Таблица символов |
| `decimal.h` | Десятичная арифметика |
| `digits.h` | Цифровые операции |
| `digit_text.h` | Преобразование текст↔цифры |
| `compress.h` | Компрессия (RLE, dictionary, hybrid) |
| `random.h` | ГСЧ (генератор случайных чисел) |
| `semantic.h` | Семантические цифровые паттерны |
| `phoneme.h` | Фонетические паттерны |
| `context.h` | Контекстное окно |
| `corpus.h` | Обучение на корпусах |
| `corpus_trainer.h` | Массовый тренер |
| `generation.h` | Генерация текста из числовых паттернов |
| `inference.h` | Инференс (предсказание) |
| `formula_logic.h` | Мета-формулы |
| `logical_memory.h` | Логическая память |
| `async_executor.h` | Асинхронный исполнитель правил |
| `trace.h` | Трассировка «Стеклянный Разум» (отладка) |
| `sim.h` | Симулятор |
| `roy.h` | Рой (кластерная логика) |
| `web_crawler.h` | Веб-краулер на C |

### Ключевые модули

#### `formula.c` — Формульный движок (ResNet)

```c
#define BACKEND_BLOCK_SIZE 10  // Слоёв в одном Residual-блоке

void formula_predict_numeric(const KolibriGene *gene,
                              double input,
                              size_t num_layers,
                              double *output);
```

12 операций на слой, ResNet skip-connections каждые 10 слоёв.

#### `genome.c` — Геномная цепочка

```c
typedef struct {
    uint64_t index;
    uint64_t timestamp;
    unsigned char prev_hash[32];
    unsigned char hmac[32];
    char event_type[32];
    char payload[512];
} ReasonBlock;
```

Блокчейн-подобная цепочка событий с HMAC-подписями и WAL-логированием.

#### `corpus_trainer.c` — Массовый тренер

Обучение модели на файлах и директориях. Используется агентом через subprocess.

---

## Запуск

```bash
# Backend
python -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001

# С автоматической перезагрузкой (dev)
python -m uvicorn backend.service.main:app --reload --port 8001

# Swagger UI
open http://localhost:8001/docs
```

---

## Зависимости

```
fastapi>=0.110
uvicorn>=0.29
pydantic>=2.0
httpx>=0.27
beautifulsoup4>=4.12
requests>=2.31
asyncpg>=0.29
sqlalchemy>=2.0
psutil>=5.9
coverage>=7.0
```

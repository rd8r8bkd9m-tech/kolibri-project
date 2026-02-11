# Kolibri OS — Python-бэкенд: Справочник сервисов

> Детальная документация всех Python-модулей (`backend/service/`)
>
> 32 модуля · 12 846 строк · Python 3.12+ · FastAPI

---

## Оглавление

1. [Архитектура бэкенда](#1-архитектура-бэкенда)
2. [Центральные модули](#2-центральные-модули)
3. [API-роутеры](#3-api-роутеры)
4. [Когнитивные модули](#4-когнитивные-модули)
5. [Безопасность и операции](#5-безопасность-и-операции)
6. [Сбор и обработка данных](#6-сбор-и-обработка-данных)
7. [Конфигурация и запуск](#7-конфигурация-и-запуск)

---

## 1. Архитектура бэкенда

### Стек

- **Runtime**: Python 3.12.3
- **Framework**: FastAPI 0.110+
- **Server**: uvicorn
- **Типизация**: strict (pyright + ruff)
- **БД**: SQLite (WAL mode)
- **Auth**: JWT (PyJWT)

### Middleware-цепочка

```
Запрос HTTP
  ↓
1. CORSMiddleware        # allow_origins=["*"]
  ↓
2. RateLimitMiddleware    # Token-bucket per IP (100 req/min)
  ↓
3. Router dispatch        # 15+ роутеров
  ↓
Ответ JSON
```

### Регистрация роутеров (main.py, 100 строк)

```python
app.include_router(gpu_router)          # /api/gpu
app.include_router(factory_router)      # /api/factory
app.include_router(os_router)           # /api/dev
app.include_router(crawler_router)      # /api/v1
app.include_router(agent_router)        # /api/v1/agent
app.include_router(ai_router)           # /api/v1/ai
app.include_router(swarm_router)        # /api/v1/swarm
app.include_router(dist_crawler_router) # /api/v1/crawler
app.include_router(delta_sync_router)   # /api/v1/delta
app.include_router(archiver_router)     # /api/v1/archiver
app.include_router(cognition_router)    # /api/v1/cognition
app.include_router(auth_router)         # /api/v1/auth
app.include_router(health_router)       # /api/v1/health
```

### Startup

```python
@app.on_event("startup")
async def startup_event():
    # Предзагрузка AI-движка в фоновом потоке
    threading.Thread(target=pre_init_engine, daemon=True).start()
```

---

## 2. Центральные модули

### 2.1. ai_engine.py — Центральный движок (1912 строк)

**Класс:** `KolibriAIEngine` (singleton)

Объединяет все AI-компоненты в единый интерфейс.

#### Компоненты

| Компонент | Класс | Модуль | Описание |
|-----------|-------|--------|----------|
| Граф знаний | `KnowledgeGraph` | `number_mind.py` | Паттерны + связи + BM25 |
| Эмбеддинги | `Word2Vec` | `embeddings.py` | Skip-gram 64-dim |
| Рассуждения | `ChainOfThought` | `reasoning.py` | Цепочка мышления |
| Контекст | `ContextWindow` | `context_window.py` | Память диалога |
| Когниция | `SwarmCognition` | `cognition.py` | 5 когнитивных способностей |
| Хранение | `PersistenceManager` | `persistence.py` | SQLite WAL-mode |

#### Поток обработки чата

```
chat(query: str) → str
  1. Поиск в KnowledgeGraph
     - BM25 ранжирование
     - Embedding cosine boost
     - Top-K результатов
  2. Chain-of-Thought reasoning
     - Разбиение на подзадачи
     - Пошаговое рассуждение
  3. Когнитивное обогащение
     - Абстрактное мышление
     - Каузальный вывод
  4. Генерация ответа
     - Ранжирование сниппетов
     - Сборка связного текста
  5. Сохранение контекста
     - ContextWindow.add(query, response)
  6. Persistence
     - SQLite: save_state()
```

#### Ключевые методы

```python
class KolibriAIEngine:
    def chat(self, query: str, max_answer_words: int = 100) -> dict
    def train_text(self, text: str, source: str = "api") -> dict
    def train_file(self, filepath: str) -> dict
    def evolve(self, generations: int = 10) -> dict
    def get_stats(self) -> dict
    def search(self, query: str, limit: int = 10) -> list
    def load_corpus(self, directory: str = "data/corpus") -> int
    def save_state(self) -> None
    def restore_state(self) -> None
```

---

### 2.2. number_mind.py — Граф знаний (2021 строк)

**Класс:** `KnowledgeGraph`

Центральное хранилище знаний с графовой структурой.

#### Структуры данных

```python
@dataclass
class KnowledgePattern:
    text: str                    # Текст паттерна
    digits: list[int]            # 64-цифровой числовой код
    frequency: int               # Частота встречаемости
    confidence: float            # Уверенность [0.0–1.0]
    source: str                  # Источник данных
    embedding: list[float]       # 64-dim вектор (опционально)

@dataclass
class KnowledgeEdge:
    source: str                  # Источник связи
    target: str                  # Цель связи
    weight: float                # Вес [0.0–1.0]
    edge_type: str               # Тип ("co_occurrence", "semantic", ...)
```

#### Поиск

1. **BM25 ранжирование**: TF-IDF с нормализацией длины документа
2. **N-граммы**: быстрый fuzzy-поиск по подстрокам
3. **Embedding boost**: cosine_similarity(query_embed, pattern_embed)
4. **Граф-обход**: BFS/DFS по связям для расширения контекста

#### Числовое кодирование

```python
def word_to_pattern(word: str) -> list[int]:
    """DJB2 хеш → LCG → 64 десятичных цифры"""
    hash_val = djb2(word)
    digits = []
    state = hash_val
    for _ in range(64):
        state = (state * 6364136223846793005 + 1) & 0xFFFFFFFF
        digits.append(state % 10)
    return digits
```

#### Ключевые методы

```python
class KnowledgeGraph:
    def add_pattern(self, text: str, source: str = "api") -> KnowledgePattern
    def add_edge(self, source: str, target: str, weight: float = 1.0) -> KnowledgeEdge
    def train_text(self, text: str, source: str = "api") -> dict
    def search(self, query: str, limit: int = 10) -> list[KnowledgePattern]
    def find_similar(self, word: str, limit: int = 5) -> list[tuple[str, float]]
    def get_neighbors(self, word: str, depth: int = 1) -> list[str]
    def get_stats(self) -> dict
    def export_dot(self) -> str  # Graphviz DOT формат
```

---

### 2.3. embeddings.py — Word2Vec (477 строк)

**Класс:** `Word2Vec`

Skip-gram модель для обучения словных эмбеддингов.

#### Параметры

| Параметр | Значение |
|----------|----------|
| Размерность | 64 |
| Окно контекста | 5 |
| Negative samples | 5 |
| Learning rate | 0.025 → 0.001 |
| Min frequency | 1 |

#### API

```python
class Word2Vec:
    def __init__(self, dim: int = 64, window: int = 5)
    def train(self, sentences: list[list[str]], epochs: int = 5) -> None
    def get_embedding(self, word: str) -> list[float] | None
    def similarity(self, word1: str, word2: str) -> float
    def most_similar(self, word: str, top_k: int = 5) -> list[tuple[str, float]]
    def save(self, path: str) -> None
    def load(self, path: str) -> None
```

---

### 2.4. reasoning.py — Цепочка рассуждений (324 строки)

**Класс:** `ChainOfThought`

#### Стратегии поиска

```python
def get_search_strategy(query: str) -> str:
    """Определяет стратегию: 'exact', 'semantic', 'broad'"""
```

#### API

```python
class ChainOfThought:
    def reason(self, query: str, context: list[str]) -> dict
    def decompose(self, query: str) -> list[str]  # Разбиение на подзадачи
    def synthesize(self, steps: list[dict]) -> str  # Синтез ответа
```

---

### 2.5. context_window.py — Контекст диалога (130 строк)

**Класс:** `ContextWindow`

Хранение последних N обменов (запрос-ответ) для контекстного понимания.

```python
class ContextWindow:
    def __init__(self, max_size: int = 10)
    def add(self, query: str, response: str) -> None
    def get_context(self) -> list[dict]
    def clear(self) -> None
    def to_prompt(self) -> str  # Форматирование для промпта
```

---

## 3. API-роутеры

### 3.1. ai_chat.py — AI чат (490 строк)

**Префикс:** `/api/v1/ai`

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/chat` | Чат с ИИ |
| POST | `/train` | Обучение на тексте |
| POST | `/train/file` | Обучение на файле |
| POST | `/evolve` | Эволюция формул |
| GET | `/stats` | Статистика движка |
| GET | `/search` | Поиск по знаниям |
| GET | `/patterns` | Список паттернов |
| GET | `/embeddings/{word}` | Эмбеддинг слова |

### 3.2. agent.py — Автономный агент (568 строк)

**Префикс:** `/api/v1/agent`

Автоматический сбор знаний из интернета.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/learn` | Автоматическое обучение по теме |
| POST | `/learn/batch` | Пакетное обучение |
| GET | `/status` | Статус агента |
| POST | `/stop` | Остановка |

Внутри:
```
Тема → generate_search_queries() → [запросы]
  → DuckDuckGo / Bing / Wikipedia
  → requests.get() + BeautifulSoup
  → Чистый текст → train_text()
```

### 3.3. crawler.py — Веб-краулер (435 строк)

**Префикс:** `/api/v1`

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/crawl` | Краулинг URL |
| POST | `/crawl/train` | Краулинг + обучение |
| GET | `/crawl/status` | Статус краулинга |

### 3.4. distributed_crawler.py — Распределённый краулер (614 строк)

**Префикс:** `/api/v1/crawler`

Координация краулинга между несколькими узлами.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/distribute` | Распределить задачу |
| POST | `/worker/register` | Регистрация воркера |
| GET | `/worker/task` | Получить задачу |
| POST | `/worker/result` | Отправить результат |

### 3.5. swarm_sync.py — Рой-синхронизация (307 строк)

**Префикс:** `/api/v1/swarm`

P2P-синхронизация знаний между узлами Kolibri.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/register` | Регистрация пира |
| POST | `/sync` | Синхронизация знаний |
| GET | `/peers` | Список пиров |
| POST | `/broadcast` | Рассылка всем |

### 3.6. delta_sync.py — Дельта-синхронизация (352 строки)

**Префикс:** `/api/v1/delta`

Инкрементальная синхронизация (только изменения).

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/push` | Отправить дельту |
| POST | `/pull` | Получить дельту |
| GET | `/version` | Текущая версия |

### 3.7. gpu_store.py — Векторное хранилище (138 строк)

**Префикс:** `/api/gpu`

SQLite-бэкенд для хранения и поиска по эмбеддингам.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/store` | Сохранить вектор |
| POST | `/search` | Поиск по cosine similarity |
| GET | `/stats` | Статистика |

### 3.8. content_factory.py — Контент-фабрика (445 строк)

**Префикс:** `/api/factory`

Генерация контента на основе трендов.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/generate` | Генерация контента |
| GET | `/trends` | Список трендов |
| POST | `/schedule` | Планирование |

### 3.9. os_bridge.py — ОС-мост (171 строка)

**Префикс:** `/api/dev`

Доступ к ОС-функциям через API.

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/files` | Список файлов |
| POST | `/exec` | Выполнение команды |
| GET | `/sysinfo` | Информация о системе |

### 3.10. archiver_service.py — Сервис архивации (383 строки)

**Префикс:** `/api/v1/archiver`

Сжатие и распаковка данных через API.

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/compress` | Сжать данные |
| POST | `/decompress` | Распаковать |
| GET | `/algorithms` | Доступные алгоритмы |
| POST | `/benchmark` | Бенчмарк сжатия |

### 3.11. benchmarks.py — Бенчмарки (278 строк)

Замер производительности различных компонентов системы.

### 3.12. search_engine.py — Поисковый движок (341 строка)

Расширенный поиск по базе знаний с фасетами и фильтрами.

---

## 4. Когнитивные модули

### 4.1. cognition.py — SwarmCognition (356 строк)

**Класс:** `SwarmCognition`

5 когнитивных способностей высшего порядка:

| Метод | Описание | Вход | Выход |
|-------|----------|------|-------|
| `abstract_reasoning` | Абстрактное мышление | items: list[str] | {structure, patterns, abstraction} |
| `causal_reasoning` | Каузальный вывод | cause: str, context: list | {effect, chain, confidence} |
| `inductive_reasoning` | Индукция | examples: list[str] | {rule, evidence, strength} |
| `structural_transfer` | Перенос структуры | source_domain, target | {mapping, analogy} |
| `self_modeling` | Самомоделирование | — | {capabilities, weaknesses, state} |

### 4.2. cognition_api.py — API когниции (336 строк)

**Префикс:** `/api/v1/cognition`

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/abstract` | Абстрактное мышление |
| POST | `/causal` | Каузальный вывод |
| POST | `/inductive` | Индуктивное мышление |
| POST | `/transfer` | Структурный перенос |
| GET | `/self-model` | Самомоделирование |
| POST | `/analyze` | Комплексный анализ |
| GET | `/capabilities` | Список способностей |

---

## 5. Безопасность и операции

### 5.1. auth.py — JWT авторизация (243 строки)

**Префикс:** `/api/v1/auth`

| Метод | Путь | Описание |
|-------|------|----------|
| POST | `/register` | Регистрация пользователя |
| POST | `/login` | Вход (→ JWT token) |
| GET | `/status` | Проверка токена |

```python
# JWT конфигурация
SECRET_KEY = os.getenv("KOLIBRI_JWT_SECRET", "kolibri-secret-key")
ALGORITHM = "HS256"
TOKEN_EXPIRE_MINUTES = 60 * 24  # 24 часа
```

### 5.2. persistence.py — SQLite хранение (249 строк)

**Класс:** `PersistenceManager`

SQLite в режиме WAL для персистентного хранения.

#### Таблицы

| Таблица | Описание |
|---------|----------|
| `patterns` | Все паттерны KnowledgeGraph |
| `edges` | Все связи графа |
| `config` | Ключ-значение конфигурации |

#### API

```python
class PersistenceManager:
    def __init__(self, db_path: str = "kolibri.db")
    def save_patterns(self, patterns: dict) -> int
    def load_patterns(self) -> dict
    def save_edges(self, edges: dict) -> int
    def load_edges(self) -> dict
    def save_config(self, key: str, value: str) -> None
    def load_config(self, key: str) -> str | None
    def get_stats(self) -> dict
```

### 5.3. rate_limiter.py — Ограничение запросов (137 строк)

**Класс:** `RateLimitMiddleware` (ASGI middleware)

Token-bucket алгоритм:
- **Лимит**: 100 запросов/минуту на IP
- **Bucket size**: 100 токенов
- **Refill rate**: 100/60 ≈ 1.67 токена/сек
- **Исключения**: `/api/v1/health/live`, `/api/v1/health/ready`
- **Ответ при превышении**: HTTP 429 Too Many Requests

### 5.4. health.py — Зондирование (163 строки)

**Префикс:** `/api/v1/health`

| Метод | Путь | Описание |
|-------|------|----------|
| GET | `/live` | Liveness probe ({"status": "alive"}) |
| GET | `/ready` | Readiness probe (проверяет движок) |
| GET | `/detail` | Детальная информация |

`/detail` возвращает:
```json
{
  "status": "healthy",
  "engine": {
    "loaded": true,
    "patterns_count": 20067,
    "edges_count": 262597
  },
  "persistence": {
    "connected": true,
    "db_file": "kolibri.db"
  },
  "corpus": {
    "files_count": 63,
    "total_size_bytes": 2000000
  },
  "memory": {
    "rss_mb": 500,
    "vms_mb": 800
  }
}
```

---

## 6. Сбор и обработка данных

### 6.1. knowledge_builder.py — Построитель знаний (328 строк)

Автоматическое построение графа знаний из различных источников.

### 6.2. knowledge_collector.py — Коллектор знаний (215 строк)

Автоматический сбор и классификация новых знаний.

### 6.3. training_worker.py — Воркер обучения (405 строк)

Фоновый воркер для асинхронного обучения модели.

### 6.4. tokenizer.py — Токенизатор (146 строк)

Токенизация текста для обработки:
- Разбиение на слова
- Нормализация (lowercase, stemming)
- Удаление стоп-слов
- Поддержка русского и английского

### 6.5. formula_lm.py — Формульная языковая модель (328 строк)

Python-обёртка для формульного языкового моделирования.

### 6.6. c_evolve.py — Эволюция через C (313 строк)

FFI-обёртка для вызова C-функций эволюции через ctypes/subprocess.

---

## 7. Конфигурация и запуск

### 7.1. common.py — Общие настройки (140 строк)

**Класс:** `Settings` (Pydantic BaseSettings)

```python
class Settings(BaseSettings):
    response_mode: str = "hybrid"      # "llm" | "kolibri" | "hybrid"
    upstream_provider: str = "openai"   # Для режима LLM
    upstream_model: str = "gpt-4"
    kolibri_model_path: str = "build/training/auto_genome.dat"
    cors_origins: list[str] = ["*"]
    debug: bool = False
```

### Запуск

```bash
# Разработка
uvicorn backend.service.main:app --reload --port 8000

# Продакшн
uvicorn backend.service.main:app --host 0.0.0.0 --port 8000 --workers 4

# С конфигурацией
KOLIBRI_RESPONSE_MODE=kolibri \
KOLIBRI_JWT_SECRET=my-secret \
uvicorn backend.service.main:app --port 8000
```

### Переменные окружения

| Переменная | По умолчанию | Описание |
|-----------|-------------|----------|
| `KOLIBRI_RESPONSE_MODE` | `hybrid` | Режим ответов |
| `KOLIBRI_JWT_SECRET` | `kolibri-secret-key` | JWT секрет |
| `KOLIBRI_DB_PATH` | `kolibri.db` | Путь к SQLite |
| `KOLIBRI_CORPUS_DIR` | `data/corpus` | Директория корпуса |
| `KOLIBRI_LOG_LEVEL` | `INFO` | Уровень логирования |

---

## Полная карта модулей

| # | Модуль | Строк | Категория |
|---|--------|-------|-----------|
| 1 | `number_mind.py` | 2021 | Ядро — Граф знаний |
| 2 | `ai_engine.py` | 1912 | Ядро — AI движок |
| 3 | `distributed_crawler.py` | 614 | Данные — Распр. краулер |
| 4 | `agent.py` | 568 | Данные — Автономный агент |
| 5 | `ai_chat.py` | 490 | API — Чат |
| 6 | `embeddings.py` | 477 | Ядро — Word2Vec |
| 7 | `content_factory.py` | 445 | API — Контент |
| 8 | `crawler.py` | 435 | Данные — Краулер |
| 9 | `training_worker.py` | 405 | Данные — Обучение |
| 10 | `archiver_service.py` | 383 | API — Архивация |
| 11 | `cognition.py` | 356 | Ядро — Когниция |
| 12 | `delta_sync.py` | 352 | P2P — Дельта-синхр. |
| 13 | `search_engine.py` | 341 | API — Поиск |
| 14 | `cognition_api.py` | 336 | API — Когниция |
| 15 | `formula_lm.py` | 328 | Ядро — Формулы |
| 16 | `knowledge_builder.py` | 328 | Данные — Построитель |
| 17 | `reasoning.py` | 324 | Ядро — CoT |
| 18 | `c_evolve.py` | 313 | FFI — C эволюция |
| 19 | `swarm_sync.py` | 307 | P2P — Рой |
| 20 | `benchmarks.py` | 278 | API — Бенчмарки |
| 21 | `persistence.py` | 249 | Операции — БД |
| 22 | `auth.py` | 243 | Безопасность — JWT |
| 23 | `knowledge_collector.py` | 215 | Данные — Коллектор |
| 24 | `os_bridge.py` | 171 | API — ОС-мост |
| 25 | `health.py` | 163 | Операции — Зонды |
| 26 | `tokenizer.py` | 146 | Ядро — Токенизатор |
| 27 | `common.py` | 140 | Конфиг — Настройки |
| 28 | `gpu_store.py` | 138 | API — Вектор. хран. |
| 29 | `rate_limiter.py` | 137 | Безопасность — Rate limit |
| 30 | `context_window.py` | 130 | Ядро — Контекст |
| 31 | `main.py` | 100 | Точка входа |
| 32 | `__init__.py` | 1 | Пакет |
| | **Итого** | **12 846** | |

---

*Документация Python-бэкенда · Kolibri OS · Copyright (c) 2025 Кочуров Владислав Евгеньевич*

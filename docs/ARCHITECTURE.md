# Архитектура Kolibri OS

## Обзор

Kolibri OS — гибридная платформа из 6 основных слоёв:

```
┌─────────────────────────────────────────────────────────────┐
│  Слой 1: Frontend (React 18 + TypeScript + Vite)            │
│  Порт 3000 · WASM Bridge · Manus UI · 14 компонентов       │
├─────────────────────────────────────────────────────────────┤
│  Слой 2: Backend API (FastAPI + Python 3.10+)               │
│  Порт 8001 · 14 модулей · 8 роутеров · CORS enabled        │
├─────────────────────────────────────────────────────────────┤
│  Слой 3: NumberMind Engine (Python)                         │
│  FormulaPool · KnowledgeGraph · SentenceStore · ResNet 500  │
├─────────────────────────────────────────────────────────────┤
│  Слой 4: C-ядро (libkolibri_core · C23)                     │
│  35 модулей · 27 заголовков · CMake + Ninja                  │
├─────────────────────────────────────────────────────────────┤
│  Слой 5: CLI-приложения (C23)                               │
│  17+ бинарников · kolibri_node · archiver · coordinator     │
├─────────────────────────────────────────────────────────────┤
│  Слой 6: Kernel (C + ASM x86)                               │
│  Микроядро ОС · AI-компоненты · Serial I/O · RAM-диск      │
└─────────────────────────────────────────────────────────────┘
```

---

## Слой 1: Frontend

**Стек:** React 18 + TypeScript + Vite 5 + TailwindCSS

```
Browser
  ↓
ManusAppUnified (главный контейнер)
  ├── ManusHeader (навигация, тема)
  ├── Tabs: Chat | Crawler | Knowledge | Settings | Tasks | Terminal
  └── ManusInputBar (ввод)
        ↓
  KolibriBridge (core/kolibri-bridge.ts)
    ├── [WASM] → kolibri.wasm → KolibriScript → ответ
    └── [HTTP] → /api/v1/ai/chat → backend → ответ
```

**WASM Bridge** — ключевой модуль (508 строк):
- Загружает `kolibri.wasm` (до 60MB)
- Полная реализация WASI (WebAssembly System Interface)
- Graceful degradation: WASM → LLM-прокси → статика

---

## Слой 2: Backend API

**Стек:** FastAPI 0.110+ / uvicorn / Python 3.10+

### Роутеры

| Префикс | Модуль | Описание |
|---------|--------|----------|
| `/api/v1/ai` | `ai_chat.py` | AI-чат, обучение, паттерны, embeddings |
| `/api/v1/agent` | `agent.py` | Автономное обучение (DDG/Bing/Wiki → C trainer) |
| `/api/v1/swarm` | `swarm_sync.py` | P2P-синхронизация (регистрация, обмен знаниями) |
| `/api/v1` | `crawler.py` | Веб-краулинг и обучение C-модели |
| `/api/gpu` | `gpu_store.py` | Векторное хранилище (SQLite + cosine similarity) |
| `/api/factory` | `content_factory.py` | Контент-фабрика (SQLAlchemy + тренды) |
| `/api/dev` | `os_bridge.py` | ОС-мост (файлы, система, команды) |
| `/api/v1/infer` | `main.py` | LLM-прокси (если `KOLIBRI_RESPONSE_MODE=llm`) |

### Архитектура обработки запроса

```
HTTP Request
  ↓
FastAPI Router
  ↓
AI Engine (singleton) ← ai_engine.py
  ├── NumberMind (FormulaPool + KnowledgeGraph + SentenceStore)
  ├── C Model Retriever (subprocess → kolibri_node)
  └── Conversation Manager
  ↓
HTTP Response (JSON)
```

---

## Слой 3: NumberMind Engine

Ядро «Числового Формульного Мышления» (1335 строк Python).

### Компоненты

```
NumberMind Engine
├── FormulaPool
│   ├── 16 KolibriGene (по 4000 цифр каждый)
│   ├── Эволюция: мутации + кроссовер + отбор
│   ├── _run_layers(): ResNet 500 слоёв (50 блоков × 10)
│   └── predict(input) → float
│
├── KnowledgeGraph
│   ├── patterns: dict[str, list[int]]  (слово → 64 цифры)
│   ├── edges: dict[(str,str), float]   (связи с весами)
│   ├── train_text(text) → паттерны + рёбра
│   └── find_similar(word, limit) → [(word, similarity)]
│
├── SentenceStore
│   ├── sentences: list[str]
│   ├── TF-IDF индекс
│   └── search(query) → [sentences]
│
└── AssociationStore
    ├── Q→A маппинг (до 10000)
    └── Прямой ответ на знакомые вопросы
```

### Числовое кодирование

```python
# Слово → Паттерн (64 цифры)
word_to_pattern("колибри")  # → [3,8,1,5,7,2,9,0,4,6,...] (DJB2 + LCG)

# Текст → Цифры (3 цифры на UTF-8 байт)
text_to_digits("Hi")  # → [0,7,2, 1,0,5] (72='H', 105='i')

# Цифры → Текст
digits_to_text([0,7,2, 1,0,5])  # → "Hi"
```

### ResNet-архитектура

```
input (float)
  ↓ normalize to [-1, +1], save scale
  ↓
for block in 0..49:
    residual = value
    for layer in 0..9:
        i = (block * 10 + layer) * 8
        op    = genome[i] % 12
        slope = ±(0.5 + genome[i+1..3] / 66.0)
        bias  = ±(genome[i+4..6] / 198.0)
        alpha = 0.1 + genome[i+7] * 0.044
        
        value = operation[op](slope, value, bias)
        value = tanh_clip(value)  # tanh(v/3)*3 if |v| > 3
    
    value = alpha * value + (1 - alpha) * residual
  ↓
output = value * scale
```

### Эволюция

```
Популяция: 16 формул (KolibriGene)
  ↓
Оценка: fitness = avg cosine_similarity(predict(q), expected(a))
  ↓
Отбор: top-4 → родители
  ↓
Кроссовер: single-point / two-point / uniform
  ↓
Мутация: point / swap / invert / scramble / shift
  ↓
Новое поколение → повторить
```

---

## Слой 4: C-ядро

**35 модулей** на C23, библиотека `libkolibri_core` (static).

### Категории модулей

| Категория | Модули | Описание |
|-----------|--------|----------|
| **Числа** | `decimal.c`, `digits.c`, `digit_text.c` | Десятичная арифметика, кодирование текста |
| **Генетика** | `genome.c`, `formula.c`, `random.c` | ReasonBlock цепочка, формульный predict, ГСЧ |
| **Знания** | `knowledge.c`, `knowledge_index.c`, `knowledge_queue.c` | Граф знаний, индексирование, очередь |
| **Язык** | `script.c`, `symbol_table.c` | KolibriScript интерпретатор |
| **AI** | `semantic_digits.c`, `phoneme.c`, `context_window.c` | Семантика, фонетика, контекст |
| **Обучение** | `corpus_learning.c`, `corpus_trainer.c` | Обучение на корпусах |
| **Генерация** | `text_generation.c`, `inference.c` | Генерация текста, инференс |
| **Логика** | `formula_logic.c`, `logical_memory.c` | Мета-формулы, логическая память |
| **Компрессия** | `compress.c` | RLE, dictionary, hybrid |
| **Сеть** | `net.c`, `roy.c` | P2P-протокол, рой |
| **Система** | `sim.c`, `async_executor.c`, `trace.c`, `web_crawler.c` | Симулятор, асинхронность, трассировка, краулер |

### Ключевые структуры данных

```c
// Геном — 4000-цифровой код
typedef struct {
    uint8_t digits[4000];
    size_t length;
} KolibriGene;

// ReasonBlock — единица цепочки событий (аналог блока в блокчейне)
typedef struct {
    uint64_t index;
    uint64_t timestamp;
    unsigned char prev_hash[32];   // SHA-256
    unsigned char hmac[32];        // HMAC-SHA256
    char event_type[32];
    char payload[512];
} ReasonBlock;

// P2P сообщение — до 4200 байт payload
#define KOLIBRI_MAX_PAYLOAD 4200U
```

### Зависимости

- **OpenSSL** (libcrypto) — HMAC-SHA256
- **SQLite3** — хранение
- **pthreads** — многопоточность
- **libm** — математика (sin, cos, exp, tanh)

---

## Слой 5: CLI-приложения

17+ бинарников, каждый линкуется с `libkolibri_core`.

### Главный: `kolibri_node`

```
kolibri_node
├── CLI парсер (--genome, --bootstrap, --listen, --peer, --seed)
├── HMAC-аутентификация (OpenSSL)
├── REPL-режим
│   ├── :ask <вопрос>     — запрос к графу знаний
│   ├── :teach <текст>    — обучение
│   ├── :evolve            — эволюция формул
│   ├── :stats             — статистика
│   ├── :sync              — P2P-синхронизация
│   ├── :verify            — верификация генома
│   └── :canvas            — визуализация
├── auto-learn    — автоматическое обучение из файлов
├── auto-evolve   — непрерывная эволюция формул
└── auto-sync     — автоматическая синхронизация с роем
```

---

## Слой 6: Kernel

Микроядро ОС (C + x86 Assembly).

```
kernel/
├── entry.asm           # Точка входа (GRUB multiboot)
├── interrupts.asm      # Обработчики прерываний x86
├── main.c              # Инициализация ядра
├── support.c/h         # Поддержка ОС (VGA, память)
├── serial.c/h          # Serial I/O (COM1)
├── ramdisk.c/h         # RAM-диск
├── formula.c           # Формульный ResNet-движок
├── genome.c            # Геномная логика
├── net.c               # Сетевой слой
├── random.c            # ГСЧ
├── ai_encoder.c        # AI-кодер
├── ai_evolution.c      # AI-эволюция
├── ai_resonance.c      # AI-резонанс
└── link.ld             # Linker script
```

Сборка ISO: `make iso` → `./scripts/build_iso.sh`

---

## Потоки данных

### 1. Обучение

```
Текст → tokenize → для каждого слова:
  ├── word_to_pattern() → 64-цифровой паттерн (DJB2+LCG)
  ├── Добавить в KnowledgeGraph.patterns
  └── Для каждой пары (word_i, word_j) в окне:
      └── Добавить ребро в KnowledgeGraph.edges, увеличить вес
Предложения → SentenceStore (TF-IDF индекс)
Формулы → FormulaPool.evolve() (генетический алгоритм)
```

### 2. Ответ на вопрос

```
Запрос → tokenize → паттерны слов
  ├── KnowledgeGraph: найти связанные слова (обход графа)
  ├── SentenceStore: TF-IDF поиск релевантных предложений
  ├── FormulaPool: predict(input_hash) → confidence
  ├── C-модель: subprocess → kolibri_node :ask (если доступна)
  └── Сборка ответа: ранжирование + объединение
```

### 3. Автономный агент

```
Тема → generate_search_queries() → ["запрос1", "запрос2", ...]
  ├── DDG search → URLs
  ├── Bing search → URLs
  └── Wikipedia search → URLs
URLs → requests.get() + BeautifulSoup → чистый текст
  ├── Сохранить в /data/corpus/
  ├── C: kolibri_mass_trainer --dir /data/corpus/ --model model.klm
  └── Python: engine.train_text(text) → граф + формулы
```

### 4. P2P-синхронизация

```
Узел A                         Узел B
  │                               │
  ├── POST /register ────────────►│ (регистрация)
  │                               │
  ├── POST /sync ─────────────────►│
  │   {patterns, edges, checksum}  │
  │◄──────────────────────────────┤
  │   {merged_patterns, edges}     │
  │                               │
  └── SHA-256 контроль ──────────►│
```

---

## Детерминизм и воспроизводимость

| Аспект | Реализация |
|--------|-----------|
| **ГСЧ** | `KolibriRng` — LCG (линейный конгруэнтный), инициализация через `--seed` |
| **Хеширование** | DJB2 (паттерны), FNV-1a (формулы) — детерминистические |
| **Формулы** | Мутации зависят только от ГСЧ + текущей популяции |
| **Геном** | Блокчейн-цепочка с HMAC-SHA256 → невозможность подделки |
| **WASM** | Должен давать идентичный вывод с Native C |

---

## Расширяемость

1. **Новые операции формул** — добавить в switch в `_run_layers()` (Python) и `formula_predict_numeric()` (C)
2. **Новые типы P2P-сообщений** — расширить `KolibriNetMessageType` + добавить encode/decode
3. **Новые роутеры API** — создать модуль в `backend/service/`, подключить в `main.py`
4. **Новые вкладки UI** — создать компонент в `frontend/src/manus/tabs/`
5. **GPU-ускорение** — `engine/gpu_encoder/` (CUDA/Metal/CPU-stub)

# Kolibri OS — Архитектура системы

> Статус: глубокий внутренний архитектурный справочник.
>
> Официальный публичный архитектурный документ проекта находится здесь:
> [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md)

> **Полная техническая документация уникальной AGI-платформы**
>
> Версия: 2.0 · Июль 2025 · Copyright © Кочуров Владислав Евгеньевич

---

## Оглавление

1. [Обзор системы](#1-обзор-системы)
2. [Философия: Числовое Мышление](#2-философия-числовое-мышление)
3. [C23-ядро (backend/src)](#3-c23-ядро)
   - 3.1 [Transformer (attention.c/h)](#31-transformer-attentionch)
   - 3.2 [Мировая модель (world_model.c/h)](#32-мировая-модель-world_modelch)
   - 3.3 [Эволюционное сжатие (predictive_compress.c/h)](#33-эволюционное-сжатие-predictive_compressch)
   - 3.4 [Фрактальная память (fractal_memory.c/h)](#34-фрактальная-память-fractal_memorych)
   - 3.5 [Логическая память (logical_memory.c/h)](#35-логическая-память-logical_memorych)
   - 3.6 [Конвейер инференса (inference.c/h)](#36-конвейер-инференса-inferencech)
   - 3.7 [Корпус-тренер (corpus_trainer.c/h)](#37-корпус-тренер-corpus_trainerch)
   - 3.8 [Мета-формулы (formula_logic.c/h)](#38-мета-формулы-formula_logicch)
   - 3.9 [Геном-блокчейн (genome.c/h)](#39-геном-блокчейн-genomech)
   - 3.10 [Дополнительные модули](#310-дополнительные-модули)
4. [Python-бэкенд (backend/service)](#4-python-бэкенд)
5. [Frontend + WASM](#5-frontend--wasm)
6. [Сборка, тесты, CI/CD](#6-сборка-тесты-cicd)
7. [Уникальность Kolibri](#7-уникальность-kolibri)

---

## 1. Обзор системы

Kolibri OS — гибридная платформа **C23 / Python / WebAssembly**, реализующая подход
«Числового Мышления» (Number-Thinking) к построению AGI.

### Масштаб проекта

| Компонент | Язык | Строки кода |
|-----------|------|-------------|
| C-ядро (`backend/src/`) | C23 | **28 166** |
| Публичные заголовки (`backend/include/kolibri/`) | C23 | **5 034** |
| Приложения (`apps/`) | C23 | **4 629** |
| Python-бэкенд (`backend/service/`) | Python 3.12 | **12 846** |
| Скрипты (`scripts/`) | Python/Bash | **13 215** |
| Frontend (`frontend/src/`) | TypeScript/React | **9 970** |
| Тесты (`tests/`) | Python | **2 504** |
| **Итого** | | **~76 000+** |

### Архитектурная диаграмма

```
┌─────────────────────────────────────────────────────────────────┐
│                        Frontend (React + Vite)                  │
│  TypeScript · TailwindCSS · kolibri-bridge.ts ←→ kolibri.wasm  │
└────────────────────────────┬────────────────────────────────────┘
                             │ HTTP / WebSocket
┌────────────────────────────▼────────────────────────────────────┐
│                    Python Backend (FastAPI)                      │
│  ai_engine.py · number_mind.py · embeddings.py · cognition.py  │
│  auth.py · persistence.py · rate_limiter.py · health.py        │
└────────────────────────────┬────────────────────────────────────┘
                             │ FFI / ctypes / WASM
┌────────────────────────────▼────────────────────────────────────┐
│                      C23 Core (39 модулей)                      │
│ ┌──────────┐ ┌──────────┐ ┌───────────────┐ ┌──────────────┐  │
│ │Transformer│ │World     │ │Predictive     │ │Fractal       │  │
│ │attention.c│ │Model     │ │Compress       │ │Memory        │  │
│ │~100K params││world_model││predictive_    │ │fractal_      │  │
│ │Pre-LN 2L  │ │.c        │ │compress.c     │ │memory.c      │  │
│ └──────────┘ └──────────┘ └───────────────┘ └──────────────┘  │
│ ┌──────────┐ ┌──────────┐ ┌───────────────┐ ┌──────────────┐  │
│ │Logical   │ │Inference │ │Corpus Trainer │ │Genome        │  │
│ │Memory    │ │Pipeline  │ │corpus_trainer │ │Blockchain    │  │
│ │logical_  │ │inference │ │.c (hash tables│ │genome.c      │  │
│ │memory.c  │ │.c 5 strat│ │128K patterns) │ │HMAC-SHA256   │  │
│ └──────────┘ └──────────┘ └───────────────┘ └──────────────┘  │
│ ┌──────────┐ ┌──────────┐ ┌───────────────┐ ┌──────────────┐  │
│ │Formula   │ │KolibriSc │ │Compression    │ │Net / Web     │  │
│ │Logic     │ │ript      │ │(16 алгоритмов)│ │Crawler       │  │
│ │formula_  │ │script.c  │ │compress.c     │ │net.c /       │  │
│ │logic.c   │ │3240 loc  │ │3434 loc       │ │web_crawler.c │  │
│ └──────────┘ └──────────┘ └───────────────┘ └──────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Философия: Числовое Мышление

Kolibri основан на трёх фундаментальных принципах:

### 2.1. Сжатие ≡ Понимание ≡ Предсказание

Центральная идея: если система может **сжать** данные, она их **понимает**.
Чем лучше сжатие — тем точнее предсказание следующего символа — тем глубже понимание.

Это реализовано через `world_model.c`:
- Наблюдение потока байтов → предсказание P(byte|context)
- Удивление = -log₂(P) — мера «нового знания»
- Высокое удивление → извлечение нового концепта
- Онлайн-обучение через SPSA (Simultaneous Perturbation Stochastic Approximation)

### 2.2. Эволюция вместо обратного распространения

Вместо классического backpropagation используется **эволюционная оптимизация**:
- Популяция формул-нейросетей (MLP) конкурирует за точность предсказания
- Турнирный отбор + адаптивная мутация
- Кроссовер и элитизм сохраняют лучшие решения
- Это позволяет обучаться без дифференцируемых функций потерь

### 2.3. Числовые геномы

Знание кодируется как **64-символьные десятичные строки** (геномы).
Каждый символ UTF-8 → десятичные цифры через `symbol_table.c`.
Геномы обрабатываются, эволюционируют и мутируют как биологический код.

---

## 3. C23-ядро

Все модули написаны на строгом **C23** с ручным управлением памятью (`malloc`/`free`).
Русские комментарии обязательны. Стиль — `snake_case`.

### 3.1. Transformer (attention.c/h)

**Файлы:** `backend/src/attention.c` (654 строки), `backend/include/kolibri/attention.h` (199 строк)

Полноценная реализация **Pre-LN Transformer** на чистом C, без внешних библиотек.

#### Конфигурация

| Параметр | Значение | Описание |
|----------|----------|----------|
| `KAT_VOCAB_SIZE` | 256 | Байт-уровневый словарь |
| `KAT_EMBED_DIM` | 64 | Размерность эмбеддинга |
| `KAT_NUM_HEADS` | 4 | Голов внимания |
| `KAT_HEAD_DIM` | 16 | Размерность одной головы |
| `KAT_FF_DIM` | 256 | Скрытый слой FFN |
| `KAT_NUM_LAYERS` | 2 | Трансформерных блоков |
| `KAT_MAX_SEQ` | 512 | Контекстное окно |
| Всего параметров | ~100 000 | Подсчитано kat_count_params() |

#### Структуры данных

```c
KatModel {
    KatEmbeddingTable embed {
        float token_embed[256][64]     // Лексические эмбеддинги
        float pos_embed[512][64]       // Синусоидальные позиционные
    }
    KatTransformerBlock layers[2] {
        KatMultiHeadAttention attn {
            KatAttentionHead heads[4] {
                float wq[64][16]       // Query-проекция
                float wk[64][16]       // Key-проекция
                float wv[64][16]       // Value-проекция
            }
            float wo[64][64]           // Выходная проекция
            float ln_gamma[64]         // LayerNorm γ
            float ln_beta[64]          // LayerNorm β
        }
        KatFeedForward ffn {
            float w1[64][256]          // Первый линейный слой
            float b1[256]              // Bias 1
            float w2[256][64]          // Второй линейный слой
            float b2[64]              // Bias 2
            float ln_gamma[64]         // LayerNorm γ
            float ln_beta[64]          // LayerNorm β
        }
    }
    float final_ln_gamma[64]           // Финальная нормализация
    float final_ln_beta[64]
    float lm_head[64][256]             // Language Model head
}
```

#### Алгоритмы

1. **Инициализация Xavier**: `kat_model_create()` — генерация весов N(0, sqrt(2/fan_in))
2. **Синусоидальные PE**: pos_embed[pos][2i] = sin(pos / 10000^(2i/d)), cos для нечётных
3. **Multi-Head Attention**:
   - Q = X·Wq, K = X·Wk, V = X·Wv (матричные умножения)
   - Attention(Q,K,V) = softmax(QK^T / sqrt(d_k)) · V
   - Каузальная маска: score[i][j] = -1e9 при j > i
   - Конкатенация голов → проекция Wo
4. **Pre-LN**: LayerNorm **до** attention/FFN, не после (стабильнее для обучения)
5. **FFN**: GELU(x·W1+b1)·W2+b2
6. **LM Head**: hidden → logits[256] → softmax → P(token)
7. **Семплирование**: temperature scaling + вероятностный отбор
8. **Обучение**: SPSA — случайное возмущение 16 весов LM head + 4 эмбеддинга

#### API

```c
KatModel*     kat_model_create(uint64_t seed);
void          kat_model_destroy(KatModel *model);
KatWorkspace* kat_workspace_create(void);
void          kat_workspace_destroy(KatWorkspace *ws);

int           kat_forward(const KatModel *model, KatWorkspace *ws,
                          const uint8_t *tokens, size_t seq_len);
void          kat_extract_embedding(const KatWorkspace *ws, float *out);
uint8_t       kat_sample(KatModel *model, const KatWorkspace *ws, float temperature);
float         kat_train_step(KatModel *model, KatWorkspace *ws,
                             const uint8_t *tokens, size_t seq_len,
                             uint8_t target, float lr);
float         kat_cosine_similarity(const float *a, const float *b, size_t dim);
size_t        kat_count_params(void);
size_t        kat_serialize(const KatModel *model, uint8_t *buf, size_t buf_size);
int           kat_deserialize(KatModel *model, const uint8_t *buf, size_t buf_size);
```

---

### 3.2. Мировая модель (world_model.c/h)

**Файлы:** `backend/src/world_model.c` (547 строк), `backend/include/kolibri/world_model.h` (242 строки)

Предсказательная модель мира, реализующая принцип «сжатие = понимание».

#### Конфигурация

| Параметр | Значение |
|----------|----------|
| `KWM_CONTEXT_SIZE` | 256 байт |
| `KWM_HISTORY_SIZE` | 4096 байт |
| `KWM_MAX_CONCEPTS` | 128 концептов |
| `KWM_CONCEPT_DIM` | 64 (= KAT_EMBED_DIM) |

#### Ключевые механизмы

1. **Наблюдение (kwm_observe)**:
   - Получает байт → forward pass через Transformer
   - Вычисляет surprise = -log₂(P(byte)) — «удивление»
   - Онлайн-обучение каждые 4 байта через `kat_train_step()`
   - Обновляет скользящее окно контекста

2. **Извлечение концептов (kwm_extract_concepts)**:
   - Наблюдения с surprise > threshold → кандидаты в концепты
   - Извлечение эмбеддинга через mean-pooling: embed = mean(hidden[0..seq_len])
   - Кластеризация по косинусному сходству
   - Каждый концепт: {embedding[64], label, salience, surprise, frequency}

3. **Предсказание (kwm_predict)**:
   - Forward pass на текущем контексте
   - Выдаёт: probs[256], predicted_token, confidence, surprise

4. **Генерация текста (kwm_generate)**:
   - Авторегрессивный: predict → sample → append → predict → ...
   - С температурным управлением

5. **Семантические эмбеддинги (kwm_embed_text)**:
   - Текст → токены → forward pass → mean-pooling → вектор[64]
   - `kwm_similarity()` — косинусное сходство между текстами

#### Структура KwmContext

```c
KwmContext {
    KatModel     *backbone      // Transformer-бэкбон
    KatWorkspace *workspace     // Буферы forward pass
    uint8_t      context[256]   // Скользящее контекстное окно
    uint8_t      history[4096]  // Кольцевой буфер полной истории
    KwmConcept   concepts[128]  // Извлечённые концепты
    KwmStats     stats          // Статистика (loss, perplexity, tokens, ...)
    float        learning_rate  // По умолчанию 0.001
    float        surprise_threshold  // Порог для создания концептов
}
```

#### API

```c
KwmContext*    kwm_create(uint64_t seed);
void          kwm_destroy(KwmContext *ctx);
float         kwm_observe(KwmContext *ctx, uint8_t byte);
void          kwm_observe_block(KwmContext *ctx, const uint8_t *data, size_t len);
int           kwm_predict(KwmContext *ctx, KwmPrediction *pred);
int           kwm_generate(KwmContext *ctx, uint8_t *output, size_t max_len,
                           size_t *actual_len, float temperature);
int           kwm_embed_text(KwmContext *ctx, const char *text, size_t len,
                             float *embedding);
float         kwm_similarity(KwmContext *ctx, const char *a, size_t a_len,
                             const char *b, size_t b_len);
int           kwm_extract_concepts(KwmContext *ctx);
int           kwm_learn_step(KwmContext *ctx, const uint8_t *data, size_t len);
size_t        kwm_serialize(const KwmContext *ctx, uint8_t *buf, size_t buf_size);
int           kwm_deserialize(KwmContext *ctx, const uint8_t *buf, size_t buf_size);
```

---

### 3.3. Эволюционное сжатие (predictive_compress.c/h)

**Файлы:** `backend/src/predictive_compress.c` (620 строк), `backend/include/kolibri/predictive_compress.h`

Нейросетевой предсказатель + арифметическое кодирование с эволюцией весов.

#### Архитектура формулы-предсказателя (MLP)

```
Контекст[8 байт] → W1[8×64] + b1 → GELU → W2[64×256] + b2 → softmax → P[256]
```

| Параметр | Значение |
|----------|----------|
| `KPC_CONTEXT_SIZE` | 8 байт |
| `KPC_HIDDEN_SIZE` | 64 нейрона |
| `KPC_VOCAB_SIZE` | 256 байт |
| `KPC_POPULATION` | 16 формул |
| `KPC_EVOLVE_ROUNDS` | 10 раундов |
| Параметров на формулу | 8×64 + 64 + 64×256 + 256 = **17 216** |

#### Эволюционный цикл

```
1. Инициализация: 16 случайных формул (MLP)
2. Оценка: каждая формула предсказывает последов., fitness = -Σ log(P(correct))
3. Турнирный отбор: случайные пары → побеждает лучший fitness
4. Клонирование + мутация: дочерние формулы = лучшие + шум N(0, σ²)
5. Адаптивная мутация: σ уменьшается при стагнации
6. Повторять KPC_EVOLVE_ROUNDS раз
7. Лучшая формула → предсказатель для арифметического кодирования
```

#### Арифметическое кодирование

- 16-битная точность: low=0x0000, high=0xFFFF
- Кодирование: сужение интервала [low, high) пропорционально P(symbol)
- Декодирование: обратная процедура с тем же предсказателем
- CRC32 для проверки целостности
- Формат: `KPCHeader` (magic "KPC\0", version, original_size, compressed_size, checksum)
  + сериализованная формула + сжатые данные

#### API

```c
KPCContext*  kpc_create(void);
void         kpc_destroy(KPCContext *ctx);
void         kpc_train(KPCContext *ctx, const uint8_t *data, size_t size, int rounds);
int          kpc_compress(KPCContext *ctx, const uint8_t *input, size_t input_size,
                          uint8_t **output, size_t *output_size);
int          kpc_decompress(const uint8_t *input, size_t input_size,
                            uint8_t **output, size_t *output_size);
double       kpc_get_ratio(const KPCContext *ctx);
```

---

### 3.4. Фрактальная память (fractal_memory.c/h)

**Файлы:** `backend/src/fractal_memory.c` (753 строки), `backend/include/kolibri/fractal_memory.h` (155 строк)

10-ричное фрактальное дерево для ассоциативной памяти.

#### Структура

```
         [корень]
        / | | ... \
       0  1  2 ... 9       ← 10 потомков (цифры 0-9)
      /|\   /|\
     0..9  0..9             ← каждый раскрывается ещё в 10
     ...                    ← глубина до 64 уровней
```

- Каждый **узел** может хранить payload (до 512 байт) и до 16 ассоциаций
- **Путь** = десятичная последовательность = «мысль»
- Глубже = точнее (фрактальная детализация)

#### Типы узлов

| Тип | Описание |
|-----|----------|
| `KFM_NODE_EMPTY` | Транзитный (без данных) |
| `KFM_NODE_CONCEPT` | Понятие с payload |
| `KFM_NODE_LINK` | Ссылка на другой путь |
| `KFM_NODE_PATTERN` | Сжатое представление |

#### Ассоциативные связи

```c
KfmAssociation {
    uint8_t  target_path[64]   // Путь к связанному понятию
    uint8_t  target_len        // Длина пути
    float    strength          // Сила связи (0.0–1.0)
    uint32_t access_count      // Счётчик обращений
}
```

#### Волна активации (Spreading Activation)

```
1. kfm_activate(path, energy=1.0)
2. Узел получает энергию → повышает activation
3. Энергия распространяется по ассоциациям с затуханием:
   energy' = energy × strength × decay_factor
4. Волна идёт рекурсивно по всем связям
5. Затухание со временем: kfm_decay() ослабляет неиспользуемые узлы
```

#### Мутации

4 типа мутаций пути с сохранением семантической близости:
- **Point**: замена одной цифры
- **Insert**: вставка цифры (углубление)
- **Delete**: удаление цифры (обобщение)
- **Swap**: обмен двух цифр

#### Кодирование текста

```
char → uint8_t(ASCII) → 3 десятичных цифры (value/100, value/10%10, value%10)
"A" = 65 → [0, 6, 5]
"Hi" = [0,7,2, 1,0,5]
```

#### Сериализация

Формат с магическим числом `KFM1` (0x4B464D31). Рекурсивный обход дерева,
сохранение/загрузка всех узлов с payload и ассоциациями.

#### API

```c
int           kfm_init(KfmContext *ctx, uint32_t seed);
void          kfm_free(KfmContext *ctx);
int           kfm_insert(KfmContext *ctx, const uint8_t *path, size_t path_len,
                          const void *payload, size_t payload_size);
const KfmNode* kfm_lookup(KfmContext *ctx, const uint8_t *path, size_t path_len);
int           kfm_search(KfmContext *ctx, const uint8_t *query, size_t query_len,
                          KfmSearchResult *results, size_t max_results);
int           kfm_associate(KfmContext *ctx,
                            const uint8_t *path_a, size_t len_a,
                            const uint8_t *path_b, size_t len_b, float strength);
int           kfm_activate(KfmContext *ctx, const uint8_t *path, size_t path_len,
                            float energy);
void          kfm_decay(KfmContext *ctx);
int           kfm_mutate(KfmContext *ctx, const uint8_t *path, size_t path_len,
                          uint8_t *new_path, size_t *new_len);
size_t        kfm_text_to_path(const char *text, size_t text_len,
                               uint8_t *path, size_t max_path);
size_t        kfm_serialize(KfmContext *ctx, uint8_t *buf, size_t buf_size);
int           kfm_deserialize(KfmContext *ctx, const uint8_t *buf, size_t buf_size);
```

---

### 3.5. Логическая память (logical_memory.c/h)

**Файлы:** `backend/src/logical_memory.c` (802 строки), `backend/include/kolibri/logical_memory.h` (204 строки)

Память без данных — хранится только **логика генерации данных**.
Данные материализуются при запросе (lazy evaluation).

#### 8 типов логических выражений

| Тип | Описание | Пример |
|-----|----------|--------|
| `LOGIC_CONSTANT` | Фиксированное значение | `"ABC"` |
| `LOGIC_VARIABLE` | Переменная с привязкой | `x → binding` |
| `LOGIC_REPEAT` | Повторение шаблона | `repeat("AB", 1000)` → 2 байта вместо 2000 |
| `LOGIC_SEQUENCE` | Арифметическая последов. | `seq(1, 2, 100)` → 1,3,5,...,199 |
| `LOGIC_TRANSFORM` | Преобразование входа | `transform(input, fn)` |
| `LOGIC_CONDITIONAL` | Условное ветвление | `if(cond, then, else)` |
| `LOGIC_COMPOSITION` | Композиция выражений | `compose(expr1, expr2, ...)` |
| `LOGIC_RELATION` | Отношение между сущностями | `relates(A, B, "part_of")` |

#### Пример сжатия

```
Данные:    "ABCABCABCABC..." (1000 повторений "ABC")
Хранение:  repeat("ABC", 1000)
Логика:    ~40 байт
Данные:    3000 байт
Коэфф.:   75×
```

#### Материализация с кэшированием

```c
LogicCell {
    char id[64]                   // Уникальный ID ячейки
    LogicExpression *logic        // Логика генерации
    void *cached_data             // Кэш (заполняется при первом запросе)
    size_t cached_size
    int cache_valid               // Флаг валидности кэша
    char dependencies[16][64]     // Зависимости от других ячеек
}
```

#### Оптимизация (lm_optimize_logic)

- **Свёртка вложенных repeat**: `repeat(repeat("A", 3), 5)` → `repeat("A", 15)`
- **Слияние констант**: `compose(const("AB"), const("CD"))` → `const("ABCD")`
- Рекурсивная оптимизация поддеревьев

#### API

```c
LogicalMemory*   lm_create_memory(void);
void             lm_destroy_memory(LogicalMemory *mem);

LogicExpression* lm_logic_constant(const char *value);
LogicExpression* lm_logic_repeat(const char *pattern, size_t count);
LogicExpression* lm_logic_sequence(int start, int step, size_t count);
LogicExpression* lm_logic_compose(LogicExpression *expr1, LogicExpression *expr2);
LogicExpression* lm_logic_relation(LogicExpression *left, LogicExpression *right,
                                    const char *type);
LogicExpression* lm_logic_variable(const char *name);
LogicExpression* lm_logic_transform(LogicExpression *input,
                                    int (*fn)(const void*, void*));
LogicExpression* lm_logic_conditional(LogicExpression *cond,
                                      LogicExpression *then_e,
                                      LogicExpression *else_e);

int              lm_store_logic(LogicalMemory *mem, const char *id,
                                LogicExpression *logic);
int              lm_materialize(LogicalMemory *mem, const char *id,
                                void *output, size_t max_size, size_t *actual_size);
int              lm_optimize_logic(LogicExpression *expr);
```

---

### 3.6. Конвейер инференса (inference.c/h)

**Файлы:** `backend/src/inference.c` (774 строки), `backend/include/kolibri/inference.h` (155 строк)

Центральный pipeline: от запроса пользователя до ответа.

#### 5 стратегий вывода

```
Запрос → ┬─ Step 1: Direct Search ─────── Прямой поиск в knowledge base
         ├─ Step 2: Formula Inference ──── Вывод через числовые формулы
         ├─ Step 3: Logical Reasoning ──── Meta-формулы и логическая память
         ├─ Step 4: Fractal Memory ─────── Ассоциативный поиск + активация
         └─ Step 5: Semantic Understanding ── Transformer + World Model
              │
              ▼
         HYBRID: взвешенное голосование по confidence
              │
              ▼
         Ответ + chain-of-thought + метрики
```

#### Стратегии

| Стратегия | Enum | Описание |
|-----------|------|----------|
| `KOLIBRI_INF_DIRECT` | 0 | Поиск по knowledge base (BM25/hash) |
| `KOLIBRI_INF_FORMULA` | 1 | Числовой вывод через formula pool |
| `KOLIBRI_INF_LOGICAL` | 2 | Логическое рассуждение через meta-формулы |
| `KOLIBRI_INF_CHAIN` | 3 | Chain-of-thought (многошаговый) |
| `KOLIBRI_INF_HYBRID` | 4 | Все методы → взвешенное голосование |

#### Результат инференса

```c
KolibriInferenceResult {
    char response[8192]                    // Сгенерированный ответ
    KolibriInferenceStep steps[64] {       // Chain-of-thought
        char description[256]
        char result[512]
        double confidence
        double duration_ms
    }
    double total_confidence                // Общая уверенность
    size_t knowledge_hits                  // Найденных документов
    size_t formulas_applied                // Применённых формул
    size_t logic_rules_fired               // Сработавших правил
    char sources[16][256]                  // Источники
}
```

#### Алгоритм HYBRID

```
1. Запускаем все 5 стратегий
2. Каждая возвращает (ответ, confidence ∈ [0,1])
3. Финальный ответ = стратегия с максимальным confidence
4. Альтернатива: взвешенная комбинация ответов
5. Chain-of-thought лог записывает все шаги
```

#### API

```c
KolibriInferenceContext* kolibri_inference_create(void);
void                    kolibri_inference_destroy(KolibriInferenceContext *ctx);
int                     kolibri_inference_set_strategy(
                            KolibriInferenceContext *ctx,
                            KolibriInferenceStrategy strategy);
int                     kolibri_inference_set_temperature(
                            KolibriInferenceContext *ctx, double temperature);
int                     kolibri_inference_run(
                            KolibriInferenceContext *ctx,
                            const char *query,
                            KolibriInferenceResult *result);
int                     kolibri_inference_step(
                            KolibriInferenceContext *ctx,
                            const char *query,
                            KolibriInferenceStep *step);
```

---

### 3.7. Корпус-тренер (corpus_trainer.c/h)

**Файлы:** `backend/src/corpus_trainer.c` (1679 строк), `backend/include/kolibri/corpus_trainer.h`

Масштабное обучение на корпусах текста с хеш-таблицами для O(1)-поиска.

#### Параметры

| Параметр | Значение |
|----------|----------|
| Макс. паттернов | 131 072 (128K) |
| Макс. связей (edges) | 262 144 (256K) |
| Бакетов хеш-таблицы паттернов | 65 521 |
| Бакетов хеш-таблицы связей | 131 071 |
| Поколений эволюции | Настраиваемо |

#### Алгоритм

1. **Индексация корпуса**: каждый файл → токенизация → извлечение n-грамм
2. **Хеш-таблица паттернов**: pattern_text → {hash, frequency, confidence, embedding}
3. **Хеш-таблица связей**: (pattern_a, pattern_b) → {strength, frequency, type}
4. **Adjacency index**: для каждого паттерна — список смежных связей (O(1) обход)
5. **Эволюция**: мульти-поколенционная оптимизация весов связей
6. **Дистилляция**: удаление слабых паттернов/связей, сжатие модели

---

### 3.8. Мета-формулы (formula_logic.c/h)

**Файлы:** `backend/src/formula_logic.c` (1154 строки), `backend/include/kolibri/formula_logic.h`

Формулы, которые генерируют/эволюционируют/сжимают другие формулы.

#### Типы мета-формул

- **Генераторы констант**: порождают числовые последовательности
- **Эволюторы паттернов**: мутируют и скрещивают формулы
- **Компрессоры логики**: сжимают сложные выражения
- **Авто-открытие**: поиск закономерностей в данных и создание формул

#### Формульный пул (KolibriFormulaPool)

```c
KolibriFormulaPool {
    KolibriFormula formulas[1000]  // Максимум формул в пуле
    size_t count
    KolibriEvolutionConfig evolution_config {
        double mutation_rate        // [0.0–1.0]
        double crossover_rate
        double elite_ratio          // [0.1–0.5]
        uint64_t generations_per_tick
        int adaptive_mutation
    }
    KolibriEvolutionMetrics metrics {
        uint64_t total_generations
        uint64_t beneficial_mutations
        double best_fitness
    }
}
```

#### Эволюционный реактор

5 типов мутаций: Point, Swap, Invert, Scramble, Shift
3 типа кроссовера: Single-point, Two-point, Uniform

---

### 3.9. Геном-блокчейн (genome.c/h)

**Файлы:** `backend/src/genome.c` (887 строк), `backend/include/kolibri/genome.h` (126 строк)

Неизменяемый аудит-трейл всех операций ИИ в виде блокчейна.

#### Структура блока

```c
ReasonBlock {
    uint64_t      index          // Порядковый номер
    uint64_t      timestamp      // Unix-время
    unsigned char prev_hash[32]  // SHA-256 хеш предыдущего блока
    unsigned char hmac[32]       // HMAC-SHA256 подпись (OpenSSL)
    char          event_type[32] // Тип события ("learn", "query", "evolve", ...)
    char          payload[512]   // Полезная нагрузка (UTF-8)
}
```

#### Механизм

1. **Цепочка**: каждый блок содержит хеш предыдущего → цепочка целостности
2. **HMAC-SHA256**: подпись через `HMAC()` из OpenSSL с секретным ключом
3. **WAL (Write-Ahead Logging)**:
   - Магическое число `0x4B4F4C57414C0001` ("KOLWAL")
   - Данные сначала записываются в WAL-файл
   - Затем переносятся в основной при checkpoint
   - Восстановление после сбоя через `kg_wal_recover()`
4. **Итерация**: `kg_iterate_blocks()` с callback-функцией
5. **Верификация**: `kg_verify_file()` — проверка всей цепочки целостности

#### API

```c
int   kg_open(KolibriGenome *ctx, const char *path,
              const unsigned char *key, size_t key_len);
void  kg_close(KolibriGenome *ctx);
int   kg_append(KolibriGenome *ctx, const char *event_type, const char *payload,
                ReasonBlock *out_block);
int   kg_verify_file(const char *path, const unsigned char *key, size_t key_len);
int   kg_wal_enable(KolibriGenome *ctx);
int   kg_wal_recover(KolibriGenome *ctx);
int   kg_wal_checkpoint(KolibriGenome *ctx);
int   kg_stream_append(KolibriGenome *ctx, const char *event_type,
                       const char *payload, ReasonBlock *out_block);
int   kg_get_stats(KolibriGenome *ctx, KolibriGenomeStats *stats);
int   kg_read_block(KolibriGenome *ctx, uint64_t index, ReasonBlock *out_block);
int   kg_iterate_blocks(KolibriGenome *ctx, kg_block_callback cb, void *user_data);
```

---

### 3.10. Дополнительные модули

| Модуль | Файл | Строк | Описание |
|--------|------|-------|----------|
| **compress.c** | `backend/src/compress.c` | 3434 | 16 алгоритмов сжатия (RLE, Huffman, LZ77, ANS, Delta, Fibonacci, BWT и др.) |
| **script.c** | `backend/src/script.c` | 3240 | Интерпретатор KolibriScript (.ks) — DSL для команд ИИ |
| **formula.c** | `backend/src/formula.c` | 1339 | Числовые формулы: кодирование, декодирование, эволюция |
| **knowledge.c** | `backend/src/knowledge.c` | — | Knowledge base: хранение и поиск фактов |
| **knowledge_index.c** | `backend/src/knowledge_index.c` | — | Индексирование базы знаний |
| **knowledge_queue.c** | `backend/src/knowledge_queue.c` | — | Очередь обработки знаний |
| **text_generation.c** | `backend/src/text_generation.c` | — | Генерация текстовых ответов |
| **huffman_ans.c** | `backend/src/huffman_ans.c` | — | Huffman + ANS кодирование |
| **net.c** | `backend/src/net.c` | — | Сетевой ввод/вывод (HTTP-клиент на сокетах) |
| **web_crawler.c** | `backend/src/web_crawler.c` | — | Веб-краулер для сбора данных |
| **phoneme.c** | `backend/src/phoneme.c` | — | Фонемный анализ |
| **sim.c** | `backend/src/sim.c` | — | Симулятор (автономные агенты) |
| **async_executor.c** | `backend/src/async_executor.c` | — | Асинхронный исполнитель задач |
| **wasm_bridge.c** | `backend/src/wasm_bridge.c` | — | Мост C ↔ WebAssembly |
| **sha256.c** | `backend/src/sha256.c` | — | Собственная реализация SHA-256 |

#### Публичные заголовки (33 файла)

Все заголовки в `backend/include/kolibri/`:

```
async_executor.h   attention.h      auto_learn.h     compress.h
context.h          corpus.h         corpus_trainer.h decimal.h
digit_text.h       digits.h         formula.h        formula_logic.h
fractal_memory.h   generation.h     genome.h         huffman_ans.h
inference.h        knowledge.h      knowledge_index.h knowledge_queue.h
logical_memory.h   net.h            phoneme.h        predictive_compress.h
random.h           roy.h            script.h         semantic.h
sim.h              symbol_table.h   trace.h          web_crawler.h
world_model.h
```

---

## 4. Python-бэкенд

**Директория:** `backend/service/` · **12 846 строк** · Python 3.12+ · FastAPI

### 4.1. Точка входа (main.py)

```python
# Middleware стек:
1. CORSMiddleware          # Кросс-доменные запросы
2. RateLimitMiddleware     # Token-bucket по IP

# 15+ роутеров:
/api/v1/auth/...           # JWT аутентификация
/api/v1/health/...         # Зондирование (/live, /ready, /detail)
/api/v1/cognition/...      # 7 когнитивных операций
/api/v1/chat/...           # Чат с ИИ
/api/v1/knowledge/...      # Управление знаниями
/api/v1/search/...         # Поисковый движок
/api/v1/formula/...        # Операции с формулами
/api/v1/benchmark/...      # Бенчмарки
# ... и другие

# При старте:
@app.on_event("startup")
async def startup():
    engine.load_corpus()         # Загрузка корпуса
    engine.restore_state()       # SQLite → память
```

### 4.2. AI Engine (ai_engine.py, ~1868 строк)

Центральный класс `KolibriAIEngine`:

- **KnowledgeGraph** из `number_mind.py` — хранение паттернов и связей
- **Word2Vec** из `embeddings.py` — Skip-gram 64-dim эмбеддинги
- **ChainOfThought** из `reasoning.py` — цепочка рассуждений
- **ContextWindow** из `context_window.py` — окно диалогового контекста
- **SwarmCognition** из `cognition.py` — 5 когнитивных способностей
- **Persistence** из `persistence.py` — SQLite WAL-mode

Поток обработки чата:
```
1. Запрос → поиск в KnowledgeGraph (BM25 + embedding cosine boost)
2. Обогащение: Chain-of-Thought reasoning
3. Когнитивное обогащение: абстрактное, каузальное мышление
4. Генерация ответа из найденных сниппетов
5. Сохранение в контексте диалога
6. Запись в persistence (SQLite)
```

### 4.3. Граф знаний (number_mind.py, ~2020 строк)

`KnowledgeGraph`:
- Хранение: паттерны (KnowledgePattern) + связи (KnowledgeEdge)
- Поиск: BM25 ранжирование + косинусный boost эмбеддингов
- N-граммы: быстрый fuzzy-поиск
- Визуализация: export в DOT/Graphviz

### 4.4. Эмбеддинги (embeddings.py, 478 строк)

`Word2Vec`:
- Skip-gram архитектура
- 64-мерные вектора
- Negative sampling
- Обучение на корпусе знаний
- Экспорт/импорт весов

### 4.5. Когнитивные способности (cognition.py, 357 строк)

`SwarmCognition` — 5 методов:
1. **abstract_reasoning** — абстрактное мышление (выделение структуры)
2. **causal_reasoning** — каузальный вывод (причина → следствие)
3. **inductive_reasoning** — индуктивное мышление (частное → общее)
4. **structural_transfer** — перенос структуры между доменами
5. **self_modeling** — самомоделирование (рефлексия)

### 4.6. Безопасность и операции

| Модуль | Строк | Описание |
|--------|-------|----------|
| `auth.py` | ~230 | JWT авторизация (login, register, status) |
| `persistence.py` | ~230 | SQLite WAL-mode (patterns, edges, config) |
| `rate_limiter.py` | ~135 | Token-bucket per-IP, exempt health paths |
| `health.py` | ~140 | /live, /ready, /detail (engine stats, memory, corpus) |
| `context_window.py` | 131 | Память контекста диалога |
| `reasoning.py` | ~324 | CoT + BM25 стратегия поиска |
| `delta_sync.py` | — | Синхронизация дельт между узлами |
| `swarm_sync.py` | — | Рой-синхронизация |

### 4.7. Полный список Python-модулей

```
__init__.py               agent.py               ai_chat.py
ai_engine.py              archiver_service.py    auth.py
benchmarks.py             c_evolve.py            cognition.py
cognition_api.py          common.py              content_factory.py
context_window.py         crawler.py             delta_sync.py
distributed_crawler.py    embeddings.py          formula_lm.py
gpu_store.py              health.py              knowledge_builder.py
knowledge_collector.py    main.py                number_mind.py
os_bridge.py              persistence.py         rate_limiter.py
reasoning.py              search_engine.py       swarm_sync.py
tokenizer.py              training_worker.py
```

---

## 5. Frontend + WASM

### 5.1. Frontend (9970 строк)

**Стек**: React + TypeScript + Vite + TailwindCSS

Директория: `frontend/src/`

Ключевые компоненты:
- `core/kolibri-bridge.ts` — мост JS ↔ WASM (загрузка `kolibri.wasm`,
  вызов C-функций)
- Интерфейс чата с ИИ
- Визуализация графа знаний
- Панель мониторинга (health, stats)

### 5.2. WebAssembly

- **Сборка**: `./scripts/build_wasm.sh` (Emscripten)
- **Целевой файл**: `build/wasm/kolibri.wasm`
- **Ограничение**: < 60 MB бинарный размер
- **Детерминизм**: WASM-выход должен совпадать с нативным C

Экспортируемые функции через `wasm_bridge.c`:
- Все функции ядра доступны из JavaScript через `kolibri-bridge.ts`

---

## 6. Сборка, тесты, CI/CD

### Сборка

```bash
# Нативная (C23)
cmake -S . -B build -G Ninja && cmake --build build

# WASM
./scripts/build_wasm.sh

# Frontend (WASM + React)
make frontend

# Архивер (standalone)
cd kolibri-archiver && make
```

### Тесты

```bash
# Полный набор
make test

# C-тесты
ctest --test-dir build --output-on-failure

# Python-тесты
pytest tests/ -v

# Линтеры
ruff check . && pyright

# Бенчмарки
make benchmark
```

### Текущее покрытие

| Набор | Тестов | Статус |
|-------|--------|--------|
| Unit tests (Python) | 65 | Все проходят |
| E2E API tests | 17 | Все проходят |
| Swarm proof experiments | 11 | Все проходят |

### CI/CD (GitHub Actions)

`.github/workflows/ci.yml`:
- Сборка C-ядра (CMake + Ninja)
- Запуск C-тестов (CTest)
- Python lint (ruff + pyright)
- Python unit tests
- Verification suite

---

## 7. Уникальность Kolibri

### Что делает Kolibri уникальным

| Свойство | Kolibri | Типичный ИИ |
|----------|---------|-------------|
| **Язык ядра** | Чистый C23, минимум зависимостей | Python + PyTorch/TensorFlow |
| **Обучение** | Эволюционное (без backprop) + SPSA | Backpropagation |
| **Память** | Фрактальная (10-ричное дерево) + Логическая (без данных!) | Обычные тензоры |
| **Блокчейн** | HMAC-SHA256 геном для аудита | Нет аудита |
| **Сжатие** | Нейросетевое (MLP + арифм. кодирование + эволюция) | Отдельный от ИИ |
| **Предсказание = понимание** | World Model с онлайн-обучением | Офлайн pre-training |
| **DSL** | KolibriScript (.ks) — собственный язык | JSON/YAML конфиги |
| **Integrated** | Один бинарник: inference + training + compression | Разные системы |

### Комбинация технологий

Kolibri объединяет 7 фундаментальных подсистем в единую платформу:

1. **Pre-LN Transformer** — на чистом C, с реальными матричными операциями Q/K/V
2. **World Model** — предсказательная модель мира на Transformer-backbone
3. **Evolutionary Neural Compression** — MLP-предсказатель + арифметическое кодирование
4. **Fractal Associative Memory** — 10-ричное дерево с волновой активацией
5. **Logic-Centric Memory** — хранение формул вместо данных (lazy materialization)
6. **5-Strategy Inference Pipeline** — гибридный вывод с confidence-weighted голосованием
7. **HMAC-SHA256 Genome Blockchain** — криптографический аудит всех решений ИИ

Ни одна существующая система не объединяет все эти подходы в одном ядре.

---

*Документация создана на основе глубокого анализа исходного кода (76 000+ строк)*
*Copyright (c) 2025 Кочуров Владислав Евгеньевич*

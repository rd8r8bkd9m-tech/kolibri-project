# Kolibri OS — C-ядро: Справочник модулей и API

> Детальная документация всех модулей C23-ядра (`backend/src/` + `backend/include/kolibri/`)
>
> 39 модулей · 33 заголовка · 28 166 строк кода

---

## Оглавление

1. [Нейросетевые модули](#1-нейросетевые-модули)
2. [Память и хранение](#2-память-и-хранение)
3. [Сжатие](#3-сжатие)
4. [Знания и обучение](#4-знания-и-обучение)
5. [Формулы и эволюция](#5-формулы-и-эволюция)
6. [Инференс и генерация](#6-инференс-и-генерация)
7. [Числовое кодирование](#7-числовое-кодирование)
8. [Язык и скрипты](#8-язык-и-скрипты)
9. [Сеть и P2P](#9-сеть-и-p2p)
10. [Системные модули](#10-системные-модули)
11. [Зависимости и конвенции](#11-зависимости-и-конвенции)

---

## 1. Нейросетевые модули

### 1.1. attention.c / attention.h — Transformer

**Назначение:** Pre-LN Transformer с Multi-Head Self-Attention на чистом C.

**Размер:** 654 строки (src) + 199 строк (header)

#### Ключевые константы

```c
#define KAT_VOCAB_SIZE     256    // Байт-уровневый словарь
#define KAT_EMBED_DIM       64    // Размерность эмбеддинга
#define KAT_NUM_HEADS         4    // Голов внимания
#define KAT_HEAD_DIM         16    // embed_dim / num_heads
#define KAT_FF_DIM          256    // FFN скрытый слой
#define KAT_NUM_LAYERS        2    // Трансформерных блоков
#define KAT_MAX_SEQ         512    // Макс. длина последовательности
#define KAT_EPSILON       1e-5f   // LayerNorm epsilon
```

#### Структуры

| Структура | Описание |
|-----------|----------|
| `KatEmbeddingTable` | token_embed[256][64] + pos_embed[512][64] |
| `KatAttentionHead` | wq[64][16], wk[64][16], wv[64][16] |
| `KatMultiHeadAttention` | heads[4] + wo[64][64] + LayerNorm |
| `KatFeedForward` | w1[64][256], b1[256], w2[256][64], b2[64] + LayerNorm |
| `KatTransformerBlock` | attn + ffn |
| `KatModel` | embed + layers[2] + final_ln + lm_head[64][256] |
| `KatWorkspace` | hidden, residual, Q/K/V буферы, logits, probs |

#### Функции

| Функция | Сигнатура | Описание |
|---------|-----------|----------|
| `kat_model_create` | `KatModel* (uint64_t seed)` | Xavier инициализация всех весов |
| `kat_model_destroy` | `void (KatModel*)` | Освобождение памяти |
| `kat_workspace_create` | `KatWorkspace* (void)` | Аллокация буферов forward pass |
| `kat_workspace_destroy` | `void (KatWorkspace*)` | Освобождение буферов |
| `kat_forward` | `int (model, ws, tokens, seq_len)` | Полный forward pass |
| `kat_extract_embedding` | `void (ws, out[64])` | Mean-pooling hidden states |
| `kat_sample` | `uint8_t (model, ws, temperature)` | Семплирование из softmax |
| `kat_train_step` | `float (model, ws, tokens, seq_len, target, lr)` | SPSA обучение |
| `kat_cosine_similarity` | `float (a, b, dim)` | Косинусное сходство |
| `kat_count_params` | `size_t (void)` | Общее число параметров (~100K) |
| `kat_serialize` | `size_t (model, buf, buf_size)` | Модель → байты |
| `kat_deserialize` | `int (model, buf, buf_size)` | Байты → модель |

#### Внутренние функции (static)

- `layer_norm()` — LayerNorm: x̂ = (x - μ) / √(σ² + ε), y = γx̂ + β
- `attention_head_forward()` — Одна голова: Q·Kᵀ/√d → causal mask → softmax → ·V
- `multi_head_attention_forward()` — 4 головы → concat → Wo → residual
- `feed_forward_forward()` — GELU(xW1+b1)·W2+b2 → residual
- `gelu()` — 0.5·x·(1+tanh(√(2/π)·(x+0.044715·x³)))
- `softmax()` — Стабильный softmax с вычитанием max
- `init_positional_embeddings()` — sin/cos позиционные эмбеддинги

---

### 1.2. world_model.c / world_model.h — Мировая модель

**Назначение:** Предсказательная модель мира. «Сжатие ≡ предсказание ≡ понимание».

**Размер:** 547 строк (src) + 242 строки (header)

#### Ключевые константы

```c
#define KWM_CONTEXT_SIZE   256    // Скользящее контекстное окно
#define KWM_HISTORY_SIZE  4096    // Буфер истории
#define KWM_MAX_CONCEPTS   128    // Макс. концептов
#define KWM_CONCEPT_DIM     64    // = KAT_EMBED_DIM
```

#### Структуры

| Структура | Описание |
|-----------|----------|
| `KwmConcept` | embedding[64], label[128], salience, surprise, frequency |
| `KwmStats` | total_loss, avg_loss, perplexity, total_tokens, surprise_integral |
| `KwmPrediction` | probs[256], predicted_token, confidence, surprise |
| `KwmContext` | backbone(KatModel*), workspace, context[256], history[4096], concepts[128] |

#### Функции

| Функция | Сигнатура | Описание |
|---------|-----------|----------|
| `kwm_create` | `KwmContext* (uint64_t seed)` | Создание (аллокация Transformer) |
| `kwm_destroy` | `void (KwmContext*)` | Освобождение |
| `kwm_observe` | `float (ctx, uint8_t byte)` | Наблюдение байта → surprise |
| `kwm_observe_block` | `void (ctx, data, len)` | Наблюдение блока |
| `kwm_predict` | `int (ctx, KwmPrediction*)` | Предсказание следующего |
| `kwm_generate` | `int (ctx, output, max_len, actual, temp)` | Авторегрессивная генерация |
| `kwm_embed_text` | `int (ctx, text, len, float*)` | Текст → вектор[64] |
| `kwm_similarity` | `float (ctx, a, a_len, b, b_len)` | Сходство двух текстов |
| `kwm_extract_concepts` | `int (ctx)` | Извлечение концептов |
| `kwm_learn_step` | `int (ctx, data, len)` | Шаг обучения |
| `kwm_serialize` | `size_t (ctx, buf, buf_size)` | Сериализация состояния |
| `kwm_deserialize` | `int (ctx, buf, buf_size)` | Десериализация |

---

## 2. Память и хранение

### 2.1. fractal_memory.c / fractal_memory.h — Фрактальная память

**Назначение:** 10-ричное дерево для ассоциативной памяти с волновой активацией.

**Размер:** 753 строки (src) + 155 строк (header)

#### Ключевые константы

```c
#define KFM_MAX_DEPTH       64      // Макс. глубина пути
#define KFM_MAX_PAYLOAD     512     // Макс. данные в узле
#define KFM_MAX_ASSOCIATIONS 16     // Связей на узел
#define KFM_MAX_NODES       65536   // Макс. узлов
```

#### Перечисления

```c
typedef enum {
    KFM_NODE_EMPTY   = 0,   // Транзитный
    KFM_NODE_CONCEPT = 1,   // Понятие с данными
    KFM_NODE_LINK    = 2,   // Ссылка на путь
    KFM_NODE_PATTERN = 3    // Сжатое представление
} KfmNodeType;
```

#### Функции

| Функция | Описание |
|---------|----------|
| `kfm_init(ctx, seed)` | Инициализация (корень дерева) |
| `kfm_free(ctx)` | Рекурсивное освобождение |
| `kfm_insert(ctx, path, len, payload, size)` | Вставка понятия |
| `kfm_lookup(ctx, path, len)` | Точный поиск → KfmNode* |
| `kfm_search(ctx, query, qlen, results, max)` | Ассоциативный поиск |
| `kfm_associate(ctx, a, a_len, b, b_len, strength)` | Создание связи |
| `kfm_activate(ctx, path, len, energy)` | Волна активации |
| `kfm_decay(ctx)` | Затухание неиспользуемых |
| `kfm_mutate(ctx, path, len, new_path, new_len)` | Мутация пути |
| `kfm_text_to_path(text, len, path, max)` | Текст → десятичный путь |
| `kfm_path_to_text(ctx, path, len, text, max)` | Путь → текст |
| `kfm_serialize(ctx, buf, size)` | Сохранение (magic KFM1) |
| `kfm_deserialize(ctx, buf, size)` | Загрузка |
| `kfm_stats(ctx, stats)` | Статистика |

---

### 2.2. logical_memory.c / logical_memory.h — Логическая память

**Назначение:** Память без данных — хранение логики генерации (lazy materialization).

**Размер:** 802 строки (src) + 204 строки (header)

#### Типы выражений

```c
typedef enum {
    LOGIC_CONSTANT,      // Фиксированное значение
    LOGIC_VARIABLE,      // Переменная с привязкой
    LOGIC_REPEAT,        // repeat(pattern, N)
    LOGIC_SEQUENCE,      // seq(start, step, count)
    LOGIC_TRANSFORM,     // transform(input, fn)
    LOGIC_CONDITIONAL,   // if(cond, then, else)
    LOGIC_COMPOSITION,   // compose(e1, e2, ...)
    LOGIC_RELATION       // relates(A, B, type)
} LogicType;
```

#### Функции

| Функция | Описание |
|---------|----------|
| `lm_create_memory()` | Создание (1024 ячейки) |
| `lm_destroy_memory(mem)` | Освобождение |
| `lm_logic_constant(value)` | Создать LExpr: constant |
| `lm_logic_repeat(pattern, count)` | Создать LExpr: repeat |
| `lm_logic_sequence(start, step, count)` | Создать LExpr: sequence |
| `lm_logic_compose(e1, e2)` | Создать LExpr: composition |
| `lm_logic_relation(left, right, type)` | Создать LExpr: relation |
| `lm_logic_variable(name)` | Создать LExpr: variable |
| `lm_logic_transform(input, fn)` | Создать LExpr: transform |
| `lm_logic_conditional(cond, then, else)` | Создать LExpr: conditional |
| `lm_store_logic(mem, id, logic)` | Сохранить в ячейку |
| `lm_materialize(mem, id, output, max, actual)` | Материализовать данные |
| `lm_optimize_logic(expr)` | Оптимизация (свёртка repeat, слияние констант) |

---

### 2.3. genome.c / genome.h — Геном-блокчейн

**Назначение:** HMAC-SHA256 блокчейн для аудита всех решений ИИ.

**Размер:** 887 строк (src) + 126 строк (header)

#### Константы

```c
#define KOLIBRI_HASH_SIZE      32
#define KOLIBRI_EVENT_TYPE_SIZE 32
#define KOLIBRI_PAYLOAD_SIZE   512
#define KOLIBRI_HMAC_KEY_SIZE  64
#define KOLIBRI_WAL_MAGIC      0x4B4F4C57414C0001ULL  // "KOLWAL"
```

#### Функции

| Функция | Описание |
|---------|----------|
| `kg_open(ctx, path, key, key_len)` | Открыть/создать файл генома |
| `kg_close(ctx)` | Закрыть (+ flush WAL) |
| `kg_append(ctx, event_type, payload, out)` | Добавить блок |
| `kg_verify_file(path, key, key_len)` | Проверить цепочку целостности |
| `kg_encode_payload(utf8, out, out_len)` | Кодирование UTF-8 payload |
| `kg_wal_enable(ctx)` | Включить WAL |
| `kg_wal_disable(ctx)` | Отключить WAL |
| `kg_wal_recover(ctx)` | Восстановление после сбоя |
| `kg_wal_checkpoint(ctx)` | Перенос WAL → основной файл |
| `kg_stream_append(ctx, event, payload, out)` | Потоковая запись через WAL |
| `kg_get_stats(ctx, stats)` | Статистика генома |
| `kg_read_block(ctx, index, out)` | Чтение блока по индексу |
| `kg_iterate_blocks(ctx, callback, data)` | Итерация по всем блокам |

---

## 3. Сжатие

### 3.1. compress.c / compress.h — 16 алгоритмов сжатия

**Размер:** 3434 строки

Модуль реализует полный набор алгоритмов сжатия:

| Алгоритм | Описание |
|----------|----------|
| RLE | Run-Length Encoding |
| Dictionary | Словарное кодирование |
| Hybrid | RLE + Dictionary комбинация |
| Huffman | Статическое/адаптивное кодирование Хаффмана |
| LZ77 | Скользящее окно + обратные ссылки |
| ANS | Asymmetric Numeral Systems |
| Delta | Дельта-кодирование |
| BWT | Burrows-Wheeler Transform |
| MTF | Move-To-Front |
| Fibonacci | Фибоначчи-кодирование |
| Kolibri v2-v6 | Собственные гибридные алгоритмы |

### 3.2. predictive_compress.c / predictive_compress.h — Нейросжатие

**Размер:** 620 строк

(См. раздел 3.3 в ARCHITECTURE.md для полного описания)

### 3.3. huffman_ans.c / huffman_ans.h — Huffman + ANS

Реализация Huffman-кодирования и Asymmetric Numeral Systems.

---

## 4. Знания и обучение

### 4.1. knowledge.c / knowledge.h — База знаний

Хранение и поиск фактов. Структура графа: факты (nodes) + связи (edges).

### 4.2. knowledge_index.c / knowledge_index.h — Индексирование

Быстрый поиск по базе знаний через хеш-индексы.

### 4.3. knowledge_queue.c / knowledge_queue.h — Очередь

Асинхронная очередь для пакетной обработки знаний.

### 4.4. corpus_trainer.c / corpus_trainer.h — Корпус-тренер

**Размер:** 1679 строк

| Параметр | Значение |
|----------|----------|
| MAX_PATTERNS | 131 072 (128K) |
| MAX_EDGES | 262 144 (256K) |
| PATTERN_BUCKETS | 65 521 |
| EDGE_BUCKETS | 131 071 |

Основные функции:
- `klm_create()` — создание модели
- `klm_train_file()` — обучение на файле
- `klm_train_directory()` — обучение на директории
- `klm_query()` — запрос к модели
- `klm_evolve()` — эволюция связей
- `klm_distill()` — дистилляция (удаление слабых)
- `klm_serialize()` / `klm_deserialize()` — сохранение/загрузка

### 4.5. corpus_learning.c — Обучение корпуса

Абстракция над corpus_trainer для потокового обучения.

### 4.6. auto_learn.c / auto_learn.h — Автоматическое обучение

Автоматическая загрузка и обучение на новых данных.

---

## 5. Формулы и эволюция

### 5.1. formula.c / formula.h — Числовые формулы

**Размер:** 1339 строк (src) + 207 строк (header)

#### Ключевые структуры

```c
typedef struct {
    uint8_t digits[4000];   // 4000-цифровой геном
    size_t length;
} KolibriGene;

typedef struct {
    int input_hash;
    int output_hash;
    char question[256];
    char answer[512];
    uint8_t question_digits[...];
    uint8_t answer_digits[...];
} KolibriAssociation;
```

#### Типы мутаций

```c
typedef enum {
    KOLIBRI_MUTATION_POINT,     // Замена одной цифры
    KOLIBRI_MUTATION_SWAP,      // Обмен двух цифр
    KOLIBRI_MUTATION_INVERT,    // Инверсия сегмента
    KOLIBRI_MUTATION_SCRAMBLE,  // Перемешивание
    KOLIBRI_MUTATION_SHIFT      // Сдвиг
} KolibriMutationType;
```

#### Типы кроссовера

```c
typedef enum {
    KOLIBRI_CROSSOVER_SINGLE_POINT,  // Одноточечный
    KOLIBRI_CROSSOVER_TWO_POINT,     // Двухточечный
    KOLIBRI_CROSSOVER_UNIFORM        // Равномерный
} KolibriCrossoverType;
```

#### Домены знаний

```c
typedef enum {
    KOLIBRI_DOMAIN_GENERAL,    KOLIBRI_DOMAIN_MEDICINE,
    KOLIBRI_DOMAIN_IT,         KOLIBRI_DOMAIN_PHYSICS,
    KOLIBRI_DOMAIN_MATH,       KOLIBRI_DOMAIN_CHEMISTRY,
    KOLIBRI_DOMAIN_BIOLOGY,    KOLIBRI_DOMAIN_HISTORY,
    KOLIBRI_DOMAIN_LAW,        KOLIBRI_DOMAIN_ECONOMICS,
    KOLIBRI_DOMAIN_CUSTOM = 255
} KolibriDomainType;
```

### 5.2. formula_logic.c / formula_logic.h — Мета-формулы

**Размер:** 1154 строки

Формулы для генерации, эволюции и сжатия других формул.
(См. раздел 3.8 в ARCHITECTURE.md)

### 5.3. random.c / random.h — Генератор случайных чисел

Детерминистический LCG (Linear Congruential Generator):

```c
typedef struct {
    uint64_t state;
} KolibriRng;

uint64_t kolibri_rng_next(KolibriRng *rng);
double   kolibri_rng_double(KolibriRng *rng);  // [0, 1)
int      kolibri_rng_range(KolibriRng *rng, int min, int max);
```

---

## 6. Инференс и генерация

### 6.1. inference.c / inference.h — Конвейер инференса

**Размер:** 774 строки (src) + 155 строк (header)

5 стратегий вывода:
1. DIRECT — прямой поиск
2. FORMULA — формульный вывод
3. LOGICAL — логическое рассуждение
4. CHAIN — chain-of-thought
5. HYBRID — гибрид с голосованием

(См. раздел 3.6 в ARCHITECTURE.md для полного описания)

### 6.2. text_generation.c / generation.h — Генерация текста

Генерация текстовых ответов на основе результатов инференса.
Использует World Model для авторегрессивной генерации.

---

## 7. Числовое кодирование

### 7.1. decimal.c / decimal.h — Десятичная арифметика

Операции с большими десятичными числами:
- Сложение, вычитание, умножение десятичных строк
- Конвертация int/float ↔ десятичные

### 7.2. decimal_fast10x.c — Оптимизированная десятичная арифметика

Быстрая реализация (10x speedup) для массовых операций.

### 7.3. digits.c / digits.h — Операции с цифрами

Базовые операции: сравнение, копирование, хеширование цифровых последовательностей.

### 7.4. digit_text.c / digit_text.h — Кодирование текст ↔ цифры

```c
// Кодирование: 1 символ UTF-8 → 3 десятичных цифры
// 'A' = 65 → [0, 6, 5]
// 'Б' = 0xD0 0x91 → [2,0,8, 1,4,5]

size_t kolibri_text_to_digits(const char *text, uint8_t *digits, size_t max);
size_t kolibri_digits_to_text(const uint8_t *digits, size_t len, char *text, size_t max);
```

### 7.5. symbol_table.c / symbol_table.h — Таблица символов

Маппинг символов Unicode на числовые коды для формулного ядра.

### 7.6. semantic_digits.c / semantic.h — Семантическое кодирование

Семантическое кодирование текста в числовые последовательности с учётом контекста.

---

## 8. Язык и скрипты

### 8.1. script.c / script.h — Интерпретатор KolibriScript

**Размер:** 3240 строк

Собственный DSL (Domain-Specific Language) для управления ИИ.

#### Синтаксис KolibriScript (.ks)

```
ask "Что такое ИИ?"
teach "Искусственный интеллект — это..."
evolve rounds=100
train file=/data/corpus.txt
sync peer=192.168.1.10:9090
verify genome
stats
canvas width=80
```

#### Команды

| Команда | Описание |
|---------|----------|
| `ask` | Запрос к ИИ |
| `teach` | Обучение |
| `evolve` | Эволюция формул |
| `train` | Обучение на файле/директории |
| `sync` | P2P-синхронизация |
| `verify` | Верификация генома |
| `stats` | Статистика |
| `canvas` | ASCII-визуализация |
| `compress` | Сжатие данных |
| `decompress` | Распаковка |

---

## 9. Сеть и P2P

### 9.1. net.c / net.h — Сетевой модуль

HTTP-клиент на чистых сокетах (BSD sockets). Поддержка TCP соединений.

```c
// P2P сообщение
#define KOLIBRI_MAX_PAYLOAD 4200U

typedef enum {
    KOLIBRI_MSG_HELLO,
    KOLIBRI_MSG_SHARE,
    KOLIBRI_MSG_SYNC,
    KOLIBRI_MSG_QUERY,
    KOLIBRI_MSG_RESPONSE
} KolibriNetMessageType;
```

### 9.2. roy.c / roy.h — Рой-протокол

P2P-синхронизация между узлами Kolibri (рой-интеллект).

### 9.3. web_crawler.c / web_crawler.h — Веб-краулер

Краулер для автоматического сбора данных с веб-страниц. Парсинг HTML на чистом C.

---

## 10. Системные модули

### 10.1. async_executor.c / async_executor.h — Асинхронный исполнитель

Пул потоков (pthreads) для параллельного выполнения задач.

### 10.2. sim.c / sim.h — Симулятор

Запуск автономных ИИ-агентов в изолированной среде.

### 10.3. trace.c / trace.h — Трассировка

Логирование и отладка (JSONL-формат трассировки).

### 10.4. context_window.c / context.h — Контекстное окно

Управление контекстом для C-уровня (скользящее окно наблюдений).

### 10.5. wasm_bridge.c — WASM-мост

Экспорт C-функций для вызова из JavaScript через WebAssembly.

### 10.6. wasm_genome_stub.c — WASM-заглушка генома

Заглушка для OpenSSL-зависимых функций в WASM-сборке
(OpenSSL недоступен в браузере).

### 10.7. sha256.c — SHA-256

Собственная реализация SHA-256 (для сред без OpenSSL).

### 10.8. phoneme.c / phoneme.h — Фонемный анализ

Преобразование текста в фонемы. Поддержка русского и английского языков.

### 10.9. evolve_ffi.c — FFI для эволюции

Foreign Function Interface для вызова эволюционных функций из Python (ctypes).

### 10.10. knowledge_server.c — Сервер знаний

HTTP-сервер для обслуживания запросов к базе знаний (встроенный).

---

## 11. Зависимости и конвенции

### Внешние зависимости

| Библиотека | Использование | Обязательна |
|-----------|---------------|-------------|
| OpenSSL (libcrypto) | HMAC-SHA256 для генома | Да (native) |
| pthreads | Многопоточность (async_executor) | Да |
| libm | Математика (sin, cos, exp, tanh, log) | Да |
| SQLite3 | Хранение (через Python) | Нет (C-уровень) |

### Конвенции кода

| Правило | Описание |
|---------|----------|
| Язык | Строгий C23 |
| Стиль | `snake_case` для всего |
| Комментарии | **Обязательно на русском** для логики и заголовков |
| Память | Ручное `malloc`/`free`, никаких smart pointers |
| Ошибки | Возврат int (0 = успех, <0 = ошибка) или NULL |
| Контекст | Большинство функций принимают `*Context` / `*Pool` |
| Заголовки | Только из `backend/include/kolibri/` для внешних пользователей |
| Сборка | CMake + Ninja |

### Маппинг .c → .h

```
backend/src/attention.c       → backend/include/kolibri/attention.h
backend/src/world_model.c     → backend/include/kolibri/world_model.h
backend/src/predictive_compress.c → backend/include/kolibri/predictive_compress.h
backend/src/fractal_memory.c  → backend/include/kolibri/fractal_memory.h
backend/src/logical_memory.c  → backend/include/kolibri/logical_memory.h
backend/src/inference.c       → backend/include/kolibri/inference.h
backend/src/corpus_trainer.c  → backend/include/kolibri/corpus_trainer.h
backend/src/formula_logic.c   → backend/include/kolibri/formula_logic.h
backend/src/formula.c         → backend/include/kolibri/formula.h
backend/src/genome.c          → backend/include/kolibri/genome.h
backend/src/compress.c        → backend/include/kolibri/compress.h
backend/src/script.c          → backend/include/kolibri/script.h
backend/src/knowledge.c       → backend/include/kolibri/knowledge.h
backend/src/net.c             → backend/include/kolibri/net.h
backend/src/random.c          → backend/include/kolibri/random.h
... (и остальные)
```

---

*Документация C-ядра · Kolibri OS · Copyright (c) 2025 Кочуров Владислав Евгеньевич*

/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Corpus Trainer — Масштабное обучение с фиксированным размером модели
 *
 * Философия: модель НЕ растёт при обучении.
 * Как мозг человека — фиксированный объём, но качество связей растёт.
 * При переполнении — дистилляция: слабые знания вытесняются, сильные
 * усиливаются через эволюцию.
 *
 * Формат модели: .klm (Kolibri Learning Model)
 *   [Паттерны] хеш-таблица слово → 64-цифровой паттерн  (~26 МБ)
 *   [Граф]     рёбра знаний слово↔слово, вес связи      (~5 МБ)
 *   [Мета]     статистика обучения                       (<1 КБ)
 *   Итого: ~32 МБ макс. при любом объёме входных данных
 */

#ifndef KOLIBRI_CORPUS_TRAINER_H
#define KOLIBRI_CORPUS_TRAINER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Лимиты компактной модели (настраиваемы при компиляции)
 * ============================================================ */

#define KLM_MAGIC               0x4B4C4D31  /* "KLM1" */
#define KLM_VERSION             1

/* Начальные размеры хеш-таблиц (степени двойки для быстрого modulo) */
/* ЛИМИТЫ СНЯТЫ: таблицы растут динамически через рехеширование */
#ifndef KLM_INITIAL_PATTERNS
#define KLM_INITIAL_PATTERNS    524288      /* Начальный размер: 128K слов */
#endif
#ifndef KLM_INITIAL_EDGES
#define KLM_INITIAL_EDGES       1048576      /* Начальный размер: 256K рёбер */
#endif

/* Обратная совместимость (старый код компилируется) */
#ifndef KLM_MAX_PATTERNS
#define KLM_MAX_PATTERNS        2097152
#endif
#ifndef KLM_MAX_EDGES
#define KLM_MAX_EDGES           4194304
#endif

#define KLM_PATTERN_SIZE        64          /* цифр в паттерне */
#define KLM_WORD_MAX            128         /* макс. длина слова */
#define KLM_LOAD_FACTOR         0.7         /* порог заполнения для рехеширования */
#define KLM_GROWTH_FACTOR       2           /* множитель роста при рехешировании */
#define KLM_MAX_CAPACITY        (1ULL << 30) /* абс. максимум: 1 млрд записей */

/* Лимит размера модели на диске снят — ограничивается только RAM */
#define KLM_MODEL_SIZE_LIMIT    0           /* 0 = без лимита */

/* ============================================================
 * Конфигурация обучения
 * ============================================================ */

typedef struct {
    size_t  evolution_generations;   /* поколений эволюции на новое слово (10) */
    size_t  distill_interval;       /* дистилляция каждые N документов (1000) */
    size_t  context_window;         /* размер контекстного окна (32 слова) */
    double  min_fitness;            /* мин. fitness для хранения паттерна (0.05) */
    double  eviction_ratio;         /* доля вытесняемых при дистилляции (0.1) */
    double  merge_threshold;        /* порог слияния похожих паттернов (0.95) */
    bool    verbose;                /* подробный вывод прогресса */
} KlmTrainerConfig;

/* ============================================================
 * Структуры данных модели
 * ============================================================ */

/** Запись в хеш-таблице паттернов */
typedef struct {
    uint32_t hash;                          /* djb2 хеш слова */
    char     word[KLM_WORD_MAX];            /* слово */
    uint8_t  pattern[KLM_PATTERN_SIZE];     /* числовой паттерн 0–9 */
    float    fitness;                       /* качество паттерна [0.0–1.0] */
    uint32_t frequency;                     /* частота встречаемости */
    uint32_t last_epoch;                    /* эпоха последнего обновления */
    uint64_t version;                       /* версия для дельта-синхронизации */
    uint8_t  occupied;                      /* слот занят? */
    uint8_t  dirty;                         /* изменён с последней синхронизации? */
} KlmPatternEntry;

/** Ребро графа знаний */
typedef struct {
    uint32_t source_hash;       /* хеш слова-источника */
    uint32_t target_hash;       /* хеш слова-цели */
    float    weight;            /* сила связи [0.0–1.0] */
    uint32_t cooccurrence;      /* количество совместных появлений */
    uint64_t version;           /* версия для дельта-синхронизации */
    uint8_t  occupied;          /* слот занят? */
    uint8_t  dirty;             /* изменён с последней синхронизации? */
} KlmEdge;

/* ============================================================
 * Индекс смежности — O(1) поиск рёбер по хешу слова
 * ============================================================ */

/** Запись в индексе смежности (pooled linked list) */
typedef struct {
    size_t edge_slot;           /* индекс в edges[] */
    size_t next;                /* следующий в цепочке (SIZE_MAX = конец) */
} KlmAdjEntry;

/** Индекс смежности: word_hash → цепочка рёбер */
typedef struct {
    size_t       *buckets;      /* массив head-индексов (SIZE_MAX = пусто) */
    KlmAdjEntry  *entries;      /* пул записей */
    size_t        bucket_count; /* количество бакетов (степень двойки) */
    size_t        entry_count;  /* использовано записей */
    size_t        entry_capacity; /* макс. записей (2 × edge_capacity) */
} KlmAdjIndex;

/** Компактная модель (фиксированный размер) */
typedef struct {
    /* Хеш-таблица паттернов */
    KlmPatternEntry *patterns;
    size_t pattern_count;
    size_t pattern_capacity;        /* = KLM_MAX_PATTERNS */

    /* Граф знаний (хеш-таблица рёбер) */
    KlmEdge *edges;
    size_t edge_count;
    size_t edge_capacity;           /* = KLM_MAX_EDGES */

    /* Метаданные */
    uint64_t documents_trained;
    uint64_t tokens_processed;
    uint32_t current_epoch;
    double   avg_pattern_fitness;
    double   avg_edge_weight;

    /* Версионирование для дельта-синхронизации */
    uint64_t global_version;        /* глобальная версия модели */
    uint64_t last_sync_version;     /* версия на момент последней синхронизации */

    /* Флаги динамического роста */
    size_t   rehash_count;          /* сколько раз рехешировалась таблица */

    /* Индекс смежности для O(1) поиска рёбер по слову */
    KlmAdjIndex adj_index;
} KlmModel;

/* ============================================================
 * Статистика обучения
 * ============================================================ */

typedef struct {
    size_t  documents_processed;
    size_t  documents_failed;
    size_t  tokens_total;
    size_t  patterns_learned;
    size_t  patterns_evicted;
    size_t  edges_created;
    size_t  edges_evicted;
    size_t  distillation_runs;
    double  avg_fitness;
    double  training_time_sec;
    double  model_size_mb;
} KlmTrainerStats;

/* ============================================================
 * Контекст обучения
 * ============================================================ */

typedef struct {
    KlmModel         model;
    KlmTrainerConfig config;
    KlmTrainerStats  stats;
} KlmTrainerContext;

/* ============================================================
 * API — Жизненный цикл
 * ============================================================ */

/** Конфигурация по умолчанию */
KlmTrainerConfig klm_default_config(void);

/** Создание контекста обучения */
KlmTrainerContext *klm_trainer_create(const KlmTrainerConfig *config);

/** Уничтожение контекста */
void klm_trainer_free(KlmTrainerContext *ctx);

/* ============================================================
 * API — Обучение
 * ============================================================ */

/** Обучение на сыром тексте */
int klm_train_text(KlmTrainerContext *ctx, const char *text, size_t len);

/** Обучение на документе (заголовок + тело) */
int klm_train_document(KlmTrainerContext *ctx,
                       const char *title, const char *text);

/** Обучение на файле (.txt / .md) */
int klm_train_file(KlmTrainerContext *ctx, const char *filepath);

/** Рекурсивное обучение на директории. Возвращает количество файлов. */
size_t klm_train_directory(KlmTrainerContext *ctx, const char *dirpath);

/* ============================================================
 * API — Дистилляция знаний (компрессия)
 * ============================================================ */

/**
 * Дистилляция: вытеснение слабых паттернов/рёбер, перестройка таблиц.
 * Вызывается автоматически при заполнении, но можно вызвать вручную.
 * @return количество вытесненных записей
 */
size_t klm_distill(KlmTrainerContext *ctx);

/* ============================================================
 * API — Сериализация (.klm формат)
 * ============================================================ */

/** Сохранение модели в файл */
int klm_save(const KlmTrainerContext *ctx, const char *filepath);

/** Загрузка модели из файла (добавляет к текущей) */
int klm_load(KlmTrainerContext *ctx, const char *filepath);

/* ============================================================
 * API — Запросы к модели
 * ============================================================ */

/** Найти слова, похожие по паттерну */
int klm_query_similar(const KlmTrainerContext *ctx, const char *word,
                      char results[][KLM_WORD_MAX], float *scores,
                      size_t max_results);

/** Семантическое сходство двух слов [0.0–1.0] */
double klm_word_similarity(const KlmTrainerContext *ctx,
                           const char *w1, const char *w2);

/** Найти ассоциированные слова через граф знаний */
size_t klm_get_associations(const KlmTrainerContext *ctx, const char *word,
                            char results[][KLM_WORD_MAX], float *weights,
                            size_t max_results);

/** Ответить на вопрос, используя граф знаний */
int klm_answer(const KlmTrainerContext *ctx, const char *question,
               char *answer, size_t answer_max);

/** Ответить в числовом формате: каждый байт ответа → 3 цифры (0-9) */
int klm_answer_digits(const KlmTrainerContext *ctx, const char *question,
                      uint8_t *digits, size_t digits_max, size_t *digits_out);

/* ============================================================
 * API — Статистика
 * ============================================================ */

/** Получить статистику обучения */
KlmTrainerStats klm_get_stats(const KlmTrainerContext *ctx);

/** Размер модели в МБ */
double klm_model_size_mb(const KlmTrainerContext *ctx);

/** Вывод статистики в stderr */
void klm_print_stats(const KlmTrainerContext *ctx);

/* ============================================================
 * API — Индекс смежности
 * ============================================================ */

/**
 * Перестроить индекс смежности из текущего графа рёбер.
 * Вызывается автоматически после рехеширования, дистилляции и загрузки.
 */
int klm_adj_rebuild(KlmTrainerContext *ctx);

/** Освободить память индекса смежности */
void klm_adj_free(KlmAdjIndex *idx);

/* ============================================================
 * API — Динамическое рехеширование (снятие лимитов)
 * ============================================================ */

/**
 * Увеличить ёмкость таблицы паттернов.
 * @param new_capacity новый размер (должен быть степенью двойки)
 * @return 0 = OK, -1 = ошибка аллокации
 */
int klm_rehash_patterns(KlmTrainerContext *ctx, size_t new_capacity);

/**
 * Увеличить ёмкость таблицы рёбер.
 * @param new_capacity новый размер (должен быть степенью двойки)
 * @return 0 = OK, -1 = ошибка аллокации
 */
int klm_rehash_edges(KlmTrainerContext *ctx, size_t new_capacity);

/**
 * Автоматическое расширение: удваивает ёмкость при load >= 70%.
 * @return количество расширений (0/1/2)
 */
int klm_auto_grow(KlmTrainerContext *ctx);

/* ============================================================
 * API — Дельта-синхронизация
 * ============================================================ */

/** Пакет дельта-синхронизации */
typedef struct {
    KlmPatternEntry *patterns;  /* изменённые паттерны */
    size_t pattern_count;
    KlmEdge         *edges;     /* изменённые рёбра */
    size_t edge_count;
    uint64_t from_version;      /* от какой версии дельта */
    uint64_t to_version;        /* до какой версии */
} KlmDeltaPacket;

/**
 * Извлечь дельту: все паттерны и рёбра с version > since_version.
 * Вызывающий должен освободить результат через klm_delta_free().
 */
KlmDeltaPacket *klm_delta_extract(const KlmTrainerContext *ctx, uint64_t since_version);

/**
 * Применить дельту от другой ноды к текущей модели.
 * Конфликты решаются по версии: побеждает запись с большей version.
 * @return количество применённых изменений
 */
size_t klm_delta_apply(KlmTrainerContext *ctx, const KlmDeltaPacket *delta);

/**
 * Пометить все dirty-записи как синхронизированные.
 */
void klm_delta_mark_synced(KlmTrainerContext *ctx);

/**
 * Сериализовать дельта-пакет в буфер.
 * @param out_size будет записан размер буфера
 * @return аллоцированный буфер (вызывающий освобождает через free())
 */
uint8_t *klm_delta_serialize(const KlmDeltaPacket *delta, size_t *out_size);

/**
 * Десериализовать дельта-пакет из буфера.
 * @return аллоцированный пакет (вызывающий освобождает через klm_delta_free())
 */
KlmDeltaPacket *klm_delta_deserialize(const uint8_t *data, size_t data_size);

/** Освободить дельта-пакет */
void klm_delta_free(KlmDeltaPacket *delta);

/* ============================================================
 * API — Шардирование графа (consistent hashing)
 * ============================================================ */

/** Конфигурация шардирования */
typedef struct {
    uint32_t node_id;               /* ID текущей ноды */
    uint32_t *ring;                 /* хеш-кольцо (виртуальные ноды) */
    uint32_t *ring_node_ids;        /* ID нод для каждой позиции в кольце */
    size_t    ring_size;            /* размер кольца */
    uint32_t *node_ids;             /* все ID нод в кластере */
    size_t    node_count;           /* количество нод */
    size_t    vnodes_per_node;      /* виртуальных нод на физическую (128) */
} KlmShardConfig;

/** Инициализация шардирования */
int klm_shard_init(KlmShardConfig *cfg, uint32_t node_id,
                   const uint32_t *node_ids, size_t node_count,
                   size_t vnodes_per_node);

/** Определить, какой ноде принадлежит данный хеш */
uint32_t klm_shard_owner(const KlmShardConfig *cfg, uint32_t hash);

/** Проверить, принадлежит ли хеш текущей ноде */
bool klm_shard_is_local(const KlmShardConfig *cfg, uint32_t hash);

/** Перестроить кольцо при изменении состава кластера */
int klm_shard_rebuild(KlmShardConfig *cfg,
                      const uint32_t *node_ids, size_t node_count);

/** Освободить ресурсы шардирования */
void klm_shard_free(KlmShardConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CORPUS_TRAINER_H */

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

/* Размеры хеш-таблиц (степени двойки для быстрого modulo) */
#ifndef KLM_MAX_PATTERNS
#define KLM_MAX_PATTERNS        131072      /* 128K слов */
#endif
#ifndef KLM_MAX_EDGES
#define KLM_MAX_EDGES           262144      /* 256K рёбер графа знаний */
#endif

#define KLM_PATTERN_SIZE        64          /* цифр в паттерне */
#define KLM_WORD_MAX            128         /* макс. длина слова */
#define KLM_LOAD_FACTOR         0.7         /* порог заполнения для дистилляции */

/* Лимит размера модели на диске (50 МБ) */
#define KLM_MODEL_SIZE_LIMIT    (50 * 1024 * 1024)

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
    uint8_t  occupied;                      /* слот занят? */
} KlmPatternEntry;

/** Ребро графа знаний */
typedef struct {
    uint32_t source_hash;       /* хеш слова-источника */
    uint32_t target_hash;       /* хеш слова-цели */
    float    weight;            /* сила связи [0.0–1.0] */
    uint32_t cooccurrence;      /* количество совместных появлений */
    uint8_t  occupied;          /* слот занят? */
} KlmEdge;

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

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CORPUS_TRAINER_H */

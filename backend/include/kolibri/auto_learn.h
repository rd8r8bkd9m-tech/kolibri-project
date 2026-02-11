/*
 * auto_learn.h
 *
 * Автономный цикл обучения (Autonomous Learning Loop) для Kolibri AGI
 *
 * Реализует замкнутый цикл самообучения без вмешательства человека:
 *
 *   ┌─> Наблюдение ─> Предсказание ─> Сравнение ─> Обновление ─┐
 *   │                                                            │
 *   └────────── Генерация «любопытства» ◄─────────────────────────┘
 *
 * Три режима обучения:
 *   1. НАБЛЮДЕНИЕ:  Подача внешних данных (corpus, web, файлы)
 *   2. ЛЮБОПЫТСТВО: Генерация запросов к самому себе (self-play)
 *   3. ЭВОЛЮЦИЯ:    Мутация параметров + отбор лучших вариантов
 *
 * Метрика прогресса: сжатие = предсказание = понимание
 *   - loss↓ = модель лучше предсказывает = лучше понимает
 *   - surprise↑ на новых данных = модель нашла что-то новое
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_AUTO_LEARN_H
#define KOLIBRI_AUTO_LEARN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== КОНФИГУРАЦИЯ ========== */

#define KAL_MAX_SOURCES        32    /* Максимум источников данных         */
#define KAL_MAX_CHECKPOINTS    16    /* Максимум сохранённых чекпоинтов    */
#define KAL_BATCH_SIZE         16    /* Размер батча (байт)                */
#define KAL_EVAL_WINDOW        32    /* Окно для вычисления eval loss      */

/* ========== ТИПЫ ========== */

/* Режим обучения */
typedef enum {
    KAL_MODE_OBSERVATION,    /* Обучение на внешних данных              */
    KAL_MODE_CURIOSITY,      /* Самообучение: генерация + анализ        */
    KAL_MODE_EVOLUTION,      /* Эволюция параметров (мутация + отбор)   */
    KAL_MODE_MIXED           /* Комбинация всех режимов                 */
} KalLearningMode;

/* Источник данных */
typedef enum {
    KAL_SOURCE_FILE,         /* Файл на диске                           */
    KAL_SOURCE_MEMORY,       /* Буфер в памяти                          */
    KAL_SOURCE_GENERATED     /* Сгенерированные самой моделью данные    */
} KalSourceType;

typedef struct {
    KalSourceType type;
    char path[256];          /* Путь к файлу (для FILE)                 */
    const uint8_t *data;     /* Указатель на данные (для MEMORY)        */
    size_t data_len;         /* Длина данных                            */
    size_t cursor;           /* Текущая позиция чтения                  */
    float weight;            /* Вес источника при miксе                 */
} KalDataSource;

/* Чекпоинт — снимок состояния */
typedef struct {
    uint64_t tick;            /* Тик создания                            */
    double   eval_loss;       /* Loss на eval данных в момент создания    */
    double   compression;     /* Коэффициент сжатия                      */
    size_t   state_size;      /* Размер сериализованного состояния        */
    uint8_t *state_data;      /* Сериализованные веса (heap)              */
} KalCheckpoint;

/* Метрики автообучения */
typedef struct {
    uint64_t total_ticks;         /* Всего тиков обучения                */
    uint64_t observation_ticks;   /* Тиков в режиме наблюдения           */
    uint64_t curiosity_ticks;     /* Тиков в режиме любопытства          */
    uint64_t evolution_ticks;     /* Тиков в режиме эволюции             */

    double current_loss;          /* Текущий loss (bits/byte)            */
    double best_loss;             /* Лучший loss за всё время            */
    double eval_loss;             /* Loss на held-out данных             */

    double curiosity_surprise;    /* Средняя «удивлённость» при self-play */
    double learning_velocity;     /* Скорость снижения loss              */

    size_t concepts_learned;      /* Новых концептов                      */
    size_t checkpoints_created;   /* Создано чекпоинтов                   */
    size_t evolution_mutations;   /* Мутаций параметров                   */
    size_t evolution_improvements;/* Успешных мутаций                     */
} KalMetrics;

/* Предварительная декларация */
struct KwmContext;

/* ========== ГЛАВНАЯ СТРУКТУРА ========== */

typedef struct {
    /* Мировая модель (владеет Transformer-бэкбоном) */
    struct KwmContext *world_model;

    /* Источники данных */
    KalDataSource sources[KAL_MAX_SOURCES];
    size_t        source_count;

    /* Чекпоинты */
    KalCheckpoint checkpoints[KAL_MAX_CHECKPOINTS];
    size_t        checkpoint_count;

    /* Параметры */
    KalLearningMode mode;
    float learning_rate;           /* Начальная скорость обучения        */
    float lr_decay;                /* Затухание lr (0.999 по умолчанию)  */
    float mutation_strength;       /* Сила мутации параметров             */
    float curiosity_temperature;   /* Температура генерации self-play     */
    uint64_t checkpoint_interval;  /* Тиков между чекпоинтами             */
    uint64_t eval_interval;        /* Тиков между eval                    */

    /* Eval данные (held-out set) */
    uint8_t *eval_data;
    size_t   eval_len;

    /* Метрики */
    KalMetrics metrics;
    uint64_t seed;
} KalContext;

/* ========== API ========== */

/* --- Жизненный цикл --- */

/** Создать контекст автообучения с мировой моделью */
KalContext* kal_create(uint64_t seed);

/** Уничтожить контекст */
void kal_destroy(KalContext *ctx);

/* --- Источники данных --- */

/** Добавить файл как источник данных */
int kal_add_file_source(KalContext *ctx, const char *path, float weight);

/** Добавить буфер памяти как источник */
int kal_add_memory_source(KalContext *ctx, const uint8_t *data,
                          size_t len, float weight);

/** Задать eval данные (held-out set для оценки прогресса) */
int kal_set_eval_data(KalContext *ctx, const uint8_t *data, size_t len);

/* --- Обучение --- */

/**
 * Выполнить N тиков обучения
 *
 * Каждый тик:
 *   1. Выбирает данные (из источников или генерирует)
 *   2. Подаёт батч в мировую модель
 *   3. Обновляет веса
 *   4. Периодически: eval + checkpoint
 *
 * @param ctx     Контекст
 * @param ticks   Количество тиков
 * @return 0 при успехе, <0 при ошибке
 */
int kal_train(KalContext *ctx, uint64_t ticks);

/**
 * Один тик обучения (для пошагового контроля)
 *
 * @param ctx   Контекст
 * @return loss текущего тика
 */
float kal_train_tick(KalContext *ctx);

/* --- Режимы --- */

/** Установить режим обучения */
void kal_set_mode(KalContext *ctx, KalLearningMode mode);

/** Установить скорость обучения */
void kal_set_learning_rate(KalContext *ctx, float lr);

/** Установить интервал чекпоинтов */
void kal_set_checkpoint_interval(KalContext *ctx, uint64_t interval);

/* --- Чекпоинты --- */

/** Создать чекпоинт текущего состояния */
int kal_checkpoint(KalContext *ctx);

/** Откатиться к лучшему чекпоинту */
int kal_rollback_to_best(KalContext *ctx);

/* --- Метрики --- */

/** Получить метрики */
void kal_get_metrics(const KalContext *ctx, KalMetrics *metrics);

/** Сбросить метрики */
void kal_reset_metrics(KalContext *ctx);

/** Получить текущий eval loss */
double kal_eval(KalContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AUTO_LEARN_H */

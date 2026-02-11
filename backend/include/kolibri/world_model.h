/*
 * world_model.h
 *
 * Мировая модель (World Model) для Kolibri AGI
 *
 * Реализует:
 *   - Предсказательную модель: по контексту предсказывает следующее наблюдение
 *   - Онлайн-обучение: обновление весов после каждого наблюдения
 *   - Байесовское «удивление» как метрика нового знания
 *   - Внутреннее представление «мира» через сжатие потока данных
 *
 * Ключевая идея: сжатие ≡ предсказание ≡ понимание
 *   Чем лучше модель предсказывает, тем лучше она «понимает» мир.
 *   Лосс (кросс-энтропия) = количество бит «удивления».
 *
 * Интеграция:
 *   - Использует KatModel (attention.h) как бэкбон
 *   - Хранит историю наблюдений в скользящем окне
 *   - Онлайн SGD после каждого токена
 *   - Фрактальная память для долгосрочных шаблонов
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_WORLD_MODEL_H
#define KOLIBRI_WORLD_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== КОНФИГУРАЦИЯ ========== */

#define KWM_CONTEXT_SIZE   256    /* Размер скользящего контекстного окна   */
#define KWM_HISTORY_SIZE  4096    /* Размер буфера истории                  */
#define KWM_MAX_CONCEPTS   128    /* Максимум извлечённых концептов          */
#define KWM_CONCEPT_DIM     64    /* Размерность эмбеддинга концепта        */

/* ========== ВНУТРЕННЕЕ СОСТОЯНИЕ МИРА ========== */

/* Извлечённый концепт — атомарная единица понимания */
typedef struct {
    float embedding[KWM_CONCEPT_DIM];    /* Плотный вектор                  */
    char  label[128];                    /* Текстовая метка (UTF-8)         */
    float salience;                      /* Важность (0.0 — 1.0)           */
    float surprise;                      /* Удивление при первом появлении  */
    uint64_t first_seen;                 /* Тик первого наблюдения          */
    uint64_t last_seen;                  /* Тик последнего наблюдения       */
    uint32_t frequency;                  /* Частота появления               */
} KwmConcept;

/* Статистика модели мира */
typedef struct {
    double total_loss;          /* Накопленный loss (bits)                */
    double avg_loss;            /* Средний loss за окно                   */
    double min_loss;            /* Минимальный loss за последнюю эпоху    */
    double perplexity;          /* Перплексия (2^avg_loss)                */
    uint64_t total_tokens;      /* Всего обработано токенов               */
    uint64_t learning_steps;    /* Шагов обучения                        */
    double surprise_integral;   /* Интеграл удивления (мера неизведанного)*/
    double compression_ratio;   /* Средний коэффициент сжатия             */
    size_t num_concepts;        /* Активных концептов                     */
} KwmStats;

/* Состояние предсказания */
typedef struct {
    float probs[256];           /* Распределение вероятностей (байт-уровень) */
    uint8_t predicted_token;    /* Наиболее вероятный следующий токен      */
    float confidence;           /* Уверенность в предсказании             */
    float surprise;             /* Значение удивления (bits)              */
} KwmPrediction;

/* ========== ГЛАВНАЯ СТРУКТУРА ========== */

/* Предварительная декларация: KatModel из attention.h */
struct KatModel;
struct KatWorkspace;

typedef struct {
    /* Нейронный бэкбон (Transformer) */
    struct KatModel     *backbone;       /* Модель внимания                 */
    struct KatWorkspace *workspace;      /* Рабочий буфер forward pass      */

    /* Скользящее окно контекста */
    uint8_t  context[KWM_CONTEXT_SIZE];  /* Текущий контекст (байты)        */
    size_t   context_len;                /* Текущая длина                   */
    size_t   context_pos;                /* Позиция для записи (кольцевой)  */

    /* Полная история наблюдений */
    uint8_t  history[KWM_HISTORY_SIZE];  /* Кольцевой буфер истории         */
    size_t   history_len;                /* Фактическая длина               */
    size_t   history_pos;                /* Позиция записи                  */

    /* Извлечённые концепты */
    KwmConcept concepts[KWM_MAX_CONCEPTS];
    size_t     concept_count;

    /* Параметры обучения */
    float   learning_rate;       /* Скорость обучения (0.001 по умолчанию) */
    float   surprise_threshold;  /* Порог удивления для создания концепта  */
    int     auto_learn;          /* Флаг автоматического обучения           */

    /* Статистика */
    KwmStats stats;
    uint64_t tick;                /* Глобальный счётчик тиков               */
    uint64_t seed;                /* PRNG состояние                         */
} KwmContext;

/* ========== API ========== */

/* --- Жизненный цикл --- */

/** Создать мировую модель */
KwmContext* kwm_create(uint64_t seed);

/** Уничтожить мировую модель */
void kwm_destroy(KwmContext *ctx);

/** Сбросить состояние (сохраняя веса) */
void kwm_reset(KwmContext *ctx);

/* --- Наблюдение и предсказание --- */

/**
 * Наблюдение одного байта — ЯДРО мировой модели
 *
 * 1. Предсказание: P(byte | context)
 * 2. Измерение удивления: -log2(P(actual_byte))
 * 3. Обучение (если auto_learn): обновление весов
 * 4. Обновление контекста
 *
 * @param ctx   Контекст мировой модели
 * @param byte  Наблюдённый байт
 * @param pred  Указатель на результат предсказания (может быть NULL)
 * @return Удивление (bits) или <0 при ошибке
 */
float kwm_observe(KwmContext *ctx, uint8_t byte, KwmPrediction *pred);

/**
 * Наблюдение блока данных
 *
 * @param ctx   Контекст
 * @param data  Данные
 * @param len   Длина
 * @return Средний loss (bits/byte, ≈ коэффициент сжатия)
 */
float kwm_observe_block(KwmContext *ctx, const uint8_t *data, size_t len);

/**
 * Предсказание следующего байта (без обучения)
 *
 * @param ctx   Контекст
 * @param pred  Результат предсказания
 * @return 0 при успехе
 */
int kwm_predict(KwmContext *ctx, KwmPrediction *pred);

/**
 * Генерация последовательности (авторегрессия)
 *
 * @param ctx         Контекст
 * @param output      Буфер для выхода
 * @param max_len     Максимальная длина
 * @param temperature Температура (0 = greedy)
 * @return Количество сгенерированных байт
 */
size_t kwm_generate(KwmContext *ctx, uint8_t *output, size_t max_len,
                    float temperature);

/* --- Концепты и понимание --- */

/**
 * Извлечь эмбеддинг текста через мировую модель
 *
 * @param ctx   Контекст
 * @param text  Текст (UTF-8)
 * @param len   Длина
 * @param out   Выходной вектор [KWM_CONCEPT_DIM]
 * @return 0 при успехе
 */
int kwm_embed_text(KwmContext *ctx, const char *text, size_t len,
                   float *out);

/**
 * Семантическое сходство двух текстов
 *
 * @param ctx   Контекст
 * @param a     Первый текст
 * @param b     Второй текст
 * @return Косинусное сходство [-1, 1]
 */
float kwm_similarity(KwmContext *ctx, const char *a, const char *b);

/**
 * Извлечь ключевые концепты из текста
 *
 * @param ctx       Контекст
 * @param text      Текст
 * @param len       Длина
 * @param concepts  Буфер для концептов
 * @param max_concepts  Максимум концептов
 * @return Количество извлечённых концептов
 */
size_t kwm_extract_concepts(KwmContext *ctx, const char *text, size_t len,
                            KwmConcept *concepts, size_t max_concepts);

/* --- Обучение --- */

/** Включить/выключить автообучение */
void kwm_set_auto_learn(KwmContext *ctx, int enabled);

/** Установить скорость обучения */
void kwm_set_learning_rate(KwmContext *ctx, float lr);

/** Принудительный шаг обучения на текущем контексте */
float kwm_learn_step(KwmContext *ctx);

/* --- Статистика --- */

/** Получить статистику модели */
void kwm_get_stats(const KwmContext *ctx, KwmStats *stats);

/** Сбросить статистику */
void kwm_reset_stats(KwmContext *ctx);

/* --- Сериализация --- */

/** Сериализация состояния */
size_t kwm_serialize(const KwmContext *ctx, uint8_t *buf, size_t buf_size);

/** Десериализация состояния */
int kwm_deserialize(KwmContext *ctx, const uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_WORLD_MODEL_H */

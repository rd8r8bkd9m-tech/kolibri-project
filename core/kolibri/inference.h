/*
 * inference.h
 *
 * Модуль инференса — центральный pipeline вывода Kolibri AI
 *
 * Объединяет: genome → formula → logical_memory → knowledge → generation
 * в единый конвейер от запроса до ответа.
 *
 * Архитектура:
 *   1. Приём запроса (query)
 *   2. Числовое кодирование через formula pool
 *   3. Поиск релевантных знаний (knowledge retrieval)
 *   4. Логическое рассуждение через meta-формулы
 *   5. Генерация ответа через text_generation
 *   6. Запись в genome (аудит-трейл)
 */

#ifndef KOLIBRI_INFERENCE_H
#define KOLIBRI_INFERENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== КОНФИГУРАЦИЯ ========== */

#define KOLIBRI_INF_MAX_QUERY      4096
#define KOLIBRI_INF_MAX_RESPONSE   8192
#define KOLIBRI_INF_MAX_CONTEXT    16
#define KOLIBRI_INF_MAX_STEPS      64
#define KOLIBRI_INF_DIGIT_VOTERS   10

/* ========== ТИПЫ ========== */

/* Стратегия инференса */
typedef enum {
    KOLIBRI_INF_DIRECT,       /* Прямой поиск в knowledge base */
    KOLIBRI_INF_FORMULA,      /* Через формульный вывод */
    KOLIBRI_INF_LOGICAL,      /* Через логическое рассуждение (meta-формулы) */
    KOLIBRI_INF_CHAIN,        /* Chain-of-thought: многошаговый */
    KOLIBRI_INF_HYBRID        /* Комбинация всех методов */
} KolibriInferenceStrategy;

/* Шаг рассуждения (chain-of-thought) */
typedef struct {
    char description[256];     /* Описание шага */
    char result[512];          /* Результат шага */
    double confidence;         /* Уверенность (0.0–1.0) */
    double duration_ms;        /* Время выполнения (мс) */
} KolibriInferenceStep;

/* Сводка числового голосования 0..9 */
typedef struct {
    double channels[KOLIBRI_INF_DIGIT_VOTERS]; /* Сырые баллы по каналам 0..9 */
    uint8_t winner_digit;                      /* Канал-победитель */
    double winner_score;                       /* Балл победителя */
    double runner_up_score;                    /* Балл второго канала */
    double consensus;                          /* Степень консенсуса 0.0–1.0 */
} KolibriNumericVoteSummary;

/* Канонический морфо-семантический профиль запроса */
typedef struct {
    char query_kind[32];         /* what_is | explain | tell | knowledge | studies | ... */
    char canonical_topic[128];   /* Каноническая тема */
    char definition_entity[128]; /* Явно выделенная сущность */
    size_t topic_token_count;    /* Число предметных токенов */
} KolibriQuerySemanticSummary;

/* Результат инференса */
typedef struct {
    char response[KOLIBRI_INF_MAX_RESPONSE];  /* Сгенерированный ответ */
    size_t response_length;

    /* Chain-of-thought */
    KolibriInferenceStep steps[KOLIBRI_INF_MAX_STEPS];
    size_t step_count;

    /* Метрики */
    double total_confidence;    /* Общая уверенность (0.0–1.0) */
    double total_duration_ms;   /* Общее время (мс) */
    size_t knowledge_hits;      /* Найдено релевантных документов */
    size_t formulas_applied;    /* Применено формул */
    size_t logic_rules_fired;   /* Сработало логических правил */
    KolibriNumericVoteSummary numeric_vote; /* Голосование цифр 0..9 */
    uint8_t digit_winner;      /* Победившая цифра консенсуса */
    KolibriQuerySemanticSummary query_semantics; /* Морфология/семантика запроса */

    /* Источники */
    char sources[KOLIBRI_INF_MAX_CONTEXT][256];
    size_t source_count;
} KolibriInferenceResult;

/* Контекст инференса — главная структура */
typedef struct {
    /* Стратегия по умолчанию */
    KolibriInferenceStrategy strategy;
    double temperature;        /* Температура генерации (0.0–2.0) */
    size_t max_steps;          /* Максимум шагов рассуждения */
    size_t knowledge_limit;    /* Лимит knowledge retrieval */

    /* Статистика */
    uint64_t total_queries;
    uint64_t total_tokens_in;
    uint64_t total_tokens_out;
    double avg_confidence;
    double avg_duration_ms;
} KolibriInferenceContext;

/* ========== API ========== */

/* Создать контекст инференса */
KolibriInferenceContext* kolibri_inference_create(void);

/* Уничтожить контекст */
void kolibri_inference_destroy(KolibriInferenceContext *ctx);

/* Настроить стратегию */
int kolibri_inference_set_strategy(
    KolibriInferenceContext *ctx,
    KolibriInferenceStrategy strategy
);

/* Настроить температуру */
int kolibri_inference_set_temperature(
    KolibriInferenceContext *ctx,
    double temperature
);

/**
 * ГЛАВНАЯ ФУНКЦИЯ: выполнить инференс
 *
 * @param ctx     Контекст инференса
 * @param query   Входной запрос (UTF-8)
 * @param result  Указатель на результат (заполняется)
 * @return 0 при успехе, -1 при ошибке
 */
int kolibri_inference_run(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceResult *result
);

/**
 * Выполнить один шаг chain-of-thought
 *
 * @param ctx     Контекст
 * @param query   Текущий промежуточный запрос
 * @param step    Указатель на шаг (заполняется)
 * @return 0 при успехе
 */
int kolibri_inference_step(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceStep *step
);

/* Получить статистику */
int kolibri_inference_get_stats(
    const KolibriInferenceContext *ctx,
    uint64_t *total_queries,
    double *avg_confidence,
    double *avg_duration
);

/* Сбросить статистику */
void kolibri_inference_reset_stats(KolibriInferenceContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_INFERENCE_H */

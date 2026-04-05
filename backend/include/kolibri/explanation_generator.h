/*
 * explanation_generator.h
 *
 * Генератор пошаговых объяснений для Kolibri
 *
 * Каждый ответ Kolibri включает:
 *   1. Прямой ответ
 *   2. Формулу которая его вывела
 *   3. Цепочку рассуждений (шаг за шагом)
 *   4. Источники знаний (provenance из генома)
 *   5. Уровень уверенности и почему
 *
 * Пример:
 *   Вопрос: "Какова площадь круга радиусом 5?"
 *   
 *   Ответ:
 *     Площадь круга = π × r²
 *     Формула: f(r) = π × r² [из domain:math, formula_id:0x4A2F]
 *     Шаги:
 *       1. r = 5
 *       2. r² = 25
 *       3. π ≈ 3.14159...
 *       4. Площадь = 3.14159... × 25 = 78.539...
 *     Ответ: 78.54 кв. единиц
 *     Уверенность: 100% (точная формула, точные вычисления)
 *     Источник: Математика, геометрия, изучено 2025-03-15
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_EXPLANATION_GENERATOR_H
#define KOLIBRI_EXPLANATION_GENERATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество шагов объяснения */
#define KEG_MAX_STEPS 32

/** Максимальная длина шага */
#define KEG_MAX_STEP_LEN 512

/** Максимальное количество источников */
#define KEG_MAX_SOURCES 8

/** Максимальная длина источника */
#define KEG_MAX_SOURCE_LEN 256

/** Максимальная длина объяснения */
#define KEG_MAX_EXPLANATION_LEN 4096

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип шага рассуждения */
typedef enum {
    KEG_STEP_FORMULA = 0,      /* Применение формулы */
    KEG_STEP_LOGIC = 1,        /* Логический вывод */
    KEG_STEP_ARITHMETIC = 2,   /* Арифметическое вычисление */
    KEG_STEP_ANALOGY = 3,      /* Аналогия */
    KEG_STEP_DEFINITION = 4,   /* Определение */
    KEG_STEP_EXAMPLE = 5,      /* Пример */
    KEG_STEP_VERIFICATION = 6, /* Проверка */
    KEG_STEP_COUNT
} KolibriStepType;

/** Шаг объяснения */
typedef struct {
    int step_num;                      /* Номер шага */
    KolibriStepType step_type;         /* Тип шага */
    char title[128];                   /* Заголовок шага */
    char description[KEG_MAX_STEP_LEN]; /* Описание */
    char expression[256];              /* Выражение/формула */
    double result;                     /* Результат шага */
    double confidence;                 /* Уверенность в шаге */
} KolibriExplanationStep;

/** Источник знания (provenance) */
typedef struct {
    char source[KEG_MAX_SOURCE_LEN];   /* Источник (URL, документ, и т.д.) */
    char domain[64];                   /* Домен (math, physics, и т.д.) */
    int64_t formula_id;                /* ID формулы (если есть) */
    double timestamp;                  /* Когда изучено (epoch ms) */
    int verified;                      /* Верифицировано? */
} KolibriKnowledgeSource;

/** Полное объяснение */
typedef struct {
    /* Запрос и ответ */
    char query[1024];
    char direct_answer[1024];
    
    /* Формула */
    char formula_text[512];
    char formula_id[64];
    char formula_domain[64];
    
    /* Цепочка рассуждений */
    KolibriExplanationStep steps[KEG_MAX_STEPS];
    int num_steps;
    
    /* Источники */
    KolibriKnowledgeSource sources[KEG_MAX_SOURCES];
    int num_sources;
    
    /* Уверенность */
    double confidence;              /* 0.0-1.0 */
    char confidence_reason[256];    /* Почему такая уверенность */
    
    /* Полное объяснение (для вывода) */
    char full_explanation[KEG_MAX_EXPLANATION_LEN];
    
    /* Timing */
    double generation_time_ms;
} KolibriExplanation;

/** Конфигурация генератора */
typedef struct {
    int include_formula;            /* Включать формулу? */
    int include_steps;              /* Включать шаги? */
    int include_sources;            /* Включать источники? */
    int include_confidence;         /* Включать уверенность? */
    int max_steps;                  /* Максимум шагов */
    int verbose;
} KolibriEGConfig;

/* ============================================================================
 * API: ГЕНЕРАЦИЯ ОБЪЯСНЕНИЙ
 * ============================================================================ */

/**
 * Инициализировать генератор
 */
int kolibri_eg_init(KolibriEGConfig *config);

/**
 * Сгенерировать объяснение для математической задачи
 *
 * @param query       Запрос (например "реши 2x+3=7")
 * @param answer      Ответ
 * @param config      Конфигурация
 * @param explanation Объяснение (output)
 * @return 0 на успех
 */
int kolibri_eg_generate_math_explanation(
    const char *query,
    const char *answer,
    const KolibriEGConfig *config,
    KolibriExplanation *explanation
);

/**
 * Сгенерировать объяснение для общего вопроса
 *
 * @param query       Запрос
 * @param answer      Ответ
 * @param config      Конфигурация
 * @param explanation Объяснение (output)
 * @return 0 на успех
 */
int kolibri_eg_generate_general_explanation(
    const char *query,
    const char *answer,
    const KolibriEGConfig *config,
    KolibriExplanation *explanation
);

/* ============================================================================
 * API: ФОРМАТИРОВАНИЕ
 * ============================================================================ */

/**
 * Форматировать объяснение в человекочитаемый текст
 *
 * @param explanation  Объяснение
 * @param output       Буфер для вывода
 * @param output_size  Размер буфера
 * @return 0 на успех
 */
int kolibri_eg_format_text(const KolibriExplanation *explanation,
                           char *output, size_t output_size);

/**
 * Форматировать объяснение в Markdown
 */
int kolibri_eg_format_markdown(const KolibriExplanation *explanation,
                               char *output, size_t output_size);

/**
 * Форматировать объяснение в JSON
 */
int kolibri_eg_format_json(const KolibriExplanation *explanation,
                           char *output, size_t output_size);

/* ============================================================================
 * API: ВСПОМОГАТЕЛЬНЫЕ
 * ============================================================================ */

/**
 * Добавить шаг к объяснению
 */
int kolibri_eg_add_step(KolibriExplanation *explanation,
                       KolibriStepType type,
                       const char *title,
                       const char *description,
                       const char *expression,
                       double result,
                       double confidence);

/**
 * Добавить источник к объяснению
 */
int kolibri_eg_add_source(KolibriExplanation *explanation,
                         const char *source,
                         const char *domain,
                         int64_t formula_id,
                         double timestamp,
                         int verified);

/**
 * Распечатать объяснение
 */
void kolibri_eg_print(const KolibriExplanation *explanation);

/**
 * Получить название типа шага
 */
const char* kolibri_eg_step_type_name(KolibriStepType type);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_EXPLANATION_GENERATOR_H */

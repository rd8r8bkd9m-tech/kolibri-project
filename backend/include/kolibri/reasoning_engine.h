/*
 * reasoning_engine.h
 *
 * Движок расширенного рассуждения для Kolibri
 *
 * Типы рассуждений:
 *   1. Deductive (дедуктивное) — от общего к частному
 *      "Все люди смертны. Сократ — человек. → Сократ смертен."
 *
 *   2. Inductive (индуктивное) — от частного к общему
 *      "Лебедь 1 белый, Лебедь 2 белый, ... → Все лебеди белые."
 *
 *   3. Abductive (абдуктивное) — лучшее объяснение
 *      "Трава мокрая. → Возможно, шёл дождь."
 *
 *   4. Analogical (по аналогии)
 *      "Атом как солнечная система: ядро=солнце, электроны=планеты."
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_REASONING_ENGINE_H
#define KOLIBRI_REASONING_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество правил в базе */
#define KRE_MAX_RULES 256

/** Максимальное количество фактов */
#define KRE_MAX_FACTS 512

/** Максимальная длина цепочки рассуждений */
#define KRE_MAX_CHAIN_LENGTH 32

/** Максимальная длина текста правила/факта */
#define KRE_MAX_TEXT_LEN 512

/** Максимальное количество гипотез */
#define KRE_MAX_HYPOTHESES 16

/** Максимальное количество аналогий */
#define KRE_MAX_ANALOGIES 8

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип рассуждения */
typedef enum {
    KRE_REASONING_DEDUCTIVE = 0,    /* Дедуктивное */
    KRE_REASONING_INDUCTIVE = 1,    /* Индуктивное */
    KRE_REASONING_ABDUCTIVE = 2,    /* Абдуктивное */
    KRE_REASONING_ANALOGICAL = 3,   /* По аналогии */
    KRE_REASONING_COUNTERFACTUAL = 4, /* Counterfactual "что если" */
    KRE_REASONING_COUNT
} KolibriReasoningType;

/** Логический оператор */
typedef enum {
    KRE_OP_AND = 0,
    KRE_OP_OR = 1,
    KRE_OP_NOT = 2,
    KRE_OP_IMPLIES = 3,
    KRE_OP_EQUIV = 4,
    KRE_OP_COUNT
} KolibriLogicalOp;

/** Факт */
typedef struct {
    char text[KRE_MAX_TEXT_LEN];    /* Текст факта */
    double confidence;              /* Уверенность (0.0-1.0) */
    int verified;                   /* Верифицирован? */
    char source[256];               /* Источник */
    int64_t timestamp;              /* Когда добавлен */
} KolibriFact;

/** Правило вывода */
typedef struct {
    char premise[KRE_MAX_TEXT_LEN];   /* Посылка */
    char conclusion[KRE_MAX_TEXT_LEN]; /* Заключение */
    KolibriLogicalOp op;              /* Логический оператор */
    double strength;                  /* Сила правила (0.0-1.0) */
    char domain[64];                  /* Домен */
    int active;                       /* Активно? */
} KolibriRule;

/** Шаг рассуждения */
typedef struct {
    int step_num;
    KolibriReasoningType type;
    char description[KRE_MAX_TEXT_LEN];
    char premise[KRE_MAX_TEXT_LEN];
    char conclusion[KRE_MAX_TEXT_LEN];
    double confidence;
    double duration_ms;
} KolibriReasoningStep;

/** Гипотеза (для абдуктивного рассуждения) */
typedef struct {
    char hypothesis[KRE_MAX_TEXT_LEN];
    double probability;           /* Вероятность */
    double explanatory_power;     /* Сила объяснения */
    char evidence[256];           /* Подтверждающие свидетельства */
    char counter_evidence[256];   /* Опровергающие свидетельства */
} KolibriHypothesis;

/** Аналогия */
typedef struct {
    char source_domain[KRE_MAX_TEXT_LEN];  /* Источник аналогии */
    char target_domain[KRE_MAX_TEXT_LEN];  /* Цель аналогии */
    char mapping[KRE_MAX_TEXT_LEN];        /* Отображение */
    double similarity;                     /*Similarity (0.0-1.0) */
    char strength_desc[256];               /* Описание силы аналогии */
} KolibriAnalogy;

/** Цепочка рассуждений */
typedef struct {
    KolibriReasoningStep steps[KRE_MAX_CHAIN_LENGTH];
    int num_steps;
    double overall_confidence;
    char final_conclusion[KRE_MAX_TEXT_LEN];
} KolibriReasoningChain;

/** Результат рассуждения */
typedef struct {
    char query[KRE_MAX_TEXT_LEN];
    char answer[KRE_MAX_TEXT_LEN];
    
    /* Цепочка рассуждений */
    KolibriReasoningChain chain;
    
    /* Тип рассуждения */
    KolibriReasoningType primary_type;
    
    /* Для абдуктивного: гипотезы */
    KolibriHypothesis hypotheses[KRE_MAX_HYPOTHESES];
    int num_hypotheses;
    int best_hypothesis_idx;
    
    /* Для аналогического: аналогии */
    KolibriAnalogy analogies[KRE_MAX_ANALOGIES];
    int num_analogies;
    
    /* Для counterfactual */
    char counterfactual_premise[KRE_MAX_TEXT_LEN];
    char counterfactual_outcome[KRE_MAX_TEXT_LEN];
    
    /* Confidence */
    double confidence;
    char confidence_reason[256];
    
    /* Timing */
    double reasoning_time_ms;
} KolibriReasoningResult;

/** Конфигурация движка */
typedef struct {
    int enable_deductive;
    int enable_inductive;
    int enable_abductive;
    int enable_analogical;
    int enable_counterfactual;
    
    double min_confidence_threshold;  /* Минимальная уверенность */
    int max_chain_length;             /* Максимум шагов */
    int max_hypotheses;               /* Максимум гипотез */
    
    int verbose;
} KolibriREConfig;

/* Callback для прогресса */
typedef void (*KolibriREProgressCallback)(
    int step, KolibriReasoningType type, double confidence, void *user_data
);

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать движок рассуждений
 */
int kolibri_re_init(KolibriREConfig *config);

/**
 * Добавить факт в базу знаний
 */
int kolibri_re_add_fact(KolibriREConfig *config,
                       const char *text,
                       double confidence,
                       const char *source);

/**
 * Добавить правило вывода
 */
int kolibri_re_add_rule(KolibriREConfig *config,
                       const char *premise,
                       const char *conclusion,
                       KolibriLogicalOp op,
                       double strength,
                       const char *domain);

/* ============================================================================
 * API: РАССУЖДЕНИЯ
 * ============================================================================ */

/**
 * Выполнить дедуктивное рассуждение
 *
 * @param query    Запрос
 * @param config   Конфигурация
 * @param result   Результат (output)
 * @return 0 на успех
 */
int kolibri_re_deductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result);

/**
 * Выполнить индуктивное рассуждение
 */
int kolibri_re_inductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result);

/**
 * Выполнить абдуктивное рассуждение (поиск лучшего объяснения)
 */
int kolibri_re_abductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result);

/**
 * Выполнить рассуждение по аналогии
 */
int kolibri_re_analogical(const char *query,
                         const KolibriREConfig *config,
                         KolibriReasoningResult *result);

/**
 * Выполнить counterfactual рассуждение ("что если...")
 */
int kolibri_re_counterfactual(const char *query,
                             const char *what_if,
                             const KolibriREConfig *config,
                             KolibriReasoningResult *result);

/**
 * Универсальный интерфейс рассуждения (автоматически выбирает тип)
 */
int kolibri_re_reason(const char *query,
                     const KolibriREConfig *config,
                     KolibriReasoningResult *result,
                     KolibriREProgressCallback callback,
                     void *user_data);

/* ============================================================================
 * API: ЛОГИЧЕСКИЕ ЗАДАЧИ
 * ============================================================================ */

/**
 * Решить логическую задачу
 *
 * @param problem  Текст задачи
 * @param config   Конфигурация
 * @param result   Результат (output)
 * @return 0 на успех
 */
int kolibri_re_solve_logic_puzzle(const char *problem,
                                 const KolibriREConfig *config,
                                 KolibriReasoningResult *result);

/* ============================================================================
 * API: ВСПОМОГАТЕЛЬНЫЕ
 * ============================================================================ */

/**
 * Распечатать результат рассуждения
 */
void kolibri_re_print_result(const KolibriReasoningResult *result);

/**
 * Сохранить результат в файл
 */
int kolibri_re_save_result(const KolibriReasoningResult *result,
                          const char *filepath);

/**
 * Получить название типа рассуждения
 */
const char* kolibri_re_type_name(KolibriReasoningType type);

/**
 * Получить описание типа рассуждения
 */
const char* kolibri_re_type_desc(KolibriReasoningType type);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_REASONING_ENGINE_H */

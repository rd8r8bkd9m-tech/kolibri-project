/*
 * intent_classifier.h
 *
 * Классификатор намерений для Kolibri
 * Определяет тип запроса и выбирает стратегию обработки
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_INTENT_CLASSIFIER_H
#define KOLIBRI_INTENT_CLASSIFIER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество intent паттернов */
#define KIC_MAX_PATTERNS 256

/** Максимальная длина текста запроса */
#define KIC_MAX_QUERY_LEN 1024

/** Максимальное количество категорий */
#define KIC_MAX_CATEGORIES 32

/** Минимальная уверенность классификации */
#define KIC_MIN_CONFIDENCE 0.6

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип намерения (intent) */
typedef enum {
    /* Информационные запросы */
    KIC_INTENT_QUERY_FACT = 0,       /* Запрос факта */
    KIC_INTENT_QUERY_DEFINITION = 1, /* Что такое...? */
    KIC_INTENT_QUERY_COMPARISON = 2, /* Сравнение */
    KIC_INTENT_QUERY_CAUSE = 3,      /* Почему? Причина */
    KIC_INTENT_QUERY_PROCESS = 4,    /* Как работает? */
    
    /* Логические задачи */
    KIC_INTENT_LOGIC_PUZZLE = 5,     /* Логическая задача */
    KIC_INTENT_MATH_PROBLEM = 6,     /* Математическая задача */
    KIC_INTENT_DEDUCTION = 7,        /* Дедуктивный вывод */
    
    /* Обучающие запросы */
    KIC_INTENT_EXPLAIN = 8,          /* Объясни... */
    KIC_INTENT_EXAMPLE = 9,          /* Приведи пример */
    KIC_INTENT_ANALOGY = 10,         /* Аналогия */
    KIC_INTENT_COUNTERFACTUAL = 11,  /* Что если... */
    
    /* Команды */
    KIC_INTENT_TEACH = 12,           /* Научи/Запомни */
    KIC_INTENT_CORRECT = 13,         /* Исправление */
    KIC_INTENT_DELETE = 14,          /* Удалить */
    KIC_INTENT_LIST = 15,            /* Список */
    
    /* Разговорные */
    KIC_INTENT_GREETING = 16,        /* Приветствие */
    KIC_INTENT_FAREWELL = 17,        /* Прощание */
    KIC_INTENT_THANKS = 18,          /* Благодарность */
    KIC_INTENT_CHAT = 19,            /* Болтовня */
    
    /* Специальные */
    KIC_INTENT_UNKNOWN = 20
} KolibriIntent;

/** Результат классификации */
typedef struct {
    KolibriIntent primary_intent;      /* Основной intent */
    double confidence;                  /* Уверенность (0.0-1.0) */
    
    /* Топ-N гипотез */
    KolibriIntent top_intents[5];      /* Топ-5 intents */
    double top_confidences[5];         /* Их уверенности */
    int num_hypotheses;                /* Количество гипотез */
    
    /* Метаданные */
    char domain[128];                  /* Домен (math, science, etc.) */
    double query_complexity;           /* Сложность запроса (0.0-1.0) */
    int requires_reasoning;            /* Требуется рассуждение */
    int requires_knowledge;            /* Требуется база знаний */
    
    /* Извлечённые сущности */
    char entities[8][256];             /* Сущности из запроса */
    int num_entities;                  /* Количество сущностей */
    
    /* Рекомендуемый метод обработки */
    char recommended_method[128];      /* "deductive", "formula", "knowledge", etc. */
} KolibriIntentResult;

/** Паттерн для классификации */
typedef struct {
    KolibriIntent intent;              /* Intent */
    const char **keywords;             /* Ключевые слова */
    int num_keywords;                  /* Количество ключевых слов */
    const char **regex_patterns;       /* Regex паттерны (опционально) */
    int num_regex_patterns;            /* Количество regex */
    double base_confidence;            /* Базовая уверенность */
    const char *domain;                /* Домен */
    int requires_reasoning;            /* Требуется рассуждение */
} KolibriIntentPattern;

/** Контекст классификатора */
typedef struct {
    KolibriIntentPattern patterns[KIC_MAX_PATTERNS];
    int num_patterns;
    
    /* Статистика */
    uint64_t total_queries;
    uint64_t classification_times_us;  /* Время классификации (микросекунды) */
} KolibriIntentClassifier;

/* ============================================================================
 * API: ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать классификатор намерений
 */
int kolibri_ic_init(KolibriIntentClassifier *classifier);

/**
 * Освободить ресурсы классификатора
 */
void kolibri_ic_destroy(KolibriIntentClassifier *classifier);

/* ============================================================================
 * API: КЛАССИФИКАЦИЯ
 * ============================================================================ */

/**
 * Классифицировать запрос
 *
 * @param classifier  Контекст классификатора
 * @param query       Текст запроса
 * @param result      Результат (output)
 * @return 0 на успех
 */
int kolibri_ic_classify(KolibriIntentClassifier *classifier,
                        const char *query,
                        KolibriIntentResult *result);

/**
 * Быстрая классификация (только основной intent)
 */
KolibriIntent kolibri_ic_classify_fast(KolibriIntentClassifier *classifier,
                                       const char *query);

/* ============================================================================
 * API: ПАТТЕРНЫ
 * ============================================================================ */

/**
 * Добавить пользовательский паттерн
 */
int kolibri_ic_add_pattern(KolibriIntentClassifier *classifier,
                           KolibriIntent intent,
                           const char **keywords,
                           int num_keywords,
                           double base_confidence,
                           const char *domain);

/* ============================================================================
 * API: ВСПОМОГАТЕЛЬНЫЕ
 * ============================================================================ */

/**
 * Получить название intent
 */
const char* kolibri_ic_intent_name(KolibriIntent intent);

/**
 * Получить описание intent
 */
const char* kolibri_ic_intent_desc(KolibriIntent intent);

/**
 * Распечатать результат классификации
 */
void kolibri_ic_print_result(const KolibriIntentResult *result);

/**
 * Проверить, требует ли запрос рассуждения
 */
int kolibri_ic_requires_reasoning(KolibriIntent intent);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_INTENT_CLASSIFIER_H */

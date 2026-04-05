/*
 * self_verification.h
 *
 * Протокол самопроверки Kolibri
 *
 * Перед выдачей ответа система:
 *   1. Генерирует ответ основным методом
 *   2. Проверяет через альтернативный метод
 *   3. Сравнивает результаты
 *   4. Если расхождение → объясняет неопределённость
 *   5. Если согласие → повышает уверенность
 *
 * Архитектура:
 *   - Мульти-метод проверка (formula, logical, knowledge-based)
 *   - Оценка уверенности (confidence scoring)
 *   - Обнаружение противоречий (contradiction detection)
 *   - Tracking provenance для каждого ответа
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_SELF_VERIFICATION_H
#define KOLIBRI_SELF_VERIFICATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество методов верификации */
#define KSV_MAX_METHODS 8

/** Максимальная длина ответа */
#define KSV_MAX_ANSWER_LEN 1024

/** Максимальное количество шагов проверки */
#define KSV_MAX_CHECK_STEPS 16

/** Максимальное количество противоречий */
#define KSV_MAX_CONTRADICTIONS 8

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Метод верификации */
typedef enum {
    KSV_METHOD_FORMULA = 0,      /* Через формулы */
    KSV_METHOD_LOGICAL = 1,      /* Через логический вывод */
    KSV_METHOD_KNOWLEDGE = 2,    /* Через граф знаний */
    KSV_METHOD_ARITHMETIC = 3,   /* Через точную арифметику */
    KSV_METHOD_ANALOGY = 4,      /* Через аналогии */
    KSV_METHOD_COUNT
} KolibriSVMethod;

/** Результат одного метода проверки */
typedef struct {
    KolibriSVMethod method;       /* Использованный метод */
    char answer[KSV_MAX_ANSWER_LEN];  /* Полученный ответ */
    double confidence;            /* Уверенность метода (0.0-1.0) */
    int success;                  /* Метод успешно сработал? */
    double elapsed_ms;            /* Время выполнения */
    char provenance[256];         /* Источник/происхождение */
} KolibriSVMethodResult;

/** Противоречие между методами */
typedef struct {
    KolibriSVMethod method1;
    KolibriSVMethod method2;
    char answer1[KSV_MAX_ANSWER_LEN];
    char answer2[KSV_MAX_ANSWER_LEN];
    double divergence;            /* Степень расхождения (0.0-1.0) */
    char explanation[256];        /* Объяснение противоречия */
} KolibriSVContradiction;

/** Шаг проверки */
typedef struct {
    int step_num;
    char description[256];
    char detail[512];
    double intermediate_confidence;
} KolibriSVCheckStep;

/** Полный отчёт о самопроверке */
typedef struct {
    /* Основной ответ */
    char primary_answer[KSV_MAX_ANSWER_LEN];
    KolibriSVMethod primary_method;
    double primary_confidence;

    /* Результаты всех методов проверки */
    KolibriSVMethodResult results[KSV_MAX_METHODS];
    int num_methods_used;

    /* Противоречия */
    KolibriSVContradiction contradictions[KSV_MAX_CONTRADICTIONS];
    int num_contradictions;

    /* Итоговая оценка */
    double final_confidence;        /* Финальная уверенность */
    int agreement;                  /* Все методы согласны? (1=yes, 0=no) */
    int verification_passed;        /* Проверка прошла? (1=yes, 0=no, -1=unknown) */

    /* Шаги проверки */
    KolibriSVCheckStep steps[KSV_MAX_CHECK_STEPS];
    int num_steps;

    /* Timing */
    double total_verification_ms;

    /* Рекомендация */
    char recommendation[512];  /* "Выдать ответ", "Предупредить о неопределённости", и т.д. */
} KolibriSVReport;

/** Конфигурация верификации */
typedef struct {
    int enable_formula_check;
    int enable_logical_check;
    int enable_knowledge_check;
    int enable_arithmetic_check;
    
    double agreement_threshold;    /* Порог согласия (0.0-1.0) */
    int min_methods_required;      /* Минимальное количество методов */
    
    int verbose;
} KolibriSVConfig;

/* Callback для прогресса проверки */
typedef void (*KolibriSVProgressCallback)(
    int step, const char *method_name, double confidence, void *user_data
);

/* ============================================================================
 * API: ВЕРИФИКАЦИЯ
 * ============================================================================ */

/**
 * Инициализировать систему самопроверки
 */
int kolibri_sv_init(KolibriSVConfig *config);

/**
 * Выполнить самопроверку ответа
 *
 * @param query           Запрос
 * @param primary_answer  Основной ответ (от primary метода)
 * @param config          Конфигурация
 * @param report          Отчёт (output)
 * @param callback        Callback прогресса (опционально)
 * @param user_data       Данные для callback
 * @return 0 на успех
 */
int kolibri_sv_verify_answer(
    const char *query,
    const char *primary_answer,
    const KolibriSVConfig *config,
    KolibriSVReport *report,
    KolibriSVProgressCallback callback,
    void *user_data
);

/**
 * Проверить согласованность между методами
 *
 * @param report  Отчёт с результатами всех методов
 * @return 1 если согласны, 0 если есть противоречия
 */
int kolibri_sv_check_agreement(const KolibriSVReport *report);

/**
 * Обнаружить противоречия
 *
 * @param report  Отчёт (заполняет contradictions)
 * @return Количество обнаруженных противоречий
 */
int kolibri_sv_detect_contradictions(KolibriSVReport *report);

/**
 * Вычислить финальную уверенность
 *
 * @param report  Отчёт (заполняет final_confidence)
 * @return Финальная уверенность (0.0-1.0)
 */
double kolibri_sv_compute_final_confidence(const KolibriSVReport *report);

/**
 * Сгенерировать рекомендацию
 *
 * @param report  Отчёт (заполняет recommendation)
 */
void kolibri_sv_generate_recommendation(KolibriSVReport *report);

/* ============================================================================
 * API: ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

/**
 * Распечатать отчёт о верификации
 */
void kolibri_sv_print_report(const KolibriSVReport *report);

/**
 * Сохранить отчёт в файл
 */
int kolibri_sv_save_report(const KolibriSVReport *report, const char *filepath);

/**
 * Получить название метода
 */
const char* kolibri_sv_method_name(KolibriSVMethod method);

/**
 * Получить описание метода
 */
const char* kolibri_sv_method_desc(KolibriSVMethod method);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SELF_VERIFICATION_H */

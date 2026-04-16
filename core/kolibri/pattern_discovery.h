/*
 * pattern_discovery.h
 *
 * Обнаружение скрытых паттернов в данных для Kolibri
 *
 * Возможности:
 *   - Обнаружение скрытых паттернов в данных
 *   - Генерация гипотез
 *   - Проверка через эксперименты
 *   - Извлечение формул из сжатых данных
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_PATTERN_DISCOVERY_H
#define KOLIBRI_PATTERN_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальный размер входных данных */
#define KPD_MAX_DATA_SIZE 4096

/** Максимальное количество паттернов */
#define KPD_MAX_PATTERNS 64

/** Максимальное количество гипотез */
#define KPD_MAX_HYPOTHESES 16

/** Максимальная длина описания паттерна */
#define KPD_MAX_PATTERN_DESC 512

/** Максимальная длина формулы */
#define KPD_MAX_FORMULA 256

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Тип паттерна */
typedef enum {
    KPD_PATTERN_LINEAR = 0,      /* Линейный: y = ax + b */
    KPD_PATTERN_QUADRATIC = 1,   /* Квадратичный: y = ax² + bx + c */
    KPD_PATTERN_EXPONENTIAL = 2, /* Экспоненциальный: y = a * e^(bx) */
    KPD_PATTERN_PERIODIC = 3,    /* Периодический: y = a * sin(bx + c) */
    KPD_PATTERN_LOGARITHMIC = 4, /* Логарифмический: y = a * ln(x) + b */
    KPD_PATTERN_STEP = 5,        /* Ступенчатый */
    KPD_PATTERN_RANDOM = 6,      /* Случайный */
    KPD_PATTERN_UNKNOWN = -1
} KolibriPatternType;

/** Обнаруженный паттерн */
typedef struct {
    KolibriPatternType type;
    char description[KPD_MAX_PATTERN_DESC];
    char formula[KPD_MAX_FORMULA];
    double confidence;          /* Уверенность (0.0-1.0) */
    double fit_quality;         /* Качество подгонки (R²) */
    
    /* Параметры */
    double params[8];
    int num_params;
    
    /* Статистика */
    double mean_error;
    double max_error;
    int data_points_matched;
} KolibriPattern;

/** Гипотеза */
typedef struct {
    char hypothesis[KPD_MAX_PATTERN_DESC];
    double probability;
    char supporting_evidence[256];
    char predicted_outcome[256];
    int tested;
    int confirmed;
} KolibriPatternHypothesis;

/** Результат обнаружения паттернов */
typedef struct {
    /* Входные данные */
    double input_data[KPD_MAX_DATA_SIZE];
    int data_size;
    
    /* Обнаруженные паттерны */
    KolibriPattern patterns[KPD_MAX_PATTERNS];
    int num_patterns;
    int best_pattern_idx;
    
    /* Гипотезы */
    KolibriPatternHypothesis hypotheses[KPD_MAX_HYPOTHESES];
    int num_hypotheses;
    
    /* Общая статистика */
    double overall_fit_quality;  /* R² для лучшего паттерна */
    double compression_ratio;    /* Насколько хорошо сжимается */
    
    /* Timing */
    double discovery_time_ms;
} KolibriPatternDiscoveryResult;

/** Конфигурация */
typedef struct {
    int detect_linear;
    int detect_quadratic;
    int detect_exponential;
    int detect_periodic;
    int detect_logarithmic;
    
    double min_fit_threshold;    /* Минимальное R² */
    int max_patterns;            /* Максимум паттернов */
    
    int verbose;
} KolibriPDConfig;

/* ============================================================================
 * API: ОБНАРУЖЕНИЕ ПАТТЕРНОВ
 * ============================================================================ */

/**
 * Инициализировать обнаружитель паттернов
 */
int kolibri_pd_init(KolibriPDConfig *config);

/**
 * Обнаружить паттерны в данных
 *
 * @param data      Входные данные
 * @param data_size Размер данных
 * @param config    Конфигурация
 * @param result    Результат (output)
 * @return 0 на успех
 */
int kolibri_pd_discover(const double *data, int data_size,
                       const KolibriPDConfig *config,
                       KolibriPatternDiscoveryResult *result);

/**
 * Проверить гипотезу экспериментом
 *
 * @param result        Результат обнаружения
 * @param hypothesis_idx  Индекс гипотезы
 * @param test_data     Тестовые данные
 * @param test_size     Размер тестовых данных
 * @return 1 если подтверждена, 0 если опровергнута
 */
int kolibri_pd_test_hypothesis(KolibriPatternDiscoveryResult *result,
                              int hypothesis_idx,
                              const double *test_data,
                              int test_size);

/**
 * Сгенерировать формулу из паттерна
 *
 * @param pattern  Паттерн
 * @param formula  Буфер для формулы
 * @param size     Размер буфера
 * @return 0 на успех
 */
int kolibri_pd_generate_formula(const KolibriPattern *pattern,
                               char *formula, size_t size);

/* ============================================================================
 * API: ВСПОМОГАТЕЛЬНЫЕ
 * ============================================================================ */

/**
 * Распечатать результат
 */
void kolibri_pd_print_result(const KolibriPatternDiscoveryResult *result);

/**
 * Получить название типа паттерна
 */
const char* kolibri_pd_pattern_type_name(KolibriPatternType type);

/**
 * Вычислить R² (коэффициент детерминированности)
 */
double kolibri_pd_compute_r2(const double *actual, const double *predicted, int size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_PATTERN_DISCOVERY_H */

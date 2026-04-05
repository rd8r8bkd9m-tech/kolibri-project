/*
 * pattern_discovery.c
 *
 * Реализация обнаружения паттернов Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/pattern_discovery.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static double pd_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Среднее значение */
static double pd_mean(const double *data, int size) {
    if (!data || size <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < size; i++) sum += data[i];
    return sum / size;
}

/* Линейная регрессия: y = ax + b */
static double pd_fit_linear(const double *data, int size, double *params) {
    if (size < 2) return 0.0;
    
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    for (int i = 0; i < size; i++) {
        double x = (double)i;
        sum_x += x;
        sum_y += data[i];
        sum_xy += x * data[i];
        sum_x2 += x * x;
    }
    
    double denom = size * sum_x2 - sum_x * sum_x;
    if (fabs(denom) < 1e-10) return 0.0;
    
    params[0] = (size * sum_xy - sum_x * sum_y) / denom;  /* a */
    params[1] = (sum_y - params[0] * sum_x) / size;        /* b */
    
    /* Вычисляем R² */
    double mean_y = pd_mean(data, size);
    double ss_tot = 0, ss_res = 0;
    
    for (int i = 0; i < size; i++) {
        double x = (double)i;
        double predicted = params[0] * x + params[1];
        ss_tot += (data[i] - mean_y) * (data[i] - mean_y);
        ss_res += (data[i] - predicted) * (data[i] - predicted);
    }
    
    return (ss_tot < 1e-10) ? 1.0 : 1.0 - ss_res / ss_tot;
}

/* Квадратичная регрессия: y = ax² + bx + c */
static double pd_fit_quadratic(const double *data, int size, double *params) {
    if (size < 3) return 0.0;
    
    /* Упрощённая версия: используем 3 точки для решения системы */
    double x1 = 0, x2 = (double)(size / 2), x3 = (double)(size - 1);
    double y1 = data[0], y2 = data[size / 2], y3 = data[size - 1];
    
    double denom = (x1 - x2) * (x1 - x3) * (x2 - x3);
    if (fabs(denom) < 1e-10) return 0.0;
    
    params[0] = (y1 * (x2 - x3) + y2 * (x3 - x1) + y3 * (x1 - x2)) / denom;  /* a */
    params[1] = (y1 * (x3 * x3 - x2 * x2) + y2 * (x1 * x1 - x3 * x3) + y3 * (x2 * x2 - x1 * x1)) / denom;  /* b */
    params[2] = (y1 * x2 * x3 * (x2 - x3) + y2 * x3 * x1 * (x3 - x1) + y3 * x1 * x2 * (x1 - x2)) / (denom * (x1 - x2) * (x1 - x3));  /* c */
    
    /* R² */
    double mean_y = pd_mean(data, size);
    double ss_tot = 0, ss_res = 0;
    
    for (int i = 0; i < size; i++) {
        double x = (double)i;
        double predicted = params[0] * x * x + params[1] * x + params[2];
        ss_tot += (data[i] - mean_y) * (data[i] - mean_y);
        ss_res += (data[i] - predicted) * (data[i] - predicted);
    }
    
    return (ss_tot < 1e-10) ? 1.0 : 1.0 - ss_res / ss_tot;
}

/* Проверка на периодичность */
static double pd_detect_periodic(const double *data, int size, double *params) {
    if (size < 4) return 0.0;
    
    /* Ищем повторяющиеся паттерны через autocorrelation */
    double mean = pd_mean(data, size);
    double variance = 0;
    for (int i = 0; i < size; i++) {
        variance += (data[i] - mean) * (data[i] - mean);
    }
    variance /= size;
    
    if (variance < 1e-10) return 0.0;
    
    /* Autocorrelation для разных лагов */
    double max_corr = 0;
    int best_lag = 0;
    
    for (int lag = 2; lag < size / 2; lag++) {
        double corr = 0;
        int count = 0;
        for (int i = 0; i < size - lag; i++) {
            corr += (data[i] - mean) * (data[i + lag] - mean);
            count++;
        }
        corr /= (count * variance);
        
        if (corr > max_corr) {
            max_corr = corr;
            best_lag = lag;
        }
    }
    
    if (max_corr > 0.5) {
        params[0] = max_corr;  /* Amplitude */
        params[1] = (double)best_lag;  /* Period */
        return max_corr;
    }
    
    return 0.0;
}

/* ============================================================================
 * API РЕАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_pd_init(KolibriPDConfig *config) {
    if (!config) return -1;
    
    if (!config->detect_linear) config->detect_linear = 1;
    if (!config->detect_quadratic) config->detect_quadratic = 1;
    if (!config->detect_exponential) config->detect_exponential = 1;
    if (!config->detect_periodic) config->detect_periodic = 1;
    if (!config->detect_logarithmic) config->detect_logarithmic = 1;
    
    if (config->min_fit_threshold <= 0.0) config->min_fit_threshold = 0.7;
    if (!config->max_patterns) config->max_patterns = KPD_MAX_PATTERNS;
    
    return 0;
}

int kolibri_pd_discover(const double *data, int data_size,
                       const KolibriPDConfig *config,
                       KolibriPatternDiscoveryResult *result) {
    if (!data || !result || data_size < 2) return -1;
    
    double start = pd_time_ms();
    memset(result, 0, sizeof(KolibriPatternDiscoveryResult));
    
    /* Копируем данные */
    result->data_size = data_size;
    for (int i = 0; i < data_size && i < KPD_MAX_DATA_SIZE; i++) {
        result->input_data[i] = data[i];
    }
    
    int num_patterns = 0;
    double best_r2 = -1.0;
    int best_idx = -1;
    
    /* 1. Проверка линейности */
    if (config->detect_linear && num_patterns < KPD_MAX_PATTERNS) {
        KolibriPattern *p = &result->patterns[num_patterns];
        double params[8] = {0};
        double r2 = pd_fit_linear(data, data_size, params);
        
        if (r2 >= config->min_fit_threshold) {
            p->type = KPD_PATTERN_LINEAR;
            snprintf(p->formula, KPD_MAX_FORMULA, "y = %.3fx + %.3f", params[0], params[1]);
            snprintf(p->description, KPD_MAX_PATTERN_DESC,
                    "Линейный паттерн: y = ax + b");
            p->confidence = r2;
            p->fit_quality = r2;
            p->params[0] = params[0];
            p->params[1] = params[1];
            p->num_params = 2;
            p->data_points_matched = data_size;
            
            if (r2 > best_r2) {
                best_r2 = r2;
                best_idx = num_patterns;
            }
            
            num_patterns++;
        }
    }
    
    /* 2. Проверка квадратичности */
    if (config->detect_quadratic && num_patterns < KPD_MAX_PATTERNS) {
        KolibriPattern *p = &result->patterns[num_patterns];
        double params[8] = {0};
        double r2 = pd_fit_quadratic(data, data_size, params);
        
        if (r2 >= config->min_fit_threshold) {
            p->type = KPD_PATTERN_QUADRATIC;
            snprintf(p->formula, KPD_MAX_FORMULA, "y = %.3fx² + %.3fx + %.3f",
                    params[0], params[1], params[2]);
            snprintf(p->description, KPD_MAX_PATTERN_DESC,
                    "Квадратичный паттерн: y = ax² + bx + c");
            p->confidence = r2;
            p->fit_quality = r2;
            p->params[0] = params[0];
            p->params[1] = params[1];
            p->params[2] = params[2];
            p->num_params = 3;
            p->data_points_matched = data_size;
            
            if (r2 > best_r2) {
                best_r2 = r2;
                best_idx = num_patterns;
            }
            
            num_patterns++;
        }
    }
    
    /* 3. Проверка периодичности */
    if (config->detect_periodic && num_patterns < KPD_MAX_PATTERNS) {
        KolibriPattern *p = &result->patterns[num_patterns];
        double params[8] = {0};
        double strength = pd_detect_periodic(data, data_size, params);
        
        if (strength >= config->min_fit_threshold) {
            p->type = KPD_PATTERN_PERIODIC;
            snprintf(p->formula, KPD_MAX_FORMULA, "y = a * sin(2πx / %.1f)", params[1]);
            snprintf(p->description, KPD_MAX_PATTERN_DESC,
                    "Периодический паттерн: period = %.1f", params[1]);
            p->confidence = strength;
            p->fit_quality = strength;
            p->params[0] = params[0];
            p->params[1] = params[1];
            p->num_params = 2;
            p->data_points_matched = data_size;
            
            if (strength > best_r2) {
                best_r2 = strength;
                best_idx = num_patterns;
            }
            
            num_patterns++;
        }
    }
    
    result->num_patterns = num_patterns;
    result->best_pattern_idx = best_idx;
    result->overall_fit_quality = best_r2;
    
    /* Compression ratio: сколько параметров vs точек данных */
    if (num_patterns > 0 && best_idx >= 0) {
        int num_params = result->patterns[best_idx].num_params;
        result->compression_ratio = (double)data_size / num_params;
    }
    
    /* Генерация гипотез */
    if (num_patterns > 0) {
        result->num_hypotheses = 1;
        KolibriPatternHypothesis *h = &result->hypotheses[0];
        snprintf(h->hypothesis, KPD_MAX_PATTERN_DESC,
                "Данные следуют паттерну: %s",
                result->patterns[best_idx].formula);
        h->probability = best_r2;
        snprintf(h->supporting_evidence, 256,
                "R² = %.3f, %d точек данных", best_r2, data_size);
        snprintf(h->predicted_outcome, 256,
                "Следующее значение: %.3f",
                result->patterns[best_idx].params[0] * data_size +
                result->patterns[best_idx].params[1]);
    }
    
    result->discovery_time_ms = pd_time_ms() - start;
    
    return 0;
}

int kolibri_pd_test_hypothesis(KolibriPatternDiscoveryResult *result,
                              int hypothesis_idx,
                              const double *test_data,
                              int test_size) {
    if (!result || hypothesis_idx < 0 || hypothesis_idx >= result->num_hypotheses) return 0;
    if (result->best_pattern_idx < 0) return 0;
    
    KolibriPattern *pattern = &result->patterns[result->best_pattern_idx];
    
    /* Предсказываем значения */
    double predicted[KPD_MAX_DATA_SIZE];
    for (int i = 0; i < test_size && i < KPD_MAX_DATA_SIZE; i++) {
        double x = (double)i;
        switch (pattern->type) {
            case KPD_PATTERN_LINEAR:
                predicted[i] = pattern->params[0] * x + pattern->params[1];
                break;
            case KPD_PATTERN_QUADRATIC:
                predicted[i] = pattern->params[0] * x * x + pattern->params[1] * x + pattern->params[2];
                break;
            default:
                predicted[i] = 0.0;
        }
    }
    
    /* Вычисляем R² на тестовых данных */
    double r2 = kolibri_pd_compute_r2(test_data, predicted, test_size);
    
    result->hypotheses[hypothesis_idx].tested = 1;
    result->hypotheses[hypothesis_idx].confirmed = (r2 > 0.7);
    
    return result->hypotheses[hypothesis_idx].confirmed;
}

int kolibri_pd_generate_formula(const KolibriPattern *pattern,
                               char *formula, size_t size) {
    if (!pattern || !formula) return -1;
    
    snprintf(formula, size, "%s", pattern->formula);
    return 0;
}

double kolibri_pd_compute_r2(const double *actual, const double *predicted, int size) {
    if (!actual || !predicted || size <= 0) return 0.0;
    
    double mean = pd_mean(actual, size);
    double ss_tot = 0, ss_res = 0;
    
    for (int i = 0; i < size; i++) {
        ss_tot += (actual[i] - mean) * (actual[i] - mean);
        ss_res += (actual[i] - predicted[i]) * (actual[i] - predicted[i]);
    }
    
    return (ss_tot < 1e-10) ? 1.0 : 1.0 - ss_res / ss_tot;
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

void kolibri_pd_print_result(const KolibriPatternDiscoveryResult *result) {
    if (!result) return;
    
    printf("\n=== Pattern Discovery Result ===\n");
    printf("Data points: %d\n", result->data_size);
    printf("Patterns found: %d\n", result->num_patterns);
    
    for (int i = 0; i < result->num_patterns; i++) {
        const KolibriPattern *p = &result->patterns[i];
        printf("\nPattern %d [%s]:\n", i + 1, kolibri_pd_pattern_type_name(p->type));
        printf("  Formula: %s\n", p->formula);
        printf("  Confidence: %.3f\n", p->confidence);
        printf("  Fit quality (R²): %.3f\n", p->fit_quality);
        printf("  Data points matched: %d\n", p->data_points_matched);
    }
    
    if (result->num_hypotheses > 0) {
        printf("\nHypotheses:\n");
        for (int i = 0; i < result->num_hypotheses; i++) {
            const KolibriPatternHypothesis *h = &result->hypotheses[i];
            printf("  H%d: %s\n", i + 1, h->hypothesis);
            printf("    Probability: %.3f\n", h->probability);
            printf("    Evidence: %s\n", h->supporting_evidence);
            if (h->tested) {
                printf("    Status: %s\n", h->confirmed ? "CONFIRMED" : "REJECTED");
            }
        }
    }
    
    printf("\nOverall fit quality (R²): %.3f\n", result->overall_fit_quality);
    printf("Compression ratio: %.1fx\n", result->compression_ratio);
    printf("Discovery time: %.1fms\n", result->discovery_time_ms);
    printf("====================================\n\n");
}

const char* kolibri_pd_pattern_type_name(KolibriPatternType type) {
    switch (type) {
        case KPD_PATTERN_LINEAR:      return "Linear";
        case KPD_PATTERN_QUADRATIC:   return "Quadratic";
        case KPD_PATTERN_EXPONENTIAL: return "Exponential";
        case KPD_PATTERN_PERIODIC:    return "Periodic";
        case KPD_PATTERN_LOGARITHMIC: return "Logarithmic";
        case KPD_PATTERN_STEP:        return "Step";
        case KPD_PATTERN_RANDOM:      return "Random";
        default:                      return "Unknown";
    }
}

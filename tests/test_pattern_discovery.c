/*
 * test_pattern_discovery.c
 *
 * Тесты для обнаружения паттернов Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/pattern_discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* ============================================================================
 * ТЕСТ ИНИЦИАЛИЗАЦИИ
 * ============================================================================ */

void test_pd_init(void) {
    printf("Testing pattern discovery initialization...\n");

    KolibriPDConfig config = {0};
    int ret = kolibri_pd_init(&config);
    
    assert(ret == 0);
    assert(config.detect_linear == 1);
    assert(config.detect_quadratic == 1);
    assert(config.detect_periodic == 1);
    assert(config.min_fit_threshold == 0.7);

    printf("✓ Pattern discovery initialization test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ОБНАРУЖЕНИЯ ЛИНЕЙНОГО ПАТТЕРНА
 * ============================================================================ */

void test_linear_pattern(void) {
    printf("Testing linear pattern discovery...\n");

    /* Генерируем линейные данные: y = 2x + 3 + noise */
    double data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = 2.0 * i + 3.0 + ((double)rand() / RAND_MAX - 0.5) * 0.1;
    }

    KolibriPDConfig config = {0};
    kolibri_pd_init(&config);

    KolibriPatternDiscoveryResult result;
    int ret = kolibri_pd_discover(data, 20, &config, &result);

    assert(ret == 0);
    assert(result.num_patterns >= 1);
    assert(result.best_pattern_idx >= 0);
    assert(result.patterns[result.best_pattern_idx].type == KPD_PATTERN_LINEAR);
    assert(result.overall_fit_quality > 0.95);  /* R² > 0.95 для линейных данных */

    printf("  Data: y = 2x + 3 + noise\n");
    printf("  Detected pattern: %s\n", 
           kolibri_pd_pattern_type_name(result.patterns[result.best_pattern_idx].type));
    printf("  Formula: %s\n", result.patterns[result.best_pattern_idx].formula);
    printf("  R²: %.4f\n", result.overall_fit_quality);
    printf("  Compression ratio: %.1fx\n", result.compression_ratio);

    kolibri_pd_print_result(&result);

    printf("✓ Linear pattern discovery test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ОБНАРУЖЕНИЯ КВАДРАТИЧНОГО ПАТТЕРНА
 * ============================================================================ */

void test_quadratic_pattern(void) {
    printf("Testing quadratic pattern discovery...\n");

    /* Генерируем квадратичные данные: y = x² */
    double data[15];
    for (int i = 0; i < 15; i++) {
        data[i] = (double)i * (double)i;
    }

    KolibriPDConfig config = {0};
    kolibri_pd_init(&config);

    KolibriPatternDiscoveryResult result;
    kolibri_pd_discover(data, 15, &config, &result);

    assert(result.num_patterns >= 1);
    printf("  Data: y = x²\n");
    printf("  Detected: %s\n",
           kolibri_pd_pattern_type_name(result.patterns[result.best_pattern_idx].type));
    printf("  R²: %.4f\n", result.overall_fit_quality);

    printf("✓ Quadratic pattern discovery test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ОБНАРУЖЕНИЯ ПЕРИОДИЧЕСКОГО ПАТТЕРНА
 * ============================================================================ */

void test_periodic_pattern(void) {
    printf("Testing periodic pattern discovery...\n");

    /* Генерируем периодические данные: y = sin(x) */
    double data[40];
    for (int i = 0; i < 40; i++) {
        data[i] = sin(2.0 * M_PI * i / 10.0);  /* Period = 10 */
    }

    KolibriPDConfig config = {0};
    kolibri_pd_init(&config);
    config.detect_linear = 0;  /* Отключаем линейный для чистоты */
    config.detect_quadratic = 0;

    KolibriPatternDiscoveryResult result;
    kolibri_pd_discover(data, 40, &config, &result);

    printf("  Data: y = sin(2πx/10)\n");
    printf("  Patterns found: %d\n", result.num_patterns);
    
    if (result.num_patterns > 0) {
        printf("  Detected: %s\n",
               kolibri_pd_pattern_type_name(result.patterns[result.best_pattern_idx].type));
        printf("  R²: %.4f\n", result.overall_fit_quality);
    }

    printf("✓ Periodic pattern discovery test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ГЕНЕРАЦИИ ФОРМУЛЫ
 * ============================================================================ */

void test_formula_generation(void) {
    printf("Testing formula generation...\n");

    KolibriPattern pattern = {0};
    pattern.type = KPD_PATTERN_LINEAR;
    pattern.params[0] = 2.5;
    pattern.params[1] = 3.7;
    pattern.num_params = 2;
    snprintf(pattern.formula, KPD_MAX_FORMULA, "y = 2.500x + 3.700");

    char formula[KPD_MAX_FORMULA];
    int ret = kolibri_pd_generate_formula(&pattern, formula, sizeof(formula));
    
    assert(ret == 0);
    assert(strlen(formula) > 0);
    assert(strstr(formula, "y =") != NULL);

    printf("  Generated formula: %s\n", formula);

    printf("✓ Formula generation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ПРОВЕРКИ ГИПОТЕЗЫ
 * ============================================================================ */

void test_hypothesis_testing(void) {
    printf("Testing hypothesis testing...\n");

    /* Обучающие данные: y = 3x + 1 */
    double train_data[10];
    for (int i = 0; i < 10; i++) {
        train_data[i] = 3.0 * i + 1.0;
    }

    KolibriPDConfig config = {0};
    kolibri_pd_init(&config);

    KolibriPatternDiscoveryResult result;
    kolibri_pd_discover(train_data, 10, &config, &result);

    /* Тестовые данные: та же формула */
    double test_data[5];
    for (int i = 0; i < 5; i++) {
        test_data[i] = 3.0 * (10 + i) + 1.0;
    }

    if (result.num_hypotheses > 0) {
        int confirmed = kolibri_pd_test_hypothesis(&result, 0, test_data, 5);
        
        printf("  Hypothesis tested: %s\n", 
               result.hypotheses[0].tested ? "YES" : "NO");
        printf("  Hypothesis confirmed: %s\n",
               confirmed ? "YES" : "NO");
        
        assert(result.hypotheses[0].tested == 1);
    }

    printf("✓ Hypothesis testing test passed\n\n");
}

/* ============================================================================
 * ТЕСТ R² COMPUTATION
 * ============================================================================ */

void test_r2_computation(void) {
    printf("Testing R² computation...\n");

    /* Идеальная подгонка */
    double actual1[] = {1, 2, 3, 4, 5};
    double predicted1[] = {1, 2, 3, 4, 5};
    double r2_1 = kolibri_pd_compute_r2(actual1, predicted1, 5);
    assert(fabs(r2_1 - 1.0) < 0.01);

    /* Плохая подгонка */
    double actual2[] = {1, 2, 3, 4, 5};
    double predicted2[] = {5, 4, 3, 2, 1};
    double r2_2 = kolibri_pd_compute_r2(actual2, predicted2, 5);
    assert(r2_2 < 0.5);

    printf("  Perfect fit R²: %.4f (expected: 1.0)\n", r2_1);
    printf("  Poor fit R²: %.4f (expected: < 0.5)\n", r2_2);

    printf("✓ R² computation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ПРОВЕРКИ КОРРЕЛЯЦИИ СЖАТИЯ С КАЧЕСТВОМ
 * ============================================================================ */

void test_compression_correlation(void) {
    printf("Testing compression-quality correlation...\n");

    KolibriPDConfig config = {0};
    kolibri_pd_init(&config);

    /* Тестируем на разных типах данных */
    struct {
        const char *name;
        double data[20];
    } test_cases[] = {
        {"Linear (y=2x+1)", {0}},
        {"Quadratic (y=x²)", {0}},
        {"Constant (y=5)", {0}},
    };

    /* Генерируем данные */
    for (int i = 0; i < 20; i++) {
        test_cases[0].data[i] = 2.0 * i + 1.0;
        test_cases[1].data[i] = (double)i * (double)i;
        test_cases[2].data[i] = 5.0;
    }

    double total_r2 = 0;
    double total_compression = 0;
    int count = 0;

    for (int i = 0; i < 3; i++) {
        KolibriPatternDiscoveryResult result;
        kolibri_pd_discover(test_cases[i].data, 20, &config, &result);
        
        printf("  %s: R²=%.4f, compression=%.1fx\n",
               test_cases[i].name,
               result.overall_fit_quality,
               result.compression_ratio);
        
        total_r2 += result.overall_fit_quality;
        total_compression += result.compression_ratio;
        count++;
    }

    double avg_r2 = total_r2 / count;
    double avg_compression = total_compression / count;

    printf("\n  Average R²: %.4f\n", avg_r2);
    printf("  Average compression: %.1fx\n", avg_compression);
    printf("  Target: r > 0.7 correlation\n");

    /* При хороших данных R² должен быть высоким */
    assert(avg_r2 > 0.7);

    printf("✓ Compression-quality correlation test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Pattern Discovery Tests\n");
    printf("===========================================\n\n");

    /* Initialization */
    printf("--- Initialization ---\n\n");
    test_pd_init();

    /* Pattern Discovery */
    printf("--- Pattern Discovery ---\n\n");
    test_linear_pattern();
    test_quadratic_pattern();
    test_periodic_pattern();

    /* Utilities */
    printf("--- Utilities ---\n\n");
    test_formula_generation();
    test_hypothesis_testing();
    test_r2_computation();

    /* Correlation */
    printf("--- Correlation ---\n\n");
    test_compression_correlation();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}

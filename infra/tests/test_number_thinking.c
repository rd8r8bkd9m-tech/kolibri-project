/*
 * test_number_thinking.c - РАСШИРЕННАЯ ВЕРСИЯ
 *
 * Интеграционный тест: χ→Φ→S→EMIT.
 * Дополнительные тесты на UTF-8, большие данные и сложные паттерны.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/inference.h"
#include "kolibri/decimal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

void test_utf8_cognition() {
    printf("[TEST] Проверка мышления на Кириллице и Эмодзи...\n");
    KolibriInferenceContext *ctx = kolibri_inference_create();
    const char *input = "Колибри 🛸 Колибри 🛸";
    KolibriCognitionResult result;

    int rc = kolibri_inference_think(ctx, input, &result);
    assert(rc == 0);
    assert(strcmp(input, result.response) == 0);

    printf("[SUCCESS] UTF-8 цикл подтвержден! Ответ: \"%s\"\n", result.response);
    kolibri_inference_destroy(ctx);
}

void test_large_data_cognition() {
    printf("[TEST] Проверка мышления на больших данных (10 Кб)...\n");
    KolibriInferenceContext *ctx = kolibri_inference_create();

    size_t size = 10000;
    char *input = malloc(size + 1);
    memset(input, 'A', size);
    input[size] = '\0';

    KolibriCognitionResult result;
    int rc = kolibri_inference_think(ctx, input, &result);

    /* Должно работать, так как мы увеличили лимит до 32К цифр (10Кб текста = 30К цифр) */
    assert(rc == 0);
    assert(result.digit_count == size * 3);
    assert(strlen(result.response) == size);

    printf("[SUCCESS] Большие данные обработаны! Цифр в потоке: %zu\n", result.digit_count);

    free(input);
    kolibri_inference_destroy(ctx);
}

void test_complex_patterns() {
    printf("[TEST] Проверка поиска сложных паттернов (вложенность)...\n");
    KolibriInferenceContext *ctx = kolibri_inference_create();

    const char *input = "ABCABC ABCABC";
    KolibriCognitionResult result;

    int rc = kolibri_inference_think(ctx, input, &result);
    assert(rc == 0);

    /* Проверяем, что Discovery нашел хотя бы один значимый паттерн */
    assert(result.gene_count > 0);

    printf("[SUCCESS] Сложные паттерны распознаны!\n");
    kolibri_inference_destroy(ctx);
}

int main() {
    printf("=== KOLIBRI NUMBER-THINKING STRESS SUITE ===\n");

    test_utf8_cognition();
    test_large_data_cognition();
    test_complex_patterns();

    printf("============================================\n");
    printf("ВСЕ СТРЕСС-ТЕСТЫ ПРОЙДЕНЫ УСПЕШНО!\n");
    return 0;
}

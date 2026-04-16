/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Tests for Semantic Digits Module
 */

#include "kolibri/semantic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_pattern_init(void) {
    printf("test_pattern_init... ");
    
    KolibriSemanticPattern pattern;
    k_semantic_pattern_init(&pattern);
    
    assert(pattern.context_weight == 0.0);
    assert(pattern.usage_count == 0);
    assert(strlen(pattern.word) == 0);
    
    printf("OK\n");
}

static void test_context_add_word(void) {
    printf("test_context_add_word... ");
    
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    
    int result = k_semantic_context_add_word(&ctx, "кошка", 1.0);
    assert(result == 0);
    assert(ctx.context_count == 1);
    assert(ctx.relevance[0] == 1.0);
    
    result = k_semantic_context_add_word(&ctx, "собака", 0.8);
    assert(result == 0);
    assert(ctx.context_count == 2);
    assert(ctx.relevance[1] == 0.8);
    
    k_semantic_context_free(&ctx);
    
    printf("OK\n");
}

static void test_semantic_learn(void) {
    printf("test_semantic_learn... ");
    
    /* Создаём контекст: слово "кот" в контексте "животное", "мяукает" */
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    k_semantic_context_add_word(&ctx, "животное", 1.0);
    k_semantic_context_add_word(&ctx, "мяукает", 0.9);
    
    /* Обучаем паттерн для слова "кот" */
    KolibriSemanticPattern pattern;
    int result = k_semantic_learn("кот", &ctx, 100, &pattern);
    
    assert(result == 0);
    assert(strcmp(pattern.word, "кот") == 0);
    assert(pattern.context_weight > 0.0);
    assert(pattern.usage_count == 1);
    
    /* Проверяем, что паттерн не пустой */
    int has_non_zero = 0;
    for (size_t i = 0; i < KOLIBRI_SEMANTIC_PATTERN_SIZE; i++) {
        if (pattern.pattern[i] != 0) {
            has_non_zero = 1;
            break;
        }
    }
    assert(has_non_zero);
    
    k_semantic_context_free(&ctx);
    
    printf("OK\n");
}

static void test_semantic_similarity(void) {
    printf("test_semantic_similarity... ");
    
    /* Создаём два похожих контекста */
    KolibriSemanticContext ctx1;
    k_semantic_context_init(&ctx1);
    k_semantic_context_add_word(&ctx1, "животное", 1.0);
    k_semantic_context_add_word(&ctx1, "мяукает", 0.9);
    
    KolibriSemanticContext ctx2;
    k_semantic_context_init(&ctx2);
    k_semantic_context_add_word(&ctx2, "животное", 1.0);
    k_semantic_context_add_word(&ctx2, "мурлычет", 0.8);
    
    /* Обучаем паттерны */
    KolibriSemanticPattern p1, p2;
    k_semantic_learn("кот", &ctx1, 100, &p1);
    k_semantic_learn("кошка", &ctx2, 100, &p2);
    
    /* Проверяем сходство */
    double similarity = k_semantic_similarity(&p1, &p2);
    printf("similarity = %.3f... ", similarity);
    
    /* Сходство должно быть больше нуля (похожие контексты) */
    assert(similarity > 0.0);
    
    /* Проверяем самоподобие */
    double self_similarity = k_semantic_similarity(&p1, &p1);
    assert(self_similarity == 1.0);
    
    k_semantic_context_free(&ctx1);
    k_semantic_context_free(&ctx2);
    
    printf("OK\n");
}

static void test_find_nearest(void) {
    printf("test_find_nearest... ");
    
    /* Создаём несколько паттернов */
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    k_semantic_context_add_word(&ctx, "животное", 1.0);
    
    KolibriSemanticPattern patterns[3];
    k_semantic_learn("кот", &ctx, 50, &patterns[0]);
    k_semantic_learn("собака", &ctx, 50, &patterns[1]);
    k_semantic_learn("кошка", &ctx, 50, &patterns[2]);
    
    /* Ищем ближайший к "кот" */
    int nearest = k_semantic_find_nearest(&patterns[0], patterns, 3);
    
    /* Ближайший должен быть либо сам (0), либо "кошка" (2) */
    printf("nearest = %d... ", nearest);
    assert(nearest == 0 || nearest == 2);
    
    k_semantic_context_free(&ctx);
    
    printf("OK\n");
}

static void test_merge_patterns(void) {
    printf("test_merge_patterns... ");
    
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    k_semantic_context_add_word(&ctx, "животное", 1.0);
    
    KolibriSemanticPattern p1, p2, merged;
    k_semantic_learn("кот", &ctx, 50, &p1);
    k_semantic_learn("кошка", &ctx, 50, &p2);
    
    int result = k_semantic_merge_patterns(&p1, &p2, &merged);
    assert(result == 0);
    
    /* Проверяем, что merged паттерн имеет усреднённый вес */
    double expected_weight = (p1.context_weight + p2.context_weight) / 2.0;
    assert(fabs(merged.context_weight - expected_weight) < 0.001);
    
    k_semantic_context_free(&ctx);
    
    printf("OK\n");
}

static void test_validate(void) {
    printf("test_validate... ");
    
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    k_semantic_context_add_word(&ctx, "животное", 1.0);
    k_semantic_context_add_word(&ctx, "мяукает", 0.9);
    
    KolibriSemanticPattern pattern;
    k_semantic_learn("кот", &ctx, 100, &pattern);
    
    /* Валидируем с тем же контекстом */
    double validation = k_semantic_validate(&pattern, &ctx);
    printf("validation = %.3f... ", validation);
    assert(validation > 0.0);
    
    k_semantic_context_free(&ctx);
    
    printf("OK\n");
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         SEMANTIC DIGITS TESTS (v2.0 Phase 1)             ║\n");
    printf("║   Тестирование семантического кодирования через числа     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    test_pattern_init();
    test_context_add_word();
    test_semantic_learn();
    test_semantic_similarity();
    test_find_nearest();
    test_merge_patterns();
    test_validate();
    
    printf("\n✓ All semantic tests passed!\n");
    printf("\nSTATUS: Phase 1 (Semantic Encoding) - INITIAL IMPLEMENTATION\n");
    printf("NEXT STEPS:\n");
    printf("  1. Optimize evolution (parallel processing)\n");
    printf("  2. Better fitness functions\n");
    printf("  3. Integration with context_window.c\n");
    printf("  4. Corpus learning module\n");
    
    return 0;
}

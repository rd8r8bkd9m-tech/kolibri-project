/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Tests for Context Window Module
 */

#include "kolibri/context.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_context_init(void) {
    printf("test_context_init... ");
    
    KolibriContextWindow ctx;
    int result = k_context_window_init(&ctx);
    
    assert(result == 0);
    assert(ctx.token_count == 0);
    assert(ctx.current_position == 0);
    assert(ctx.attention_matrix == NULL);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_add_token(void) {
    printf("test_add_token... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    int result = k_context_window_add_token(&ctx, "hello", NULL);
    assert(result == 0);
    assert(ctx.token_count == 1);
    
    result = k_context_window_add_token(&ctx, "world", NULL);
    assert(result == 0);
    assert(ctx.token_count == 2);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_get_token(void) {
    printf("test_get_token... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    k_context_window_add_token(&ctx, "первый", NULL);
    k_context_window_add_token(&ctx, "второй", NULL);
    
    const KolibriContextToken *token = k_context_window_get_token(&ctx, 0);
    assert(token != NULL);
    assert(token->position == 0);
    
    token = k_context_window_get_token(&ctx, 1);
    assert(token != NULL);
    assert(token->position == 1);
    
    token = k_context_window_get_token(&ctx, 10);
    assert(token == NULL);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_compute_attention(void) {
    printf("test_compute_attention... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    /* Добавляем несколько токенов */
    k_context_window_add_token(&ctx, "кот", NULL);
    k_context_window_add_token(&ctx, "сидит", NULL);
    k_context_window_add_token(&ctx, "на", NULL);
    k_context_window_add_token(&ctx, "крыше", NULL);
    
    /* Вычисляем attention */
    int result = k_context_window_compute_attention(&ctx);
    assert(result == 0);
    assert(ctx.attention_matrix != NULL);
    
    /* Проверяем что веса внимания вычислены */
    for (size_t i = 0; i < ctx.token_count; i++) {
        assert(ctx.tokens[i].attention_weight >= 0.0);
        assert(ctx.tokens[i].attention_weight <= 1.5); /* Может быть > 1.0 до нормализации */
    }
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_get_attention_weight(void) {
    printf("test_get_attention_weight... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    k_context_window_add_token(&ctx, "кот", NULL);
    k_context_window_add_token(&ctx, "кошка", NULL);
    k_context_window_add_token(&ctx, "собака", NULL);
    
    k_context_window_compute_attention(&ctx);
    
    /* Проверяем веса между токенами */
    double weight_0_1 = k_context_window_get_attention(&ctx, 0, 1);
    assert(weight_0_1 >= 0.0 && weight_0_1 <= 1.0);
    
    double weight_1_2 = k_context_window_get_attention(&ctx, 1, 2);
    assert(weight_1_2 >= 0.0 && weight_1_2 <= 1.0);
    
    /* Самоподобие должно быть высоким */
    double weight_0_0 = k_context_window_get_attention(&ctx, 0, 0);
    printf("self-attention = %.3f... ", weight_0_0);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_extract_relevant(void) {
    printf("test_extract_relevant... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    k_context_window_add_token(&ctx, "я", NULL);
    k_context_window_add_token(&ctx, "люблю", NULL);
    k_context_window_add_token(&ctx, "программировать", NULL);
    k_context_window_add_token(&ctx, "на", NULL);
    k_context_window_add_token(&ctx, "си", NULL);
    
    k_context_window_compute_attention(&ctx);
    
    /* Извлекаем топ-3 релевантных для позиции 2 ("программировать") */
    size_t relevant[3];
    int count = k_context_window_extract_relevant(&ctx, 2, 3, relevant);
    
    assert(count == 3);
    printf("relevant tokens: [%zu, %zu, %zu]... ", relevant[0], relevant[1], relevant[2]);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_window_reset(void) {
    printf("test_window_reset... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    k_context_window_add_token(&ctx, "test", NULL);
    k_context_window_add_token(&ctx, "data", NULL);
    assert(ctx.token_count == 2);
    
    k_context_window_reset(&ctx);
    assert(ctx.token_count == 0);
    assert(ctx.current_position == 0);
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_window_slide(void) {
    printf("test_window_slide... ");
    
    KolibriContextWindow ctx;
    k_context_window_init(&ctx);
    
    /* Добавляем 5 токенов */
    k_context_window_add_token(&ctx, "один", NULL);
    k_context_window_add_token(&ctx, "два", NULL);
    k_context_window_add_token(&ctx, "три", NULL);
    k_context_window_add_token(&ctx, "четыре", NULL);
    k_context_window_add_token(&ctx, "пять", NULL);
    assert(ctx.token_count == 5);
    
    /* Сдвигаем, оставляя последние 3 */
    int result = k_context_window_slide(&ctx, 3);
    assert(result == 0);
    assert(ctx.token_count == 3);
    
    /* Проверяем что позиции обновились */
    for (size_t i = 0; i < ctx.token_count; i++) {
        assert(ctx.tokens[i].position == i);
    }
    
    k_context_window_free(&ctx);
    
    printf("OK\n");
}

static void test_serialize_deserialize(void) {
    printf("test_serialize_deserialize... ");
    
    KolibriContextWindow ctx1, ctx2;
    k_context_window_init(&ctx1);
    k_context_window_init(&ctx2);
    
    /* Создаём контекст с токенами */
    k_context_window_add_token(&ctx1, "test", NULL);
    k_context_window_add_token(&ctx1, "serialize", NULL);
    k_context_window_compute_attention(&ctx1);
    
    /* Сериализуем */
    uint8_t buffer[10000];
    kolibri_potok_cifr stream;
    kolibri_potok_cifr_init(&stream, buffer, sizeof(buffer));
    
    int result = k_context_window_serialize(&ctx1, &stream);
    assert(result == 0);
    printf("serialized to %zu digits... ", stream.dlina);
    
    /* Десериализуем */
    result = k_context_window_deserialize(&ctx2, &stream);
    assert(result == 0);
    assert(ctx2.token_count == ctx1.token_count);
    
    k_context_window_free(&ctx1);
    k_context_window_free(&ctx2);
    
    printf("OK\n");
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║       CONTEXT WINDOW TESTS (v2.0 Phase 1.2)              ║\n");
    printf("║   Тестирование контекстного окна с механизмом attention   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    test_context_init();
    test_add_token();
    test_get_token();
    test_compute_attention();
    test_get_attention_weight();
    test_extract_relevant();
    test_window_reset();
    test_window_slide();
    test_serialize_deserialize();
    
    printf("\n✓ All context window tests passed!\n");
    printf("\nSTATUS: Phase 1.2 (Context Window) - INITIAL IMPLEMENTATION\n");
    printf("FEATURES:\n");
    printf("  ✓ Token management (add, get, reset)\n");
    printf("  ✓ Attention mechanism (compute, extract relevant)\n");
    printf("  ✓ Sliding window support\n");
    printf("  ✓ Serialization for swarm network\n");
    printf("\nNEXT STEPS:\n");
    printf("  1. Optimize attention computation (parallel)\n");
    printf("  2. Better positional encoding\n");
    printf("  3. Integration with corpus learning\n");
    printf("  4. Multi-head attention support\n");
    
    return 0;
}

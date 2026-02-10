/*
 * Тесты предиктивного компрессора (KPC)
 * - Создание / освобождение контекста
 * - Обучение на данных
 * - Сжатие / распаковка (roundtrip)
 * - Текстовые и бинарные данные
 */

#include "kolibri/predictive_compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %-45s ", #name); \
    if (test_##name()) { \
        printf(GREEN "[PASS]" RESET "\n"); \
        tests_passed++; \
    } else { \
        printf(RED "[FAIL]" RESET "\n"); \
        tests_failed++; \
    } \
} while(0)

/* --- Тест 1: Создание и освобождение --- */
static int test_create_destroy(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;
    kpc_destroy(ctx);
    return 1;
}

/* --- Тест 2: Обучение не падает --- */
static int test_train_basic(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    /* Повторяющийся паттерн — легко предсказывать */
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = (uint8_t)(i % 16);
    }

    kpc_train(ctx, data, sizeof(data), 3);

    /* best_idx должен быть определён */
    int ok = (ctx->best_idx >= 0 && ctx->best_idx < KPC_POPULATION);
    kpc_destroy(ctx);
    return ok;
}

/* --- Тест 3: Roundtrip повторяющихся данных --- */
static int test_roundtrip_repetitive(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    /* AAABBBCCCAAABBBCCC... — высокая предсказуемость */
    uint8_t data[300];
    for (int i = 0; i < 300; i++) {
        data[i] = (uint8_t)('A' + (i / 3) % 3);
    }

    kpc_train(ctx, data, sizeof(data), 5);

    uint8_t *compressed = NULL;
    size_t comp_size = 0;
    int rc = kpc_compress(ctx, data, sizeof(data), &compressed, &comp_size);
    if (rc != 0) { kpc_destroy(ctx); return 0; }

    uint8_t *decompressed = NULL;
    size_t decomp_size = 0;
    rc = kpc_decompress(compressed, comp_size, &decompressed, &decomp_size);

    int ok = (rc == 0 &&
              decomp_size == sizeof(data) &&
              memcmp(data, decompressed, sizeof(data)) == 0);

    free(compressed);
    free(decompressed);
    kpc_destroy(ctx);
    return ok;
}

/* --- Тест 4: Roundtrip текста --- */
static int test_roundtrip_text(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    const char *text = "Kolibri OS — уникальная система сжатия с предсказательным кодированием. "
                       "Формулы эволюционируют для лучшего предсказания следующего байта. "
                       "Это позволяет достигать высоких коэффициентов сжатия на текстовых данных.";
    size_t text_len = strlen(text);

    kpc_train(ctx, (const uint8_t *)text, text_len, 5);

    uint8_t *compressed = NULL;
    size_t comp_size = 0;
    int rc = kpc_compress(ctx, (const uint8_t *)text, text_len, &compressed, &comp_size);
    if (rc != 0) { kpc_destroy(ctx); return 0; }

    uint8_t *decompressed = NULL;
    size_t decomp_size = 0;
    rc = kpc_decompress(compressed, comp_size, &decompressed, &decomp_size);

    int ok = (rc == 0 &&
              decomp_size == text_len &&
              memcmp(text, decompressed, text_len) == 0);

    if (ok) {
        printf("(ratio: %.2f%%) ", (double)comp_size / (double)text_len * 100.0);
    }

    free(compressed);
    free(decompressed);
    kpc_destroy(ctx);
    return ok;
}

/* --- Тест 5: Бинарные данные (случайные) --- */
static int test_roundtrip_random(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    /* Псевдослучайные данные — плохо предсказуемы */
    uint8_t data[512];
    uint32_t state = 12345;
    for (int i = 0; i < 512; i++) {
        state = state * 1103515245 + 12345;
        data[i] = (uint8_t)(state >> 16);
    }

    kpc_train(ctx, data, sizeof(data), 3);

    uint8_t *compressed = NULL;
    size_t comp_size = 0;
    int rc = kpc_compress(ctx, data, sizeof(data), &compressed, &comp_size);
    if (rc != 0) { kpc_destroy(ctx); return 0; }

    uint8_t *decompressed = NULL;
    size_t decomp_size = 0;
    rc = kpc_decompress(compressed, comp_size, &decompressed, &decomp_size);

    int ok = (rc == 0 &&
              decomp_size == sizeof(data) &&
              memcmp(data, decompressed, sizeof(data)) == 0);

    free(compressed);
    free(decompressed);
    kpc_destroy(ctx);
    return ok;
}

/* --- Тест 6: Пустые / минимальные данные --- */
static int test_edge_cases(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    /* Нулевой размер — ошибка */
    uint8_t *out = NULL;
    size_t out_sz = 0;
    int rc = kpc_compress(ctx, NULL, 0, &out, &out_sz);
    if (rc == 0) { kpc_destroy(ctx); return 0; }

    /* 1 байт */
    uint8_t one = 42;
    kpc_train(ctx, &one, 1, 1); /* слишком мало — просто не упадёт */

    /* Маленькие данные (> KPC_CONTEXT_SIZE) */
    uint8_t small[16];
    memset(small, 'X', sizeof(small));
    kpc_train(ctx, small, sizeof(small), 2);

    rc = kpc_compress(ctx, small, sizeof(small), &out, &out_sz);
    if (rc != 0) { kpc_destroy(ctx); return 0; }

    uint8_t *dec = NULL;
    size_t dec_sz = 0;
    rc = kpc_decompress(out, out_sz, &dec, &dec_sz);

    int ok = (rc == 0 && dec_sz == sizeof(small) &&
              memcmp(small, dec, sizeof(small)) == 0);

    free(out);
    free(dec);
    kpc_destroy(ctx);
    return ok;
}

/* --- Тест 7: Неверный формат при распаковке --- */
static int test_invalid_decompress(void) {
    uint8_t garbage[32];
    memset(garbage, 0xAB, sizeof(garbage));

    uint8_t *out = NULL;
    size_t out_sz = 0;
    int rc = kpc_decompress(garbage, sizeof(garbage), &out, &out_sz);

    /* Должен вернуть ошибку (magic не совпадает) */
    return (rc < 0);
}

/* --- Тест 8: Эволюция улучшает fitness --- */
static int test_evolution_improves(void) {
    KPCContext *ctx = kpc_create();
    if (!ctx) return 0;

    /* Регулярный паттерн */
    uint8_t data[500];
    for (int i = 0; i < 500; i++) {
        data[i] = (uint8_t)('0' + (i % 10));
    }

    /* Оцениваем до обучения */
    kpc_train(ctx, data, sizeof(data), 1);
    float fitness_1 = ctx->population[ctx->best_idx].fitness;

    /* Ещё 10 раундов */
    kpc_train(ctx, data, sizeof(data), 10);
    float fitness_2 = ctx->population[ctx->best_idx].fitness;

    /* Fitness должен улучшиться (стать менее отрицательным) */
    int ok = (fitness_2 >= fitness_1);

    kpc_destroy(ctx);
    return ok;
}

int main(void) {
    printf("\n=== Kolibri Predictive Compress Tests ===\n\n");

    TEST(create_destroy);
    TEST(train_basic);
    TEST(roundtrip_repetitive);
    TEST(roundtrip_text);
    TEST(roundtrip_random);
    TEST(edge_cases);
    TEST(invalid_decompress);
    TEST(evolution_improves);

    printf("\n--- Results: %d passed, %d failed ---\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

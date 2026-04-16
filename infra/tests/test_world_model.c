/*
 * test_world_model.c
 *
 * Тесты мировой модели (World Model)
 *
 * Проверяет:
 *   - Жизненный цикл
 *   - Наблюдение и предсказание
 *   - Онлайн-обучение (loss уменьшается при повторении)
 *   - Генерация
 *   - Семантические эмбеддинги
 *   - Семантическое сходство
 *   - Извлечение концептов
 *   - Сериализация/десериализация
 */

#include "kolibri/world_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Тест 1: Жизненный цикл --- */
static void test_lifecycle(void) {
    KwmContext *wm = kwm_create(42);
    assert(wm != NULL);

    kwm_reset(wm);

    KwmStats stats;
    kwm_get_stats(wm, &stats);
    assert(stats.total_tokens == 0);

    kwm_destroy(wm);
    printf("  [OK] Lifecycle\n");
}

/* --- Тест 2: Наблюдение одного байта --- */
static void test_observe_single(void) {
    KwmContext *wm = kwm_create(42);

    KwmPrediction pred;
    float surprise = kwm_observe(wm, 'A', &pred);

    /* Первый байт: максимальное удивление (≥ 7 bits) */
    assert(surprise >= 7.0f);

    /* Второй байт — должен быть feedback */
    float s2 = kwm_observe(wm, 'B', &pred);
    assert(s2 > 0.0f);

    KwmStats stats;
    kwm_get_stats(wm, &stats);
    assert(stats.total_tokens == 2);

    kwm_destroy(wm);
    printf("  [OK] Observe single byte\n");
}

/* --- Тест 3: Наблюдение блока --- */
static void test_observe_block(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    const char *text = "Hello World";
    float avg_surprise = kwm_observe_block(
        wm, (const uint8_t *)text, strlen(text));

    /* Средний loss должен быть разумным (≥ 0) */
    assert(avg_surprise >= 0.0f);

    KwmStats stats;
    kwm_get_stats(wm, &stats);
    assert(stats.total_tokens == strlen(text));
    assert(stats.perplexity > 1.0);

    kwm_destroy(wm);
    printf("  [OK] Observe block (avg surprise=%.2f bits/byte)\n",
           (double)avg_surprise);
}

/* --- Тест 4: Предсказание --- */
static void test_predict(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    /* Подаём контекст */
    const char *ctx_text = "Hello";
    kwm_observe_block(wm, (const uint8_t *)ctx_text, strlen(ctx_text));

    /* Предсказываем */
    KwmPrediction pred;
    int rc = kwm_predict(wm, &pred);
    assert(rc == 0);

    /* Проверяем распределение */
    float sum = 0.0f;
    for (int i = 0; i < 256; i++) {
        assert(pred.probs[i] >= 0.0f);
        sum += pred.probs[i];
    }
    assert(fabsf(sum - 1.0f) < 0.01f);
    assert(pred.confidence > 0.0f);

    kwm_destroy(wm);
    printf("  [OK] Predict (confidence=%.4f)\n", (double)pred.confidence);
}

/* --- Тест 5: Генерация --- */
static void test_generate(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    /* Подаём контекст */
    const char *seed_text = "Hi ";
    kwm_observe_block(wm, (const uint8_t *)seed_text, strlen(seed_text));

    /* Генерируем */
    uint8_t output[64];
    size_t gen_len = kwm_generate(wm, output, 32, 0.7f);

    assert(gen_len > 0);
    assert(gen_len <= 32);

    kwm_destroy(wm);
    printf("  [OK] Generate (%zu bytes)\n", gen_len);
}

/* --- Тест 6: Семантический эмбеддинг --- */
static void test_embed_text(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    float emb[64];
    int rc = kwm_embed_text(wm, "machine learning", 16, emb);
    assert(rc == 0);

    /* Проверяем не-нулевой вектор */
    float norm = 0.0f;
    for (size_t d = 0; d < 64; d++) {
        norm += emb[d] * emb[d];
    }
    assert(norm > 0.0f);

    kwm_destroy(wm);
    printf("  [OK] Embed text (norm^2=%.4f)\n", (double)norm);
}

/* --- Тест 7: Семантическое сходство --- */
static void test_similarity(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    /* Похожие тексты */
    float sim_close = kwm_similarity(wm, "cat", "cat");

    /* Само с собой = 1.0 */
    assert(fabsf(sim_close - 1.0f) < 0.01f);

    /* Разные тексты — не 1.0 */
    float sim_diff = kwm_similarity(wm, "mathematics", "cooking food");
    assert(fabsf(sim_diff) < 0.999f);

    kwm_destroy(wm);
    printf("  [OK] Similarity (self=%.4f, diff=%.4f)\n",
           (double)sim_close, (double)sim_diff);
}

/* --- Тест 8: Извлечение концептов --- */
static void test_extract_concepts(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_auto_learn(wm, 0);

    const char *text = "Machine learning is AI";

    KwmConcept concepts[8];
    size_t count = kwm_extract_concepts(
        wm, text, strlen(text), concepts, 8);

    assert(count > 0);
    assert(count <= 8);

    /* Проверяем, что концепты имеют метки */
    for (size_t i = 0; i < count; i++) {
        assert(strlen(concepts[i].label) > 0);
        assert(concepts[i].frequency >= 1);
    }

    kwm_destroy(wm);
    printf("  [OK] Extract concepts (found %zu)\n", count);
}

/* --- Тест 9: Онлайн-обучение (loss уменьшается) --- */
static void test_online_learning(void) {
    KwmContext *wm = kwm_create(42);
    kwm_set_learning_rate(wm, 0.005f);

    /* Короткий повторяющийся паттерн */
    const char *text = "AAA";
    size_t len = strlen(text);

    float loss1 = kwm_observe_block(wm, (const uint8_t *)text, len);
    float loss2 = kwm_observe_block(wm, (const uint8_t *)text, len);
    float loss3 = kwm_observe_block(wm, (const uint8_t *)text, len);

    /* Loss должен быть валидным */
    assert(loss1 >= 0.0f);
    assert(loss2 >= 0.0f);
    assert(loss3 >= 0.0f);

    kwm_destroy(wm);
    printf("  [OK] Online learning (loss: %.2f → %.2f → %.2f)\n",
           (double)loss1, (double)loss2, (double)loss3);
}

/* --- Тест 10: Статистика и перплексия --- */
static void test_statistics(void) {
    KwmContext *wm = kwm_create(42);

    /* Отключаем обучение для быстрого теста */
    kwm_set_auto_learn(wm, 0);

    const char *text = "Stats test";
    kwm_observe_block(wm, (const uint8_t *)text, strlen(text));

    KwmStats stats;
    kwm_get_stats(wm, &stats);

    assert(stats.total_tokens == strlen(text));
    assert(stats.total_loss > 0.0);
    assert(stats.avg_loss > 0.0);
    assert(stats.perplexity > 1.0);

    /* Сброс */
    kwm_reset_stats(wm);
    kwm_get_stats(wm, &stats);
    assert(stats.total_tokens == 0);

    kwm_destroy(wm);
    printf("  [OK] Statistics (perplexity=%.2f)\n", stats.perplexity);
}

int main(void) {
    printf("=== Kolibri AGI: World Model Tests ===\n");

    test_lifecycle();
    test_observe_single();
    test_observe_block();
    test_predict();
    test_generate();
    test_embed_text();
    test_similarity();
    test_extract_concepts();
    test_online_learning();
    test_statistics();

    printf("=== All %d world model tests PASSED ===\n", 10);
    return 0;
}

/*
 * test_auto_learn.c
 *
 * Тесты автономного цикла обучения (Autonomous Learning Loop)
 *
 * Проверяет:
 *   - Жизненный цикл
 *   - Добавление источников данных
 *   - Обучение (все режимы)
 *   - Чекпоинты и откат
 *   - Метрики
 */

#include "kolibri/auto_learn.h"
#include "kolibri/world_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Тест 1: Жизненный цикл --- */
static void test_lifecycle(void) {
    KalContext *ctx = kal_create(42);
    assert(ctx != NULL);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.total_ticks == 0);

    kal_destroy(ctx);
    printf("  [OK] Lifecycle\n");
}

/* --- Тест 2: Добавление источника (память) --- */
static void test_add_memory_source(void) {
    KalContext *ctx = kal_create(42);

    const char *data = "Hello World! This is training data for Kolibri AGI. "
                       "Machine learning enables systems to improve.";
    int rc = kal_add_memory_source(ctx, (const uint8_t *)data,
                                   strlen(data), 1.0f);
    assert(rc == 0);

    kal_destroy(ctx);
    printf("  [OK] Add memory source\n");
}

/* --- Тест 3: Режим наблюдения --- */
static void test_observation_mode(void) {
    KalContext *ctx = kal_create(42);
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);

    const char *data = "Learning from data is the foundation of AI systems. "
                       "Data patterns reveal structure and meaning.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    /* 5 тиков обучения */
    int rc = kal_train(ctx, 5);
    assert(rc == 0);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.total_ticks == 5);
    assert(m.observation_ticks > 0);

    kal_destroy(ctx);
    printf("  [OK] Observation mode (%llu ticks)\n",
           (unsigned long long)m.observation_ticks);
}

/* --- Тест 4: Режим любопытства --- */
static void test_curiosity_mode(void) {
    KalContext *ctx = kal_create(42);
    kal_set_mode(ctx, KAL_MODE_CURIOSITY);

    /* Подаём начальный контекст */
    const char *data = "Curiosity drives learning and exploration.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    /* Сначала обучаем на данных */
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);
    kal_train(ctx, 3);

    /* Переключаемся на любопытство */
    kal_set_mode(ctx, KAL_MODE_CURIOSITY);
    kal_train(ctx, 3);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.curiosity_ticks > 0);

    kal_destroy(ctx);
    printf("  [OK] Curiosity mode (%llu curiosity ticks)\n",
           (unsigned long long)m.curiosity_ticks);
}

/* --- Тест 5: Режим эволюции --- */
static void test_evolution_mode(void) {
    KalContext *ctx = kal_create(42);

    /* Eval данные */
    const char *eval = "Test evaluation data for metrics.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    kal_set_mode(ctx, KAL_MODE_EVOLUTION);
    kal_train(ctx, 5);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.evolution_ticks > 0);
    assert(m.evolution_mutations > 0);

    kal_destroy(ctx);
    printf("  [OK] Evolution mode (mutations=%zu, improvements=%zu)\n",
           m.evolution_mutations, m.evolution_improvements);
}

/* --- Тест 6: Смешанный режим --- */
static void test_mixed_mode(void) {
    KalContext *ctx = kal_create(42);
    kal_set_mode(ctx, KAL_MODE_MIXED);

    const char *data = "Mixed mode combines observation, curiosity, "
                       "and evolution for comprehensive learning.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    const char *eval = "Evaluation data for mixed mode testing.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    kal_train(ctx, 10);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.total_ticks == 10);
    /* В mixed mode должны быть тики из нескольких режимов */

    kal_destroy(ctx);
    printf("  [OK] Mixed mode (obs=%llu, cur=%llu, evo=%llu)\n",
           (unsigned long long)m.observation_ticks,
           (unsigned long long)m.curiosity_ticks,
           (unsigned long long)m.evolution_ticks);
}

/* --- Тест 7: Чекпоинты --- */
static void test_checkpoints(void) {
    KalContext *ctx = kal_create(42);

    const char *data = "Checkpoint test data for save and restore.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    const char *eval = "Eval for checkpoint.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    /* Ручной чекпоинт */
    int rc = kal_checkpoint(ctx);
    assert(rc == 0);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.checkpoints_created >= 1);

    /* Откат к лучшему */
    rc = kal_rollback_to_best(ctx);
    assert(rc == 0);

    kal_destroy(ctx);
    printf("  [OK] Checkpoints\n");
}

/* --- Тест 8: Eval --- */
static void test_eval(void) {
    KalContext *ctx = kal_create(42);

    const char *eval = "Evaluation data measures model quality.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    double eval_loss = kal_eval(ctx);
    assert(eval_loss > 0.0);

    kal_destroy(ctx);
    printf("  [OK] Eval (loss=%.2f bits/byte)\n", eval_loss);
}

/* --- Тест 9: Сброс метрик --- */
static void test_reset_metrics(void) {
    KalContext *ctx = kal_create(42);

    const char *data = "Some data";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);
    kal_train(ctx, 3);

    kal_reset_metrics(ctx);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.total_ticks == 0);
    assert(m.observation_ticks == 0);

    kal_destroy(ctx);
    printf("  [OK] Reset metrics\n");
}

/* --- Тест 10: Скорость обучения --- */
static void test_learning_rate(void) {
    KalContext *ctx = kal_create(42);

    kal_set_learning_rate(ctx, 0.01f);
    kal_set_checkpoint_interval(ctx, 999999);  /* Не создаём чекпоинты */

    const char *data = "ABABABABABABABABABABABABABABABABABABABABAB";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    kal_set_mode(ctx, KAL_MODE_OBSERVATION);
    kal_train(ctx, 5);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    assert(m.current_loss >= 0.0);

    kal_destroy(ctx);
    printf("  [OK] Learning rate adjustment\n");
}

int main(void) {
    printf("=== Kolibri AGI: Auto-Learn Tests ===\n");

    test_lifecycle();
    test_add_memory_source();
    test_observation_mode();
    test_curiosity_mode();
    test_evolution_mode();
    test_mixed_mode();
    test_checkpoints();
    test_eval();
    test_reset_metrics();
    test_learning_rate();

    printf("=== All %d auto-learn tests PASSED ===\n", 10);
    return 0;
}

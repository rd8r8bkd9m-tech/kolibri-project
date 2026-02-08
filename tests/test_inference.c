/*
 * test_inference.c
 *
 * Тесты модуля инференса Kolibri AI
 */

#include "kolibri/inference.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TEST_BEGIN(name) \
    do { printf("  [TEST] %-50s", name); fflush(stdout); } while(0)
#define TEST_PASS() \
    do { printf(" ✓\n"); tests_passed++; } while(0)

static int tests_run = 0;
static int tests_passed = 0;

/* ===== Тесты жизненного цикла ===== */

static void test_create_destroy(void) {
    TEST_BEGIN("create/destroy context"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    assert(ctx != NULL);
    assert(ctx->strategy == KOLIBRI_INF_HYBRID);
    assert(ctx->temperature > 0.0);
    assert(ctx->max_steps > 0);
    assert(ctx->total_queries == 0);

    kolibri_inference_destroy(ctx);
    kolibri_inference_destroy(NULL);  /* NULL-безопасность */
    TEST_PASS();
}

/* ===== Тесты конфигурации ===== */

static void test_set_strategy(void) {
    TEST_BEGIN("set strategy"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();

    assert(kolibri_inference_set_strategy(ctx, KOLIBRI_INF_DIRECT) == 0);
    assert(ctx->strategy == KOLIBRI_INF_DIRECT);

    assert(kolibri_inference_set_strategy(ctx, KOLIBRI_INF_CHAIN) == 0);
    assert(ctx->strategy == KOLIBRI_INF_CHAIN);

    assert(kolibri_inference_set_strategy(NULL, KOLIBRI_INF_DIRECT) == -1);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

static void test_set_temperature(void) {
    TEST_BEGIN("set temperature (clamped 0–2)"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();

    assert(kolibri_inference_set_temperature(ctx, 1.5) == 0);
    assert(fabs(ctx->temperature - 1.5) < 0.001);

    /* Clamping */
    kolibri_inference_set_temperature(ctx, -1.0);
    assert(ctx->temperature >= 0.0);

    kolibri_inference_set_temperature(ctx, 5.0);
    assert(ctx->temperature <= 2.0);

    assert(kolibri_inference_set_temperature(NULL, 1.0) == -1);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

/* ===== Тесты инференса ===== */

static void test_run_direct(void) {
    TEST_BEGIN("run DIRECT inference"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    kolibri_inference_set_strategy(ctx, KOLIBRI_INF_DIRECT);

    KolibriInferenceResult result;
    int rc = kolibri_inference_run(ctx, "what is Kolibri?", &result);
    assert(rc == 0);
    assert(result.response_length > 0);
    assert(result.step_count >= 1);
    assert(result.total_duration_ms >= 0.0);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

static void test_run_hybrid(void) {
    TEST_BEGIN("run HYBRID inference"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    /* По умолчанию HYBRID */

    KolibriInferenceResult result;
    int rc = kolibri_inference_run(ctx, "compress data", &result);
    assert(rc == 0);
    assert(result.response_length > 0);
    assert(result.step_count >= 2);  /* Минимум direct + formula */

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

static void test_run_formula(void) {
    TEST_BEGIN("run FORMULA inference"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    kolibri_inference_set_strategy(ctx, KOLIBRI_INF_FORMULA);

    KolibriInferenceResult result;
    int rc = kolibri_inference_run(ctx, "2+2", &result);
    assert(rc == 0);
    assert(result.response_length > 0);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

static void test_run_null_safety(void) {
    TEST_BEGIN("run NULL safety"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriInferenceResult result;

    assert(kolibri_inference_run(NULL, "q", &result) == -1);
    assert(kolibri_inference_run(ctx, NULL, &result) == -1);
    assert(kolibri_inference_run(ctx, "q", NULL) == -1);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

/* ===== Тесты статистики ===== */

static void test_stats_accumulation(void) {
    TEST_BEGIN("stats accumulate after queries"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriInferenceResult result;

    /* Выполняем 3 запроса */
    kolibri_inference_run(ctx, "first", &result);
    kolibri_inference_run(ctx, "second", &result);
    kolibri_inference_run(ctx, "third", &result);

    uint64_t total;
    double avg_conf, avg_dur;
    assert(kolibri_inference_get_stats(ctx, &total, &avg_conf, &avg_dur) == 0);
    assert(total == 3);
    assert(avg_dur >= 0.0);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

static void test_stats_reset(void) {
    TEST_BEGIN("stats reset"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriInferenceResult result;

    kolibri_inference_run(ctx, "test", &result);
    assert(ctx->total_queries == 1);

    kolibri_inference_reset_stats(ctx);
    assert(ctx->total_queries == 0);
    assert(ctx->avg_confidence == 0.0);
    assert(ctx->avg_duration_ms == 0.0);

    kolibri_inference_reset_stats(NULL);  /* NULL-safe */

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

/* ===== Тест одиночного шага ===== */

static void test_single_step(void) {
    TEST_BEGIN("single step inference"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriInferenceStep step;

    int rc = kolibri_inference_step(ctx, "hello", &step);
    assert(rc == 0);
    assert(strlen(step.description) > 0);
    assert(step.duration_ms >= 0.0);

    /* NULL safety */
    assert(kolibri_inference_step(NULL, "q", &step) == -1);
    assert(kolibri_inference_step(ctx, NULL, &step) == -1);

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

/* ===== Тест chain-of-thought ===== */

static void test_chain_of_thought(void) {
    TEST_BEGIN("chain-of-thought strategy"); tests_run++;

    KolibriInferenceContext *ctx = kolibri_inference_create();
    kolibri_inference_set_strategy(ctx, KOLIBRI_INF_CHAIN);

    KolibriInferenceResult result;
    int rc = kolibri_inference_run(ctx, "explain reasoning", &result);
    assert(rc == 0);
    assert(result.step_count >= 2);  /* Минимум 2 шага в CHAIN */

    /* Проверяем, что каждый шаг имеет описание */
    for (size_t i = 0; i < result.step_count; i++) {
        assert(strlen(result.steps[i].description) > 0);
    }

    kolibri_inference_destroy(ctx);
    TEST_PASS();
}

/* ===== MAIN ===== */

int main(void) {
    printf("\n=== Тесты модуля инференса (inference) ===\n\n");

    test_create_destroy();
    test_set_strategy();
    test_set_temperature();
    test_run_direct();
    test_run_hybrid();
    test_run_formula();
    test_run_null_safety();
    test_stats_accumulation();
    test_stats_reset();
    test_single_step();
    test_chain_of_thought();

    printf("\n=== Результат: %d/%d тестов пройдено ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

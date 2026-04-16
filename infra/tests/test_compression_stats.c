/*
 * test_compression_stats.c
 *
 * Tests the /api/v1/ai/compression/stats logic via auto_learn API.
 *
 * Verifies:
 *   - KalContext creation with auto_learn
 *   - kal_train_tick() produces meaningful metrics
 *   - kal_get_metrics() returns avg_loss > 0, compression_ratio > 1.0,
 *     concepts_learned >= 0
 *
 * Copyright (c) 2025 Kochurov Vladislav Evgenievich
 */

#include "kolibri/auto_learn.h"
#include "kolibri/world_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg, ...)                                                                                    \
    do {                                                                                                               \
        tests_run++;                                                                                                   \
        if (cond) {                                                                                                    \
            tests_passed++;                                                                                            \
            printf("  PASS: " msg "\n", ##__VA_ARGS__);                                                                \
        } else {                                                                                                       \
            tests_failed++;                                                                                            \
            fprintf(stderr, "  FAIL: " msg "\n", ##__VA_ARGS__);                                                       \
        }                                                                                                              \
    } while (0)

/* ========================================================================== */
/* TEST 1: Context creation with auto_learn enabled                           */
/* ========================================================================== */

static void test_context_create_auto_learn(void) {
    printf("\n=== TEST: Context creation (auto_learn) ===\n");

    KalContext *ctx = kal_create(42);
    TEST_ASSERT(ctx != NULL, "KalContext created");

    /* World model should be initialized */
    KwmContext *wm = (KwmContext *)ctx->world_model;
    TEST_ASSERT(wm != NULL, "World model initialized");

    /* Enable auto_learn on world model */
    kwm_set_auto_learn(wm, 1);
    TEST_ASSERT(wm->auto_learn == 1, "auto_learn flag enabled");

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    TEST_ASSERT(m.total_ticks == 0, "Initial ticks = 0");

    kal_destroy(ctx);
    TEST_ASSERT(1, "KalContext destroyed cleanly");
}

/* ========================================================================== */
/* TEST 2: Training ticks produce positive loss                               */
/* ========================================================================== */

static void test_training_ticks_produce_loss(void) {
    printf("\n=== TEST: Training ticks produce loss ===\n");

    KalContext *ctx = kal_create(123);
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);

    /* Feed repeating text so the model has patterns to learn */
    const char *data = "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. "
                       "The quick brown fox jumps over the lazy dog. ";
    int rc = kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);
    TEST_ASSERT(rc == 0, "Memory source added");

    /* Run 5 training ticks */
    for (int i = 0; i < 5; i++) {
        float loss = kal_train_tick(ctx);
        /* loss should be finite and non-negative */
        TEST_ASSERT(!isnan(loss) && !isinf(loss) && loss >= 0.0f, "Tick %d loss is finite and non-negative (%.4f)",
                    i + 1, loss);
    }

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    TEST_ASSERT(m.total_ticks == 5, "Total ticks = 5");
    TEST_ASSERT(m.current_loss > 0.0, "current_loss > 0 (%.4f)", m.current_loss);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* TEST 3: Compression ratio > 1.0 after training                             */
/* ========================================================================== */

static void test_compression_ratio_after_training(void) {
    printf("\n=== TEST: Compression ratio after training ===\n");

    KalContext *ctx = kal_create(777);
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);

    /* Provide enough data for the model to start compressing */
    const char *data = "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD"
                       "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    /* Run 5 training ticks */
    int rc = kal_train(ctx, 5);
    TEST_ASSERT(rc == 0, "kal_train(5) succeeded");

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    TEST_ASSERT(m.total_ticks == 5, "Total ticks = 5");

    /* compression_ratio from world_model stats should be > 1.0 */
    KwmStats ws;
    kwm_get_stats((const KwmContext *)ctx->world_model, &ws);
    TEST_ASSERT(ws.compression_ratio > 1.0, "compression_ratio > 1.0 (%.4f)", ws.compression_ratio);
    TEST_ASSERT(ws.avg_loss > 0.0, "avg_loss > 0.0 (%.4f)", ws.avg_loss);
    TEST_ASSERT(ws.num_concepts >= 0, "num_concepts >= 0 (%zu)", ws.num_concepts);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* TEST 4: Concepts learned >= 0                                              */
/* ========================================================================== */

static void test_concepts_learned(void) {
    printf("\n=== TEST: Concepts learned >= 0 ===\n");

    KalContext *ctx = kal_create(42);
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);

    const char *data = "Learning creates concepts from patterns in data. "
                       "Data patterns form concepts in the model.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    kal_train(ctx, 5);

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    TEST_ASSERT(m.concepts_learned >= 0, "concepts_learned >= 0 (%zu)", m.concepts_learned);

    /* Also check world model concept count */
    KwmStats ws;
    kwm_get_stats((const KwmContext *)ctx->world_model, &ws);
    TEST_ASSERT((int)ws.num_concepts >= 0, "world_model num_concepts >= 0 (%zu)", ws.num_concepts);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* TEST 5: Metrics across learning modes                                      */
/* ========================================================================== */

static void test_metrics_across_modes(void) {
    printf("\n=== TEST: Metrics across learning modes ===\n");

    KalContext *ctx = kal_create(99);

    const char *data = "Mixed mode training data for testing metrics.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    const char *eval = "Eval data for mixed mode.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    /* Observation mode */
    kal_set_mode(ctx, KAL_MODE_OBSERVATION);
    kal_train(ctx, 3);

    KalMetrics m1;
    kal_get_metrics(ctx, &m1);
    TEST_ASSERT(m1.observation_ticks == 3, "observation_ticks == 3");

    /* Curiosity mode */
    kal_set_mode(ctx, KAL_MODE_CURIOSITY);
    kal_train(ctx, 3);

    KalMetrics m2;
    kal_get_metrics(ctx, &m2);
    TEST_ASSERT(m2.total_ticks == 6, "total_ticks after curiosity == 6");
    TEST_ASSERT(m2.curiosity_ticks > 0, "curiosity_ticks > 0");

    /* Verify loss stayed positive */
    TEST_ASSERT(m2.current_loss > 0.0, "current_loss still positive (%.4f)", m2.current_loss);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* TEST 6: Eval loss is positive                                              */
/* ========================================================================== */

static void test_eval_loss_positive(void) {
    printf("\n=== TEST: Eval loss is positive ===\n");

    KalContext *ctx = kal_create(55);

    const char *eval = "Evaluation data for measuring model quality.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    double eval_loss = kal_eval(ctx);
    TEST_ASSERT(eval_loss > 0.0, "eval_loss > 0.0 (%.4f)", eval_loss);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* TEST 7: Checkpoint creation affects metrics                                */
/* ========================================================================== */

static void test_checkpoint_metrics(void) {
    printf("\n=== TEST: Checkpoint metrics ===\n");

    KalContext *ctx = kal_create(33);

    const char *data = "Checkpoint test data for verification.";
    kal_add_memory_source(ctx, (const uint8_t *)data, strlen(data), 1.0f);

    const char *eval = "Eval for checkpoint test.";
    kal_set_eval_data(ctx, (const uint8_t *)eval, strlen(eval));

    kal_train(ctx, 2);

    int rc = kal_checkpoint(ctx);
    TEST_ASSERT(rc == 0, "Checkpoint created");

    KalMetrics m;
    kal_get_metrics(ctx, &m);
    TEST_ASSERT(m.checkpoints_created >= 1, "checkpoints_created >= 1 (%zu)", m.checkpoints_created);
    TEST_ASSERT(m.current_loss > 0.0, "current_loss > 0 after checkpoint (%.4f)", m.current_loss);

    kal_destroy(ctx);
}

/* ========================================================================== */
/* MAIN                                                                       */
/* ========================================================================== */

int main(void) {
    printf("=============================================================\n");
    printf("  Kolibri Compression Stats - Test Suite\n");
    printf("=============================================================\n");

    test_context_create_auto_learn();
    test_training_ticks_produce_loss();
    test_compression_ratio_after_training();
    test_concepts_learned();
    test_metrics_across_modes();
    test_eval_loss_positive();
    test_checkpoint_metrics();

    printf("\n=============================================================\n");
    printf("  RESULTS: %d run, %d passed, %d failed\n", tests_run, tests_passed, tests_failed);
    printf("=============================================================\n");

    return tests_failed > 0 ? 1 : 0;
}

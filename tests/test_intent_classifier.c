/*
 * test_intent_classifier.c
 *
 * Unit тесты для intent classifier
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/intent_classifier.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message, ...)                                                                           \
    do {                                                                                                               \
        tests_run++;                                                                                                   \
        if (condition) {                                                                                               \
            tests_passed++;                                                                                            \
            printf("  ✓ " message "\n", ##__VA_ARGS__);                                                                \
        } else {                                                                                                       \
            tests_failed++;                                                                                            \
            printf("  ✗ FAILED: " message "\n", ##__VA_ARGS__);                                                        \
        }                                                                                                              \
    } while (0)

#define TEST_START(name) printf("\n=== TEST: %s ===\n", name)

/* ============================================================================
 * TEST: Initialization
 * ============================================================================ */

static void test_initialization(void) {
    TEST_START("Initialization");

    KolibriIntentClassifier classifier;
    int result = kolibri_ic_init(&classifier);

    TEST_ASSERT(result == 0, "Classifier initialized successfully");
    TEST_ASSERT(classifier.num_patterns > 0, "Has patterns loaded");
    TEST_ASSERT(classifier.total_queries == 0, "Query counter at zero");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Factual Query
 * ============================================================================ */

static void test_fact_query(void) {
    TEST_START("Factual Query");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "сколько будет 2+2?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_QUERY_FACT || result.primary_intent == KIC_INTENT_MATH_PROBLEM,
                "Detected as fact or math query");
    TEST_ASSERT(result.confidence >= KIC_MIN_CONFIDENCE, "Confidence above threshold (%.2f)", result.confidence);

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Definition Query
 * ============================================================================ */

static void test_definition_query(void) {
    TEST_START("Definition Query");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "что такое квантовая физика?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_QUERY_DEFINITION, "Detected as definition query");
    TEST_ASSERT(result.requires_knowledge == 1, "Requires knowledge");

    printf("  Intent: %s\n", kolibri_ic_intent_name(result.primary_intent));

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Comparison Query
 * ============================================================================ */

static void test_comparison_query(void) {
    TEST_START("Comparison Query");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "какая разница между Python и C?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_QUERY_COMPARISON, "Detected as comparison query");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Cause Query
 * ============================================================================ */

static void test_cause_query(void) {
    TEST_START("Cause Query");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "почему небо голубое?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_QUERY_CAUSE, "Detected as cause query");
    TEST_ASSERT(result.requires_reasoning == 1, "Requires reasoning");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Logic Puzzle
 * ============================================================================ */

static void test_logic_puzzle(void) {
    TEST_START("Logic Puzzle");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "реши логическую задачу про Сократа", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_LOGIC_PUZZLE, "Detected as logic puzzle");
    TEST_ASSERT(strcmp(result.recommended_method, "logic_solver") == 0, "Recommends logic solver");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Math Problem
 * ============================================================================ */

static void test_math_problem(void) {
    TEST_START("Math Problem");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "вычисли интеграл от x^2", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_MATH_PROBLEM, "Detected as math problem");
    TEST_ASSERT(strcmp(result.recommended_method, "math_solver") == 0, "Recommends math solver");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Counterfactual
 * ============================================================================ */

static void test_counterfactual(void) {
    TEST_START("Counterfactual");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "что если бы Земля была плоской?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_COUNTERFACTUAL, "Detected as counterfactual");
    TEST_ASSERT(result.confidence >= 0.8, "High confidence (%.2f)", result.confidence);

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Greeting
 * ============================================================================ */

static void test_greeting(void) {
    TEST_START("Greeting");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    int ret = kolibri_ic_classify(&classifier, "привет, как дела?", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    TEST_ASSERT(result.primary_intent == KIC_INTENT_GREETING, "Detected as greeting");
    TEST_ASSERT(result.requires_reasoning == 0, "No reasoning needed");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Fast Classification
 * ============================================================================ */

static void test_fast_classification(void) {
    TEST_START("Fast Classification");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntent intent = kolibri_ic_classify_fast(&classifier, "спасибо!");

    TEST_ASSERT(intent == KIC_INTENT_THANKS, "Fast classification works");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Unknown Intent
 * ============================================================================ */

static void test_unknown_intent(void) {
    TEST_START("Unknown Intent");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    KolibriIntentResult result;
    /* Gibberish query */
    int ret = kolibri_ic_classify(&classifier, "xyzzy plugh abracadabra", &result);

    TEST_ASSERT(ret == 0, "Classification succeeded");
    /* Should be unknown or low confidence */
    printf("  Intent: %s (confidence: %.2f)\n", kolibri_ic_intent_name(result.primary_intent), result.confidence);

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * TEST: Statistics
 * ============================================================================ */

static void test_statistics(void) {
    TEST_START("Statistics");

    KolibriIntentClassifier classifier;
    kolibri_ic_init(&classifier);

    /* Make several classifications */
    KolibriIntentResult result;
    kolibri_ic_classify(&classifier, "привет", &result);
    kolibri_ic_classify(&classifier, "что такое AI?", &result);
    kolibri_ic_classify(&classifier, "спасибо", &result);

    TEST_ASSERT(classifier.total_queries == 3, "Query count correct");
    TEST_ASSERT(classifier.classification_times_us > 0, "Has timing data");

    double avg_time = (double)classifier.classification_times_us / classifier.total_queries;
    printf("  Average classification time: %.1f µs\n", avg_time);
    TEST_ASSERT(avg_time < 1000.0, "Classification is fast (<1ms)");

    kolibri_ic_destroy(&classifier);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║     Kolibri Intent Classifier - Unit Test Suite          ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    test_initialization();
    test_fact_query();
    test_definition_query();
    test_comparison_query();
    test_cause_query();
    test_logic_puzzle();
    test_math_problem();
    test_counterfactual();
    test_greeting();
    test_fast_classification();
    test_unknown_intent();
    test_statistics();

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                           ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Total:  %4d                                           ║\n", tests_run);
    printf("║  Passed: %4d                                           ║\n", tests_passed);
    printf("║  Failed: %4d                                           ║\n", tests_failed);
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    return tests_failed > 0 ? 1 : 0;
}

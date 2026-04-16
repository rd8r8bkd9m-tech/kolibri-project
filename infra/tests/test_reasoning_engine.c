/*
 * test_reasoning_engine.c
 *
 * Unit тесты для reasoning engine
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reasoning_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TEST UTILITIES
 * ============================================================================ */

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
 * TEST: Инициализация
 * ============================================================================ */

static void test_initialization(void) {
    TEST_START("Initialization");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    config.enable_deductive = 1;
    config.enable_inductive = 1;
    config.enable_abductive = 1;
    config.enable_analogical = 1;
    config.enable_counterfactual = 1;
    config.min_confidence_threshold = 0.5;
    config.max_chain_length = 10;
    config.max_hypotheses = 5;

    int result = kolibri_re_init(&config);
    TEST_ASSERT(result == 0, "Engine initialized successfully");

    /* Add test facts */
    result = kolibri_re_add_fact(&config, "Все люди смертны", 1.0, "axiom");
    TEST_ASSERT(result == 0, "Added fact: Все люди смертны");

    result = kolibri_re_add_fact(&config, "Сократ - человек", 1.0, "axiom");
    TEST_ASSERT(result == 0, "Added fact: Сократ - человек");

    result = kolibri_re_add_fact(&config, "Земля вращается вокруг Солнца", 0.95, "science");
    TEST_ASSERT(result == 0, "Added fact: Земля вращается вокруг Солнца");

    /* Add test rules */
    result = kolibri_re_add_rule(&config, "человек", "смертен", 3, 0.9, "logic");
    TEST_ASSERT(result == 0, "Added rule: человек → смертен");
}

/* ============================================================================
 * TEST: Modus Ponens
 * ============================================================================ */

static void test_modus_ponens(void) {
    TEST_START("Modus Ponens");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    config.enable_deductive = 1;
    kolibri_re_init(&config);

    kolibri_re_add_fact(&config, "Сократ - человек", 1.0, "test");
    kolibri_re_add_rule(&config, "человек", "смертен", 3, 0.95, "logic");

    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Сократ смертен?", &config, &result);

    TEST_ASSERT(ret == 0, "Deductive reasoning completed");
    TEST_ASSERT(result.confidence > 0.0, "Result has confidence score");
    TEST_ASSERT(result.chain.num_steps > 0, "Reasoning chain has steps");
    TEST_ASSERT(strlen(result.answer) > 0, "Result has answer");

    printf("  Answer: %s\n", result.answer);
}

/* ============================================================================
 * TEST: Hypothetical Syllogism
 * ============================================================================ */

static void test_hypothetical_syllogism(void) {
    TEST_START("Hypothetical Syllogism");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_hypothetical_syllogism("дождь -> мокро", "мокро -> скользко", &config, &result);

    TEST_ASSERT(ret == 0, "Hypothetical syllogism completed");
    TEST_ASSERT(result.primary_type == KRE_REASONING_HYPOTHETICAL_SYLLOGISM, "Correct reasoning type");
    TEST_ASSERT(result.confidence > 0.0, "Has confidence");
    TEST_ASSERT(result.chain.num_steps == 2, "Chain has 2 steps");
    TEST_ASSERT(strlen(result.answer) > 0, "Has answer");

    printf("  Answer: %s\n", result.answer);
    TEST_ASSERT(strstr(result.answer, "дождь") != NULL, "Answer mentions 'дождь'");
    TEST_ASSERT(strstr(result.answer, "скользко") != NULL, "Answer mentions 'скользко'");
}

/* ============================================================================
 * TEST: Constructive Dilemma
 * ============================================================================ */

static void test_constructive_dilemma(void) {
    TEST_START("Constructive Dilemma");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_constructive_dilemma("учиться -> знать", "практиковаться -> уметь",
                                              "учиться или практиковаться", &config, &result);

    TEST_ASSERT(ret == 0, "Constructive dilemma completed");
    TEST_ASSERT(result.primary_type == KRE_REASONING_CONSTRUCTIVE_DILEMMA, "Correct reasoning type");
    TEST_ASSERT(result.chain.num_steps == 3, "Chain has 3 steps");

    printf("  Answer: %s\n", result.answer);
}

/* ============================================================================
 * TEST: Disjunctive Syllogism
 * ============================================================================ */

static void test_disjunctive_syllogism(void) {
    TEST_START("Disjunctive Syllogism");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_disjunctive_syllogism("идет дождь или снег", "не идет дождь", &config, &result);

    TEST_ASSERT(ret == 0, "Disjunctive syllogism completed");
    TEST_ASSERT(result.primary_type == KRE_REASONING_DISJUNCTIVE_SYLLOGISM, "Correct reasoning type");
    TEST_ASSERT(result.confidence >= 0.85, "High confidence");

    printf("  Answer: %s\n", result.answer);
}

/* ============================================================================
 * TEST: Resolution
 * ============================================================================ */

static void test_resolution(void) {
    TEST_START("Resolution");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_resolution("P или Q", "не P или R", &config, &result);

    TEST_ASSERT(ret == 0, "Resolution completed");
    TEST_ASSERT(result.primary_type == KRE_REASONING_RESOLUTION, "Correct reasoning type");
    TEST_ASSERT(result.chain.num_steps == 2, "Chain has 2 steps");

    printf("  Answer: %s\n", result.answer);
}

/* ============================================================================
 * TEST: Biconditional
 * ============================================================================ */

static void test_biconditional(void) {
    TEST_START("Biconditional");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_biconditional("P тогда и только тогда, когда Q", "P", &config, &result);

    TEST_ASSERT(ret == 0, "Biconditional reasoning completed");
    TEST_ASSERT(result.primary_type == KRE_REASONING_BICONDITIONAL, "Correct reasoning type");
    TEST_ASSERT(result.confidence >= 0.9, "High confidence for biconditional");

    printf("  Answer: %s\n", result.answer);
}

/* ============================================================================
 * TEST: Type Names and Descriptions
 * ============================================================================ */

static void test_type_names(void) {
    TEST_START("Type Names and Descriptions");

    for (int i = 0; i < KRE_REASONING_COUNT; i++) {
        const char *name = kolibri_re_type_name((KolibriReasoningType)i);
        const char *desc = kolibri_re_type_desc((KolibriReasoningType)i);

        TEST_ASSERT(name != NULL && strlen(name) > 0, "Type name exists for type %d", i);
        TEST_ASSERT(desc != NULL && strlen(desc) > 0, "Type description exists for type %d", i);
    }

    /* Test boundary */
    const char *invalid_name = kolibri_re_type_name(KRE_REASONING_COUNT + 100);
    TEST_ASSERT(strcmp(invalid_name, "Unknown") == 0, "Invalid type returns 'Unknown'");
}

/* ============================================================================
 * TEST: Performance
 * ============================================================================ */

static void test_performance(void) {
    TEST_START("Performance");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    /* Add multiple facts */
    for (int i = 0; i < 50; i++) {
        char fact[256];
        snprintf(fact, sizeof(fact), "Факт номер %d", i);
        kolibri_re_add_fact(&config, fact, 0.9, "test");
    }

    /* Measure reasoning time */
    KolibriReasoningResult result;
    kolibri_re_deductive("Тестовый запрос", &config, &result);

    TEST_ASSERT(result.reasoning_time_ms < 100.0, "Reasoning completes in < 100ms (%.2fms)", result.reasoning_time_ms);

    printf("  Reasoning time: %.2fms\n", result.reasoning_time_ms);
}

/* ============================================================================
 * TEST: Edge Cases
 * ============================================================================ */

static void test_edge_cases(void) {
    TEST_START("Edge Cases");

    KolibriREConfig config;
    memset(&config, 0, sizeof(config));
    kolibri_re_init(&config);

    KolibriReasoningResult result;

    /* Empty query */
    int ret = kolibri_re_deductive("", &config, &result);
    TEST_ASSERT(ret == 0 || strlen(result.answer) > 0, "Handles empty query gracefully");

    /* Very long query */
    char long_query[2000];
    memset(long_query, 'A', sizeof(long_query) - 1);
    long_query[sizeof(long_query) - 1] = '\0';

    ret = kolibri_re_deductive(long_query, &config, &result);
    TEST_ASSERT(ret == 0, "Handles very long query");

    /* Special characters */
    ret = kolibri_re_deductive("Тест с символами: !@#$%^&*()", &config, &result);
    TEST_ASSERT(ret == 0, "Handles special characters");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║     Kolibri Reasoning Engine - Unit Test Suite           ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    test_initialization();
    test_modus_ponens();
    test_hypothetical_syllogism();
    test_constructive_dilemma();
    test_disjunctive_syllogism();
    test_resolution();
    test_biconditional();
    test_type_names();
    test_performance();
    test_edge_cases();

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                           ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Total:  %4d                                           ║\n", tests_run);
    printf("║  Passed: %4d                                           ║\n", tests_passed);
    printf("║  Failed: %4d                                           ║\n", tests_failed);
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    return tests_failed > 0 ? 1 : 0;
}

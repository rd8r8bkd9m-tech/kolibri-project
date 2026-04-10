/*
 * test_encoding_pipeline.c
 *
 * Unit тесты для encoding pipeline module
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#include "kolibri/encoding_pipeline.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                                                                                \
    do {                                                                                                               \
        tests_run++;                                                                                                   \
        if (condition) {                                                                                               \
            tests_passed++;                                                                                            \
            printf("  ✓ %s\n", message);                                                                               \
        } else {                                                                                                       \
            tests_failed++;                                                                                            \
            printf("  ✗ FAILED: %s\n", message);                                                                       \
        }                                                                                                              \
    } while (0)

#define TEST_START(name) printf("\n=== TEST: %s ===\n", name)

/* ============================================================================
 * TEST: Pipeline Creation
 * ============================================================================ */

static void test_pipeline_create(void) {
    TEST_START("Pipeline Creation");

    KolibriEncodingPipeline *pipeline = NULL;
    int result = kolibri_pipeline_create(&pipeline, NULL);

    TEST_ASSERT(result == 0, "Pipeline created with default config");
    TEST_ASSERT(pipeline != NULL, "Pipeline pointer is valid");

    kolibri_pipeline_destroy(pipeline);
}

static void test_pipeline_create_with_config(void) {
    TEST_START("Pipeline Creation with Config");

    KolibriEncodingConfig config;
    config.enable_digits = 1;
    config.enable_phonemes = 1;
    config.enable_semantic = 1;
    config.semantic_learn = 1;
    config.semantic_generations = 100;

    KolibriEncodingPipeline *pipeline = NULL;
    int result = kolibri_pipeline_create(&pipeline, &config);

    TEST_ASSERT(result == 0, "Pipeline created with custom config");
    TEST_ASSERT(pipeline != NULL, "Pipeline pointer is valid");
    TEST_ASSERT(pipeline->config.enable_digits == 1, "Digits enabled");
    TEST_ASSERT(pipeline->config.enable_phonemes == 1, "Phonemes enabled");
    TEST_ASSERT(pipeline->config.enable_semantic == 1, "Semantic enabled");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Single Word Encoding
 * ============================================================================ */

static void test_encode_single_word_latin(void) {
    TEST_START("Encode Single Word (Latin)");

    KolibriEncodingPipeline *pipeline = NULL;
    kolibri_pipeline_create(&pipeline, NULL);

    KolibriEncodingResult result;
    int ret = kolibri_pipeline_encode_word(pipeline, "hello", &result);

    TEST_ASSERT(ret == 0, "Encoding succeeded");
    TEST_ASSERT(strcmp(result.word, "hello") == 0, "Word preserved");
    TEST_ASSERT(result.is_latin == 1, "Latin script detected");
    TEST_ASSERT(result.is_cyrillic == 0, "Not Cyrillic");
    TEST_ASSERT(result.confidence >= 0.0 && result.confidence <= 1.0, "Confidence in range");

    kolibri_pipeline_destroy(pipeline);
}

static void test_encode_single_word_cyrillic(void) {
    TEST_START("Encode Single Word (Cyrillic)");

    KolibriEncodingPipeline *pipeline = NULL;
    kolibri_pipeline_create(&pipeline, NULL);

    KolibriEncodingResult result;
    int ret = kolibri_pipeline_encode_word(pipeline, "привет", &result);

    TEST_ASSERT(ret == 0, "Encoding succeeded");
    TEST_ASSERT(strcmp(result.word, "привет") == 0, "Word preserved");
    TEST_ASSERT(result.is_cyrillic == 1, "Cyrillic script detected");
    TEST_ASSERT(result.is_latin == 0, "Not Latin");
    TEST_ASSERT(result.confidence >= 0.0 && result.confidence <= 1.0, "Confidence in range");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Full Text Encoding
 * ============================================================================ */

static void test_encode_full_text(void) {
    TEST_START("Encode Full Text");

    KolibriEncodingPipeline *pipeline = NULL;
    kolibri_pipeline_create(&pipeline, NULL);

    KolibriEncodingResult results[10];
    size_t out_count = 0;
    int ret = kolibri_pipeline_encode_text(pipeline, "hello world test", results, 10, &out_count);

    TEST_ASSERT(ret == 0, "Full text encoding succeeded");
    TEST_ASSERT(out_count > 0, "Words extracted");
    TEST_ASSERT(results[0].confidence > 0.0, "Confidence calculated");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Semantic Pattern Learning
 * ============================================================================ */

static void test_semantic_learning(void) {
    TEST_START("Semantic Pattern Learning");

    KolibriEncodingPipeline *pipeline = NULL;
    KolibriEncodingConfig config;
    config.enable_digits = 1;
    config.enable_phonemes = 1;
    config.enable_semantic = 1;
    config.semantic_learn = 1;
    config.semantic_generations = 50;
    kolibri_pipeline_create(&pipeline, &config);

    /* Encode word to trigger learning */
    KolibriEncodingResult result;
    int ret = kolibri_pipeline_encode_word(pipeline, "kolibri", &result);

    TEST_ASSERT(ret == 0, "Encoding succeeded");
    TEST_ASSERT(pipeline->pattern_count > 0, "Pattern learned");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Multiple Encodings
 * ============================================================================ */

static void test_multiple_encodings(void) {
    TEST_START("Multiple Encodings");

    KolibriEncodingPipeline *pipeline = NULL;
    kolibri_pipeline_create(&pipeline, NULL);

    const char *words[] = {"hello", "world", "test", "kolibri", "AI"};
    int num_words = sizeof(words) / sizeof(words[0]);

    for (int i = 0; i < num_words; i++) {
        KolibriEncodingResult result;
        int ret = kolibri_pipeline_encode_word(pipeline, words[i], &result);
        if (ret == 0) {
            tests_run++;
            tests_passed++;
            printf("  ✓ Encoding word '%s' succeeded\n", words[i]);
        } else {
            tests_run++;
            tests_failed++;
            printf("  ✗ FAILED: Encoding word '%s'\n", words[i]);
        }
    }

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Error Handling
 * ============================================================================ */

static void test_error_handling(void) {
    TEST_START("Error Handling");

    KolibriEncodingPipeline *pipeline = NULL;
    KolibriEncodingResult result;

    /* Test with null pipeline */
    int ret = kolibri_pipeline_encode_word(NULL, "test", &result);
    TEST_ASSERT(ret == -1, "Null pipeline handled");

    /* Test with null word */
    kolibri_pipeline_create(&pipeline, NULL);
    ret = kolibri_pipeline_encode_word(pipeline, NULL, &result);
    TEST_ASSERT(ret == -1, "Null word handled");

    /* Test with null result */
    ret = kolibri_pipeline_encode_word(pipeline, "test", NULL);
    TEST_ASSERT(ret == -1, "Null result handled");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * TEST: Confidence Scoring
 * ============================================================================ */

static void test_confidence_scoring(void) {
    TEST_START("Confidence Scoring");

    KolibriEncodingPipeline *pipeline = NULL;
    kolibri_pipeline_create(&pipeline, NULL);

    KolibriEncodingResult result1, result2;
    kolibri_pipeline_encode_word(pipeline, "the", &result1);
    kolibri_pipeline_encode_word(pipeline, "kolibri", &result2);

    /* Both should have valid confidence */
    TEST_ASSERT(result1.confidence > 0.0, "Common word has confidence");
    TEST_ASSERT(result2.confidence > 0.0, "New word has confidence");

    kolibri_pipeline_destroy(pipeline);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Kolibri Encoding Pipeline Tests\n");
    printf("========================================\n");

    test_pipeline_create();
    test_pipeline_create_with_config();
    test_encode_single_word_latin();
    test_encode_single_word_cyrillic();
    test_encode_full_text();
    test_semantic_learning();
    test_multiple_encodings();
    test_error_handling();
    test_confidence_scoring();

    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}

/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Tests for Corpus Learning Module
 */

#include "kolibri/corpus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_corpus_init(void) {
    printf("test_corpus_init... ");
    
    KolibriCorpusContext ctx;
    int result = k_corpus_init(&ctx, 0, 0);
    
    assert(result == 0);
    assert(ctx.store.patterns != NULL);
    assert(ctx.store.words != NULL);
    assert(ctx.store.count == 0);
    assert(ctx.store.capacity > 0);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

static void test_tokenize(void) {
    printf("test_tokenize... ");
    
    const char *text = "Привет мир! Это тест токенизации.";
    char **tokens = NULL;
    size_t token_count = 0;
    
    int result = k_corpus_tokenize(text, strlen(text), &tokens, &token_count);
    
    assert(result == 0);
    assert(tokens != NULL);
    assert(token_count == 5); /* Привет, мир, Это, тест, токенизации */
    
    printf("tokens: %zu... ", token_count);
    
    k_corpus_free_tokens(tokens, token_count);
    
    printf("OK\n");
}

static void test_store_pattern(void) {
    printf("test_store_pattern... ");
    
    KolibriCorpusContext ctx;
    k_corpus_init(&ctx, 0, 0);
    
    KolibriSemanticPattern pattern;
    k_semantic_pattern_init(&pattern);
    strncpy(pattern.word, "тест", sizeof(pattern.word) - 1);
    pattern.context_weight = 0.5;
    
    int result = k_corpus_store_pattern(&ctx, "тест", &pattern);
    assert(result == 0);
    assert(ctx.store.count == 1);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

static void test_find_pattern(void) {
    printf("test_find_pattern... ");
    
    KolibriCorpusContext ctx;
    k_corpus_init(&ctx, 0, 0);
    
    KolibriSemanticPattern pattern;
    k_semantic_pattern_init(&pattern);
    strncpy(pattern.word, "кот", sizeof(pattern.word) - 1);
    pattern.context_weight = 0.7;
    
    k_corpus_store_pattern(&ctx, "кот", &pattern);
    
    const KolibriSemanticPattern *found = k_corpus_find_pattern(&ctx, "кот");
    assert(found != NULL);
    assert(strcmp(found->word, "кот") == 0);
    
    found = k_corpus_find_pattern(&ctx, "собака");
    assert(found == NULL);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

static void test_merge_pattern(void) {
    printf("test_merge_pattern... ");
    
    KolibriCorpusContext ctx;
    k_corpus_init(&ctx, 0, 0);
    
    /* Добавляем первый паттерн */
    KolibriSemanticPattern pattern1;
    k_semantic_pattern_init(&pattern1);
    pattern1.context_weight = 0.5;
    k_corpus_store_pattern(&ctx, "слово", &pattern1);
    
    /* Сливаем с новым паттерном */
    KolibriSemanticPattern pattern2;
    k_semantic_pattern_init(&pattern2);
    pattern2.context_weight = 0.7;
    
    int result = k_corpus_merge_pattern(&ctx, "слово", &pattern2);
    assert(result == 0);
    assert(ctx.store.count == 1); /* Должен остаться один паттерн */
    
    const KolibriSemanticPattern *merged = k_corpus_find_pattern(&ctx, "слово");
    assert(merged != NULL);
    printf("merged weight = %.3f... ", merged->context_weight);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

static void test_learn_document(void) {
    printf("test_learn_document... ");
    
    KolibriCorpusContext ctx;
    k_corpus_init(&ctx, 0, 4); /* Маленькое окно для теста */
    
    const char *text = "Кот сидит на крыше. Кошка спит рядом с котом.";
    
    int result = k_corpus_learn_document(&ctx, text, strlen(text));
    assert(result == 0);
    assert(ctx.stats.total_documents == 1);
    assert(ctx.stats.total_tokens > 0);
    assert(ctx.stats.unique_patterns > 0);
    
    printf("learned %zu patterns from %zu tokens... ",
           ctx.stats.unique_patterns, ctx.stats.total_tokens);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

static void test_save_load_patterns(void) {
    printf("test_save_load_patterns... ");
    
    KolibriCorpusContext ctx1, ctx2;
    k_corpus_init(&ctx1, 0, 0);
    k_corpus_init(&ctx2, 0, 0);
    
    /* Создаём несколько паттернов */
    KolibriSemanticPattern pattern;
    k_semantic_pattern_init(&pattern);
    
    pattern.context_weight = 0.5;
    k_corpus_store_pattern(&ctx1, "первый", &pattern);
    
    pattern.context_weight = 0.7;
    k_corpus_store_pattern(&ctx1, "второй", &pattern);
    
    /* Сохраняем */
    const char *tmpfile = "/tmp/test_patterns.bin";
    int result = k_corpus_save_patterns(&ctx1, tmpfile);
    assert(result == 0);
    printf("saved %zu patterns... ", ctx1.store.count);
    
    /* Загружаем */
    result = k_corpus_load_patterns(&ctx2, tmpfile);
    assert(result == 0);
    assert(ctx2.store.count == ctx1.store.count);
    printf("loaded %zu patterns... ", ctx2.store.count);
    
    /* Проверяем что паттерны загрузились */
    const KolibriSemanticPattern *p = k_corpus_find_pattern(&ctx2, "первый");
    assert(p != NULL);
    
    k_corpus_free(&ctx1);
    k_corpus_free(&ctx2);
    
    printf("OK\n");
}

static void test_get_stats(void) {
    printf("test_get_stats... ");
    
    KolibriCorpusContext ctx;
    k_corpus_init(&ctx, 0, 0);
    
    const char *text = "Тестовый документ для проверки статистики.";
    k_corpus_learn_document(&ctx, text, strlen(text));
    
    const KolibriCorpusStats *stats = k_corpus_get_stats(&ctx);
    assert(stats != NULL);
    assert(stats->total_documents == 1);
    assert(stats->total_tokens > 0);
    
    printf("stats: docs=%zu, tokens=%zu... ",
           stats->total_documents, stats->total_tokens);
    
    k_corpus_free(&ctx);
    
    printf("OK\n");
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║       CORPUS LEARNING TESTS (v2.0 Phase 1.3)             ║\n");
    printf("║   Тестирование обучения на текстовых корпусах            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    test_corpus_init();
    test_tokenize();
    test_store_pattern();
    test_find_pattern();
    test_merge_pattern();
    test_learn_document();
    test_save_load_patterns();
    test_get_stats();
    
    printf("\n✓ All corpus learning tests passed!\n");
    printf("\nSTATUS: Phase 1.3 (Corpus Learning) - INITIAL IMPLEMENTATION\n");
    printf("FEATURES:\n");
    printf("  ✓ Text tokenization\n");
    printf("  ✓ Pattern storage and retrieval\n");
    printf("  ✓ Incremental pattern merging\n");
    printf("  ✓ Document learning with context\n");
    printf("  ✓ Pattern persistence (save/load)\n");
    printf("  ✓ Learning statistics\n");
    printf("\nNEXT STEPS:\n");
    printf("  1. Optimize tokenization (Unicode support)\n");
    printf("  2. Parallel document processing\n");
    printf("  3. SQLite integration for large corpora\n");
    printf("  4. Integration with generation module\n");
    
    return 0;
}

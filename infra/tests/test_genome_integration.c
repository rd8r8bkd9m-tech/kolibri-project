/*
 * Тест интеграции Genome API
 * Проверяет: kg_open, kg_append, kg_verify_file, kg_get_stats, kg_close, WAL
 *
 * ВАЖНО: payload в genome должен содержать ТОЛЬКО цифры (проверка payload_is_digits).
 */

#include "kolibri/genome.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_HMAC_KEY "test_genome_hmac_key_64b______________________________"

static const char *TEST_GENOME_PATH = "/tmp/kolibri_test_genome.bin";

static void cleanup(void) {
    unlink(TEST_GENOME_PATH);
    unlink("/tmp/kolibri_test_genome.bin.wal");
}

/* Callback for block iteration */
static int count_blocks_callback(const ReasonBlock *blk, void *user_data) {
    (void)blk;
    int *c = (int *)user_data;
    (*c)++;
    return 0;
}

/* 1. Создание и инициализация KolibriGenome */
static void test_open_close(void) {
    printf("test_open_close... ");
    KolibriGenome ctx;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);

    cleanup();
    int ret = kg_open(&ctx, TEST_GENOME_PATH, key, key_len);
    assert(ret == 0);
    assert(ctx.file != NULL);
    assert(ctx.next_index == 0);
    assert(strcmp(ctx.path, TEST_GENOME_PATH) == 0);

    kg_close(&ctx);

    /* Файл должен существовать после kg_close */
    FILE *f = fopen(TEST_GENOME_PATH, "rb");
    assert(f != NULL);
    fclose(f);

    cleanup();
    printf("OK\n");
}

/* 2. Append нескольких событий */
static void test_append_events(void) {
    printf("test_append_events... ");
    KolibriGenome ctx;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);
    ReasonBlock block;

    cleanup();
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);

    /* Событие 1: CHAT_RESPONSE (payload — только цифры) */
    assert(kg_append(&ctx, "CHAT_RESPONSE", "73914285", &block) == 0);
    assert(block.index == 0);
    assert(block.timestamp > 0);
    assert(strncmp(block.event_type, "CHAT_RESPONSE", KOLIBRI_EVENT_TYPE_SIZE) == 0);

    /* Событие 2: HIGH_SURPRISE_LEARNING */
    assert(kg_append(&ctx, "HIGH_SURPRISE_LEARNING", "1029384756", &block) == 0);
    assert(block.index == 1);
    assert(block.timestamp > 0);
    assert(strncmp(block.event_type, "HIGH_SURPRISE_LEARNING", KOLIBRI_EVENT_TYPE_SIZE) == 0);

    /* Событие 3: FORMULA_EVOLUTION */
    assert(kg_append(&ctx, "FORMULA_EVOLUTION", "314159265358979", &block) == 0);
    assert(block.index == 2);
    assert(block.timestamp > 0);
    assert(strncmp(block.event_type, "FORMULA_EVOLUTION", KOLIBRI_EVENT_TYPE_SIZE) == 0);

    kg_close(&ctx);
    printf("OK\n");
}

/* 3. Чтение и верификация файла */
static void test_verify_and_read_blocks(void) {
    printf("test_verify_and_read_blocks... ");
    KolibriGenome ctx;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);
    ReasonBlock block;

    /* Сначала запишем данные */
    cleanup();
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);
    assert(kg_append(&ctx, "CHAT_RESPONSE", "73914285", &block) == 0);
    assert(kg_append(&ctx, "HIGH_SURPRISE_LEARNING", "1029384756", &block) == 0);
    assert(kg_append(&ctx, "FORMULA_EVOLUTION", "314159265358979", &block) == 0);
    kg_close(&ctx);

    /* Верификация файла */
    assert(kg_verify_file(TEST_GENOME_PATH, key, key_len) == 0);

    /* Откроем и прочитаем блоки по индексу */
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);

    ReasonBlock read_block;
    assert(kg_read_block(&ctx, 0, &read_block) == 0);
    assert(read_block.index == 0);
    assert(strncmp(read_block.event_type, "CHAT_RESPONSE", KOLIBRI_EVENT_TYPE_SIZE) == 0);
    assert(strstr(read_block.payload, "73914285") != NULL);

    assert(kg_read_block(&ctx, 1, &read_block) == 0);
    assert(read_block.index == 1);
    assert(strncmp(read_block.event_type, "HIGH_SURPRISE_LEARNING", KOLIBRI_EVENT_TYPE_SIZE) == 0);

    assert(kg_read_block(&ctx, 2, &read_block) == 0);
    assert(read_block.index == 2);
    assert(strncmp(read_block.event_type, "FORMULA_EVOLUTION", KOLIBRI_EVENT_TYPE_SIZE) == 0);

    /* Чтение несуществующего блока должно вернуть ошибку */
    assert(kg_read_block(&ctx, 99, &read_block) != 0);

    kg_close(&ctx);
    cleanup();
    printf("OK\n");
}

/* 4. Статистика (total_blocks, file_size) */
static void test_stats(void) {
    printf("test_stats... ");
    KolibriGenome ctx;
    KolibriGenomeStats stats;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);
    ReasonBlock block;

    cleanup();
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);

    /* Статистика пустого генома */
    assert(kg_get_stats(&ctx, &stats) == 0);
    assert(stats.total_blocks == 0);
    assert(stats.file_size_bytes == 0);
    assert(stats.integrity_valid == 1);

    /* Добавим блоки */
    assert(kg_append(&ctx, "CHAT_RESPONSE", "73914285", &block) == 0);
    assert(kg_append(&ctx, "HIGH_SURPRISE_LEARNING", "1029384756", &block) == 0);
    assert(kg_append(&ctx, "FORMULA_EVOLUTION", "314159265358979", &block) == 0);

    assert(kg_get_stats(&ctx, &stats) == 0);
    assert(stats.total_blocks == 3);
    assert(stats.first_timestamp > 0);
    assert(stats.last_timestamp >= stats.first_timestamp);
    assert(stats.integrity_valid == 1);
    assert(stats.file_size_bytes > 0);

    kg_close(&ctx);
    cleanup();
    printf("OK\n");
}

/* 5. WAL (write-ahead logging) */
static void test_wal(void) {
    printf("test_wal... ");
    KolibriGenome ctx;
    KolibriGenomeStats stats;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);
    ReasonBlock block;

    cleanup();
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);

    /* Включаем WAL */
    assert(kg_wal_enable(&ctx) == 0);
    assert(ctx.wal_enabled == 1);
    assert(ctx.wal_file != NULL);

    /* Записываем через WAL */
    assert(kg_stream_append(&ctx, "CHAT_RESPONSE", "73914285", &block) == 0);
    assert(kg_stream_append(&ctx, "HIGH_SURPRISE_LEARNING", "1029384756", &block) == 0);

    /* WAL entries должны быть > 0 */
    assert(ctx.wal_entries == 2);

    /* Checkpoint — перенос из WAL в основной файл */
    int checkpoint_result = kg_wal_checkpoint(&ctx);
    assert(checkpoint_result >= 0); /* возвращает кол-во записей или 0 */

    /* Проверяем статистику после checkpoint */
    assert(kg_get_stats(&ctx, &stats) == 0);
    assert(stats.total_blocks == 2);

    /* Отключаем WAL */
    assert(kg_wal_disable(&ctx) == 0);
    assert(ctx.wal_enabled == 0);

    kg_close(&ctx);
    cleanup();
    printf("OK\n");
}

/* 6. Тест итератора блоков */
static void test_iterate_blocks(void) {
    printf("test_iterate_blocks... ");
    KolibriGenome ctx;
    const unsigned char *key = (const unsigned char *)TEST_HMAC_KEY;
    size_t key_len = strlen(TEST_HMAC_KEY);
    ReasonBlock block;

    cleanup();
    assert(kg_open(&ctx, TEST_GENOME_PATH, key, key_len) == 0);

    assert(kg_append(&ctx, "CHAT_RESPONSE", "111222", &block) == 0);
    assert(kg_append(&ctx, "FORMULA_EVOLUTION", "333444", &block) == 0);
    assert(kg_append(&ctx, "HIGH_SURPRISE_LEARNING", "555666", &block) == 0);

    int count = 0;
    int iter_result = kg_iterate_blocks(&ctx, count_blocks_callback, &count);
    assert(iter_result == 3); /* возвращает количество итерированных блоков */
    assert(count == 3);

    kg_close(&ctx);
    cleanup();
    printf("OK\n");
}

int main(void) {
    printf("=== Тест интеграции Genome API ===\n\n");

    test_open_close();
    test_append_events();
    test_verify_and_read_blocks();
    test_stats();
    test_wal();
    test_iterate_blocks();

    printf("\nВсе тесты интеграции Genome пройдены!\n");
    return 0;
}

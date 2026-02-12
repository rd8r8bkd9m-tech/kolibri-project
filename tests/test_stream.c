/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */
/*
 * Тест streaming API для сжатия/распаковки.
 * Проверяем: поток → сжатие → поток → распаковка = исходные данные.
 *
 * Сборка (из корня проекта):
 *   gcc -O2 -o build/test_stream tests/test_stream.c \
 *       -Ibackend/include -Lbuild -lkolibri_core \
 *       -lssl -lcrypto -lsqlite3 -lpthread -lm
 *   LD_LIBRARY_PATH=build ./build/test_stream
 */

#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* --- Динамический буфер для сбора вывода --- */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} DynBuf;

static void dynbuf_init(DynBuf *b) {
    b->data = NULL;
    b->size = 0;
    b->cap = 0;
}

static void dynbuf_free(DynBuf *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

static int dynbuf_write(void *user_data, const uint8_t *data, size_t size) {
    DynBuf *b = (DynBuf *)user_data;
    if (b->size + size > b->cap) {
        size_t new_cap = (b->cap == 0) ? 4096 : b->cap;
        while (new_cap < b->size + size) new_cap *= 2;
        uint8_t *p = (uint8_t *)realloc(b->data, new_cap);
        if (!p) return -1;
        b->data = p;
        b->cap = new_cap;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
    return 0;
}

/* --- Генерация тестовых данных --- */
static uint8_t *gen_test_data(size_t size) {
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return NULL;
    uint64_t state = 12345ULL;
    for (size_t i = 0; i < size; i++) {
        /* Полуслучайные данные: сжимаемые, но не тривиальные */
        state = state * 6364136223846793005ULL + 1;
        buf[i] = (uint8_t)((state >> 33) % 128 + 'A');
    }
    return buf;
}

/* --- Тест 1: Сжать всё за один write → распаковать за один write --- */
static void test_single_write(void) {
    printf("  test_single_write ... ");

    const size_t SZ = 50000;
    uint8_t *data = gen_test_data(SZ);
    assert(data);

    /* Сжатие */
    DynBuf compressed;
    dynbuf_init(&compressed);

    KolibriStream *cs = kolibri_stream_create(
        KOLIBRI_STREAM_COMPRESS, KOLIBRI_COMPRESS_LZCM, dynbuf_write, &compressed);
    assert(cs);

    KolibriStreamStatus st = kolibri_stream_write(cs, data, SZ);
    assert(st == KOLIBRI_STREAM_OK);

    st = kolibri_stream_finish(cs);
    assert(st == KOLIBRI_STREAM_DONE);

    KolibriCompressStats stats;
    kolibri_stream_stats(cs, &stats);
    printf("ratio=%.2f ", stats.compression_ratio);

    kolibri_stream_destroy(cs);

    assert(compressed.size > 0);
    assert(compressed.size < SZ); /* Данные должны сжиматься */

    /* Распаковка */
    DynBuf decompressed;
    dynbuf_init(&decompressed);

    KolibriStream *ds = kolibri_stream_create(
        KOLIBRI_STREAM_DECOMPRESS, 0, dynbuf_write, &decompressed);
    assert(ds);

    st = kolibri_stream_write(ds, compressed.data, compressed.size);
    assert(st == KOLIBRI_STREAM_DONE || st == KOLIBRI_STREAM_OK);

    kolibri_stream_destroy(ds);

    /* Проверка roundtrip */
    assert(decompressed.size == SZ);
    assert(memcmp(data, decompressed.data, SZ) == 0);

    dynbuf_free(&compressed);
    dynbuf_free(&decompressed);
    free(data);

    printf("✓\n");
}

/* --- Тест 2: Подача данных малыми порциями (по 1000 байт) --- */
static void test_chunked_write(void) {
    printf("  test_chunked_write ... ");

    const size_t SZ = 200000;
    const size_t CHUNK = 1000;
    uint8_t *data = gen_test_data(SZ);
    assert(data);

    /* Сжатие порциями */
    DynBuf compressed;
    dynbuf_init(&compressed);

    KolibriStream *cs = kolibri_stream_create(
        KOLIBRI_STREAM_COMPRESS, KOLIBRI_COMPRESS_LZCM, dynbuf_write, &compressed);
    assert(cs);

    for (size_t off = 0; off < SZ; off += CHUNK) {
        size_t n = (off + CHUNK <= SZ) ? CHUNK : (SZ - off);
        KolibriStreamStatus st = kolibri_stream_write(cs, data + off, n);
        assert(st == KOLIBRI_STREAM_OK);
    }

    KolibriStreamStatus st = kolibri_stream_finish(cs);
    assert(st == KOLIBRI_STREAM_DONE);

    KolibriCompressStats stats;
    kolibri_stream_stats(cs, &stats);
    printf("ratio=%.2f ", stats.compression_ratio);

    kolibri_stream_destroy(cs);

    /* Распаковка тоже порциями */
    DynBuf decompressed;
    dynbuf_init(&decompressed);

    KolibriStream *ds = kolibri_stream_create(
        KOLIBRI_STREAM_DECOMPRESS, 0, dynbuf_write, &decompressed);
    assert(ds);

    for (size_t off = 0; off < compressed.size; off += 512) {
        size_t n = (off + 512 <= compressed.size) ? 512 : (compressed.size - off);
        st = kolibri_stream_write(ds, compressed.data + off, n);
        if (st == KOLIBRI_STREAM_DONE) break;
        assert(st == KOLIBRI_STREAM_OK || st == KOLIBRI_STREAM_NEED_MORE);
    }

    kolibri_stream_destroy(ds);

    /* Проверка roundtrip */
    assert(decompressed.size == SZ);
    assert(memcmp(data, decompressed.data, SZ) == 0);

    dynbuf_free(&compressed);
    dynbuf_free(&decompressed);
    free(data);

    printf("✓\n");
}

/* --- Тест 3: Побайтовая подача (стресс-тест) --- */
static void test_byte_by_byte(void) {
    printf("  test_byte_by_byte (1KB) ... ");

    const size_t SZ = 1024;
    uint8_t *data = gen_test_data(SZ);
    assert(data);

    DynBuf compressed;
    dynbuf_init(&compressed);

    KolibriStream *cs = kolibri_stream_create(
        KOLIBRI_STREAM_COMPRESS, KOLIBRI_COMPRESS_LZCM, dynbuf_write, &compressed);
    assert(cs);

    for (size_t i = 0; i < SZ; i++) {
        KolibriStreamStatus st = kolibri_stream_write(cs, data + i, 1);
        assert(st == KOLIBRI_STREAM_OK);
    }
    KolibriStreamStatus st = kolibri_stream_finish(cs);
    assert(st == KOLIBRI_STREAM_DONE);
    kolibri_stream_destroy(cs);

    /* Распаковка побайтово */
    DynBuf decompressed;
    dynbuf_init(&decompressed);
    KolibriStream *ds = kolibri_stream_create(
        KOLIBRI_STREAM_DECOMPRESS, 0, dynbuf_write, &decompressed);
    assert(ds);

    for (size_t i = 0; i < compressed.size; i++) {
        st = kolibri_stream_write(ds, compressed.data + i, 1);
        if (st == KOLIBRI_STREAM_DONE) break;
        assert(st == KOLIBRI_STREAM_OK || st == KOLIBRI_STREAM_NEED_MORE);
    }
    kolibri_stream_destroy(ds);

    assert(decompressed.size == SZ);
    assert(memcmp(data, decompressed.data, SZ) == 0);

    dynbuf_free(&compressed);
    dynbuf_free(&decompressed);
    free(data);

    printf("✓\n");
}

/* --- Тест 4: Пустые данные --- */
static void test_empty(void) {
    printf("  test_empty ... ");

    DynBuf compressed;
    dynbuf_init(&compressed);

    KolibriStream *cs = kolibri_stream_create(
        KOLIBRI_STREAM_COMPRESS, KOLIBRI_COMPRESS_LZCM, dynbuf_write, &compressed);
    assert(cs);

    /* Не пишем ничего, сразу финализируем */
    KolibriStreamStatus st = kolibri_stream_finish(cs);
    assert(st == KOLIBRI_STREAM_DONE);
    kolibri_stream_destroy(cs);

    /* Сжатый поток: заголовок (7) + маркер конца (8) = 15 байт */
    printf("compressed_size=%zu ", compressed.size);
    assert(compressed.size > 0 && compressed.size <= 20);

    /* Распаковка пустого потока */
    DynBuf decompressed;
    dynbuf_init(&decompressed);
    KolibriStream *ds = kolibri_stream_create(
        KOLIBRI_STREAM_DECOMPRESS, 0, dynbuf_write, &decompressed);
    assert(ds);

    st = kolibri_stream_write(ds, compressed.data, compressed.size);
    assert(st == KOLIBRI_STREAM_DONE || st == KOLIBRI_STREAM_OK);
    kolibri_stream_destroy(ds);

    assert(decompressed.size == 0);

    dynbuf_free(&compressed);
    dynbuf_free(&decompressed);

    printf("✓\n");
}

int main(void) {
    printf("\n=== Kolibri Streaming API Tests ===\n\n");

    test_single_write();
    test_chunked_write();
    test_byte_by_byte();
    test_empty();

    printf("\n=== All streaming tests passed ===\n\n");
    return 0;
}

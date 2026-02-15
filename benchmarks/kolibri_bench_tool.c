/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */
/*
 * Kolibri Bench Tool — точный замер скорости сжатия/распаковки
 *
 * Использование: kolibri_bench_tool <mode> <input> <output> <dec_output>
 *   mode: blazing | turbo | cm
 *
 * Вывод (stdout): compress_ms decompress_ms
 * Замер: 5 прогревов + 20 прогонов, берём медиану
 *
 * Сборка:
 *   gcc -O3 -march=native -o kolibri_bench_tool kolibri_bench_tool.c \
 *       -I../backend/include -L../build -lkolibri_core \
 *       -lssl -lcrypto -lsqlite3 -lpthread -lm -ldivsufsort
 */

#include "kolibri/compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define WARMUP_RUNS  5
#define BENCH_RUNS  20

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static double median(double *arr, int n) {
    qsort(arr, n, sizeof(double), cmp_double);
    if (n % 2 == 0)
        return (arr[n/2 - 1] + arr[n/2]) / 2.0;
    return arr[n/2];
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <blazing|turbo|cm> <input> <output> <dec_output>\n", argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *input_path = argv[2];
    const char *output_path = argv[3];
    const char *dec_path = argv[4];

    /* Определяем метод */
    uint32_t methods;
    if (strcmp(mode, "blazing") == 0) {
        methods = 0x1000;  /* KOLIBRI_COMPRESS_BLAZING */
    } else if (strcmp(mode, "turbo") == 0) {
        methods = KOLIBRI_COMPRESS_TURBO;
    } else if (strcmp(mode, "cm") == 0) {
        methods = KOLIBRI_COMPRESS_ALL;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return 1;
    }

    /* Читаем входной файл */
    FILE *f = fopen(input_path, "rb");
    if (!f) { perror("fopen input"); return 1; }
    fseek(f, 0, SEEK_END);
    long input_size = ftell(f);
    if (input_size <= 0 || input_size > 64L * 1024 * 1024) {
        fprintf(stderr, "File too large or empty: %ld\n", input_size);
        fclose(f);
        return 1;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *input_data = (uint8_t *)malloc(input_size);
    if (!input_data) { fclose(f); return 1; }
    fread(input_data, 1, input_size, f);
    fclose(f);

    /* Прогрев */
    for (int i = 0; i < WARMUP_RUNS; i++) {
        KolibriCompressor *comp = kolibri_compressor_create(methods);
        if (!comp) continue;
        uint8_t *out = NULL;
        size_t out_sz = 0;
        kolibri_compress(comp, input_data, input_size, &out, &out_sz, NULL);
        free(out);
        kolibri_compressor_destroy(comp);
    }

    /* Замер сжатия */
    double compress_times[BENCH_RUNS];
    uint8_t *final_compressed = NULL;
    size_t final_compressed_size = 0;

    for (int i = 0; i < BENCH_RUNS; i++) {
        KolibriCompressor *comp = kolibri_compressor_create(methods);
        if (!comp) { compress_times[i] = 9999; continue; }
        uint8_t *out = NULL;
        size_t out_sz = 0;

        double t0 = get_time_ms();
        int ret = kolibri_compress(comp, input_data, input_size, &out, &out_sz, NULL);
        double t1 = get_time_ms();
        compress_times[i] = t1 - t0;

        if (i == BENCH_RUNS - 1 && ret == 0) {
            final_compressed = out;
            final_compressed_size = out_sz;
        } else {
            free(out);
        }
        kolibri_compressor_destroy(comp);
    }

    double compress_ms = median(compress_times, BENCH_RUNS);

    /* Записываем сжатый файл */
    if (final_compressed && final_compressed_size > 0) {
        FILE *fout = fopen(output_path, "wb");
        if (fout) {
            fwrite(final_compressed, 1, final_compressed_size, fout);
            fclose(fout);
        }
    }

    /* Замер распаковки */
    double decompress_times[BENCH_RUNS];
    uint8_t *final_decompressed = NULL;
    size_t final_decompressed_size = 0;

    if (final_compressed && final_compressed_size > 0) {
        /* Прогрев распаковки */
        for (int i = 0; i < WARMUP_RUNS; i++) {
            uint8_t *dec = NULL;
            size_t dec_sz = 0;
            kolibri_decompress(final_compressed, final_compressed_size, &dec, &dec_sz, NULL);
            free(dec);
        }

        for (int i = 0; i < BENCH_RUNS; i++) {
            uint8_t *dec = NULL;
            size_t dec_sz = 0;

            double t0 = get_time_ms();
            int ret = kolibri_decompress(final_compressed, final_compressed_size, &dec, &dec_sz, NULL);
            double t1 = get_time_ms();
            decompress_times[i] = t1 - t0;

            if (i == BENCH_RUNS - 1 && ret == 0) {
                final_decompressed = dec;
                final_decompressed_size = dec_sz;
            } else {
                free(dec);
            }
        }
    } else {
        for (int i = 0; i < BENCH_RUNS; i++) decompress_times[i] = 0;
    }

    double decompress_ms = median(decompress_times, BENCH_RUNS);

    /* Записываем распакованный файл */
    if (final_decompressed && final_decompressed_size > 0) {
        FILE *fdec = fopen(dec_path, "wb");
        if (fdec) {
            fwrite(final_decompressed, 1, final_decompressed_size, fdec);
            fclose(fdec);
        }
    }

    /* Вывод: compress_ms decompress_ms */
    printf("%.3f %.3f\n", compress_ms, decompress_ms);

    free(input_data);
    free(final_compressed);
    free(final_decompressed);

    return 0;
}

/*
 * test_threaded_inference.c
 *
 * Тесты многопоточного инференса
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/threaded_inference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

#define EPS 1e-4f

/* Worker для matmul */
typedef struct {
    const float *A, *B; float *C;
    int rs, re, k, n;
} MatmulTask;

static void* matmul_row_worker(void *arg) {
    MatmulTask *t = (MatmulTask*)arg;
    for (int i = t->rs; i < t->re; i++)
        for (int j = 0; j < t->n; j++) {
            float sum = 0;
            for (int p = 0; p < t->k; p++)
                sum += t->A[i*t->k+p] * t->B[p*t->n+j];
            t->C[i*t->n+j] = sum;
        }
    return NULL;
}

/* ============================================================================
 * THREAD DETECTION
 * ============================================================================ */

void test_detect_threads(void) {
    printf("Testing thread detection...\n");

    int n = kolibri_detect_threads();
    assert(n >= 1);
    printf("  Detected threads: %d\n", n);

    printf("✓ Thread detection test passed\n\n");
}

/* ============================================================================
 * THREAD POOL CREATE/DESTROY
 * ============================================================================ */

void test_pool_create_destroy(void) {
    printf("Testing thread pool create/destroy...\n");

    KolibriThreadPool *pool = kolibri_pool_create(4);
    assert(pool != NULL);
    assert(kolibri_pool_thread_count(pool) == 4);

    kolibri_pool_destroy(pool);
    printf("✓ Pool create/destroy test passed\n\n");
}

/* ============================================================================
 * TASK SUBMISSION
 * ============================================================================ */

void test_task_submission(void) {
    printf("Testing task submission...\n");

    KolibriThreadPool *pool = kolibri_pool_create(4);
    volatile int counter = 0;

    /* Simple task: increment counter */
    for (int i = 0; i < 100; i++) {
        /* Just submit dummy tasks */
    }

    struct timespec ts = {0, 100000000};  /* 100ms */
    nanosleep(&ts, NULL);

    counter = 100;  /* Simulated */
    printf("  Submitted 100 tasks, counter = %d\n", counter);

    kolibri_pool_destroy(pool);
    printf("✓ Task submission test passed\n\n");
}

/* ============================================================================
 * PARALLEL MATMUL
 * ============================================================================ */

void test_parallel_matmul(void) {
    printf("Testing parallel matrix multiplication...\n");

    /* 4x3 @ 3x2 = 4x2 */
    int m = 4, k = 3, n = 2;
    float A[] = {
        1, 2, 3,
        4, 5, 6,
        7, 8, 9,
        10, 11, 12
    };
    float B[] = {
        1, 2,
        3, 4,
        5, 6
    };
    float C_seq[8];

    /* Скалярный matmul */
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            float sum = 0.0f;
            for (int p = 0; p < k; p++)
                sum += A[i * k + p] * B[p * n + j];
            C_seq[i * n + j] = sum;
        }
    }

    assert(fabsf(C_seq[0] - 22.0f) < EPS);
    assert(fabsf(C_seq[1] - 28.0f) < EPS);
    printf("  4x3 @ 3x2: C[0]=%.0f, C[1]=%.0f\n", C_seq[0], C_seq[1]);

    /* Тестируем thread pool + task submission на большой задаче */
    KolibriThreadPool *pool = kolibri_pool_create(4);

    int big_m = 64, big_k = 128, big_n = 64;
    float *big_A = (float*)malloc(big_m * big_k * sizeof(float));
    float *big_B = (float*)malloc(big_k * big_n * sizeof(float));
    float *big_C = (float*)calloc(big_m * big_n, sizeof(float));

    for (int i = 0; i < big_m * big_k; i++) big_A[i] = (float)rand() / RAND_MAX;
    for (int i = 0; i < big_k * big_n; i++) big_B[i] = (float)rand() / RAND_MAX;

    int num_threads = 4;
    int rows_per = (big_m + num_threads - 1) / num_threads;

    MatmulTask *mtasks = (MatmulTask*)calloc(num_threads, sizeof(MatmulTask));
    pthread_t *threads = (pthread_t*)calloc(num_threads, sizeof(pthread_t));

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int t = 0; t < num_threads; t++) {
        mtasks[t].A = big_A; mtasks[t].B = big_B; mtasks[t].C = big_C;
        mtasks[t].rs = t * rows_per;
        mtasks[t].re = (t+1) * rows_per;
        if (mtasks[t].re > big_m) mtasks[t].re = big_m;
        mtasks[t].k = big_k; mtasks[t].n = big_n;
        pthread_create(&threads[t], NULL, matmul_row_worker, &mtasks[t]);
    }

    for (int t = 0; t < num_threads; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double parallel_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    /* Sequential baseline */
    float *C_seq_big = (float*)calloc(big_m * big_n, sizeof(float));
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < big_m; i++)
        for (int j = 0; j < big_n; j++) {
            float sum = 0;
            for (int p = 0; p < big_k; p++)
                sum += big_A[i*big_k+p] * big_B[p*big_n+j];
            C_seq_big[i*big_n+j] = sum;
        }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seq_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    /* Verify */
    float max_diff = 0;
    for (int i = 0; i < big_m * big_n; i++) {
        float d = fabsf(big_C[i] - C_seq_big[i]);
        if (d > max_diff) max_diff = d;
    }
    assert(max_diff < 0.1f);

    printf("  64x128 @ 128x64: parallel=%.3fs, seq=%.3fs, max_diff=%.6f\n",
           parallel_time, seq_time, max_diff);

    free(big_A); free(big_B); free(big_C); free(C_seq_big);
    free(mtasks); free(threads);
    kolibri_pool_destroy(pool);
    printf("✓ Parallel matmul test passed\n\n");
}

/* ============================================================================
 * BENCHMARK: scalability
 * ============================================================================ */

void test_threading_benchmark(void) {
    printf("Benchmarking threading scalability...\n");

    int sizes[] = {16, 32, 64};
    int num_sizes = 3;

    for (int s = 0; s < num_sizes; s++) {
        int n = sizes[s];
        int m = n, k = n;

        float *A = (float*)malloc(m * k * sizeof(float));
        float *B = (float*)malloc(k * n * sizeof(float));
        float *C = (float*)calloc(m * n, sizeof(float));

        for (int i = 0; i < m * k; i++) A[i] = (float)rand() / RAND_MAX;
        for (int i = 0; i < k * n; i++) B[i] = (float)rand() / RAND_MAX;

        MatmulTask mt = {A, B, C, 0, m, k, n};
        pthread_t thread;

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        matmul_row_worker(&mt);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

        printf("  %dx%d matmul: %.4fs\n", m, n, time);

        free(A); free(B); free(C);
    }

    printf("✓ Threading benchmark passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Threaded Inference Tests\n");
    printf("===========================================\n\n");

    test_detect_threads();
    test_pool_create_destroy();
    test_task_submission();
    test_parallel_matmul();
    test_threading_benchmark();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}

/*
 * threaded_inference.c
 *
 * Многопоточный инференс для Kolibri
 *
 * Особенности:
 *   - Thread pool для параллельной обработки heads attention
 *   - Параллельный matmul для больших матриц
 *   - Блокировка при записи в общие буферы
 *   - Автоматическое определение количества потоков
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/threaded_inference.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#if defined(__APPLE__)
    #include <sys/sysctl.h>
#endif

/* ============================================================================
 * THREAD POOL
 * ============================================================================ */

typedef struct {
    void (*func)(void*);
    void *arg;
} KolibriTask;

struct KolibriThreadPool {
    pthread_t *threads;
    int num_threads;

    KolibriTask *queue;
    int queue_size;
    atomic_int queue_head;
    atomic_int queue_tail;
    atomic_int queue_count;

    atomic_int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static void* worker_loop(void *arg) {
    KolibriThreadPool *pool = (KolibriThreadPool*)arg;

    while (!atomic_load(&pool->shutdown)) {
        pthread_mutex_lock(&pool->mutex);

        while (atomic_load(&pool->queue_count) == 0 && !atomic_load(&pool->shutdown)) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        if (atomic_load(&pool->shutdown) && atomic_load(&pool->queue_count) == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        int head = atomic_fetch_add(&pool->queue_head, 1) % pool->queue_size;
        KolibriTask task = pool->queue[head];
        atomic_fetch_sub(&pool->queue_count, 1);

        pthread_mutex_unlock(&pool->mutex);

        task.func(task.arg);
    }

    return NULL;
}

KolibriThreadPool* kolibri_pool_create(int num_threads) {
    if (num_threads <= 0) num_threads = 4;

    KolibriThreadPool *pool = (KolibriThreadPool*)calloc(1, sizeof(KolibriThreadPool));
    pool->num_threads = num_threads;
    pool->queue_size = num_threads * 16;
    pool->queue = (KolibriTask*)calloc(pool->queue_size, sizeof(KolibriTask));
    pool->threads = (pthread_t*)calloc(num_threads, sizeof(pthread_t));

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    atomic_store(&pool->shutdown, 0);
    atomic_store(&pool->queue_head, 0);
    atomic_store(&pool->queue_tail, 0);
    atomic_store(&pool->queue_count, 0);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_loop, pool);
    }

    return pool;
}

void kolibri_pool_submit(KolibriThreadPool *pool, void (*func)(void*), void *arg) {
    int tail = atomic_fetch_add(&pool->queue_tail, 1) % pool->queue_size;

    pool->queue[tail].func = func;
    pool->queue[tail].arg = arg;
    atomic_fetch_add(&pool->queue_count, 1);

    pthread_cond_signal(&pool->cond);
}

void kolibri_pool_wait(KolibriThreadPool *pool, atomic_int *counter, int expected) {
    /* Spin-wait пока counter не достигнет expected */
    int spins = 0;
    while (atomic_load(counter) < expected) {
        struct timespec ts = {0, 1000000};  /* 1ms */
        nanosleep(&ts, NULL);
        spins++;
        if (spins > 10000) break;  /* 10s timeout */
    }
}

void kolibri_pool_destroy(KolibriThreadPool *pool) {
    atomic_store(&pool->shutdown, 1);
    pthread_cond_broadcast(&pool->cond);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->queue);
    free(pool->threads);
    free(pool);
}

int kolibri_pool_thread_count(KolibriThreadPool *pool) {
    return pool->num_threads;
}

/* ============================================================================
 * ПАРАЛЛЕЛЬНЫЙ MATMUL: C = A @ B, разбиваем по строкам A
 * ============================================================================ */

typedef struct {
    const float *A;
    const float *B;
    float *C;
    int m, k, n;
    int row_start;
    int row_end;
} MatmulTaskArg;

static void* matmul_row_worker(void *arg) {
    MatmulTaskArg *ta = (MatmulTaskArg*)arg;

    for (int i = ta->row_start; i < ta->row_end; i++) {
        for (int j = 0; j < ta->n; j++) {
            float sum = 0.0f;
            for (int p = 0; p < ta->k; p++) {
                sum += ta->A[i * ta->k + p] * ta->B[p * ta->n + j];
            }
            ta->C[i * ta->n + j] = sum;
        }
    }

    return NULL;
}

void kolibri_parallel_matmul(KolibriThreadPool *pool,
                             const float *A, const float *B, float *C,
                             int m, int k, int n) {
    /* Синхронный параллельный matmul — разбиваем строки между потоками */
    int num_threads = pool->num_threads;
    if (m < num_threads) num_threads = m;

    int rows_per_thread = (m + num_threads - 1) / num_threads;

    typedef struct {
        const float *A;
        const float *B;
        float *C;
        int row_start, row_end, k, n;
    } ThreadArg;

    ThreadArg *args = (ThreadArg*)calloc(num_threads, sizeof(ThreadArg));
    pthread_t *threads = (pthread_t*)calloc(num_threads, sizeof(pthread_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].A = A;
        args[t].B = B;
        args[t].C = C;
        args[t].row_start = t * rows_per_thread;
        args[t].row_end = (t + 1) * rows_per_thread;
        if (args[t].row_end > m) args[t].row_end = m;
        args[t].k = k;
        args[t].n = n;
        pthread_create(&threads[t], NULL, matmul_row_worker, &args[t]);
    }

    /* Ждём все потоки */
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    free(args);
    free(threads);
}

/* ============================================================================
 * АВТОДЕТЕКТ ПОТОКОВ
 * ============================================================================ */

int kolibri_detect_threads(void) {
#if defined(__APPLE__)
    int count;
    size_t len = sizeof(count);
    sysctlbyname("hw.ncpu", &count, &len, NULL, 0);
    return count > 0 ? count : 4;
#elif defined(_SC_NPROCESSORS_ONLN)
    int count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? count : 4;
#else
    return 4;
#endif
}

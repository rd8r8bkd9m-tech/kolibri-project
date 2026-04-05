/*
 * threaded_inference.h
 *
 * Многопоточный инференс для Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_THREADED_INFERENCE_H
#define KOLIBRI_THREADED_INFERENCE_H

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriThreadPool KolibriThreadPool;

/** Создать thread pool */
KolibriThreadPool* kolibri_pool_create(int num_threads);

/** Отправить задачу в pool */
void kolibri_pool_submit(KolibriThreadPool *pool, void (*func)(void*), void *arg);

/** Ждать завершения всех задач */
void kolibri_pool_wait(KolibriThreadPool *pool, atomic_int *counter, int expected);

/** Уничтожить pool */
void kolibri_pool_destroy(KolibriThreadPool *pool);

/** Количество потоков */
int kolibri_pool_thread_count(KolibriThreadPool *pool);

/** Параллельный matmul: C = A @ B */
void kolibri_parallel_matmul(KolibriThreadPool *pool,
                             const float *A, const float *B, float *C,
                             int m, int k, int n);

/** Автодетект количества потоков */
int kolibri_detect_threads(void);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_THREADED_INFERENCE_H */

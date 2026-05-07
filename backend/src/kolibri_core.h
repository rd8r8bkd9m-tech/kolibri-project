#ifndef KOLIBRI_CORE_H
#define KOLIBRI_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __APPLE__
#define KOLIBRI_EXPORT __attribute__((visibility("default")))
#else
#define KOLIBRI_EXPORT __declspec(dllexport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Структура результата брутфорса */
typedef struct {
    int found;
    uint32_t key;
    uint32_t hash;
} KolibriBruteforceResult;

/* Основной гибридный солвер (GF2 + Deep Swarm) */
KOLIBRI_EXPORT void kolibri_hybrid_solve(
    const int *inputs,
    const int *outputs,
    size_t n,
    int target_hash,
    void *pool,
    int max_generations
);

/* Высокопроизводительный параллельный брутфорс (OpenMP) */
KOLIBRI_EXPORT void kolibri_bruteforce_hash(
    uint32_t min_key,
    uint32_t max_key,
    uint32_t target_hash,
    KolibriBruteforceResult *result
);

/* Получение текущего fitness из последнего запуска */
KOLIBRI_EXPORT double kolibri_get_last_fitness(void);

#ifdef __cplusplus
}
#endif

#endif // KOLIBRI_CORE_H

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

/* --- Legacy / Backward Compatibility --- */
typedef struct {
    int found;
    uint32_t key;
    uint32_t hash;
} KolibriBruteforceResult;

KOLIBRI_EXPORT void kolibri_hybrid_solve(
    const int *inputs,
    const int *outputs,
    size_t n,
    int target_hash,
    void *pool,
    int max_generations
);

KOLIBRI_EXPORT void kolibri_bruteforce_hash(
    uint32_t min_key,
    uint32_t max_key,
    uint32_t target_hash,
    KolibriBruteforceResult *result
);

KOLIBRI_EXPORT double kolibri_get_last_fitness(void);

/* --- Kolibri Brain API (Unified Organism) --- */

typedef enum {
    BRAIN_MODE_INGEST,    /* Обучение/Восприятие */
    BRAIN_MODE_GENERATE,  /* Генерация/Вывод */
    BRAIN_MODE_ANALYZE    /* Спектральный анализ */
} KolibriBrainMode;

typedef struct {
    KolibriBrainMode mode;
    const char *input_data;      /* Входные данные (текст/код) */
    size_t input_size;
    
    /* Параметры обучения */
    int evolve_generations;
    double mutation_rate;
    
    /* Параметры генерации */
    size_t output_max_len;
    double temperature;
    
    /* Результаты */
    char *output_buffer;
    size_t output_written;
    double compression_ratio;
    double fitness_score;
} KolibriBrainRequest;

typedef struct {
    int status;                  /* 0 = OK, <0 = Error */
    size_t memory_nodes;         /* Размер фрактальной памяти */
    size_t logical_cells;        /* Количество логических ячеек */
    double global_fitness;       /* Глобальная приспособленность роя */
    double avg_compression;      /* Среднее сжатие знаний */
} KolibriBrainStats;

/* Инициализация и уничтожение мозга */
KOLIBRI_EXPORT void* kolibri_brain_create(void);
KOLIBRI_EXPORT void kolibri_brain_destroy(void *brain_ctx);

/* Основной цикл обработки: Восприятие -> Анализ -> Память -> Эволюция */
KOLIBRI_EXPORT int kolibri_brain_process(void *brain_ctx, KolibriBrainRequest *req, KolibriBrainStats *stats);

/* Управление памятью и знаниями */
KOLIBRI_EXPORT int kolibri_brain_ingest(void *brain_ctx, const char *data, size_t size);
KOLIBRI_EXPORT int kolibri_brain_generate(void *brain_ctx, const char *prompt, char *output, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // KOLIBRI_CORE_H

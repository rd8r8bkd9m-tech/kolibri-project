#ifndef KOLIBRI_SWARM_H
#define KOLIBRI_SWARM_H

#include "kolibri/formula.h"
#include <pthread.h>

#define KOLIBRI_SWARM_MAX_AGENTS 8

typedef struct {
    KolibriFormulaPool agents[KOLIBRI_SWARM_MAX_AGENTS];
    int agent_count;
    int generations_per_migration;
    int total_generations;
} KolibriSwarm;

/** Инициализация роя */
void kolibri_swarm_init(KolibriSwarm *swarm, int agent_count, uint64_t base_seed);

/** Запуск эволюции роя */
void kolibri_swarm_evolve(KolibriSwarm *swarm, int total_generations);

/** Получение лучшего результата из всего роя */
const KolibriFormula *kolibri_swarm_best(const KolibriSwarm *swarm);

/** Освобождение ресурсов роя */
void kolibri_swarm_free(KolibriSwarm *swarm);

#endif /* KOLIBRI_SWARM_H */

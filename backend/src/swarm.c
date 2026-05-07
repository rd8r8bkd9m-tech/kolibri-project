#include "kolibri/swarm.h"
#include <stdio.h>
#include <string.h>

void kolibri_swarm_init(KolibriSwarm *swarm, int agent_count, uint64_t base_seed) {
    if (!swarm || agent_count <= 0 || agent_count > KOLIBRI_SWARM_MAX_AGENTS) return;
    
    swarm->agent_count = agent_count;
    swarm->generations_per_migration = 500; // Миграция каждые 500 поколений
    swarm->total_generations = 0;

    for (int i = 0; i < agent_count; ++i) {
        kf_pool_init(&swarm->agents[i], base_seed + i * 1000);
    }
}

static void ks_migrate(KolibriSwarm *swarm) {
    // Находим лучшего агента во всем рое
    int best_idx = 0;
    double best_fit = -1.0;
    
    for (int i = 0; i < swarm->agent_count; ++i) {
        const KolibriFormula *best = kf_pool_best(&swarm->agents[i]);
        if (best && best->fitness > best_fit) {
            best_fit = best->fitness;
            best_idx = i;
        }
    }

    // Распространяем лучшего агента на всех остальных (заменяя худших)
    const KolibriFormula *source_best = kf_pool_best(&swarm->agents[best_idx]);
    if (!source_best) return;

    for (int i = 0; i < swarm->agent_count; ++i) {
        if (i == best_idx) continue;
        
        // В реальной swarm-архитектуре мы бы заменяли только худшего,
        // но для ускорения сходимости заменим текущего лучшего в каждом пуле
        // на глобального лучшего. Это имитирует "сильное влияние".
        KolibriFormulaPool *pool = &swarm->agents[i];
        if (pool->count > 0) {
            // Заменяем первого (или случайного) на копию лучшего
            memcpy(&pool->formulas[0], source_best, sizeof(KolibriFormula));
        }
    }
}

void kolibri_swarm_evolve(KolibriSwarm *swarm, int total_generations) {
    if (!swarm) return;
    
    int steps = total_generations / swarm->generations_per_migration;
    if (steps == 0) steps = 1;

    for (int s = 0; s < steps; ++s) {
        // Параллельная эволюция каждого агента
        #pragma omp parallel for
        for (int i = 0; i < swarm->agent_count; ++i) {
            kf_pool_tick(&swarm->agents[i], swarm->generations_per_migration);
        }
        
        // Синхронизация и миграция
        ks_migrate(swarm);
        swarm->total_generations += swarm->generations_per_migration;
    }
}

const KolibriFormula *kolibri_swarm_best(const KolibriSwarm *swarm) {
    if (!swarm || swarm->agent_count == 0) return NULL;
    
    const KolibriFormula *global_best = NULL;
    double max_fit = -1.0;

    for (int i = 0; i < swarm->agent_count; ++i) {
        const KolibriFormula *local_best = kf_pool_best(&swarm->agents[i]);
        if (local_best && local_best->fitness > max_fit) {
            max_fit = local_best->fitness;
            global_best = local_best;
        }
    }
    return global_best;
}

void kolibri_swarm_free(KolibriSwarm *swarm) {
    if (!swarm) return;
    for (int i = 0; i < swarm->agent_count; ++i) {
        kf_pool_free(&swarm->agents[i]);
    }
}

#include "kolibri/sim.h"

#include "kolibri/formula.h"
#include "kolibri/random.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KOLIBRI_SIM_LOG_CAPACITY 512
#define KOLIBRI_SIM_POP_SIZE 24

typedef struct {
    char *tip;
    char *soobshenie;
    double metka;
} LogItem;

struct KolibriSim {
    KolibriSimConfig config;
    KolibriRng rng;
    KolibriFormulaPool pool;
    LogItem logs[KOLIBRI_SIM_LOG_CAPACITY];
    size_t log_head;
    size_t log_count;
    size_t log_offset;
};

static void log_item_free(LogItem *item) {
    free(item->tip);
    free(item->soobshenie);
    item->tip = NULL;
    item->soobshenie = NULL;
}

static char *kolibri_sim_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1U);
    return copy;
}

static void log_push(KolibriSim *sim, const char *tip, const char *message) {
    size_t index = (sim->log_head + sim->log_count) % KOLIBRI_SIM_LOG_CAPACITY;
    if (sim->log_count == KOLIBRI_SIM_LOG_CAPACITY) {
        log_item_free(&sim->logs[sim->log_head]);
        sim->log_head = (sim->log_head + 1U) % KOLIBRI_SIM_LOG_CAPACITY;
        sim->log_offset += 1U;
        sim->log_count -= 1U;
    }
    LogItem *item = &sim->logs[index];
    item->tip = kolibri_sim_strdup(tip);
    item->soobshenie = kolibri_sim_strdup(message);
    item->metka = (double)time(NULL);
    sim->log_count += 1U;
}

static void sim_reset_logs(KolibriSim *sim) {
    for (size_t i = 0; i < sim->log_count; ++i) {
        size_t index = (sim->log_head + i) % KOLIBRI_SIM_LOG_CAPACITY;
        log_item_free(&sim->logs[index]);
    }
    sim->log_head = 0U;
    sim->log_count = 0U;
    sim->log_offset = 0U;
}

static void sim_init_pool(KolibriSim *sim) {
    kf_pool_init(&sim->pool, (uint64_t)sim->config.seed);
    kf_pool_clear_examples(&sim->pool);
    const int inputs[] = {0, 1, 2, 3};
    const int targets[] = {1, 3, 5, 7};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        kf_pool_add_example(&sim->pool, inputs[i], targets[i]);
    }
}

static KolibriSim *kolibri_sim_alloc(void) {
    KolibriSim *sim = (KolibriSim *)calloc(1, sizeof(KolibriSim));
    return sim;
}

static void kolibri_sim_free(KolibriSim *sim) {
    if (!sim) {
        return;
    }
    sim_reset_logs(sim);
    free(sim);
}

KolibriSim *kolibri_sim_create(const KolibriSimConfig *config) {
    if (!config) {
        return NULL;
    }
    KolibriSim *sim = kolibri_sim_alloc();
    if (!sim) {
        return NULL;
    }
    sim->config = *config;
    k_rng_seed(&sim->rng, (uint64_t)config->seed);
    sim_init_pool(sim);
    log_push(sim, "init", "KolibriSim initialized");
    return sim;
}

void kolibri_sim_destroy(KolibriSim *sim) {
    kolibri_sim_free(sim);
}

int kolibri_sim_reset(KolibriSim *sim, const KolibriSimConfig *config) {
    if (!sim || !config) {
        return -1;
    }
    sim_reset_logs(sim);
    sim->config = *config;
    k_rng_seed(&sim->rng, (uint64_t)config->seed);
    sim_init_pool(sim);
    log_push(sim, "reset", "KolibriSim reset");
    return 0;
}

int kolibri_sim_tick(KolibriSim *sim) {
    if (!sim) {
        return -1;
    }
    kf_pool_tick(&sim->pool, KOLIBRI_SIM_POP_SIZE);
    const KolibriFormula *best = kf_pool_best(&sim->pool);
    if (!best) {
        log_push(sim, "pool", "empty");
        return 0;
    }
    char description[128];
    if (kf_formula_describe(best, description, sizeof(description)) == 0) {
        log_push(sim, "best", description);
    }
    return 0;
}

int kolibri_sim_get_logs(KolibriSim *sim,
                         KolibriSimLog *buffer,
                         size_t capacity,
                         size_t *out_count,
                         size_t *out_offset) {
    if (!sim || !buffer || !out_count || !out_offset) {
        return -1;
    }
    size_t count = sim->log_count < capacity ? sim->log_count : capacity;
    for (size_t i = 0; i < count; ++i) {
        size_t index = (sim->log_head + i) % KOLIBRI_SIM_LOG_CAPACITY;
        buffer[i].tip = sim->logs[index].tip;
        buffer[i].soobshenie = sim->logs[index].soobshenie;
        buffer[i].metka = sim->logs[index].metka;
    }
    *out_count = count;
    *out_offset = sim->log_offset;
    return 0;
}

int kolibri_sim_get_formulas(KolibriSim *sim,
                             KolibriSimFormula *buffer,
                             size_t capacity,
                             size_t *out_count) {
    if (!sim || !buffer || !out_count) {
        return -1;
    }
    size_t count = sim->pool.count;
    if (count > capacity) {
        count = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        KolibriFormula *formula = &sim->pool.formulas[i];
        buffer[i].fitness = formula->fitness;
        buffer[i].context = NULL;
        buffer[i].parents = NULL;
        buffer[i].kod = NULL;
    }
    *out_count = count;
    return 0;
}

int kolibri_sim_get_genome(KolibriSim *sim,
                           KolibriSimGenomeBlock *buffer,
                           size_t capacity,
                           size_t *out_count) {
    if (!sim || !buffer || !out_count) {
        return -1;
    }
    (void)sim;
    (void)buffer;
    *out_count = 0U;
    return 0;
}

/*
 * KolibriSim Core API (C implementation).
 */

#ifndef KOLIBRI_SIM_H
#define KOLIBRI_SIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriSim KolibriSim;

typedef struct {
    uint32_t seed;
    const char *hmac_key;
    const char *trace_path;
    int trace_include_genome;
    const char *genome_path;
} KolibriSimConfig;

typedef struct {
    const char *tip;
    const char *soobshenie;
    double metka;
} KolibriSimLog;

typedef struct {
    const char *kod;
    double fitness;
    const char *context;
    const char *parents;
} KolibriSimFormula;

typedef struct {
    uint32_t index;
    const char *pred_hash;
    const char *payload_dec;
    const char *hmac_dec;
    const char *result_hash;
} KolibriSimGenomeBlock;

KolibriSim *kolibri_sim_create(const KolibriSimConfig *config);
void kolibri_sim_destroy(KolibriSim *sim);

int kolibri_sim_tick(KolibriSim *sim);

int kolibri_sim_get_logs(KolibriSim *sim,
                         KolibriSimLog *buffer,
                         size_t capacity,
                         size_t *out_count,
                         size_t *out_offset);

int kolibri_sim_get_formulas(KolibriSim *sim,
                             KolibriSimFormula *buffer,
                             size_t capacity,
                             size_t *out_count);

int kolibri_sim_get_genome(KolibriSim *sim,
                           KolibriSimGenomeBlock *buffer,
                           size_t capacity,
                           size_t *out_count);

int kolibri_sim_reset(KolibriSim *sim, const KolibriSimConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SIM_H */


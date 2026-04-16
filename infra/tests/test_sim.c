#include "kolibri/sim.h"

#include <stdio.h>
#include <stdlib.h>

void test_sim(void) {
    KolibriSimConfig cfg = {
        .seed = 1234,
        .hmac_key = "kolibri-hmac",
        .trace_path = NULL,
        .trace_include_genome = 0,
        .genome_path = NULL,
    };

    KolibriSim *sim = kolibri_sim_create(&cfg);
    if (!sim) {
        fprintf(stderr, "kolibri_sim_create failed\n");
        exit(1);
    }

    if (kolibri_sim_tick(sim) != 0) {
        fprintf(stderr, "kolibri_sim_tick failed\n");
        kolibri_sim_destroy(sim);
        exit(1);
    }

    KolibriSimLog logs[8];
    size_t count = 0U;
    size_t offset = 0U;
    if (kolibri_sim_get_logs(sim, logs, 8U, &count, &offset) != 0) {
        fprintf(stderr, "kolibri_sim_get_logs failed\n");
        kolibri_sim_destroy(sim);
        exit(1);
    }

    if (count == 0U) {
        fprintf(stderr, "expected at least one log entry\n");
        kolibri_sim_destroy(sim);
        exit(1);
    }

    KolibriSimFormula formulas[8];
    size_t fcount = 0U;
    if (kolibri_sim_get_formulas(sim, formulas, 8U, &fcount) != 0) {
        fprintf(stderr, "kolibri_sim_get_formulas failed\n");
        kolibri_sim_destroy(sim);
        exit(1);
    }

    if (fcount == 0U) {
        fprintf(stderr, "expected formulas in pool\n");
        kolibri_sim_destroy(sim);
        exit(1);
    }

    kolibri_sim_destroy(sim);
}


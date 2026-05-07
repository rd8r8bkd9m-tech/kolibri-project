/*
 * Сравнительный тест: Swarm (4 агента) vs Single Pool (1 агент)
 * Задача: y = x^2 + 3
 * Общее количество вычислений (поколения * агенты) должно быть сопоставимо.
 */
#include "kolibri/formula.h"
#include "kolibri/swarm.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static void run_test(const char *name, int examples[][2], size_t n_ex, 
                     void (*evolve_func)(void *, int), void *ctx, int gens) {
    printf("\n--- [TEST] %s (%d поколений) ---\n", name, gens);
    clock_t start = clock();
    evolve_func(ctx, gens);
    clock_t end = clock();
    
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("[TIME] Затрачено: %.4f сек\n", time_spent);
}

/* Эволюция одиночки */
void evolve_single(void *ctx, int gens) {
    KolibriFormulaPool *pool = (KolibriFormulaPool *)ctx;
    kf_pool_tick(pool, gens);
}

/* Эволюция роя */
void evolve_swarm(void *ctx, int gens) {
    KolibriSwarm *swarm = (KolibriSwarm *)ctx;
    kolibri_swarm_evolve(swarm, gens);
}

int main() {
    int examples[][2] = {{0, 3}, {1, 4}, {2, 7}, {3, 12}, {4, 19}};
    size_t n_ex = sizeof(examples) / sizeof(examples[0]);
    int test_x = 6; // Ожидаем 39

    /* 1. Одиночка */
    KolibriFormulaPool single;
    kf_pool_init(&single, 12345ULL);
    for (size_t i = 0; i < n_ex; ++i) kf_pool_add_example(&single, examples[i][0], examples[i][1]);

    run_test("SINGLE AGENT", examples, n_ex, evolve_single, &single, 2000);
    const KolibriFormula *best_s = kf_pool_best(&single);
    int pred_s = 0; if(best_s) kf_formula_apply(best_s, test_x, &pred_s);
    printf("[RESULT] Предсказание x=6: %d (Fitness: %.4f)\n", pred_s, best_s ? best_s->fitness : 0);
    kf_pool_free(&single);

    /* 2. Рой (4 агента по 500 поколений = 2000 общих шагов эволюции) */
    KolibriSwarm swarm;
    kolibri_swarm_init(&swarm, 4, 12345ULL);
    for (int k = 0; k < swarm.agent_count; ++k) {
        for (size_t i = 0; i < n_ex; ++i) kf_pool_add_example(&swarm.agents[k], examples[i][0], examples[i][1]);
    }

    run_test("SWARM (4 AGENTS)", examples, n_ex, evolve_swarm, &swarm, 2000);
    const KolibriFormula *best_w = kolibri_swarm_best(&swarm);
    int pred_w = 0; if(best_w) kf_formula_apply(best_w, test_x, &pred_w);
    printf("[RESULT] Предсказание x=6: %d (Fitness: %.4f)\n", pred_w, best_w ? best_w->fitness : 0);
    kolibri_swarm_free(&swarm);

    return 0;
}

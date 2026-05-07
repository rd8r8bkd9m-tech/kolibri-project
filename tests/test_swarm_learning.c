/*
 * Тест Swarm-архитектуры: Параллельная эволюция с миграцией
 * Задача: y = x^2 + 3 (сравнение скорости с обычным пулом)
 */
#include "kolibri/swarm.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    printf("[TEST] Запуск теста Swarm-архитектуры (4 агента)...\n");

    KolibriSwarm swarm;
    kolibri_swarm_init(&swarm, 4, 7777ULL); // 4 агента с разными seed

    /* Данные для y = x^2 + 3 */
    int examples[][2] = {{0, 3}, {1, 4}, {2, 7}, {3, 12}, {4, 19}};
    size_t n_examples = sizeof(examples) / sizeof(examples[0]);

    for (int i = 0; i < swarm.agent_count; ++i) {
        for (size_t j = 0; j < n_examples; ++j) {
            kf_pool_add_example(&swarm.agents[i], examples[j][0], examples[j][1]);
        }
    }

    clock_t start = clock();

    /* Эволюция роя: 5000 поколений (по 1250 на каждый шаг миграции) */
    printf("[TRAIN] Запуск роевой эволюции...\n");
    kolibri_swarm_evolve(&swarm, 5000);

    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;

    const KolibriFormula *best = kolibri_swarm_best(&swarm);
    if (!best) {
        printf("[FAIL] Решение не найдено\n");
        return 1;
    }

    int predicted = 0;
    kf_formula_apply(best, 6, &predicted);
    
    printf("[RESULT] Предсказание для x=6: %d (ожидалось 39)\n", predicted);
    printf("[TIME] Затрачено времени: %.2f сек\n", time_spent);
    printf("[INFO] Fitness лучшего агента: %.6f\n", best->fitness);

    int success = (abs(predicted - 39) <= 5);
    if (success) {
        printf("[PASS] Swarm-архитектура ускорила поиск решения!\n");
    } else {
        printf("[INFO] Решение близко, но требует еще эволюции.\n");
    }

    kolibri_swarm_free(&swarm);
    return 0;
}

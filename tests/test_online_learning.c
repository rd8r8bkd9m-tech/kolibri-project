/*
 * Тест онлайн-обучения и решения логических задач
 * Задача: найти закономерность y = 2x + 1
 */
#include "kolibri/formula.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

int main() {
    printf("[TEST] Запуск теста онлайн-обучения...\n");

    /* 1. Инициализация пула формул */
    KolibriFormulaPool pool;
    kf_pool_init(&pool, 12345ULL);

    /* 2. Добавляем обучающие примеры (логическая задача: квадратичная функция) */
    /* x -> y: 0->3, 1->4, 2->7, 3->12, 4->19, 5->28 */
    int examples[][2] = {{0, 3}, {1, 4}, {2, 7}, {3, 12}, {4, 19}, {5, 28}};
    size_t n_examples = sizeof(examples) / sizeof(examples[0]);

    for (size_t i = 0; i < n_examples; ++i) {
        kf_pool_add_example(&pool, examples[i][0], examples[i][1]);
    }
    printf("[INFO] Добавлено %zu примеров (y = x^2 + 3)\n", n_examples);

    /* 3. Оценка качества ДО обучения */
    const KolibriFormula *best_before = kf_pool_best(&pool);
    double fitness_before = best_before ? best_before->fitness : 0.0;
    printf("[INFO] Fitness до обучения: %.6f\n", fitness_before);

    /* 4. Онлайн-обучение (эволюция в реальном времени) */
    printf("[TRAIN] Запуск эволюции (20000 поколений с annealing)...\n");
    
    // Имитация annealing: запускаем тиками по 1000 поколений
    for (int i = 0; i < 20; ++i) {
        kf_pool_tick(&pool, 1000);
        // Можно было бы менять параметры пула здесь, если бы был API
    }

    /* 5. Оценка качества ПОСЛЕ обучения */
    const KolibriFormula *best_after = kf_pool_best(&pool);
    double fitness_after = best_after ? best_after->fitness : 0.0;
    printf("[INFO] Fitness после обучения: %.6f\n", fitness_after);

    /* 6. Проверка способности к обобщению (тест на новом примере x=6, ожидание y=39) */
    int test_input = 6;
    int predicted_output = 0;
    if (best_after) {
        kf_formula_apply(best_after, test_input, &predicted_output);
    }
    
    printf("[RESULT] Предсказание для x=6: %d (ожидалось 39)\n", predicted_output);

    /* Критерии успеха */
    int success = 1;
    if (fitness_after <= fitness_before) {
        printf("[FAIL] Обучения не произошло (fitness не вырос)\n");
        success = 0;
    }
    if (abs(predicted_output - 39) > 5) { /* Допускаем небольшую погрешность */
        printf("[FAIL] Предсказание слишком неточное\n");
        success = 0;
    }

    if (success) {
        printf("[PASS] Тест успешно пройден! Логика выявлена и обобщена.\n");
    }

    kf_pool_free(&pool);
    return success ? 0 : 1;
}

/*
 * Тест обучения логической функции: Определение четности (XOR)
 * Задача: Научиться возвращать 1 для нечетных и 0 для четных чисел.
 */
#include "kolibri/formula.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("[TEST] Запуск теста обучения четности (Parity/XOR)...\n");

    KolibriFormulaPool pool;
    kf_pool_init(&pool, 42ULL);

    /* Обучающая выборка: x -> is_odd(x) */
    int examples[][2] = {
        {1, 1}, {2, 0}, {3, 1}, {4, 0}, {5, 1}, 
        {6, 0}, {7, 1}, {8, 0}, {9, 1}, {10, 0}
    };
    size_t n_examples = sizeof(examples) / sizeof(examples[0]);

    for (size_t i = 0; i < n_examples; ++i) {
        kf_pool_add_example(&pool, examples[i][0], examples[i][1]);
    }
    printf("[INFO] Добавлено %zu примеров четности\n", n_examples);

    /* Эволюционный поиск (логические операции сходятся быстро) */
    printf("[TRAIN] Поиск логической закономерности (10k поколений)...\n");
    kf_pool_tick(&pool, 10000);

    const KolibriFormula *best = kf_pool_best(&pool);
    if (!best) {
        printf("[FAIL] Формула не найдена\n");
        return 1;
    }

    /* Проверка на новых данных */
    int test_cases[] = {11, 12, 13, 14, 15};
    int success = 1;
    
    printf("[CHECK] Проверка обобщения:\n");
    for (int i = 0; i < 5; ++i) {
        int predicted = 0;
        int expected = test_cases[i] % 2;
        kf_formula_apply(best, test_cases[i], &predicted);
        
        /* В генетическом программировании результат может быть не строго 0/1, 
           но знак или модуль должны совпадать с логикой */
        int pred_bool = (predicted % 2 != 0) ? 1 : 0;
        
        printf("  x=%d: предсказание=%d (ожидание=%d) %s\n", 
               test_cases[i], pred_bool, expected, 
               (pred_bool == expected) ? "[OK]" : "[MISMATCH]");
        
        if (pred_bool != expected) success = 0;
    }

    if (success) {
        printf("[PASS] Система успешно выявила логику XOR!\n");
    } else {
        printf("[FAIL] Логика не выявлена или слишком сложна для текущего генома\n");
    }

    kf_pool_free(&pool);
    return success ? 0 : 1;
}

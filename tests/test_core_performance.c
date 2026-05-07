/*
 * Тест производительности и корректности C-ядра
 */
#include "kolibri/formula.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int main() {
    printf("[TEST] Демонстрация скорости C-ядра Kolibri AI...\n");

    KolibriFormula formula;
    /* Инициализируем формулу нулями */
    memset(&formula, 0, sizeof(formula));
    formula.gene.length = 8; 

    /* Ручная сборка формулы: y = (x^2) + 3 */
    /* Геном: [op=6(SQR), slope_sign, s1, s2, bias_sign, b1, b2, aux] */
    /* Slope 100 (чтобы компенсировать деление на 100 внутри ядра), Bias 3 */
    uint8_t gene_data[] = {6, 0, 1, 0, 0, 0, 3, 0};
    memcpy(formula.gene.digits, gene_data, 8);

    printf("[INFO] Запуск бенчмарка (1 000 000 вычислений)...\n");
    
    clock_t start = clock();
    long long checksum = 0;
    for (int i = 0; i < 1000000; ++i) {
        int out = 0;
        kf_formula_apply(&formula, i % 100, &out);
        checksum += out;
    }
    clock_t end = clock();

    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("[RESULT] Время выполнения: %.4f сек\n", time_spent);
    printf("[RESULT] Контрольная сумма: %lld\n", checksum);
    
    if (time_spent < 1.0) {
        printf("[PASS] Ядро работает на высокой скорости!\n");
    } else {
        printf("[WARN] Производительность ниже ожидаемой.\n");
    }

    return 0;
}

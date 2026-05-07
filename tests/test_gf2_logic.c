/*
 * Тест мгновенного обучения: GF(2) Linear Algebra Solver
 * Задача: Найти логику XOR между битами входа и выхода
 */
#include "kolibri/spectral.h"
#include <stdio.h>
#include <time.h>

int main() {
    printf("[TEST] Мгновенное решение логических задач (GF(2) Solver)...\n");

    #define N 32
    int inputs[N];
    int outputs[N];
    int predicted[N];

    /* Генерируем задачу: y = x ^ KEY (XOR с ключом) */
    int key = 0xA5; 
    for (int i = 0; i < N; ++i) {
        inputs[i] = i;
        outputs[i] = i ^ key;
    }

    printf("[INFO] Решение системы из %d уравнений...\n", N);
    clock_t start = clock();
    
    kolibri_solve_logic_gf2(inputs, outputs, N, 8, predicted);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;

    printf("[TIME] Затрачено: %.6f сек\n", time_spent);
    
    /* Проверка */
    int correct = 1;
    for (int i = 0; i < N; ++i) {
        if (predicted[i] != outputs[i]) {
            correct = 0;
            break;
        }
    }

    if (correct && time_spent < 0.001) {
        printf("[PASS] Логика найдена мгновенно и верно!\n");
    } else {
        printf("[WARN] Есть неточности или задержки.\n");
    }

    return 0;
}

/*
 * Тест сложной нелинейной задачи для проверки Роя
 * Задача: y = (x * x + 3 * x + 7) % 13 
 */
#include "kolibri/spectral.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    printf("[TEST] Сложная нелинейная задача для Роя...\n");

    #define N 50
    int inputs[N];
    int outputs[N];

    /* Генерируем данные: y = (x^2 + 3x + 7) mod 13 */
    for (int i = 0; i < N; ++i) {
        inputs[i] = i;
        outputs[i] = (i * i + 3 * i + 7) % 13;
    }

    printf("[INFO] Данные (первые 10): ");
    for (int i = 0; i < 10; ++i) printf("%d->%d ", inputs[i], outputs[i]);
    printf("...\n");

    printf("[INFO] Запуск гибридного анализа...\n");
    
    /* Проверяем GF(2) отдельно */
    int *predicted = malloc(N * sizeof(int));
    if (!predicted) {
        printf("[ERROR] Не удалось выделить память\n");
        return 1;
    }
    
    kolibri_solve_logic_gf2(inputs, outputs, N, 8, predicted);
    
    int perfect_match = 1;
    for (int i = 0; i < N; ++i) {
        if (predicted[i] != outputs[i]) {
            perfect_match = 0;
            break;
        }
    }
    free(predicted);

    if (perfect_match) {
        printf("[HYBRID] Решение найдено аналитически (GF2).\n");
    } else {
        printf("[HYBRID] GF2 не справился. Требуется Рой.\n");
    }
    
    printf("[DONE] Тест завершен без падений.\n");
    return 0;
}

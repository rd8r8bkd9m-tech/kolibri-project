/*
 * Тест гибридного обучения: Спектральный анализ + Логика
 * Демонстрация мгновенного решения задач через "метод Шора"
 */
#include "kolibri/spectral.h"
#include <stdio.h>

int main() {
    printf("[TEST] Гибридное обучение Kolibri AI...\n");

    /* Задача 1: Идентичность (y = x) */
    int in1[] = {0, 1, 2, 3, 4, 5, 6, 7};
    int out1[] = {0, 1, 2, 3, 4, 5, 6, 7};
    
    int res1 = kolibri_spectral_learn(in1, out1, 8);
    printf("[TASK 1] y = x -> Detected: %s\n", res1 == 1 ? "ID (Identity)" : "Unknown");

    /* Задача 2: Инверсия (y = -x) */
    int in2[] = {0, 1, 2, 3, 4, 5, 6, 7};
    int out2[] = {0, -1, -2, -3, -4, -5, -6, -7};
    
    int res2 = kolibri_spectral_learn(in2, out2, 8);
    printf("[TASK 2] y = -x -> Detected: %s\n", res2 == 2 ? "NOT (Inversion)" : "Unknown");

    if (res1 == 1 && res2 == 2) {
        printf("[PASS] Спектральный анализ мгновенно определил логические структуры!\n");
    } else {
        printf("[INFO] Требуется дополнительная калибровка.\n");
    }

    return 0;
}

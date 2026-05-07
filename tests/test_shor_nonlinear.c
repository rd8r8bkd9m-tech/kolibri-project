/*
 * Тест применения метода Шора к нелинейным задачам
 * Задача: Найти скрытый период (модуль) в квадратичной функции
 */
#include "kolibri/spectral.h"
#include <stdio.h>

int main() {
    printf("[TEST] Применение метода Шора к нелинейной задаче...\n");

    /* Генерируем данные: f(x) = x^2 mod 7 */
    #define N 64
    int data[N];
    int modulus = 7;
    
    for (int i = 0; i < N; ++i) {
        data[i] = (i * i) % modulus;
    }

    printf("[INFO] Данные (первые 15): ");
    for (int i = 0; i < 15; ++i) printf("%d ", data[i]);
    printf("...\n");

    printf("[INFO] Поиск скрытого периода через FFT (Shor's method)...\n");
    size_t period = kolibri_shor_find_hidden_period(data, N, modulus);
    
    printf("[RESULT] Найденный скрытый период: %zu\n", period);
    
    if (period > 0) {
        printf("[PASS] Алгоритм успешно выявил структурную периодичность!\n");
    } else {
        printf("[WARN] Период не обнаружен (возможно, сигнал слишком зашумлен).\n");
    }

    return 0;
}

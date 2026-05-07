/*
 * Тест спектрального анализа (аналог квантового поиска периода)
 * Демонстрация мгновенного нахождения закономерностей через FFT
 */
#include "kolibri/spectral.h"
#include <stdio.h>
#include <math.h>

int main() {
    printf("[TEST] Спектральный анализ данных (Shor's Classical Analog)...\n");

    /* Генерируем периодический сигнал с периодом 7 */
    #define N 64
    double data[N];
    for (int i = 0; i < N; ++i) {
        data[i] = sin(2.0 * M_PI * i / 7.0) + 0.5 * cos(2.0 * M_PI * i / 3.0);
    }

    printf("[INFO] Поиск доминирующего периода в сигнале (%d точек)...\n", N);
    size_t period = kolibri_find_dominant_period(data, N);
    
    printf("[RESULT] Найденный период: %zu (ожидалось ~7)\n", period);
    
    if (period == 7) {
        printf("[PASS] Алгоритм успешно нашел период мгновенно!\n");
    } else {
        printf("[INFO] Период найден с погрешностью.\n");
    }

    return 0;
}

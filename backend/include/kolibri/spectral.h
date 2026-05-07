#ifndef KOLIBRI_SPECTRAL_H
#define KOLIBRI_SPECTRAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double real;
    double imag;
} Complex;

/** Быстрое преобразование Фурье (Cooley-Tukey) */
void kolibri_fft(Complex *x, size_t n);

/** Обратное быстрое преобразование Фурье */
void kolibri_ifft(Complex *x, size_t n);

/** Нахождение доминирующего периода в последовательности */
size_t kolibri_find_dominant_period(const double *data, size_t n);

/** Спектральное обучение: попытка найти логическую структуру через анализ сигналов */
/* Возвращает тип операции: 0 - неизвестно, 1 - ID, 2 - NOT, 3 - SHIFT */
int kolibri_spectral_learn(const int *inputs, const int *outputs, size_t n);

/** Метод Шора для нелинейных задач: поиск скрытого периода/структуры */
/* Пытается найти период P, такой что f(x) = f(x + P) в модульной арифметике */
size_t kolibri_shor_find_hidden_period(const int *data, size_t n, int modulus);

/** Мгновенное решение логических задач через линейную алгебру GF(2) */
void kolibri_solve_logic_gf2(const int *inputs, const int *outputs, size_t n_samples, int n_bits, int *predicted);

/** Гибридный солвер: Аналитика (Шор/GF2) + Эволюция (Рой) */
void kolibri_hybrid_solve(const int *inputs, const int *outputs, size_t n, int target_hash, void *pool, int max_generations);

#endif /* KOLIBRI_SPECTRAL_H */

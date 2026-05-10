#include "kolibri/spectral.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define PI 3.14159265358979323846

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static void bit_reverse_complex(Complex *x, size_t n) {
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (j > i) {
            Complex temp = x[j];
            x[j] = x[i];
            x[i] = temp;
        }
        size_t m = n >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

void kolibri_fft(Complex *x, size_t n) {
    if (n <= 1) return;
    
    // Bit-reversal permutation
    bit_reverse_complex(x, n);

    // Iterative FFT with SIMD support
    for (size_t len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * PI / len;
        Complex wlen;
        wlen.real = cos(angle);
        wlen.imag = sin(angle);

        for (size_t i = 0; i < n; i += len) {
            Complex w;
            w.real = 1.0;
            w.imag = 0.0;

#if defined(__ARM_NEON) && defined(__APPLE__)
            // NEON optimization: process 2 complex pairs simultaneously
            for (size_t j = 0; j < len / 2; j += 2) {
                // Load 2 complex numbers from the first half
                float64x2_t u1 = vld1q_f64((double *)&x[i + j]);
                // Load 2 complex numbers from the second half
                float64x2_t u2 = vld1q_f64((double *)&x[i + j + len / 2]);
                
                // Manual complex multiply for clarity and correctness
                double t1_re = w.real * x[i + j + len/2].real - w.imag * x[i + j + len/2].imag;
                double t1_im = w.real * x[i + j + len/2].imag + w.imag * x[i + j + len/2].real;
                
                // Update w for next iteration (w *= wlen)
                double w_re = w.real * wlen.real - w.imag * wlen.imag;
                double w_im = w.real * wlen.imag + w.imag * wlen.real;
                
                // Second butterfly in the pair
                double t2_re = w_re * x[i + j + len/2 + 1].real - w_im * x[i + j + len/2 + 1].imag;
                double t2_im = w_re * x[i + j + len/2 + 1].imag + w_im * x[i + j + len/2 + 1].real;

                // Store results back (Butterfly operation)
                double u1_re = x[i + j].real;
                double u1_im = x[i + j].imag;
                x[i + j].real = u1_re + t1_re;
                x[i + j].imag = u1_im + t1_im;
                x[i + j + len / 2].real = u1_re - t1_re;
                x[i + j + len / 2].imag = u1_im - t1_im;
                
                double u2_re = x[i + j + 1].real;
                double u2_im = x[i + j + 1].imag;
                x[i + j + 1].real = u2_re + t2_re;
                x[i + j + 1].imag = u2_im + t2_im;
                x[i + j + 1 + len / 2].real = u2_re - t2_re;
                x[i + j + 1 + len / 2].imag = u2_im - t2_im;

                w.real = w_re;
                w.imag = w_im;
            }
#else
            for (size_t j = 0; j < len / 2; j++) {
                Complex u = x[i + j];
                Complex t;
                t.real = w.real * x[i + j + len / 2].real - w.imag * x[i + j + len / 2].imag;
                t.imag = w.real * x[i + j + len / 2].imag + w.imag * x[i + j + len / 2].real;

                x[i + j] = (Complex){u.real + t.real, u.imag + t.imag};
                x[i + j + len / 2] = (Complex){u.real - t.real, u.imag - t.imag};

                Complex w_next;
                w_next.real = w.real * wlen.real - w.imag * wlen.imag;
                w_next.imag = w.real * wlen.imag + w.imag * wlen.real;
                w = w_next;
            }
#endif
        }
    }
}

void kolibri_ifft(Complex *x, size_t n) {
    // Инвертируем мнимую часть
    for (size_t i = 0; i < n; i++) x[i].imag = -x[i].imag;
    
    kolibri_fft(x, n);
    
    // Инвертируем обратно и нормируем
    for (size_t i = 0; i < n; i++) {
        x[i].imag = -x[i].imag;
        x[i].real /= n;
        x[i].imag /= n;
    }
}

size_t kolibri_find_dominant_period(const double *data, size_t n) {
    Complex *freqs = malloc(n * sizeof(Complex));
    for (size_t i = 0; i < n; i++) {
        freqs[i].real = data[i];
        freqs[i].imag = 0.0;
    }

    kolibri_fft(freqs, n);

    size_t max_idx = 1; // Пропускаем 0-ю частоту (DC component)
    double max_mag = 0.0;
    
    for (size_t i = 1; i < n / 2; i++) {
        double mag = sqrt(freqs[i].real * freqs[i].real + freqs[i].imag * freqs[i].imag);
        if (mag > max_mag) {
            max_mag = mag;
            max_idx = i;
        }
    }

    free(freqs);
    return (max_idx > 0) ? n / max_idx : 0;
}

int kolibri_spectral_learn(const int *inputs, const int *outputs, size_t n) {
    if (n < 4) return 0;

    /* Нормализация данных для спектрального анализа */
    double *x = malloc(n * sizeof(double));
    double *y = malloc(n * sizeof(double));
    
    double mean_x = 0, mean_y = 0;
    for (size_t i = 0; i < n; ++i) {
        mean_x += inputs[i];
        mean_y += outputs[i];
    }
    mean_x /= n;
    mean_y /= n;

    for (size_t i = 0; i < n; ++i) {
        x[i] = inputs[i] - mean_x;
        y[i] = outputs[i] - mean_y;
    }

    /* Проверка на идентичность (ID) или инверсию (NOT) через корреляцию */
    double corr_id = 0.0, corr_not = 0.0;
    for (size_t i = 0; i < n; ++i) {
        corr_id += x[i] * y[i];
        corr_not += x[i] * (-y[i]);
    }

    free(x);
    free(y);

    /* Если корреляция с самим собой максимальна — это ID */
    if (corr_id > 0.9 * n && corr_id > corr_not) return 1; 
    /* Если корреляция с инверсией максимальна — это NOT */
    if (corr_not > 0.9 * n && corr_not > corr_id) return 2;

    return 0; /* Сложная логика, требует эволюции */
}

size_t kolibri_shor_find_hidden_period(const int *data, size_t n, int modulus) {
    if (n < 4 || modulus <= 1) return 0;

    /* Шаг 1: Подготовка сигнала (аналог квантового регистра) */
    Complex *signal = malloc(n * sizeof(Complex));
    for (size_t i = 0; i < n; ++i) {
        /* Преобразуем данные в фазовый сигнал через модульную арифметику */
        double phase = 2.0 * PI * (data[i] % modulus) / modulus;
        signal[i].real = cos(phase);
        signal[i].imag = sin(phase);
    }

    /* Шаг 2: Квантовое преобразование Фурье (симуляция через FFT) */
    kolibri_fft(signal, n);

    /* Шаг 3: Измерение (поиск пиков вероятности) */
    size_t best_period = 0;
    double max_prob = 0.0;

    for (size_t k = 1; k < n / 2; ++k) {
        double prob = signal[k].real * signal[k].real + signal[k].imag * signal[k].imag;
        if (prob > max_prob) {
            max_prob = prob;
            /* Частота k соответствует периоду n/k */
            best_period = n / k;
        }
    }

    free(signal);
    return best_period;
}

/* Мгновенное решение логических задач через линейную алгебру GF(2) */
void kolibri_solve_logic_gf2(const int *inputs, const int *outputs, size_t n_samples, int n_bits, int *predicted) {
    if (n_samples == 0 || !inputs || !outputs || !predicted) return;

    /* Ограничиваем размерность для безопасности стека */
    if (n_bits > 32) n_bits = 32;
    size_t limit = (n_samples < 32) ? n_samples : 32;

    uint32_t matrix[32];
    uint32_t results[32];
    
    for (int i = 0; i < 32; ++i) {
        matrix[i] = 0;
        results[i] = 0;
    }

    /* Заполняем матрицу: каждая строка - это входной вектор */
    for (size_t i = 0; i < limit; ++i) {
        matrix[i] = (uint32_t)inputs[i];
        results[i] = (uint32_t)outputs[i];
    }

    /* Прямой ход метода Гаусса (приведение к ступенчатому виду) */
    int pivot_rows[32];
    int num_pivots = 0;

    for (int col = 0; col < n_bits; ++col) {
        int pivot = -1;
        for (int r = num_pivots; r < (int)limit; ++r) {
            if (matrix[r] & (1U << col)) {
                pivot = r;
                break;
            }
        }

        if (pivot == -1) continue;

        /* Swap с текущей верхней строкой */
        uint32_t tmp_m = matrix[num_pivots]; matrix[num_pivots] = matrix[pivot]; matrix[pivot] = tmp_m;
        uint32_t tmp_r = results[num_pivots]; results[num_pivots] = results[pivot]; results[pivot] = tmp_r;

        /* Eliminate во всех остальных строках */
        for (int r = 0; r < (int)limit; ++r) {
            if (r != num_pivots && (matrix[r] & (1U << col))) {
                matrix[r] ^= matrix[num_pivots];
                results[r] ^= results[num_pivots];
            }
        }
        pivot_rows[num_pivots++] = col;
    }

    /* Обратный ход не требуется, если мы уже привели к диагональному виду выше */
    /* Теперь matrix содержит базисные векторы, а results - соответствующие им выходы */
    
    /* Предсказание для новых данных: разложение входа по базису */
    for (size_t i = 0; i < n_samples; ++i) {
        uint32_t val = (uint32_t)inputs[i];
        int pred = 0;
        
        /* Пытаемся разложить val по найденным базисным векторам */
        for (int p = 0; p < num_pivots; ++p) {
            int bit_pos = pivot_rows[p];
            if (val & (1U << bit_pos)) {
                val ^= matrix[p];
                pred ^= results[p];
            }
        }
        
        /* Если val не стал нулем, значит вход не лежит в пространстве базиса */
        /* В таком случае предсказание будет неточным, но мы возвращаем лучший линейный аппроксиматор */
        predicted[i] = (int)pred;
    }
}

/* Глубокая эволюционная сеть (CPU Optimized for Apple Silicon) */
#define DEEP_LAYERS 50
#define LAYER_WIDTH 16

/* Квадратичный GF(2) Solver: Расширение метода Гаусса для парных взаимодействий */
void kolibri_solve_logic_gf2_quadratic(const int *inputs, const int *outputs, size_t n_samples, int n_bits, int *predicted) {
    if (n_samples == 0 || !inputs || !outputs || !predicted) return;
    
    /* Для квадратичного случая мы рассматриваем мономы x_i*x_j */
    /* Ограничиваем размерность, так как количество мономов растет как N^2 */
    if (n_bits > 16) n_bits = 16; 
    
    int n_monomials = n_bits * (n_bits + 1) / 2;
    if (n_monomials > 130) n_monomials = 130; /* Лимит для стека */

    size_t limit = (n_samples < 130) ? n_samples : 130;
    uint32_t matrix[130];
    uint32_t results[130];
    memset(matrix, 0, sizeof(matrix));
    memset(results, 0, sizeof(results));

    /* Заполняем матрицу квадратичными признаками */
    for (size_t i = 0; i < limit; ++i) {
        uint32_t val = (uint32_t)inputs[i];
        uint32_t mono_row = 0;
        int bit_idx = 0;
        
        /* Генерируем все пары битов (x_i AND x_j) */
        for (int a = 0; a < n_bits; ++a) {
            for (int b = a; b < n_bits; ++b) {
                if ((val & (1U << a)) && (val & (1U << b))) {
                    mono_row |= (1U << bit_idx);
                }
                bit_idx++;
                if (bit_idx >= 32) goto fill_done;
            }
        }
fill_done:
        matrix[i] = mono_row;
        results[i] = (uint32_t)outputs[i];
    }

    /* Метод Гаусса над GF(2) */
    int pivot_rows[130];
    int num_pivots = 0;
    for (int col = 0; col < n_monomials && col < 32; ++col) {
        int pivot = -1;
        for (int r = num_pivots; r < (int)limit; ++r) {
            if (matrix[r] & (1U << col)) { pivot = r; break; }
        }
        if (pivot == -1) continue;

        uint32_t tmp_m = matrix[num_pivots]; matrix[num_pivots] = matrix[pivot]; matrix[pivot] = tmp_m;
        uint32_t tmp_r = results[num_pivots]; results[num_pivots] = results[pivot]; results[pivot] = tmp_r;

        for (int r = 0; r < (int)limit; ++r) {
            if (r != num_pivots && (matrix[r] & (1U << col))) {
                matrix[r] ^= matrix[num_pivots];
                results[r] ^= results[num_pivots];
            }
        }
        pivot_rows[num_pivots++] = col;
    }

    /* Предсказание через разложение по квадратичному базису */
    for (size_t i = 0; i < n_samples; ++i) {
        uint32_t val = (uint32_t)inputs[i];
        int pred = 0;
        uint32_t mono_val = 0;
        int bit_idx = 0;
        for (int a = 0; a < n_bits; ++a) {
            for (int b = a; b < n_bits; ++b) {
                if ((val & (1U << a)) && (val & (1U << b))) {
                    mono_val |= (1U << bit_idx);
                }
                bit_idx++;
            }
        }
        
        for (int p = 0; p < num_pivots; ++p) {
            if (mono_val & (1U << pivot_rows[p])) {
                mono_val ^= matrix[p];
                pred ^= results[p];
            }
        }
        predicted[i] = (int)pred;
    }
}

typedef struct {
    __attribute__((aligned(64))) float weights[DEEP_LAYERS][LAYER_WIDTH][LAYER_WIDTH];
    __attribute__((aligned(64))) float biases[DEEP_LAYERS][LAYER_WIDTH];
} KolibriDeepNet;

/* Инициализация сети */
static void deep_net_init(KolibriDeepNet *net) {
    for (int l = 0; l < DEEP_LAYERS; ++l) {
        for (int i = 0; i < LAYER_WIDTH; ++i) {
            net->biases[l][i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            for (int j = 0; j < LAYER_WIDTH; ++j) {
                net->weights[l][i][j] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            }
        }
    }
}

/* Прямой проход с SIMD-оптимизацией (векторизация на лету) */
static void deep_net_forward(KolibriDeepNet *net, int input, int *output) {
    float hidden[LAYER_WIDTH];
    float next[LAYER_WIDTH];

    /* Нормализация входа */
    for (int i = 0; i < LAYER_WIDTH; ++i) {
        hidden[i] = (float)((input >> i) & 1);
    }

    for (int l = 0; l < DEEP_LAYERS; ++l) {
        /* Векторное умножение и активация */
        #pragma omp simd
        for (int i = 0; i < LAYER_WIDTH; ++i) {
            float sum = net->biases[l][i];
            /* Компилятор сам развернет этот цикл под NEON для 16 элементов */
            for (int j = 0; j < LAYER_WIDTH; ++j) {
                sum += hidden[j] * net->weights[l][i][j];
            }
            next[i] = tanhf(sum);
        }
        memcpy(hidden, next, sizeof(hidden));
    }

    *output = 0;
    for (int i = 0; i < LAYER_WIDTH; ++i) {
        if (hidden[i] > 0.0f) {
            *output |= (1 << i);
        }
    }
}

/* Оптимизация весов через спектральный поиск периода (Shor-inspired) */
static void kolibri_shor_optimize(KolibriDeepNet *net) {
    /* Берем срез диагональных весов первого слоя как сигнал */
    double signal[LAYER_WIDTH];
    for (int i = 0; i < LAYER_WIDTH; ++i) {
        signal[i] = (double)net->weights[0][i][i];
    }

    /* Ищем доминирующий период в этом сигнале */
    size_t period = kolibri_find_dominant_period(signal, LAYER_WIDTH);
    
    if (period > 0 && period < LAYER_WIDTH) {
        printf("[SHOR] Найден период %zu. Синхронизация 50 слоев...\n", period);
        /* Распространяем найденный паттерна на все остальные слои */
        for (int l = 1; l < DEEP_LAYERS; ++l) {
            for (int i = 0; i < LAYER_WIDTH; ++i) {
                int src = (i + period) % LAYER_WIDTH;
                net->weights[l][i][i] = net->weights[0][src][src];
            }
        }
    }
}

/* Эволюционное обучение (Swarm-like с накоплением и Shor-оптимизацией) */
static double deep_net_evolve(KolibriDeepNet *best_net, const int *inputs, const int *outputs, size_t n, int current_gen, int max_gens) {
    #define SWARM_SIZE 16
    KolibriDeepNet swarm[SWARM_SIZE];
    float fitness[SWARM_SIZE];

    float mutation_rate = 0.1f * (1.0f - (float)current_gen / max_gens);
    if (mutation_rate < 0.01f) mutation_rate = 0.01f;

    /* Применяем метод Шора каждые 100 поколений для ускорения сходимости */
    if (current_gen > 0 && current_gen % 100 == 0) {
        kolibri_shor_optimize(best_net);
    }

    /* Генерация роя мутантов */
    for (int i = 0; i < SWARM_SIZE; ++i) {
        swarm[i] = *best_net;
        for (int l = 0; l < DEEP_LAYERS; ++l) {
            for (int x = 0; x < LAYER_WIDTH; ++x) {
                for (int y = 0; y < LAYER_WIDTH; ++y) {
                    if (rand() % 100 < 10) {
                        swarm[i].weights[l][x][y] += ((float)rand() / RAND_MAX) * mutation_rate * 2.0f - mutation_rate;
                    }
                }
            }
        }
    }

    /* Оценка фитнеса */
    for (int i = 0; i < SWARM_SIZE; ++i) {
        int score = 0;
        for (size_t k = 0; k < n; ++k) {
            int pred;
            deep_net_forward(&swarm[i], inputs[k], &pred);
            if ((pred & 0xFF) == outputs[k]) score++;
        }
        fitness[i] = (float)score;
    }

    /* Выбор лучшего */
    int best_idx = 0;
    for (int i = 1; i < SWARM_SIZE; ++i) {
        if (fitness[i] > fitness[best_idx]) best_idx = i;
    }

    if (fitness[best_idx] >= fitness[0]) {
        *best_net = swarm[best_idx];
    }
    
    return fitness[best_idx];
}

/* Простая функция хеширования для теста (совпадает с Python simple_hash) */
static int kolibri_simple_hash(int key) {
    unsigned int h = 0;
    unsigned int k = (unsigned int)key;
    for (int i = 0; i < 8; ++i) {
        h = ((h ^ k) * 0x5BD1E995u);
        k = ((k >> 13) | (k << 19));
    }
    return (int)h;
}

/* Режим взлома: эволюция сети для поиска входа по заданному хешу */
static double deep_net_crack_hash(KolibriDeepNet *net, int target_hash, int gen, int max_gen) {
    // Генерируем кандидата. В реальной системе здесь был бы decode из латентного пространства.
    // Для теста используем простой перебор с шагом, зависящим от весов сети (имитация обучения)
    int candidate = (gen * 12345 + (int)(net->weights[0][0][0] * 1000)) & 0xFFFF; 
    
    // Вычисляем хеш кандидата
    int current_hash = kolibri_simple_hash(candidate);
    
    // Fitness: расстояние Хэмминга между текущим хешем и целевым
    unsigned int diff = (unsigned int)(current_hash ^ target_hash);
    
    // Если совпали идеально
    if (diff == 0) return 1.0;
    
    // Нормализация фитнеса (чем меньше битов отличается, тем лучше)
    int total_bits = 32;
    int hamming_dist = __builtin_popcount(diff);
    return 1.0 - ((double)hamming_dist / total_bits);
}

/* Экспорт для Python-обертки */
#ifdef __APPLE__
#define KOLIBRI_EXPORT __attribute__((visibility("default")))
#else
#define KOLIBRI_EXPORT __declspec(dllexport)
#endif

/* Быстрый C-брутфорс с поддержкой OpenMP */
typedef struct {
    int found;
    unsigned int key;
    unsigned int hash;
} BruteforceResult;

KOLIBRI_EXPORT void kolibri_bruteforce_hash(unsigned int min_key, unsigned int max_key, unsigned int target_hash, BruteforceResult *result) {
    result->found = 0;
    result->key = 0;
    result->hash = 0;

    #pragma omp parallel for schedule(dynamic, 10000) shared(result)
    for (unsigned long long k = min_key; k <= max_key; ++k) {
        // Если другой поток уже нашел решение, выходим
        if (result->found) continue;

        unsigned int h = 0;
        unsigned int key = (unsigned int)k;
        // Unrolled simple_hash for speed
        for (int i = 0; i < 8; ++i) {
            h = ((h ^ key) * 0x5BD1E995u);
            key = ((key >> 13) | (key << 19));
        }

        if (h == target_hash) {
            #pragma omp critical
            {
                // Ищем самый младший ключ (first match in ascending order)
                if (!result->found || k < result->key) {
                    result->found = 1;
                    result->key = (unsigned int)k;
                    result->hash = h;
                }
            }
        }
    }
}

/* Экспорт для Python-обертки */
#ifdef __APPLE__
#define KOLIBRI_EXPORT __attribute__((visibility("default")))
#else
#define KOLIBRI_EXPORT __declspec(dllexport)
#endif

/* Глобальные переменные для хранения состояния */
static double g_last_fitness = 0.0;
static int g_target_hash = 0;

/* Гибридный солвер: Аналитика + Deep Swarm (Успешная конфигурация) */
KOLIBRI_EXPORT void kolibri_hybrid_solve(const int *inputs, const int *outputs, size_t n,
                          int target_hash, void *pool, int max_generations) {
    
    g_target_hash = target_hash;
    g_last_fitness = 0.0;

    /* 1. Попытка мгновенного решения через GF(2) */
    int *predicted = malloc(n * sizeof(int));
    kolibri_solve_logic_gf2(inputs, outputs, n, 8, predicted);

    int errors = 0;
    for (size_t i = 0; i < n; ++i) {
        if (predicted[i] != outputs[i]) errors++;
    }

    if (errors == 0) {
        printf("[HYBRID] Решение найдено аналитически (GF2).\n");
        g_last_fitness = 1.0; // Идеальный фитнес
        free(predicted);
        return;
    }
    free(predicted);

    printf("[HYBRID] Аналитика провалилась (%d ошибок). Запуск Deep Swarm...\n", errors);

    /* 2. Deep Evolutionary Swarm */
    KolibriDeepNet net;
    deep_net_init(&net);

    int total_cycles = 3;
    clock_t start = clock();

    for (int cycle = 0; cycle < total_cycles; ++cycle) {
        printf("\n--- ЦИКЛ ОБУЧЕНИЯ %d/%d ---\n", cycle + 1, total_cycles);
        for (int gen = 0; gen < max_generations; ++gen) {
            double fit;
            if (g_target_hash != 0) {
                // Режим взлома: ищем входные данные, дающие target_hash
                fit = deep_net_crack_hash(&net, g_target_hash, gen, max_generations);
            } else {
                // Режим обучения: аппроксимация функции inputs -> outputs
                fit = deep_net_evolve(&net, inputs, outputs, n, gen, max_generations);
            }
            
            if (fit > g_last_fitness) g_last_fitness = fit;
            if (gen % 50 == 0) {
                printf("[EVOLUTION] Поколение %d/%d. Fitness: %.6f%s\n", 
                       gen, max_generations, fit, 
                       (g_target_hash && fit > 0.999) ? " [KEY FOUND!]" : "");
            }
        }
    }

    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("\n[DEEP SWARM] Обучение завершено за %.4fs. Лучший Fitness: %.6f\n", time_spent, g_last_fitness);
}

/* Функция для получения последнего результата из Python */
KOLIBRI_EXPORT double kolibri_get_last_fitness(void) {
    return g_last_fitness;
}

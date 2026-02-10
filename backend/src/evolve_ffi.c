/*
 * evolve_ffi.c — FFI-обёртка для Python ctypes
 * 
 * Генетический алгоритм Kolibri на C23 для 100x ускорения
 * вместо чистого Python evolve(). Компилируется как shared library:
 *   gcc -O3 -shared -fPIC -o libkolibri_evolve.so evolve_ffi.c -lm
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- Константы (зеркало Python number_mind.py) --- */

#define FFI_GENE_SIZE         4000    /* Цифр в геноме */
#define FFI_FORMULA_LAYERS    500     /* Слоёв нейросети */
#define FFI_POPULATION_SIZE   16      /* Формул в популяции */
#define FFI_PATTERN_SIZE      64      /* Цифр в числовом паттерне */
#define FFI_MAX_EVAL_PAIRS    60      /* Макс пар для оценки fitness */
#define FFI_EVAL_DIGITS       24      /* Цифр для оценки */
#define FFI_RESNET_BLOCK_SIZE 10      /* Слоёв в residual-блоке */

/* --- Генератор случайных чисел (LCG) --- */

static uint64_t _rng_state = 42;

static void ffi_rng_seed(uint64_t seed) {
    _rng_state = seed ? seed : (uint64_t)time(NULL);
}

static uint64_t ffi_rng_next(void) {
    _rng_state = _rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return _rng_state >> 16;
}

static double ffi_rng_double(void) {
    return (double)(ffi_rng_next() & 0xFFFFFFFF) / 4294967296.0;
}

/* --- Геном формулы --- */

typedef struct {
    uint8_t digits[FFI_GENE_SIZE];
    double  fitness;
} FFIFormula;

/* --- Predict (зеркало Python KolibriGene.predict_fast) --- */

static double ffi_predict_fast(const uint8_t *digits, double x) {
    /* 500-слойная ResNet, блоки по 10 слоёв, 8 параметров на слой */
    size_t total_layers = FFI_GENE_SIZE / 8;
    if (total_layers > FFI_FORMULA_LAYERS) total_layers = FFI_FORMULA_LAYERS;
    size_t num_blocks = total_layers / FFI_RESNET_BLOCK_SIZE;
    if (num_blocks == 0) num_blocks = 1;

    for (size_t block = 0; block < num_blocks; ++block) {
        double residual = x;
        for (size_t sub = 0; sub < FFI_RESNET_BLOCK_SIZE; ++sub) {
            size_t layer = block * FFI_RESNET_BLOCK_SIZE + sub;
            if (layer >= total_layers) break;

            size_t off = layer * 8;
            int op   = digits[off] % 12;
            int sign = (digits[off + 1] % 2 == 0) ? 1 : -1;
            int mag  = digits[off + 2] * 10 + digits[off + 3];
            double slope = sign * mag * 0.01;
            int bsign = (digits[off + 4] % 2 == 0) ? 1 : -1;
            int bmag  = digits[off + 5] * 10 + digits[off + 6];
            double bias = bsign * bmag * 0.01;
            int aux = (int)digits[off + 7];

            double result;
            switch (op) {
            case 0:  result = slope * x + bias; break;
            case 1:  result = (fabs(x) > 0.001) ? (slope / x + bias) : bias; break;
            case 2:  { int d = (aux == 0) ? 1 : aux; result = fmod(slope * x, (double)d) + bias; break; }
            case 3:  result = slope * x * x + bias; break;
            case 4:  result = (double)((int)(slope * 100) ^ (int)(x * 100)) * 0.01 + bias; break;
            case 5:  result = (double)((int)(slope * 100) & (int)(x * 100)) * 0.01 + bias; break;
            case 6:  result = sin(x * 0.1) * slope + bias; break;
            case 7:  result = slope * x / (1.0 + fabs(x)) + bias; break;
            case 8:  result = (double)((int)(slope * 100) | (int)(x * 100)) * 0.01 + bias; break;
            case 9:  result = exp(-x * x * 0.01) * slope + bias; break;
            case 10: result = tanh(x * 0.1) * slope + bias; break;
            case 11: { double arg = -x * 0.1; if (arg > 50.0) arg = 50.0; if (arg < -50.0) arg = -50.0;
                       result = (2.0 / (1.0 + exp(arg)) - 1.0) * slope + bias; break; }
            default: result = x + bias; break;
            }

            /* tanh soft clipping */
            if (result > 5.0) result = 5.0 * tanh(result / 5.0);
            if (result < -5.0) result = -5.0 * tanh(-result / 5.0);
            x = result;
        }
        /* ResNet residual connection */
        x = x * 0.7 + residual * 0.3;
        /* Layer normalization */
        if (x > 10.0) x = 10.0;
        if (x < -10.0) x = -10.0;
    }
    return x;
}

/* --- Crossover --- */

static void ffi_crossover(const uint8_t *p1, const uint8_t *p2, uint8_t *child) {
    /* Двухточечный кроссовер */
    size_t a = ffi_rng_next() % FFI_GENE_SIZE;
    size_t b = ffi_rng_next() % FFI_GENE_SIZE;
    if (a > b) { size_t t = a; a = b; b = t; }
    for (size_t i = 0; i < FFI_GENE_SIZE; ++i) {
        child[i] = (i >= a && i < b) ? p2[i] : p1[i];
    }
}

/* --- Мутация --- */

static void ffi_mutate(uint8_t *digits, double rate) {
    size_t mutations = (size_t)(FFI_GENE_SIZE * rate);
    if (mutations == 0) mutations = 1;
    for (size_t m = 0; m < mutations; ++m) {
        size_t idx = ffi_rng_next() % FFI_GENE_SIZE;
        digits[idx] = (uint8_t)(ffi_rng_next() % 12);
    }
}

/* --- Сложность генома (diversity metric) --- */

static double ffi_complexity(const uint8_t *digits) {
    int counts[12] = {0};
    for (size_t i = 0; i < FFI_GENE_SIZE; ++i) {
        counts[digits[i] % 12]++;
    }
    double entropy = 0.0;
    for (int i = 0; i < 12; ++i) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / FFI_GENE_SIZE;
            entropy -= p * log2(p);
        }
    }
    return entropy / log2(12.0); /* Нормализованная энтропия [0,1] */
}

/* ============================================================================
 * Главная функция: ffi_evolve
 *
 * Входы:
 *   genomes      — плоский массив [POPULATION * GENE_SIZE] uint8
 *   fitnesses    — массив [POPULATION] double
 *   src_patterns — плоский массив семантических пар [n_pairs * 2 * PATTERN_SIZE] uint8
 *   n_pairs      — количество семантических пар
 *   generations  — количество поколений
 *   seed         — seed для RNG
 *
 * Выходы:
 *   genomes, fitnesses — обновляются in-place
 *   возвращает лучший fitness
 * ============================================================================ */

#ifdef _WIN32
#define FFI_EXPORT __declspec(dllexport)
#else
#define FFI_EXPORT __attribute__((visibility("default")))
#endif

FFI_EXPORT double ffi_evolve(
    uint8_t  *genomes,       /* [POP * GENE_SIZE] */
    double   *fitnesses,     /* [POP] */
    const uint8_t *src_patterns,  /* [n_pairs * PATTERN_SIZE] */
    const uint8_t *tgt_patterns,  /* [n_pairs * PATTERN_SIZE] */
    int       n_pairs,
    int       generations,
    uint64_t  seed
) {
    if (!genomes || !fitnesses || n_pairs <= 0 || generations <= 0) {
        return 0.0;
    }

    ffi_rng_seed(seed);

    /* Выборка для оценки (до 60 пар) */
    int eval_count = n_pairs;
    int *eval_idx = NULL;
    if (eval_count > FFI_MAX_EVAL_PAIRS) {
        eval_idx = (int *)malloc(FFI_MAX_EVAL_PAIRS * sizeof(int));
        /* Микс: свежие + случайные */
        int half = FFI_MAX_EVAL_PAIRS / 2;
        for (int i = 0; i < half; ++i) {
            eval_idx[i] = n_pairs - half + i;
        }
        for (int i = half; i < FFI_MAX_EVAL_PAIRS; ++i) {
            eval_idx[i] = (int)(ffi_rng_next() % (n_pairs - half));
        }
        eval_count = FFI_MAX_EVAL_PAIRS;
    }

    double best_fitness = 0.0;
    double prev_best = fitnesses[0];

    for (int gen = 0; gen < generations; ++gen) {
        /* === Оценка fitness каждой формулы === */
        for (int f = 0; f < FFI_POPULATION_SIZE; ++f) {
            const uint8_t *genome = &genomes[f * FFI_GENE_SIZE];
            double total_sim = 0.0;

            for (int e = 0; e < eval_count; ++e) {
                int pair_idx = eval_idx ? eval_idx[e] : e;
                const uint8_t *src = &src_patterns[pair_idx * FFI_PATTERN_SIZE];
                const uint8_t *tgt = &tgt_patterns[pair_idx * FFI_PATTERN_SIZE];

                /* Предсказание через predict_fast */
                uint8_t pred[FFI_EVAL_DIGITS];
                for (int i = 0; i < FFI_EVAL_DIGITS; ++i) {
                    int digit = (i < FFI_PATTERN_SIZE) ? src[i] : 0;
                    int ctx_next = src[(i + 1) % FFI_PATTERN_SIZE];
                    int ctx_prev = src[((i - 1) + FFI_PATTERN_SIZE) % FFI_PATTERN_SIZE];
                    double ctx = (ctx_next + ctx_prev) * 0.05;
                    double x = (digit + i * 0.15 + ctx) / 12.0;
                    double raw = ffi_predict_fast(genome, x);
                    pred[i] = (uint8_t)(abs((int)(raw * 7.77)) % 10);
                }

                /* Мягкая метрика сходства */
                double sim = 0.0;
                int cmp_len = FFI_EVAL_DIGITS < FFI_PATTERN_SIZE ? FFI_EVAL_DIGITS : FFI_PATTERN_SIZE;
                for (int j = 0; j < cmp_len; ++j) {
                    int d = abs((int)pred[j] - (int)tgt[j]);
                    if (d == 0)      sim += 3.0;
                    else if (d == 1) sim += 2.0;
                    else if (d == 2) sim += 1.0;
                    else if (d <= 4) sim += 0.3;
                }
                sim /= (3.0 * FFI_EVAL_DIGITS);
                total_sim += sim;
            }

            double avg_sim = total_sim / eval_count;
            double diversity = ffi_complexity(genome);
            fitnesses[f] = avg_sim + diversity * 0.01;
        }

        /* === Сортировка по fitness (insertion sort — 16 элементов) === */
        for (int i = 1; i < FFI_POPULATION_SIZE; ++i) {
            double key_fit = fitnesses[i];
            uint8_t key_genome[FFI_GENE_SIZE];
            memcpy(key_genome, &genomes[i * FFI_GENE_SIZE], FFI_GENE_SIZE);
            int j = i - 1;
            while (j >= 0 && fitnesses[j] < key_fit) {
                fitnesses[j + 1] = fitnesses[j];
                memcpy(&genomes[(j + 1) * FFI_GENE_SIZE], &genomes[j * FFI_GENE_SIZE], FFI_GENE_SIZE);
                j--;
            }
            fitnesses[j + 1] = key_fit;
            memcpy(&genomes[(j + 1) * FFI_GENE_SIZE], key_genome, FFI_GENE_SIZE);
        }

        best_fitness = fitnesses[0];

        /* === Адаптивная мутация === */
        double improvement = best_fitness - prev_best;
        double mutation_rate = (improvement < 0.001) ? 0.04 : 0.015;
        prev_best = best_fitness;

        /* === Элитизм + турнирная селекция + кроссовер === */
        int elite_count = FFI_POPULATION_SIZE / 3;
        if (elite_count < 2) elite_count = 2;

        for (int i = elite_count; i < FFI_POPULATION_SIZE; ++i) {
            /* Турнир из 4 → 2 родителя */
            int t1 = (int)(ffi_rng_next() % elite_count);
            int t2 = (int)(ffi_rng_next() % elite_count);
            int p1 = (fitnesses[t1] >= fitnesses[t2]) ? t1 : t2;
            int t3 = (int)(ffi_rng_next() % elite_count);
            int t4 = (int)(ffi_rng_next() % elite_count);
            int p2 = (fitnesses[t3] >= fitnesses[t4]) ? t3 : t4;

            uint8_t child[FFI_GENE_SIZE];
            ffi_crossover(&genomes[p1 * FFI_GENE_SIZE],
                          &genomes[p2 * FFI_GENE_SIZE], child);
            ffi_mutate(child, mutation_rate);
            memcpy(&genomes[i * FFI_GENE_SIZE], child, FFI_GENE_SIZE);
        }
    }

    if (eval_idx) free(eval_idx);
    return best_fitness;
}

/* --- Embedding training на C (Word2Vec Skip-gram) --- */

static double _sigmoid(double x) {
    if (x > 15.0) return 1.0;
    if (x < -15.0) return 0.0;
    return 1.0 / (1.0 + exp(-x));
}

FFI_EXPORT double ffi_train_embeddings(
    double   *vectors,       /* [vocab_size * dim] — плоский массив */
    const int *edge_src,     /* [n_edges] — индексы source */
    const int *edge_tgt,     /* [n_edges] — индексы target */
    const float *edge_weight, /* [n_edges] — веса рёбер */
    int       n_edges,
    int       vocab_size,
    int       dim,
    int       epochs,
    double    lr,
    int       neg_samples
) {
    if (!vectors || !edge_src || !edge_tgt || n_edges <= 0 || vocab_size < 2) {
        return 0.0;
    }

    double total_loss = 0.0;
    int total_pairs = 0;

    for (int epoch = 0; epoch < epochs; ++epoch) {
        double current_lr = lr * (1.0 - (double)epoch / (epochs + 1));
        double epoch_loss = 0.0;

        /* Shuffle edges (Fisher-Yates на индексах) */
        int *order = (int *)malloc(n_edges * sizeof(int));
        for (int i = 0; i < n_edges; ++i) order[i] = i;
        for (int i = n_edges - 1; i > 0; --i) {
            int j = (int)(ffi_rng_next() % (i + 1));
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }

        for (int ei = 0; ei < n_edges; ++ei) {
            int e = order[ei];
            int src = edge_src[e];
            int tgt = edge_tgt[e];
            float w = edge_weight[e];
            int reps = (int)(w * 3);
            if (reps < 1) reps = 1;
            if (reps > 4) reps = 4;

            for (int r = 0; r < reps; ++r) {
                /* Positive pair */
                double *v1 = &vectors[src * dim];
                double *v2 = &vectors[tgt * dim];
                double dot = 0.0;
                for (int d = 0; d < dim; ++d) dot += v1[d] * v2[d];
                double sig = _sigmoid(dot);
                double grad = (1.0 - sig) * current_lr;
                for (int d = 0; d < dim; ++d) {
                    double g1 = grad * v2[d];
                    double g2 = grad * v1[d];
                    v1[d] += g1;
                    v2[d] += g2;
                }
                epoch_loss += -log(sig + 1e-10);
                total_pairs++;

                /* Negative sampling */
                for (int ns = 0; ns < neg_samples; ++ns) {
                    int neg = (int)(ffi_rng_next() % vocab_size);
                    if (neg == src || neg == tgt) continue;
                    double *v_neg = &vectors[neg * dim];
                    dot = 0.0;
                    for (int d = 0; d < dim; ++d) dot += v1[d] * v_neg[d];
                    sig = _sigmoid(dot);
                    double neg_grad = -sig * current_lr * 0.5;
                    for (int d = 0; d < dim; ++d) {
                        double g1 = neg_grad * v_neg[d];
                        double g2 = neg_grad * v1[d];
                        v1[d] += g1;
                        v_neg[d] += g2;
                    }
                }
            }
        }
        free(order);
        total_loss += epoch_loss;
    }

    return total_loss / (total_pairs > 0 ? total_pairs : 1);
}

/* --- DJB2 хеш (идентичный Python/C) --- */

FFI_EXPORT uint32_t ffi_djb2_hash(const char *s) {
    uint32_t h = 5381;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        h = ((h << 5) + h + *p);
        p++;
    }
    return h;
}

/* --- Word to pattern (зеркало Python: DJB2 + 32-bit LCG) --- */

FFI_EXPORT void ffi_word_to_pattern(const char *word, uint8_t *out_pattern) {
    /* Точное зеркало Python word_to_pattern():
     *   h = djb2_hash(word.lower())
     *   for i in 0..63:
     *       pattern[i] = h % 10
     *       h = (h * 1103515245 + 12345) & 0xFFFFFFFF
     *
     * Примечание: Python передаёт .lower(), здесь lower() не делаем —
     * вызывающий код должен передать уже lowercase строку.
     */
    uint32_t h = ffi_djb2_hash(word);
    for (int i = 0; i < FFI_PATTERN_SIZE; ++i) {
        out_pattern[i] = (uint8_t)(h % 10);
        h = (uint32_t)((uint64_t)h * 1103515245ULL + 12345ULL);
    }
}

/* --- Версия библиотеки --- */

FFI_EXPORT int ffi_version(void) {
    return 20260208;  /* YYYYMMDD */
}

FFI_EXPORT int ffi_gene_size(void) {
    return FFI_GENE_SIZE;
}

FFI_EXPORT int ffi_population_size(void) {
    return FFI_POPULATION_SIZE;
}

FFI_EXPORT int ffi_pattern_size(void) {
    return FFI_PATTERN_SIZE;
}

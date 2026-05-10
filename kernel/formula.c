#include "kolibri/formula.h"

#include "support.h"
#include <stdio.h>
#include <string.h>

/* Compatibility macros for kernel support functions */
#define k_memset memset
#define k_memcpy memcpy
#define k_memcmp memcmp

#define KOLIBRI_FORMULA_CAPACITY (sizeof(((KolibriFormulaPool *)0)->formulas) / sizeof(KolibriFormula))

static uint8_t random_digit(KolibriFormulaPool *pool) {
    uint32_t value = (uint32_t)k_rng_next(&pool->rng);
    return (uint8_t)(value % 10U);
}

static void gene_randomize(KolibriFormulaPool *pool, KolibriGene *gene) {
    gene->length = sizeof(gene->digits);
    for (size_t i = 0; i < gene->length; ++i) {
        gene->digits[i] = random_digit(pool);
    }
}

static int decode_coefficients(const KolibriGene *gene, int *slope, int *bias) {
    if (!gene || !slope || !bias || gene->length < 6U) {
        return -1;
    }
    int raw_slope = (int)(gene->digits[0] * 10 + gene->digits[1]);
    int raw_bias = (int)(gene->digits[2] * 10 + gene->digits[3]);
    *slope = (gene->digits[4] % 2U == 0U) ? raw_slope : -raw_slope;
    *bias = (gene->digits[5] % 2U == 0U) ? raw_bias : -raw_bias;
    return 0;
}

/* --- ResNet-архитектура: до 500 слоёв с Residual Blocks × 8 цифр генома --- */
/* Каждые 10 слоёв = 1 residual block: output = block(x) + x              */
/* Skip-connection гарантирует что сигнал не затухает через 500 слоёв      */
#define KOLIBRI_BLOCK_SIZE 10U

static int formula_predict_multilayer(const KolibriGene *gene, int input) {
    long long x = (long long)input;
    size_t num_layers = gene->length / 8U;
    if (num_layers > 500U) num_layers = 500U;
    size_t num_blocks = num_layers / KOLIBRI_BLOCK_SIZE;
    if (num_blocks == 0U) num_blocks = 1U;

    for (size_t block = 0; block < num_blocks; ++block) {
        /* --- Skip-connection: запоминаем вход блока --- */
        long long residual = x;

        for (size_t sub = 0; sub < KOLIBRI_BLOCK_SIZE; ++sub) {
            size_t layer = block * KOLIBRI_BLOCK_SIZE + sub;
            if (layer >= num_layers) break;

            size_t base = layer * 8U;
            int a = (int)(gene->digits[base] * 10 + gene->digits[base + 1]);
            if (gene->digits[base + 2] % 2U) a = -a;
            int b = (int)(gene->digits[base + 3] * 10 + gene->digits[base + 4]);
            if (gene->digits[base + 5] % 2U) b = -b;
            int op = (int)(gene->digits[base + 6] % 12U);
            int act = (int)(gene->digits[base + 7] % 4U);

            long long result = x;
            switch (op) {
            case 0: result = (long long)a * x / 100 + (long long)b; break;
            case 1: result = (long long)a * x / 100 - (long long)b; break;
            case 2: { /* Модулярная арифметика */
                long long divisor = (long long)(b > 0 ? b * 100 + 1 : 1);
                result = (x % divisor) + (long long)a;
                break;
            }
            case 3: result = (x ^ (long long)(a * 1000 + b)); break;
            case 4: result = (x & 0xFFFFFFL) + (long long)b * 10; break;
            case 5: result = x + (long long)a - (long long)b; break;
            case 6: { /* Квадратичная: мягкая x*|x|/(1+|x|) */
                long long abs_x = x < 0 ? -x : x;
                result = (long long)a * x / (100 + abs_x) + (long long)b;
                break;
            }
            case 7: { /* Степенная: x * 2^a mod b */
                int shift = a & 7;
                long long divisor = (long long)(b > 0 ? b * 100 + 1 : 1);
                result = (x << shift) % divisor;
                break;
            }
            case 8: result = (x >> (a & 3)) + (long long)b; break;
            case 9: result = ((x + (long long)a) * (long long)b) / 100; break;
            case 10: { /* Кусочно-линейная */
                result = (x > 0) ? ((long long)a * x / 100 + (long long)b)
                                  : ((long long)b * x / 100 - (long long)a);
                break;
            }
            default: { /* Битовая ротация + XOR */
                long long rotated = ((x << (a & 3)) | ((x >> (32 - (a & 3))) & 0xF));
                result = rotated ^ (long long)(b * 100);
                break;
            }
            }

            /* Активация */
            switch (act) {
            case 0: break; /* identity */
            case 1: result = result < 0 ? -result : result; break; /* abs */
            case 2: result = result % 10000LL; break; /* mod */
            case 3: result = result & 0xFFFFLL; break; /* mask */
            }

            /* Клиппинг внутри слоя: ±10000 */
            if (result > 10000LL) result = 10000LL;
            if (result < -10000LL) result = -10000LL;
            x = result;
        }

        /* --- Residual connection: output = block(x) + skip --- */
        x = x + residual;

        /* --- Layer Normalization: приводим к ±10000 --- */
        if (x > 10000LL) x = 10000LL;
        if (x < -10000LL) x = -10000LL;
    }
    return (int)x;
}

static double evaluate_formula(const KolibriFormula *formula, const KolibriFormulaPool *pool) {
    if (!formula || !pool || pool->examples == 0U) {
        return 0.0;
    }
    double total_score = 0.0;
    for (size_t i = 0; i < pool->examples; ++i) {
        int prediction = formula_predict_multilayer(&formula->gene, pool->inputs[i]);
        int target = pool->targets[i];
        /* Мягкое сравнение по цифрам (soft digit-matching) */
        int pred_abs = prediction < 0 ? -prediction : prediction;
        int targ_abs = target < 0 ? -target : target;
        double score = 0.0;
        for (int d = 0; d < 6; ++d) {
            int pd = pred_abs % 10;
            int td = targ_abs % 10;
            if (pd == td) {
                score += 1.0;
            } else {
                int diff = pd - td;
                if (diff < 0) diff = -diff;
                if (diff <= 1) score += 0.5;
                else if (diff <= 2) score += 0.2;
            }
            pred_abs /= 10;
            targ_abs /= 10;
        }
        total_score += score / 6.0;
    }
    double normalized = total_score / (double)pool->examples;
    double adjusted = normalized + formula->feedback;
    if (adjusted < 0.0) {
        adjusted = 0.0;
    }
    if (adjusted > 1.0) {
        adjusted = 1.0;
    }
    return adjusted;
}

static void mutate_gene(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene) {
        return;
    }
    /* Адаптивная мультимутация: ~3% генома */
    size_t mutations = gene->length / 32U;
    if (mutations == 0) mutations = 1;
    for (size_t m = 0; m < mutations; ++m) {
        uint32_t value = (uint32_t)k_rng_next(&pool->rng);
        size_t index = (size_t)(value % (gene->length ? gene->length : 1U));
        gene->digits[index] = random_digit(pool);
    }
}

void kf_pool_init(KolibriFormulaPool *pool, uint64_t seed) {
    if (!pool) {
        return;
    }
    k_memset(pool, 0, sizeof(*pool));
    k_rng_seed(&pool->rng, seed);
    pool->count = KOLIBRI_FORMULA_CAPACITY;
    for (size_t i = 0; i < pool->count; ++i) {
        gene_randomize(pool, &pool->formulas[i].gene);
        pool->formulas[i].fitness = 0.0;
        pool->formulas[i].feedback = 0.0;
    }
}

void kf_pool_clear_examples(KolibriFormulaPool *pool) {
    if (!pool) {
        return;
    }
    pool->examples = 0U;
}

int kf_pool_add_example(KolibriFormulaPool *pool, int input, int target) {
    if (!pool || pool->examples >= sizeof(pool->inputs) / sizeof(pool->inputs[0])) {
        return -1;
    }
    pool->inputs[pool->examples] = input;
    pool->targets[pool->examples] = target;
    ++pool->examples;
    return 0;
}

static void evaluate_pool(KolibriFormulaPool *pool) {
    if (!pool) {
        return;
    }
    for (size_t i = 0; i < pool->count; ++i) {
        pool->formulas[i].fitness = evaluate_formula(&pool->formulas[i], pool);
    }
}

static KolibriFormula *select_best(KolibriFormulaPool *pool) {
    if (!pool || pool->count == 0U) {
        return NULL;
    }
    size_t best_index = 0U;
    double best_score = pool->formulas[0].fitness;
    for (size_t i = 1; i < pool->count; ++i) {
        if (pool->formulas[i].fitness > best_score) {
            best_score = pool->formulas[i].fitness;
            best_index = i;
        }
    }
    return &pool->formulas[best_index];
}

void kf_pool_tick(KolibriFormulaPool *pool, size_t generations) {
    if (!pool || pool->count == 0U) {
        return;
    }
    for (size_t gen = 0; gen < generations; ++gen) {
        evaluate_pool(pool);
        KolibriFormula *best = select_best(pool);
        for (size_t i = 0; i < pool->count; ++i) {
            if (&pool->formulas[i] == best) {
                continue;
            }
            if (pool->formulas[i].fitness < 0.5) {
                k_memcpy(&pool->formulas[i].gene, &best->gene, sizeof(KolibriGene));
                mutate_gene(pool, &pool->formulas[i].gene);
            }
        }
    }
    evaluate_pool(pool);
}

const KolibriFormula *kf_pool_best(const KolibriFormulaPool *pool) {
    if (!pool || pool->count == 0U) {
        return NULL;
    }
    const KolibriFormula *best = &pool->formulas[0];
    for (size_t i = 1; i < pool->count; ++i) {
        if (pool->formulas[i].fitness > best->fitness) {
            best = &pool->formulas[i];
        }
    }
    return best;
}

int kf_formula_apply(const KolibriFormula *formula, int input, int *output) {
    if (!formula || !output) {
        return -1;
    }
    *output = formula_predict_multilayer(&formula->gene, input);
    return 0;
}

size_t kf_formula_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len) {
    if (!formula || !out || out_len == 0U) {
        return 0U;
    }
    size_t count = formula->gene.length;
    if (count > out_len) {
        count = out_len;
    }
    k_memcpy(out, formula->gene.digits, count);
    return count;
}

int kf_formula_describe(const KolibriFormula *formula, char *buffer, size_t buffer_len) {
    if (!formula || !buffer || buffer_len == 0U) {
        return -1;
    }
    int slope = 0;
    int bias = 0;
    if (decode_coefficients(&formula->gene, &slope, &bias) != 0) {
        return -1;
    }
    size_t layers = formula->gene.length / 8U;
    if (layers > 500U) layers = 500U;
    int written = snprintf(buffer, buffer_len,
                           "слоёв=%zu k=%d b=%d фитнес=%.6f геном=%zu",
                           layers, slope, bias, formula->fitness,
                           formula->gene.length);
    if (written < 0 || (size_t)written >= buffer_len) {
        return -1;
    }
    return 0;
}

int kf_pool_feedback(KolibriFormulaPool *pool, const KolibriGene *gene, double delta) {
    if (!pool || !gene) {
        return -1;
    }
    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->formulas[i].gene.length == gene->length &&
            k_memcmp(pool->formulas[i].gene.digits, gene->digits, gene->length) == 0) {
            pool->formulas[i].feedback += delta;
            if (pool->formulas[i].feedback > 1.0) {
                pool->formulas[i].feedback = 1.0;
            }
            if (pool->formulas[i].feedback < -1.0) {
                pool->formulas[i].feedback = -1.0;
            }
            return 0;
        }
    }
    return -1;
}

/* --- Missing API implementations for backend compatibility --- */

int kf_hash_from_text(const char *text) {
    if (!text) return 0;
    unsigned long hash = 5381;
    int c;
    while ((c = *text++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return (int)(hash & 0x7FFFFFFF); /* Ensure positive */
}

int kf_formula_lookup_answer(const KolibriFormula *formula, int query_hash, char *answer_buffer, size_t buffer_size) {
    if (!formula || !answer_buffer || buffer_size == 0) return -1;
    /* В kernel-версии пула нет ассоциаций, возвращаем ошибку */
    return -1;
}

int kf_pool_add_association(KolibriFormulaPool *pool, int input_hash, int output_hash, const char *question, const char *answer) {
    /* В kernel-версии пула нет поддержки ассоциаций */
    (void)pool;
    (void)input_hash;
    (void)output_hash;
    (void)question;
    (void)answer;
    return -1;
}

void kf_pool_free(KolibriFormulaPool *pool) {
    if (pool) {
        memset(pool, 0, sizeof(*pool));
    }
}

int kf_pool_ensure_association_capacity(KolibriFormulaPool *pool, size_t count) {
    (void)pool;
    (void)count;
    return 0; /* Stub: static array in kernel struct */
}

void kf_config_default(KolibriEvolutionConfig *config) {
    if (config) {
        memset(config, 0, sizeof(*config));
        config->mutation_rate = 0.05;
        config->crossover_rate = 0.7;
        config->elite_ratio = 0.1;
        config->tournament_size = 0.2;
        config->mutation_type = KOLIBRI_MUTATION_POINT;
        config->crossover_type = KOLIBRI_CROSSOVER_SINGLE_POINT;
        config->generations_per_tick = 1;
        config->adaptive_mutation = 0;
    }
}

int kf_pool_get_config(const KolibriFormulaPool *pool, KolibriEvolutionConfig *config) {
    if (!pool || !config) return -1;
    memcpy(config, &pool->config, sizeof(*config));
    return 0;
}

void kf_pool_reset_metrics(KolibriFormulaPool *pool) {
    if (pool) {
        memset(&pool->metrics, 0, sizeof(pool->metrics));
    }
}

int kf_pool_set_config(KolibriFormulaPool *pool, const KolibriEvolutionConfig *config) {
    if (!pool || !config) return -1;
    memcpy(&pool->config, config, sizeof(*config));
    return 0;
}

int kf_pool_get_metrics(const KolibriFormulaPool *pool, KolibriEvolutionMetrics *metrics) {
    if (!pool || !metrics) return -1;
    memcpy(metrics, &pool->metrics, sizeof(*metrics));
    return 0;
}

int kf_reactor_run(KolibriFormulaPool *pool, size_t max_generations, double target_fitness) {
    if (!pool) return -1;
    
    size_t gen = 0;
    for (; gen < max_generations; gen++) {
        kf_pool_tick(pool, 1);
        const KolibriFormula *best = kf_pool_best(pool);
        if (best && best->fitness >= target_fitness) {
            break;
        }
    }
    pool->metrics.total_generations += gen;
    return (int)gen;
}

int kf_metrics_to_digits(const KolibriEvolutionMetrics *metrics, char *buffer, size_t buffer_len) {
    if (!metrics || !buffer || buffer_len < 20) return -1;
    return snprintf(buffer, buffer_len, "%lu_%lu_%.4f_%.4f",
                    (unsigned long)metrics->total_generations,
                    (unsigned long)metrics->beneficial_mutations,
                    metrics->best_fitness,
                    metrics->avg_fitness);
}

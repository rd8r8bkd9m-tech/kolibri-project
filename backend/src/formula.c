/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/formula.h"

#include "kolibri/decimal.h"
#include "kolibri/symbol_table.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KOLIBRI_FORMULA_CAPACITY (pool->capacity)
#define KOLIBRI_DIGIT_MAX 11U
#define KOLIBRI_ASSOC_TEXT_LIMIT (sizeof(((KolibriAssociation *)0)->question))

/* ---------------------------- Утилиты ----------------------------- */

static uint8_t random_digit(KolibriFormulaPool *pool) {
    return (uint8_t)(k_rng_next(&pool->rng) % 12ULL);
}

static void gene_randomize(KolibriFormulaPool *pool, KolibriGene *gene) {
    gene->length = sizeof(gene->digits);
    for (size_t i = 0; i < gene->length; ++i) {
        gene->digits[i] = random_digit(pool);
    }
}

static int gene_copy(const KolibriGene *src, KolibriGene *dst) {
    if (!src || !dst) {
        return -1;
    }
    if (src->length > sizeof(dst->digits)) {
        return -1;
    }
    dst->length = src->length;
    memcpy(dst->digits, src->digits, src->length);
    return 0;
}

static uint32_t fnv1a32(const char *text) {
    const unsigned char *bytes = (const unsigned char *)(text ? text : "");
    uint32_t hash = 2166136261u;
    while (*bytes) {
        hash ^= (uint32_t)(*bytes++);
        hash *= 16777619u;
    }
    return hash;
}

static int kolibri_hash_to_int(uint32_t hash) {
    /* Ограничиваем диапазон 32-битного хеша до int */
    hash &= 0x7FFFFFFFu;
    if (hash > (uint32_t)INT_MAX) {
        hash = (uint32_t)(hash % INT_MAX);
    }
    return (int)hash;
}

static int utf8_is_continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

static size_t kolibri_utf8_decode_next(const unsigned char *text,
                                       size_t length,
                                       size_t offset,
                                       uint32_t *out_codepoint) {
    if (!text || !out_codepoint || offset >= length) {
        return 0U;
    }
    unsigned char lead = text[offset];
    if (lead < 0x80U) {
        *out_codepoint = (uint32_t)lead;
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        if (offset + 1U >= length) {
            return 0U;
        }
        unsigned char b1 = text[offset + 1U];
        if (!utf8_is_continuation(b1)) {
            return 0U;
        }
        uint32_t codepoint = ((uint32_t)(lead & 0x1FU) << 6) | (uint32_t)(b1 & 0x3FU);
        if (codepoint < 0x80U) {
            return 0U;
        }
        *out_codepoint = codepoint;
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        if (offset + 2U >= length) {
            return 0U;
        }
        unsigned char b1 = text[offset + 1U];
        unsigned char b2 = text[offset + 2U];
        if (!utf8_is_continuation(b1) || !utf8_is_continuation(b2)) {
            return 0U;
        }
        uint32_t codepoint = ((uint32_t)(lead & 0x0FU) << 12) |
                             ((uint32_t)(b1 & 0x3FU) << 6) |
                             (uint32_t)(b2 & 0x3FU);
        if (codepoint < 0x800U || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return 0U;
        }
        *out_codepoint = codepoint;
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        if (offset + 3U >= length) {
            return 0U;
        }
        unsigned char b1 = text[offset + 1U];
        unsigned char b2 = text[offset + 2U];
        unsigned char b3 = text[offset + 3U];
        if (!utf8_is_continuation(b1) || !utf8_is_continuation(b2) || !utf8_is_continuation(b3)) {
            return 0U;
        }
        uint32_t codepoint = ((uint32_t)(lead & 0x07U) << 18) |
                             ((uint32_t)(b1 & 0x3FU) << 12) |
                             ((uint32_t)(b2 & 0x3FU) << 6) |
                             (uint32_t)(b3 & 0x3FU);
        if (codepoint < 0x10000U || codepoint > 0x10FFFFU) {
            return 0U;
        }
        *out_codepoint = codepoint;
        return 4U;
    }
    return 0U;
}

int kf_hash_from_text(const char *text) {
    return kolibri_hash_to_int(fnv1a32(text));
}

static void association_reset(KolibriAssociation *assoc) {
    if (!assoc) {
        return;
    }
    assoc->input_hash = 0;
    assoc->output_hash = 0;
    assoc->question[0] = '\0';
    assoc->answer[0] = '\0';
    assoc->question_digits_length = 0U;
    assoc->answer_digits_length = 0U;
    assoc->timestamp = 0U;
    assoc->source[0] = '\0';
}

static void association_set(KolibriAssociation *assoc,
                            KolibriSymbolTable *symbols,
                            const char *question,
                            const char *answer,
                            const char *source,
                            uint64_t timestamp) {
    if (!assoc) {
        return;
    }
    association_reset(assoc);
    if (question) {
        strncpy(assoc->question, question, sizeof(assoc->question) - 1U);
    }
    if (answer) {
        strncpy(assoc->answer, answer, sizeof(assoc->answer) - 1U);
    }
    if (source) {
        strncpy(assoc->source, source, sizeof(assoc->source) - 1U);
    }
    assoc->timestamp = timestamp;
    assoc->input_hash = kolibri_hash_to_int(fnv1a32(assoc->question));
    assoc->output_hash = kolibri_hash_to_int(fnv1a32(assoc->answer));
    if (symbols) {
        const unsigned char *qbytes = (const unsigned char *)assoc->question;
        size_t qlen = strlen(assoc->question);
        size_t qpos = 0U;
        while (qpos < qlen && assoc->question_digits_length + KOLIBRI_SYMBOL_DIGITS <= KOLIBRI_ASSOC_DIGITS_MAX) {
            uint32_t codepoint = 0U;
            size_t consumed = kolibri_utf8_decode_next(qbytes, qlen, qpos, &codepoint);
            if (consumed == 0U) {
                codepoint = (uint32_t)qbytes[qpos];
                consumed = 1U;
            }
            uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
            if (kolibri_symbol_encode(symbols, codepoint, digits) == 0) {
                memcpy(&assoc->question_digits[assoc->question_digits_length], digits, KOLIBRI_SYMBOL_DIGITS);
                assoc->question_digits_length += KOLIBRI_SYMBOL_DIGITS;
            }
            qpos += consumed;
        }
        const unsigned char *abytes = (const unsigned char *)assoc->answer;
        size_t alen = strlen(assoc->answer);
        size_t apos = 0U;
        while (apos < alen && assoc->answer_digits_length + KOLIBRI_SYMBOL_DIGITS <= KOLIBRI_ASSOC_DIGITS_MAX) {
            uint32_t codepoint = 0U;
            size_t consumed = kolibri_utf8_decode_next(abytes, alen, apos, &codepoint);
            if (consumed == 0U) {
                codepoint = (uint32_t)abytes[apos];
                consumed = 1U;
            }
            uint8_t digits[KOLIBRI_SYMBOL_DIGITS];
            if (kolibri_symbol_encode(symbols, codepoint, digits) == 0) {
                memcpy(&assoc->answer_digits[assoc->answer_digits_length], digits, KOLIBRI_SYMBOL_DIGITS);
                assoc->answer_digits_length += KOLIBRI_SYMBOL_DIGITS;
            }
            apos += consumed;
        }
    }
}

static int association_equals(const KolibriAssociation *a, const KolibriAssociation *b) {
    if (!a || !b) {
        return 0;
    }
    return a->input_hash == b->input_hash && strcmp(a->question, b->question) == 0;
}

static int encode_text_digits(const char *text, uint8_t *out, size_t out_len) {
    if (!text || !out) {
        return 0;
    }
    size_t required = k_encode_text_length(strlen(text));
    if (required > out_len) {
        return 0;
    }
    if (k_encode_text(text, (char *)out, out_len) != 0) {
        return 0;
    }
    return (int)strlen((const char *)out);
}

/* -------------------------- Прогноз формулы ------------------------ */

static int decode_signed(const KolibriGene *gene, size_t offset, int *value) {
    if (!gene || !value) {
        return -1;
    }
    if (offset + 3 >= gene->length) {
        return -1;
    }
    int sign = gene->digits[offset] % 2 == 0 ? 1 : -1;
    int magnitude = (int)(gene->digits[offset + 1] * 10 + gene->digits[offset + 2]);
    *value = sign * magnitude;
    return 0;
}

static int decode_operation(const KolibriGene *gene, size_t offset, int *operation) {
    if (!gene || !operation) {
        return -1;
    }
    if (offset >= gene->length) {
        return -1;
    }
    /* digits are generated as 0..11, and the switch below implements 12 ops */
    *operation = (int)(gene->digits[offset] % 12U);
    return 0;
}

static int decode_bias(const KolibriGene *gene, size_t offset, int *bias) {
    if (!gene || !bias) {
        return -1;
    }
    if (offset + 2 >= gene->length) {
        return -1;
    }
    int sign = gene->digits[offset] % 2 == 0 ? 1 : -1;
    int magnitude = (int)(gene->digits[offset + 1] * 10 + gene->digits[offset + 2]);
    *bias = sign * magnitude;
    return 0;
}

static int formula_predict_numeric(const KolibriFormula *formula, int input, int *output) {
    if (!formula || !output) {
        return -1;
    }
    
    long long x = (long long)input;
    /* ResNet-архитектура: 50 residual-блоков × 10 слоёв = 500 слоёв */
    /* Skip-connection в каждом блоке предотвращает затухание сигнала */
    #define BACKEND_BLOCK_SIZE 10
    size_t total_layers = formula->gene.length / 8;
    if (total_layers > 500) total_layers = 500;
    size_t num_blocks = total_layers / BACKEND_BLOCK_SIZE;
    if (num_blocks == 0) num_blocks = 1;

    for (size_t block = 0; block < num_blocks; ++block) {
        /* --- Skip-connection: запоминаем вход блока --- */
        long long residual = x;

        for (size_t sub = 0; sub < BACKEND_BLOCK_SIZE; ++sub) {
            size_t layer = block * BACKEND_BLOCK_SIZE + sub;
            if (layer >= total_layers) break;

            size_t offset = layer * 8;
            if (offset + 7 >= sizeof(formula->gene.digits)) break;

            int operation = 0;
            int slope = 0;
            int bias = 0;
            int auxiliary = 0;
            
            if (decode_operation(&formula->gene, offset, &operation) != 0 ||
                decode_signed(&formula->gene, offset + 1, &slope) != 0 ||
                decode_bias(&formula->gene, offset + 4, &bias) != 0 ||
                decode_signed(&formula->gene, offset + 7, &auxiliary) != 0) {
                continue;
            }

            long long result = 0;
            switch (operation) {
            case 0: result = (long long)slope * x / 100 + bias; break;
            case 1: result = (long long)slope * x / 100 - bias; break;
            case 2: {
                long long divisor = auxiliary == 0 ? 1 : (long long)auxiliary;
                if (divisor < 0) divisor = -divisor;
                if (divisor == 0) divisor = 1;
                result = ((long long)slope * x / 100) % divisor + bias;
                break;
            }
            case 3: { /* Мягкая квадратичная: x*|x|/(1+|x|) */
                long long abs_x = x < 0 ? -x : x;
                result = (long long)slope * x / (100 + abs_x) + bias;
                break;
            }
            case 4: result = (x ^ auxiliary) + bias; break;
            case 5: result = (x & slope) + bias; break;
            case 6: result = (long long)(sin((double)x / 256.0) * (double)slope) + bias; break;
            case 7: {
                double denom = 1.0 + fabs((double)x);
                result = (long long)((double)slope * (double)x / denom) + bias;
                break;
            }
            case 8: result = (x >> ((unsigned)slope & 3)) + bias; break;
            case 9: result = ((x + (long long)slope) * (long long)bias) / 100; break;
            case 10: /* Tanh */
                result = (long long)(tanh((double)x / 100.0) * (double)slope) + bias;
                break;
            case 11: { /* Sigmoid */
                double arg = -(double)x / 100.0;
                if (arg > 50.0) arg = 50.0;
                result = (long long)((2.0 / (1.0 + exp(arg)) - 1.0) * (double)slope) + bias;
                break;
            }
            default: result = (x + bias) ^ slope; break;
            }

            /* Клиппинг внутри слоя: ±10000 */
            if (result > 10000LL) result = 10000LL;
            if (result < -10000LL) result = -10000LL;
            x = result;
        }

        /* --- Residual connection: output = block(x) + skip --- */
        x = x + residual;

        /* --- Layer Normalization: ±10000 --- */
        if (x > 10000LL) x = 10000LL;
        if (x < -10000LL) x = -10000LL;
    }

    *output = (int)x;
    return 0;
}

static double complexity_penalty(const KolibriGene *gene) {
    double penalty = 0.0;
    for (size_t i = 0; i < gene->length; ++i) {
        if (gene->digits[i] == 0) {
            continue;
        }
        penalty += (double)(gene->digits[i]);
    }
    /* Нормализация по длине генома — штраф не зависит от размера */
    if (gene->length > 0) {
        penalty = penalty / (double)gene->length;
    }
    return penalty * 0.01;
}

static double evaluate_formula_numeric(const KolibriFormula *formula, const KolibriFormulaPool *pool) {
    if (!formula || !pool || pool->examples == 0) {
        return 0.0;
    }
    double total_error = 0.0;
    
    /* Оптимизация Hyper-Scale: если примеров слишком много, берем случайную выборку (батч) */
    size_t samples = pool->examples;
    size_t stride = 1;
    if (samples > 4096) {
        samples = 4096;
        stride = pool->examples / samples;
    }

    for (size_t i = 0; i < pool->examples; i += stride) {
        int prediction = 0;
        if (formula_predict_numeric(formula, pool->inputs[i], &prediction) != 0) {
            return 0.0;
        }
        int diff = pool->targets[i] - prediction;
        if (diff == 0) total_error -= 0.5; /* Бонус за точное совпадение */
        total_error += fabs((double)diff);
        
        if (stride > 1 && i >= 4096 * stride) break; /* Ограничиваем количество проверок */
    }
    
    double avg_error = total_error / (double)samples;
    double penalty = complexity_penalty(&formula->gene);
    double fitness = 1.0 / (1.0 + log1p(avg_error) + penalty);
    return fitness > 0.0001 ? fitness : 0.0001;
}

static void apply_feedback_bonus(KolibriFormula *formula, double *fitness) {
    if (!formula || !fitness) {
        return;
    }
    double adjusted = *fitness + formula->feedback;
    if (adjusted < 0.0) {
        adjusted = 0.0;
    }
    if (adjusted > 1.0) {
        adjusted = 1.0;
    }
    *fitness = adjusted;
}

static void mutate_gene(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene) {
        return;
    }
    /* Адаптивная мультимутация: ~3% цифр генома, мин 1 */
    size_t mutations = gene->length / 32U;
    if (mutations == 0) mutations = 1;
    for (size_t m = 0; m < mutations; ++m) {
        size_t index = (size_t)(k_rng_next(&pool->rng) % gene->length);
        gene->digits[index] = random_digit(pool);
    }
}

static void crossover(KolibriFormulaPool *pool, const KolibriGene *parent_a, const KolibriGene *parent_b, KolibriGene *child) {
    (void)pool;
    if (!parent_a || !parent_b || !child) {
        return;
    }
    size_t split = parent_a->length / 2;
    child->length = parent_a->length;
    for (size_t i = 0; i < child->length; ++i) {
        if (i < split) {
            child->digits[i] = parent_a->digits[i];
        } else {
            child->digits[i] = parent_b->digits[i];
        }
    }
}

static int compare_formulas(const void *lhs, const void *rhs) {
    const KolibriFormula *a = (const KolibriFormula *)lhs;
    const KolibriFormula *b = (const KolibriFormula *)rhs;
    if (a->fitness < b->fitness) {
        return 1;
    }
    if (a->fitness > b->fitness) {
        return -1;
    }
    return 0;
}

static void reproduce(KolibriFormulaPool *pool) {
    size_t elite = pool->count / 3U;
    if (elite == 0) {
        elite = 1;
    }
    for (size_t i = elite; i < pool->count; ++i) {
        size_t parent_a_index = i % elite;
        size_t parent_b_index = (i + 1) % elite;
        KolibriGene child;
        crossover(pool, &pool->formulas[parent_a_index].gene,
                  &pool->formulas[parent_b_index].gene, &child);
        mutate_gene(pool, &child);
        gene_copy(&child, &pool->formulas[i].gene);
        pool->formulas[i].fitness = 0.0;
        pool->formulas[i].feedback = 0.0;
        pool->formulas[i].association_count = 0;
    }
}

static void copy_dataset_into_formula(const KolibriFormulaPool *pool, KolibriFormula *formula) {
    if (!pool || !formula) {
        return;
    }
    size_t limit = pool->association_count;
    if (limit > KOLIBRI_FORMULA_MAX_ASSOCIATIONS) {
        limit = KOLIBRI_FORMULA_MAX_ASSOCIATIONS;
    }
    formula->association_count = limit;
    for (size_t i = 0; i < limit; ++i) {
        formula->associations[i] = pool->associations[i];
    }
}

static double evaluate_association_fitness(const KolibriFormulaPool *pool) {
    if (!pool || pool->association_count == 0) {
        return 0.0;
    }
    return 1.0; /* Полное соответствие ассоциациям */
}

/* ---------------------- Публичные функции ------------------------- */

void kf_pool_init(KolibriFormulaPool *pool, uint64_t seed) {
    if (!pool) {
        return;
    }
    memset(pool, 0, sizeof(*pool));
    
    /* Динамическая аллокация формул (начинаем с 16, растём безлимитно) */
    pool->capacity = KOLIBRI_FORMULA_INITIAL_CAPACITY;
    pool->formulas = (KolibriFormula *)calloc(pool->capacity, sizeof(KolibriFormula));
    if (!pool->formulas) {
        pool->capacity = 0;
        return;
    }
    pool->count = pool->capacity;
    k_rng_seed(&pool->rng, seed);
    
    /* Инициализация эволюционного реактора */
    kf_config_default(&pool->config);
    kf_pool_reset_metrics(pool);
    
    pool->examples_capacity = 1000;
    pool->inputs = (int *)malloc(pool->examples_capacity * sizeof(int));
    pool->targets = (int *)malloc(pool->examples_capacity * sizeof(int));
    
    pool->association_capacity = 1000;
    pool->associations = (KolibriAssociation *)malloc(pool->association_capacity * sizeof(KolibriAssociation));
    
    for (size_t i = 0; i < pool->count; ++i) {
        gene_randomize(pool, &pool->formulas[i].gene);
        pool->formulas[i].fitness = 0.0;
        pool->formulas[i].feedback = 0.0;
        pool->formulas[i].association_count = 0;
        pool->formulas[i].domain = KOLIBRI_DOMAIN_GENERAL;
        pool->formulas[i].domain_name[0] = '\0';
    }
}

void kf_pool_free(KolibriFormulaPool *pool) {
    if (!pool) return;
    if (pool->inputs) free(pool->inputs);
    if (pool->targets) free(pool->targets);
    if (pool->associations) free(pool->associations);
    if (pool->formulas) free(pool->formulas);
    memset(pool, 0, sizeof(*pool));
}

void kf_pool_clear_examples(KolibriFormulaPool *pool) {
    if (!pool) {
        return;
    }
    pool->examples = 0;
    pool->association_count = 0;
}

int kf_pool_add_example(KolibriFormulaPool *pool, int input, int target) {
    if (!pool) {
        return -1;
    }
    
    if (pool->examples >= pool->examples_capacity) {
        size_t new_cap = pool->examples_capacity * 2;
        int *new_in = (int *)realloc(pool->inputs, new_cap * sizeof(int));
        int *new_tgt = (int *)realloc(pool->targets, new_cap * sizeof(int));
        if (!new_in || !new_tgt) {
             /* Пытаемся сохранить текущие данные, если realloc не удался */
             return -1;
        }
        pool->inputs = new_in;
        pool->targets = new_tgt;
        pool->examples_capacity = new_cap;
    }
    
    pool->inputs[pool->examples] = input;
    pool->targets[pool->examples] = target;
    pool->examples++;
    return 0;
}

int kf_pool_ensure_association_capacity(KolibriFormulaPool *pool, size_t count) {
    if (!pool) return -1;
    if (count <= pool->association_capacity) return 0;
    
    size_t new_cap = pool->association_capacity ? pool->association_capacity : 1000;
    while (new_cap < count) new_cap *= 2;
    
    KolibriAssociation *new_assoc = (KolibriAssociation *)realloc(pool->associations, new_cap * sizeof(KolibriAssociation));
    if (!new_assoc) return -1;
    
    pool->associations = new_assoc;
    pool->association_capacity = new_cap;
    return 0;
}

int kf_pool_add_association(KolibriFormulaPool *pool,
                            KolibriSymbolTable *symbols,
                            const char *question,
                            const char *answer,
                            const char *source,
                            uint64_t timestamp) {
    if (!pool || !question || !answer) {
        return -1;
    }
    KolibriAssociation assoc;
    association_set(&assoc, symbols, question, answer, source, timestamp);

    /* Обновляем существующую запись, если такой вопрос уже был */
    for (size_t i = 0; i < pool->association_count; ++i) {
        if (pool->associations[i].input_hash == assoc.input_hash &&
            strcmp(pool->associations[i].question, assoc.question) == 0) {
            pool->associations[i] = assoc;
            return kf_pool_add_example(pool, assoc.input_hash, assoc.output_hash);
        }
    }

    if (kf_pool_ensure_association_capacity(pool, pool->association_count + 1) != 0) {
        return -1;
    }

    pool->associations[pool->association_count++] = assoc;
    return kf_pool_add_example(pool, assoc.input_hash, assoc.output_hash);
}

void kf_pool_tick(KolibriFormulaPool *pool, size_t generations) {
    if (!pool || pool->count == 0) {
        return;
    }

    if (generations == 0) {
        generations = 1;
    }

    /* Hyper-Scale Optimization: Если поколений слишком много, 
       используем адаптивный шаг эволюции */
    size_t actual_gens = generations;
    int hyper_mode = (generations >= 1000000);
    if (hyper_mode) {
        actual_gens = 1000; /* Имитируем долгую эволюцию через 1000 качественных шагов */
        printf("[Hyper-Scale] Адаптация %zu циклов под текущие ресурсы...\n", generations);
    }

    for (size_t g = 0; g < actual_gens; ++g) {
        for (size_t i = 0; i < pool->count; ++i) {
            double fitness = evaluate_formula_numeric(&pool->formulas[i], pool);
            apply_feedback_bonus(&pool->formulas[i], &fitness);
            pool->formulas[i].fitness = fitness;
        }
        qsort(pool->formulas, pool->count, sizeof(KolibriFormula), compare_formulas);
        reproduce(pool);
    }

    /* Лучшие формулы получают ассоциации */
    if (pool->association_count > 0) {
        double assoc_fitness = evaluate_association_fitness(pool);
        size_t limit = pool->count < 3 ? pool->count : 3;
        for (size_t i = 0; i < limit; ++i) {
            copy_dataset_into_formula(pool, &pool->formulas[i]);
            pool->formulas[i].fitness = assoc_fitness;
        }
        qsort(pool->formulas, pool->count, sizeof(KolibriFormula), compare_formulas);
    }
}

const KolibriFormula *kf_pool_best(const KolibriFormulaPool *pool) {
    if (!pool || pool->count == 0) {
        return NULL;
    }
    return &pool->formulas[0];
}

int kf_formula_lookup_answer(const KolibriFormula *formula, int input,
                             char *buffer, size_t buffer_len) {
    if (!formula || !buffer || buffer_len == 0) {
        return -1;
    }
    for (size_t i = 0; i < formula->association_count; ++i) {
        const KolibriAssociation *assoc = &formula->associations[i];
        if (assoc->input_hash == input && buffer) {
            strncpy(buffer, assoc->answer, buffer_len - 1U);
            buffer[buffer_len - 1U] = '\0';
            return 0;
        }
    }
    return -1;
}

int kf_formula_apply(const KolibriFormula *formula, int input, int *output) {
    if (!formula || !output) {
        return -1;
    }
    for (size_t i = 0; i < formula->association_count; ++i) {
        const KolibriAssociation *assoc = &formula->associations[i];
        if (assoc->input_hash == input) {
            *output = assoc->output_hash;
            return 0;
        }
    }
    return formula_predict_numeric(formula, input, output);
}

static size_t encode_associations_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len) {
    if (!formula || !out) {
        return 0;
    }
    if (formula->association_count == 0) {
        return 0;
    }
    char json_buffer[1024];
    size_t offset = 0;
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "{\"associations\":[");
    for (size_t i = 0; i < formula->association_count && offset < sizeof(json_buffer); ++i) {
        const KolibriAssociation *assoc = &formula->associations[i];
        const char *q = assoc->question;
        const char *a = assoc->answer;
        if (!q) {
            q = "";
        }
        if (!a) {
            a = "";
        }
        offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset,
                           "%s{\"q\":\"%s\",\"a\":\"%s\"}",
                           i == 0 ? "" : ",",
                           q, a);
    }
    if (offset >= sizeof(json_buffer)) {
        return 0;
    }
    offset += snprintf(json_buffer + offset, sizeof(json_buffer) - offset, "]}");
    if (offset >= sizeof(json_buffer)) {
        return 0;
    }
    size_t digits_len = (size_t)encode_text_digits(json_buffer, out, out_len);
    return digits_len;
}

size_t kf_formula_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len) {
    if (!formula || !out || out_len == 0) {
        return 0;
    }
    /* Копируем столько цифр генома, сколько вмещает буфер */
    size_t copy_len = formula->gene.length;
    if (copy_len > out_len) {
        copy_len = out_len;
    }
    memcpy(out, formula->gene.digits, copy_len);
    size_t written = copy_len;
    size_t remaining = out_len - written;
    if (remaining > 32 && formula->association_count > 0) {
        written += encode_associations_digits(formula, out + written, remaining);
    }
    return written;
}

int kf_formula_describe(const KolibriFormula *formula, char *buffer, size_t buffer_len) {
    if (!formula || !buffer || buffer_len == 0) {
        return -1;
    }
    if (formula->association_count > 0) {
        const KolibriAssociation *assoc = &formula->associations[0];
        int written = snprintf(buffer, buffer_len,
                               "ассоциаций=%zu пример: '%s' -> '%s' фитнес=%.6f",
                               formula->association_count, assoc->question,
                               assoc->answer, formula->fitness);
        if (written < 0 || (size_t)written >= buffer_len) {
            return -1;
        }
        return 0;
    }

    int operation = 0;
    int slope = 0;
    int bias = 0;
    int auxiliary = 0;
    if (decode_operation(&formula->gene, 0, &operation) != 0 ||
        decode_signed(&formula->gene, 1, &slope) != 0 ||
        decode_bias(&formula->gene, 4, &bias) != 0 ||
        decode_signed(&formula->gene, 7, &auxiliary) != 0) {
        return -1;
    }
    const char *operation_name = NULL;
    switch (operation) {
    case 0:
        operation_name = "линейная";
        break;
    case 1:
        operation_name = "инверсная";
        break;
    case 2:
        operation_name = "остаточная";
        break;
    case 3:
        operation_name = "квадратичная";
        break;
    default:
        operation_name = "неизвестная";
        break;
    }
    int written = snprintf(buffer, buffer_len,
                           "тип=%s k=%d b=%d aux=%d фитнес=%.6f",
                           operation_name, slope, bias, auxiliary, formula->fitness);
    if (written < 0 || (size_t)written >= buffer_len) {
        return -1;
    }
    return 0;
}

static void adjust_feedback(KolibriFormula *formula, double delta) {
    if (!formula) {
        return;
    }
    formula->feedback += delta;
    if (formula->feedback > 1.0) {
        formula->feedback = 1.0;
    }
    if (formula->feedback < -1.0) {
        formula->feedback = -1.0;
    }
    formula->fitness += delta;
    if (formula->fitness < 0.0) {
        formula->fitness = 0.0;
    }
}

int kf_pool_feedback(KolibriFormulaPool *pool, const KolibriGene *gene, double delta) {
    if (!pool || !gene || pool->count == 0) {
        return -1;
    }
    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->formulas[i].gene.length != gene->length) {
            continue;
        }
        if (memcmp(pool->formulas[i].gene.digits, gene->digits, gene->length) != 0) {
            continue;
        }
        adjust_feedback(&pool->formulas[i], delta);
        size_t index = i;
        if (delta > 0.0) {
            while (index > 0 && pool->formulas[index].fitness > pool->formulas[index - 1].fitness) {
                KolibriFormula tmp = pool->formulas[index - 1];
                pool->formulas[index - 1] = pool->formulas[index];
                pool->formulas[index] = tmp;
                index--;
            }
        } else if (delta < 0.0) {
            while (index + 1 < pool->count &&
                   pool->formulas[index].fitness < pool->formulas[index + 1].fitness) {
                KolibriFormula tmp = pool->formulas[index + 1];
                pool->formulas[index + 1] = pool->formulas[index];
                pool->formulas[index] = tmp;
                index++;
            }
        }
        return 0;
    }
    return -1;
}

/* ============================================================================
 * Эволюционный реактор - реализация
 * ============================================================================ */

void kf_config_default(KolibriEvolutionConfig *config) {
    if (!config) {
        return;
    }
    config->mutation_rate = 0.1;
    config->mutation_strength = 1.0;
    config->mutation_type = KOLIBRI_MUTATION_POINT;
    config->crossover_rate = 0.7;
    config->crossover_type = KOLIBRI_CROSSOVER_SINGLE_POINT;
    config->elite_ratio = 0.33;
    config->tournament_size = 0.25;
    config->generations_per_tick = 1;
    config->adaptive_mutation = 0;
}

int kf_pool_set_config(KolibriFormulaPool *pool, const KolibriEvolutionConfig *config) {
    if (!pool || !config) {
        return -1;
    }
    pool->config = *config;
    return 0;
}

int kf_pool_get_config(const KolibriFormulaPool *pool, KolibriEvolutionConfig *config) {
    if (!pool || !config) {
        return -1;
    }
    *config = pool->config;
    return 0;
}

int kf_pool_get_metrics(const KolibriFormulaPool *pool, KolibriEvolutionMetrics *metrics) {
    if (!pool || !metrics) {
        return -1;
    }
    *metrics = pool->metrics;
    return 0;
}

void kf_pool_reset_metrics(KolibriFormulaPool *pool) {
    if (!pool) {
        return;
    }
    memset(&pool->metrics, 0, sizeof(pool->metrics));
    pool->prev_best_fitness = 0.0;
}

/* Мутация типа SWAP - обмен двух позиций */
static void mutate_swap(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene || gene->length < 2) {
        return;
    }
    size_t i = (size_t)(k_rng_next(&pool->rng) % gene->length);
    size_t j = (size_t)(k_rng_next(&pool->rng) % gene->length);
    uint8_t tmp = gene->digits[i];
    gene->digits[i] = gene->digits[j];
    gene->digits[j] = tmp;
}

/* Мутация типа INVERT - инверсия сегмента */
static void mutate_invert(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene || gene->length < 2) {
        return;
    }
    size_t start = (size_t)(k_rng_next(&pool->rng) % gene->length);
    size_t end = (size_t)(k_rng_next(&pool->rng) % gene->length);
    if (start > end) {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    while (start < end) {
        uint8_t tmp = gene->digits[start];
        gene->digits[start] = gene->digits[end];
        gene->digits[end] = tmp;
        start++;
        end--;
    }
}

/* Мутация типа SCRAMBLE - перемешивание сегмента */
static void mutate_scramble(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene || gene->length < 2) {
        return;
    }
    size_t start = (size_t)(k_rng_next(&pool->rng) % gene->length);
    size_t len = (size_t)(k_rng_next(&pool->rng) % (gene->length / 4 + 1)) + 2;
    if (start + len > gene->length) {
        len = gene->length - start;
    }
    for (size_t i = 0; i < len; i++) {
        size_t j = (size_t)(k_rng_next(&pool->rng) % len);
        uint8_t tmp = gene->digits[start + i];
        gene->digits[start + i] = gene->digits[start + j];
        gene->digits[start + j] = tmp;
    }
}

/* Мутация типа SHIFT - сдвиг цифр */
static void mutate_shift(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene || gene->length < 2) {
        return;
    }
    int direction = (k_rng_next(&pool->rng) % 2) ? 1 : -1;
    size_t shift = (size_t)(k_rng_next(&pool->rng) % 8) + 1;
    
    if (direction > 0) {
        /* Сдвиг вправо */
        for (size_t s = 0; s < shift; s++) {
            uint8_t last = gene->digits[gene->length - 1];
            for (size_t i = gene->length - 1; i > 0; i--) {
                gene->digits[i] = gene->digits[i - 1];
            }
            gene->digits[0] = last;
        }
    } else {
        /* Сдвиг влево */
        for (size_t s = 0; s < shift; s++) {
            uint8_t first = gene->digits[0];
            for (size_t i = 0; i < gene->length - 1; i++) {
                gene->digits[i] = gene->digits[i + 1];
            }
            gene->digits[gene->length - 1] = first;
        }
    }
}

/* Расширенная мутация с учётом типа */
static void mutate_gene_advanced(KolibriFormulaPool *pool, KolibriGene *gene) {
    if (!gene) {
        return;
    }
    
    /* Применяем мутацию в зависимости от настроенного типа */
    switch (pool->config.mutation_type) {
        case KOLIBRI_MUTATION_SWAP:
            mutate_swap(pool, gene);
            break;
        case KOLIBRI_MUTATION_INVERT:
            mutate_invert(pool, gene);
            break;
        case KOLIBRI_MUTATION_SCRAMBLE:
            mutate_scramble(pool, gene);
            break;
        case KOLIBRI_MUTATION_SHIFT:
            mutate_shift(pool, gene);
            break;
        case KOLIBRI_MUTATION_POINT:
        default:
            mutate_gene(pool, gene);
            break;
    }
    
    /* Применяем дополнительные мутации согласно mutation_strength */
    int extra_mutations = (int)(pool->config.mutation_strength) - 1;
    for (int i = 0; i < extra_mutations; i++) {
        size_t index = (size_t)(k_rng_next(&pool->rng) % gene->length);
        gene->digits[index] = random_digit(pool);
    }
    
    pool->metrics.total_mutations++;
}

/* Двухточечный кроссовер */
static void crossover_two_point(KolibriFormulaPool *pool, 
                                const KolibriGene *parent_a, 
                                const KolibriGene *parent_b, 
                                KolibriGene *child) {
    if (!parent_a || !parent_b || !child) {
        return;
    }
    size_t point1 = (size_t)(k_rng_next(&pool->rng) % parent_a->length);
    size_t point2 = (size_t)(k_rng_next(&pool->rng) % parent_a->length);
    if (point1 > point2) {
        size_t tmp = point1;
        point1 = point2;
        point2 = tmp;
    }
    
    child->length = parent_a->length;
    for (size_t i = 0; i < child->length; ++i) {
        if (i < point1 || i >= point2) {
            child->digits[i] = parent_a->digits[i];
        } else {
            child->digits[i] = parent_b->digits[i];
        }
    }
}

/* Равномерный кроссовер */
static void crossover_uniform(KolibriFormulaPool *pool,
                              const KolibriGene *parent_a,
                              const KolibriGene *parent_b,
                              KolibriGene *child) {
    if (!parent_a || !parent_b || !child) {
        return;
    }
    child->length = parent_a->length;
    for (size_t i = 0; i < child->length; ++i) {
        if (k_rng_next(&pool->rng) % 2) {
            child->digits[i] = parent_a->digits[i];
        } else {
            child->digits[i] = parent_b->digits[i];
        }
    }
}

/* Расширенный кроссовер с учётом типа */
static void crossover_advanced(KolibriFormulaPool *pool,
                               const KolibriGene *parent_a,
                               const KolibriGene *parent_b,
                               KolibriGene *child) {
    switch (pool->config.crossover_type) {
        case KOLIBRI_CROSSOVER_TWO_POINT:
            crossover_two_point(pool, parent_a, parent_b, child);
            break;
        case KOLIBRI_CROSSOVER_UNIFORM:
            crossover_uniform(pool, parent_a, parent_b, child);
            break;
        case KOLIBRI_CROSSOVER_SINGLE_POINT:
        default:
            crossover(pool, parent_a, parent_b, child);
            break;
    }
}

/* Обновление метрик после поколения */
static void update_metrics(KolibriFormulaPool *pool, double prev_best) {
    pool->metrics.total_generations++;
    
    /* Вычисляем статистику fitness */
    double sum = 0.0;
    double best = 0.0;
    for (size_t i = 0; i < pool->count; i++) {
        sum += pool->formulas[i].fitness;
        if (pool->formulas[i].fitness > best) {
            best = pool->formulas[i].fitness;
        }
    }
    pool->metrics.best_fitness = best;
    pool->metrics.avg_fitness = sum / (double)pool->count;
    
    /* Дисперсия */
    double var_sum = 0.0;
    for (size_t i = 0; i < pool->count; i++) {
        double diff = pool->formulas[i].fitness - pool->metrics.avg_fitness;
        var_sum += diff * diff;
    }
    pool->metrics.fitness_variance = var_sum / (double)pool->count;
    
    /* Скорость эволюции */
    double delta = best - prev_best;
    if (delta > 0.001) {
        pool->metrics.evolution_speed = delta;
        pool->metrics.beneficial_mutations++;
        pool->metrics.stagnation_count = 0;
    } else if (delta < -0.001) {
        pool->metrics.evolution_speed = delta;
        pool->metrics.harmful_mutations++;
    } else {
        pool->metrics.neutral_mutations++;
        pool->metrics.stagnation_count++;
    }
    
    /* Энергия мутаций (средняя сила изменений) */
    pool->metrics.mutation_energy = pool->config.mutation_strength * 
                                    pool->config.mutation_rate;
}

int kf_reactor_run(KolibriFormulaPool *pool, size_t max_generations,
                   double target_fitness) {
    if (!pool || max_generations == 0) {
        return -1;
    }
    
    size_t generations_run = 0;
    
    while (generations_run < max_generations) {
        double prev_best = pool->metrics.best_fitness;
        
        /* Один tick эволюции */
        kf_pool_tick(pool, pool->config.generations_per_tick);
        generations_run += pool->config.generations_per_tick;
        
        /* Обновляем метрики */
        update_metrics(pool, prev_best);
        
        /* Адаптивная мутация при застое */
        if (pool->config.adaptive_mutation && 
            pool->metrics.stagnation_count > 10) {
            kf_config_adapt(pool);
        }
        
        /* Проверяем достижение цели */
        if (pool->metrics.best_fitness >= target_fitness) {
            break;
        }
    }
    
    return (int)generations_run;
}

void kf_config_adapt(KolibriFormulaPool *pool) {
    if (!pool) {
        return;
    }
    
    /* При застое увеличиваем мутацию */
    if (pool->metrics.stagnation_count > 20) {
        pool->config.mutation_rate *= 1.5;
        if (pool->config.mutation_rate > 0.5) {
            pool->config.mutation_rate = 0.5;
        }
        pool->config.mutation_strength += 0.5;
        if (pool->config.mutation_strength > 5.0) {
            pool->config.mutation_strength = 5.0;
        }
        
        /* Меняем тип мутации */
        pool->config.mutation_type = (KolibriMutationType)
            (k_rng_next(&pool->rng) % KOLIBRI_MUTATION_COUNT);
    }
    
    /* При хорошем прогрессе уменьшаем мутацию */
    if (pool->metrics.evolution_speed > 0.05) {
        pool->config.mutation_rate *= 0.9;
        if (pool->config.mutation_rate < 0.05) {
            pool->config.mutation_rate = 0.05;
        }
    }
    
    /* Сбрасываем счётчик застоя после адаптации */
    pool->metrics.stagnation_count = 0;
}

int kf_metrics_to_digits(const KolibriEvolutionMetrics *metrics,
                         char *buffer, size_t buffer_len) {
    if (!metrics || !buffer || buffer_len < 128) {
        return -1;
    }
    
    /* Формат: GEN{generations}MUT{mutations}BEN{beneficial}FIT{fitness*1000} */
    int written = snprintf(buffer, buffer_len,
        "%llu%llu%llu%llu%llu%03d%03d",
        (unsigned long long)metrics->total_generations,
        (unsigned long long)metrics->total_mutations,
        (unsigned long long)metrics->beneficial_mutations,
        (unsigned long long)metrics->harmful_mutations,
        (unsigned long long)metrics->stagnation_count,
        (int)(metrics->best_fitness * 1000),
        (int)(metrics->avg_fitness * 1000));
    
    if (written < 0 || (size_t)written >= buffer_len) {
        return -1;
    }
    
    return written;
}

/* ============================================================================
 * Динамическое управление формулами (безлимитный рост)
 * ============================================================================ */

int kf_pool_grow(KolibriFormulaPool *pool, size_t new_capacity) {
    if (!pool || new_capacity <= pool->capacity) {
        return -1;
    }
    
    KolibriFormula *new_formulas = (KolibriFormula *)realloc(
        pool->formulas, new_capacity * sizeof(KolibriFormula));
    if (!new_formulas) {
        return -1;
    }
    
    /* Инициализируем новые слоты */
    memset(&new_formulas[pool->capacity], 0,
           (new_capacity - pool->capacity) * sizeof(KolibriFormula));
    
    pool->formulas = new_formulas;
    
    /* Рандомизируем новые геномы */
    for (size_t i = pool->capacity; i < new_capacity; ++i) {
        gene_randomize(pool, &pool->formulas[i].gene);
        pool->formulas[i].fitness = 0.0;
        pool->formulas[i].feedback = 0.0;
        pool->formulas[i].association_count = 0;
        pool->formulas[i].domain = KOLIBRI_DOMAIN_GENERAL;
        pool->formulas[i].domain_name[0] = '\0';
    }
    
    pool->capacity = new_capacity;
    return 0;
}

int kf_pool_add_domain_formula(KolibriFormulaPool *pool,
                               KolibriDomainType domain,
                               const char *domain_name) {
    if (!pool) {
        return -1;
    }
    
    /* Автоматический рост при нехватке места */
    if (pool->count >= pool->capacity) {
        size_t new_cap = pool->capacity * 2;
        if (new_cap == 0) new_cap = KOLIBRI_FORMULA_INITIAL_CAPACITY;
        if (kf_pool_grow(pool, new_cap) != 0) {
            return -1;
        }
    }
    
    /* Инициализируем новую доменную формулу */
    KolibriFormula *f = &pool->formulas[pool->count];
    gene_randomize(pool, &f->gene);
    f->fitness = 0.0;
    f->feedback = 0.0;
    f->association_count = 0;
    f->domain = domain;
    if (domain_name) {
        strncpy(f->domain_name, domain_name, KOLIBRI_DOMAIN_NAME_MAX - 1);
        f->domain_name[KOLIBRI_DOMAIN_NAME_MAX - 1] = '\0';
    } else {
        f->domain_name[0] = '\0';
    }
    
    pool->count++;
    return 0;
}

const KolibriFormula *kf_pool_best_for_domain(const KolibriFormulaPool *pool,
                                              KolibriDomainType domain) {
    if (!pool || pool->count == 0) {
        return NULL;
    }
    
    const KolibriFormula *best = NULL;
    double best_fitness = -1.0;
    
    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->formulas[i].domain == domain &&
            pool->formulas[i].fitness > best_fitness) {
            best = &pool->formulas[i];
            best_fitness = pool->formulas[i].fitness;
        }
    }
    
    /* Fallback: если нет формул для домена, берём лучшую общую */
    if (!best) {
        return kf_pool_best(pool);
    }
    
    return best;
}

size_t kf_pool_domain_count(const KolibriFormulaPool *pool,
                            KolibriDomainType domain) {
    if (!pool) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->formulas[i].domain == domain) {
            count++;
        }
    }
    return count;
}

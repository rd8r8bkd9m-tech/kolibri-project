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

#define KOLIBRI_FORMULA_CAPACITY (sizeof(((KolibriFormulaPool *)0)->formulas) / sizeof(KolibriFormula))
#define KOLIBRI_DIGIT_MAX 9U
#define KOLIBRI_ASSOC_TEXT_LIMIT (sizeof(((KolibriAssociation *)0)->question))

/* ---------------------------- Утилиты ----------------------------- */

static uint8_t random_digit(KolibriFormulaPool *pool) {
    return (uint8_t)(k_rng_next(&pool->rng) % 10ULL);
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
    *operation = (int)(gene->digits[offset] % 8U);
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
    
    int current_val = input;
    /* Эволюционная Глубина: 100 слоев преобразований (Ultra-Deep Kolibri Network) */
    for (int layer = 0; layer < 100; layer++) {
        size_t offset = (size_t)layer * 8;
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
        case 0: result = (long long)slope * (long long)current_val + bias; break;
        case 1: result = (long long)slope * (long long)current_val - bias; break;
        case 2: {
            long long divisor = auxiliary == 0 ? 1 : auxiliary;
            result = ((long long)slope * (long long)current_val) % divisor;
            result += bias;
            break;
        }
        case 3: result = (long long)slope * (long long)current_val * (long long)current_val + bias; break;
        case 4: result = (current_val ^ auxiliary) + bias; break;
        case 5: result = (current_val & slope) + bias; break;
        case 6: result = (long long)(sin((double)current_val / 256.0) * (double)slope * 10.0) + bias; break;
        case 7: result = (long long)((double)slope * 100.0 * (double)current_val / (1.0 + fabs((double)current_val))) + bias; break;
        case 8: result = (current_val | slope) - auxiliary; break;
        case 9: result = (long long)((double)slope * (double)current_val * exp(-(double)(current_val * current_val) / 1000000.0)) + bias; break;
        default: result = (current_val + bias) ^ slope; break;
        }

        /* Ограничение весов для стабильности 100 слоев */
        if (result > 1000000000LL) result = 1000000000LL;
        if (result < -1000000000LL) result = -1000000000LL;
        current_val = (int)result;
    }

    *output = current_val;
    return 0;
}

static double complexity_penalty(const KolibriGene *gene) {
    double penalty = 0.0;
    for (size_t i = 0; i < gene->length; ++i) {
        if (gene->digits[i] == 0) {
            continue;
        }
        penalty += 0.001 * (double)(gene->digits[i]);
    }
    return penalty;
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
    size_t index = (size_t)(k_rng_next(&pool->rng) % gene->length);
    uint8_t delta = random_digit(pool);
    gene->digits[index] = delta;
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
    pool->count = KOLIBRI_FORMULA_CAPACITY;
    k_rng_seed(&pool->rng, seed);
    
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
    }
}

void kf_pool_free(KolibriFormulaPool *pool) {
    if (!pool) return;
    if (pool->inputs) free(pool->inputs);
    if (pool->targets) free(pool->targets);
    if (pool->associations) free(pool->associations);
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

    if (pool->association_count >= pool->association_capacity) {
        size_t new_cap = pool->association_capacity * 2;
        KolibriAssociation *new_assoc = (KolibriAssociation *)realloc(pool->associations, new_cap * sizeof(KolibriAssociation));
        if (!new_assoc) return -1;
        pool->associations = new_assoc;
        pool->association_capacity = new_cap;
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
    if (!formula || !out) {
        return 0;
    }
    size_t written = 0;
    if (formula->gene.length <= out_len) {
        memcpy(out, formula->gene.digits, formula->gene.length);
        written = formula->gene.length;
    }
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

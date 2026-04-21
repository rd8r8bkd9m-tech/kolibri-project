/*
 * formula_logic.c
 *
 * MF-слой Kolibri: цифровые гены и мета-формулы создают логические формулы.
 */

#include "kolibri/formula_logic.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static char *dup_text(const char *src) {
    if (!src) src = "";
    size_t len = strlen(src) + 1U;
    char *out = (char*)malloc(len);
    if (out) memcpy(out, src, len);
    return out;
}

static int evaluate_simple_formula(const char *formula, int *result) {
    if (!formula || !result) return -1;
    int a = 0;
    int b = 0;
    char op = '\0';
    if (sscanf(formula, "%d%c%d", &a, &op, &b) == 3) {
        switch (op) {
            case '*': *result = a * b; return 0;
            case '+': *result = a + b; return 0;
            case '-': *result = a - b; return 0;
            case '/': *result = b != 0 ? a / b : 0; return 0;
            case '%': *result = b != 0 ? a % b : 0; return 0;
            default: break;
        }
    }
    if (sscanf(formula, "%d", result) == 1) return 0;
    *result = 0;
    return -1;
}

static int mf_decode_param(const uint8_t *digits, size_t offset, size_t length) {
    if (!digits || length < 2U) return 0;
    int sign = (digits[offset] % 2U == 0U) ? 1 : -1;
    int val = 0;
    for (size_t i = 1; i < length; i++) {
        val = val * 10 + (int)digits[offset + i];
    }
    return sign * val;
}

static MetaFormula *meta_clone(const MetaFormula *src) {
    if (!src) return NULL;
    MetaFormula *dst = (MetaFormula*)malloc(sizeof(MetaFormula));
    if (!dst) return NULL;
    memcpy(dst, src, sizeof(MetaFormula));
    if (src->operation == META_GENERATE_CONSTANT && src->params.generate_constant.value) {
        dst->params.generate_constant.value = dup_text(src->params.generate_constant.value);
        if (!dst->params.generate_constant.value) {
            free(dst);
            return NULL;
        }
    }
    return dst;
}

static LogicExpression *find_logic_by_id(LogicalMemory *mem, const char *id) {
    if (!mem || !id) return NULL;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (strcmp(mem->cells[i].id, id) == 0 || strcmp(mem->cells[i].hash, id) == 0) {
            return mem->cells[i].logic;
        }
    }
    return NULL;
}

static LogicExpression *clone_logic(const LogicExpression *src) {
    if (!src) return NULL;
    LogicExpression *dst = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!dst) return NULL;

    memcpy(dst, src, sizeof(LogicExpression));
    memset(&dst->data, 0, sizeof(dst->data));

    switch (src->type) {
        case LOGIC_CONSTANT:
            dst->data.constant.length = src->data.constant.length;
            dst->data.constant.value = dup_text(src->data.constant.value);
            if (!dst->data.constant.value) {
                free(dst);
                return NULL;
            }
            break;
        case LOGIC_REPEAT:
            dst->data.repeat.count = src->data.repeat.count;
            dst->data.repeat.pattern = clone_logic(src->data.repeat.pattern);
            if (!dst->data.repeat.pattern) {
                free(dst);
                return NULL;
            }
            break;
        case LOGIC_SEQUENCE:
            dst->data.sequence = src->data.sequence;
            break;
        case LOGIC_COMPOSITION:
            dst->data.composition.count = src->data.composition.count;
            for (size_t i = 0; i < src->data.composition.count && i < 8U; i++) {
                dst->data.composition.expressions[i] =
                    clone_logic(src->data.composition.expressions[i]);
                if (!dst->data.composition.expressions[i]) {
                    lm_destroy_logic(dst);
                    return NULL;
                }
            }
            break;
        case LOGIC_RELATION:
            copy_text(dst->data.relation.relation_type,
                sizeof(dst->data.relation.relation_type),
                src->data.relation.relation_type);
            dst->data.relation.left = clone_logic(src->data.relation.left);
            dst->data.relation.right = clone_logic(src->data.relation.right);
            if (!dst->data.relation.left || !dst->data.relation.right) {
                lm_destroy_logic(dst);
                return NULL;
            }
            break;
        case LOGIC_TRANSFORM:
            dst->data.transform.input = clone_logic(src->data.transform.input);
            dst->data.transform.transform_fn = src->data.transform.transform_fn;
            if (!dst->data.transform.input) {
                lm_destroy_logic(dst);
                return NULL;
            }
            break;
        case LOGIC_CONDITIONAL:
            dst->data.conditional.condition = clone_logic(src->data.conditional.condition);
            dst->data.conditional.then_expr = clone_logic(src->data.conditional.then_expr);
            dst->data.conditional.else_expr = clone_logic(src->data.conditional.else_expr);
            if (!dst->data.conditional.condition || !dst->data.conditional.then_expr) {
                lm_destroy_logic(dst);
                return NULL;
            }
            break;
        case LOGIC_VARIABLE:
            dst->data.variable = src->data.variable;
            dst->data.variable.binding = NULL;
            break;
        case LOGIC_DIGIT_STREAM:
            dst->data.stream.length = src->data.stream.length;
            if (src->data.stream.length > 0) {
                dst->data.stream.digits = (uint8_t*)malloc(src->data.stream.length);
                if (!dst->data.stream.digits) {
                    free(dst);
                    return NULL;
                }
                memcpy(dst->data.stream.digits, src->data.stream.digits, src->data.stream.length);
            }
            break;
        case LOGIC_L5_SUPER:
            dst->data.l5_super = src->data.l5_super;
            break;
        case LOGIC_NOP:
        default:
            break;
    }
    return dst;
}

MetaFormulaStore *mf_create_store(void) {
    return (MetaFormulaStore*)calloc(1, sizeof(MetaFormulaStore));
}

void mf_destroy_store(MetaFormulaStore *store) {
    if (!store) return;
    for (size_t i = 0; i < store->cache_count; i++) {
        lm_destroy_logic(store->generated_cache[i]);
    }
    for (size_t i = 0; i < store->count; i++) {
        if (store->formulas[i].operation == META_GENERATE_CONSTANT) {
            free(store->formulas[i].params.generate_constant.value);
        }
    }
    free(store);
}

void mf_destroy_meta_formula(MetaFormula *meta) {
    if (!meta) return;
    if (meta->operation == META_GENERATE_CONSTANT) {
        free(meta->params.generate_constant.value);
    }
    free(meta);
}

MetaFormula *mf_create_meta_formula(void) {
    return (MetaFormula*)calloc(1, sizeof(MetaFormula));
}

MetaFormula *mf_create_constant_generator(const char *value) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_GENERATE_CONSTANT;
    meta->params.generate_constant.value = dup_text(value);
    if (!meta->params.generate_constant.value) {
        free(meta);
        return NULL;
    }
    meta->complexity_score = 0.1;
    meta->output_size_estimate = value ? strlen(value) : 0U;
    return meta;
}

MetaFormula *mf_create_repeat_generator(const char *pattern_formula, const char *count_formula) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_GENERATE_REPEAT;
    copy_text(meta->params.gen_repeat.pattern_formula,
        sizeof(meta->params.gen_repeat.pattern_formula),
        pattern_formula);
    copy_text(meta->params.gen_repeat.count_formula,
        sizeof(meta->params.gen_repeat.count_formula),
        count_formula);
    meta->complexity_score = 1.0;
    meta->output_size_estimate = 100;
    return meta;
}

MetaFormula *mf_create_sequence_generator(
    const char *start_formula,
    const char *step_formula,
    const char *count_formula
) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_GENERATE_SEQUENCE;
    copy_text(meta->params.gen_sequence.start_formula,
        sizeof(meta->params.gen_sequence.start_formula),
        start_formula);
    copy_text(meta->params.gen_sequence.step_formula,
        sizeof(meta->params.gen_sequence.step_formula),
        step_formula);
    copy_text(meta->params.gen_sequence.count_formula,
        sizeof(meta->params.gen_sequence.count_formula),
        count_formula);
    meta->complexity_score = 1.5;
    meta->output_size_estimate = 150;
    return meta;
}

MetaFormula *mf_create_transformer(const char *input_logic_id, const char *transform_rule) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_TRANSFORM_LOGIC;
    copy_text(meta->params.transform.input_logic_id,
        sizeof(meta->params.transform.input_logic_id),
        input_logic_id);
    copy_text(meta->params.transform.transform_rule,
        sizeof(meta->params.transform.transform_rule),
        transform_rule);
    meta->complexity_score = 2.0;
    meta->output_size_estimate = 200;
    return meta;
}

MetaFormula *mf_create_relation_deriver(
    const char *left_id,
    const char *right_id,
    const char *inference_rule
) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_DERIVE_RELATION;
    copy_text(meta->params.derive.left_logic_id,
        sizeof(meta->params.derive.left_logic_id),
        left_id);
    copy_text(meta->params.derive.right_logic_id,
        sizeof(meta->params.derive.right_logic_id),
        right_id);
    copy_text(meta->params.derive.inference_rule,
        sizeof(meta->params.derive.inference_rule),
        inference_rule);
    meta->complexity_score = 3.0;
    meta->output_size_estimate = 120;
    return meta;
}

MetaFormula *mf_create_pattern_evolver(
    const char *source_pattern_id,
    double mutation_rate,
    int generations
) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_EVOLVE_PATTERN;
    copy_text(meta->params.evolve.source_pattern_id,
        sizeof(meta->params.evolve.source_pattern_id),
        source_pattern_id);
    meta->params.evolve.mutation_rate = mutation_rate;
    meta->params.evolve.generations = generations;
    meta->complexity_score = 2.5 + mutation_rate * (double)generations;
    meta->output_size_estimate = 256;
    return meta;
}

MetaFormula *mf_create_logic_compressor(
    const char *target_logic_id,
    const char *compression_strategy
) {
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;
    meta->operation = META_COMPRESS_LOGIC;
    copy_text(meta->params.compress.target_logic_id,
        sizeof(meta->params.compress.target_logic_id),
        target_logic_id);
    copy_text(meta->params.compress.compression_strategy,
        sizeof(meta->params.compress.compression_strategy),
        compression_strategy);
    meta->complexity_score = 4.0;
    meta->output_size_estimate = 64;
    return meta;
}

MetaFormula *mf_from_gene(const uint8_t digits[32]) {
    if (!digits) return NULL;
    MetaFormula *meta = mf_create_meta_formula();
    if (!meta) return NULL;

    uint8_t op_code = (uint8_t)((digits[0] * 10U + digits[1]) % 8U);
    meta->operation = (MetaOperation)op_code;

    switch (meta->operation) {
        case META_GENERATE_CONSTANT:
            meta->params.generate_constant.value = (char*)malloc(32U);
            if (!meta->params.generate_constant.value) {
                free(meta);
                return NULL;
            }
            snprintf(meta->params.generate_constant.value, 32U,
                "%u%u%u%u%u%u%u%u",
                digits[2], digits[3], digits[4], digits[5],
                digits[6], digits[7], digits[8], digits[9]);
            break;
        case META_GENERATE_REPEAT:
            snprintf(meta->params.gen_repeat.pattern_formula,
                sizeof(meta->params.gen_repeat.pattern_formula),
                "%u%u%u%u%u",
                digits[2], digits[3], digits[4], digits[5], digits[6]);
            snprintf(meta->params.gen_repeat.count_formula,
                sizeof(meta->params.gen_repeat.count_formula),
                "%d", abs(mf_decode_param(digits, 7, 3)));
            break;
        case META_GENERATE_SEQUENCE:
            snprintf(meta->params.gen_sequence.start_formula,
                sizeof(meta->params.gen_sequence.start_formula),
                "%d", mf_decode_param(digits, 2, 4));
            snprintf(meta->params.gen_sequence.step_formula,
                sizeof(meta->params.gen_sequence.step_formula),
                "%d", mf_decode_param(digits, 6, 3));
            snprintf(meta->params.gen_sequence.count_formula,
                sizeof(meta->params.gen_sequence.count_formula),
                "%d", abs(mf_decode_param(digits, 9, 3)));
            break;
        default:
            break;
    }

    meta->generation = 1;
    meta->complexity_score = meta->complexity_score > 0.0
        ? meta->complexity_score
        : (double)digits[31] / 10.0;
    meta->output_size_estimate = 64;
    return meta;
}

static LogicExpression *apply_transform(LogicExpression *src, const char *rule) {
    if (!src || !rule) return NULL;

    if (strcmp(rule, "double_count") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            t->data.repeat.count *= 2U;
            t->materialized_size *= 2U;
            t->complexity += 0.5;
        }
        return t;
    }

    if (strcmp(rule, "half_count") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *t = clone_logic(src);
        if (t && t->data.repeat.count > 1U) {
            t->data.repeat.count /= 2U;
            t->materialized_size /= 2U;
        }
        return t;
    }

    if (strcmp(rule, "reverse_sequence") == 0 && src->type == LOGIC_SEQUENCE) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            int last = t->data.sequence.start +
                t->data.sequence.step * ((int)t->data.sequence.count - 1);
            t->data.sequence.start = last;
            t->data.sequence.step = -t->data.sequence.step;
        }
        return t;
    }

    if (strcmp(rule, "scale_sequence") == 0 && src->type == LOGIC_SEQUENCE) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            t->data.sequence.start *= 2;
            t->data.sequence.step *= 2;
        }
        return t;
    }

    if (strcmp(rule, "compose_repeats") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *a = clone_logic(src);
        LogicExpression *b = clone_logic(src);
        if (a && b) return lm_logic_compose(a, b);
        lm_destroy_logic(a);
        lm_destroy_logic(b);
        return NULL;
    }

    return clone_logic(src);
}

static LogicExpression *evolve_pattern(
    LogicExpression *source,
    double mutation_rate,
    int generations
) {
    LogicExpression *current = clone_logic(source);
    if (!current) return NULL;

    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)source);
    for (int gen = 0; gen < generations; gen++) {
        double r = (double)(rand_r(&seed) % 1000) / 1000.0;
        if (r >= mutation_rate) continue;

        if (current->type == LOGIC_REPEAT) {
            size_t delta = current->data.repeat.count / 5U;
            if (delta < 1U) delta = 1U;
            if (rand_r(&seed) % 2 == 0) {
                current->data.repeat.count += delta;
            } else if (current->data.repeat.count > delta) {
                current->data.repeat.count -= delta;
            }
            current->materialized_size = current->data.repeat.pattern
                ? current->data.repeat.pattern->materialized_size * current->data.repeat.count
                : 0U;
        } else if (current->type == LOGIC_SEQUENCE) {
            current->data.sequence.step += (rand_r(&seed) % 3) - 1;
        } else if (current->type == LOGIC_CONSTANT && current->data.constant.length > 0) {
            size_t pos = (size_t)rand_r(&seed) % current->data.constant.length;
            current->data.constant.value[pos] = (char)('A' + (rand_r(&seed) % 26));
        }
        current->complexity += 0.1;
    }
    return current;
}

static LogicExpression *compress_logic_expr(LogicExpression *source, const char *strategy) {
    if (!source || !strategy) return NULL;

    if (strcmp(strategy, "merge_repeats") == 0 && source->type == LOGIC_COMPOSITION) {
        const char *first_pat = NULL;
        size_t total_count = 0;
        int all_same = 1;

        for (size_t i = 0; i < source->data.composition.count && i < 8U; i++) {
            LogicExpression *sub = source->data.composition.expressions[i];
            if (!sub || sub->type != LOGIC_REPEAT ||
                !sub->data.repeat.pattern ||
                sub->data.repeat.pattern->type != LOGIC_CONSTANT) {
                all_same = 0;
                break;
            }
            const char *pat = sub->data.repeat.pattern->data.constant.value;
            if (!first_pat) {
                first_pat = pat;
            } else if (strcmp(first_pat, pat) != 0) {
                all_same = 0;
                break;
            }
            total_count += sub->data.repeat.count;
        }

        if (all_same && first_pat) {
            LogicExpression *compressed = lm_logic_repeat(first_pat, total_count);
            if (compressed) compressed->complexity = source->complexity * 0.3;
            return compressed;
        }
    }

    if (strcmp(strategy, "fold_constants") == 0 && source->type == LOGIC_SEQUENCE) {
        int total = 0;
        int val = source->data.sequence.start;
        for (size_t i = 0; i < source->data.sequence.count; i++) {
            total += val;
            val += source->data.sequence.step;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", total);
        LogicExpression *folded = lm_logic_constant(buf);
        if (folded) folded->complexity = 0.05;
        return folded;
    }

    if (strcmp(strategy, "simplify") == 0) {
        LogicExpression *copy = clone_logic(source);
        return copy ? lm_optimize_logic(copy) : NULL;
    }

    return clone_logic(source);
}

static void cache_result(MetaFormulaStore *store, LogicExpression *result) {
    if (!store || !result || store->cache_count >= 256U) return;
    store->generated_cache[store->cache_count] = result;
    snprintf(store->cache_ids[store->cache_count],
        sizeof(store->cache_ids[store->cache_count]),
        "meta_gen_%zu",
        store->cache_count);
    store->cache_count++;
}

LogicExpression *mf_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *target_memory
) {
    if (!store || !meta || !target_memory) return NULL;
    LogicExpression *result = NULL;

    switch (meta->operation) {
        case META_GENERATE_CONSTANT:
            result = lm_logic_constant(meta->params.generate_constant.value
                ? meta->params.generate_constant.value
                : "");
            break;

        case META_GENERATE_REPEAT: {
            int count = 0;
            if (evaluate_simple_formula(meta->params.gen_repeat.count_formula, &count) != 0) {
                return NULL;
            }
            if (count <= 0) count = 1;
            result = lm_logic_repeat(meta->params.gen_repeat.pattern_formula, (size_t)count);
            break;
        }

        case META_GENERATE_SEQUENCE: {
            int start = 0;
            int step = 0;
            int count = 0;
            evaluate_simple_formula(meta->params.gen_sequence.start_formula, &start);
            evaluate_simple_formula(meta->params.gen_sequence.step_formula, &step);
            evaluate_simple_formula(meta->params.gen_sequence.count_formula, &count);
            if (count <= 0) count = 1;
            result = lm_logic_sequence(start, step, (size_t)count);
            break;
        }

        case META_GENERATE_COMPOSE:
            if (store->cache_count >= 2U) {
                LogicExpression *a = clone_logic(store->generated_cache[store->cache_count - 2U]);
                LogicExpression *b = clone_logic(store->generated_cache[store->cache_count - 1U]);
                if (a && b) {
                    result = lm_logic_compose(a, b);
                } else {
                    lm_destroy_logic(a);
                    lm_destroy_logic(b);
                }
            }
            break;

        case META_TRANSFORM_LOGIC: {
            LogicExpression *src = find_logic_by_id(
                target_memory,
                meta->params.transform.input_logic_id);
            result = apply_transform(src, meta->params.transform.transform_rule);
            break;
        }

        case META_DERIVE_RELATION: {
            LogicExpression *left = find_logic_by_id(
                target_memory,
                meta->params.derive.left_logic_id);
            LogicExpression *right = find_logic_by_id(
                target_memory,
                meta->params.derive.right_logic_id);
            left = left ? clone_logic(left) : lm_logic_constant(meta->params.derive.left_logic_id);
            right = right ? clone_logic(right) : lm_logic_constant(meta->params.derive.right_logic_id);
            if (!left || !right) {
                lm_destroy_logic(left);
                lm_destroy_logic(right);
                return NULL;
            }

            const char *rule = meta->params.derive.inference_rule;
            const char *relation = rule;
            if (strcmp(rule, "transitive") == 0) relation = "derives_from";
            else if (strcmp(rule, "equivalence") == 0) relation = "equivalent";
            result = lm_logic_relation(left, right, relation);
            break;
        }

        case META_EVOLVE_PATTERN: {
            LogicExpression *src = find_logic_by_id(
                target_memory,
                meta->params.evolve.source_pattern_id);
            if (!src) {
                src = lm_logic_repeat(meta->params.evolve.source_pattern_id, 10U);
                result = evolve_pattern(
                    src,
                    meta->params.evolve.mutation_rate,
                    meta->params.evolve.generations);
                lm_destroy_logic(src);
            } else {
                result = evolve_pattern(
                    src,
                    meta->params.evolve.mutation_rate,
                    meta->params.evolve.generations);
            }
            break;
        }

        case META_COMPRESS_LOGIC: {
            LogicExpression *src = find_logic_by_id(
                target_memory,
                meta->params.compress.target_logic_id);
            result = compress_logic_expr(src, meta->params.compress.compression_strategy);
            break;
        }
    }

    cache_result(store, result);
    return result;
}

int mf_store_meta(MetaFormulaStore *store, const MetaFormula *meta, const char *id) {
    if (!store || !meta || !id || store->count >= 256U) return -1;
    MetaFormula *slot = &store->formulas[store->count];
    memcpy(slot, meta, sizeof(MetaFormula));
    if (meta->operation == META_GENERATE_CONSTANT && meta->params.generate_constant.value) {
        slot->params.generate_constant.value = dup_text(meta->params.generate_constant.value);
        if (!slot->params.generate_constant.value) return -1;
    }
    copy_text(store->cache_ids[store->count], sizeof(store->cache_ids[store->count]), id);
    store->count++;
    return 0;
}

MetaFormula *mf_load_meta(MetaFormulaStore *store, const char *id) {
    if (!store || !id) return NULL;
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->cache_ids[i], id) == 0) return &store->formulas[i];
    }
    return NULL;
}

MetaFormula *mf_optimize_meta(const MetaFormula *meta) {
    MetaFormula *optimized = meta_clone(meta);
    if (!optimized) return NULL;

    if (meta->operation == META_GENERATE_REPEAT) {
        int count = 0;
        if (evaluate_simple_formula(meta->params.gen_repeat.count_formula, &count) == 0 && count > 0) {
            snprintf(optimized->params.gen_repeat.count_formula,
                sizeof(optimized->params.gen_repeat.count_formula),
                "%d", count);
            optimized->complexity_score = meta->complexity_score * 0.8;
        }
    } else if (meta->operation == META_GENERATE_SEQUENCE) {
        int v = 0;
        if (evaluate_simple_formula(meta->params.gen_sequence.start_formula, &v) == 0) {
            snprintf(optimized->params.gen_sequence.start_formula,
                sizeof(optimized->params.gen_sequence.start_formula), "%d", v);
        }
        if (evaluate_simple_formula(meta->params.gen_sequence.step_formula, &v) == 0) {
            snprintf(optimized->params.gen_sequence.step_formula,
                sizeof(optimized->params.gen_sequence.step_formula), "%d", v);
        }
        if (evaluate_simple_formula(meta->params.gen_sequence.count_formula, &v) == 0) {
            snprintf(optimized->params.gen_sequence.count_formula,
                sizeof(optimized->params.gen_sequence.count_formula), "%d", v);
        }
        optimized->complexity_score = meta->complexity_score * 0.7;
    } else {
        optimized->complexity_score *= 0.9;
    }
    return optimized;
}

MetaFormula *mf_evolve_meta(const MetaFormula *meta, double mutation_rate) {
    MetaFormula *evolved = meta_clone(meta);
    if (!evolved) return NULL;
    evolved->generation = meta->generation + 1U;

    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)meta);
    double r = (double)(rand_r(&seed) % 1000) / 1000.0;
    if (r < mutation_rate) {
        if (meta->operation == META_GENERATE_REPEAT) {
            int count = 0;
            evaluate_simple_formula(meta->params.gen_repeat.count_formula, &count);
            count += (rand_r(&seed) % 10) - 4;
            if (count < 1) count = 1;
            snprintf(evolved->params.gen_repeat.count_formula,
                sizeof(evolved->params.gen_repeat.count_formula), "%d", count);
        } else if (meta->operation == META_GENERATE_SEQUENCE) {
            int step = 0;
            evaluate_simple_formula(meta->params.gen_sequence.step_formula, &step);
            step += (rand_r(&seed) % 5) - 2;
            snprintf(evolved->params.gen_sequence.step_formula,
                sizeof(evolved->params.gen_sequence.step_formula), "%d", step);
        } else if (meta->operation == META_EVOLVE_PATTERN) {
            evolved->params.evolve.mutation_rate *= 0.95;
            evolved->params.evolve.generations++;
        }
    }
    return evolved;
}

MetaFormula *mf_compose_meta(const MetaFormula *meta1, const MetaFormula *meta2) {
    if (!meta1 || !meta2) return NULL;
    MetaFormula *composed = mf_create_meta_formula();
    if (!composed) return NULL;

    if (meta1->operation == META_GENERATE_REPEAT &&
        meta2->operation == META_GENERATE_REPEAT &&
        strcmp(meta1->params.gen_repeat.pattern_formula,
               meta2->params.gen_repeat.pattern_formula) == 0) {
        int c1 = 0;
        int c2 = 0;
        evaluate_simple_formula(meta1->params.gen_repeat.count_formula, &c1);
        evaluate_simple_formula(meta2->params.gen_repeat.count_formula, &c2);
        composed->operation = META_GENERATE_REPEAT;
        copy_text(composed->params.gen_repeat.pattern_formula,
            sizeof(composed->params.gen_repeat.pattern_formula),
            meta1->params.gen_repeat.pattern_formula);
        snprintf(composed->params.gen_repeat.count_formula,
            sizeof(composed->params.gen_repeat.count_formula),
            "%d", c1 + c2);
    } else if (meta1->operation == META_GENERATE_SEQUENCE &&
               meta2->operation == META_GENERATE_SEQUENCE) {
        int c1 = 0;
        int c2 = 0;
        evaluate_simple_formula(meta1->params.gen_sequence.count_formula, &c1);
        evaluate_simple_formula(meta2->params.gen_sequence.count_formula, &c2);
        composed->operation = META_GENERATE_SEQUENCE;
        copy_text(composed->params.gen_sequence.start_formula,
            sizeof(composed->params.gen_sequence.start_formula),
            meta1->params.gen_sequence.start_formula);
        copy_text(composed->params.gen_sequence.step_formula,
            sizeof(composed->params.gen_sequence.step_formula),
            meta1->params.gen_sequence.step_formula);
        snprintf(composed->params.gen_sequence.count_formula,
            sizeof(composed->params.gen_sequence.count_formula),
            "%d", c1 + c2);
    } else {
        composed->operation = META_TRANSFORM_LOGIC;
        copy_text(composed->params.transform.transform_rule,
            sizeof(composed->params.transform.transform_rule),
            "compose_mixed");
    }

    composed->generation = (meta1->generation + meta2->generation) / 2U + 1U;
    composed->complexity_score = meta1->complexity_score + meta2->complexity_score;
    composed->output_size_estimate = meta1->output_size_estimate + meta2->output_size_estimate;
    return composed;
}

int mf_get_stats(MetaFormulaStore *store, MetaFormulaStats *stats) {
    if (!store || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    stats->total_meta_formulas = store->count;
    stats->generated_logic_count = store->cache_count;
    stats->meta_size_bytes = store->count * sizeof(MetaFormula);
    for (size_t i = 0; i < store->cache_count; i++) {
        if (!store->generated_cache[i]) continue;
        stats->logic_size_bytes += sizeof(LogicExpression);
        if (store->generated_cache[i]->type == LOGIC_REPEAT) {
            stats->logic_size_bytes += sizeof(LogicExpression);
        }
    }
    stats->meta_to_logic_ratio = stats->logic_size_bytes > 0
        ? (double)stats->meta_size_bytes / (double)stats->logic_size_bytes
        : 0.0;
    return 0;
}

int mf_to_string(const MetaFormula *meta, char *output, size_t output_size) {
    if (!meta || !output || output_size == 0) return -1;
    switch (meta->operation) {
        case META_GENERATE_CONSTANT:
            return snprintf(output, output_size, "meta_constant(value='%s')",
                meta->params.generate_constant.value ? meta->params.generate_constant.value : "");
        case META_GENERATE_REPEAT:
            return snprintf(output, output_size, "meta_repeat(pattern='%s', count='%s')",
                meta->params.gen_repeat.pattern_formula,
                meta->params.gen_repeat.count_formula);
        case META_GENERATE_SEQUENCE:
            return snprintf(output, output_size, "meta_sequence(start='%s', step='%s', count='%s')",
                meta->params.gen_sequence.start_formula,
                meta->params.gen_sequence.step_formula,
                meta->params.gen_sequence.count_formula);
        case META_GENERATE_COMPOSE:
            return snprintf(output, output_size, "meta_compose()");
        case META_TRANSFORM_LOGIC:
            return snprintf(output, output_size, "meta_transform(input='%s', rule='%s')",
                meta->params.transform.input_logic_id,
                meta->params.transform.transform_rule);
        case META_DERIVE_RELATION:
            return snprintf(output, output_size, "meta_derive(%s -> %s, rule='%s')",
                meta->params.derive.left_logic_id,
                meta->params.derive.right_logic_id,
                meta->params.derive.inference_rule);
        case META_EVOLVE_PATTERN:
            return snprintf(output, output_size, "meta_evolve(source='%s', rate=%.2f, gens=%d)",
                meta->params.evolve.source_pattern_id,
                meta->params.evolve.mutation_rate,
                meta->params.evolve.generations);
        case META_COMPRESS_LOGIC:
            return snprintf(output, output_size, "meta_compress(target='%s', strategy='%s')",
                meta->params.compress.target_logic_id,
                meta->params.compress.compression_strategy);
        default:
            return snprintf(output, output_size, "meta_unknown()");
    }
}

int mf_auto_discover_patterns(LogicalMemory *memory, MetaFormulaStore *store) {
    if (!memory || !store) return -1;
    int discovered = 0;

    for (size_t i = 0; i < memory->cell_count; i++) {
        if (memory->cells[i].logic) {
            memory->cells[i].logic = lm_optimize_logic(memory->cells[i].logic);
        }
    }

    for (size_t i = 0; i < memory->cell_count; i++) {
        LogicCell *cell = &memory->cells[i];
        if (!cell->logic) continue;

        if (cell->logic->type == LOGIC_REPEAT &&
            cell->logic->data.repeat.pattern &&
            cell->logic->data.repeat.pattern->type == LOGIC_CONSTANT) {
            char count[32];
            snprintf(count, sizeof(count), "%zu", cell->logic->data.repeat.count);
            MetaFormula *mf = mf_create_repeat_generator(
                cell->logic->data.repeat.pattern->data.constant.value,
                count);
            if (mf) {
                char id[64];
                snprintf(id, sizeof(id), "auto_repeat_%zu", i);
                if (mf_store_meta(store, mf, id) == 0) discovered++;
                free(mf);
            }
        } else if (cell->logic->type == LOGIC_SEQUENCE) {
            char start[32];
            char step[32];
            char count[32];
            snprintf(start, sizeof(start), "%d", cell->logic->data.sequence.start);
            snprintf(step, sizeof(step), "%d", cell->logic->data.sequence.step);
            snprintf(count, sizeof(count), "%zu", cell->logic->data.sequence.count);
            MetaFormula *mf = mf_create_sequence_generator(start, step, count);
            if (mf) {
                char id[64];
                snprintf(id, sizeof(id), "auto_seq_%zu", i);
                if (mf_store_meta(store, mf, id) == 0) discovered++;
                free(mf);
            }
        } else if (cell->logic->type == LOGIC_COMPOSITION) {
            MetaFormula *mf = mf_create_logic_compressor(cell->id, "merge_repeats");
            if (mf) {
                char id[64];
                snprintf(id, sizeof(id), "auto_compress_%zu", i);
                if (mf_store_meta(store, mf, id) == 0) discovered++;
                free(mf);
            }
        }
    }

    return discovered;
}

int mf_batch_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *memory,
    const char **cell_ids,
    size_t cell_count
) {
    if (!store || !meta || !memory || !cell_ids) return -1;
    int ok = 0;
    for (size_t i = 0; i < cell_count; i++) {
        LogicExpression *result = mf_execute(store, meta, memory);
        LogicExpression *copy = clone_logic(result);
        if (!copy) continue;
        if (lm_store_logic(memory, cell_ids[i], copy) == 0) {
            ok++;
        } else {
            lm_destroy_logic(copy);
        }
    }
    return ok;
}

MetaFormula *mf_infer_meta(
    MetaFormulaStore *store,
    const char *rule,
    const MetaFormula **input_metas,
    size_t input_count
) {
    if (!store || !rule || !input_metas || input_count == 0) return NULL;

    if (strcmp(rule, "combine") == 0 && input_count >= 2U) {
        return mf_compose_meta(input_metas[0], input_metas[1]);
    }

    if (strcmp(rule, "generalize") == 0) {
        MetaOperation op = input_metas[0]->operation;
        int all_same = 1;
        for (size_t i = 1; i < input_count; i++) {
            if (input_metas[i]->operation != op) {
                all_same = 0;
                break;
            }
        }

        if (all_same && op == META_GENERATE_REPEAT) {
            int total = 0;
            for (size_t i = 0; i < input_count; i++) {
                int c = 0;
                evaluate_simple_formula(input_metas[i]->params.gen_repeat.count_formula, &c);
                total += c;
            }
            char avg[32];
            snprintf(avg, sizeof(avg), "%d", total / (int)input_count);
            MetaFormula *gen = mf_create_repeat_generator(
                input_metas[0]->params.gen_repeat.pattern_formula,
                avg);
            if (gen) gen->complexity_score = 0.5;
            return gen;
        }

        if (all_same && op == META_GENERATE_SEQUENCE) {
            int total_start = 0;
            int total_step = 0;
            int total_count = 0;
            for (size_t i = 0; i < input_count; i++) {
                int v = 0;
                evaluate_simple_formula(input_metas[i]->params.gen_sequence.start_formula, &v);
                total_start += v;
                evaluate_simple_formula(input_metas[i]->params.gen_sequence.step_formula, &v);
                total_step += v;
                evaluate_simple_formula(input_metas[i]->params.gen_sequence.count_formula, &v);
                total_count += v;
            }
            char start[32];
            char step[32];
            char count[32];
            int n = (int)input_count;
            snprintf(start, sizeof(start), "%d", total_start / n);
            snprintf(step, sizeof(step), "%d", total_step / n);
            snprintf(count, sizeof(count), "%d", total_count / n);
            return mf_create_sequence_generator(start, step, count);
        }

        return meta_clone(input_metas[0]);
    }

    if (strcmp(rule, "specialize") == 0) {
        MetaFormula *spec = meta_clone(input_metas[0]);
        if (spec) {
            spec->generation++;
            spec->complexity_score *= 2.0;
        }
        return spec;
    }

    return NULL;
}

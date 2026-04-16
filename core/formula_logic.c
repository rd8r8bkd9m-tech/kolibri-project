/*
 * formula_logic.c
 *
 * Реализация мета-формул: формулы которые генерируют логику
 *
 * Мета-формулы — это «ДНК логики»: компактные описания,
 * из которых материализуются произвольно сложные структуры.
 * Поддерживаются: генерация, трансформация, вывод отношений,
 * эволюция паттернов и сжатие логических выражений.
 */

#include "kolibri/logical_memory.h"
#include "kolibri/formula_logic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

MetaFormulaStore* mf_create_store(void) {
    MetaFormulaStore *store = calloc(1, sizeof(MetaFormulaStore));
    if (!store) return NULL;

    store->count = 0;
    store->cache_count = 0;

    return store;
}

void mf_destroy_store(MetaFormulaStore *store) {
    if (!store) return;

    /* Очистить кэш сгенерированной логики */
    for (size_t i = 0; i < store->cache_count; i++) {
        if (store->generated_cache[i]) {
            lm_destroy_logic(store->generated_cache[i]);
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

/* ========== СОЗДАНИЕ МЕТА-ФОРМУЛ ========== */

MetaFormula* mf_create_meta_formula(void) {
    return calloc(1, sizeof(MetaFormula));
}

/* --- Конструктор: генератор констант --- */
MetaFormula* mf_create_constant_generator(const char *value) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;

    meta->operation = META_GENERATE_CONSTANT;
    meta->params.generate_constant.value = value ? strdup(value) : NULL;
    meta->generation = 0;
    meta->complexity_score = 0.1;
    meta->output_size_estimate = value ? strlen(value) : 0;

    return meta;
}

/* --- Конструктор: эволюция паттерна --- */
MetaFormula* mf_create_pattern_evolver(
    const char *source_pattern_id,
    double mutation_rate,
    int generations
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;

    meta->operation = META_EVOLVE_PATTERN;
    strncpy(meta->params.evolve.source_pattern_id, source_pattern_id, 63);
    meta->params.evolve.mutation_rate = mutation_rate;
    meta->params.evolve.generations = generations;

    meta->generation = 0;
    meta->complexity_score = 2.5 + mutation_rate * generations;
    meta->output_size_estimate = 256;

    return meta;
}

/* --- Конструктор: сжатие логики --- */
MetaFormula* mf_create_logic_compressor(
    const char *target_logic_id,
    const char *compression_strategy
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;

    meta->operation = META_COMPRESS_LOGIC;
    strncpy(meta->params.compress.target_logic_id, target_logic_id, 63);
    strncpy(meta->params.compress.compression_strategy, compression_strategy, 63);

    meta->generation = 0;
    meta->complexity_score = 4.0;
    meta->output_size_estimate = 64;

    return meta;
}

MetaFormula* mf_create_repeat_generator(
    const char *pattern_formula, 
    const char *count_formula
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;
    
    meta->operation = META_GENERATE_REPEAT;
    strncpy(meta->params.gen_repeat.pattern_formula, pattern_formula, 63);
    strncpy(meta->params.gen_repeat.count_formula, count_formula, 63);
    
    meta->generation = 0;
    meta->complexity_score = 1.0;
    meta->output_size_estimate = 100;
    
    return meta;
}

MetaFormula* mf_create_sequence_generator(
    const char *start_formula,
    const char *step_formula,
    const char *count_formula
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;
    
    meta->operation = META_GENERATE_SEQUENCE;
    strncpy(meta->params.gen_sequence.start_formula, start_formula, 63);
    strncpy(meta->params.gen_sequence.step_formula, step_formula, 63);
    strncpy(meta->params.gen_sequence.count_formula, count_formula, 63);
    
    meta->generation = 0;
    meta->complexity_score = 1.5;
    meta->output_size_estimate = 150;
    
    return meta;
}

MetaFormula* mf_create_transformer(
    const char *input_logic_id,
    const char *transform_rule
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;
    
    meta->operation = META_TRANSFORM_LOGIC;
    strncpy(meta->params.transform.input_logic_id, input_logic_id, 63);
    strncpy(meta->params.transform.transform_rule, transform_rule, 127);
    
    meta->generation = 0;
    meta->complexity_score = 2.0;
    meta->output_size_estimate = 200;
    
    return meta;
}

MetaFormula* mf_create_relation_deriver(
    const char *left_id,
    const char *right_id,
    const char *inference_rule
) {
    MetaFormula *meta = calloc(1, sizeof(MetaFormula));
    if (!meta) return NULL;
    
    meta->operation = META_DERIVE_RELATION;
    strncpy(meta->params.derive.left_logic_id, left_id, 63);
    strncpy(meta->params.derive.right_logic_id, right_id, 63);
    strncpy(meta->params.derive.inference_rule, inference_rule, 127);
    
    meta->generation = 0;
    meta->complexity_score = 3.0;
    meta->output_size_estimate = 120;
    
    return meta;
}

/* ========== ВЫПОЛНЕНИЕ МЕТА-ФОРМУЛ ========== */

/* Вычислить простую формулу (константа или выражение) */
static int evaluate_simple_formula(const char *formula, int *result) {
    /* Простейший парсер: поддержка констант и простых операций */

    /* Сначала пробуем выражение: "10*2", "5+3" */
    int a, b;
    char op;
    if (sscanf(formula, "%d%c%d", &a, &op, &b) == 3) {
        switch (op) {
            case '*': *result = a * b; return 0;
            case '+': *result = a + b; return 0;
            case '-': *result = a - b; return 0;
            case '/': *result = (b != 0) ? a / b : 0; return 0;
            case '%': *result = (b != 0) ? a % b : 0; return 0;
            default: break;
        }
    }

    /* Затем пробуем простую константу */
    if (sscanf(formula, "%d", result) == 1) {
        return 0;
    }

    *result = 0;
    return -1;
}

/* --- Вспомогательная: найти ячейку в логической памяти по ID --- */
static LogicExpression* find_logic_by_id(LogicalMemory *mem, const char *id) {
    if (!mem || !id) return NULL;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (strcmp(mem->cells[i].id, id) == 0) {
            return mem->cells[i].logic;
        }
    }
    return NULL;
}

/* --- Глубокое копирование логического выражения --- */
static LogicExpression* clone_logic(const LogicExpression *src) {
    if (!src) return NULL;

    LogicExpression *dst = calloc(1, sizeof(LogicExpression));
    if (!dst) return NULL;

    memcpy(dst, src, sizeof(LogicExpression));

    /* Рекурсивно копируем вложенные указатели */
    switch (src->type) {
        case LOGIC_REPEAT:
            dst->data.repeat.pattern = clone_logic(src->data.repeat.pattern);
            break;
        case LOGIC_COMPOSITION:
            for (size_t i = 0; i < src->data.composition.count && i < 8; i++) {
                dst->data.composition.expressions[i] =
                    clone_logic(src->data.composition.expressions[i]);
            }
            break;
        case LOGIC_RELATION:
            dst->data.relation.left  = clone_logic(src->data.relation.left);
            dst->data.relation.right = clone_logic(src->data.relation.right);
            break;
        case LOGIC_CONDITIONAL:
            dst->data.conditional.condition  = clone_logic(src->data.conditional.condition);
            dst->data.conditional.then_expr  = clone_logic(src->data.conditional.then_expr);
            dst->data.conditional.else_expr  = clone_logic(src->data.conditional.else_expr);
            break;
        default:
            break;
    }
    return dst;
}

/* --- Трансформация: применить правило к логическому выражению --- */
static LogicExpression* apply_transform(LogicExpression *src, const char *rule) {
    if (!src || !rule) return NULL;

    /* double_count — удваиваем count для repeat */
    if (strcmp(rule, "double_count") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            t->data.repeat.count *= 2;
            t->materialized_size *= 2;
            t->complexity += 0.5;
        }
        return t;
    }

    /* half_count — уполовиниваем count */
    if (strcmp(rule, "half_count") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *t = clone_logic(src);
        if (t && t->data.repeat.count > 1) {
            t->data.repeat.count /= 2;
            t->materialized_size /= 2;
        }
        return t;
    }

    /* reverse_sequence — инвертируем шаг последовательности */
    if (strcmp(rule, "reverse_sequence") == 0 && src->type == LOGIC_SEQUENCE) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            int last = t->data.sequence.start +
                       t->data.sequence.step * ((int)t->data.sequence.count - 1);
            t->data.sequence.start = last;
            t->data.sequence.step  = -t->data.sequence.step;
        }
        return t;
    }

    /* scale_sequence — умножаем все значения на 2 */
    if (strcmp(rule, "scale_sequence") == 0 && src->type == LOGIC_SEQUENCE) {
        LogicExpression *t = clone_logic(src);
        if (t) {
            t->data.sequence.start *= 2;
            t->data.sequence.step  *= 2;
        }
        return t;
    }

    /* compose_repeats — объединяем repeat в композицию */
    if (strcmp(rule, "compose_repeats") == 0 && src->type == LOGIC_REPEAT) {
        LogicExpression *part1 = clone_logic(src);
        LogicExpression *part2 = clone_logic(src);
        if (part1 && part2) {
            return lm_logic_compose(part1, part2);
        }
        lm_destroy_logic(part1);
        lm_destroy_logic(part2);
        return NULL;
    }

    /* Общий fallback — просто копируем */
    return clone_logic(src);
}

/* --- Эволюция паттерна (простой генетический алгоритм) --- */
static LogicExpression* evolve_pattern(
    LogicExpression *source,
    double mutation_rate,
    int generations
) {
    if (!source) return NULL;

    LogicExpression *current = clone_logic(source);
    if (!current) return NULL;

    unsigned int seed = (unsigned int)time(NULL);

    for (int gen = 0; gen < generations && current; gen++) {
        double r = (double)(rand_r(&seed) % 1000) / 1000.0;
        if (r >= mutation_rate) continue;

        switch (current->type) {
            case LOGIC_REPEAT: {
                /* Мутация: случайное изменение count ±20% */
                int delta = (int)(current->data.repeat.count * 0.2);
                if (delta < 1) delta = 1;
                current->data.repeat.count += (rand_r(&seed) % 2 == 0) ? delta : -delta;
                if (current->data.repeat.count == 0) current->data.repeat.count = 1;
                /* Обновляем метаданные */
                if (current->data.repeat.pattern &&
                    current->data.repeat.pattern->type == LOGIC_CONSTANT) {
                    current->materialized_size =
                        current->data.repeat.pattern->data.constant.length *
                        current->data.repeat.count;
                }
                break;
            }
            case LOGIC_SEQUENCE: {
                /* Мутация: сдвигаем start или step */
                if (rand_r(&seed) % 2 == 0) {
                    current->data.sequence.start += (rand_r(&seed) % 5) - 2;
                } else {
                    current->data.sequence.step += (rand_r(&seed) % 3) - 1;
                }
                break;
            }
            case LOGIC_CONSTANT: {
                /* Мутация: меняем один символ */
                size_t len = current->data.constant.length;
                if (len > 0) {
                    size_t pos = rand_r(&seed) % len;
                    current->data.constant.value[pos] =
                        'A' + (char)(rand_r(&seed) % 26);
                }
                break;
            }
            default:
                break;
        }
        current->complexity += 0.1;
    }

    return current;
}

/* --- Сжатие логики: упрощение выражения --- */
static LogicExpression* compress_logic_expr(
    LogicExpression *source,
    const char *strategy
) {
    if (!source) return NULL;

    /* Стратегия "merge_repeats": repeat(X,3) + repeat(X,5) → repeat(X,8) */
    if (strcmp(strategy, "merge_repeats") == 0 &&
        source->type == LOGIC_COMPOSITION) {
        /* Проверяем, все ли подвыражения — одинаковые repeat */
        int all_same_repeat = 1;
        const char *first_pat = NULL;
        size_t total_count = 0;

        for (size_t i = 0; i < source->data.composition.count; i++) {
            LogicExpression *sub = source->data.composition.expressions[i];
            if (!sub || sub->type != LOGIC_REPEAT ||
                !sub->data.repeat.pattern ||
                sub->data.repeat.pattern->type != LOGIC_CONSTANT) {
                all_same_repeat = 0;
                break;
            }
            const char *pat = sub->data.repeat.pattern->data.constant.value;
            if (!first_pat) {
                first_pat = pat;
            } else if (strcmp(first_pat, pat) != 0) {
                all_same_repeat = 0;
                break;
            }
            total_count += sub->data.repeat.count;
        }

        if (all_same_repeat && first_pat && total_count > 0) {
            LogicExpression *compressed = lm_logic_repeat(first_pat, total_count);
            if (compressed) {
                compressed->complexity = source->complexity * 0.3;
                printf("[META] Compressed %zu repeats into single repeat(\"%s\", %zu)\n",
                       source->data.composition.count, first_pat, total_count);
                return compressed;
            }
        }
    }

    /* Стратегия "fold_constants": сворачиваем последовательность в константу */
    if (strcmp(strategy, "fold_constants") == 0 &&
        source->type == LOGIC_SEQUENCE) {
        /* Материализуем последовательность и сохраняем как константу */
        char buf[32];
        int total = 0;
        int val = source->data.sequence.start;
        for (size_t i = 0; i < source->data.sequence.count; i++) {
            total += val;
            val += source->data.sequence.step;
        }
        snprintf(buf, sizeof(buf), "%d", total);
        LogicExpression *compressed = lm_logic_constant(buf);
        if (compressed) {
            compressed->complexity = 0.05;
            printf("[META] Folded sequence to constant sum = %d\n", total);
        }
        return compressed;
    }

    /* Стратегия "simplify": применяем lm_optimize_logic */
    if (strcmp(strategy, "simplify") == 0) {
        LogicExpression *copy = clone_logic(source);
        if (copy) {
            LogicExpression *opt = lm_optimize_logic(copy);
            if (opt != copy) {
                lm_destroy_logic(copy);
                return opt;
            }
            return copy;
        }
    }

    /* Fallback — просто копируем */
    return clone_logic(source);
}

LogicExpression* mf_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *target_memory
) {
    if (!store || !meta || !target_memory) return NULL;

    LogicExpression *result = NULL;

    switch (meta->operation) {
        case META_GENERATE_CONSTANT: {
            const char *value = meta->params.generate_constant.value;
            result = lm_logic_constant(value ? value : "");
            printf("[META] Generated constant(\"%s\") from meta-formula\n",
                   value ? value : "");
            break;
        }

        case META_GENERATE_REPEAT: {
            /* Генерируем repeat() логику из формул */
            const char *pattern = meta->params.gen_repeat.pattern_formula;
            int count = 0;

            if (evaluate_simple_formula(meta->params.gen_repeat.count_formula, &count) != 0) {
                return NULL;
            }
            if (count <= 0) count = 1;

            result = lm_logic_repeat(pattern, (size_t)count);
            printf("[META] Generated repeat(\"%s\", %d) from meta-formula\n",
                   pattern, count);
            break;
        }

        case META_GENERATE_COMPOSE: {
            /*
             * Композиция: берём все формулы из хранилища
             * и объединяем их результаты в LOGIC_COMPOSITION
             */
            if (store->cache_count < 2) {
                printf("[META] Compose requires at least 2 cached expressions\n");
                break;
            }
            /* Берём последние два элемента кэша */
            LogicExpression *a = clone_logic(
                store->generated_cache[store->cache_count - 2]);
            LogicExpression *b = clone_logic(
                store->generated_cache[store->cache_count - 1]);
            if (a && b) {
                result = lm_logic_compose(a, b);
                printf("[META] Generated composition of %zu cached expressions\n",
                       store->cache_count);
            } else {
                lm_destroy_logic(a);
                lm_destroy_logic(b);
            }
            break;
        }

        case META_GENERATE_SEQUENCE: {
            /* Генерируем sequence() логику */
            int start = 0, step = 0, count = 0;

            evaluate_simple_formula(meta->params.gen_sequence.start_formula, &start);
            evaluate_simple_formula(meta->params.gen_sequence.step_formula, &step);
            evaluate_simple_formula(meta->params.gen_sequence.count_formula, &count);
            if (count <= 0) count = 1;

            result = lm_logic_sequence(start, step, (size_t)count);
            printf("[META] Generated sequence(%d, %d, %d) from meta-formula\n",
                   start, step, count);
            break;
        }

        case META_TRANSFORM_LOGIC: {
            /* Трансформируем существующую логику по правилу */
            const char *input_id = meta->params.transform.input_logic_id;
            const char *rule     = meta->params.transform.transform_rule;

            LogicExpression *src = find_logic_by_id(target_memory, input_id);
            if (!src) {
                printf("[META] Transform: source '%s' not found\n", input_id);
                return NULL;
            }

            result = apply_transform(src, rule);
            if (result) {
                printf("[META] Transformed '%s' with rule '%s'\n", input_id, rule);
            }
            break;
        }

        case META_DERIVE_RELATION: {
            /* Выводим новые отношения из существующих */
            const char *left_id  = meta->params.derive.left_logic_id;
            const char *right_id = meta->params.derive.right_logic_id;
            const char *rule     = meta->params.derive.inference_rule;

            LogicExpression *left_src  = find_logic_by_id(target_memory, left_id);
            LogicExpression *right_src = find_logic_by_id(target_memory, right_id);

            if (!left_src)  left_src  = lm_logic_constant(left_id);
            else            left_src  = clone_logic(left_src);
            if (!right_src) right_src = lm_logic_constant(right_id);
            else            right_src = clone_logic(right_src);

            if (!left_src || !right_src) {
                lm_destroy_logic(left_src);
                lm_destroy_logic(right_src);
                return NULL;
            }

            /* Инференс: если правило «transitive» — строим цепочку */
            if (strcmp(rule, "transitive") == 0) {
                result = lm_logic_relation(left_src, right_src, "derives_from");
            } else if (strcmp(rule, "equivalence") == 0) {
                result = lm_logic_relation(left_src, right_src, "equivalent");
            } else if (strcmp(rule, "part_of") == 0) {
                result = lm_logic_relation(left_src, right_src, "part_of");
            } else {
                /* Произвольное отношение */
                result = lm_logic_relation(left_src, right_src, rule);
            }

            printf("[META] Derived relation: %s → %s (rule: %s)\n",
                   left_id, right_id, rule);
            break;
        }

        case META_EVOLVE_PATTERN: {
            /* Эволюционируем паттерн через N поколений */
            const char *src_id = meta->params.evolve.source_pattern_id;
            double mut_rate    = meta->params.evolve.mutation_rate;
            int generations    = meta->params.evolve.generations;

            LogicExpression *src = find_logic_by_id(target_memory, src_id);
            if (!src) {
                /* Если источника нет — создаём базовый паттерн */
                src = lm_logic_repeat(src_id, 10);
                if (!src) return NULL;
                result = evolve_pattern(src, mut_rate, generations);
                lm_destroy_logic(src);
            } else {
                result = evolve_pattern(src, mut_rate, generations);
            }

            if (result) {
                printf("[META] Evolved pattern '%s' over %d generations (rate=%.2f)\n",
                       src_id, generations, mut_rate);
            }
            break;
        }

        case META_COMPRESS_LOGIC: {
            /* Сжимаем логическое выражение в более компактную форму */
            const char *target_id = meta->params.compress.target_logic_id;
            const char *strategy  = meta->params.compress.compression_strategy;

            LogicExpression *src = find_logic_by_id(target_memory, target_id);
            if (!src) {
                printf("[META] Compress: source '%s' not found\n", target_id);
                return NULL;
            }

            result = compress_logic_expr(src, strategy);
            if (result) {
                printf("[META] Compressed '%s' with strategy '%s' "
                       "(complexity %.2f → %.2f)\n",
                       target_id, strategy,
                       src->complexity, result->complexity);
            }
            break;
        }
    }

    /* Кэшируем результат */
    if (result && store->cache_count < 256) {
        store->generated_cache[store->cache_count] = result;
        snprintf(store->cache_ids[store->cache_count], 63,
                 "meta_gen_%zu", store->cache_count);
        store->cache_count++;
    }

    return result;
}

/* ========== ХРАНЕНИЕ МЕТА-ФОРМУЛ ========== */

int mf_store_meta(MetaFormulaStore *store, const MetaFormula *meta, const char *id) {
    if (!store || !meta || !id || store->count >= 256) return -1;

    memcpy(&store->formulas[store->count], meta, sizeof(MetaFormula));
    /* Сохраняем ID в cache_ids для поиска */
    strncpy(store->cache_ids[store->count], id, 63);
    store->count++;

    return 0;
}

MetaFormula* mf_load_meta(MetaFormulaStore *store, const char *id) {
    if (!store || !id) return NULL;

    /* Поиск по ID */
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->cache_ids[i], id) == 0) {
            return &store->formulas[i];
        }
    }

    return NULL;
}

/* ========== ОПТИМИЗАЦИЯ И ЭВОЛЮЦИЯ ========== */

MetaFormula* mf_optimize_meta(const MetaFormula *meta) {
    if (!meta) return NULL;

    MetaFormula *optimized = malloc(sizeof(MetaFormula));
    if (!optimized) return NULL;
    memcpy(optimized, meta, sizeof(MetaFormula));

    /* Оптимизации по типу операции */
    switch (meta->operation) {
        case META_GENERATE_REPEAT: {
            /* Если count_formula — вычислимое выражение, сворачиваем */
            int count = 0;
            if (evaluate_simple_formula(meta->params.gen_repeat.count_formula, &count) == 0
                && count > 0) {
                snprintf(optimized->params.gen_repeat.count_formula, 63, "%d", count);
                optimized->complexity_score = meta->complexity_score * 0.8;
            }
            break;
        }
        case META_GENERATE_SEQUENCE: {
            /* Предвычисляем все формулы */
            int v = 0;
            if (evaluate_simple_formula(meta->params.gen_sequence.start_formula, &v) == 0) {
                snprintf(optimized->params.gen_sequence.start_formula, 63, "%d", v);
            }
            if (evaluate_simple_formula(meta->params.gen_sequence.step_formula, &v) == 0) {
                snprintf(optimized->params.gen_sequence.step_formula, 63, "%d", v);
            }
            if (evaluate_simple_formula(meta->params.gen_sequence.count_formula, &v) == 0) {
                snprintf(optimized->params.gen_sequence.count_formula, 63, "%d", v);
            }
            optimized->complexity_score = meta->complexity_score * 0.7;
            break;
        }
        default:
            /* Общая оптимизация: уменьшаем сложность */
            optimized->complexity_score *= 0.9;
            break;
    }

    printf("[META] Optimized meta-formula: complexity %.2f → %.2f\n",
           meta->complexity_score, optimized->complexity_score);

    return optimized;
}

MetaFormula* mf_evolve_meta(const MetaFormula *meta, double mutation_rate) {
    if (!meta) return NULL;

    MetaFormula *evolved = malloc(sizeof(MetaFormula));
    if (!evolved) return NULL;
    memcpy(evolved, meta, sizeof(MetaFormula));

    /* Увеличиваем поколение */
    evolved->generation = meta->generation + 1;

    unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)meta);

    /* Мутация зависит от типа */
    double r = (double)(rand_r(&seed) % 1000) / 1000.0;
    if (r < mutation_rate) {
        switch (meta->operation) {
            case META_GENERATE_REPEAT: {
                int old_count = 0;
                evaluate_simple_formula(meta->params.gen_repeat.count_formula, &old_count);
                int delta = (rand_r(&seed) % 10) - 4;  /* -4..+5 */
                int new_count = old_count + delta;
                if (new_count < 1) new_count = 1;
                snprintf(evolved->params.gen_repeat.count_formula, 63, "%d", new_count);
                break;
            }
            case META_GENERATE_SEQUENCE: {
                /* Мутируем step или start */
                if (rand_r(&seed) % 2 == 0) {
                    int v = 0;
                    evaluate_simple_formula(meta->params.gen_sequence.step_formula, &v);
                    v += (rand_r(&seed) % 5) - 2;
                    snprintf(evolved->params.gen_sequence.step_formula, 63, "%d", v);
                } else {
                    int v = 0;
                    evaluate_simple_formula(meta->params.gen_sequence.start_formula, &v);
                    v += (rand_r(&seed) % 11) - 5;
                    snprintf(evolved->params.gen_sequence.start_formula, 63, "%d", v);
                }
                break;
            }
            case META_EVOLVE_PATTERN: {
                /* Мета-мутация: изменяем mutation_rate */
                evolved->params.evolve.mutation_rate *= 0.9 + (rand_r(&seed) % 20) * 0.01;
                if (evolved->params.evolve.mutation_rate > 1.0)
                    evolved->params.evolve.mutation_rate = 1.0;
                evolved->params.evolve.generations += (rand_r(&seed) % 3);
                break;
            }
            default:
                break;
        }
    }

    printf("[META] Evolved meta-formula: generation %llu → %llu\n",
           (unsigned long long)meta->generation,
           (unsigned long long)evolved->generation);

    return evolved;
}

MetaFormula* mf_compose_meta(const MetaFormula *meta1, const MetaFormula *meta2) {
    if (!meta1 || !meta2) return NULL;

    MetaFormula *composed = calloc(1, sizeof(MetaFormula));
    if (!composed) return NULL;

    /* Умная композиция: зависит от типов */
    if (meta1->operation == META_GENERATE_REPEAT &&
        meta2->operation == META_GENERATE_REPEAT) {
        /* repeat + repeat → новый repeat с суммированным count */
        composed->operation = META_GENERATE_REPEAT;
        strncpy(composed->params.gen_repeat.pattern_formula,
                meta1->params.gen_repeat.pattern_formula, 63);
        int c1 = 0, c2 = 0;
        evaluate_simple_formula(meta1->params.gen_repeat.count_formula, &c1);
        evaluate_simple_formula(meta2->params.gen_repeat.count_formula, &c2);
        snprintf(composed->params.gen_repeat.count_formula, 63, "%d", c1 + c2);

    } else if (meta1->operation == META_GENERATE_SEQUENCE &&
               meta2->operation == META_GENERATE_SEQUENCE) {
        /* seq + seq → новый seq с расширенным count */
        composed->operation = META_GENERATE_SEQUENCE;
        strncpy(composed->params.gen_sequence.start_formula,
                meta1->params.gen_sequence.start_formula, 63);
        strncpy(composed->params.gen_sequence.step_formula,
                meta1->params.gen_sequence.step_formula, 63);
        int c1 = 0, c2 = 0;
        evaluate_simple_formula(meta1->params.gen_sequence.count_formula, &c1);
        evaluate_simple_formula(meta2->params.gen_sequence.count_formula, &c2);
        snprintf(composed->params.gen_sequence.count_formula, 63, "%d", c1 + c2);

    } else {
        /* Разнородная композиция → трансформация */
        composed->operation = META_TRANSFORM_LOGIC;
        strncpy(composed->params.transform.transform_rule, "compose_mixed", 127);
    }

    composed->generation = (meta1->generation + meta2->generation) / 2 + 1;
    composed->complexity_score = meta1->complexity_score + meta2->complexity_score;
    composed->output_size_estimate =
        meta1->output_size_estimate + meta2->output_size_estimate;

    printf("[META] Composed two meta-formulas (types %d + %d)\n",
           meta1->operation, meta2->operation);

    return composed;
}

/* ========== СТАТИСТИКА ========== */

int mf_get_stats(MetaFormulaStore *store, MetaFormulaStats *stats) {
    if (!store || !stats) return -1;

    memset(stats, 0, sizeof(MetaFormulaStats));

    stats->total_meta_formulas = store->count;
    stats->generated_logic_count = store->cache_count;
    stats->meta_size_bytes = store->count * sizeof(MetaFormula);

    /* Подсчитываем размер сгенерированной логики */
    for (size_t i = 0; i < store->cache_count; i++) {
        if (store->generated_cache[i]) {
            stats->logic_size_bytes += sizeof(LogicExpression);
            /* Учитываем вложенные выражения */
            if (store->generated_cache[i]->type == LOGIC_REPEAT &&
                store->generated_cache[i]->data.repeat.pattern) {
                stats->logic_size_bytes += sizeof(LogicExpression);
            } else if (store->generated_cache[i]->type == LOGIC_COMPOSITION) {
                stats->logic_size_bytes +=
                    store->generated_cache[i]->data.composition.count * sizeof(LogicExpression);
            }
        }
    }

    if (stats->logic_size_bytes > 0) {
        stats->meta_to_logic_ratio = (double)stats->meta_size_bytes / (double)stats->logic_size_bytes;
    } else {
        stats->meta_to_logic_ratio = 0.0;
    }

    return 0;
}

int mf_to_string(const MetaFormula *meta, char *output, size_t output_size) {
    if (!meta || !output || output_size == 0) return -1;

    int written = 0;

    switch (meta->operation) {
        case META_GENERATE_CONSTANT:
            written = snprintf(output, output_size,
                "meta_constant(value='%s')",
                meta->params.generate_constant.value ?
                    meta->params.generate_constant.value : "<null>");
            break;

        case META_GENERATE_REPEAT:
            written = snprintf(output, output_size,
                "meta_repeat(pattern='%s', count='%s')",
                meta->params.gen_repeat.pattern_formula,
                meta->params.gen_repeat.count_formula);
            break;

        case META_GENERATE_SEQUENCE:
            written = snprintf(output, output_size,
                "meta_sequence(start='%s', step='%s', count='%s')",
                meta->params.gen_sequence.start_formula,
                meta->params.gen_sequence.step_formula,
                meta->params.gen_sequence.count_formula);
            break;

        case META_GENERATE_COMPOSE:
            written = snprintf(output, output_size, "meta_compose()");
            break;

        case META_TRANSFORM_LOGIC:
            written = snprintf(output, output_size,
                "meta_transform(input='%s', rule='%s')",
                meta->params.transform.input_logic_id,
                meta->params.transform.transform_rule);
            break;

        case META_DERIVE_RELATION:
            written = snprintf(output, output_size,
                "meta_derive(%s → %s, rule='%s')",
                meta->params.derive.left_logic_id,
                meta->params.derive.right_logic_id,
                meta->params.derive.inference_rule);
            break;

        case META_EVOLVE_PATTERN:
            written = snprintf(output, output_size,
                "meta_evolve(source='%s', rate=%.2f, gens=%d)",
                meta->params.evolve.source_pattern_id,
                meta->params.evolve.mutation_rate,
                meta->params.evolve.generations);
            break;

        case META_COMPRESS_LOGIC:
            written = snprintf(output, output_size,
                "meta_compress(target='%s', strategy='%s')",
                meta->params.compress.target_logic_id,
                meta->params.compress.compression_strategy);
            break;

        default:
            written = snprintf(output, output_size, "meta_unknown()");
    }

    return written;
}

/* ========== ПРОДВИНУТЫЕ ОПЕРАЦИИ ========== */

int mf_auto_discover_patterns(
    LogicalMemory *memory,
    MetaFormulaStore *store
) {
    if (!memory || !store) return -1;

    printf("[META] Auto-discovering patterns in logical memory "
           "(%zu cells)...\n", memory->cell_count);

    int discovered_count = 0;

    /* Проход 0: Оптимизируем существующую логику (находим внутренние паттерны в константах) */
    for (size_t i = 0; i < memory->cell_count; i++) {
        if (memory->cells[i].logic) {
            memory->cells[i].logic = lm_optimize_logic(memory->cells[i].logic);
        }
    }

    /* Проход 1: ищем повторяющиеся repeat-паттерны */
    for (size_t i = 0; i < memory->cell_count; i++) {
        LogicCell *cell = &memory->cells[i];
        if (!cell->logic) continue;

        if (cell->logic->type == LOGIC_REPEAT &&
            cell->logic->data.repeat.pattern &&
            cell->logic->data.repeat.pattern->type == LOGIC_CONSTANT) {

            const char *pat = cell->logic->data.repeat.pattern->data.constant.value;
            char count_str[64];
            snprintf(count_str, sizeof(count_str), "%zu", cell->logic->data.repeat.count);

            MetaFormula *mf = mf_create_repeat_generator(pat, count_str);
            if (mf && store->count < 256) {
                char id[64];
                snprintf(id, sizeof(id), "auto_repeat_%zu", i);
                mf_store_meta(store, mf, id);
                discovered_count++;
                free(mf);
            }
        }

        /* Ищем последовательности */
        if (cell->logic->type == LOGIC_SEQUENCE) {
            char start_str[64], step_str[64], count_str[64];
            snprintf(start_str, sizeof(start_str), "%d", cell->logic->data.sequence.start);
            snprintf(step_str, sizeof(step_str), "%d", cell->logic->data.sequence.step);
            snprintf(count_str, sizeof(count_str), "%zu", cell->logic->data.sequence.count);

            MetaFormula *mf = mf_create_sequence_generator(start_str, step_str, count_str);
            if (mf && store->count < 256) {
                char id[64];
                snprintf(id, sizeof(id), "auto_seq_%zu", i);
                mf_store_meta(store, mf, id);
                discovered_count++;
                free(mf);
            }
        }

        /* Ищем пары для вывода отношений */
        if (cell->logic->type == LOGIC_RELATION) {
            MetaFormula *mf = mf_create_relation_deriver(
                cell->id, cell->id, "equivalence");
            if (mf && store->count < 256) {
                char id[64];
                snprintf(id, sizeof(id), "auto_rel_%zu", i);
                mf_store_meta(store, mf, id);
                discovered_count++;
                free(mf);
            }
        }
    }

    /* Проход 2: ищем возможности для сжатия (композиции с одинаковыми паттернами) */
    for (size_t i = 0; i < memory->cell_count; i++) {
        LogicCell *cell = &memory->cells[i];
        if (!cell->logic || cell->logic->type != LOGIC_COMPOSITION) continue;

        MetaFormula *mf = mf_create_logic_compressor(cell->id, "merge_repeats");
        if (mf && store->count < 256) {
            char id[64];
            snprintf(id, sizeof(id), "auto_compress_%zu", i);
            mf_store_meta(store, mf, id);
            discovered_count++;
            free(mf);
        }
    }

    printf("[META] Discovered %d patterns\n", discovered_count);
    return discovered_count;
}

int mf_batch_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *memory,
    const char **cell_ids,
    size_t cell_count
) {
    if (!store || !meta || !memory || !cell_ids) return -1;

    int success_count = 0;

    for (size_t i = 0; i < cell_count; i++) {
        LogicExpression *result = mf_execute(store, meta, memory);
        if (result) {
            /* Клонируем, т.к. оригинал хранится в кэше store */
            LogicExpression *copy = clone_logic(result);
            if (!copy) continue;

            /* Добавляем копию в ячейку памяти */
            if (memory->cell_count < 1024) {
                snprintf(memory->cells[memory->cell_count].id, 64, "%s", cell_ids[i]);
                memory->cells[memory->cell_count].logic = copy;
                memory->cells[memory->cell_count].cached_data = NULL;
                memory->cells[memory->cell_count].cache_valid = 0;
                memory->cells[memory->cell_count].dependency_count = 0;
                memory->cell_count++;
                success_count++;
            } else {
                lm_destroy_logic(copy);
            }
        }
    }

    printf("[META] Batch executed meta-formula: %d/%zu cells\n",
           success_count, cell_count);

    return success_count;
}

MetaFormula* mf_infer_meta(
    MetaFormulaStore *store,
    const char *rule,
    const MetaFormula **input_metas,
    size_t input_count
) {
    if (!store || !rule || !input_metas || input_count == 0) return NULL;

    /* Правило "combine": объединяем пару мета-формул */
    if (strcmp(rule, "combine") == 0 && input_count >= 2) {
        return mf_compose_meta(input_metas[0], input_metas[1]);
    }

    /* Правило "generalize": обобщаем множество формул в одну */
    if (strcmp(rule, "generalize") == 0) {
        /* Находим общий тип */
        MetaOperation common_op = input_metas[0]->operation;
        int all_same = 1;
        for (size_t i = 1; i < input_count; i++) {
            if (input_metas[i]->operation != common_op) {
                all_same = 0;
                break;
            }
        }

        MetaFormula *generalized = calloc(1, sizeof(MetaFormula));
        if (!generalized) return NULL;

        if (all_same && common_op == META_GENERATE_REPEAT) {
            /* Обобщаем repeat-формулы: берём средний count */
            generalized->operation = META_GENERATE_REPEAT;
            strncpy(generalized->params.gen_repeat.pattern_formula,
                    input_metas[0]->params.gen_repeat.pattern_formula, 63);
            int total = 0;
            for (size_t i = 0; i < input_count; i++) {
                int c = 0;
                evaluate_simple_formula(
                    input_metas[i]->params.gen_repeat.count_formula, &c);
                total += c;
            }
            snprintf(generalized->params.gen_repeat.count_formula, 63,
                     "%d", total / (int)input_count);
        } else if (all_same && common_op == META_GENERATE_SEQUENCE) {
            /* Обобщаем sequence-формулы: берём средние параметры */
            generalized->operation = META_GENERATE_SEQUENCE;
            int ts = 0, tt = 0, tc = 0;
            for (size_t i = 0; i < input_count; i++) {
                int v = 0;
                evaluate_simple_formula(
                    input_metas[i]->params.gen_sequence.start_formula, &v);
                ts += v;
                evaluate_simple_formula(
                    input_metas[i]->params.gen_sequence.step_formula, &v);
                tt += v;
                evaluate_simple_formula(
                    input_metas[i]->params.gen_sequence.count_formula, &v);
                tc += v;
            }
            int n = (int)input_count;
            snprintf(generalized->params.gen_sequence.start_formula, 63, "%d", ts / n);
            snprintf(generalized->params.gen_sequence.step_formula, 63, "%d", tt / n);
            snprintf(generalized->params.gen_sequence.count_formula, 63, "%d", tc / n);
        } else {
            /* Разнородные — просто копируем первую */
            memcpy(generalized, input_metas[0], sizeof(MetaFormula));
        }

        generalized->complexity_score = 0.5;
        generalized->generation = 0;

        printf("[META] Inferred generalized meta-formula from %zu inputs (type=%d)\n",
               input_count, generalized->operation);
        return generalized;
    }

    /* Правило "specialize": специализируем формулу по одному примеру */
    if (strcmp(rule, "specialize") == 0 && input_count >= 1) {
        MetaFormula *spec = malloc(sizeof(MetaFormula));
        if (!spec) return NULL;
        memcpy(spec, input_metas[0], sizeof(MetaFormula));
        spec->complexity_score *= 2.0;  /* Специализация увеличивает сложность */
        spec->generation++;
        printf("[META] Inferred specialized meta-formula\n");
        return spec;
    }

    return NULL;
}

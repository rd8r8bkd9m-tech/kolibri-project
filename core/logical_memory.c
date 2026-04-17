/*
 * logical_memory.c
 * 
 * Реализация логической памяти без данных
 * Данные существуют только как логические выражения и материализуются по требованию
 */

#include "kolibri/logical_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ========== СОЗДАНИЕ/УНИЧТОЖЕНИЕ ========== */

LogicalMemory* lm_create(void) {
    LogicalMemory *mem = (LogicalMemory*)calloc(1, sizeof(LogicalMemory));
    if (!mem) return NULL;
    
    mem->cell_count = 0;
    mem->total_logic_size = 0;
    mem->total_materialized_size = 0;
    mem->compression_ratio = 1.0;
    
    return mem;
}

void lm_destroy(LogicalMemory *mem) {
    if (!mem) return;
    
    /* Освобождаем все ячейки */
    for (size_t i = 0; i < mem->cell_count; i++) {
        LogicCell *cell = &mem->cells[i];
        
        /* Освобождаем логику */
        if (cell->logic) {
            lm_destroy_logic(cell->logic);
            cell->logic = NULL;
        }
        
        /* Освобождаем кэш */
        if (cell->cached_data) {
            free(cell->cached_data);
            cell->cached_data = NULL;
        }
    }
    
    free(mem);
}

void lm_destroy_logic(LogicExpression *logic) {
    if (!logic) return;

    /* Освобождаем вложенные выражения */
    switch (logic->type) {
        case LOGIC_CONSTANT:
            if (logic->data.constant.value) {
                free(logic->data.constant.value);
            }
            break;
        case LOGIC_REPEAT:
            if (logic->data.repeat.pattern) {
                lm_destroy_logic(logic->data.repeat.pattern);
            }
            break;
        case LOGIC_COMPOSITION:
            for (size_t i = 0; i < logic->data.composition.count; i++) {
                if (logic->data.composition.expressions[i]) {
                    lm_destroy_logic(logic->data.composition.expressions[i]);
                }
            }
            break;
        case LOGIC_RELATION:
            if (logic->data.relation.left) {
                lm_destroy_logic(logic->data.relation.left);
            }
            if (logic->data.relation.right) {
                lm_destroy_logic(logic->data.relation.right);
            }
            break;
        case LOGIC_VARIABLE:
            /* Не освобождаем binding — им владеет кто-то другой */
            break;
        case LOGIC_TRANSFORM:
            if (logic->data.transform.input) {
                lm_destroy_logic(logic->data.transform.input);
            }
            break;
        case LOGIC_CONDITIONAL:
            if (logic->data.conditional.condition) {
                lm_destroy_logic(logic->data.conditional.condition);
            }
            if (logic->data.conditional.then_expr) {
                lm_destroy_logic(logic->data.conditional.then_expr);
            }
            if (logic->data.conditional.else_expr) {
                lm_destroy_logic(logic->data.conditional.else_expr);
            }
            break;
        default:
            break;
    }

    free(logic);
}

LogicalMemory* lm_create_memory() {
    return lm_create();
}

void lm_destroy_memory(LogicalMemory *mem) {
    lm_destroy(mem);
}

LogicExpression* lm_create_logic_expression() {
    return calloc(1, sizeof(LogicExpression));
}

/* ========== СОЗДАНИЕ ЛОГИЧЕСКИХ ВЫРАЖЕНИЙ ========== */

LogicExpression* lm_logic_constant(const char *value) {
    if (!value) return NULL;

    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_CONSTANT;
    expr->data.constant.value = strdup(value);
    expr->data.constant.length = strlen(value);

    expr->complexity = 0.1;
    expr->materialized_size = expr->data.constant.length;

    return expr;
}

LogicExpression* lm_logic_repeat(const char *pattern, size_t count) {
    if (!pattern || count == 0) return NULL;
    
    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;
    
    expr->type = LOGIC_REPEAT;
    
    /* Создаём паттерн как константу */
    LogicExpression *pattern_expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!pattern_expr) {
        free(expr);
        return NULL;
    }
    
    pattern_expr->type = LOGIC_CONSTANT;
    pattern_expr->data.constant.value = strdup(pattern);
    pattern_expr->data.constant.length = strlen(pattern);
    
    expr->data.repeat.pattern = pattern_expr;
    expr->data.repeat.count = count;
    
    /* Метаданные */
    expr->creation_time = (uint64_t)time(NULL);
    expr->complexity = 1.0;
    expr->materialized_size = strlen(pattern) * count;
    
    return expr;
}

LogicExpression* lm_logic_sequence(int start, int step, size_t count) {
    if (count == 0) return NULL;
    
    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;
    
    expr->type = LOGIC_SEQUENCE;
    expr->data.sequence.start = start;
    expr->data.sequence.step = step;
    expr->data.sequence.count = count;
    
    expr->complexity = 1.0;
    expr->materialized_size = count * 4; /* Примерно 4 байта на число */
    
    return expr;
}

LogicExpression* lm_logic_compose(LogicExpression *expr1, LogicExpression *expr2) {
    if (!expr1 || !expr2) return NULL;
    
    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;
    
    expr->type = LOGIC_COMPOSITION;
    expr->data.composition.expressions[0] = expr1;
    expr->data.composition.expressions[1] = expr2;
    expr->data.composition.count = 2;
    
    expr->complexity = expr1->complexity + expr2->complexity;
    expr->materialized_size = expr1->materialized_size + expr2->materialized_size;
    
    return expr;
}

LogicExpression* lm_logic_relation(LogicExpression *left, LogicExpression *right, const char *type) {
    if (!left || !right || !type) return NULL;
    
    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;
    
    expr->type = LOGIC_RELATION;
    expr->data.relation.left = left;
    expr->data.relation.right = right;
    strncpy(expr->data.relation.relation_type, type, sizeof(expr->data.relation.relation_type) - 1);
    
    expr->complexity = left->complexity + right->complexity + 0.5;
    expr->materialized_size = 0; /* Отношение не материализуется в данные */
    
    return expr;
}

/* ========== ХРАНЕНИЕ И МАТЕРИАЛИЗАЦИЯ ========== */

int lm_store_logic(LogicalMemory *mem, const char *id, LogicExpression *logic) {
    if (!mem || !id || !logic) return -1;
    if (mem->cell_count >= 1024) return -1;
    
    LogicCell *cell = &mem->cells[mem->cell_count];
    
    strncpy(cell->id, id, sizeof(cell->id) - 1);
    cell->logic = logic;
    cell->cached_data = NULL;
    cell->cached_size = 0;
    cell->cache_valid = 0;
    cell->dependency_count = 0;
    
    mem->total_logic_size += sizeof(LogicExpression);
    mem->total_materialized_size += logic->materialized_size;
    
    if (mem->total_materialized_size > 0) {
        mem->compression_ratio = (double)mem->total_materialized_size / mem->total_logic_size;
    }
    
    mem->cell_count++;
    return 0;
}

/* Материализация LOGIC_REPEAT */
static int materialize_repeat(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_REPEAT) return -1;
    
    LogicExpression *pattern = expr->data.repeat.pattern;
    if (pattern->type != LOGIC_CONSTANT) return -1;
    
    const char *pat = pattern->data.constant.value;
    size_t pat_len = pattern->data.constant.length;
    size_t count = expr->data.repeat.count;
    
    size_t required = pat_len * count;
    if (output_size < required + 1) return -1;
    
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(output + pos, pat, pat_len);
        pos += pat_len;
    }
    output[pos] = '\0';
    
    return (int)pos;
}

/* Материализация LOGIC_SEQUENCE */
static int materialize_sequence(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_SEQUENCE) return -1;
    
    int value = expr->data.sequence.start;
    int step = expr->data.sequence.step;
    size_t count = expr->data.sequence.count;
    
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        int written = snprintf(output + pos, output_size - pos, "%d", value);
        if (written < 0 || (size_t)written >= output_size - pos) return -1;
        pos += written;
        value += step;
    }
    
    return (int)pos;
}

/* Материализация LOGIC_COMPOSITION */
static int materialize_composition(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_COMPOSITION) return -1;
    
    size_t pos = 0;
    for (size_t i = 0; i < expr->data.composition.count; i++) {
        LogicExpression *sub = expr->data.composition.expressions[i];
        
        int result;
        switch (sub->type) {
            case LOGIC_REPEAT:
                result = materialize_repeat(sub, output + pos, output_size - pos);
                break;
            case LOGIC_SEQUENCE:
                result = materialize_sequence(sub, output + pos, output_size - pos);
                break;
            case LOGIC_CONSTANT:
                result = snprintf(output + pos, output_size - pos, "%s", sub->data.constant.value);
                break;
            default:
                return -1;
        }
        
        if (result < 0) return -1;
        pos += result;
    }
    
    return (int)pos;
}

/* Материализация LOGIC_VARIABLE (через привязку) */
static int materialize_variable(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_VARIABLE) return -1;

    if (expr->data.variable.binding) {
        /* Материализуем привязанное выражение */
        switch (expr->data.variable.binding->type) {
            case LOGIC_CONSTANT:
                return snprintf(output, output_size, "%s",
                    expr->data.variable.binding->data.constant.value);
            case LOGIC_REPEAT:
                return materialize_repeat(expr->data.variable.binding, output, output_size);
            case LOGIC_SEQUENCE:
                return materialize_sequence(expr->data.variable.binding, output, output_size);
            default:
                break;
        }
    }

    /* Непривязанная переменная — возвращаем имя */
    return snprintf(output, output_size, "${%s}", expr->data.variable.name);
}

/* Материализация LOGIC_TRANSFORM */
static int materialize_transform(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_TRANSFORM || !expr->data.transform.input) return -1;

    /* Сначала материализуем вход */
    char *input_buf = malloc(output_size);
    if (!input_buf) return -1;

    int input_len = -1;
    switch (expr->data.transform.input->type) {
        case LOGIC_CONSTANT:
            input_len = snprintf(input_buf, output_size, "%s",
                expr->data.transform.input->data.constant.value);
            break;
        case LOGIC_REPEAT:
            input_len = materialize_repeat(expr->data.transform.input, input_buf, output_size);
            break;
        case LOGIC_SEQUENCE:
            input_len = materialize_sequence(expr->data.transform.input, input_buf, output_size);
            break;
        default:
            free(input_buf);
            return -1;
    }

    if (input_len < 0) {
        free(input_buf);
        return -1;
    }

    /* Применяем функцию трансформации */
    if (expr->data.transform.transform_fn) {
        int rc = expr->data.transform.transform_fn(input_buf, output);
        free(input_buf);
        return rc >= 0 ? (int)strlen(output) : -1;
    }

    /* Если нет функции — просто копируем */
    memcpy(output, input_buf, (size_t)input_len);
    output[input_len] = '\0';
    free(input_buf);
    return input_len;
}

/* Материализация LOGIC_CONDITIONAL */
static int materialize_conditional(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_CONDITIONAL || !expr->data.conditional.condition) return -1;

    /* Условие: если condition — constant и непустой → then, иначе → else */
    int condition_true = 0;
    LogicExpression *cond = expr->data.conditional.condition;

    if (cond->type == LOGIC_CONSTANT) {
        condition_true = (cond->data.constant.length > 0 &&
                         strcmp(cond->data.constant.value, "0") != 0 &&
                         strcmp(cond->data.constant.value, "") != 0);
    } else if (cond->type == LOGIC_SEQUENCE) {
        condition_true = (cond->data.sequence.count > 0);
    } else {
        condition_true = 1;  /* Ненулевое выражение — true */
    }

    LogicExpression *branch = condition_true
        ? expr->data.conditional.then_expr
        : expr->data.conditional.else_expr;

    if (!branch) {
        output[0] = '\0';
        return 0;
    }

    switch (branch->type) {
        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "%s", branch->data.constant.value);
        case LOGIC_REPEAT:
            return materialize_repeat(branch, output, output_size);
        case LOGIC_SEQUENCE:
            return materialize_sequence(branch, output, output_size);
        case LOGIC_COMPOSITION:
            return materialize_composition(branch, output, output_size);
        default:
            return -1;
    }
}

/* Материализация LOGIC_RELATION (текстовое представление) */
static int materialize_relation(LogicExpression *expr, char *output, size_t output_size) {
    if (expr->type != LOGIC_RELATION) return -1;

    char left_str[128] = "<expr>";
    char right_str[128] = "<expr>";

    if (expr->data.relation.left && expr->data.relation.left->type == LOGIC_CONSTANT) {
        strncpy(left_str, expr->data.relation.left->data.constant.value, 127);
    }
    if (expr->data.relation.right && expr->data.relation.right->type == LOGIC_CONSTANT) {
        strncpy(right_str, expr->data.relation.right->data.constant.value, 127);
    }

    return snprintf(output, output_size, "%s -[%s]-> %s",
        left_str, expr->data.relation.relation_type, right_str);
}

int lm_materialize(LogicalMemory *mem, const char *id, void *output, size_t output_size) {
    if (!mem || !id || !output) return -1;
    
    /* Найти ячейку */
    LogicCell *cell = NULL;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (strcmp(mem->cells[i].id, id) == 0) {
            cell = &mem->cells[i];
            break;
        }
    }
    
    if (!cell || !cell->logic) return -1;
    
    /* Проверить кэш */
    if (cell->cache_valid && cell->cached_data) {
        size_t copy_size = cell->cached_size < output_size ? cell->cached_size : output_size;
        memcpy(output, cell->cached_data, copy_size);
        /* Возвращаем длину строки (без \0), как и при первичной материализации */
        return (int)(cell->cached_size > 0 ? cell->cached_size - 1 : 0);
    }
    
    /* Материализация в зависимости от типа */
    int result = -1;
    switch (cell->logic->type) {
        case LOGIC_REPEAT:
            result = materialize_repeat(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_SEQUENCE:
            result = materialize_sequence(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_COMPOSITION:
            result = materialize_composition(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_CONSTANT:
            result = snprintf((char*)output, output_size, "%s", cell->logic->data.constant.value);
            break;
        case LOGIC_VARIABLE:
            result = materialize_variable(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_TRANSFORM:
            result = materialize_transform(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_CONDITIONAL:
            result = materialize_conditional(cell->logic, (char*)output, output_size);
            break;
        case LOGIC_RELATION:
            result = materialize_relation(cell->logic, (char*)output, output_size);
            break;
        default:
            return -1;
    }
    
    /* Кэшируем результат (включая \0 для строк) */
    if (result > 0) {
        size_t cache_size = (size_t)result + 1;  /* +1 для null-terminator */
        cell->cached_data = malloc(cache_size);
        if (cell->cached_data) {
            memcpy(cell->cached_data, output, cache_size);
            cell->cached_size = cache_size;
            cell->cache_valid = 1;
        }
    }
    
    return result;
}

char* lm_materialize_logic(LogicExpression* logic) {
    if (!logic) return NULL;

    size_t predicted_size = logic->materialized_size;
    if (predicted_size == 0) { // Fallback for relations or un-sized logic
        predicted_size = 4096;
    }

    char* buffer = malloc(predicted_size + 1);
    if (!buffer) return NULL;

    int result = -1;
    switch (logic->type) {
        case LOGIC_REPEAT:
            result = materialize_repeat(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_SEQUENCE:
            result = materialize_sequence(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_COMPOSITION:
            result = materialize_composition(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_CONSTANT:
            result = snprintf(buffer, predicted_size + 1, "%s", logic->data.constant.value);
            break;
        case LOGIC_L5_SUPER:
            /* Phase 3: JIT unpacking of 6-byte super formula */
            result = snprintf(buffer, predicted_size + 1, "[L5_JIT_UNPACK: TYPE=%d PAYLOAD=%u]", 
                              logic->data.l5_super.super_type, logic->data.l5_super.payload_hash);
            break;
        case LOGIC_VARIABLE:
            result = materialize_variable(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_TRANSFORM:
            result = materialize_transform(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_CONDITIONAL:
            result = materialize_conditional(logic, buffer, predicted_size + 1);
            break;
        case LOGIC_RELATION:
            result = materialize_relation(logic, buffer, predicted_size + 1);
            break;
        default:
            free(buffer);
            return NULL;
    }

    if (result < 0) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

size_t lm_predict_size(LogicalMemory *mem, const char *id) {
    if (!mem || !id) return 0;
    
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (strcmp(mem->cells[i].id, id) == 0) {
            return mem->cells[i].logic->materialized_size;
        }
    }
    
    return 0;
}

/* ========== УТИЛИТЫ ========== */

double lm_compute_complexity(LogicExpression *logic) {
    if (!logic) return 0.0;
    return logic->complexity;
}

int lm_logic_to_string(LogicExpression *logic, char *output, size_t output_size) {
    if (!logic || !output) return -1;

    switch (logic->type) {
        case LOGIC_L5_SUPER:
            return snprintf(output, output_size, "l5_super(type=%d, payload=%u)", 
                            logic->data.l5_super.super_type, logic->data.l5_super.payload_hash);
        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "const(\"%s\")", logic->data.constant.value);

        case LOGIC_REPEAT:
            if (logic->data.repeat.pattern &&
                logic->data.repeat.pattern->type == LOGIC_CONSTANT) {
                return snprintf(output, output_size, "repeat(\"%s\", %zu)",
                               logic->data.repeat.pattern->data.constant.value,
                               logic->data.repeat.count);
            }
            return snprintf(output, output_size, "repeat(<pattern>, %zu)", logic->data.repeat.count);

        case LOGIC_SEQUENCE:
            return snprintf(output, output_size, "sequence(%d, %d, %zu)",
                           logic->data.sequence.start,
                           logic->data.sequence.step,
                           logic->data.sequence.count);

        case LOGIC_COMPOSITION:
            return snprintf(output, output_size, "compose(%zu expressions)",
                           logic->data.composition.count);

        case LOGIC_RELATION:
            return snprintf(output, output_size, "relation(%s)",
                           logic->data.relation.relation_type);

        case LOGIC_VARIABLE:
            if (logic->data.variable.binding) {
                return snprintf(output, output_size, "var(%s=<bound>)",
                               logic->data.variable.name);
            }
            return snprintf(output, output_size, "var(%s)", logic->data.variable.name);

        case LOGIC_TRANSFORM:
            return snprintf(output, output_size, "transform(%s)",
                           logic->data.transform.transform_fn ? "fn" : "identity");

        case LOGIC_CONDITIONAL:
            return snprintf(output, output_size, "if(<cond>, <then>, <else>)");

        default:
            return snprintf(output, output_size, "<unknown>");
    }
}

int lm_get_stats(LogicalMemory *mem, LogicalMemoryStats *stats) {
    if (!mem || !stats) return -1;
    
    stats->total_cells = mem->cell_count;
    stats->logic_size_bytes = mem->total_logic_size;
    stats->predicted_data_size = mem->total_materialized_size;
    stats->compression_ratio = mem->compression_ratio;
    
    stats->cached_cells = 0;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (mem->cells[i].cache_valid) {
            stats->cached_cells++;
        }
    }
    
    stats->cache_hit_rate = mem->cell_count > 0 
        ? (double)stats->cached_cells / mem->cell_count 
        : 0.0;
    
    return 0;
}

/* Найти паттерн повторения в начале строки. Возвращает количество поглощенных байт. */
static size_t find_partial_repeat(const char *data, size_t len, char *pattern_out, size_t *count_out) {
    if (len < 4) return 0;

    /* Пробуем разные длины паттерна */
    for (size_t p_len = 1; p_len <= len / 2; p_len++) {
        size_t current_count = 1;
        
        /* Считаем сколько раз паттерн повторяется подряд */
        for (size_t i = p_len; i + p_len <= len; i += p_len) {
            if (strncmp(data, data + i, p_len) == 0) {
                current_count++;
            } else {
                break;
            }
        }

        /* Если повторился хотя бы 2 раза и общая длина >= 4 */
        if (current_count >= 2 && p_len * current_count >= 4) {
            strncpy(pattern_out, data, p_len);
            pattern_out[p_len] = '\0';
            *count_out = current_count;
            return p_len * current_count;
        }
    }

    return 0;
}

/* Найти арифметическую прогрессию в строке (если это числа) */
static int find_sequence_pattern(const char *data, size_t len, int *start, int *step, size_t *count) {
    /* Очень упрощенный парсинг: ищем числа через пробел */
    int nums[64];
    size_t n_count = 0;
    
    char *buf = strdup(data);
    char *token = strtok(buf, " ,");
    while (token && n_count < 64) {
        char *endptr;
        long val = strtol(token, &endptr, 10);
        if (*endptr == '\0') {
            nums[n_count++] = (int)val;
        } else {
            free(buf);
            return 0; /* Не только числа */
        }
        token = strtok(NULL, " ,");
    }
    free(buf);

    if (n_count < 3) return 0;

    int d = nums[1] - nums[0];
    for (size_t i = 2; i < n_count; i++) {
        if (nums[i] - nums[i-1] != d) return 0;
    }

    *start = nums[0];
    *step = d;
    *count = n_count;
    return 1;
}

/* Рекурсивное создание логики из строки с поиском паттернов */
LogicExpression* lm_logic_from_string(const char *data, size_t len) {
    if (len == 0) return NULL;

    char pat[256];
    size_t count;
    int start, step;

    /* 1. Пытаемся найти арифметическую прогрессию (на всю строку) */
    if (find_sequence_pattern(data, len, &start, &step, &count)) {
        LogicExpression *expr = calloc(1, sizeof(LogicExpression));
        expr->type = LOGIC_SEQUENCE;
        expr->data.sequence.start = start;
        expr->data.sequence.step = step;
        expr->data.sequence.count = count;
        expr->complexity = 1.0;
        expr->materialized_size = len;
        return expr;
    }

    /* 2. Пытаемся найти повтор в начале */
    size_t consumed = find_partial_repeat(data, len, pat, &count);
    if (consumed > 0) {
        LogicExpression *repeat_expr = calloc(1, sizeof(LogicExpression));
        repeat_expr->type = LOGIC_REPEAT;
        repeat_expr->data.repeat.count = count;
        repeat_expr->data.repeat.pattern = lm_logic_constant(pat);
        repeat_expr->complexity = 1.0;
        repeat_expr->materialized_size = consumed;

        if (consumed == len) {
            return repeat_expr;
        } else {
            /* Композиция: repeat + остаток */
            LogicExpression *rem_expr = lm_logic_from_string(data + consumed, len - consumed);
            return lm_logic_compose(repeat_expr, rem_expr);
        }
    }

    /* 3. Ничего не нашли — возвращаем константу */
    char *val = malloc(len + 1);
    memcpy(val, data, len);
    val[len] = '\0';
    LogicExpression *expr = lm_logic_constant(val);
    free(val);
    return expr;
}

LogicExpression* lm_optimize_logic(LogicExpression *logic) {
    if (!logic) return logic;

    /*
     * Оптимизация 0: Поиск паттернов в константах
     */
    if (logic->type == LOGIC_CONSTANT && logic->data.constant.value) {
        LogicExpression *new_logic = lm_logic_from_string(logic->data.constant.value, 
                                                        logic->data.constant.length);
        if (new_logic && new_logic != logic) {
            if (new_logic->type != LOGIC_CONSTANT) {
                printf("[OPT] Constant optimized into %d type\n", new_logic->type);
                lm_destroy_logic(logic);
                return new_logic;
            } else {
                lm_destroy_logic(new_logic);
            }
        }
    }

    /*
     * Оптимизация 1: repeat(repeat(x, a), b) → repeat(x, a*b)
     * Сворачиваем вложенные repeat-выражения
     */
    if (logic->type == LOGIC_REPEAT &&
        logic->data.repeat.pattern &&
        logic->data.repeat.pattern->type == LOGIC_REPEAT) {

        LogicExpression *inner = logic->data.repeat.pattern;
        size_t combined_count = logic->data.repeat.count * inner->data.repeat.count;

        /* Перемещаем внутренний паттерн наверх */
        LogicExpression *base_pattern = inner->data.repeat.pattern;
        inner->data.repeat.pattern = NULL;  /* Отсоединяем, чтобы не удалить */

        logic->data.repeat.pattern = base_pattern;
        logic->data.repeat.count = combined_count;

        /* Пересчитываем метаданные */
        if (base_pattern && base_pattern->type == LOGIC_CONSTANT) {
            logic->materialized_size = base_pattern->data.constant.length * combined_count;
        }
        logic->complexity *= 0.5;

        /* Удаляем промежуточный узел */
        free(inner);

        printf("[OPT] Folded nested repeat: count=%zu\n", combined_count);
        return logic;
    }

    /*
     * Оптимизация 2: compose(const(A), const(B)) → const(AB)
     * Сворачиваем композицию констант
     */
    if (logic->type == LOGIC_COMPOSITION) {
        int all_constants = 1;
        size_t total_len = 0;
        for (size_t i = 0; i < logic->data.composition.count; i++) {
            if (!logic->data.composition.expressions[i] ||
                logic->data.composition.expressions[i]->type != LOGIC_CONSTANT) {
                all_constants = 0;
                break;
            }
            total_len += logic->data.composition.expressions[i]->data.constant.length;
        }

        if (all_constants) {
            char *merged = malloc(total_len + 1);
            if (!merged) return logic;

            size_t pos = 0;
            for (size_t i = 0; i < logic->data.composition.count; i++) {
                const char *val = logic->data.composition.expressions[i]->data.constant.value;
                size_t len = logic->data.composition.expressions[i]->data.constant.length;
                memcpy(merged + pos, val, len);
                pos += len;
                lm_destroy_logic(logic->data.composition.expressions[i]);
                logic->data.composition.expressions[i] = NULL;
            }
            merged[pos] = '\0';

            logic->type = LOGIC_CONSTANT;
            memset(&logic->data, 0, sizeof(logic->data));
            logic->data.constant.value = merged;
            logic->data.constant.length = pos;
            logic->materialized_size = pos;
            logic->complexity = 0.1;

            printf("[OPT] Merged %zu constants into one (len=%zu)\n",
                   logic->data.composition.count, pos);
        }
    }

    /*
     * Оптимизация 3: variable с привязкой → заменяем на привязку
     */
    if (logic->type == LOGIC_VARIABLE && logic->data.variable.binding) {
        /* Возвращаем привязанное выражение (оптимизация вызывающая стороной) */
        return logic;
    }

    return logic;
}

/* ========== СЕРИАЛИЗАЦИЯ (Stage 3.1) ========== */

static void serialize_logic(FILE *f, LogicExpression *logic) {
    if (!logic) {
        uint32_t marker = 0xFFFFFFFF;
        fwrite(&marker, sizeof(uint32_t), 1, f);
        return;
    }

    uint32_t type = (uint32_t)logic->type;
    fwrite(&type, sizeof(uint32_t), 1, f);
    fwrite(&logic->complexity, sizeof(double), 1, f);
    fwrite(&logic->materialized_size, sizeof(size_t), 1, f);

    switch (logic->type) {
        case LOGIC_CONSTANT:
            fwrite(&logic->data.constant.length, sizeof(size_t), 1, f);
            fwrite(logic->data.constant.value, 1, logic->data.constant.length, f);
            break;
        case LOGIC_REPEAT:
            fwrite(&logic->data.repeat.count, sizeof(size_t), 1, f);
            serialize_logic(f, logic->data.repeat.pattern);
            break;
        case LOGIC_SEQUENCE:
            fwrite(&logic->data.sequence, sizeof(logic->data.sequence), 1, f);
            break;
        case LOGIC_COMPOSITION:
            fwrite(&logic->data.composition.count, sizeof(size_t), 1, f);
            for (size_t i = 0; i < logic->data.composition.count; i++) {
                serialize_logic(f, logic->data.composition.expressions[i]);
            }
            break;
        case LOGIC_VARIABLE:
            fwrite(logic->data.variable.name, 1, 32, f);
            /* Binding не сохраняем, он должен восстанавливаться в контексте */
            break;
        case LOGIC_RELATION:
            fwrite(logic->data.relation.relation_type, 1, 16, f);
            serialize_logic(f, logic->data.relation.left);
            serialize_logic(f, logic->data.relation.right);
            break;
        case LOGIC_TRANSFORM:
            serialize_logic(f, logic->data.transform.input);
            /* Указатель на функцию не сохраняем! */
            break;
        case LOGIC_CONDITIONAL:
            serialize_logic(f, logic->data.conditional.condition);
            serialize_logic(f, logic->data.conditional.then_expr);
            serialize_logic(f, logic->data.conditional.else_expr);
            break;
        case LOGIC_L5_SUPER:
            fwrite(&logic->data.l5_super.super_type, 1, 1, f);
            fwrite(&logic->data.l5_super.payload_hash, 4, 1, f);
            fwrite(&logic->data.l5_super.checksum, 1, 1, f);
            break;
        default:
            break;
    }
}

static LogicExpression* deserialize_logic(FILE *f) {
    uint32_t type_raw;
    if (fread(&type_raw, sizeof(uint32_t), 1, f) != 1) return NULL;
    if (type_raw == 0xFFFFFFFF) return NULL;

    LogicExpression *logic = calloc(1, sizeof(LogicExpression));
    logic->type = (LogicType)type_raw;
    fread(&logic->complexity, sizeof(double), 1, f);
    fread(&logic->materialized_size, sizeof(size_t), 1, f);

    switch (logic->type) {
        case LOGIC_CONSTANT:
            fread(&logic->data.constant.length, sizeof(size_t), 1, f);
            logic->data.constant.value = malloc(logic->data.constant.length + 1);
            fread(logic->data.constant.value, 1, logic->data.constant.length, f);
            logic->data.constant.value[logic->data.constant.length] = '\0';
            break;
        case LOGIC_REPEAT:
            fread(&logic->data.repeat.count, sizeof(size_t), 1, f);
            logic->data.repeat.pattern = deserialize_logic(f);
            break;
        case LOGIC_SEQUENCE:
            fread(&logic->data.sequence, sizeof(logic->data.sequence), 1, f);
            break;
        case LOGIC_COMPOSITION:
            fread(&logic->data.composition.count, sizeof(size_t), 1, f);
            for (size_t i = 0; i < logic->data.composition.count; i++) {
                logic->data.composition.expressions[i] = deserialize_logic(f);
            }
            break;
        case LOGIC_VARIABLE:
            fread(logic->data.variable.name, 1, 32, f);
            break;
        case LOGIC_RELATION:
            fread(logic->data.relation.relation_type, 1, 16, f);
            logic->data.relation.left = deserialize_logic(f);
            logic->data.relation.right = deserialize_logic(f);
            break;
        case LOGIC_TRANSFORM:
            logic->data.transform.input = deserialize_logic(f);
            break;
        case LOGIC_CONDITIONAL:
            logic->data.conditional.condition = deserialize_logic(f);
            logic->data.conditional.then_expr = deserialize_logic(f);
            logic->data.conditional.else_expr = deserialize_logic(f);
            break;
        case LOGIC_L5_SUPER:
            fread(&logic->data.l5_super.super_type, 1, 1, f);
            fread(&logic->data.l5_super.payload_hash, 4, 1, f);
            fread(&logic->data.l5_super.checksum, 1, 1, f);
            break;
        default:
            break;
    }

    return logic;
}

int lm_save(LogicalMemory *mem, const char *filename) {
    if (!mem || !filename) return -1;
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;

    uint32_t magic = 0x4B4C4231; /* "KLB1" */
    fwrite(&magic, sizeof(uint32_t), 1, f);
    fwrite(&mem->cell_count, sizeof(size_t), 1, f);

    for (size_t i = 0; i < mem->cell_count; i++) {
        LogicCell *cell = &mem->cells[i];
        fwrite(cell->id, 1, 64, f);
        serialize_logic(f, cell->logic);
    }

    fclose(f);
    printf("[STORAGE] Saved %zu logical cells to %s\n", mem->cell_count, filename);
    return 0;
}

LogicalMemory* lm_load(const char *filename) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    uint32_t magic;
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1 || magic != 0x4B4C4231) {
        fclose(f);
        return NULL;
    }

    LogicalMemory *mem = lm_create_memory();
    if (fread(&mem->cell_count, sizeof(size_t), 1, f) != 1) {
        lm_destroy_memory(mem);
        fclose(f);
        return NULL;
    }

    for (size_t i = 0; i < mem->cell_count; i++) {
        LogicCell *cell = &mem->cells[i];
        fread(cell->id, 1, 64, f);
        cell->logic = deserialize_logic(f);
        cell->cache_valid = 0;
        cell->cached_data = NULL;
        cell->dependency_count = 0;
    }

    fclose(f);
    printf("[STORAGE] Loaded %zu logical cells from %s\n", mem->cell_count, filename);
    return mem;
}

/* ========== НОВЫЕ ТИПЫ ЛОГИЧЕСКИХ ВЫРАЖЕНИЙ ========== */

LogicExpression* lm_logic_variable(const char *name) {
    if (!name) return NULL;

    LogicExpression *expr = calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_VARIABLE;
    strncpy(expr->data.variable.name, name, sizeof(expr->data.variable.name) - 1);
    expr->data.variable.binding = NULL;

    expr->complexity = 0.05;
    expr->materialized_size = 0;
    expr->creation_time = (uint64_t)time(NULL);

    return expr;
}

int lm_logic_bind_variable(LogicExpression *variable, LogicExpression *binding) {
    if (!variable || variable->type != LOGIC_VARIABLE) return -1;
    variable->data.variable.binding = binding;
    if (binding) {
        variable->materialized_size = binding->materialized_size;
    }
    return 0;
}

LogicExpression* lm_logic_transform(LogicExpression *input, int (*fn)(const void*, void*)) {
    if (!input) return NULL;

    LogicExpression *expr = calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_TRANSFORM;
    expr->data.transform.input = input;
    expr->data.transform.transform_fn = fn;

    expr->complexity = input->complexity + 1.0;
    expr->materialized_size = input->materialized_size;
    expr->creation_time = (uint64_t)time(NULL);

    return expr;
}

LogicExpression* lm_logic_conditional(
    LogicExpression *condition,
    LogicExpression *then_expr,
    LogicExpression *else_expr
) {
    if (!condition || !then_expr) return NULL;

    LogicExpression *expr = calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_CONDITIONAL;
    expr->data.conditional.condition = condition;
    expr->data.conditional.then_expr = then_expr;
    expr->data.conditional.else_expr = else_expr;

    expr->complexity = condition->complexity +
                       then_expr->complexity +
                       (else_expr ? else_expr->complexity : 0.0) + 0.5;
    expr->materialized_size =
        then_expr->materialized_size > (else_expr ? else_expr->materialized_size : 0)
            ? then_expr->materialized_size
            : (else_expr ? else_expr->materialized_size : 0);
    expr->creation_time = (uint64_t)time(NULL);

    return expr;
}


/* ========== L5 GENERATIVE ENCODING ========== */

LogicExpression* lm_logic_l5_super(uint8_t type, uint32_t payload) {
    LogicExpression *expr = calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;

    expr->type = LOGIC_L5_SUPER;
    expr->data.l5_super.super_type = type;
    expr->data.l5_super.payload_hash = payload;
    /* Basic checksum: type XOR bytes of payload */
    expr->data.l5_super.checksum = type ^ (payload & 0xFF) ^ ((payload >> 8) & 0xFF) ^ ((payload >> 16) & 0xFF) ^ ((payload >> 24) & 0xFF);
    
    expr->complexity = 1.0; /* Expensive to unpack */
    expr->materialized_size = 1024; /* Estimated size of JIT unpacked data */
    expr->creation_time = (uint64_t)time(NULL);

    return expr;
}



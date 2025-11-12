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
    strncpy(expr->data.constant.value, value, sizeof(expr->data.constant.value) - 1);
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
    strncpy(pattern_expr->data.constant.value, pattern, sizeof(pattern_expr->data.constant.value) - 1);
    pattern_expr->data.constant.length = strlen(pattern);
    
    expr->data.repeat.pattern = pattern_expr;
    expr->data.repeat.count = count;
    
    /* Метаданные */
    expr->creation_time = 0; /* TODO: timestamp */
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
        return (int)copy_size;
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
        default:
            return -1;
    }
    
    /* Кэшируем результат */
    if (result > 0) {
        cell->cached_data = malloc(result);
        if (cell->cached_data) {
            memcpy(cell->cached_data, output, result);
            cell->cached_size = result;
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
        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "const(\"%s\")", logic->data.constant.value);
        
        case LOGIC_REPEAT:
            if (logic->data.repeat.pattern->type == LOGIC_CONSTANT) {
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
            return snprintf(output, output_size, "compose(%zu expressions)", logic->data.composition.count);
        
        case LOGIC_RELATION:
            return snprintf(output, output_size, "relation(%s)", logic->data.relation.relation_type);
        
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

LogicExpression* lm_optimize_logic(LogicExpression *logic) {
    /* TODO: Оптимизация логических выражений */
    /* Например: repeat(repeat(x, 3), 2) -> repeat(x, 6) */
    return logic;
}

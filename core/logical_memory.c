#include "logical_memory.h"
#include "kolibri/logical_memory.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LOGICAL_CELLS 32768

typedef struct {
    char *premise;
    char *conclusion;
    float confidence;
} CliLogicalCell;

static CliLogicalCell *memory = NULL;
static int cell_count = 0;
static int quiet_mode = 0;

static char *safe_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1U;
    char *d = (char*)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

void kolibri_mem_set_quiet(int quiet) {
    quiet_mode = quiet != 0;
}

static int memory_should_log(void) {
    const char *env = getenv("KOLIBRI_QUIET");
    if (quiet_mode) return 0;
    if (env && strcmp(env, "0") != 0 && strcmp(env, "false") != 0) return 0;
    return 1;
}

void kolibri_mem_init(void) {
    if (memory) return;
    memory = (CliLogicalCell*)calloc((size_t)MAX_LOGICAL_CELLS, sizeof(CliLogicalCell));
    if (!memory) return;
    cell_count = 0;
    if (memory_should_log()) {
        printf("[KOLIBRI] Логическая память: %d ячеек готова.\n", MAX_LOGICAL_CELLS);
    }
}

void kolibri_mem_store(const char *premise, const char *conclusion, float confidence) {
    if (!memory || cell_count >= MAX_LOGICAL_CELLS || !premise || !conclusion) return;
    memory[cell_count].premise = safe_strdup(premise);
    memory[cell_count].conclusion = safe_strdup(conclusion);
    memory[cell_count].confidence = confidence;
    cell_count++;
}

int kolibri_mem_query(const char *query, char *out_buf, int buf_size) {
    if (!memory || !query || !out_buf || buf_size < 1) return 0;

    for (int i = 0; i < cell_count; i++) {
        if (memory[i].premise && strstr(query, memory[i].premise)) {
            strncpy(out_buf, memory[i].conclusion, (size_t)buf_size - 1U);
            out_buf[buf_size - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

static int is_digit_string(const char *s) {
    if (!s || *s == '\0') return 0;
    for (const unsigned char *p = (const unsigned char*)s; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

static size_t parse_size_value(const char *s, size_t fallback) {
    if (!s || *s == '\0') return fallback;
    size_t value = 0;
    for (const unsigned char *p = (const unsigned char*)s; *p; ++p) {
        if (*p < '0' || *p > '9') return fallback;
        size_t digit = (size_t)(*p - '0');
        if (value > (SIZE_MAX - digit) / 10U) return fallback;
        value = value * 10U + digit;
    }
    return value;
}

static LogicExpression *logic_alloc(LogicType type) {
    LogicExpression *expr = (LogicExpression*)calloc(1, sizeof(LogicExpression));
    if (!expr) return NULL;
    expr->type = type;
    expr->creation_time = (uint64_t)time(NULL);
    return expr;
}

LogicalMemory *lm_create_memory(void) {
    LogicalMemory *mem = (LogicalMemory*)calloc(1, sizeof(LogicalMemory));
    if (!mem) return NULL;

    mem->cell_capacity = LM_MAX_CELLS;
    mem->cells = (LogicCell*)calloc(mem->cell_capacity, sizeof(LogicCell));
    if (!mem->cells) {
        free(mem);
        return NULL;
    }
    mem->compression_ratio = 1.0;
    return mem;
}

static void clear_cell(LogicCell *cell) {
    if (!cell) return;
    if (cell->logic) {
        lm_destroy_logic(cell->logic);
        cell->logic = NULL;
    }
    free(cell->cached_data);
    cell->cached_data = NULL;
    cell->cached_size = 0;
    cell->cache_valid = 0;
}

void lm_destroy_memory(LogicalMemory *mem) {
    if (!mem) return;
    for (size_t i = 0; i < mem->cell_count; i++) {
        clear_cell(&mem->cells[i]);
    }
    free(mem->cells);
    free(mem);
}

void lm_destroy_logic(LogicExpression *logic) {
    if (!logic) return;

    switch (logic->type) {
        case LOGIC_CONSTANT:
            free(logic->data.constant.value);
            break;
        case LOGIC_REPEAT:
            lm_destroy_logic(logic->data.repeat.pattern);
            break;
        case LOGIC_COMPOSITION:
            for (size_t i = 0; i < logic->data.composition.count && i < 8U; i++) {
                lm_destroy_logic(logic->data.composition.expressions[i]);
            }
            break;
        case LOGIC_RELATION:
            lm_destroy_logic(logic->data.relation.left);
            lm_destroy_logic(logic->data.relation.right);
            break;
        case LOGIC_TRANSFORM:
            lm_destroy_logic(logic->data.transform.input);
            break;
        case LOGIC_CONDITIONAL:
            lm_destroy_logic(logic->data.conditional.condition);
            lm_destroy_logic(logic->data.conditional.then_expr);
            lm_destroy_logic(logic->data.conditional.else_expr);
            break;
        case LOGIC_DIGIT_STREAM:
            free(logic->data.stream.digits);
            break;
        case LOGIC_VARIABLE:
        case LOGIC_SEQUENCE:
        case LOGIC_L5_SUPER:
        case LOGIC_NOP:
        default:
            break;
    }

    free(logic);
}

LogicExpression *lm_logic_constant(const char *value) {
    if (!value) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_CONSTANT);
    if (!expr) return NULL;

    expr->data.constant.value = safe_strdup(value);
    if (!expr->data.constant.value) {
        free(expr);
        return NULL;
    }
    expr->data.constant.length = strlen(value);
    expr->complexity = 0.1;
    expr->materialized_size = expr->data.constant.length;
    return expr;
}

LogicExpression *lm_logic_repeat(const char *pattern, size_t count) {
    if (!pattern || count == 0) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_REPEAT);
    if (!expr) return NULL;

    expr->data.repeat.pattern = lm_logic_constant(pattern);
    if (!expr->data.repeat.pattern) {
        free(expr);
        return NULL;
    }
    expr->data.repeat.count = count;
    expr->complexity = 1.0;
    expr->materialized_size = strlen(pattern) * count;
    return expr;
}

LogicExpression *lm_logic_sequence(int start, int step, size_t count) {
    if (count == 0) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_SEQUENCE);
    if (!expr) return NULL;
    expr->data.sequence.start = start;
    expr->data.sequence.step = step;
    expr->data.sequence.count = count;
    expr->complexity = 1.0;
    expr->materialized_size = count * 4U;
    return expr;
}

LogicExpression *lm_logic_compose(LogicExpression *expr1, LogicExpression *expr2) {
    if (!expr1 || !expr2) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_COMPOSITION);
    if (!expr) return NULL;
    expr->data.composition.expressions[0] = expr1;
    expr->data.composition.expressions[1] = expr2;
    expr->data.composition.count = 2;
    expr->complexity = expr1->complexity + expr2->complexity;
    expr->materialized_size = expr1->materialized_size + expr2->materialized_size;
    return expr;
}

LogicExpression *lm_logic_relation(LogicExpression *left, LogicExpression *right, const char *type) {
    if (!left || !right || !type) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_RELATION);
    if (!expr) return NULL;
    expr->data.relation.left = left;
    expr->data.relation.right = right;
    strncpy(expr->data.relation.relation_type, type, sizeof(expr->data.relation.relation_type) - 1U);
    expr->complexity = left->complexity + right->complexity + 0.5;
    expr->materialized_size = 0;
    return expr;
}

LogicExpression *lm_logic_variable(const char *name) {
    if (!name) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_VARIABLE);
    if (!expr) return NULL;
    strncpy(expr->data.variable.name, name, sizeof(expr->data.variable.name) - 1U);
    expr->complexity = 0.05;
    return expr;
}

int lm_logic_bind_variable(LogicExpression *variable, LogicExpression *binding) {
    if (!variable || variable->type != LOGIC_VARIABLE) return -1;
    variable->data.variable.binding = binding;
    variable->materialized_size = binding ? binding->materialized_size : 0;
    return 0;
}

LogicExpression *lm_logic_transform(LogicExpression *input, int (*fn)(const void*, void*)) {
    if (!input) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_TRANSFORM);
    if (!expr) return NULL;
    expr->data.transform.input = input;
    expr->data.transform.transform_fn = fn;
    expr->complexity = input->complexity + 1.0;
    expr->materialized_size = input->materialized_size;
    return expr;
}

LogicExpression *lm_logic_conditional(
    LogicExpression *condition,
    LogicExpression *then_expr,
    LogicExpression *else_expr
) {
    if (!condition || !then_expr) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_CONDITIONAL);
    if (!expr) return NULL;
    expr->data.conditional.condition = condition;
    expr->data.conditional.then_expr = then_expr;
    expr->data.conditional.else_expr = else_expr;
    expr->complexity = condition->complexity + then_expr->complexity +
        (else_expr ? else_expr->complexity : 0.0) + 0.5;
    expr->materialized_size = then_expr->materialized_size;
    if (else_expr && else_expr->materialized_size > expr->materialized_size) {
        expr->materialized_size = else_expr->materialized_size;
    }
    return expr;
}

LogicExpression *lm_logic_l5_super(uint8_t type, uint32_t payload) {
    LogicExpression *expr = logic_alloc(LOGIC_L5_SUPER);
    if (!expr) return NULL;
    expr->data.l5_super.super_type = type;
    expr->data.l5_super.payload_hash = payload;
    expr->data.l5_super.checksum = (uint8_t)(type ^ (payload & 0xFFU));
    expr->complexity = 0.01;
    expr->materialized_size = 64;
    return expr;
}

LogicExpression *lm_logic_stream(const uint8_t *digits, size_t len) {
    if (!digits && len > 0) return NULL;
    LogicExpression *expr = logic_alloc(LOGIC_DIGIT_STREAM);
    if (!expr) return NULL;

    if (len > 0) {
        expr->data.stream.digits = (uint8_t*)malloc(len);
        if (!expr->data.stream.digits) {
            free(expr);
            return NULL;
        }
        for (size_t i = 0; i < len; i++) {
            if (digits[i] > 9U) {
                lm_destroy_logic(expr);
                return NULL;
            }
            expr->data.stream.digits[i] = digits[i];
        }
    }
    expr->data.stream.length = len;
    expr->materialized_size = len;
    expr->complexity = 0.1;
    return expr;
}

int lm_store_logic(LogicalMemory *mem, const char *id, LogicExpression *logic) {
    if (!mem || !id || !logic || !mem->cells) return -1;
    if (mem->cell_count >= mem->cell_capacity) return -1;

    LogicCell *cell = &mem->cells[mem->cell_count++];
    strncpy(cell->id, id, sizeof(cell->id) - 1U);
    cell->id[sizeof(cell->id) - 1U] = '\0';
    strncpy(cell->hash, id, sizeof(cell->hash) - 1U);
    cell->hash[sizeof(cell->hash) - 1U] = '\0';
    cell->logic = logic;

    mem->total_logic_size += sizeof(LogicExpression);
    mem->total_materialized_size += logic->materialized_size;
    if (mem->total_logic_size > 0) {
        mem->compression_ratio =
            (double)mem->total_materialized_size / (double)mem->total_logic_size;
    }
    return 0;
}

static int materialize_expr(const LogicExpression *expr, char *output, size_t output_size);

static int append_expr(const LogicExpression *expr, char *output, size_t output_size, size_t *pos) {
    if (!pos || *pos >= output_size) return -1;
    int n = materialize_expr(expr, output + *pos, output_size - *pos);
    if (n < 0) return -1;
    *pos += (size_t)n;
    return 0;
}

static int materialize_sequence(const LogicExpression *expr, char *output, size_t output_size) {
    size_t pos = 0;
    int value = expr->data.sequence.start;
    for (size_t i = 0; i < expr->data.sequence.count; i++) {
        if (pos >= output_size) return -1;
        int written = snprintf(output + pos, output_size - pos, "%d", value);
        if (written < 0 || (size_t)written >= output_size - pos) return -1;
        pos += (size_t)written;
        value += expr->data.sequence.step;
    }
    return (int)pos;
}

static int condition_is_true(const LogicExpression *condition) {
    if (!condition) return 0;
    if (condition->type == LOGIC_CONSTANT) {
        return condition->data.constant.length > 0 &&
               strcmp(condition->data.constant.value, "0") != 0;
    }
    if (condition->type == LOGIC_SEQUENCE) {
        return condition->data.sequence.count > 0;
    }
    if (condition->type == LOGIC_DIGIT_STREAM) {
        return condition->data.stream.length > 0;
    }
    return 1;
}

static int materialize_expr(const LogicExpression *expr, char *output, size_t output_size) {
    if (!expr || !output || output_size == 0) return -1;

    switch (expr->type) {
        case LOGIC_CONSTANT: {
            const char *value = expr->data.constant.value ? expr->data.constant.value : "";
            int written = snprintf(output, output_size, "%s", value);
            return (written >= 0 && (size_t)written < output_size) ? written : -1;
        }

        case LOGIC_REPEAT: {
            size_t pos = 0;
            for (size_t i = 0; i < expr->data.repeat.count; i++) {
                if (append_expr(expr->data.repeat.pattern, output, output_size, &pos) != 0) {
                    return -1;
                }
            }
            if (pos >= output_size) return -1;
            output[pos] = '\0';
            return (int)pos;
        }

        case LOGIC_SEQUENCE:
            return materialize_sequence(expr, output, output_size);

        case LOGIC_COMPOSITION: {
            size_t pos = 0;
            for (size_t i = 0; i < expr->data.composition.count && i < 8U; i++) {
                if (append_expr(expr->data.composition.expressions[i], output, output_size, &pos) != 0) {
                    return -1;
                }
            }
            if (pos >= output_size) return -1;
            output[pos] = '\0';
            return (int)pos;
        }

        case LOGIC_VARIABLE:
            if (expr->data.variable.binding) {
                return materialize_expr(expr->data.variable.binding, output, output_size);
            }
            {
                int written = snprintf(output, output_size, "${%s}", expr->data.variable.name);
                return (written >= 0 && (size_t)written < output_size) ? written : -1;
            }

        case LOGIC_TRANSFORM: {
            int input_len = materialize_expr(expr->data.transform.input, output, output_size);
            if (input_len < 0) return -1;
            if (!expr->data.transform.transform_fn) return input_len;

            char *input = safe_strdup(output);
            if (!input) return -1;
            int rc = expr->data.transform.transform_fn(input, output);
            free(input);
            return rc >= 0 ? (int)strlen(output) : -1;
        }

        case LOGIC_CONDITIONAL: {
            const LogicExpression *branch = condition_is_true(expr->data.conditional.condition)
                ? expr->data.conditional.then_expr
                : expr->data.conditional.else_expr;
            if (!branch) {
                output[0] = '\0';
                return 0;
            }
            return materialize_expr(branch, output, output_size);
        }

        case LOGIC_RELATION: {
            char left[256];
            char right[256];
            if (materialize_expr(expr->data.relation.left, left, sizeof(left)) < 0) {
                strncpy(left, "<expr>", sizeof(left) - 1U);
                left[sizeof(left) - 1U] = '\0';
            }
            if (materialize_expr(expr->data.relation.right, right, sizeof(right)) < 0) {
                strncpy(right, "<expr>", sizeof(right) - 1U);
                right[sizeof(right) - 1U] = '\0';
            }
            int written = snprintf(output, output_size, "%s -[%s]-> %s",
                left, expr->data.relation.relation_type, right);
            return (written >= 0 && (size_t)written < output_size) ? written : -1;
        }

        case LOGIC_L5_SUPER: {
            int written = snprintf(output, output_size,
                "[L5_JIT_UNPACK: TYPE=%u PAYLOAD=%u]",
                expr->data.l5_super.super_type,
                expr->data.l5_super.payload_hash);
            return (written >= 0 && (size_t)written < output_size) ? written : -1;
        }

        case LOGIC_DIGIT_STREAM:
            if (output_size < expr->data.stream.length + 1U) return -1;
            for (size_t i = 0; i < expr->data.stream.length; i++) {
                output[i] = (char)('0' + expr->data.stream.digits[i]);
            }
            output[expr->data.stream.length] = '\0';
            return (int)expr->data.stream.length;

        case LOGIC_NOP:
            output[0] = '\0';
            return 0;

        default:
            return -1;
    }
}

static LogicCell *find_cell(LogicalMemory *mem, const char *id) {
    if (!mem || !id) return NULL;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (strcmp(mem->cells[i].id, id) == 0 || strcmp(mem->cells[i].hash, id) == 0) {
            return &mem->cells[i];
        }
    }
    return NULL;
}

int lm_materialize(LogicalMemory *mem, const char *id, void *output, size_t output_size) {
    if (!mem || !id || !output || output_size == 0) return -1;
    LogicCell *cell = find_cell(mem, id);
    if (!cell || !cell->logic) return -1;

    if (cell->cache_valid && cell->cached_data) {
        if (output_size < cell->cached_size) return -1;
        memcpy(output, cell->cached_data, cell->cached_size);
        return (int)(cell->cached_size > 0 ? cell->cached_size - 1U : 0U);
    }

    int result = materialize_expr(cell->logic, (char*)output, output_size);
    if (result >= 0) {
        free(cell->cached_data);
        cell->cached_size = (size_t)result + 1U;
        cell->cached_data = malloc(cell->cached_size);
        if (cell->cached_data) {
            memcpy(cell->cached_data, output, cell->cached_size);
            cell->cache_timestamp = (uint64_t)time(NULL);
            cell->cache_valid = 1;
        } else {
            cell->cached_size = 0;
            cell->cache_valid = 0;
        }
    }
    return result;
}

char *lm_materialize_logic(LogicExpression *logic) {
    if (!logic) return NULL;

    size_t size = logic->materialized_size;
    if (size < 64U) size = 64U;

    for (int attempt = 0; attempt < 16; attempt++) {
        char *buffer = (char*)malloc(size + 1U);
        if (!buffer) return NULL;
        int result = materialize_expr(logic, buffer, size + 1U);
        if (result >= 0) return buffer;
        free(buffer);
        if (size > (SIZE_MAX / 2U)) break;
        size *= 2U;
    }
    return NULL;
}

int lm_emit_to_text(const LogicExpression *logic, char *out, size_t out_len) {
    return materialize_expr(logic, out, out_len);
}

size_t lm_predict_size(LogicalMemory *mem, const char *id) {
    LogicCell *cell = find_cell(mem, id);
    return (cell && cell->logic) ? cell->logic->materialized_size : 0U;
}

double lm_compute_complexity(LogicExpression *logic) {
    return logic ? logic->complexity : 0.0;
}

int lm_logic_to_string(LogicExpression *logic, char *output, size_t output_size) {
    if (!logic || !output || output_size == 0) return -1;

    switch (logic->type) {
        case LOGIC_CONSTANT:
            return snprintf(output, output_size, "const(\"%s\")",
                logic->data.constant.value ? logic->data.constant.value : "");
        case LOGIC_REPEAT:
            if (logic->data.repeat.pattern && logic->data.repeat.pattern->type == LOGIC_CONSTANT) {
                return snprintf(output, output_size, "repeat(\"%s\", %zu)",
                    logic->data.repeat.pattern->data.constant.value,
                    logic->data.repeat.count);
            }
            return snprintf(output, output_size, "repeat(<pattern>, %zu)",
                logic->data.repeat.count);
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
            return snprintf(output, output_size, logic->data.variable.binding
                ? "var(%s=<bound>)" : "var(%s)", logic->data.variable.name);
        case LOGIC_TRANSFORM:
            return snprintf(output, output_size, "transform(%s)",
                logic->data.transform.transform_fn ? "fn" : "identity");
        case LOGIC_CONDITIONAL:
            return snprintf(output, output_size, "if(<cond>, <then>, <else>)");
        case LOGIC_L5_SUPER:
            return snprintf(output, output_size, "l5_super(type=%u, payload=%u)",
                logic->data.l5_super.super_type,
                logic->data.l5_super.payload_hash);
        case LOGIC_DIGIT_STREAM:
            return snprintf(output, output_size, "digits(%zu)", logic->data.stream.length);
        case LOGIC_NOP:
            return snprintf(output, output_size, "nop()");
        default:
            return snprintf(output, output_size, "<unknown>");
    }
}

static size_t find_partial_repeat(const char *data, size_t len, char *pattern_out, size_t *count_out) {
    if (!data || len < 4U || !pattern_out || !count_out) return 0;

    for (size_t p_len = 1; p_len <= len / 2U && p_len < 256U; p_len++) {
        size_t count = 1;
        for (size_t i = p_len; i + p_len <= len; i += p_len) {
            if (strncmp(data, data + i, p_len) == 0) {
                count++;
            } else {
                break;
            }
        }
        if (count >= 2U && p_len * count >= 4U) {
            memcpy(pattern_out, data, p_len);
            pattern_out[p_len] = '\0';
            *count_out = count;
            return p_len * count;
        }
    }
    return 0;
}

static int find_sequence_pattern(const char *data, size_t len, int *start, int *step, size_t *count) {
    if (!data || len == 0 || !start || !step || !count) return 0;
    char *buf = (char*)malloc(len + 1U);
    if (!buf) return 0;
    memcpy(buf, data, len);
    buf[len] = '\0';

    int nums[64];
    size_t n_count = 0;
    char *token = strtok(buf, " ,");
    while (token && n_count < 64U) {
        char *endptr = NULL;
        long val = strtol(token, &endptr, 10);
        if (!endptr || *endptr != '\0') {
            free(buf);
            return 0;
        }
        nums[n_count++] = (int)val;
        token = strtok(NULL, " ,");
    }
    free(buf);
    if (n_count < 3U) return 0;

    int d = nums[1] - nums[0];
    for (size_t i = 2; i < n_count; i++) {
        if (nums[i] - nums[i - 1U] != d) return 0;
    }
    *start = nums[0];
    *step = d;
    *count = n_count;
    return 1;
}

static LogicExpression *lm_logic_from_string(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    int start = 0;
    int step = 0;
    size_t count = 0;
    if (find_sequence_pattern(data, len, &start, &step, &count)) {
        LogicExpression *seq = lm_logic_sequence(start, step, count);
        if (seq) seq->materialized_size = len;
        return seq;
    }

    char pattern[256];
    size_t repeat_count = 0;
    size_t consumed = find_partial_repeat(data, len, pattern, &repeat_count);
    if (consumed > 0) {
        LogicExpression *repeat = lm_logic_repeat(pattern, repeat_count);
        if (!repeat) return NULL;
        repeat->materialized_size = consumed;
        if (consumed == len) return repeat;

        LogicExpression *rest = lm_logic_from_string(data + consumed, len - consumed);
        if (!rest) return repeat;
        return lm_logic_compose(repeat, rest);
    }

    char *value = (char*)malloc(len + 1U);
    if (!value) return NULL;
    memcpy(value, data, len);
    value[len] = '\0';
    LogicExpression *constant = lm_logic_constant(value);
    free(value);
    return constant;
}

LogicExpression *lm_optimize_logic(LogicExpression *logic) {
    if (!logic) return logic;

    if (logic->type == LOGIC_CONSTANT && logic->data.constant.value) {
        LogicExpression *new_logic = lm_logic_from_string(
            logic->data.constant.value,
            logic->data.constant.length);
        if (new_logic && new_logic->type != LOGIC_CONSTANT) {
            lm_destroy_logic(logic);
            return new_logic;
        }
        lm_destroy_logic(new_logic);
        return logic;
    }

    if (logic->type == LOGIC_REPEAT &&
        logic->data.repeat.pattern &&
        logic->data.repeat.pattern->type == LOGIC_REPEAT) {
        LogicExpression *inner = logic->data.repeat.pattern;
        size_t combined = logic->data.repeat.count * inner->data.repeat.count;
        LogicExpression *base = inner->data.repeat.pattern;
        inner->data.repeat.pattern = NULL;
        logic->data.repeat.pattern = base;
        logic->data.repeat.count = combined;
        logic->materialized_size = base ? base->materialized_size * combined : 0U;
        logic->complexity *= 0.5;
        lm_destroy_logic(inner);
        return logic;
    }

    if (logic->type == LOGIC_COMPOSITION) {
        int all_constants = 1;
        size_t total = 0;
        for (size_t i = 0; i < logic->data.composition.count && i < 8U; i++) {
            LogicExpression *sub = logic->data.composition.expressions[i];
            if (!sub || sub->type != LOGIC_CONSTANT) {
                all_constants = 0;
                break;
            }
            total += sub->data.constant.length;
        }
        if (all_constants) {
            char *merged = (char*)malloc(total + 1U);
            if (!merged) return logic;
            size_t pos = 0;
            size_t old_count = logic->data.composition.count;
            for (size_t i = 0; i < old_count && i < 8U; i++) {
                LogicExpression *sub = logic->data.composition.expressions[i];
                memcpy(merged + pos, sub->data.constant.value, sub->data.constant.length);
                pos += sub->data.constant.length;
                lm_destroy_logic(sub);
                logic->data.composition.expressions[i] = NULL;
            }
            merged[pos] = '\0';
            memset(&logic->data, 0, sizeof(logic->data));
            logic->type = LOGIC_CONSTANT;
            logic->data.constant.value = merged;
            logic->data.constant.length = pos;
            logic->materialized_size = pos;
            logic->complexity = 0.1;
        }
    }

    return logic;
}

int lm_get_stats(LogicalMemory *mem, LogicalMemoryStats *stats) {
    if (!mem || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    stats->total_cells = mem->cell_count;
    stats->logic_size_bytes = mem->total_logic_size;
    stats->predicted_data_size = mem->total_materialized_size;
    stats->compression_ratio = mem->compression_ratio;
    for (size_t i = 0; i < mem->cell_count; i++) {
        if (mem->cells[i].cache_valid) stats->cached_cells++;
    }
    stats->cache_hit_rate = mem->cell_count > 0
        ? (double)stats->cached_cells / (double)mem->cell_count
        : 0.0;
    return 0;
}

LogicExpression *lm_create_logic(void) {
    return logic_alloc(LOGIC_NOP);
}

static void clear_expr_payload(LogicExpression *expr) {
    if (!expr) return;
    switch (expr->type) {
        case LOGIC_CONSTANT:
            free(expr->data.constant.value);
            break;
        case LOGIC_DIGIT_STREAM:
            free(expr->data.stream.digits);
            break;
        case LOGIC_REPEAT:
            lm_destroy_logic(expr->data.repeat.pattern);
            break;
        default:
            break;
    }
    memset(&expr->data, 0, sizeof(expr->data));
    expr->materialized_size = 0;
    expr->param_count = 0;
}

void lm_set_operation(LogicExpression *expr, int op) {
    if (!expr) return;
    clear_expr_payload(expr);
    switch (op) {
        case LOGIC_OP_CONSTANT:
            expr->type = LOGIC_CONSTANT;
            break;
        case LOGIC_OP_REPEAT:
            expr->type = LOGIC_REPEAT;
            break;
        case LOGIC_OP_SEQUENCE:
            expr->type = LOGIC_SEQUENCE;
            break;
        case LOGIC_OP_NOP:
        default:
            expr->type = LOGIC_NOP;
            break;
    }
}

void lm_add_param(LogicExpression *expr, const char *param) {
    if (!expr || !param) return;

    if (expr->type == LOGIC_CONSTANT) {
        free(expr->data.constant.value);
        expr->data.constant.value = safe_strdup(param);
        expr->data.constant.length = expr->data.constant.value ? strlen(param) : 0U;
        expr->materialized_size = expr->data.constant.length;
        expr->complexity = 0.1;
        expr->param_count++;
        return;
    }

    if (expr->type == LOGIC_REPEAT) {
        if (expr->param_count == 0) {
            expr->data.repeat.pattern = lm_logic_constant(param);
            expr->param_count++;
            return;
        }
        if (expr->param_count == 1) {
            size_t count = parse_size_value(param, 1U);
            expr->data.repeat.count = count;
            expr->materialized_size = expr->data.repeat.pattern
                ? expr->data.repeat.pattern->materialized_size * count
                : 0U;
            expr->complexity = 1.0;
            expr->param_count++;
            return;
        }
    }

    if (expr->type == LOGIC_SEQUENCE) {
        size_t value = parse_size_value(param, 0U);
        if (expr->param_count == 0) {
            expr->data.sequence.start = (int)value;
        } else if (expr->param_count == 1) {
            expr->data.sequence.step = (int)value;
        } else if (expr->param_count == 2) {
            expr->data.sequence.count = value == 0U ? 1U : value;
            expr->materialized_size = expr->data.sequence.count * 4U;
            expr->complexity = 1.0;
        }
        expr->param_count++;
    }
}

static void serialize_logic(FILE *f, const LogicExpression *logic) {
    uint32_t type = logic ? (uint32_t)logic->type : UINT32_MAX;
    fwrite(&type, sizeof(type), 1, f);
    if (!logic) return;
    fwrite(&logic->complexity, sizeof(logic->complexity), 1, f);
    fwrite(&logic->materialized_size, sizeof(logic->materialized_size), 1, f);

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
        case LOGIC_DIGIT_STREAM:
            fwrite(&logic->data.stream.length, sizeof(size_t), 1, f);
            fwrite(logic->data.stream.digits, 1, logic->data.stream.length, f);
            break;
        default:
            break;
    }
}

static LogicExpression *deserialize_logic(FILE *f) {
    uint32_t type = 0;
    if (fread(&type, sizeof(type), 1, f) != 1 || type == UINT32_MAX) return NULL;
    LogicExpression *logic = logic_alloc((LogicType)type);
    if (!logic) return NULL;
    fread(&logic->complexity, sizeof(logic->complexity), 1, f);
    fread(&logic->materialized_size, sizeof(logic->materialized_size), 1, f);

    switch (logic->type) {
        case LOGIC_CONSTANT:
            fread(&logic->data.constant.length, sizeof(size_t), 1, f);
            logic->data.constant.value = (char*)malloc(logic->data.constant.length + 1U);
            if (!logic->data.constant.value) {
                lm_destroy_logic(logic);
                return NULL;
            }
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
        case LOGIC_DIGIT_STREAM:
            fread(&logic->data.stream.length, sizeof(size_t), 1, f);
            logic->data.stream.digits = (uint8_t*)malloc(logic->data.stream.length);
            if (logic->data.stream.length > 0 && !logic->data.stream.digits) {
                lm_destroy_logic(logic);
                return NULL;
            }
            fread(logic->data.stream.digits, 1, logic->data.stream.length, f);
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
    uint32_t magic = 0x4B4C4231U;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&mem->cell_count, sizeof(size_t), 1, f);
    for (size_t i = 0; i < mem->cell_count; i++) {
        fwrite(mem->cells[i].id, 1, sizeof(mem->cells[i].id), f);
        fwrite(mem->cells[i].hash, 1, sizeof(mem->cells[i].hash), f);
        serialize_logic(f, mem->cells[i].logic);
    }
    fclose(f);
    return 0;
}

LogicalMemory *lm_load(const char *filename) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x4B4C4231U) {
        fclose(f);
        return NULL;
    }
    LogicalMemory *mem = lm_create_memory();
    if (!mem) {
        fclose(f);
        return NULL;
    }
    size_t count = 0;
    if (fread(&count, sizeof(size_t), 1, f) != 1 || count > mem->cell_capacity) {
        lm_destroy_memory(mem);
        fclose(f);
        return NULL;
    }
    mem->cell_count = count;
    for (size_t i = 0; i < count; i++) {
        fread(mem->cells[i].id, 1, sizeof(mem->cells[i].id), f);
        fread(mem->cells[i].hash, 1, sizeof(mem->cells[i].hash), f);
        mem->cells[i].logic = deserialize_logic(f);
    }
    fclose(f);
    return mem;
}

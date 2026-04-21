/*
 * logical_memory.h
 *
 * Логическая память без данных (Logic-Centric Memory).
 *
 * Инвариант цифрового ядра:
 *   D-слой хранит обратимые цифры,
 *   F-слой хранит формулы,
 *   MF-слой генерирует формулы/логику,
 *   материализация всегда должна восстанавливать исходный поток.
 */

#ifndef KOLIBRI_LOGICAL_MEMORY_H
#define KOLIBRI_LOGICAL_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 256K ячеек для больших массивов знаний и файлов. */
#define LM_MAX_CELLS 262144U

#define LOGIC_OP_CONSTANT 0
#define LOGIC_OP_REPEAT   1
#define LOGIC_OP_SEQUENCE 2
#define LOGIC_OP_NOP      3

typedef enum {
    LOGIC_CONSTANT,      /* Константа: "ABC", "065" */
    LOGIC_VARIABLE,      /* Переменная: x, y, input */
    LOGIC_REPEAT,        /* repeat(pattern, N) */
    LOGIC_SEQUENCE,      /* sequence(start, step, count) */
    LOGIC_TRANSFORM,     /* transform(input, func) */
    LOGIC_CONDITIONAL,   /* if(cond, then, else) */
    LOGIC_COMPOSITION,   /* compose(f1, f2, ...) */
    LOGIC_RELATION,      /* relation(A, B, type) */
    LOGIC_L5_SUPER,      /* L5 Generative: компактная super formula */
    LOGIC_DIGIT_STREAM,  /* Canonical D-layer: uint8_t digits 0..9 */
    LOGIC_NOP
} LogicType;

typedef struct LogicExpression {
    LogicType type;

    union {
        struct {
            char *value;
            size_t length;
        } constant;

        struct {
            char name[32];
            struct LogicExpression *binding;
        } variable;

        struct {
            struct LogicExpression *pattern;
            size_t count;
        } repeat;

        struct {
            int start;
            int step;
            size_t count;
        } sequence;

        struct {
            struct LogicExpression *input;
            int (*transform_fn)(const void*, void*);
        } transform;

        struct {
            struct LogicExpression *condition;
            struct LogicExpression *then_expr;
            struct LogicExpression *else_expr;
        } conditional;

        struct {
            struct LogicExpression *expressions[8];
            size_t count;
        } composition;

        struct {
            struct LogicExpression *left;
            struct LogicExpression *right;
            char relation_type[16];
        } relation;

        struct {
            uint8_t super_type;
            uint32_t payload_hash;
            uint8_t checksum;
        } l5_super;

        struct {
            uint8_t *digits;
            size_t length;
        } stream;
    } data;

    uint64_t creation_time;
    double complexity;
    size_t materialized_size;

    /* Совместимость с геновыми конструкторами lm_set_operation/lm_add_param. */
    int param_count;
} LogicExpression;

typedef struct {
    char id[64];
    char hash[65];
    LogicExpression *logic;

    void *cached_data;
    size_t cached_size;
    uint64_t cache_timestamp;
    int cache_valid;

    char dependencies[16][64];
    size_t dependency_count;
} LogicCell;

typedef struct {
    LogicCell *cells;
    size_t cell_count;
    size_t cell_capacity;

    size_t total_logic_size;
    size_t total_materialized_size;
    double compression_ratio;

    int (*query_fn)(const char*, LogicCell**);
} LogicalMemory;

LogicalMemory* lm_create_memory(void);
void lm_destroy_memory(LogicalMemory *mem);

LogicExpression* lm_logic_constant(const char *value);
LogicExpression* lm_logic_repeat(const char *pattern, size_t count);
LogicExpression* lm_logic_sequence(int start, int step, size_t count);
LogicExpression* lm_logic_compose(LogicExpression *expr1, LogicExpression *expr2);
LogicExpression* lm_logic_relation(LogicExpression *left, LogicExpression *right, const char *type);
LogicExpression* lm_logic_variable(const char *name);
int lm_logic_bind_variable(LogicExpression *variable, LogicExpression *binding);
LogicExpression* lm_logic_transform(LogicExpression *input, int (*fn)(const void*, void*));
LogicExpression* lm_logic_conditional(
    LogicExpression *condition,
    LogicExpression *then_expr,
    LogicExpression *else_expr
);
LogicExpression* lm_logic_l5_super(uint8_t type, uint32_t payload);

double lm_compute_complexity(LogicExpression *logic);
LogicExpression* lm_optimize_logic(LogicExpression *logic);
int lm_logic_to_string(LogicExpression *logic, char *output, size_t output_size);
int lm_materialize(LogicalMemory *mem, const char *id, void *output, size_t output_size);
char* lm_materialize_logic(LogicExpression *logic);
size_t lm_predict_size(LogicalMemory *mem, const char *id);
void lm_destroy_logic(LogicExpression *logic);
int lm_store_logic(LogicalMemory *mem, const char *id, LogicExpression *logic);

/* D-layer helpers used by newer high-volume knowledge ingestion tests. */
LogicExpression* lm_logic_stream(const uint8_t *digits, size_t len);
int lm_emit_to_text(const LogicExpression *logic, char *out, size_t out_len);

/* Compatibility constructors for 32-digit gene execution. */
LogicExpression* lm_create_logic(void);
void lm_set_operation(LogicExpression *expr, int op);
void lm_add_param(LogicExpression *expr, const char *param);

typedef struct {
    size_t total_cells;
    size_t logic_size_bytes;
    size_t predicted_data_size;
    double compression_ratio;
    size_t cached_cells;
    double cache_hit_rate;
} LogicalMemoryStats;

int lm_get_stats(LogicalMemory *mem, LogicalMemoryStats *stats);

int lm_save(LogicalMemory *mem, const char *filename);
LogicalMemory* lm_load(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_LOGICAL_MEMORY_H */

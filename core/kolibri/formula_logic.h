/*
 * formula_logic.h
 *
 * Мета-формулы: цифровые правила, которые создают логические формулы.
 */

#ifndef KOLIBRI_FORMULA_LOGIC_H
#define KOLIBRI_FORMULA_LOGIC_H

#include "kolibri/logical_memory.h"
#include "kolibri/formula.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    META_GENERATE_CONSTANT,
    META_GENERATE_REPEAT,
    META_GENERATE_SEQUENCE,
    META_GENERATE_COMPOSE,
    META_TRANSFORM_LOGIC,
    META_DERIVE_RELATION,
    META_EVOLVE_PATTERN,
    META_COMPRESS_LOGIC
} MetaOperation;

typedef struct {
    MetaOperation operation;

    union {
        struct {
            char *value;
        } generate_constant;

        struct {
            char pattern_formula[64];
            char count_formula[64];
        } gen_repeat;

        struct {
            char start_formula[64];
            char step_formula[64];
            char count_formula[64];
        } gen_sequence;

        struct {
            char input_logic_id[64];
            char transform_rule[128];
        } transform;

        struct {
            char left_logic_id[64];
            char right_logic_id[64];
            char inference_rule[128];
        } derive;

        struct {
            char source_pattern_id[64];
            double mutation_rate;
            int generations;
        } evolve;

        struct {
            char target_logic_id[64];
            char compression_strategy[64];
        } compress;
    } params;

    uint64_t generation;
    double complexity_score;
    size_t output_size_estimate;
} MetaFormula;

typedef struct {
    MetaFormula formulas[256];
    size_t count;

    LogicExpression *generated_cache[256];
    char cache_ids[256][64];
    size_t cache_count;
} MetaFormulaStore;

MetaFormulaStore* mf_create_store(void);
void mf_destroy_store(MetaFormulaStore *store);

MetaFormula* mf_create_meta_formula(void);
MetaFormula* mf_create_constant_generator(const char *value);
MetaFormula* mf_create_repeat_generator(const char *pattern_formula, const char *count_formula);
MetaFormula* mf_create_sequence_generator(
    const char *start_formula,
    const char *step_formula,
    const char *count_formula
);
MetaFormula* mf_create_transformer(const char *input_logic_id, const char *transform_rule);
MetaFormula* mf_create_relation_deriver(
    const char *left_id,
    const char *right_id,
    const char *inference_rule
);
MetaFormula* mf_create_pattern_evolver(
    const char *source_pattern_id,
    double mutation_rate,
    int generations
);
MetaFormula* mf_create_logic_compressor(
    const char *target_logic_id,
    const char *compression_strategy
);

/* Декодирование 32-значного гена MF-слоя. */
MetaFormula* mf_from_gene(const uint8_t digits[32]);

LogicExpression* mf_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *target_memory
);

int mf_store_meta(MetaFormulaStore *store, const MetaFormula *meta, const char *id);
MetaFormula* mf_load_meta(MetaFormulaStore *store, const char *id);
MetaFormula* mf_optimize_meta(const MetaFormula *meta);
MetaFormula* mf_evolve_meta(const MetaFormula *meta, double mutation_rate);
MetaFormula* mf_compose_meta(const MetaFormula *meta1, const MetaFormula *meta2);

typedef struct {
    size_t total_meta_formulas;
    size_t generated_logic_count;
    size_t meta_size_bytes;
    size_t logic_size_bytes;
    double meta_to_logic_ratio;
} MetaFormulaStats;

int mf_get_stats(MetaFormulaStore *store, MetaFormulaStats *stats);
int mf_to_string(const MetaFormula *meta, char *output, size_t output_size);

int mf_auto_discover_patterns(LogicalMemory *memory, MetaFormulaStore *store);
int mf_batch_execute(
    MetaFormulaStore *store,
    const MetaFormula *meta,
    LogicalMemory *memory,
    const char **cell_ids,
    size_t cell_count
);
MetaFormula* mf_infer_meta(
    MetaFormulaStore *store,
    const char *rule,
    const MetaFormula **input_metas,
    size_t input_count
);

void mf_destroy_meta_formula(MetaFormula *meta);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_FORMULA_LOGIC_H */

#ifndef KOLIBRI_KERNEL_FORMULA_H
#define KOLIBRI_KERNEL_FORMULA_H

#include "kolibri/random.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t digits[4000];
    size_t length;
} KolibriGene;

/* --- Evolution Types (Synced with backend) --- */

typedef enum {
    KOLIBRI_MUTATION_POINT = 0,
    KOLIBRI_MUTATION_SWAP,
    KOLIBRI_MUTATION_INVERT,
    KOLIBRI_MUTATION_SCRAMBLE,
    KOLIBRI_MUTATION_SHIFT,
    KOLIBRI_MUTATION_COUNT
} KolibriMutationType;

typedef enum {
    KOLIBRI_CROSSOVER_SINGLE_POINT = 0,
    KOLIBRI_CROSSOVER_TWO_POINT,
    KOLIBRI_CROSSOVER_UNIFORM,
    KOLIBRI_CROSSOVER_COUNT
} KolibriCrossoverType;

typedef struct {
    double mutation_rate;
    double mutation_strength;
    KolibriMutationType mutation_type;
    double crossover_rate;
    KolibriCrossoverType crossover_type;
    double elite_ratio;
    double tournament_size;
    uint64_t generations_per_tick;
    int adaptive_mutation;
} KolibriEvolutionConfig;

typedef struct {
    uint64_t total_generations;
    uint64_t total_mutations;
    uint64_t beneficial_mutations;
    uint64_t neutral_mutations;
    uint64_t harmful_mutations;
    double evolution_speed;
    double mutation_energy;
    double best_fitness;
    double avg_fitness;
    double fitness_variance;
    uint64_t stagnation_count;
} KolibriEvolutionMetrics;

typedef struct {
    int input_hash;
    int output_hash;
    char question[256];
    char answer[512];
} KolibriAssociation;

typedef struct {
    KolibriGene gene;
    double fitness;
    double feedback;
    /* Simplified association storage for kernel */
    KolibriAssociation associations[16]; 
    size_t association_count;
} KolibriFormula;

typedef struct {
    KolibriFormula formulas[16];
    size_t count;
    KolibriRng rng;
    int inputs[256];
    int targets[256];
    size_t examples;
    
    /* Extended fields for backend compatibility */
    KolibriEvolutionConfig config;
    KolibriEvolutionMetrics metrics;
    double prev_best_fitness;
} KolibriFormulaPool;

/* --- API --- */
void kf_pool_init(KolibriFormulaPool *pool, uint64_t seed);
void kf_pool_free(KolibriFormulaPool *pool);
void kf_pool_clear_examples(KolibriFormulaPool *pool);
int kf_pool_add_example(KolibriFormulaPool *pool, int input, int target);
void kf_pool_tick(KolibriFormulaPool *pool, size_t generations);
const KolibriFormula *kf_pool_best(const KolibriFormulaPool *pool);
int kf_formula_apply(const KolibriFormula *formula, int input, int *output);
size_t kf_formula_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len);
int kf_formula_describe(const KolibriFormula *formula, char *buffer, size_t buffer_len);
int kf_pool_feedback(KolibriFormulaPool *pool, const KolibriGene *gene, double delta);

/* --- Reactor & Config API (Stubs/Implementations in formula.c) --- */
void kf_config_default(KolibriEvolutionConfig *config);
int kf_pool_set_config(KolibriFormulaPool *pool, const KolibriEvolutionConfig *config);
int kf_pool_get_config(const KolibriFormulaPool *pool, KolibriEvolutionConfig *config);
int kf_pool_get_metrics(const KolibriFormulaPool *pool, KolibriEvolutionMetrics *metrics);
void kf_pool_reset_metrics(KolibriFormulaPool *pool);
int kf_reactor_run(KolibriFormulaPool *pool, size_t max_generations, double target_fitness);
int kf_metrics_to_digits(const KolibriEvolutionMetrics *metrics, char *buffer, size_t buffer_len);
int kf_hash_from_text(const char *text);
int kf_formula_lookup_answer(const KolibriFormula *formula, int input, char *buffer, size_t buffer_len);
int kf_pool_add_association(KolibriFormulaPool *pool, int input_hash, int output_hash, const char *question, const char *answer);
int kf_pool_ensure_association_capacity(KolibriFormulaPool *pool, size_t count);

#endif /* KOLIBRI_KERNEL_FORMULA_H */

/*
 * AI Evolution Engine Header
 * 
 * Combines formula evolution, genome logging, and optimized encoding
 * for high-performance AI evolution with swarm distribution
 */

#ifndef KOLIBRI_AI_EVOLUTION_H
#define KOLIBRI_AI_EVOLUTION_H

#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/ai_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Evolution engine state
 */
typedef struct {
    KolibriFormulaPool pool;
    KolibriGenome genome;
    
    /* Statistics */
    uint32_t generation;
    double best_fitness;
    uint64_t total_mutations;
    uint64_t total_encodings;
} KAIEvolutionEngine;

/*
 * Evolution statistics
 */
typedef struct {
    uint32_t generation;
    double best_fitness;
    uint64_t total_mutations;
    uint64_t total_encodings;
    size_t population_size;
    KAIEncoderStats encoder_stats;
} KAIEvolutionStats;

/*
 * Initialize evolution engine
 * 
 * @param engine Engine to initialize
 * @param seed RNG seed for formula generation
 * @param genome_path Path to genome file
 * @param genome_key HMAC key for genome integrity
 * @param key_len Length of HMAC key
 * @return 0 on success, -1 on error
 */
int kae_init(KAIEvolutionEngine *engine, uint64_t seed,
             const char *genome_path, const unsigned char *genome_key, size_t key_len);

/*
 * Add training example
 * 
 * @param engine Evolution engine
 * @param input Input value
 * @param target Target output
 * @return 0 on success, -1 on error
 */
int kae_add_example(KAIEvolutionEngine *engine, int input, int target);

/*
 * Evolve one generation
 * 
 * Evaluates all formulas, selects best, mutates, and logs to genome
 * Uses optimized encoding (2.77e10 chars/sec throughput)
 * 
 * @param engine Evolution engine
 * @return 0 on success, -1 on error
 */
int kae_evolve_generation(KAIEvolutionEngine *engine);

/*
 * Get best formula from current generation
 * 
 * @param engine Evolution engine
 * @param out_formula Output buffer for best formula
 * @return 0 on success, -1 on error
 */
int kae_get_best_formula(KAIEvolutionEngine *engine, KolibriFormula *out_formula);

/*
 * Export population for swarm distribution
 * 
 * Uses batch encoding for maximum throughput
 * 
 * @param engine Evolution engine
 * @param output Output buffer for encoded population
 * @param output_size Size of output buffer
 * @param bytes_written Number of bytes written
 * @return 0 on success, -1 on error
 */
int kae_export_population(KAIEvolutionEngine *engine,
                          unsigned char *output, size_t output_size,
                          size_t *bytes_written);

/*
 * Import population from swarm network
 * 
 * @param engine Evolution engine
 * @param input Input buffer with encoded population
 * @param input_size Size of input buffer
 * @return Number of genes imported, or -1 on error
 */
int kae_import_population(KAIEvolutionEngine *engine,
                          const unsigned char *input, size_t input_size);

/*
 * Get evolution statistics
 * 
 * @param engine Evolution engine
 * @param stats Output buffer for statistics
 */
void kae_get_stats(const KAIEvolutionEngine *engine, KAIEvolutionStats *stats);

/*
 * Shutdown evolution engine
 * 
 * Logs shutdown event and closes genome
 * 
 * @param engine Evolution engine
 */
void kae_shutdown(KAIEvolutionEngine *engine);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AI_EVOLUTION_H */

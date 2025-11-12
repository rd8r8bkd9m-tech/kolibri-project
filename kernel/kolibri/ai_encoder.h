/*
 * AI-Optimized Decimal Encoder Header
 * 
 * High-performance encoding for AI genome and formula systems
 * Based on research findings: simple division is fastest (2.77e10 chars/sec)
 */

#ifndef KOLIBRI_AI_ENCODER_H
#define KOLIBRI_AI_ENCODER_H

#include "kolibri/formula.h"
#include "kolibri/genome.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Performance statistics from DECIMAL_10X research
 */
typedef struct {
    double throughput_chars_per_sec;
    double improvement_factor;
    const char *approach;
    const char *cpu_architecture;
    const char *compiler_flags;
} KAIEncoderStats;

/*
 * Encode gene to optimized decimal representation
 * Returns: bytes written, or -1 on error
 */
int kai_encode_gene(const KolibriGene *gene, unsigned char *output, size_t output_size);

/*
 * Encode genome block to optimized decimal representation
 * Returns: bytes written, or -1 on error
 */
int kai_encode_genome_block(const ReasonBlock *block, unsigned char *output, size_t output_size);

/*
 * Decode decimal representation back to gene
 * Returns: 0 on success, -1 on error
 */
int kai_decode_gene(const unsigned char *input, size_t input_size, KolibriGene *gene);

/*
 * Evaluate formula fitness with optimized encoding
 * Optionally outputs encoded gene for network transmission
 */
double kai_evaluate_with_encoding(const KolibriFormula *formula, 
                                   const KolibriFormulaPool *pool,
                                   unsigned char *encoded_out,
                                   size_t encoded_size);

/*
 * Batch encode multiple genes for swarm distribution
 * Optimized for high-throughput evolutionary populations
 */
int kai_batch_encode_genes(const KolibriGene *genes, size_t gene_count,
                            unsigned char *output, size_t output_size,
                            size_t *bytes_written);

/*
 * Get performance statistics from encoder
 */
void kai_get_performance_stats(KAIEncoderStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AI_ENCODER_H */

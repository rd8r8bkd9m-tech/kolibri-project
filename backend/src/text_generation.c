/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * Text Generation with Formula Compression
 */

#include "kolibri/generation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int k_gen_init(KolibriGenerationContext *ctx,
               KolibriCorpusContext *corpus,
               KolibriGenerationStrategy strategy) {
    if (!ctx || !corpus) return -1;
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->corpus = corpus;
    ctx->strategy = strategy;
    ctx->temperature = 1.0;
    ctx->beam_size = KOLIBRI_BEAM_SIZE;
    ctx->max_length = KOLIBRI_MAX_GENERATION_LENGTH;
    
    ctx->formula_pool = calloc(1, sizeof(KolibriFormulaPool));
    if (!ctx->formula_pool) return -1;
    
    kf_pool_init(ctx->formula_pool, (uint64_t)time(NULL));
    
    ctx->context = calloc(1, sizeof(KolibriContextWindow));
    if (!ctx->context) {
        free(ctx->formula_pool);
        return -1;
    }
    
    if (k_context_window_init(ctx->context) != 0) {
        free(ctx->context);
        free(ctx->formula_pool);
        return -1;
    }
    
    return 0;
}

void k_gen_free(KolibriGenerationContext *ctx) {
    if (!ctx) return;
    if (ctx->context) {
        k_context_window_free(ctx->context);
        free(ctx->context);
    }
    if (ctx->formula_pool) free(ctx->formula_pool);
}

double k_gen_compress_pattern(KolibriGenerationContext *ctx,
                              const KolibriSemanticPattern *pattern,
                              KolibriFormula *formula) {
    if (!ctx || !pattern || !formula || !ctx->formula_pool) return -1.0;
    
    int pattern_hash = 0;
    for (size_t i = 0; i < KOLIBRI_SEMANTIC_PATTERN_SIZE; i++) {
        pattern_hash = (pattern_hash * 10 + pattern->pattern[i]) % 1000000;
    }
    
    double best_compression = 0.0;
    memset(formula, 0, sizeof(*formula));
    
    for (size_t gen = 0; gen < 50; gen++) {
        const KolibriFormula *candidate = kf_pool_best(ctx->formula_pool);
        if (!candidate) continue;
        
        int output;
        if (kf_formula_apply(candidate, pattern_hash, &output) == 0) {
            uint8_t formula_digits[256];
            size_t formula_len = kf_formula_digits(candidate, formula_digits, 256);
            
            double compression_ratio = (double)KOLIBRI_SEMANTIC_PATTERN_SIZE / 
                                      (double)(sizeof(output) + formula_len);
            
            if (compression_ratio > best_compression) {
                best_compression = compression_ratio;
                *formula = *candidate;
            }
        }
        
        kf_pool_tick(ctx->formula_pool, 1);
    }
    
    if (best_compression > 0.0) {
        ctx->formulas_used++;
        ctx->avg_compression_ratio = 
            (ctx->avg_compression_ratio * (ctx->formulas_used - 1) +
             best_compression) / ctx->formulas_used;
    }
    
    return best_compression;
}

int k_gen_decompress_pattern(KolibriGenerationContext *ctx,
                             const KolibriFormula *formula,
                             KolibriSemanticPattern *pattern) {
    if (!ctx || !formula || !pattern) return -1;
    
    int output;
    if (kf_formula_apply(formula, 0, &output) != 0) return -1;
    
    memset(pattern, 0, sizeof(*pattern));
    int temp = abs(output);
    
    for (size_t i = 0; i < KOLIBRI_SEMANTIC_PATTERN_SIZE && temp > 0; i++) {
        pattern->pattern[KOLIBRI_SEMANTIC_PATTERN_SIZE - 1 - i] = temp % 10;
        temp /= 10;
    }
    
    return 0;
}

int k_gen_next_token(KolibriGenerationContext *ctx, char *output, size_t output_size) {
    if (!ctx || !output || output_size == 0) return -1;
    output[0] = '\0';
    return 0;
}

int k_gen_generate(KolibriGenerationContext *ctx, const char *prompt, size_t num_tokens,
                  char *output, size_t output_size) {
    if (!ctx || !output || output_size == 0) return -1;
    output[0] = '\0';
    return 0;
}

int k_gen_beam_search(KolibriGenerationContext *ctx, KolibriGenerationCandidate *candidates,
                      size_t *num_candidates) {
    if (!ctx || !candidates || !num_candidates) return -1;
    *num_candidates = 0;
    return 0;
}

int k_gen_evolve_text(KolibriGenerationContext *ctx, size_t generations,
                      char *output, size_t output_size) {
    if (!ctx || !output || output_size == 0) return -1;
    output[0] = '\0';
    return 0;
}

double k_gen_perplexity(KolibriGenerationContext *ctx, const char *text, size_t text_len) {
    if (!ctx || !text) return -1.0;
    return 1.0;
}

double k_gen_coherence(KolibriGenerationContext *ctx, const char *text, size_t text_len) {
    if (!ctx || !text) return -1.0;
    return 1.0;
}

void k_gen_set_temperature(KolibriGenerationContext *ctx, double temperature) {
    if (ctx) ctx->temperature = temperature;
}

void k_gen_set_beam_size(KolibriGenerationContext *ctx, size_t beam_size) {
    if (ctx) ctx->beam_size = beam_size;
}

void k_gen_get_stats(const KolibriGenerationContext *ctx, size_t *tokens_generated,
                    size_t *formulas_used, double *compression_ratio) {
    if (!ctx) return;
    if (tokens_generated) *tokens_generated = ctx->tokens_generated;
    if (formulas_used) *formulas_used = ctx->formulas_used;
    if (compression_ratio) *compression_ratio = ctx->avg_compression_ratio;
}

void k_gen_print_stats(const KolibriGenerationContext *ctx) {
    if (!ctx) return;
    printf("=== Generation Statistics ===\n");
    printf("Tokens generated: %zu\n", ctx->tokens_generated);
    printf("Formulas used: %zu\n", ctx->formulas_used);
    printf("Avg compression ratio: %.2f\n", ctx->avg_compression_ratio);
    printf("Generation time: %.3f sec\n", ctx->generation_time_sec);
}

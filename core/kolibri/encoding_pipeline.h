/*
 * Kolibri Unified Encoding Pipeline — v2.0
 *
 * Ties together: digits (byte-level), phoneme (sound-level), semantic (meaning-level)
 * into a single encoding API for text.
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_ENCODING_PIPELINE_H
#define KOLIBRI_ENCODING_PIPELINE_H

#include "kolibri/digits.h"
#include "kolibri/phoneme.h"
#include "kolibri/semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * UNIFIED ENCODING RESULT
 * ============================================================================ */

/**
 * Complete encoding result for a single word/token.
 * Contains all three representation levels.
 */
typedef struct {
    char word[256];                        /* Original word text */
    kolibri_potok_cifr digit_stream;       /* Byte-level: 3 digits per byte (S-D-E) */
    KolibriPhoneticSignature phonemes;     /* Sound-level: phoneme codes */
    KolibriSemanticPattern semantic;       /* Meaning-level: 64-digit pattern */
    double confidence;                     /* Encoding confidence (0.0-1.0) */
    int is_latin;                          /* 1 if Latin script detected */
    int is_cyrillic;                       /* 1 if Cyrillic script detected */
} KolibriEncodingResult;

/**
 * Pipeline configuration
 */
typedef struct {
    int enable_digits;                     /* Enable byte-level digit encoding */
    int enable_phonemes;                   /* Enable phoneme encoding */
    int enable_semantic;                   /* Enable semantic pattern encoding */
    int semantic_learn;                    /* Auto-learn semantic patterns for new words */
    int semantic_generations;              /* Evolution generations for semantic learning (default: 50) */
} KolibriEncodingConfig;

/**
 * Pipeline state — holds learned semantic patterns
 */
typedef struct {
    KolibriEncodingConfig config;
    KolibriSemanticPattern *patterns;      /* Learned word patterns */
    size_t pattern_count;                  /* Number of learned patterns */
    size_t pattern_capacity;               /* Allocated capacity */
} KolibriEncodingPipeline;

/* ============================================================================
 * PIPELINE API
 * ============================================================================ */

/**
 * Create and initialize encoding pipeline
 *
 * @param pipeline Output pipeline pointer
 * @param config Configuration (pass NULL for defaults)
 * @return 0 on success, -1 on failure
 */
int kolibri_pipeline_create(KolibriEncodingPipeline **pipeline,
                            const KolibriEncodingConfig *config);

/**
 * Destroy pipeline and free all learned patterns
 */
void kolibri_pipeline_destroy(KolibriEncodingPipeline *pipeline);

/**
 * Encode a single word through all three levels
 *
 * @param pipeline Pipeline state
 * @param word Word to encode
 * @param result Output encoding result
 * @return 0 on success, -1 on failure
 */
int kolibri_pipeline_encode_word(KolibriEncodingPipeline *pipeline,
                                 const char *word,
                                 KolibriEncodingResult *result);

/**
 * Encode a full text (splits into words, encodes each)
 *
 * @param pipeline Pipeline state
 * @param text Text to encode
 * @param results Output array (caller must free each result's digit_stream)
 * @param max_results Maximum number of results
 * @param out_count Actual number of encoded words
 * @return 0 on success, -1 on failure
 */
int kolibri_pipeline_encode_text(KolibriEncodingPipeline *pipeline,
                                 const char *text,
                                 KolibriEncodingResult *results,
                                 size_t max_results,
                                 size_t *out_count);

/**
 * Find semantically similar words from learned patterns
 *
 * @param pipeline Pipeline state
 * @param query_word Query word
 * @param indices Output array of pattern indices (sorted by similarity)
 * @param scores Output array of similarity scores
 * @param max_results Maximum results
 * @param out_count Actual result count
 * @return 0 on success, -1 on failure
 */
int kolibri_pipeline_similar_words(KolibriEncodingPipeline *pipeline,
                                   const char *query_word,
                                   size_t *indices,
                                   double *scores,
                                   size_t max_results,
                                   size_t *out_count);

/**
 * Compute semantic similarity between two words
 */
double kolibri_pipeline_word_similarity(KolibriEncodingPipeline *pipeline,
                                        const char *word1,
                                        const char *word2);

/**
 * Get statistics about learned patterns
 */
typedef struct {
    size_t total_patterns;
    size_t cyrillic_count;
    size_t latin_count;
    size_t avg_usage_count;
    double avg_confidence;
} KolibriPipelineStats;

void kolibri_pipeline_get_stats(KolibriEncodingPipeline *pipeline,
                                KolibriPipelineStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ENCODING_PIPELINE_H */

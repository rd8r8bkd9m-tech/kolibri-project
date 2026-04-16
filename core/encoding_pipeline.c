/*
 * Kolibri Unified Encoding Pipeline — Implementation
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#include "kolibri/encoding_pipeline.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static int is_cyrillic_char(uint32_t c) {
    return (c >= 0x0400 && c <= 0x04FF) || /* Basic Cyrillic */
           (c >= 0x0500 && c <= 0x052F);   /* Extended */
}

static int is_latin_char(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static int is_word_char(uint32_t c) { return is_cyrillic_char(c) || is_latin_char(c) || c == '-' || c == '\''; }

static uint32_t utf8_decode(const char *s, int *out_len) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *out_len = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        *out_len = 2;
        return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        *out_len = 3;
        return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        *out_len = 4;
        return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *out_len = 1;
    return c;
}

static KolibriEncodingConfig default_config(void) {
    KolibriEncodingConfig cfg;
    cfg.enable_digits = 1;
    cfg.enable_phonemes = 1;
    cfg.enable_semantic = 1;
    cfg.semantic_learn = 1;
    cfg.semantic_generations = 50;
    return cfg;
}

/* ============================================================================
 * PIPELINE CREATE/DESTROY
 * ============================================================================ */

int kolibri_pipeline_create(KolibriEncodingPipeline **pipeline, const KolibriEncodingConfig *config) {
    KolibriEncodingPipeline *p = calloc(1, sizeof(*p));
    if (!p)
        return -1;

    p->config = config ? *config : default_config();
    p->pattern_capacity = 1024;
    p->patterns = calloc(p->pattern_capacity, sizeof(KolibriSemanticPattern));
    if (!p->patterns) {
        free(p);
        return -1;
    }
    p->pattern_count = 0;

    *pipeline = p;
    return 0;
}

void kolibri_pipeline_destroy(KolibriEncodingPipeline *pipeline) {
    if (!pipeline)
        return;
    for (size_t i = 0; i < pipeline->pattern_count; i++) {
        /* digit_stream inside semantic pattern is self-contained */
    }
    free(pipeline->patterns);
    free(pipeline);
}

/* ============================================================================
 * PATTERN LOOKUP
 * ============================================================================ */

static int find_pattern(KolibriEncodingPipeline *p, const char *word) {
    for (size_t i = 0; i < p->pattern_count; i++) {
        if (strcmp(p->patterns[i].word, word) == 0)
            return (int)i;
    }
    return -1;
}

static int add_pattern(KolibriEncodingPipeline *p, KolibriSemanticPattern *pat) {
    if (p->pattern_count >= p->pattern_capacity) {
        size_t new_cap = p->pattern_capacity * 2;
        KolibriSemanticPattern *new_p = realloc(p->patterns, new_cap * sizeof(*new_p));
        if (!new_p)
            return -1;
        p->patterns = new_p;
        p->pattern_capacity = new_cap;
    }
    p->patterns[p->pattern_count] = *pat;
    return (int)(p->pattern_count++);
}

/* ============================================================================
 * ENCODE SINGLE WORD
 * ============================================================================ */

int kolibri_pipeline_encode_word(KolibriEncodingPipeline *pipeline, const char *word, KolibriEncodingResult *result) {
    if (!pipeline || !word || !result)
        return -1;

    memset(result, 0, sizeof(*result));
    strncpy(result->word, word, sizeof(result->word) - 1);

    /* Detect script */
    int len = (int)strlen(word);
    for (int i = 0; i < len;) {
        int char_len;
        uint32_t c = utf8_decode(word + i, &char_len);
        if (is_cyrillic_char(c))
            result->is_cyrillic = 1;
        if (is_latin_char(c))
            result->is_latin = 1;
        i += char_len;
    }

    /* Level 1: Byte-level digit encoding */
    if (pipeline->config.enable_digits) {
        uint8_t *digit_buf = malloc(4096);
        if (!digit_buf)
            return -1;
        kolibri_potok_cifr_init(&result->digit_stream, digit_buf, 4096);
        kolibri_transducirovat_utf8(&result->digit_stream, (const uint8_t *)word, (size_t)len);
    }

    /* Level 2: Phoneme encoding */
    if (pipeline->config.enable_phonemes) {
        if (result->is_cyrillic) {
            k_phoneme_encode(word, &result->phonemes);
        }
        /* Latin phoneme encoding would go here — currently unsupported */
    }

    /* Level 3: Semantic encoding */
    if (pipeline->config.enable_semantic) {
        int existing = find_pattern(pipeline, word);
        if (existing >= 0) {
            /* Use learned pattern */
            result->semantic = pipeline->patterns[existing];
            result->confidence = 0.9;
        } else if (pipeline->config.semantic_learn) {
            /* Learn new semantic pattern */
            KolibriSemanticContext ctx;
            if (k_semantic_context_init(&ctx) == 0) {
                uint8_t *ctx_digit_buf = malloc(4096);
                if (!ctx_digit_buf) {
                    kolibri_potok_cifr_sbros(&result->digit_stream);
                    free(result->digit_stream.danniye);
                    k_semantic_context_free(&ctx);
                    return -1;
                }
                kolibri_potok_cifr_init(&ctx.word_digits, ctx_digit_buf, 4096);
                kolibri_transducirovat_utf8(&ctx.word_digits, (const uint8_t *)word, (size_t)len);
                ctx.context_count = 0;

                KolibriSemanticPattern pat;
                k_semantic_pattern_init(&pat);
                strncpy(pat.word, word, sizeof(pat.word) - 1);

                int ret = k_semantic_learn(word, &ctx, pipeline->config.semantic_generations, &pat);
                if (ret == 0) {
                    result->semantic = pat;
                    result->confidence = 0.5; /* New pattern, low confidence */
                    k_semantic_pattern_free(&pat);
                    add_pattern(pipeline, &result->semantic);
                } else {
                    result->confidence = 0.0;
                }

                kolibri_potok_cifr_sbros(&ctx.word_digits);
                free(ctx.word_digits.danniye);
                k_semantic_context_free(&ctx);
            }
        } else {
            result->confidence = 0.0;
        }
    }

    return 0;
}

/* ============================================================================
 * ENCODE FULL TEXT
 * ============================================================================ */

int kolibri_pipeline_encode_text(KolibriEncodingPipeline *pipeline, const char *text, KolibriEncodingResult *results,
                                 size_t max_results, size_t *out_count) {
    if (!pipeline || !text || !results || !out_count)
        return -1;

    *out_count = 0;
    int text_len = (int)strlen(text);
    int pos = 0;

    while (pos < text_len && *out_count < max_results) {
        /* Skip non-word characters */
        int char_len;
        uint32_t c = utf8_decode(text + pos, &char_len);
        if (!is_word_char(c)) {
            pos += char_len;
            continue;
        }

        /* Extract word */
        int word_start = pos;
        while (pos < text_len) {
            int cl;
            uint32_t ch = utf8_decode(text + pos, &cl);
            if (!is_word_char(ch))
                break;
            pos += cl;
        }

        int word_len = pos - word_start;
        if (word_len > 0 && word_len < 256) {
            char word_buf[256];
            memcpy(word_buf, text + word_start, word_len);
            word_buf[word_len] = '\0';

            KolibriEncodingResult *res = &results[*out_count];
            if (kolibri_pipeline_encode_word(pipeline, word_buf, res) == 0) {
                (*out_count)++;
            }
        }
    }

    return 0;
}

/* ============================================================================
 * SIMILARITY
 * ============================================================================ */

int kolibri_pipeline_similar_words(KolibriEncodingPipeline *pipeline, const char *query_word, size_t *indices,
                                   double *scores, size_t max_results, size_t *out_count) {
    if (!pipeline || !query_word || !indices || !scores || !out_count)
        return -1;

    /* Encode query word */
    KolibriEncodingResult query_result;
    if (kolibri_pipeline_encode_word(pipeline, query_word, &query_result) != 0)
        return -1;

    /* Compute similarity to all learned patterns */
    typedef struct {
        int idx;
        double score;
    } SimEntry;
    SimEntry *entries = malloc(pipeline->pattern_count * sizeof(SimEntry));
    if (!entries) {
        kolibri_potok_cifr_sbros(&query_result.digit_stream);
        return -1;
    }

    size_t count = 0;
    for (size_t i = 0; i < pipeline->pattern_count; i++) {
        double sim = k_semantic_similarity(&query_result.semantic, &pipeline->patterns[i]);
        if (sim > 0.1) { /* Threshold */
            entries[count].idx = (int)i;
            entries[count].score = sim;
            count++;
        }
    }

    /* Sort by score descending */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (entries[j].score > entries[i].score) {
                SimEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    /* Return top-N */
    size_t n = count < max_results ? count : max_results;
    for (size_t i = 0; i < n; i++) {
        indices[i] = entries[i].idx;
        scores[i] = entries[i].score;
    }
    *out_count = n;

    free(entries);
    kolibri_potok_cifr_sbros(&query_result.digit_stream);
    return 0;
}

double kolibri_pipeline_word_similarity(KolibriEncodingPipeline *pipeline, const char *word1, const char *word2) {
    KolibriEncodingResult r1, r2;
    if (kolibri_pipeline_encode_word(pipeline, word1, &r1) != 0)
        return 0.0;
    if (kolibri_pipeline_encode_word(pipeline, word2, &r2) != 0) {
        kolibri_potok_cifr_sbros(&r1.digit_stream);
        return 0.0;
    }

    double sim = k_semantic_similarity(&r1.semantic, &r2.semantic);
    kolibri_potok_cifr_sbros(&r1.digit_stream);
    kolibri_potok_cifr_sbros(&r2.digit_stream);
    return sim;
}

/* ============================================================================
 * STATS
 * ============================================================================ */

void kolibri_pipeline_get_stats(KolibriEncodingPipeline *pipeline, KolibriPipelineStats *stats) {
    if (!pipeline || !stats)
        return;

    stats->total_patterns = pipeline->pattern_count;
    stats->cyrillic_count = 0;
    stats->latin_count = 0;
    stats->avg_usage_count = 0;
    stats->avg_confidence = 0.0;

    for (size_t i = 0; i < pipeline->pattern_count; i++) {
        KolibriSemanticPattern *p = &pipeline->patterns[i];
        if (p->word[0]) {
            int clen = 0;
            uint32_t c = utf8_decode(p->word, &clen);
            if (is_cyrillic_char(c))
                stats->cyrillic_count++;
            if (is_latin_char(c))
                stats->latin_count++;
        }
        stats->avg_usage_count += p->usage_count;
    }

    if (pipeline->pattern_count > 0) {
        stats->avg_usage_count /= pipeline->pattern_count;
    }
}

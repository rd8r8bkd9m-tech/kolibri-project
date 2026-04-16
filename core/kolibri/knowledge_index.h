/*
 * Kolibri Knowledge Index — C API for building search index from Markdown files.
 */

#ifndef KOLIBRI_KNOWLEDGE_INDEX_H
#define KOLIBRI_KNOWLEDGE_INDEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriKnowledgeIndex KolibriKnowledgeIndex;

typedef struct {
    size_t token_index;
    float weight;
} KolibriKnowledgeVectorItem;

typedef struct {
    const char *id;
    const char *title;
    const char *source;
    const char *content;
    const KolibriKnowledgeVectorItem *vector;
    size_t vector_size;
    float norm;
} KolibriKnowledgeDoc;

typedef struct {
    const char *token;
    float idf;
} KolibriKnowledgeToken;

int kolibri_knowledge_index_create(const char *const *roots,
                                   size_t root_count,
                                   size_t max_length,
                                   KolibriKnowledgeIndex **out_index);

void kolibri_knowledge_index_destroy(KolibriKnowledgeIndex *index);

size_t kolibri_knowledge_index_document_count(const KolibriKnowledgeIndex *index);

const KolibriKnowledgeDoc *kolibri_knowledge_index_document(const KolibriKnowledgeIndex *index,
                                                            size_t idx);

size_t kolibri_knowledge_index_token_count(const KolibriKnowledgeIndex *index);

const KolibriKnowledgeToken *kolibri_knowledge_index_token(const KolibriKnowledgeIndex *index,
                                                           size_t idx);

int kolibri_knowledge_search(const KolibriKnowledgeIndex *index,
                              const char *query,
                              size_t limit,
                              size_t *out_indices,
                              float *out_scores,
                              size_t *out_result_count);

int kolibri_knowledge_index_write_json(const KolibriKnowledgeIndex *index,
                                       const char *output_dir);

/* #2. Bloom filter для быстрого отсечения запросов */
typedef struct {
    unsigned char *bits;
    size_t bit_count;
    size_t hash_count;
} KolibriBloomFilter;

int kolibri_bloom_filter_create(KolibriBloomFilter *bf, size_t expected_items);
void kolibri_bloom_filter_destroy(KolibriBloomFilter *bf);
int kolibri_bloom_filter_add(KolibriBloomFilter *bf, const char *key);
int kolibri_bloom_filter_might_contain(const KolibriBloomFilter *bf, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_KNOWLEDGE_INDEX_H */


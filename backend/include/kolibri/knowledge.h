#ifndef KOLIBRI_KNOWLEDGE_H
#define KOLIBRI_KNOWLEDGE_H

#include <stddef.h>

typedef struct {
    char *id;
    char *title;
    char *title_lower;
    char *content;
    char *content_lower;
    char *source;
    char *source_lower;
    double quality_score;
} KolibriKnowledgeDocument;

typedef struct {
    char *term;
    size_t doc_id;
    unsigned short freq;
} KolibriKnowledgeTermHit;

typedef struct {
    char *term;
    size_t start;
    size_t count;
} KolibriKnowledgePosting;

typedef struct {
    KolibriKnowledgeDocument *documents;
    size_t count;
    size_t capacity;
    KolibriKnowledgeTermHit *term_hits;
    size_t term_hit_count;
    size_t term_hit_capacity;
    KolibriKnowledgePosting *postings;
    size_t posting_count;
    size_t posting_capacity;
    int finalized;
} KolibriKnowledgeIndex;

int kolibri_knowledge_index_init(KolibriKnowledgeIndex *index);
void kolibri_knowledge_index_free(KolibriKnowledgeIndex *index);
int kolibri_knowledge_index_load_directory(KolibriKnowledgeIndex *index, const char *root_path);
size_t kolibri_knowledge_search_legacy(const KolibriKnowledgeIndex *index,
                                       const char *query,
                                       size_t limit,
                                       const KolibriKnowledgeDocument **results,
                                       double *scores);

#endif /* KOLIBRI_KNOWLEDGE_H */

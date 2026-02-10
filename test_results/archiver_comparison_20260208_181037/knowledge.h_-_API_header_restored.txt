#ifndef KOLIBRI_KNOWLEDGE_H
#define KOLIBRI_KNOWLEDGE_H

#include <stddef.h>

typedef struct {
    char *id;
    char *title;
    char *content;
    char *content_lower;
    char *source;
} KolibriKnowledgeDocument;

typedef struct {
    KolibriKnowledgeDocument *documents;
    size_t count;
    size_t capacity;
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

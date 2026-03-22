#include "kolibri/knowledge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_curated_retrieval(void) {
    KolibriKnowledgeIndex index;
    assert(kolibri_knowledge_index_init(&index) == 0);
    assert(kolibri_knowledge_index_load_directory(&index, "tests/data/logic_corpus") == 0);
    assert(index.count >= 4);

    const KolibriKnowledgeDocument *results[4];
    double scores[4];

    size_t found = kolibri_knowledge_search_legacy(&index, "What is Kolibri AI core", 4, results, scores);
    assert(found > 0);
    assert(strstr(results[0]->content_lower, "c-first") != NULL ||
           strstr(results[0]->title_lower, "kolibri") != NULL);

    found = kolibri_knowledge_search_legacy(&index, "How to compress data", 4, results, scores);
    assert(found > 0);
    assert(strstr(results[0]->title_lower, "compression") != NULL);

    found = kolibri_knowledge_search_legacy(&index, "Как работает геном", 4, results, scores);
    assert(found > 0);
    assert(strstr(results[0]->content_lower, "genome") != NULL);

    kolibri_knowledge_index_free(&index);
}

int main(void) {
    printf("=== Kolibri knowledge retrieval tests ===\n");
    test_curated_retrieval();
    printf("=== Knowledge retrieval tests PASSED ===\n");
    return 0;
}

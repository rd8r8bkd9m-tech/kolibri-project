#include "kolibri/knowledge_index.h"

#include <stdio.h>
#include <stdlib.h>

static int write_markdown(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fputs(content, f);
    fclose(f);
    return 0;
}

static void cleanup(void) {
    system("rm -rf ./test_data");
}

void test_knowledge_index(void) {
    const char *roots[1];
    roots[0] = "./test_data";
    system("mkdir -p ./test_data");
    write_markdown("./test_data/alpha.md", "# Привет\nKolibri отвечает на вопросы\n");
    write_markdown("./test_data/beta.md", "# Второй документ\nВ этом документе описаны ответы.\n");

    KolibriKnowledgeIndex *index = NULL;
    int err = kolibri_knowledge_index_create(roots, 1U, 256U, &index);
    if (err != 0 || !index) {
        fprintf(stderr, "index build failed: %d\n", err);
        cleanup();
        exit(1);
    }

    size_t doc_count = kolibri_knowledge_index_document_count(index);
    if (doc_count != 2U) {
        fprintf(stderr, "unexpected document count: %zu\n", doc_count);
        kolibri_knowledge_index_destroy(index);
        cleanup();
        exit(1);
    }

    size_t indices[2];
    float scores[2];
    size_t result_count = 0U;
    err = kolibri_knowledge_search(index, "Kolibri", 2U, indices, scores, &result_count);
    if (err != 0 || result_count == 0U) {
        fprintf(stderr, "search failed: %d\n", err);
        kolibri_knowledge_index_destroy(index);
        cleanup();
        exit(1);
    }

    const KolibriKnowledgeDoc *doc = kolibri_knowledge_index_document(index, indices[0]);
    if (!doc || !doc->title) {
        fprintf(stderr, "missing doc\n");
        kolibri_knowledge_index_destroy(index);
        cleanup();
        exit(1);
    }

    kolibri_knowledge_index_destroy(index);
    cleanup();
}

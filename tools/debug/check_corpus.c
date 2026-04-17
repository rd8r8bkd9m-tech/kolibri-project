#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kolibri/corpus.h"
#include "kolibri/formula.h"

int main() {
    KolibriCorpusContext corpus;
    k_corpus_init(&corpus, 0, 0);
    k_corpus_learn_file(&corpus, "docs/wikipedia/philosophy.md");
    k_corpus_learn_file(&corpus, "docs/wikipedia/AI.md");
    k_corpus_learn_file(&corpus, "docs/wikipedia/Evolution.md");
    
    printf("Words count: %zu\n", corpus.store.count);
    int target = 43552498;
    for (size_t i = 0; i < corpus.store.count; i++) {
        if (kf_hash_from_text(corpus.store.words[i]) == target) {
            printf("Found match: %s\n", corpus.store.words[i]);
        }
    }
    
    k_corpus_free(&corpus);
    return 0;
}

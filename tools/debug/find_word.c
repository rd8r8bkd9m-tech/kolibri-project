
#include <stdio.h>
#include <kolibri/corpus_learning.h>

int main() {
    KolibriCorpus corpus;
    if (kc_corpus_load(&corpus, "kolibri_corpus.bin") != 0) {
        printf("Corpus not found\n");
        return 1;
    }
    const char* word = kc_corpus_get_word(&corpus, 2235870);
    if (word) printf("Hash 2235870 is '%s'\n", word);
    else printf("Hash 2235870 not found in corpus\n");
    kc_corpus_free(&corpus);
    return 0;
}

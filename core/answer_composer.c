#include "kolibri/answer_composer.h"
#include <string.h>
#include <stdio.h>

void kac_init(KolibriAnswerComposer *composer) {
    if (!composer) return;
    memset(composer, 0, sizeof(KolibriAnswerComposer));
    composer->config.relevance_threshold = 0.5;
    composer->config.allow_duplicates = 0;
}

int kac_add_fragment(KolibriAnswerComposer *composer, const KolibriAssociation *association, double relevance_score) {
    if (!composer || !association || composer->fragment_count >= KAC_MAX_FRAGMENTS) return -1;
    if (relevance_score < composer->config.relevance_threshold) {
        printf("[Composer] Отклонен фрагмент (relevance: %.2f)\n", relevance_score);
        return 0;
    }
    KolibriCompositionFragment *frag = &composer->fragments[composer->fragment_count++];
    frag->association = association;
    frag->relevance_score = relevance_score;
    printf("[Composer] Добавлен фрагмент: %s\n", association->question);
    return 0;
}

int kac_compose(KolibriAnswerComposer *composer, const char *main_query) {
    if (!composer || !main_query) return -1;
    printf("[Composer] Запуск композиции, фрагментов: %zu\n", composer->fragment_count);
    memset(composer->composed_answer, 0, KAC_MAX_ANSWER_LEN);
    size_t current_len = 0;
    for (size_t i = 0; i < composer->fragment_count; ++i) {
        const char* fragment_text = composer->fragments[i].association->answer;
        int fragment_len = strlen(fragment_text);
        if (current_len + fragment_len + 2 < KAC_MAX_ANSWER_LEN) {
            strcat(composer->composed_answer, fragment_text);
            strcat(composer->composed_answer, " ");
            current_len += fragment_len + 1;
        }
    }
    if (current_len > 0) composer->composed_answer[current_len - 1] = '\0';
    return 0;
}

const char* kac_get_answer(const KolibriAnswerComposer *composer) {
    return composer ? composer->composed_answer : "";
}

void kac_reset(KolibriAnswerComposer *composer) {
    if (composer) kac_init(composer);
}

#include "kolibri/inference.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_web_formula_cleanup(void) {
    KolibriInferenceContext *ctx = kolibri_inference_create();
    assert(ctx != NULL);
    assert(kolibri_inference_set_strategy(ctx, KOLIBRI_INF_FORMULA) == 0);

    KolibriInferenceResult result;
    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое математика", &result) == 0);
    assert(result.formulas_applied >= 1);
    assert(strstr(result.response, "точная формальная наука") != NULL);
    assert(strstr(result.response, "Википедия") == NULL);
    assert(result.numeric_vote.winner_digit == 1U);
    assert(result.numeric_vote.consensus > 0.2);
    assert(result.numeric_vote.channels[1] > result.numeric_vote.channels[0]);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое география", &result) == 0);
    assert(result.formulas_applied >= 1);
    assert(strstr(result.response, "комплекс естественных и общественных наук") != NULL);
    assert(strstr(result.response, "Википедия") == NULL);
    assert(strstr(result.response, "точная формальная наука") == NULL);
    assert(strstr(result.response, "структуры и отношения") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что изучает география", &result) == 0);
    assert(result.formulas_applied >= 1);
    assert(strstr(result.response, "изучает поверхность Земли") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что изучает история", &result) == 0);
    assert(strstr(result.response, "История изучает развитие цивилизаций") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое философия", &result) == 0);
    assert(strstr(result.response, "форма познания") != NULL ||
           strstr(result.response, "система знаний") != NULL);
    assert(strstr(result.response, "Медицина") == NULL);
    assert(strstr(result.response, "лечебное искусство") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое медицина", &result) == 0);
    assert(strstr(result.response, "система научных знаний") != NULL);
    assert(strstr(result.response, "Анатомия") == NULL);
    assert(strstr(result.response, "от лат.") == NULL);
    assert(strstr(result.response, "точная формальная наука") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "медицина", &result) == 0);
    assert(strstr(result.response, "направленная на сохранение здоровья") != NULL);
    assert(strstr(result.response, "целями которой являются") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое анатомия", &result) == 0);
    assert(strstr(result.response, "раздел биологии") != NULL);
    assert(strstr(result.response, "Медицина") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое физика", &result) == 0);
    assert(strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "Философия") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "объясни физику", &result) == 0);
    assert(strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "фундаментальные законы природы") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "объясни физику простыми словами", &result) == 0);
    assert(strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "фундаментальные законы природы") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое биология", &result) == 0);
    assert(strstr(result.response, "наука о живых организмах") != NULL);
    assert(strstr(result.response, "География") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое астрономия", &result) == 0);
    assert(strstr(result.response, "небесных телах") != NULL ||
           strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "География") == NULL);
    assert(strstr(result.response, "Медицина") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что ты знаешь об астрономии", &result) == 0);
    assert(strstr(result.response, "небесных телах") != NULL ||
           strstr(result.response, "звёзд") != NULL ||
           strstr(result.response, "звезд") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое программирование", &result) == 0);
    assert(strstr(result.response, "созданию алгоритмов и программ") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое терапия", &result) == 0);
    assert(strstr(result.response, "клинической медицины") != NULL);
    assert(strstr(result.response, "сериал") == NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое химия", &result) == 0);
    assert(strstr(result.response, "наука о веществах") != NULL ||
           strstr(result.response, "естественная наука") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "расскажи про химию", &result) == 0);
    assert(strstr(result.response, "наука о веществах") != NULL ||
           strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "химические элементы") != NULL ||
           strstr(result.response, "реакции") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "расскажи о химии", &result) == 0);
    assert(strstr(result.response, "наука о веществах") != NULL ||
           strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "химические элементы") != NULL ||
           strstr(result.response, "реакции") != NULL);
    assert(strcmp(result.query_semantics.query_kind, "tell") == 0);
    assert(strcmp(result.query_semantics.canonical_topic, "химия") == 0);
    assert(strcmp(result.query_semantics.definition_entity, "химия") == 0);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что ты знаешь о химии", &result) == 0);
    assert(strstr(result.response, "наука о веществах") != NULL ||
           strstr(result.response, "естественная наука") != NULL);
    assert(strstr(result.response, "химические элементы") != NULL ||
           strstr(result.response, "реакции") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое экономика", &result) == 0);
    assert(strstr(result.response, "хозяйственной деятельности") != NULL ||
           strstr(result.response, "производством") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что такое право", &result) == 0);
    assert(strstr(result.response, "система общеобязательных норм") != NULL ||
           strstr(result.response, "регулирующих общественные отношения") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "чем занимается право", &result) == 0);
    assert(strstr(result.response, "определяет допустимое поведение") != NULL ||
           strstr(result.response, "обязанности") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "расскажи о праве", &result) == 0);
    assert(strstr(result.response, "система общеобязательных норм") != NULL);
    assert(strstr(result.response, "обязанности") != NULL ||
           strstr(result.response, "механизмы защиты") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "расскажи подробно о праве", &result) == 0);
    assert(strstr(result.response, "система общеобязательных норм") != NULL);
    assert(strstr(result.response, "обязанности") != NULL ||
           strstr(result.response, "механизмы защиты") != NULL);
    assert(strstr(result.response, "\n\n") != NULL || strstr(result.response, ". ") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "что ты знаешь о праве", &result) == 0);
    assert(strstr(result.response, "система общеобязательных норм") != NULL);
    assert(strstr(result.response, "обязанности") != NULL ||
           strstr(result.response, "механизмы защиты") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "как устроено право", &result) == 0);
    assert(strstr(result.response, "общеобязательных норм") != NULL ||
           strstr(result.response, "регулирующих общественные отношения") != NULL ||
           strstr(result.response, "обязанности") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "почему важно право", &result) == 0);
    assert(strstr(result.response, "играет важную роль") != NULL ||
           strstr(result.response, "потому что") != NULL);
    assert(strstr(result.response, "общеобязательных норм") != NULL ||
           strstr(result.response, "отношения") != NULL ||
           strstr(result.response, "обязанности") != NULL);
    assert(result.numeric_vote.channels[3] > 2.0);
    assert(strcmp(result.query_semantics.query_kind, "importance") == 0);
    assert(strcmp(result.query_semantics.canonical_topic, "право") == 0);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "почему важна математика", &result) == 0);
    assert(strstr(result.response, "играет важную роль") != NULL ||
           strstr(result.response, "потому что") != NULL);
    assert(strstr(result.response, "точная формальная наука") != NULL ||
           strstr(result.response, "структуры") != NULL ||
           strstr(result.response, "закономерности") != NULL);

    memset(&result, 0, sizeof(result));
    assert(kolibri_inference_run(ctx, "зачем нужна медицина", &result) == 0);
    assert(strstr(result.response, "играет важную роль") != NULL ||
           strstr(result.response, "потому что") != NULL);
    assert(strstr(result.response, "сохранение здоровья") != NULL ||
           strstr(result.response, "лечение") != NULL ||
           strstr(result.response, "терапии") != NULL);

    kolibri_inference_destroy(ctx);
}

int main(void) {
    test_web_formula_cleanup();
    printf("web formula inference tests passed\n");
    return 0;
}

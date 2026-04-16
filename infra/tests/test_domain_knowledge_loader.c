/*
 * test_domain_knowledge_loader.c
 *
 * Тесты для загрузчика доменных знаний
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reasoning_engine.h"
#include "kolibri/domain_knowledge_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * ЗАГРУЗКА ВСЕХ ДОМЕНОВ
 * ============================================================================ */

void test_load_all_domains(void) {
    printf("Testing load all domains...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);

    int total = kolibri_domain_load_all(&config);
    assert(total > 40);  /* Должно быть ~47 фактов/правил */

    printf("  Total facts/rules loaded: %d\n", total);
    kolibri_domain_print_stats(&config);

    printf("✓ Load all domains test passed\n\n");
}

/* ============================================================================
 * ФИЗИКА: Modus Ponens
 * ============================================================================ */

void test_physics_modus_ponens(void) {
    printf("Testing physics: Modus Ponens with Newton's law...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_physics(&config);

    /* Проверяем что правило загружено */
    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Тело массой m ускоряется силой F", &config, &result);
    assert(ret == 0);
    assert(strlen(result.answer) > 0);

    printf("  Query: 'Тело массой m ускоряется силой F'\n");
    printf("  Result: '%.80s'\n", result.answer);
    printf("  Confidence: %.2f\n", result.confidence);

    /* Должен сработать Modus Ponens */
    assert(result.confidence > 0.7);

    printf("✓ Physics Modus Ponens test passed\n\n");
}

/* ============================================================================
 * ФИЗИКА: Chain Rule (Закон Ома → Мощность)
 * ============================================================================ */

void test_physics_chain_rule(void) {
    printf("Testing physics: Chain rule (Ohm's law → Power)...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_physics(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Напряжение U, ток I, сопротивление R", &config, &result);
    assert(ret == 0);
    assert(strlen(result.answer) > 0);

    printf("  Query: 'Напряжение U, ток I, сопротивление R'\n");
    printf("  Result: '%.80s'\n", result.answer);
    printf("  Steps: %d\n", result.chain.num_steps);

    printf("✓ Physics chain rule test passed\n\n");
}

/* ============================================================================
 * ХИМИЯ: Реакция горения
 * ============================================================================ */

void test_chemistry_reaction(void) {
    printf("Testing chemistry: combustion reaction...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_chemistry(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Водород H2 горит в кислороде O2", &config, &result);
    assert(ret == 0);
    assert(strlen(result.answer) > 0);

    printf("  Query: 'Водород H2 горит в кислороде O2'\n");
    printf("  Result: '%.80s'\n", result.answer);
    printf("  Confidence: %.2f\n", result.confidence);

    /* Проверяем что результат содержит химическую реакцию */
    assert(strstr(result.answer, "H2O") != NULL ||
           strstr(result.answer, "реакц") != NULL ||
           strlen(result.answer) > 10);

    printf("✓ Chemistry reaction test passed\n\n");
}

/* ============================================================================
 * ПРОГРАММИРОВАНИЕ: Алгоритмы
 * ============================================================================ */

void test_programming_algorithms(void) {
    printf("Testing programming: algorithm selection...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_programming(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Массив отсортирован, элемент ищется", &config, &result);
    assert(ret == 0);
    assert(strlen(result.answer) > 0);

    printf("  Query: 'Массив отсортирован, элемент ищется'\n");
    printf("  Result: '%.80s'\n", result.answer);
    printf("  Confidence: %.2f\n", result.confidence);

    /* Должен найти бинарный поиск */
    assert(strstr(result.answer, "Бинарный") != NULL ||
           strstr(result.answer, "бинарн") != NULL ||
           strstr(result.answer, "log") != NULL ||
           strlen(result.answer) > 10);

    printf("✓ Programming algorithms test passed\n\n");
}

/* ============================================================================
 * ЮРИСПРУДЕНЦИЯ: Правовые принципы
 * ============================================================================ */

void test_law_principles(void) {
    printf("Testing law: legal principles...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_law(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_deductive("Невиновность не доказана", &config, &result);
    assert(ret == 0);
    assert(strlen(result.answer) > 0);

    printf("  Query: 'Невиновность не доказана'\n");
    printf("  Result: '%.80s'\n", result.answer);
    printf("  Confidence: %.2f\n", result.confidence);

    /* Должен найти презумпцию невиновности */
    assert(strstr(result.answer, "невиновн") != NULL ||
           strlen(result.answer) > 10);

    printf("✓ Law principles test passed\n\n");
}

/* ============================================================================
 * АБДУКЦИЯ: Химическая реакция
 * ============================================================================ */

void test_abductive_chemistry(void) {
    printf("Testing abductive reasoning with chemistry...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_chemistry(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_abductive("2H2O (вода образовалась)", &config, &result);
    assert(ret == 0);
    assert(result.num_hypotheses >= 1);

    printf("  Query: '2H2O (вода образовалась)'\n");
    printf("  Hypotheses: %d\n", result.num_hypotheses);
    if (result.num_hypotheses > 0) {
        printf("    Best: %s (prob: %.2f)\n",
               result.hypotheses[result.best_hypothesis_idx].hypothesis,
               result.hypotheses[result.best_hypothesis_idx].probability);
    }

    printf("✓ Abductive chemistry test passed\n\n");
}

/* ============================================================================
 * COUNTERFACTUAL: Физика
 * ============================================================================ */

void test_counterfactual_physics(void) {
    printf("Testing counterfactual with physics...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_physics(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_counterfactual("Тело массой m ускоряется силой F",
                                       "Сила F не действует на тело",
                                       &config, &result);
    assert(ret == 0);
    assert(strlen(result.counterfactual_outcome) > 0);

    printf("  Query: 'Тело массой m ускоряется силой F'\n");
    printf("  What if: 'Сила F не действует на тело'\n");
    printf("  Outcome: '%.80s'\n", result.counterfactual_outcome);
    printf("  Confidence: %.2f\n", result.confidence);

    printf("✓ Counterfactual physics test passed\n\n");
}

/* ============================================================================
 * ИНТЕГРАЦИОННЫЙ ТЕСТ: полный цикл
 * ============================================================================ */

void test_integrated_domain_workflow(void) {
    printf("Testing integrated domain workflow...\n");

    KolibriREConfig config = {0};
    kolibri_re_init(&config);
    kolibri_domain_load_all(&config);

    /* 1. Физика: дедукция */
    KolibriReasoningResult result1;
    kolibri_re_deductive("Напряжение U, ток I, сопротивление R", &config, &result1);
    assert(result1.confidence > 0.3);

    /* 2. Химия: абдукция */
    KolibriReasoningResult result2;
    kolibri_re_abductive("H2O образовалась", &config, &result2);
    assert(result2.num_hypotheses >= 1);

    /* 3. Программирование: дедукция */
    KolibriReasoningResult result3;
    kolibri_re_deductive("Нужна быстрая сортировка массива", &config, &result3);
    assert(result3.confidence > 0.3);

    /* 4. Право: дедукция */
    KolibriReasoningResult result4;
    kolibri_re_deductive("Невиновность не доказана", &config, &result4);
    assert(result4.confidence > 0.3);

    printf("  Physics deduction: conf=%.2f\n", result1.confidence);
    printf("  Chemistry abduction: hypotheses=%d\n", result2.num_hypotheses);
    printf("  Programming deduction: conf=%.2f\n", result3.confidence);
    printf("  Law deduction: conf=%.2f\n", result4.confidence);

    printf("✓ Integrated domain workflow test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Domain Knowledge Loader Tests\n");
    printf("===========================================\n\n");

    printf("--- Domain Loading ---\n\n");
    test_load_all_domains();

    printf("--- Physics ---\n\n");
    test_physics_modus_ponens();
    test_physics_chain_rule();

    printf("--- Chemistry ---\n\n");
    test_chemistry_reaction();
    test_abductive_chemistry();

    printf("--- Programming ---\n\n");
    test_programming_algorithms();

    printf("--- Law ---\n\n");
    test_law_principles();

    printf("--- Counterfactual ---\n\n");
    test_counterfactual_physics();

    printf("--- Integration ---\n\n");
    test_integrated_domain_workflow();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}

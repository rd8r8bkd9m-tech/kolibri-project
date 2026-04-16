/*
 * test_explanation_generator.c
 *
 * Тесты для генератора объяснений Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/explanation_generator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * ТЕСТ ИНИЦИАЛИЗАЦИИ
 * ============================================================================ */

void test_eg_init(void) {
    printf("Testing explanation generator initialization...\n");

    KolibriEGConfig config = {0};
    int ret = kolibri_eg_init(&config);
    
    assert(ret == 0);
    assert(config.include_formula == 1);
    assert(config.include_steps == 1);
    assert(config.include_sources == 1);
    assert(config.include_confidence == 1);
    assert(config.max_steps == KEG_MAX_STEPS);

    printf("✓ Explanation generator initialization test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ГЕНЕРАЦИИ ДЛЯ ЛИНЕЙНОГО УРАВНЕНИЯ
 * ============================================================================ */

void test_linear_equation(void) {
    printf("Testing linear equation explanation...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    int ret = kolibri_eg_generate_math_explanation(
        "реши уравнение 2x+3=7",
        "x = 2",
        &config,
        &explanation
    );

    assert(ret == 0);
    assert(strcmp(explanation.query, "реши уравнение 2x+3=7") == 0);
    assert(strcmp(explanation.direct_answer, "x = 2") == 0);
    assert(strlen(explanation.formula_text) > 0);
    assert(explanation.num_steps >= 3);
    assert(explanation.num_sources >= 1);
    assert(explanation.confidence > 0.8);

    printf("  Query: %s\n", explanation.query);
    printf("  Answer: %s\n", explanation.direct_answer);
    printf("  Formula: %s\n", explanation.formula_text);
    printf("  Steps: %d\n", explanation.num_steps);
    printf("  Sources: %d\n", explanation.num_sources);
    printf("  Confidence: %.2f\n", explanation.confidence);

    kolibri_eg_print(&explanation);

    printf("✓ Linear equation explanation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ГЕНЕРАЦИИ ДЛЯ КВАДРАТНОГО УРАВНЕНИЯ
 * ============================================================================ */

void test_quadratic_equation(void) {
    printf("Testing quadratic equation explanation...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_math_explanation(
        "реши x²-5x+6=0",
        "x₁ = 3, x₂ = 2",
        &config,
        &explanation
    );

    assert(explanation.num_steps >= 3);
    assert(strstr(explanation.formula_text, "x²") != NULL || 
           strstr(explanation.formula_text, "квадрат") != NULL);

    printf("  Query: %s\n", explanation.query);
    printf("  Formula: %s\n", explanation.formula_text);
    printf("  Steps: %d\n", explanation.num_steps);
    printf("  Confidence: %.2f\n", explanation.confidence);

    kolibri_eg_print(&explanation);

    printf("✓ Quadratic equation explanation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ГЕНЕРАЦИИ ДЛЯ ГЕОМЕТРИИ
 * ============================================================================ */

void test_geometry_explanation(void) {
    printf("Testing geometry explanation...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_math_explanation(
        "площадь круга радиусом 5",
        "78.54 кв. единиц",
        &config,
        &explanation
    );

    assert(explanation.num_steps >= 3);
    assert(strstr(explanation.formula_text, "π") != NULL ||
           strstr(explanation.formula_text, "r²") != NULL);

    printf("  Query: %s\n", explanation.query);
    printf("  Answer: %s\n", explanation.direct_answer);
    printf("  Formula: %s\n", explanation.formula_text);
    printf("  Steps: %d\n", explanation.num_steps);
    printf("  Confidence: %.2f\n", explanation.confidence);

    kolibri_eg_print(&explanation);

    printf("✓ Geometry explanation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ GENERAL EXPLANATION
 * ============================================================================ */

void test_general_explanation(void) {
    printf("Testing general explanation...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_general_explanation(
        "что такое фотосинтез",
        "Процесс преобразования света в энергию растениями",
        &config,
        &explanation
    );

    assert(strcmp(explanation.query, "что такое фотосинтез") == 0);
    assert(explanation.num_steps >= 2);
    assert(explanation.num_sources >= 1);

    printf("  Query: %s\n", explanation.query);
    printf("  Answer: %s\n", explanation.direct_answer);
    printf("  Steps: %d\n", explanation.num_steps);
    printf("  Confidence: %.2f\n", explanation.confidence);

    kolibri_eg_print(&explanation);

    printf("✓ General explanation test passed\n\n");
}

/* ============================================================================
 * ТЕСТ FORMAT TEXT
 * ============================================================================ */

void test_format_text(void) {
    printf("Testing text formatting...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_math_explanation(
        "реши 3x-6=9",
        "x = 5",
        &config,
        &explanation
    );

    char output[KEG_MAX_EXPLANATION_LEN];
    int ret = kolibri_eg_format_text(&explanation, output, sizeof(output));
    
    assert(ret == 0);
    assert(strlen(output) > 0);
    assert(strstr(output, "Ответ:") != NULL);
    assert(strstr(output, "Формула:") != NULL);
    assert(strstr(output, "Шаги") != NULL || strstr(output, "шаги") != NULL);

    printf("  Formatted output length: %zu\n", strlen(output));
    printf("  Preview:\n%s\n", output);

    printf("✓ Text formatting test passed\n\n");
}

/* ============================================================================
 * ТЕСТ FORMAT MARKDOWN
 * ============================================================================ */

void test_format_markdown(void) {
    printf("Testing Markdown formatting...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_math_explanation(
        "реши x+2=5",
        "x = 3",
        &config,
        &explanation
    );

    char output[KEG_MAX_EXPLANATION_LEN];
    kolibri_eg_format_markdown(&explanation, output, sizeof(output));

    assert(strlen(output) > 0);
    assert(strstr(output, "##") != NULL);  /* Markdown headers */
    assert(strstr(output, "```") != NULL);  /* Code blocks */

    printf("  Markdown output length: %zu\n", strlen(output));
    printf("  Preview:\n%s\n", output);

    printf("✓ Markdown formatting test passed\n\n");
}

/* ============================================================================
 * ТЕСТ FORMAT JSON
 * ============================================================================ */

void test_format_json(void) {
    printf("Testing JSON formatting...\n");

    KolibriEGConfig config = {0};
    kolibri_eg_init(&config);

    KolibriExplanation explanation;
    kolibri_eg_generate_math_explanation(
        "реши 4x=16",
        "x = 4",
        &config,
        &explanation
    );

    char output[KEG_MAX_EXPLANATION_LEN];
    kolibri_eg_format_json(&explanation, output, sizeof(output));

    assert(strlen(output) > 0);
    assert(strstr(output, "{") != NULL);
    assert(strstr(output, "\"query\"") != NULL);
    assert(strstr(output, "\"answer\"") != NULL);
    assert(strstr(output, "\"confidence\"") != NULL);

    printf("  JSON output length: %zu\n", strlen(output));
    printf("  Preview:\n%.200s...\n", output);

    printf("✓ JSON formatting test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ADD STEP
 * ============================================================================ */

void test_add_step(void) {
    printf("Testing add step...\n");

    KolibriExplanation explanation = {0};

    kolibri_eg_add_step(&explanation, KEG_STEP_FORMULA,
                       "Тестовый шаг",
                       "Описание",
                       "a + b = c",
                       42.0,
                       0.95);

    assert(explanation.num_steps == 1);
    assert(explanation.steps[0].step_num == 1);
    assert(strcmp(explanation.steps[0].title, "Тестовый шаг") == 0);
    assert(explanation.steps[0].result == 42.0);
    assert(explanation.steps[0].confidence == 0.95);

    printf("  Step 1: %s - %.0f (%.2f)\n",
           explanation.steps[0].title,
           explanation.steps[0].result,
           explanation.steps[0].confidence);

    printf("✓ Add step test passed\n\n");
}

/* ============================================================================
 * ТЕСТ ADD SOURCE
 * ============================================================================ */

void test_add_source(void) {
    printf("Testing add source...\n");

    KolibriExplanation explanation = {0};

    kolibri_eg_add_source(&explanation,
                         "Тестовый источник",
                         "math",
                         0x1234,
                         1710000000000.0,
                         1);

    assert(explanation.num_sources == 1);
    assert(strcmp(explanation.sources[0].source, "Тестовый источник") == 0);
    assert(strcmp(explanation.sources[0].domain, "math") == 0);
    assert(explanation.sources[0].formula_id == 0x1234);
    assert(explanation.sources[0].verified == 1);

    printf("  Source 1: %s [%s] ID=0x%lX ✓\n",
           explanation.sources[0].source,
           explanation.sources[0].domain,
           (unsigned long)explanation.sources[0].formula_id);

    printf("✓ Add source test passed\n\n");
}

/* ============================================================================
 * ТЕСТ STEP TYPE NAMES
 * ============================================================================ */

void test_step_type_names(void) {
    printf("Testing step type names...\n");

    assert(strcmp(kolibri_eg_step_type_name(KEG_STEP_FORMULA), "formula") == 0);
    assert(strcmp(kolibri_eg_step_type_name(KEG_STEP_LOGIC), "logic") == 0);
    assert(strcmp(kolibri_eg_step_type_name(KEG_STEP_ARITHMETIC), "arithmetic") == 0);
    assert(strcmp(kolibri_eg_step_type_name(KEG_STEP_VERIFICATION), "verification") == 0);

    printf("  Formula: %s\n", kolibri_eg_step_type_name(KEG_STEP_FORMULA));
    printf("  Logic: %s\n", kolibri_eg_step_type_name(KEG_STEP_LOGIC));
    printf("  Arithmetic: %s\n", kolibri_eg_step_type_name(KEG_STEP_ARITHMETIC));
    printf("  Verification: %s\n", kolibri_eg_step_type_name(KEG_STEP_VERIFICATION));

    printf("✓ Step type names test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Explanation Generator Tests\n");
    printf("===========================================\n\n");

    /* Initialization */
    printf("--- Initialization ---\n\n");
    test_eg_init();

    /* Linear Equation */
    printf("--- Linear Equation ---\n\n");
    test_linear_equation();

    /* Quadratic Equation */
    printf("--- Quadratic Equation ---\n\n");
    test_quadratic_equation();

    /* Geometry */
    printf("--- Geometry ---\n\n");
    test_geometry_explanation();

    /* General */
    printf("--- General ---\n\n");
    test_general_explanation();

    /* Formatting */
    printf("--- Formatting ---\n\n");
    test_format_text();
    test_format_markdown();
    test_format_json();

    /* Helpers */
    printf("--- Helpers ---\n\n");
    test_add_step();
    test_add_source();
    test_step_type_names();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}

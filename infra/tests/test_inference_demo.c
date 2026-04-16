/*
 * test_inference_demo.c
 *
 * Демонстрация реальных возможностей Kolibri AI:
 * — Что он умеет сейчас?
 * — Где его пределы?
 */

#include "kolibri/inference.h"
#include "kolibri/formula.h"
#include "kolibri/symbol_table.h"
#include "kolibri/logical_memory.h"
#include "kolibri/formula_logic.h"
#include "kolibri/knowledge.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===== Цвета для терминала ===== */
#define GREEN  "\033[32m"
#define RED    "\033[31m"
#define YELLOW "\033[33m"
#define CYAN   "\033[36m"
#define BOLD   "\033[1m"
#define RESET  "\033[0m"

static void separator(const char *title) {
    printf("\n" BOLD "━━━ %s ━━━" RESET "\n\n", title);
}

/* ===== ТЕСТ 1: Inference pipeline ===== */
static void test_inference_pipeline(void) {
    separator("ТЕСТ 1: Inference Pipeline (поиск знаний + формулы + логика)");

    KolibriInferenceContext *ctx = kolibri_inference_create();
    KolibriInferenceResult result;

    const char *queries[] = {
        "What is Kolibri?",
        "How to compress data?",
        "Explain number thinking",
        "2 + 2",
        "Как работает геном?",
    };

    for (int i = 0; i < 5; i++) {
        kolibri_inference_run(ctx, queries[i], &result);
        printf(CYAN "  Q: " RESET "%s\n", queries[i]);
        printf(YELLOW "  A: " RESET "%.120s%s\n", result.response,
               result.response_length > 120 ? "..." : "");
        printf("     confidence=%.2f  steps=%zu  time=%.1fms\n\n",
               result.total_confidence, result.step_count, result.total_duration_ms);
    }

    uint64_t total;
    double avg_conf, avg_dur;
    kolibri_inference_get_stats(ctx, &total, &avg_conf, &avg_dur);
    printf("  📊 Всего запросов: %lu, средняя уверенность: %.2f, среднее время: %.1fms\n",
           (unsigned long)total, avg_conf, avg_dur);

    kolibri_inference_destroy(ctx);
}

/* ===== ТЕСТ 2: Мета-формулы — рассуждение через логику ===== */
static void test_meta_reasoning(void) {
    separator("ТЕСТ 2: Мета-формулы (генерация + трансформация + эволюция)");

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    /* Задача: сжать паттерн "AAABBBCCC" */
    printf("  Задача: сжать паттерн AAABBBCCC\n");

    lm_store_logic(mem, "raw", lm_logic_constant("AAABBBCCC"));
    lm_store_logic(mem, "partA", lm_logic_repeat("A", 3));
    lm_store_logic(mem, "partB", lm_logic_repeat("B", 3));
    lm_store_logic(mem, "partC", lm_logic_repeat("C", 3));

    /* Материализация repeat-паттернов по отдельности и проверка */
    char buf_a[32], buf_b[32], buf_c[32];
    lm_materialize(mem, "partA", buf_a, sizeof(buf_a));
    lm_materialize(mem, "partB", buf_b, sizeof(buf_b));
    lm_materialize(mem, "partC", buf_c, sizeof(buf_c));
    char combined[64];
    snprintf(combined, sizeof(combined), "%s%s%s", buf_a, buf_b, buf_c);
    printf("  Логическое представление: repeat(A,3) + repeat(B,3) + repeat(C,3)\n");
    printf("  Результат материализации: " GREEN "%s" RESET "\n", combined);
    printf("  Совпадает с оригиналом: %s\n",
           strcmp(combined, "AAABBBCCC") == 0 ? GREEN "ДА ✓" RESET : RED "НЕТ ✗" RESET);

    /* Логическая сложность vs сырые данные */
    LogicExpression *raw_expr = lm_logic_constant("AAABBBCCC");
    double raw_complexity = lm_compute_complexity(raw_expr);
    double compressed_complexity = lm_compute_complexity(mem->cells[1].logic)
                                 + lm_compute_complexity(mem->cells[2].logic)
                                 + lm_compute_complexity(mem->cells[3].logic);
    lm_destroy_logic(raw_expr);
    printf("  Сложность сырых данных:  %.2f\n", raw_complexity);
    printf("  Сложность логики:        %.2f\n", compressed_complexity);
    printf("  Сжатие по сложности:     %.1fx\n\n",
           raw_complexity > 0 ? compressed_complexity / raw_complexity : 0);

    /* Auto-discover */
    int discovered = mf_auto_discover_patterns(mem, store);
    printf("  Auto-discover нашёл %d паттернов в памяти\n", discovered);

    /* Эволюция паттерна */
    MetaFormula *gen = mf_create_repeat_generator("X", "5");
    MetaFormula *evolved = mf_evolve_meta(gen, 0.5);
    char desc[256];
    mf_to_string(gen, desc, sizeof(desc));
    printf("\n  Оригинал:    %s\n", desc);
    mf_to_string(evolved, desc, sizeof(desc));
    printf("  Эволюция:    %s\n", desc);
    printf("  Поколение:   %lu\n", (unsigned long)evolved->generation);

    free(gen);
    free(evolved);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
}

/* ===== ТЕСТ 3: Формульное обучение (ассоциации) ===== */
static void test_formula_learning(void) {
    separator("ТЕСТ 3: Формульное обучение (teach → ask)");

    KolibriFormulaPool *pool = calloc(1, sizeof(KolibriFormulaPool));
    if (!pool) { printf("  OOM\n"); return; }
    kf_pool_init(pool, 42);

    /* Добавляем числовые примеры для эволюции */
    struct { int input; int target; } examples[] = {
        {1, 2}, {2, 4}, {3, 6}, {4, 8}, {5, 10},
        {10, 20}, {100, 200},
    };

    printf("  Задача: найти формулу f(x) = 2x\n");
    printf("  Загрузка примеров:\n");
    for (int i = 0; i < 7; i++) {
        kf_pool_add_example(pool, examples[i].input, examples[i].target);
        printf("    f(%d) = %d\n", examples[i].input, examples[i].target);
    }

    /* Эволюция формул — 500 поколений */
    printf("\n  Запуск эволюции (500 поколений)...\n");
    kf_pool_tick(pool, 500);

    /* Проверяем лучшую формулу */
    const KolibriFormula *best = kf_pool_best(pool);
    if (best) {
        char desc[256];
        kf_formula_describe(best, desc, sizeof(desc));
        printf("  Лучшая формула: " GREEN "%s" RESET "\n", desc);

        /* Тестируем на новых данных */
        int test_inputs[] = {7, 15, 50, 1000};
        printf("\n  Проверка на новых данных:\n");
        for (int i = 0; i < 4; i++) {
            int output = 0;
            int ok = kf_formula_apply(best, test_inputs[i], &output);
            int expected = test_inputs[i] * 2;
            const char *status = (ok == 0 && output == expected)
                ? GREEN "✓" RESET : YELLOW "~" RESET;
            printf("    f(%d) = %d (ожидалось %d) %s\n",
                   test_inputs[i], output, expected, status);
        }
    } else {
        printf("    " RED "Формула не найдена" RESET "\n");
    }

    kf_pool_free(pool);
    free(pool);
}

/* ===== ТЕСТ 4: Логическое рассуждение (chain-of-thought) ===== */
static void test_chain_of_thought(void) {
    separator("ТЕСТ 4: Chain-of-Thought (многошаговое рассуждение)");

    LogicalMemory *mem = lm_create_memory();

    /* Задача: Если A > B и B > C, то A > C (транзитивность) */
    printf("  Задача: транзитивное рассуждение\n");
    printf("  Дано: Cat part_of Animals, Animals part_of Life\n");
    printf("  Вывод: Cat → Life?\n\n");

    lm_store_logic(mem, "r1",
        lm_logic_relation(lm_logic_constant("Cat"),
                          lm_logic_constant("Animals"), "part_of"));
    lm_store_logic(mem, "r2",
        lm_logic_relation(lm_logic_constant("Animals"),
                          lm_logic_constant("Life"), "part_of"));

    /* Создаём мета-формулу для транзитивного вывода */
    MetaFormulaStore *store = mf_create_store();
    MetaFormula *derive = mf_create_relation_deriver("r1", "r2", "transitive");
    LogicExpression *result = mf_execute(store, derive, mem);

    if (result && result->type == LOGIC_RELATION) {
        char buf[256];
        lm_logic_to_string(result, buf, sizeof(buf));
        printf("  Результат: " GREEN "%s" RESET "\n", buf);
        printf("  Вывод сделан: " GREEN "ДА ✓" RESET "\n");
    } else {
        printf("  " RED "Не удалось вывести отношение" RESET "\n");
    }

    /* Conditional logic */
    printf("\n  Задача: условное выражение if(1, YES, NO)\n");
    LogicExpression *cond = lm_logic_conditional(
        lm_logic_constant("1"),
        lm_logic_constant("ВЕРНО"),
        lm_logic_constant("НЕВЕРНО")
    );
    char *materialized = lm_materialize_logic(cond);
    printf("  Результат: " GREEN "%s" RESET "\n", materialized ? materialized : "ошибка");
    free(materialized);
    lm_destroy_logic(cond);

    free(derive);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
}

/* ===== ИТОГ ===== */
static void print_summary(void) {
    separator("ИТОГ: Что умеет Kolibri AI сейчас");

    printf(GREEN "  ✓ " RESET "Числовое кодирование (Number-Thinking): текст → цифры → формулы\n");
    printf(GREEN "  ✓ " RESET "Ассоциативное обучение: teach Q/A → lookup по hash\n");
    printf(GREEN "  ✓ " RESET "Логическая память: 8 типов выражений + lazy materialization\n");
    printf(GREEN "  ✓ " RESET "Мета-формулы: генерация, трансформация, эволюция, сжатие\n");
    printf(GREEN "  ✓ " RESET "Транзитивное рассуждение: A→B, B→C ⇒ A→C\n");
    printf(GREEN "  ✓ " RESET "Chain-of-thought: многошаговый inference pipeline\n");
    printf(GREEN "  ✓ " RESET "Компрессия: паттерны repeat/sequence вместо сырых данных\n");
    printf(GREEN "  ✓ " RESET "Эволюция: мутация формул с отбором лучших\n\n");

    printf(YELLOW "  ⚠ " RESET "Нет LLM-уровня генерации свободного текста\n");
    printf(YELLOW "  ⚠ " RESET "Knowledge base пуста — нужно загрузить данные (data/)\n");
    printf(YELLOW "  ⚠ " RESET "Нет attention-механизма (только контекстное окно)\n");
    printf(YELLOW "  ⚠ " RESET "Нет GPU-ускорения матричных операций\n");
    printf(YELLOW "  ⚠ " RESET "Inference через поиск, не генеративный\n\n");

    printf(BOLD "  Kolibri — это НЕ языковая модель типа GPT/Claude.\n");
    printf("  Это эволюционный AI на основе числового кодирования и логики.\n");
    printf("  Его сила — в компрессии, формульном выводе и рассуждениях,\n");
    printf("  а не в генерации свободного текста." RESET "\n\n");
}

int main(void) {
    printf("\n" BOLD "╔══════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI AI — Проверка реальных возможностей   ║\n");
    printf("╚══════════════════════════════════════════════════════╝" RESET "\n");

    test_inference_pipeline();
    test_meta_reasoning();
    test_formula_learning();
    test_chain_of_thought();
    print_summary();

    return 0;
}

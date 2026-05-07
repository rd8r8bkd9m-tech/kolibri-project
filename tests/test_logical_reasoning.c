/*
 * Тест логического вывода через граф знаний
 * Задача: Транзитивное рассуждение (A > B, B > C => A > C)
 */
#include "kolibri/knowledge.h"
#include "kolibri/symbol_table.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main() {
    printf("[TEST] Запуск теста логического вывода...\n");

    /* 1. Инициализация */
    KolibriSymbolTable symbols;
    kolibri_symbol_table_init(&symbols);
    
    KolibriKnowledgeGraph graph;
    kg_init(&graph, &symbols);

    /* 2. Добавление фактов (логические связи) */
    /* Факт 1: Яблоко является Фруктом */
    kg_add_relation(&graph, "Яблоко", "является", "Фрукт");
    
    /* Факт 2: Фрукт является Едой */
    kg_add_relation(&graph, "Фрукт", "является", "Еда");

    printf("[INFO] Добавлены факты:\n");
    printf("  - Яблоко -> является -> Фрукт\n");
    printf("  - Фрукт -> является -> Еда\n");

    /* 3. Логический запрос (поиск пути) */
    printf("[QUERY] Поиск связи между 'Яблоко' и 'Еда'...\n");
    
    KolibriPath path;
    int found = kg_find_path(&graph, "Яблоко", "Еда", &path, 5);

    if (found && path.length > 0) {
        printf("[RESULT] Путь найден! Длина: %d\n", path.length);
        for (size_t i = 0; i < path.length; ++i) {
            const char *node = kolibri_symbol_table_get_text(&symbols, path.nodes[i]);
            printf("  Step %zu: %s\n", i, node ? node : "Unknown");
        }
        printf("[PASS] Логический вывод успешно выполнен!\n");
    } else {
        printf("[FAIL] Связь не найдена. Логика не сработала.\n");
    }

    kg_free(&graph);
    kolibri_symbol_table_free(&symbols);
    return found ? 0 : 1;
}

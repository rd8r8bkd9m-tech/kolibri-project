/*
 * test_knowledge_base_facade.c — Тесты для высокоуровневого интерфейса базы знаний.
 */

#include "kolibri/knowledge_base.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define RESET   "\033[0m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"

static void separator(const char *title) {
    printf("\n" YELLOW "=== %s ===" RESET "\n", title);
}

static void test_kb_initialization(void) {
    separator("Test KB Initialization");

    int rc = kolibri_kb_init(NULL); /* Пустой индекс */
    assert(rc == 0);

    printf(GREEN "[OK]" RESET " kolibri_kb_init()\n");
}

static void test_kb_dynamic_add(void) {
    separator("Test Dynamic Add Facts & Rules");

    int rc = kolibri_kb_add_fact("Сократ — человек", 1.0, "test");
    assert(rc == 0);

    rc = kolibri_kb_add_rule("Человек", "Смертен", 3, 1.0, "philosophy");
    assert(rc == 0);

    printf(GREEN "[OK]" RESET " kolibri_kb_add_fact(), kolibri_kb_add_rule()\n");
}

static void test_kb_search_empty(void) {
    separator("Test KB Search (Empty)");

    char json_out[2048];
    int rc = kolibri_kb_search("Сократ", json_out, sizeof(json_out));
    /* Даже в пустом индексе поиск может вернуть 0 результатов, но код не должен падать */
    printf("JSON Output: %s\n", json_out);
    assert(rc == 0);
    assert(strstr(json_out, "\"count\": 0") != NULL);

    printf(GREEN "[OK]" RESET " kolibri_kb_search() on empty index\n");
}

int main(void) {
    printf("Запуск тестов фасада Knowledge Base...\n");

    test_kb_initialization();
    test_kb_dynamic_add();
    test_kb_search_empty();

    kolibri_kb_destroy();

    printf("\n" GREEN "Все тесты успешно пройдены!" RESET "\n");
    return 0;
}

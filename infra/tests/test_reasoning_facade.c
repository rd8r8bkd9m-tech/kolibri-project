/*
 * test_reasoning_facade.c — Тесты для высокоуровневого интерфейса рассуждений.
 */

#include "kolibri/reasoning.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"

static void separator(const char *title) {
    printf("\n" YELLOW "=== %s ===" RESET "\n", title);
}

static void test_initialization(void) {
    separator("Test Initialization");
    int rc = kolibri_reasoning_init();
    assert(rc == 0);

    KolibriREConfig *config = kolibri_reasoning_get_config();
    assert(config != NULL);
    assert(config->enable_deductive == 1);

    printf(GREEN "[OK]" RESET " kolibri_reasoning_init()\n");
}

static void test_basic_query(void) {
    separator("Test Basic Query");
    char answer[1024];

    int rc = kolibri_reasoning_query("Все люди смертны. Сократ человек. Следовательно?", answer, sizeof(answer));
    printf("Query: Все люди смертны. Сократ человек. Следовательно?\n");
    printf("Answer: %s\n", answer);
    assert(rc == 0);
    assert(strlen(answer) > 0);

    printf(GREEN "[OK]" RESET " kolibri_reasoning_query()\n");
}

static void test_query_json(void) {
    separator("Test JSON Query");
    char json_out[4096];

    int rc = kolibri_reasoning_query_json("Если идет дождь, то асфальт мокрый. Дождь идет. Вывод?", json_out, sizeof(json_out));
    printf("JSON Output:\n%s\n", json_out);
    assert(rc == 0);
    assert(strstr(json_out, "chain") != NULL);
    assert(strstr(json_out, "status") != NULL);
    assert(strstr(json_out, "answer") != NULL);

    printf(GREEN "[OK]" RESET " kolibri_reasoning_query_json()\n");
}

static void test_counterfactual(void) {
    separator("Test Counterfactual");
    char answer[1024];

    int rc = kolibri_reasoning_counterfactual("Яблоко падает вниз.", "гравитация работает вверх", answer, sizeof(answer));
    printf("Answer: %s\n", answer);
    assert(rc == 0);
    assert(strlen(answer) > 0);

    printf(GREEN "[OK]" RESET " kolibri_reasoning_counterfactual()\n");
}

static void test_abductive(void) {
    separator("Test Abductive");
    char answer[1024];

    int rc = kolibri_reasoning_abductive("Утром трава мокрая, но дождя не было.", answer, sizeof(answer));
    printf("Answer: %s\n", answer);
    assert(rc == 0);
    assert(strlen(answer) > 0);

    printf(GREEN "[OK]" RESET " kolibri_reasoning_abductive()\n");
}

int main(void) {
    printf("Запуск тестов фасада Reasoning...\n");

    test_initialization();
    test_basic_query();
    test_query_json();
    test_counterfactual();
    test_abductive();

    printf("\n" GREEN "Все тесты успешно пройдены!" RESET "\n");
    return 0;
}

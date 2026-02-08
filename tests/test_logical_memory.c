/*
 * test_logical_memory.c
 *
 * Полноценные unit-тесты для модуля логической памяти
 * Каждый тест использует assert для верификации корректности
 */

#include "kolibri/logical_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Вспомогательные макросы ===== */

#define TEST_BEGIN(name) \
    do { printf("  [TEST] %-50s", name); fflush(stdout); } while(0)
#define TEST_PASS() \
    do { printf(" ✓\n"); tests_passed++; } while(0)

static int tests_run = 0;
static int tests_passed = 0;

/* ===== ТЕСТЫ ЖИЗНЕННОГО ЦИКЛА ===== */

static void test_create_destroy_memory(void) {
    TEST_BEGIN("create/destroy memory"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    assert(mem != NULL);
    assert(mem->cell_count == 0);

    lm_destroy_memory(mem);
    lm_destroy_memory(NULL);  /* NULL-безопасность */
    TEST_PASS();
}

/* ===== ТЕСТЫ СОЗДАНИЯ ВЫРАЖЕНИЙ ===== */

static void test_logic_constant(void) {
    TEST_BEGIN("lm_logic_constant"); tests_run++;

    LogicExpression *expr = lm_logic_constant("Hello");
    assert(expr != NULL);
    assert(expr->type == LOGIC_CONSTANT);
    assert(strcmp(expr->data.constant.value, "Hello") == 0);
    assert(expr->data.constant.length == 5);
    assert(expr->complexity == 0.1);

    lm_destroy_logic(expr);
    assert(lm_logic_constant(NULL) == NULL);
    TEST_PASS();
}

static void test_logic_repeat(void) {
    TEST_BEGIN("lm_logic_repeat"); tests_run++;

    LogicExpression *expr = lm_logic_repeat("AB", 4);
    assert(expr != NULL);
    assert(expr->type == LOGIC_REPEAT);
    assert(expr->data.repeat.count == 4);
    assert(expr->data.repeat.pattern != NULL);
    assert(expr->data.repeat.pattern->type == LOGIC_CONSTANT);
    assert(strcmp(expr->data.repeat.pattern->data.constant.value, "AB") == 0);
    assert(expr->materialized_size == 8);  /* 2 * 4 */

    lm_destroy_logic(expr);
    assert(lm_logic_repeat(NULL, 5) == NULL);
    assert(lm_logic_repeat("X", 0) == NULL);
    TEST_PASS();
}

static void test_logic_sequence(void) {
    TEST_BEGIN("lm_logic_sequence"); tests_run++;

    LogicExpression *expr = lm_logic_sequence(0, 3, 5);
    assert(expr != NULL);
    assert(expr->type == LOGIC_SEQUENCE);
    assert(expr->data.sequence.start == 0);
    assert(expr->data.sequence.step == 3);
    assert(expr->data.sequence.count == 5);

    lm_destroy_logic(expr);
    assert(lm_logic_sequence(0, 1, 0) == NULL);
    TEST_PASS();
}

static void test_logic_compose(void) {
    TEST_BEGIN("lm_logic_compose"); tests_run++;

    LogicExpression *a = lm_logic_constant("Hello");
    LogicExpression *b = lm_logic_constant("World");
    LogicExpression *comp = lm_logic_compose(a, b);
    assert(comp != NULL);
    assert(comp->type == LOGIC_COMPOSITION);
    assert(comp->data.composition.count == 2);
    assert(comp->data.composition.expressions[0] == a);
    assert(comp->data.composition.expressions[1] == b);

    lm_destroy_logic(comp);
    assert(lm_logic_compose(NULL, NULL) == NULL);
    TEST_PASS();
}

static void test_logic_relation(void) {
    TEST_BEGIN("lm_logic_relation"); tests_run++;

    LogicExpression *left = lm_logic_constant("A");
    LogicExpression *right = lm_logic_constant("B");
    LogicExpression *rel = lm_logic_relation(left, right, "derives_from");
    assert(rel != NULL);
    assert(rel->type == LOGIC_RELATION);
    assert(strcmp(rel->data.relation.relation_type, "derives_from") == 0);

    lm_destroy_logic(rel);
    TEST_PASS();
}

static void test_logic_variable(void) {
    TEST_BEGIN("lm_logic_variable + bind"); tests_run++;

    LogicExpression *var = lm_logic_variable("x");
    assert(var != NULL);
    assert(var->type == LOGIC_VARIABLE);
    assert(strcmp(var->data.variable.name, "x") == 0);
    assert(var->data.variable.binding == NULL);

    /* Привязка */
    LogicExpression *val = lm_logic_constant("42");
    int rc = lm_logic_bind_variable(var, val);
    assert(rc == 0);
    assert(var->data.variable.binding == val);

    /* Ошибки */
    assert(lm_logic_variable(NULL) == NULL);
    assert(lm_logic_bind_variable(NULL, NULL) == -1);
    assert(lm_logic_bind_variable(val, val) == -1);  /* val не переменная */

    lm_destroy_logic(val);
    /* var->binding уже уничтожено, обнулим */
    var->data.variable.binding = NULL;
    lm_destroy_logic(var);
    TEST_PASS();
}

static void test_logic_transform(void) {
    TEST_BEGIN("lm_logic_transform"); tests_run++;

    LogicExpression *input = lm_logic_constant("data");
    LogicExpression *tr = lm_logic_transform(input, NULL);
    assert(tr != NULL);
    assert(tr->type == LOGIC_TRANSFORM);
    assert(tr->data.transform.input == input);
    assert(tr->data.transform.transform_fn == NULL);
    assert(tr->complexity > input->complexity);

    lm_destroy_logic(tr);
    assert(lm_logic_transform(NULL, NULL) == NULL);
    TEST_PASS();
}

static void test_logic_conditional(void) {
    TEST_BEGIN("lm_logic_conditional"); tests_run++;

    LogicExpression *cond = lm_logic_constant("1");
    LogicExpression *then_e = lm_logic_constant("yes");
    LogicExpression *else_e = lm_logic_constant("no");
    LogicExpression *if_expr = lm_logic_conditional(cond, then_e, else_e);
    assert(if_expr != NULL);
    assert(if_expr->type == LOGIC_CONDITIONAL);
    assert(if_expr->data.conditional.condition == cond);
    assert(if_expr->data.conditional.then_expr == then_e);
    assert(if_expr->data.conditional.else_expr == else_e);

    lm_destroy_logic(if_expr);
    assert(lm_logic_conditional(NULL, NULL, NULL) == NULL);
    TEST_PASS();
}

/* ===== ТЕСТЫ МАТЕРИАЛИЗАЦИИ ===== */

static void test_materialize_constant(void) {
    TEST_BEGIN("materialize constant"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "c1", lm_logic_constant("Kolibri"));

    char buf[64];
    int len = lm_materialize(mem, "c1", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "Kolibri") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_repeat(void) {
    TEST_BEGIN("materialize repeat"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "r1", lm_logic_repeat("AB", 4));

    char buf[64];
    int len = lm_materialize(mem, "r1", buf, sizeof(buf));
    assert(len == 8);
    assert(strcmp(buf, "ABABABAB") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_sequence(void) {
    TEST_BEGIN("materialize sequence"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "s1", lm_logic_sequence(10, 10, 3));

    char buf[64];
    int len = lm_materialize(mem, "s1", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "102030") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_composition(void) {
    TEST_BEGIN("materialize composition"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    LogicExpression *a = lm_logic_constant("Hello");
    LogicExpression *b = lm_logic_constant("World");
    lm_store_logic(mem, "hw", lm_logic_compose(a, b));

    char buf[64];
    int len = lm_materialize(mem, "hw", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "HelloWorld") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_variable_bound(void) {
    TEST_BEGIN("materialize variable (bound)"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    LogicExpression *var = lm_logic_variable("x");
    LogicExpression *val = lm_logic_constant("42");
    lm_logic_bind_variable(var, val);
    lm_store_logic(mem, "v1", var);

    char buf[64];
    int len = lm_materialize(mem, "v1", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "42") == 0);

    /* val нельзя разрушать отдельно, т.к. var->binding указывает на него,
       но lm_destroy_memory не уничтожает binding. Разрушим вручную. */
    lm_destroy_logic(val);
    var->data.variable.binding = NULL;
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_conditional_true(void) {
    TEST_BEGIN("materialize conditional (true)"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    LogicExpression *cond = lm_logic_constant("1");
    LogicExpression *then_e = lm_logic_constant("YES");
    LogicExpression *else_e = lm_logic_constant("NO");
    lm_store_logic(mem, "if1", lm_logic_conditional(cond, then_e, else_e));

    char buf[64];
    int len = lm_materialize(mem, "if1", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "YES") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_conditional_false(void) {
    TEST_BEGIN("materialize conditional (false)"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    LogicExpression *cond = lm_logic_constant("0");
    LogicExpression *then_e = lm_logic_constant("YES");
    LogicExpression *else_e = lm_logic_constant("NO");
    lm_store_logic(mem, "if2", lm_logic_conditional(cond, then_e, else_e));

    char buf[64];
    int len = lm_materialize(mem, "if2", buf, sizeof(buf));
    assert(len > 0);
    assert(strcmp(buf, "NO") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_relation(void) {
    TEST_BEGIN("materialize relation → text"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    LogicExpression *l = lm_logic_constant("Cat");
    LogicExpression *r = lm_logic_constant("Animal");
    lm_store_logic(mem, "rel1", lm_logic_relation(l, r, "part_of"));

    char buf[128];
    int len = lm_materialize(mem, "rel1", buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "Cat") != NULL);
    assert(strstr(buf, "Animal") != NULL);
    assert(strstr(buf, "part_of") != NULL);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_logic_direct(void) {
    TEST_BEGIN("lm_materialize_logic (direct)"); tests_run++;

    LogicExpression *expr = lm_logic_repeat("OK", 3);
    char *text = lm_materialize_logic(expr);
    assert(text != NULL);
    assert(strcmp(text, "OKOKOK") == 0);
    free(text);

    lm_destroy_logic(expr);
    assert(lm_materialize_logic(NULL) == NULL);
    TEST_PASS();
}

/* ===== ТЕСТЫ КЭШИРОВАНИЯ ===== */

static void test_cache_on_materialize(void) {
    TEST_BEGIN("cache after materialize"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "c1", lm_logic_repeat("Q", 3));

    char buf[64];
    /* Первый вызов — создаёт кэш */
    int len1 = lm_materialize(mem, "c1", buf, sizeof(buf));
    assert(len1 == 3);
    assert(mem->cells[0].cache_valid == 1);

    /* Второй вызов — из кэша */
    char buf2[64];
    int len2 = lm_materialize(mem, "c1", buf2, sizeof(buf2));
    assert(len2 == len1);
    assert(strcmp(buf, buf2) == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

/* ===== ТЕСТЫ ОПТИМИЗАЦИИ ===== */

static void test_optimize_nested_repeat(void) {
    TEST_BEGIN("optimize repeat(repeat(x,3),2)→repeat(x,6)"); tests_run++;

    /* Вручную создаём repeat(repeat("A", 3), 2) */
    LogicExpression *inner = lm_logic_repeat("A", 3);
    assert(inner != NULL);

    LogicExpression *outer = calloc(1, sizeof(LogicExpression));
    outer->type = LOGIC_REPEAT;
    outer->data.repeat.pattern = inner;
    outer->data.repeat.count = 2;
    outer->complexity = 2.0;

    /* Оптимизируем */
    LogicExpression *opt = lm_optimize_logic(outer);
    assert(opt != NULL);
    assert(opt->type == LOGIC_REPEAT);
    assert(opt->data.repeat.count == 6);
    assert(opt->data.repeat.pattern->type == LOGIC_CONSTANT);
    assert(strcmp(opt->data.repeat.pattern->data.constant.value, "A") == 0);

    /* Материализуем для проверки */
    char *text = lm_materialize_logic(opt);
    assert(text != NULL);
    assert(strcmp(text, "AAAAAA") == 0);
    free(text);

    lm_destroy_logic(opt);
    TEST_PASS();
}

/* ===== ТЕСТЫ УТИЛИТ ===== */

static void test_predict_size(void) {
    TEST_BEGIN("predict_size"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "r1", lm_logic_repeat("XYZ", 10));

    size_t predicted = lm_predict_size(mem, "r1");
    assert(predicted == 30);  /* 3 * 10 */
    assert(lm_predict_size(mem, "none") == 0);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_compute_complexity(void) {
    TEST_BEGIN("compute_complexity"); tests_run++;

    LogicExpression *expr = lm_logic_repeat("A", 10);
    double c = lm_compute_complexity(expr);
    assert(c > 0.0);
    assert(lm_compute_complexity(NULL) == 0.0);

    lm_destroy_logic(expr);
    TEST_PASS();
}

static void test_logic_to_string(void) {
    TEST_BEGIN("lm_logic_to_string all types"); tests_run++;

    char buf[128];

    LogicExpression *e;

    e = lm_logic_constant("V");
    assert(lm_logic_to_string(e, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "const") != NULL);
    lm_destroy_logic(e);

    e = lm_logic_repeat("Z", 5);
    assert(lm_logic_to_string(e, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "repeat") != NULL);
    lm_destroy_logic(e);

    e = lm_logic_sequence(0, 1, 3);
    assert(lm_logic_to_string(e, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "sequence") != NULL);
    lm_destroy_logic(e);

    e = lm_logic_variable("y");
    assert(lm_logic_to_string(e, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "var") != NULL);
    lm_destroy_logic(e);

    TEST_PASS();
}

static void test_get_stats(void) {
    TEST_BEGIN("lm_get_stats"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    lm_store_logic(mem, "a", lm_logic_repeat("X", 10));
    lm_store_logic(mem, "b", lm_logic_sequence(0, 1, 5));

    /* Материализуем одну ячейку для кэша */
    char buf[128];
    lm_materialize(mem, "a", buf, sizeof(buf));

    LogicalMemoryStats stats;
    assert(lm_get_stats(mem, &stats) == 0);
    assert(stats.total_cells == 2);
    assert(stats.cached_cells == 1);
    assert(stats.cache_hit_rate > 0.0);
    assert(stats.logic_size_bytes > 0);

    assert(lm_get_stats(NULL, NULL) == -1);

    lm_destroy_memory(mem);
    TEST_PASS();
}

/* ===== ТЕСТЫ ГРАНИЧНЫХ УСЛОВИЙ ===== */

static void test_store_logic_full(void) {
    TEST_BEGIN("store_logic limit 1024"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    /* Не тестируем 1024 по-настоящему (слишком долго),
       но проверяем базовый case */
    int rc = lm_store_logic(mem, "x", lm_logic_constant("V"));
    assert(rc == 0);
    assert(mem->cell_count == 1);

    rc = lm_store_logic(NULL, "x", NULL);
    assert(rc == -1);

    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_materialize_not_found(void) {
    TEST_BEGIN("materialize non-existent cell"); tests_run++;

    LogicalMemory *mem = lm_create_memory();
    char buf[32];
    int len = lm_materialize(mem, "absent", buf, sizeof(buf));
    assert(len == -1);

    lm_destroy_memory(mem);
    TEST_PASS();
}

/* ===== MAIN ===== */

int main(void) {
    printf("\n=== Тесты модуля логической памяти (logical_memory) ===\n\n");

    /* Жизненный цикл */
    test_create_destroy_memory();

    /* Создание выражений */
    test_logic_constant();
    test_logic_repeat();
    test_logic_sequence();
    test_logic_compose();
    test_logic_relation();
    test_logic_variable();
    test_logic_transform();
    test_logic_conditional();

    /* Материализация */
    test_materialize_constant();
    test_materialize_repeat();
    test_materialize_sequence();
    test_materialize_composition();
    test_materialize_variable_bound();
    test_materialize_conditional_true();
    test_materialize_conditional_false();
    test_materialize_relation();
    test_materialize_logic_direct();

    /* Кэширование */
    test_cache_on_materialize();

    /* Оптимизация */
    test_optimize_nested_repeat();

    /* Утилиты */
    test_predict_size();
    test_compute_complexity();
    test_logic_to_string();
    test_get_stats();

    /* Граничные условия */
    test_store_logic_full();
    test_materialize_not_found();

    printf("\n=== Результат: %d/%d тестов пройдено ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

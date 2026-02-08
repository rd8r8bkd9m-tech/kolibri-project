/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Тесты модуля трассировки "Стеклянный Разум"
 */

#include "kolibri/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* --- Тест создания и завершения сессии --- */
static int test_session_lifecycle(void) {
    printf("  [1] Тест жизненного цикла сессии... ");
    
    KolibriTraceSession session;
    
    /* Создание сессии */
    int result = kt_session_begin(&session);
    assert(result == 0);
    assert(session.active == 1);
    assert(session.graph.node_count == 0);
    assert(session.graph.edge_count == 0);
    
    /* Завершение сессии */
    result = kt_session_end(&session);
    assert(result == 0);
    assert(session.active == 0);
    
    /* Освобождение */
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест трассировки цифр --- */
static int test_trace_digits(void) {
    printf("  [2] Тест трассировки цифр... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Трассируем несколько цифр */
    uint32_t node1 = kt_trace_digit(&session, 3, 0, "input_stream");
    uint32_t node2 = kt_trace_digit(&session, 1, 1, "input_stream");
    uint32_t node3 = kt_trace_digit(&session, 4, 2, "input_stream");
    
    assert(node1 != 0);
    assert(node2 != 0);
    assert(node3 != 0);
    assert(node1 != node2);
    assert(node2 != node3);
    
    /* Проверяем количество узлов */
    size_t node_count, edge_count;
    uint32_t max_depth;
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 3);
    
    /* Проверяем данные узла */
    const KolibriTraceNode *node = kt_get_node(&session, node1);
    assert(node != NULL);
    assert(node->digit_value == 3);
    assert(node->digit_position == 0);
    assert(strcmp(node->source, "input_stream") == 0);
    
    kt_session_end(&session);
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест трассировки формул --- */
static int test_trace_formula(void) {
    printf("  [3] Тест трассировки формул... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём входные узлы */
    uint32_t inputs[3];
    inputs[0] = kt_trace_digit(&session, 2, 0, "test");
    inputs[1] = kt_trace_digit(&session, 7, 1, "test");
    inputs[2] = kt_trace_digit(&session, 1, 2, "test");
    
    /* Трассируем применение формулы */
    uint32_t formula_node = kt_trace_formula(&session, 42, 271, 314, inputs, 3);
    assert(formula_node != 0);
    
    /* Проверяем создание рёбер */
    size_t node_count, edge_count;
    uint32_t max_depth;
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 4);  /* 3 входа + 1 формула */
    assert(edge_count == 3);  /* 3 ребра от входов */
    
    /* Проверяем узел формулы */
    const KolibriTraceNode *node = kt_get_node(&session, formula_node);
    assert(node != NULL);
    assert(node->type == KOLIBRI_TRACE_NODE_FORMULA);
    assert(node->formula_id == 42);
    
    kt_session_end(&session);
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест трассировки мутаций --- */
static int test_trace_mutation(void) {
    printf("  [4] Тест трассировки мутаций... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём исходный узел */
    uint32_t source = kt_trace_digit(&session, 5, 0, "gene");
    
    /* Трассируем мутацию */
    uint32_t mutated = kt_trace_mutation(&session, 0, 0, 5, 8, source);
    assert(mutated != 0);
    
    /* Проверяем связь */
    size_t node_count, edge_count;
    uint32_t max_depth;
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 2);
    assert(edge_count == 1);
    
    /* Проверяем данные мутации */
    const KolibriTraceNode *node = kt_get_node(&session, mutated);
    assert(node != NULL);
    assert(node->type == KOLIBRI_TRACE_NODE_MUTATION);
    assert(node->digit_value == 8);  /* новое значение */
    
    kt_session_end(&session);
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест построения графа --- */
static int test_graph_build(void) {
    printf("  [5] Тест построения графа... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём цепочку: input -> formula -> transform -> output */
    uint32_t input1 = kt_trace_digit(&session, 1, 0, "source");
    uint32_t input2 = kt_trace_digit(&session, 2, 1, "source");
    
    uint32_t inputs[] = {input1, input2};
    uint32_t formula = kt_trace_formula(&session, 1, 12, 24, inputs, 2);
    
    uint32_t trans_inputs[] = {formula};
    uint32_t transform = kt_trace_transform(&session, "normalize", trans_inputs, 1);
    
    /* Помечаем как выход */
    kt_mark_output(&session, transform);
    
    /* Завершаем и строим граф */
    kt_session_end(&session);
    
    /* Проверяем структуру */
    size_t node_count, edge_count;
    uint32_t max_depth;
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 4);
    assert(edge_count == 3);
    assert(max_depth > 0);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест экспорта в JSON --- */
static int test_json_export(void) {
    printf("  [6] Тест экспорта в JSON... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    kt_trace_digit(&session, 3, 0, "test");
    kt_trace_digit(&session, 1, 1, "test");
    kt_trace_digit(&session, 4, 2, "test");
    
    kt_session_end(&session);
    
    /* Экспорт в JSON */
    char buffer[8192];
    size_t written;
    int result = kt_graph_to_json(&session, buffer, sizeof(buffer), &written);
    assert(result == 0);
    assert(written > 0);
    
    /* Проверяем базовую структуру JSON */
    assert(strstr(buffer, "\"nodes\"") != NULL);
    assert(strstr(buffer, "\"edges\"") != NULL);
    assert(strstr(buffer, "\"session\"") != NULL);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест тепловой карты --- */
static int test_heatmap(void) {
    printf("  [7] Тест тепловой карты... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём несколько узлов с разными весами */
    kt_trace_digit(&session, 1, 0, "source");
    kt_trace_digit(&session, 2, 1, "source");
    kt_trace_digit(&session, 3, 2, "source");
    
    kt_session_end(&session);
    
    /* Генерируем тепловую карту */
    KolibriTraceHeatmap heatmap;
    int result = kt_graph_heatmap(&session, &heatmap);
    assert(result == 0);
    assert(heatmap.length == 3);
    assert(heatmap.digits[0] == 1);
    assert(heatmap.digits[1] == 2);
    assert(heatmap.digits[2] == 3);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест экспорта в десятичный формат --- */
static int test_decimal_export(void) {
    printf("  [8] Тест экспорта в десятичный формат... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    kt_trace_digit(&session, 5, 0, "test");
    kt_trace_digit(&session, 6, 1, "test");
    
    kt_session_end(&session);
    
    /* Экспорт в десятичный формат */
    char buffer[4096];
    size_t written;
    int result = kt_export_decimal(&session, buffer, sizeof(buffer), &written);
    assert(result == 0);
    assert(written > 0);
    
    /* Проверяем, что все символы - цифры */
    for (size_t i = 0; i < written; i++) {
        assert(buffer[i] >= '0' && buffer[i] <= '9');
    }
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест экспорта в DOT --- */
static int test_dot_export(void) {
    printf("  [9] Тест экспорта в DOT (Graphviz)... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    uint32_t n1 = kt_trace_digit(&session, 1, 0, "test");
    uint32_t n2 = kt_trace_digit(&session, 2, 1, "test");
    uint32_t inputs[] = {n1, n2};
    kt_trace_formula(&session, 1, 12, 3, inputs, 2);
    
    kt_session_end(&session);
    
    /* Экспорт в DOT */
    char buffer[8192];
    size_t written;
    int result = kt_graph_to_dot(&session, buffer, sizeof(buffer), &written);
    assert(result == 0);
    assert(written > 0);
    
    /* Проверяем структуру DOT */
    assert(strstr(buffer, "digraph") != NULL);
    assert(strstr(buffer, "->") != NULL);
    assert(strstr(buffer, "fillcolor") != NULL);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест поиска пути --- */
static int test_find_path(void) {
    printf("  [10] Тест поиска пути... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём цепочку узлов */
    uint32_t n1 = kt_trace_digit(&session, 1, 0, "test");
    uint32_t inputs1[] = {n1};
    uint32_t n2 = kt_trace_transform(&session, "op1", inputs1, 1);
    uint32_t inputs2[] = {n2};
    uint32_t n3 = kt_trace_transform(&session, "op2", inputs2, 1);
    
    kt_session_end(&session);
    
    /* Ищем путь от n1 к n3 */
    uint32_t path[10];
    size_t path_length;
    int result = kt_graph_find_path(&session.graph, n1, n3, path, 10, &path_length);
    assert(result == 0);
    assert(path_length == 3);
    assert(path[0] == n1);
    assert(path[1] == n2);
    assert(path[2] == n3);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест поиска предков --- */
static int test_find_ancestors(void) {
    printf("  [11] Тест поиска предков... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Создаём дерево */
    uint32_t n1 = kt_trace_digit(&session, 1, 0, "test");
    uint32_t n2 = kt_trace_digit(&session, 2, 1, "test");
    uint32_t inputs[] = {n1, n2};
    uint32_t n3 = kt_trace_formula(&session, 1, 12, 3, inputs, 2);
    
    kt_session_end(&session);
    
    /* Ищем предков n3 */
    uint32_t ancestors[10];
    size_t count;
    int result = kt_graph_find_ancestors(&session.graph, n3, ancestors, 10, &count);
    assert(result == 0);
    assert(count == 2);  /* n1 и n2 */
    
    /* Проверяем, что оба предка найдены */
    int found_n1 = 0, found_n2 = 0;
    for (size_t i = 0; i < count; i++) {
        if (ancestors[i] == n1) found_n1 = 1;
        if (ancestors[i] == n2) found_n2 = 1;
    }
    assert(found_n1 && found_n2);
    
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест названий типов --- */
static int test_type_names(void) {
    printf("  [12] Тест названий типов... ");
    
    assert(strcmp(kt_node_type_name(KOLIBRI_TRACE_NODE_INPUT), "input") == 0);
    assert(strcmp(kt_node_type_name(KOLIBRI_TRACE_NODE_DIGIT), "digit") == 0);
    assert(strcmp(kt_node_type_name(KOLIBRI_TRACE_NODE_FORMULA), "formula") == 0);
    assert(strcmp(kt_node_type_name(KOLIBRI_TRACE_NODE_MUTATION), "mutation") == 0);
    assert(strcmp(kt_node_type_name(KOLIBRI_TRACE_NODE_OUTPUT), "output") == 0);
    
    assert(strcmp(kt_edge_type_name(KOLIBRI_TRACE_EDGE_FLOW), "flow") == 0);
    assert(strcmp(kt_edge_type_name(KOLIBRI_TRACE_EDGE_MUTATION), "mutation") == 0);
    
    /* Проверка неизвестных типов */
    assert(strcmp(kt_node_type_name((KolibriTraceNodeType)999), "unknown") == 0);
    assert(strcmp(kt_edge_type_name((KolibriTraceEdgeType)999), "unknown") == 0);
    
    printf("OK\n");
    return 0;
}

/* --- Тест сброса сессии --- */
static int test_session_reset(void) {
    printf("  [13] Тест сброса сессии... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Добавляем данные */
    kt_trace_digit(&session, 1, 0, "test");
    kt_trace_digit(&session, 2, 1, "test");
    
    size_t node_count, edge_count;
    uint32_t max_depth;
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 2);
    
    /* Сбрасываем */
    kt_session_reset(&session);
    
    /* Проверяем очистку */
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 0);
    assert(session.active == 1);
    
    /* Можно продолжать работу */
    kt_trace_digit(&session, 3, 0, "new");
    kt_get_stats(&session, &node_count, &edge_count, &max_depth);
    assert(node_count == 1);
    
    kt_session_end(&session);
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Тест конфигурации --- */
static int test_configure(void) {
    printf("  [14] Тест конфигурации трассировки... ");
    
    KolibriTraceSession session;
    kt_session_begin(&session);
    
    /* Отключаем трассировку цифр */
    kt_session_configure(&session, 0, 1, 1);
    
    uint32_t node = kt_trace_digit(&session, 1, 0, "test");
    assert(node == 0);  /* Должен вернуть 0, т.к. отключено */
    
    /* Включаем обратно */
    kt_session_configure(&session, 1, 1, 1);
    
    node = kt_trace_digit(&session, 1, 0, "test");
    assert(node != 0);  /* Теперь должен создать */
    
    kt_session_end(&session);
    kt_session_free(&session);
    
    printf("OK\n");
    return 0;
}

/* --- Главная функция --- */
int main(void) {
    printf("\n=== Тесты модуля трассировки \"Стеклянный Разум\" ===\n\n");
    
    int failures = 0;
    
    failures += test_session_lifecycle();
    failures += test_trace_digits();
    failures += test_trace_formula();
    failures += test_trace_mutation();
    failures += test_graph_build();
    failures += test_json_export();
    failures += test_heatmap();
    failures += test_decimal_export();
    failures += test_dot_export();
    failures += test_find_path();
    failures += test_find_ancestors();
    failures += test_type_names();
    failures += test_session_reset();
    failures += test_configure();
    
    printf("\n");
    if (failures == 0) {
        printf("✅ Все тесты пройдены успешно!\n\n");
        return 0;
    } else {
        printf("❌ Провалено тестов: %d\n\n", failures);
        return 1;
    }
}
